// SPDX-License-Identifier: GPL-2.0
/* Copyright 2021 Synaptics Incorporated */


#include <linux/module.h>
#include <linux/delay.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <sound/soc.h>

#include "berlin_pcm.h"
#include "berlin_capture.h"
#include "berlin_util.h"
#include "berlin_spdif.h"
#include "aio_hal.h"
#include "avio_common.h"
#include "hal_vpp_wrap.h"
#include "vpp_cmd.h"
#include "mic.h"
#include "hdmi_in_if.h"

#define HDMI_CAPTURE_RATES   (SNDRV_PCM_RATE_48000)
#define HDMI_CAPTURE_FORMATS (SNDRV_PCM_FMTBIT_S16_LE \
				| SNDRV_PCM_FMTBIT_S32_LE) \

struct hdmi_priv {
	struct device *dev;
	const char *dev_name;
	unsigned int irq;
	u32 chid;
	bool requested;
	void *aio_handle;
	u32 channels;
	u32 chan_mask;
	u32 sample_rate;
	u32 frame_rate;/* frame is not same as sample_rate when hbr */
	u32 hbr;
	u32 bit_depth;
	u32 i2s_dfm;
	u32 i2s_cfm;
	u32 fmt;
	u32 format;
	u8  ch_map[8];
	struct workqueue_struct *wq;
	struct delayed_work trigger_work;
	struct snd_pcm_substream *ss;
	int ss_state;
	AIP_FRAME_DES *pFrameDesc;
	ENUM_HRX_STATUS hrx_status;
};

static const struct alsa_aip_ops *a2a_ops;

typedef enum{
	RAW_PCM,
} eAudioType;

typedef enum VPP_HDMI_AUDIO_FMT_T {
	VPP_HDMI_AUDIO_FMT_UNDEF = 0x00,
	VPP_HDMI_AUDIO_FMT_PCM   = 0x01,
} VPP_HDMI_AUDIO_FMT;

int aip_alsa_set_ops(const struct alsa_aip_ops *ptr)
{
	a2a_ops = ptr;

	return 0;
}
EXPORT_SYMBOL_GPL(aip_alsa_set_ops);

static u32 GET_PEAudioType2HDMIType(u32 audioFmt, int *DecType)
{
	switch (audioFmt) {
	case RAW_PCM:
		*DecType = VPP_HDMI_AUDIO_FMT_PCM;
		break;
	default:
		*DecType = VPP_HDMI_AUDIO_FMT_UNDEF;
		snd_printk("not support audio fmt %d\n", audioFmt);
		return -EINVAL;
	}
	return 0;
}

static struct snd_kcontrol_new berlin_indai_ctrls[] = {
	//TODO: add dai control here
};

static int berlin_indai_setfmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	return 0;
}


static int berlin_indai_startup(struct snd_pcm_substream *substream,
				 struct snd_soc_dai *dai)
{
	return 0;
}

static void berlin_indai_shutdown(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	return;
}

static int berlin_indai_aip_free(void *pFrame)
{
	return 0;
}

static int berlin_aip_event_callback_format_change(void)
{
	return 0;
}
static int berlin_aip_event_callback_get_timestamp(void)
{
	return 0;
}
static int berlin_indai_aip_event(int eventID, struct snd_pcm_substream *ss)
{
	int r = 0;

	switch (eventID) {
	case AIP_EVENT_FRAME_READY:
		r = berlin_pcm_aip_event_callback_newframe(ss);
		break;
	case AIP_EVENT_FORMAT_CHANGE:
		r = berlin_aip_event_callback_format_change();
		break;
	case AIP_EVENT_GET_TIMESTAMP:
		r = berlin_aip_event_callback_get_timestamp();
		break;
	case AIP_EVENT_MUTE:
		berlin_pcm_set_mic_mute_state(ss, 1);
		break;
	case AIP_EVENT_UNMUTE:
		berlin_pcm_set_mic_mute_state(ss, 0);
		break;
	case AIP_EVENT_INVALID:
	case AIP_EVENT_SIGNAL_STABLE:
	case AIP_EVENT_SIGNAL_UNSTABLE:
	case AIP_EVENT_OVERFLOW:
	default:
		break;
	}
	return r;
}

