// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2018-2020 Synaptics Incorporated */

#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <sound/soc.h>

#include "berlin_pcm.h"
#include "berlin_util.h"
#include "aio_hal.h"
#include "mic.h"
#include "avio_common.h"

/*
 * Max BCLK verified in VS640(platypus) is 24.576MHz,
 * based on this below list is derived.
 * 1. MIC1 (I2S 2ch x 1/2/3/4 lanes) - 192KHz can be supported
 * 2. MIC1 (TDM 4ch x 1/2 lanes) - 192KHz can be supported
 * 3. MIC1 (TDM 8ch x 1 lane) - 192KHz can not be supported
*/

#define I2S_MIC1_RATES      (SNDRV_PCM_RATE_8000_192000)
#define I2S_MIC1_FORMATS    (SNDRV_PCM_FMTBIT_S16_LE \
					| SNDRV_PCM_FMTBIT_S24_3LE \
					| SNDRV_PCM_FMTBIT_S24_LE \
					| SNDRV_PCM_FMTBIT_S32_LE)
#define MAX_MIC1_CH            4

static atomic_t mic1_cnt = ATOMIC_INIT(0);

struct mic1_priv {
	struct platform_device *pdev;
	const char *dev_name;
	u32 irqc;
	unsigned int irq[MAX_MIC1_CH];
	u32 chid[MAX_MIC1_CH];
	bool irq_requested;
	struct mic_common_cfg cfg;
	bool use_pri_clk;/*mic1 has cross feed */
	bool intlmode;/* mic1 has 4 data pin */
	/*  sample_period: sample period in terms of bclk numbers.
	 *  Typically 32 is used. For some pcm mono format, 16 may be used
	 */
	int  sample_period;
	u32 i2s_lanes;
	void *aio_handle;
};

static void mic1_set_rx_port_en(struct mic1_priv *mic1, bool en)
{
	int ret;

	ret = aio_enablerxport(mic1->aio_handle, AIO_ID_MIC1_RX, en);
	if (ret != 0)
		snd_printk("port(%d) error(%d)\n", en, ret);
}

static void mic1_ch_flush(struct mic1_priv *mic1, bool en)
{
	u32 i;

	for (i = 0; i < mic1->i2s_lanes; i++) {
		if (i < MAX_TSD)
			aio_set_aud_ch_flush(mic1->aio_handle,
				 AIO_ID_MIC1_RX, i, en);
	}
}

static void mic1_ch_en(struct mic1_priv *mic1, bool en)
{
	u32 i;

	for (i = 0; i < mic1->i2s_lanes; i++) {
		if (i < MAX_TSD)
			aio_set_aud_ch_en(mic1->aio_handle,
				 AIO_ID_MIC1_RX, i, en);
	}
}

static void mic1_ch_mute(struct mic1_priv *mic1, bool en)
{
	u32 i;

	for (i = 0; i < mic1->i2s_lanes; i++) {
		if (i < MAX_TSD)
			aio_set_aud_ch_mute(mic1->aio_handle,
				 AIO_ID_MIC1_RX, i, en);
	}
}

static void mic1_set_intlmode(struct mic1_priv *mic1, u32 en)
{
	u32 i;

	for (i = 0; i < mic1->i2s_lanes; i++) {
		if (i < MAX_TSD)
			aio_set_mic_intlmode(mic1->aio_handle,
				 AIO_ID_MIC1_RX, i, en);
	}
}

static void mic1_enable(struct mic1_priv *mic1, bool en)
{
	if (en && atomic_inc_return(&mic1_cnt) > 1)
		return;
	else if (!en && atomic_dec_if_positive(&mic1_cnt) != 0)
		return;

	mic1_set_rx_port_en(mic1, en);
}

static void mic1_sel_mic(struct mic1_priv *mic1)
{
	u32 i;

	for (i = 0; i < mic1->i2s_lanes; i++) {
		if (i < MAX_TSD)
			aio_set_pdmmicsel(mic1->aio_handle, 1<<i);
	}
}

