// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2018-2020 Synaptics Incorporated */

#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <sound/soc.h>
#include <sound/tlv.h>

#include "berlin_pcm.h"
#include "berlin_util.h"
#include "aio_hal.h"
#include "avio_common.h"

#define SPDIF_PLAYBACK_RATES   (SNDRV_PCM_RATE_8000_192000)
#define SPDIF_PLAYBACK_FORMATS (SNDRV_PCM_FMTBIT_S16_LE \
				| SNDRV_PCM_FMTBIT_S24_LE \
				| SNDRV_PCM_FMTBIT_S24_3LE \
				| SNDRV_PCM_FMTBIT_S32_LE)

static const char * const spdifo_daifmt_text[] = {"OFF", "ON"};
static SOC_ENUM_SINGLE_EXT_DECL(spdifo_daifmt, spdifo_daifmt_text);

struct spdifo_priv {
	struct device *dev;
	const char *dev_name;
	unsigned int spdif_irq;
	u32 spdif_chid;
	u32 mode;
	u32 daifmt;
	bool spdif_requested;
	void *aio_handle;
	bool mute;
	/* PCM volume control (dB based) */
	int volume_db;
	struct snd_pcm_substream *ss;
};

static void outdai_set_spdif_clk(struct spdifo_priv *out, u32 div)
{
	int ret;

	ret = aio_setspdifclk(out->aio_handle,  div);
	if (ret != 0)
		snd_printk("aio_setspdifclk() return error(ret=%d)\n", ret);
}

static int spdif_mute_get(struct snd_kcontrol *kcontrol,
						  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dai *dai = snd_kcontrol_chip(kcontrol);
	struct spdifo_priv *spdif = snd_soc_dai_get_drvdata(dai);

	ucontrol->value.integer.value[0] = spdif->mute ? 1 : 0;
	return 0;
}

static int spdif_mute_put(struct snd_kcontrol *kcontrol,
						  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dai *dai = snd_kcontrol_chip(kcontrol);
	struct spdifo_priv *spdif = snd_soc_dai_get_drvdata(dai);
	bool mute = ucontrol->value.integer.value[0] ? true : false;

	if (spdif->mute == mute)
		return 0;

	spdif->mute = mute;

	/* Apply mute setting to hardware */
	snd_printd("SPDIF mute %s\n", mute ? "ON" : "OFF");
	if (spdif->aio_handle) {
		aio_set_aud_ch_mute(spdif->aio_handle, AIO_ID_SPDIF_TX, AIO_TSD0,
							mute ? 1 : 0);
	} else {
		snd_printk("SPDIF mute: no aio_handle, skipped\n");
	}

	return 1;
}

/* SPDIF PCM Volume Control (dB based) */
static int spdif_volume_info(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = -6000;  /* -60.00 dB */
	uinfo->value.integer.max = 0;      /*   0.00 dB */
	uinfo->value.integer.step = 50;    /*   0.50 dB */
	return 0;
}

static int spdif_volume_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dai *dai = snd_kcontrol_chip(kcontrol);
	struct spdifo_priv *spdif = snd_soc_dai_get_drvdata(dai);

	ucontrol->value.integer.value[0] = spdif->volume_db;

	return 0;
}

static int spdif_volume_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dai *dai = snd_kcontrol_chip(kcontrol);
	struct spdifo_priv *spdif = snd_soc_dai_get_drvdata(dai);
	int db_value = ucontrol->value.integer.value[0];
	int old_value;

	if (db_value < -6000 || db_value > 0)
		return -EINVAL;

	old_value = spdif->volume_db;
	spdif->volume_db = db_value;

	if (old_value != db_value && spdif->ss)
		berlin_pcm_set_volume_db(spdif->ss, SPDIFO_MODE, db_value);

	return (old_value != db_value) ? 1 : 0;
}

/* TLV dB scale information */
/* Note: mute=0 because -60dB is not complete silence, just very quiet */
static const DECLARE_TLV_DB_SCALE(spdif_volume_tlv, -6000, 50, 0);

/* Control definition macros (following kernel style) */
#define SPDIF_VOLUME_CONTROL(xname, tlv_array) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE | \
		  SNDRV_CTL_ELEM_ACCESS_TLV_READ, \
	.tlv.p = (tlv_array), \
	.info = spdif_volume_info, \
	.get = spdif_volume_get, .put = spdif_volume_put}

#define SPDIF_MUTE_CONTROL(xname) \
{	.iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, \
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE, \
	.info = snd_ctl_boolean_mono_info, \
	.get = spdif_mute_get, .put = spdif_mute_put}

static int spdifo_daifmt_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dai *cpu_dai = snd_kcontrol_chip(kcontrol);
	struct spdifo_priv *outdai = snd_soc_dai_get_drvdata(cpu_dai);

	ucontrol->value.enumerated.item[0] = outdai->daifmt;

	return 0;
}