static int berlin_indai_aip_alloc_cb(void **pFrame, int *nSize, void *pContextParam)
{
	struct hdmi_priv *hdmi = (struct hdmi_priv *)pContextParam;
	struct snd_pcm_substream *ss = (struct snd_pcm_substream *)hdmi->ss;
	void *pBuffer;

	berlin_pcm_indai_aip_alloc(ss, (void **)&pBuffer, nSize);

	hdmi->pFrameDesc->pBuffer = pBuffer;

	*pFrame = (AIP_FRAME_DES *)hdmi->pFrameDesc;

	return 0;
}

static int berlin_indai_aip_free_cb(void *pFrame, void *pContextParam)
{
	berlin_indai_aip_free(pFrame);

	return 0;
}

static int berlin_indai_aip_event_cb(int eventID, void *pEventInfo, void *pContextParam)
{
	struct hdmi_priv *hdmi = (struct hdmi_priv *)pContextParam;
	struct snd_pcm_substream *ss = (struct snd_pcm_substream *)hdmi->ss;

	berlin_indai_aip_event(eventID, ss);

	return 0;
}

static int berlin_indai_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params,
		struct snd_soc_dai *dai)
{
	struct hdmi_priv *hdmi = snd_soc_dai_get_drvdata(dai);
	u32 fs = params_rate(params);
	int ret = 0;
	u32 dfm;
	struct berlin_ss_params ssparams;
	AIP_ALSA_CMD_DATA a2a_data;

	hdmi->channels = params_channels(params);

	dfm = berlin_get_sample_resolution(params_width(params));

	ssparams.irq_num = 1;
	ssparams.chid_num = 1;
	ssparams.mode = HDMII_MODE;
	ssparams.irq = &hdmi->irq;
	ssparams.dev_name = hdmi->dev_name;
	ssparams.enable_mic_mute = true;
	ssparams.interleaved = false;
	ssparams.dummy_data = false;
	ssparams.multi_lanes = false;

	berlin_pcm_set_ch_mode(substream, ssparams.chid_num, 0,
			ssparams.mode,
			ssparams.enable_mic_mute,
			ssparams.interleaved,
			ssparams.dummy_data,
			ssparams.multi_lanes,
			0,
			0,
			hdmi->aio_handle);

	hdmi->channels = params_channels(params);

	snd_printd("%s:%s:sample_rate:%d channels:%d format:%s, %d\n", __func__,
			hdmi->dev_name, params_rate(params), params_channels(params),
			snd_pcm_format_name(params_format(params)), params_format(params));

	hdmi->sample_rate = fs;
	hdmi->bit_depth = params_width(params);
	hdmi->format = params_format(params);
	hdmi->frame_rate = fs;
	if (hdmi->channels == 1) {
		hdmi->ch_map[0] = SNDRV_CHMAP_MONO;
	} else if (hdmi->channels == 2) {
		hdmi->ch_map[0] = SNDRV_CHMAP_FL;
		hdmi->ch_map[1] = SNDRV_CHMAP_FR;
	} else if (hdmi->channels == 4) {
		hdmi->ch_map[0] = SNDRV_CHMAP_FL;
		hdmi->ch_map[1] = SNDRV_CHMAP_FR;
		hdmi->ch_map[2] = SNDRV_CHMAP_RL;
		hdmi->ch_map[3] = SNDRV_CHMAP_RR;
	} else if (hdmi->channels == 6) {
		hdmi->ch_map[0] = SNDRV_CHMAP_FL;
		hdmi->ch_map[1] = SNDRV_CHMAP_FR;
		hdmi->ch_map[2] = SNDRV_CHMAP_FC;
		hdmi->ch_map[3] = SNDRV_CHMAP_LFE;
		hdmi->ch_map[4] = SNDRV_CHMAP_RL;
		hdmi->ch_map[5] = SNDRV_CHMAP_RR;
	} else if (hdmi->channels == 8) {
		hdmi->ch_map[0] = SNDRV_CHMAP_FL;
		hdmi->ch_map[1] = SNDRV_CHMAP_FR;
		hdmi->ch_map[2] = SNDRV_CHMAP_FC;
		hdmi->ch_map[3] = SNDRV_CHMAP_LFE;
		hdmi->ch_map[4] = SNDRV_CHMAP_RL;
		hdmi->ch_map[5] = SNDRV_CHMAP_RR;
		hdmi->ch_map[6] = SNDRV_CHMAP_SL;
		hdmi->ch_map[7] = SNDRV_CHMAP_SR;
	}