static void mic1_set_clk_div(struct mic1_priv *mic1, u32 div)
{
	int ret;

	ret = aio_setclkdiv(mic1->aio_handle, AIO_ID_MIC1_RX, div);
	if (ret != 0)
		snd_printk("aio_setclkdiv(%d) error(ret=%d)\n", div, ret);
}

// Keep consistant for multiple mic1 instances case
static void mic1_set_ctl(struct mic1_priv *mic1, struct aud_ctrl *ctrl)
{
	int ret;

	ret = aio_set_ctl_ext(mic1->aio_handle, AIO_ID_MIC1_RX, ctrl);
	if (ret != 0)
		snd_printk("%s, error(ret=%d)\n", __func__, ret);
}

static void mic1_set_bclk(struct mic1_priv *mic1, u8 bclk, bool inv)
{
	int ret;

	ret = aio_set_bclk_sel(mic1->aio_handle, AIO_ID_MIC1_RX, bclk);
	if (ret != 0)
		snd_printk("aio_set_bclk_sel(bclk=%d) error(ret=%d)\n",
						bclk, ret);

	ret = aio_set_bclk_inv(mic1->aio_handle, AIO_ID_MIC1_RX, inv);
	if (ret != 0)
		snd_printk("aio_set_bclk_inv(inv=%d), error(err=%d)\n", inv, ret);
}

static void mic1_set_fsync(struct mic1_priv *mic1, bool sel, bool inv)
{
	int ret;

	ret = aio_set_fsync(mic1->aio_handle, AIO_ID_MIC1_RX, sel, inv);
	if (ret != 0)
		snd_printk("aio_set_fsync(sel=%d, inv=%d) error(ret=%d)\n",
			 sel, inv, ret);
}

static void mic1_set_mm_mode(struct mic1_priv *mic1, bool en)
{
	int ret;

	ret = aio_set_mic_mm_mode(mic1->aio_handle, AIO_ID_MIC1_RX, en);
	if (ret != 0)
		snd_printk("aio_set_mic_mm_mode(en=%d) error(ret=%d)\n",
			 en, ret);
}

static void mic1_set_ws_prd(struct mic1_priv *mic1, u32 highP, u32 totalP, u32 wsInv)
{
	int ret;

	ret = aio_set_mic_ws_prd(mic1->aio_handle, AIO_ID_MIC1_RX, highP, totalP, wsInv);
	if (ret != 0)
		snd_printk("aio_set_mic_ws_prd(highP=%d, totalP=%d, wsInv=%d) error(ret=%d)\n",
				 highP, totalP, wsInv, ret);
}

static struct snd_kcontrol_new i2s_mic1_ctrls[] = {
	//TODO: add dai controls here
};

static int i2s_mic1_startup(struct snd_pcm_substream *ss,
			      struct snd_soc_dai *dai)
{
	struct mic1_priv *mic1 = snd_soc_dai_get_drvdata(dai);

	snd_printd("%s: start %p %p\n", __func__, ss, dai);
	mic1_ch_en(mic1, 1);
	mic1_ch_mute(mic1, 1);

	return 0;
}

static void i2s_mic1_shutdown(struct snd_pcm_substream *ss,
				struct snd_soc_dai *dai)
{
	struct mic1_priv *mic1 = snd_soc_dai_get_drvdata(dai);

	snd_printd("%s: stop %p %p\n", __func__, ss, dai);
	mic1_ch_mute(mic1, 1);
	mic1_ch_en(mic1, 0);
	aio_i2s_clk_sync_reset(mic1->aio_handle, AIO_ID_MIC1_RX);
}