static int spdifo_daifmt_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dai *cpu_dai = snd_kcontrol_chip(kcontrol);
	struct spdifo_priv *outdai = snd_soc_dai_get_drvdata(cpu_dai);

	outdai->daifmt = ucontrol->value.enumerated.item[0];

	return 0;
}

static struct snd_kcontrol_new berlin_outdai_ctrls[] = {
	SOC_ENUM_EXT("SPDIFO DAIFMT", spdifo_daifmt,
		spdifo_daifmt_get, spdifo_daifmt_put),
	SPDIF_MUTE_CONTROL("SPDIF Playback Switch"),
	SPDIF_VOLUME_CONTROL("SPDIF PCM Playback Volume", spdif_volume_tlv),
};

/*
 * Applies output configuration of |berlin_pcm| to i2s.
 * Must be called with instance spinlock held.
 * Only one dai instance for playback, so no spin_lock needed
 */
static void outdai_set_aio(struct spdifo_priv *out, u32 fs, int width, int chnum, u32 mclkrate)
{
	unsigned int analog_div, spdif_div;

	analog_div = berlin_get_bclk_div(mclkrate, (fs*width*chnum));
	spdif_div = analog_div - 1; /* SPDIF clock is half of analog clock */

	outdai_set_spdif_clk(out, spdif_div);
}

static int berlin_outdai_startup(struct snd_pcm_substream *substream,
				 struct snd_soc_dai *dai)
{
	//TODO
	return 0;
}

static void berlin_outdai_shutdown(struct snd_pcm_substream *substream,
				   struct snd_soc_dai *dai)
{
	//TODO
}

static int berlin_outdai_hw_params(struct snd_pcm_substream *substream,
				   struct snd_pcm_hw_params *params,
				   struct snd_soc_dai *dai)
{
	struct spdifo_priv *outdai = snd_soc_dai_get_drvdata(dai);
	u32 fs = params_rate(params);
	const struct mclk_info *mclk = NULL;
	int ret;
	struct berlin_ss_params ssparams = {};

	if (aio_i2s_get_mclk_cfg(fs, 32, &mclk) && !mclk) {
		snd_printk("fail to get mclk config");
		return -EINVAL;
	}

	/* mclk */
	aio_i2s_set_clock(outdai->aio_handle, AIO_ID_SPDIF_TX, 1, mclk->d3_switch,
						mclk->plldiv, mclk->apll_id, 1);

	ssparams.irq_num = 1;
	ssparams.chid_num = 1;
	ssparams.mode = SPDIFO_MODE;
	ssparams.irq = &outdai->spdif_irq;
	ssparams.dev_name = outdai->dev_name;

	if(outdai->daifmt)
		ssparams.dai_fmt = DAI_FMT_IEC61937;
	else
		ssparams.dai_fmt = DAI_FMT_PCM;

	snd_printd("spdif dai_fmt(%d)\n", ssparams.dai_fmt);
	ret = berlin_pcm_request_dma_irq(substream, &ssparams);
	if (ret == 0) {
		outdai->spdif_requested = true;
		outdai->ss = substream;
		berlin_pcm_set_volume_db(substream, SPDIFO_MODE,
					     outdai->volume_db);
	} else
		return ret;
	aio_setspdif_en(outdai->aio_handle, 1);

	aio_set_aud_ch_flush(outdai->aio_handle, AIO_ID_SPDIF_TX, AIO_TSD0, 0);

	berlin_set_pll(outdai->aio_handle, mclk->apll_id, mclk->apllrate);

	outdai_set_aio(outdai, fs, 32, params_channels(params), mclk->mclkrate);

	return ret;
}

static int berlin_outdai_hw_free(struct snd_pcm_substream *substream,
				 struct snd_soc_dai *dai)
{
	struct spdifo_priv *outdai = snd_soc_dai_get_drvdata(dai);

	aio_set_aud_ch_flush(outdai->aio_handle, AIO_ID_SPDIF_TX, AIO_TSD0, 1);
	aio_setspdif_en(outdai->aio_handle, 0);

	if (outdai->spdif_requested && outdai->spdif_irq >= 0) {
		berlin_pcm_free_dma_irq(substream, 1, &outdai->spdif_irq);
		outdai->spdif_requested = false;
	}
	outdai->ss = NULL;

	return 0;
}