	hdmi->fmt = RAW_PCM;
	hdmi->ss = substream;
	hdmi->pFrameDesc = (AIP_FRAME_DES *)kzalloc(sizeof(AIP_FRAME_DES), GFP_KERNEL);

	a2a_data.channels = hdmi->channels;
	a2a_data.sample_rate = fs;
	a2a_data.frame_rate = fs;/* frame is not same as sample_rate when hbr */
	a2a_data.hbr = 0;
	a2a_data.bit_depth = hdmi->bit_depth;
	GET_PEAudioType2HDMIType(hdmi->fmt, &a2a_data.dec_type);
	a2a_data.format = hdmi->format;
	a2a_data.allocFn = berlin_indai_aip_alloc_cb;
	a2a_data.freeFn = berlin_indai_aip_free_cb;
	a2a_data.eventFn = berlin_indai_aip_event_cb;
	a2a_data.hdmi = hdmi;
	a2a_data.period_bytes = params_period_bytes(params);

	if (a2a_ops) {
		a2a_ops->get_hrx_status(&hdmi->hrx_status);
		if (hdmi->hrx_status == HRX_STATUS_VIP_STABLE)
			a2a_ops->config(&a2a_data);
		else {
			snd_printk("syna hrx vip init not done\n");
			return -ENOTSUPP;
		}
	} else {
		snd_printk("syna hrx init not done\n");
		return -ENOTSUPP;
	}
	usleep_range(5000, 5100);

	return ret;
}

static int berlin_indai_hw_free(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	struct hdmi_priv *hdmi = snd_soc_dai_get_drvdata(dai);

	if (a2a_ops)
		a2a_ops->close();
	else {
		snd_printk("syna hrx init not done\n");
		return -ENOTSUPP;
	}

	kfree(hdmi->pFrameDesc);
	return 0;
}

static int berlin_indai_trigger(struct snd_pcm_substream *substream,
		int cmd, struct snd_soc_dai *dai)
{
	struct hdmi_priv *hdmi = snd_soc_dai_get_drvdata(dai);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if (a2a_ops) {
			a2a_ops->get_hrx_status(&hdmi->hrx_status);
			if (hdmi->hrx_status == HRX_STATUS_VIP_STABLE)
				a2a_ops->start();
			else {
				snd_printk("syna hrx vip init not done\n");
				return -ENOTSUPP;
			}
		} else {
			snd_printk("syna hrx init not done\n");
			return -ENOTSUPP;
		}
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		if (a2a_ops)
			a2a_ops->stop();
		else {
			snd_printk("syna hrx init not done\n");
			return -ENOTSUPP;
		}
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int berlin_indai_dai_probe(struct snd_soc_dai *dai)
{
	snd_soc_add_dai_controls(dai, berlin_indai_ctrls,
			ARRAY_SIZE(berlin_indai_ctrls));
	return 0;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(6, 12, 0))
static int berlin_indai_get_channel_map(struct snd_soc_dai *dai,
				   unsigned int *tx_num, unsigned int *tx_slot,
				   unsigned int *rx_num, unsigned int *rx_slot)
#else
static int berlin_indai_get_channel_map(const struct snd_soc_dai *dai,
				   unsigned int *tx_num, unsigned int *tx_slot,
				   unsigned int *rx_num, unsigned int *rx_slot)
#endif
{
	struct hdmi_priv *indai = snd_soc_dai_get_drvdata((struct snd_soc_dai *)dai);
	int i;

	*tx_num = indai->channels;

	for (i = 0; i < indai->channels; i++)
		tx_slot[i] = indai->ch_map[i];

	snd_printd("%s: rx_num(%u): %u %u %u %u %u %u %u %u \n", __func__, *tx_num,
		tx_slot[0], tx_slot[1], tx_slot[2], tx_slot[3],
		tx_slot[4], tx_slot[5], tx_slot[6], tx_slot[7]);

	return 0;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(6, 12, 0))
static int berlin_indai_set_channel_map(struct snd_soc_dai *dai,
				   unsigned int tx_num, unsigned int *tx_slot,
				   unsigned int rx_num, unsigned int *rx_slot)
#else
static int berlin_indai_set_channel_map(struct snd_soc_dai *dai,
				   unsigned int tx_num, const unsigned int *tx_slot,
				   unsigned int rx_num, const unsigned int *rx_slot)
#endif
{
	struct hdmi_priv *indai = snd_soc_dai_get_drvdata(dai);
	int i;

	for (i = 0; i < tx_num; i++)
		indai->ch_map[i] = tx_slot[i];

	snd_printd("%s: rx_num(%u): %u %u %u %u %u %u %u %u \n", __func__, tx_num,
		tx_slot[0], tx_slot[1], tx_slot[2], tx_slot[3],
		tx_slot[4], tx_slot[5], tx_slot[6], tx_slot[7]);

	return 0;
}

static struct snd_soc_dai_ops berlin_dai_indai_ops = {
	.startup   = berlin_indai_startup,
	.set_fmt   = berlin_indai_setfmt,
	.hw_params = berlin_indai_hw_params,
	.hw_free   = berlin_indai_hw_free,
	.trigger   = berlin_indai_trigger,
	.shutdown  = berlin_indai_shutdown,
	.set_channel_map = berlin_indai_set_channel_map,
	.get_channel_map = berlin_indai_get_channel_map,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0))
	.probe = berlin_indai_dai_probe,