static int i2s_mic1_hw_params(struct snd_pcm_substream *ss,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct mic1_priv *mic1 = snd_soc_dai_get_drvdata(dai);
	u32 fs = params_rate(params);
	u32 width = params_width(params);
	u32 chnum = params_channels(params);
	const struct mclk_info *mclk = NULL;
	u32 dfm, cfm, bclk;
	struct aud_ctrl ctrl;
	int ret;
	struct berlin_ss_params ssparams = {};
	u32 xfeed = 0; // Clock cross feed

	/* Change AIO_24DFM to AIO_32DFM */
	dfm = berlin_get_sample_resolution((width == 24 ? 32 : width));

	/* Alghough h/w supports AIO_24CFM, but 24 is not multiples of 2.
	 * There could be some restriction on clock generation for certain
	 * frequency with AIO_24CFM. Change AIO_24CFM to AIO_32CFG instead
	 */
	cfm = berlin_get_sample_period_in_bclk(mic1->sample_period == 24 ?
						32 : mic1->sample_period);
	if (mic1->cfg.is_tdm) {
		/* TDM */
		bclk = fs * mic1->sample_period * (chnum / mic1->i2s_lanes);
	} else {
		/* i2s mode: each I2S_DI[0:3] supports 2 channels */
		bclk = fs * mic1->sample_period * 2;
	}

	mic1_sel_mic(mic1);

	ctrl.chcnt	= chnum / mic1->i2s_lanes;
	ctrl.sample_period_in_bclk	= cfm;
	ctrl.sample_resolution	= dfm;
	ctrl.data_fmt	= mic1->cfg.data_fmt;
	ctrl.isleftjfy	= mic1->cfg.isleftjfy;
	ctrl.invbclk	= mic1->cfg.invbclk;
	ctrl.invfs	= mic1->cfg.invfsync;
	ctrl.msb	= true;
	ctrl.istdm	= mic1->cfg.is_tdm;
	ctrl.islframe	= false;
	mic1_set_ctl(mic1, &ctrl);

	if (aio_i2s_get_mclk_cfg(fs, mic1->sample_period, &mclk) && !mclk) {
		snd_printk("fail to get mclk config");
		return -EINVAL;
	}

	/* mic1 clock source initilization */
	aio_i2s_set_clock(mic1->aio_handle, AIO_ID_MIC_RX, 1, mclk->d3_switch,
						mclk->plldiv, mclk->apll_id, 1);

	/* xfeed configuration
	 * 00: Master mode and I2S2_LRCKIO_DO_FB (clock)
	 * 01: Slave mode and I2S2_LRCKIO_DI_2mux (from PinMux) (clock)
	 * 10: Cross feed Master Mode and I2S1_LRCKIO_DO_FB (clock)
	 * 11: Cross feed Slave Mode and I2S1_LRCKIO_DI_2mux
	 *     (from PinMux) (clock)
	 *
	 * mic1->use_pri_clk   => 0: None cross feed   1: Cross feed
	 * mic1->cfg.is_master => 0: Slave mode        1: Master mode
	 */

	/* APLL setting. Both Master mode and cross feed from I2S1 required APLL setting
	 * per fs.
	 */
	if (mic1->use_pri_clk || mic1->cfg.is_master)
		berlin_set_pll(mic1->aio_handle, mclk->apll_id, mclk->apllrate);

	if (mic1->use_pri_clk) {
		mic1_set_bclk(mic1, 2, mic1->cfg.invbclk);
		mic1_set_fsync(mic1, 1, mic1->cfg.invfsync);
		xfeed |= 2;
	} else {
		if (mic1->cfg.is_tdm) {
			if (!mic1->cfg.is_master)
				mic1_set_bclk(mic1, 0, mic1->cfg.invbclk == 0 ? 1 : 0);
			else
				mic1_set_bclk(mic1, 1, mic1->cfg.invbclk);
		} else {
			if (!mic1->cfg.is_master)
				mic1_set_bclk(mic1, 0, mic1->cfg.invbclk);
			else
				mic1_set_bclk(mic1, 1, mic1->cfg.invbclk);
		}
		mic1_set_fsync(mic1, 0, mic1->cfg.invfsync);
	}
	if (mic1->cfg.is_master) {
		u32 period;
		/* Basically supports period of 16 and 32 only */
		period = mic1->sample_period == 24 ? 32 : mic1->sample_period;

		if (mic1->cfg.is_tdm) {
			mic1_set_ws_prd(mic1, 1, (ctrl.chcnt * period) - 1, 0);
		} else {
			/* i2s mode: each I2S_DI[0:3] supports 2 channels */
			mic1_set_ws_prd(mic1, period - 1, (2 * period) - 1, 1);
		}
		mic1_set_mm_mode(mic1, 1);
		mic1_set_clk_div(mic1, berlin_get_bclk_div(mclk->mclkrate, bclk));
	} else {
		xfeed |= 1;
		mic1_set_mm_mode(mic1, 0);
	}

	ret = aio_set_xfeed_mode(mic1->aio_handle, AIO_ID_MIC1_RX, xfeed, xfeed);
	if (ret != 0) {
		snd_printk("fail to set xfeed mode for i2s, ret %d\n", ret);
		return ret;
	}

	mic1_ch_flush(mic1, 0);

	mic1_set_intlmode(mic1, mic1->intlmode);
	ssparams.irq_num = 1;
	ssparams.chid_num = 1;
	ssparams.mode = I2SI_MODE;
	ssparams.enable_mic_mute = !mic1->cfg.disable_mic_mute;
	ssparams.irq = mic1->irq;
	ssparams.interleaved = false;
	ssparams.dummy_data = false;
	ssparams.multi_lanes =
			mic1->cfg.is_tdm ? mic1->i2s_lanes > 1 ? true : false : false;
	ssparams.dev_name = mic1->dev_name;
	ret = berlin_pcm_request_dma_irq(ss, &ssparams);
	if (ret == 0)
		mic1->irq_requested = true;

	return ret;
}