static int berlin_outdai_trigger(struct snd_pcm_substream *substream,
				 int cmd, struct snd_soc_dai *dai)
{
	struct spdifo_priv *outdai = snd_soc_dai_get_drvdata(dai);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		snd_printd("spdif:%s: dainame %s, cmd: %d\n", __func__, outdai->dev_name, cmd);
		/* Only unmute if user hasn't manually muted via amixer */
		if (!outdai->mute) {
			snd_printd("SPDIF playback start: unmuting\n");
			aio_set_aud_ch_mute(outdai->aio_handle, AIO_ID_SPDIF_TX, AIO_TSD0, 0);
		} else {
			snd_printd("SPDIF playback start: keeping muted (user muted)\n");
		}
		aio_set_aud_ch_flush(outdai->aio_handle, AIO_ID_SPDIF_TX, AIO_TSD0, 0);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		snd_printd("spdif:%s: dainame %s, cmd: %d\n", __func__, outdai->dev_name, cmd);
		aio_set_aud_ch_mute(outdai->aio_handle, AIO_ID_SPDIF_TX, AIO_TSD0, 1);
		aio_set_aud_ch_flush(outdai->aio_handle, AIO_ID_SPDIF_TX, AIO_TSD0, 1);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int berlin_outdai_dai_probe(struct snd_soc_dai *dai)
{
	struct spdifo_priv *outdai = snd_soc_dai_get_drvdata(dai);

	aio_setspdif_en(outdai->aio_handle, 1);

	snd_soc_add_dai_controls(dai, berlin_outdai_ctrls,
				 ARRAY_SIZE(berlin_outdai_ctrls));
	return 0;
}

static struct snd_soc_dai_ops berlin_spdif_outdai_ops = {
	.startup   = berlin_outdai_startup,
	.hw_params = berlin_outdai_hw_params,
	.hw_free   = berlin_outdai_hw_free,
	.trigger   = berlin_outdai_trigger,
	.shutdown  = berlin_outdai_shutdown,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0))
	.probe 	   = berlin_outdai_dai_probe,
#endif
};

static struct snd_soc_dai_driver berlin_outdai_dai = {
	.name = "spdif-outdai",
#if (LINUX_VERSION_CODE < KERNEL_VERSION(6, 12, 0))
	.probe 	   = berlin_outdai_dai_probe,
#endif
	.playback = {
		.stream_name = "SPDIF-Playback",
		.channels_min = 1,
		.channels_max = 8,
		.rates = SPDIF_PLAYBACK_RATES,
		.formats = SPDIF_PLAYBACK_FORMATS,
	},
	.ops = &berlin_spdif_outdai_ops,
};

static const struct snd_soc_component_driver berlin_outdai_component = {
	.name = "spdif-outdai",
};

static int spdif_outdai_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct spdifo_priv *outdai;
	int irq, ret;

	//Defer probe until dependent soc module/s are probed/initialized
	if (!is_avio_driver_initialized())
		return -EPROBE_DEFER;

	outdai = devm_kzalloc(dev, sizeof(struct spdifo_priv),
			      GFP_KERNEL);
	if (!outdai)
		return -ENOMEM;
	outdai->dev_name = dev_name(dev);
	outdai->dev = dev;
	outdai->mute = false;

	/* Initialize volume control */
	outdai->volume_db = 0;  /* 0dB (unity gain) */

	irq = platform_get_irq_byname(pdev, "spdifo");
	if (irq < 0)
		return irq;

	/*open aio handle for alsa*/
	outdai->aio_handle = open_aio(outdai->dev_name);
	if (unlikely(outdai->aio_handle == NULL)) {
		snd_printk("aio_handle:%p  get failed\n", outdai->aio_handle);
		return -EBUSY;
	}
	dev_set_drvdata(dev, outdai);

	outdai->mode |= SPDIFO_MODE;
	outdai->spdif_irq = irq;
	outdai->spdif_chid = irqd_to_hwirq(irq_get_irq_data(irq));
	snd_printd("get spdif irq %d for node %s\n", irq, pdev->name);

	ret = devm_snd_soc_register_component(dev,
					      &berlin_outdai_component,
					      &berlin_outdai_dai, 1);
	if (ret) {
		snd_printk("failed to register DAI: %d\n", ret);
		close_aio(outdai->aio_handle);
		outdai->aio_handle = NULL;
		return ret;
	}
	snd_printd("spdif [%d %d]\n", outdai->spdif_irq, outdai->spdif_chid);
	return ret;
}

static RET_TYPE spdif_outdai_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct spdifo_priv *outdai;

	outdai = (struct spdifo_priv *)dev_get_drvdata(dev);

	/*close aio handle of alsa if have opened*/
	if (outdai && outdai->aio_handle) {
		close_aio(outdai->aio_handle);
		outdai->aio_handle = NULL;
	}

	RETURN_VALUE;
}

static const struct of_device_id spdif_outdai_dt_ids[] = {
	{ .compatible = "syna,dolphin-spdifo",  },
	{ .compatible = "syna,platypus-spdifo",  },
	{}
};
MODULE_DEVICE_TABLE(of, spdif_outdai_dt_ids);

static struct platform_driver spdif_outdai_driver = {
	.probe = spdif_outdai_probe,
	.remove = spdif_outdai_remove,
	.driver = {
		.name = "syna-spdif-outdai",
		.of_match_table = spdif_outdai_dt_ids,
	},
};
module_platform_driver(spdif_outdai_driver);

MODULE_DESCRIPTION("Synaptics SPDIF ALSA output dai");
MODULE_ALIAS("platform:spdif-outdai");
MODULE_LICENSE("GPL v2");