#endif
};

static struct snd_soc_dai_driver berlin_indai_dai = {
	.name = "berlin-hdmi-indai",
#if (LINUX_VERSION_CODE < KERNEL_VERSION(6, 12, 0))
	.probe = berlin_indai_dai_probe,
#endif
	.capture = {
		.stream_name = "HdmiCapture",
		.channels_min = 2,
		.channels_max = 8,
		.rates = HDMI_CAPTURE_RATES,
		.formats = HDMI_CAPTURE_FORMATS,
	},
	.ops = &berlin_dai_indai_ops,
};

static const struct snd_soc_component_driver berlin_indai_component = {
	.name = "hdmi-indai",
};

static int hdmi_indai_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct hdmi_priv *indai;
	int ret;

	//Defer probe until dependent soc module/s are probed/initialized
	if (!is_avio_driver_initialized()) {
		snd_printk("%s: avio driver not initialized [%d]\n", __func__, __LINE__);
		return -EPROBE_DEFER;
	}

	indai = devm_kzalloc(dev, sizeof(struct hdmi_priv),
			GFP_KERNEL);
	if (!indai)
		return -ENOMEM;
	indai->dev_name = dev_name(dev);
	indai->dev = dev;

	dev_set_drvdata(dev, indai);

	ret = devm_snd_soc_register_component(dev,
			&berlin_indai_component,
			&berlin_indai_dai, 1);
	if (ret) {
		snd_printk("failed to register DAI: %d\n", ret);
		return ret;
	}
	snd_printk("%s: done i2s [%d %d]\n", __func__,
			indai->irq, indai->chid);
	return ret;
}

static RET_TYPE hdmi_indai_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct hdmi_priv *indai;

	indai = (struct hdmi_priv *)dev_get_drvdata(dev);

	/*close aio handle of alsa if have opened*/
	if (indai && indai->aio_handle) {
		close_aio(indai->aio_handle);
		indai->aio_handle = NULL;
	}

	RETURN_VALUE;
}

static const struct of_device_id hdmi_indai_dt_ids[] = {
	{ .compatible = "syna,dolphin-hdmi-indai",  },
	{}
};
MODULE_DEVICE_TABLE(of, hdmi_indai_dt_ids);

static struct platform_driver hdmi_indai_driver = {
	.probe = hdmi_indai_probe,
	.remove = hdmi_indai_remove,
	.driver = {
		.name = "syna-hdmi-indai",
		.of_match_table = hdmi_indai_dt_ids,
	},
};
module_platform_driver(hdmi_indai_driver);

MODULE_DESCRIPTION("Synaptics HDMI Capture ALSA driver");
MODULE_ALIAS("platform:hdmi-indai");
MODULE_LICENSE("GPL v2");