static int i2s_mic1_set_dai_fmt(struct snd_soc_dai *dai,
				  unsigned int fmt)
{
	struct mic1_priv *outdai = snd_soc_dai_get_drvdata(dai);
	int ret = 0;

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		outdai->cfg.data_fmt  = 2;
		outdai->cfg.is_tdm    = false;
		outdai->cfg.isleftjfy = true;   /* don't care if data_fmt = 2 */
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		outdai->cfg.data_fmt  = 1;
		outdai->cfg.is_tdm    = false;
		outdai->cfg.isleftjfy = true;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		outdai->cfg.data_fmt  = 1;
		outdai->cfg.is_tdm    = false;
		outdai->cfg.isleftjfy = false;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		outdai->cfg.data_fmt  = 1;
		outdai->cfg.is_tdm    = true;
		outdai->cfg.isleftjfy = true;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		outdai->cfg.data_fmt  = 2;
		outdai->cfg.is_tdm    = true;
		outdai->cfg.isleftjfy = true;  /* don't care if data_fmt = 2 */
		break;
	default:
		dev_err(dai->dev, "Unknown DAI format mask %x\n", fmt);
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		outdai->cfg.invbclk  = false;
		outdai->cfg.invfsync = false;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		outdai->cfg.invbclk  = false;
		outdai->cfg.invfsync = true;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		outdai->cfg.invbclk  = true;
		outdai->cfg.invfsync = false;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		outdai->cfg.invbclk  = true;
		outdai->cfg.invfsync = true;
		break;
	default:
		dev_err(dai->dev, "Unknown DAI invert mask %x\n", fmt);
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 0, 0)
	case SND_SOC_DAIFMT_BP_FP:
		outdai->cfg.is_master = true;
		break;
	case SND_SOC_DAIFMT_BC_FC:
		outdai->cfg.is_master = false;
		break;
#else
	case SND_SOC_DAIFMT_CBP_CFP:
		outdai->cfg.is_master = false;
		break;
	case SND_SOC_DAIFMT_CBC_CFC:
		outdai->cfg.is_master = true;
		break;
#endif
	case SND_SOC_DAIFMT_CBP_CFC:
	case SND_SOC_DAIFMT_CBC_CFP:
	default:
		dev_err(dai->dev, "Do not support DAI master mask %x\n", fmt);
		return -EINVAL;
	}

	snd_printd("%s: data_fmt: %d isleftjfy: %d is_tdm: %d is_master: %d invbclk: %d invfsync: %d\n",
				__func__,
				outdai->cfg.data_fmt, outdai->cfg.isleftjfy,
				outdai->cfg.is_tdm, outdai->cfg.is_master,
				outdai->cfg.invbclk, outdai->cfg.invfsync);

	snd_printd("%s: sample_period: %d", __func__, outdai->sample_period);

	return ret;
}

static int i2s_mic1_hw_free(struct snd_pcm_substream *ss,
			      struct snd_soc_dai *dai)
{
	struct mic1_priv *mic1 = snd_soc_dai_get_drvdata(dai);

	if (mic1->irq_requested) {
		berlin_pcm_free_dma_irq(ss, 1, mic1->irq);
		mic1->irq_requested = false;
	}

	mic1_set_bclk(mic1, 0, 0);
	mic1_set_fsync(mic1, 0, 0);
	mic1_set_mm_mode(mic1, 0);

	mic1_ch_flush(mic1, 1);

	return 0;
}

static int i2s_mic1_trigger(struct snd_pcm_substream *ss,
			      int cmd, struct snd_soc_dai *dai)
{
	struct mic1_priv *mic1 = snd_soc_dai_get_drvdata(dai);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		mic1_ch_mute(mic1, 0);
		mic1_enable(mic1, 1);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		mic1_enable(mic1, 0);
		mic1_ch_mute(mic1, 1);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int i2s_mic1_dai_probe(struct snd_soc_dai *dai)
{
	snd_soc_add_dai_controls(dai, i2s_mic1_ctrls,
				 ARRAY_SIZE(i2s_mic1_ctrls));

	return 0;
}

static struct snd_soc_dai_ops i2s_dai_mic1_ops = {
	.startup = i2s_mic1_startup,
	.hw_params = i2s_mic1_hw_params,
	.set_fmt = i2s_mic1_set_dai_fmt,
	.hw_free = i2s_mic1_hw_free,
	.trigger = i2s_mic1_trigger,
	.shutdown = i2s_mic1_shutdown,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0))
	.probe = i2s_mic1_dai_probe,
#endif
};

static struct snd_soc_dai_driver i2s_mic1_dai = {
	.name = "i2s-mic1",
#if (LINUX_VERSION_CODE < KERNEL_VERSION(6, 12, 0))
	.probe = i2s_mic1_dai_probe,
#endif
	.capture = {
		.stream_name = "MIC1-Capture",
		.channels_min = 2,
		.channels_max = 8,
		.rates = I2S_MIC1_RATES,
		.formats = I2S_MIC1_FORMATS,
	},
	.ops = &i2s_dai_mic1_ops,
};

static int i2s_mic1_component_hw_free(struct snd_soc_component *component,
							struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = snd_pcm_substream_chip(substream);
	struct snd_soc_dai *cpu_dai;
	struct mic1_priv *mic1;

	if (rtd == NULL)
		return 0;

	cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);

	if (cpu_dai == NULL || cpu_dai->dev == NULL)
		return 0;

	mic1 = snd_soc_dai_get_drvdata(cpu_dai);
	if (mic1 == NULL)
		return 0;
	if (mic1->irq_requested) {
		berlin_pcm_free_dma_irq(substream, 1, mic1->irq);
		mic1->irq_requested = false;
	}

	mic1_set_bclk(mic1, 0, 0);
	mic1_set_fsync(mic1, 0, 0);
	mic1_set_mm_mode(mic1, 0);
	mic1_ch_flush(mic1, 1);

	return 0;
}

static const struct snd_soc_component_driver i2s_mic1_component = {
	.name = "i2s-mic1",
	.hw_free = i2s_mic1_component_hw_free,
};

static int i2s_mic1_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct mic1_priv *mic1;
	int i, ret;

	//Defer probe until dependent soc module/s are probed/initialized
	if (!is_avio_driver_initialized())
		return -EPROBE_DEFER;

	mic1 = devm_kzalloc(dev, sizeof(struct mic1_priv), GFP_KERNEL);
	if (!mic1)
		return -ENOMEM;

	mic1->irqc = platform_irq_count(pdev);
	if (mic1->irqc == 0) {
		snd_printk("no mic1 channel defined in dts, do onthing");
		return 0;
	}

	for (i = 0; i < mic1->irqc; i++) {
		mic1->irq[i] = platform_get_irq(pdev, i);
		if (mic1->irq[i] < 0)
			return mic1->irq[i];
		mic1->chid[i] = irqd_to_hwirq(irq_get_irq_data(mic1->irq[i]));
		if (mic1->chid[i] < 0) {
			snd_printk("got invalid dhub chid[%d] %d\n",
				 i, mic1->chid[i]);
			return -EINVAL;
		}
	}

	mic1->cfg.disable_mic_mute = of_property_read_bool(np, "disablemicmute");
	mic1->use_pri_clk = of_property_read_bool(np, "use_pri_clk");
	mic1->intlmode = of_property_read_bool(np, "intlmode");
	ret = of_property_read_u32(np, "sample-period", &mic1->sample_period);
	if (ret)
		mic1->sample_period = 32;

	/* Get the number of i2s lanes configuration from dts to decide how many
	 * lines among 4 MIC1 lines need to be enabled for capturing data
	 */
	ret = of_property_read_u32(np, "i2s-lanes", &mic1->i2s_lanes);
	if (ret)
		mic1->i2s_lanes = 1; //by default 1 i2s-lane for 2 ch audio data

	snd_printk("[%s.%u]i2s_lanes=%u\n", __func__, __LINE__, mic1->i2s_lanes);

	mic1->dev_name = dev_name(dev);
	mic1->pdev = pdev;
	mic1->aio_handle = open_aio(mic1->dev_name);
	if (unlikely(mic1->aio_handle == NULL)) {
		snd_printk("aio_handle:%p  get failed\n", mic1->aio_handle);
		return -EBUSY;
	}
	dev_set_drvdata(dev, mic1);

	ret = devm_snd_soc_register_component(dev,
					      &i2s_mic1_component,
					      &i2s_mic1_dai, 1);
	if (ret) {
		snd_printk("failed to register DAI: %d\n", ret);
		close_aio(mic1->aio_handle);
		mic1->aio_handle = NULL;
		return ret;
	}
	snd_printd("%s: done irqc %d\n", __func__, mic1->irqc);

	return ret;
}

static RET_TYPE i2s_mic1_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mic1_priv *mic1;

	mic1 = (struct mic1_priv *)dev_get_drvdata(dev);

	/*close aio handle of alsa if have opened*/
	if (mic1 && mic1->aio_handle) {
		close_aio(mic1->aio_handle);
		mic1->aio_handle = NULL;
	}

	RETURN_VALUE;
}

static const struct of_device_id i2s_mic1_dt_ids[] = {
	{ .compatible = "syna,as370-mic1",  },
	{ .compatible = "syna,dolphin-i2s-mic1",  },
	{ .compatible = "syna,platypus-i2s-mic1",  },
	{ .compatible = "syna,myna-mic1",  },
	{ .compatible = "syna,myna2-mic1",  },
	{}
};
MODULE_DEVICE_TABLE(of, i2s_mic1_dt_ids);

static struct platform_driver i2s_mic1_driver = {
	.probe = i2s_mic1_probe,
	.remove = i2s_mic1_remove,
	.driver = {
		.name = "syna-i2s-mic1",
		.of_match_table = i2s_mic1_dt_ids,
	},
};
module_platform_driver(i2s_mic1_driver);

MODULE_DESCRIPTION("Synaptics I2S MIC1 ALSA driver");
MODULE_ALIAS("platform:i2s-mic1");
MODULE_LICENSE("GPL v2");
