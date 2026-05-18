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
#include "avio_common.h"

#define I2S_PLAYBACK_RATES   (SNDRV_PCM_RATE_8000_192000)
#define I2S_PLAYBACK_FORMATS (SNDRV_PCM_FMTBIT_S16_LE \
				| SNDRV_PCM_FMTBIT_S24_LE \
				| SNDRV_PCM_FMTBIT_S24_3LE \
				| SNDRV_PCM_FMTBIT_S32_LE)

static const char * const syna_i2s_bclk_src_text[] = {
	"APLL", "XFEED", "PAD"
};

static const char * const syna_i2s_xfeed_clk_text[] = {
	"I2S1_DI", "I2S1_LPBK", "I2S2_DI", "I2S2_LPBK", "I2S3_DI", "I2S3_LPBK", "I2S4_DI", "I2S4_LPBK", "I2S5_DI", "I2S5_LPBK",
};

static const char * const syna_i2s_master_slave_text[] = {
	"Slave", "Master",
};

static const char * const syna_i2s_sync_start_text[] = {
	"OFF", "ABSO", "ASAP", "RELA",
};

static int i2s_soc_dai_get_ctrl(struct snd_kcontrol *kcontrol,
						struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dai *dai = snd_kcontrol_chip(kcontrol);
	struct i2s_soc_dai *soc_dai = snd_soc_dai_get_drvdata(dai);

	if (strstr(kcontrol->id.name, I2S_LPBK_KCONTROL_NAME)) {
		ucontrol->value.enumerated.item[0] = soc_dai->rxDataMode;
	} else if (strstr(kcontrol->id.name, I2S_CLK_SRC_KCONTROL_NAME)) {
		ucontrol->value.enumerated.item[0] = soc_dai->clkSrcSel;
	} else if (strstr(kcontrol->id.name, I2S_XFEED_CLK_KCONTROL_NAME)) {
		ucontrol->value.enumerated.item[0] = soc_dai->xFeedClkCfg;
	} else if (strstr(kcontrol->id.name, I2S_MODE_KCONTROL_NAME)) {
		ucontrol->value.enumerated.item[0] = soc_dai->is_master;
	} else if (strstr(kcontrol->id.name, I2S_SYNC_START_KCONTROL_NAME)) {
		ucontrol->value.enumerated.item[0] = soc_dai->eAPRKTMode;
	}
	return 0;
}

static int i2s_soc_dai_put_ctrl(struct snd_kcontrol *kcontrol,
						struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dai *dai = snd_kcontrol_chip(kcontrol);
	struct i2s_soc_dai *soc_dai = snd_soc_dai_get_drvdata(dai);
	int value = ucontrol->value.enumerated.item[0];

	if (strstr(kcontrol->id.name, I2S_LPBK_KCONTROL_NAME)) {
		if (value == soc_dai->rxDataMode)
			return 0;
		soc_dai->rxDataMode = value;
	} else if (strstr(kcontrol->id.name, I2S_CLK_SRC_KCONTROL_NAME)) {
		if (value == soc_dai->clkSrcSel)
			return 0;
		soc_dai->clkSrcSel = value;
	} else if (strstr(kcontrol->id.name, I2S_XFEED_CLK_KCONTROL_NAME)) {
		if (value == soc_dai->xFeedClkCfg)
			return 0;
		soc_dai->xFeedClkCfg = value;
	} else if (strstr(kcontrol->id.name, I2S_MODE_KCONTROL_NAME)) {
		if (value == soc_dai->is_master)
			return 0;
		soc_dai->is_master = value;
	} else if (strstr(kcontrol->id.name, I2S_SYNC_START_KCONTROL_NAME)) {
		if (value == soc_dai->eAPRKTMode)
			return 0;
		soc_dai->eAPRKTMode = value;
	}

	return 0;
}

static const struct soc_enum syna_i2s_bclk_src_enum =
	SOC_ENUM_SINGLE(0, 0, ARRAY_SIZE(syna_i2s_bclk_src_text),
		syna_i2s_bclk_src_text);

static const struct soc_enum syna_i2s_xfeed_clk_enum =
	SOC_ENUM_SINGLE(0, 0, ARRAY_SIZE(syna_i2s_xfeed_clk_text),
		syna_i2s_xfeed_clk_text);

static const struct soc_enum syna_i2s_master_slave_enum =
	SOC_ENUM_SINGLE(0, 0, ARRAY_SIZE(syna_i2s_master_slave_text),
		syna_i2s_master_slave_text);

static const struct soc_enum syna_i2s_sync_start_enum =
	SOC_ENUM_SINGLE(0, 0, ARRAY_SIZE(syna_i2s_sync_start_text),
		syna_i2s_sync_start_text);

static const struct snd_kcontrol_new syna_i2s1_controls[] = {
	SOC_SINGLE_BOOL_EXT("I2S1 " I2S_LPBK_KCONTROL_NAME, 0, i2s_soc_dai_get_ctrl, i2s_soc_dai_put_ctrl),
	SOC_ENUM_EXT("I2S1 " I2S_CLK_SRC_KCONTROL_NAME, syna_i2s_bclk_src_enum,
				i2s_soc_dai_get_ctrl, i2s_soc_dai_put_ctrl),
	SOC_ENUM_EXT("I2S1 " I2S_XFEED_CLK_KCONTROL_NAME, syna_i2s_xfeed_clk_enum,
				i2s_soc_dai_get_ctrl, i2s_soc_dai_put_ctrl),
	SOC_ENUM_EXT("I2S1 " I2S_MODE_KCONTROL_NAME, syna_i2s_master_slave_enum,
				i2s_soc_dai_get_ctrl, i2s_soc_dai_put_ctrl),
	SOC_ENUM_EXT("I2S1 " I2S_SYNC_START_KCONTROL_NAME, syna_i2s_sync_start_enum,
				i2s_soc_dai_get_ctrl, i2s_soc_dai_put_ctrl),
};

static const struct snd_kcontrol_new syna_i2s2_controls[] = {
	SOC_SINGLE_BOOL_EXT("I2S2 " I2S_LPBK_KCONTROL_NAME, 0, i2s_soc_dai_get_ctrl, i2s_soc_dai_put_ctrl),
	SOC_ENUM_EXT("I2S2 " I2S_CLK_SRC_KCONTROL_NAME, syna_i2s_bclk_src_enum,
				i2s_soc_dai_get_ctrl, i2s_soc_dai_put_ctrl),
	SOC_ENUM_EXT("I2S2 " I2S_XFEED_CLK_KCONTROL_NAME, syna_i2s_xfeed_clk_enum,
				i2s_soc_dai_get_ctrl, i2s_soc_dai_put_ctrl),
	SOC_ENUM_EXT("I2S2 " I2S_MODE_KCONTROL_NAME, syna_i2s_master_slave_enum,
				i2s_soc_dai_get_ctrl, i2s_soc_dai_put_ctrl),
	SOC_ENUM_EXT("I2S2 " I2S_SYNC_START_KCONTROL_NAME, syna_i2s_sync_start_enum,
				i2s_soc_dai_get_ctrl, i2s_soc_dai_put_ctrl),
};

static const struct snd_kcontrol_new syna_i2s3_controls[] = {
	SOC_SINGLE_BOOL_EXT("I2S3 " I2S_LPBK_KCONTROL_NAME, 0, i2s_soc_dai_get_ctrl, i2s_soc_dai_put_ctrl),
	SOC_ENUM_EXT("I2S3 " I2S_CLK_SRC_KCONTROL_NAME, syna_i2s_bclk_src_enum,
				i2s_soc_dai_get_ctrl, i2s_soc_dai_put_ctrl),
	SOC_ENUM_EXT("I2S3 " I2S_XFEED_CLK_KCONTROL_NAME, syna_i2s_xfeed_clk_enum,
				i2s_soc_dai_get_ctrl, i2s_soc_dai_put_ctrl),
	SOC_ENUM_EXT("I2S3 " I2S_MODE_KCONTROL_NAME, syna_i2s_master_slave_enum,
				i2s_soc_dai_get_ctrl, i2s_soc_dai_put_ctrl),
	SOC_ENUM_EXT("I2S3 " I2S_SYNC_START_KCONTROL_NAME, syna_i2s_sync_start_enum,
				i2s_soc_dai_get_ctrl, i2s_soc_dai_put_ctrl),
};

static const struct snd_kcontrol_new syna_i2s4_controls[] = {
	SOC_SINGLE_BOOL_EXT("I2S4 " I2S_LPBK_KCONTROL_NAME, 0, i2s_soc_dai_get_ctrl, i2s_soc_dai_put_ctrl),
	SOC_ENUM_EXT("I2S4 " I2S_CLK_SRC_KCONTROL_NAME, syna_i2s_bclk_src_enum,
				i2s_soc_dai_get_ctrl, i2s_soc_dai_put_ctrl),
	SOC_ENUM_EXT("I2S4 " I2S_XFEED_CLK_KCONTROL_NAME, syna_i2s_xfeed_clk_enum,
				i2s_soc_dai_get_ctrl, i2s_soc_dai_put_ctrl),
	SOC_ENUM_EXT("I2S4 " I2S_MODE_KCONTROL_NAME, syna_i2s_master_slave_enum,
				i2s_soc_dai_get_ctrl, i2s_soc_dai_put_ctrl),
	SOC_ENUM_EXT("I2S4 " I2S_SYNC_START_KCONTROL_NAME, syna_i2s_sync_start_enum,
				i2s_soc_dai_get_ctrl, i2s_soc_dai_put_ctrl),
};

static const struct snd_kcontrol_new syna_i2s5_controls[] = {
	SOC_SINGLE_BOOL_EXT("I2S5 " I2S_LPBK_KCONTROL_NAME, 0, i2s_soc_dai_get_ctrl, i2s_soc_dai_put_ctrl),
	SOC_ENUM_EXT("I2S5 " I2S_CLK_SRC_KCONTROL_NAME, syna_i2s_bclk_src_enum,
				i2s_soc_dai_get_ctrl, i2s_soc_dai_put_ctrl),
	SOC_ENUM_EXT("I2S5 " I2S_XFEED_CLK_KCONTROL_NAME, syna_i2s_xfeed_clk_enum,
				i2s_soc_dai_get_ctrl, i2s_soc_dai_put_ctrl),
	SOC_ENUM_EXT("I2S5 " I2S_MODE_KCONTROL_NAME, syna_i2s_master_slave_enum,
				i2s_soc_dai_get_ctrl, i2s_soc_dai_put_ctrl),
	SOC_ENUM_EXT("I2S5 " I2S_SYNC_START_KCONTROL_NAME, syna_i2s_sync_start_enum,
				i2s_soc_dai_get_ctrl, i2s_soc_dai_put_ctrl),
};


//below configuration is based on MYNA chip
static enum aud_clk_id map_aio_clkId(u32 i2s_chid)
{
	switch (i2s_chid) {
	case AIO_ID_I2S1_TX:
	case AIO_ID_I2S1_RX:
		return I2S_CLK_ID_I2S1;
	case AIO_ID_I2S2_TX:
	case AIO_ID_I2S2_RX:
		return I2S_CLK_ID_I2S2;
	case AIO_ID_I2S3_TX:
	case AIO_ID_I2S3_RX:
		return I2S_CLK_ID_I2S3;
#ifdef AVIO_MYNA2
	case AIO_ID_I2S4_TX:
	case AIO_ID_I2S4_RX:
		return I2S_CLK_ID_I2S4;
	case AIO_ID_I2S5_TX:
	case AIO_ID_I2S5_RX:
		return I2S_CLK_ID_I2S5;
#endif //AVIO_MYNA2
	}

	return I2S_CLK_ID_INVALID;
}

static enum aud_clk_id map_clkId_Xfeed(enum i2s_xfeed_clk xfeed_clk)
{
	switch (xfeed_clk) {
	case AIO_I2S_XFEED_I2S1_DI:
	case AIO_I2S_XFEED_I2S1_LPBK:
		return I2S_CLK_ID_I2S1;
	case AIO_I2S_XFEED_I2S2_DI:
	case AIO_I2S_XFEED_I2S2_LPBK:
		return I2S_CLK_ID_I2S2;
	case AIO_I2S_XFEED_I2S3_DI:
	case AIO_I2S_XFEED_I2S3_LPBK:
		return I2S_CLK_ID_I2S3;
	case AIO_I2S_XFEED_I2S4_DI:
	case AIO_I2S_XFEED_I2S4_LPBK:
		return I2S_CLK_ID_I2S4;
	case AIO_I2S_XFEED_I2S5_DI:
	case AIO_I2S_XFEED_I2S5_LPBK:
		return I2S_CLK_ID_I2S5;
	}

	return I2S_CLK_ID_INVALID;
}

void i2s_soc_dai_set_bclk(struct i2s_soc_dai *soc_dai, u32 id, bool is_master, bool inv)
{
	int ret;

	/* BCLK generated from (MCLK) or external */
	ret = aio_set_bclk_sel(soc_dai->aio_handle, id,
			is_master ? AIO_I2S_BCLK_SRC_MCLK : AIO_I2S_BCLK_SRC_EXTERNAL);
	if (ret != 0)
		snd_printk("aio_set_bclk_sel() return error(ret=%d)\n", ret);

	ret = aio_set_bclk_inv(soc_dai->aio_handle, id, inv);
	if (ret != 0)
		snd_printk("aio_set_bclk_inv() return error(ret=%d)\n", ret);
}

/*
 * Applies output configuration of |berlin_pcm| to i2s.
 * Must be called with instance spinlock held.
 * Only one dai instance for playback, so no spin_lock needed
 */
static int i2s_soc_dai_set_aio(struct i2s_soc_dai *soc_dai, u32 id, u32 fs, int width, int chnum,
							   int stream)
{
	unsigned int cfm, dfm;
	unsigned int bclk;
	const struct mclk_info *mclk = NULL;
	struct aud_ctrl ctrl;

	/* Change AIO_24DFM to AIO_32DFM */
	dfm = berlin_get_sample_resolution((width == 24 ? 32 : width));

	/* Alghough h/w supports AIO_24CFM, but 24 is not multiples of 2.
	 * There could be some restriction on clock generation for certain
	 * frequency with AIO_24CFM. Change AIO_24CFM to AIO_32CFG instead
	 */
	cfm = berlin_get_sample_period_in_bclk(soc_dai->sample_period == 24 ?
						32 : soc_dai->sample_period);

	bclk = fs * soc_dai->sample_period * chnum;

	soc_dai->ctrl.fs = fs;
	soc_dai->ctrl.bclk = bclk;
	soc_dai->ctrl.chcnt	= chnum;
	soc_dai->ctrl.sample_resolution	= dfm;
	soc_dai->ctrl.sample_period_in_bclk	= cfm;
	soc_dai->ctrl.msb	= true;
	if (aio_i2s_get_mclk_cfg(soc_dai->ctrl.fs, soc_dai->sample_period, &mclk) || !mclk) {
		snd_printk("fail to use mclk config");
		return -EINVAL;
	}
	ctrl = soc_dai->ctrl;
	if ((stream == SNDRV_PCM_STREAM_PLAYBACK) && (ctrl.chcnt > 1) &&
		((ctrl.istdm == false) && (ctrl.data_fmt == 2))) {
		ctrl.invfs = !ctrl.invfs;
	}
	snd_printd("%s: chnum: %d i2s idx %d\n", __func__, chnum, id);
	aio_setclkdiv(soc_dai->aio_handle, id,
			berlin_get_bclk_div(mclk->mclkrate, soc_dai->ctrl.bclk));
	aio_set_ctl_ext(soc_dai->aio_handle, id, &ctrl);
	aio_set_pcm_mono(soc_dai->aio_handle, id, (soc_dai->ctrl.chcnt == 1));
	return 0;
}

static int i2s_soc_dai_startup(struct snd_pcm_substream *substream,
				 struct snd_soc_dai *dai)
{
	struct i2s_soc_dai *soc_dai = snd_soc_dai_get_drvdata(dai);
	struct i2s_io_params *i2s_ctrl = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) ?
		&soc_dai->io_params[AIO_I2S_IO_TX] : &soc_dai->io_params[AIO_I2S_IO_RX];

	snd_printd("chid %d start...\n", i2s_ctrl->i2s_chid);
	aio_set_extPortCfg(soc_dai->aio_handle, i2s_ctrl->i2s_chid,
						soc_dai->eAPRKTMode, soc_dai->bUsePauseResume);
	return 0;
}

static void i2s_soc_dai_shutdown(struct snd_pcm_substream *substream,
				   struct snd_soc_dai *dai)
{
	struct i2s_soc_dai *soc_dai = snd_soc_dai_get_drvdata(dai);
	struct i2s_io_params *i2s_ctrl = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) ?
		&soc_dai->io_params[AIO_I2S_IO_TX] : &soc_dai->io_params[AIO_I2S_IO_RX];

	snd_printd("chid %d shutdown...\n", i2s_ctrl->i2s_chid);
	aio_set_extPortCfg(soc_dai->aio_handle, i2s_ctrl->i2s_chid,
						AIO_I2S_APRKT_OFF, soc_dai->bUsePauseResume);
	aio_i2s_clk_sync_reset(soc_dai->aio_handle, i2s_ctrl->i2s_chid);
}

static int i2s_soc_dai_setfmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct i2s_soc_dai *soc_dai = snd_soc_dai_get_drvdata(dai);
	struct i2s_io_params *i2s_tx = &soc_dai->io_params[AIO_I2S_IO_TX];
	struct i2s_io_params *i2s_rx = &soc_dai->io_params[AIO_I2S_IO_RX];
	int ret = 0;

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		soc_dai->ctrl.data_fmt  = 2;
		soc_dai->ctrl.istdm    = false;
		soc_dai->ctrl.isleftjfy = true;   /* don't care if data_fmt = 2 */
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		soc_dai->ctrl.data_fmt  = 1;
		soc_dai->ctrl.istdm    = false;
		soc_dai->ctrl.isleftjfy = true;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		soc_dai->ctrl.data_fmt  = 1;
		soc_dai->ctrl.istdm    = false;
		soc_dai->ctrl.isleftjfy = false;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		soc_dai->ctrl.data_fmt  = 1;
		soc_dai->ctrl.istdm    = true;
		soc_dai->ctrl.isleftjfy = true;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		soc_dai->ctrl.data_fmt  = 2;
		soc_dai->ctrl.istdm    = true;
		soc_dai->ctrl.isleftjfy = true;  /* don't care if data_fmt = 2 */
		break;
	default:
		dev_err(dai->dev, "Unknown DAI format mask %x\n", fmt);
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		soc_dai->ctrl.invbclk = false;
		soc_dai->ctrl.invfs   = false;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		soc_dai->ctrl.invbclk = false;
		soc_dai->ctrl.invfs   = true;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		soc_dai->ctrl.invbclk = true;
		soc_dai->ctrl.invfs   = false;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		soc_dai->ctrl.invbclk = true;
		soc_dai->ctrl.invfs   = true;
		break;
	default:
		dev_err(soc_dai->dev, "Unknown DAI invert mask 0x%x\n", fmt);
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 0, 0)
	case SND_SOC_DAIFMT_BP_FP:
		soc_dai->is_master = true;
		break;
	case SND_SOC_DAIFMT_BC_FC:
		soc_dai->is_master = false;
		break;
#else
	case SND_SOC_DAIFMT_CBP_CFP: //codec provider
		soc_dai->is_master = false;
		break;
	case SND_SOC_DAIFMT_CBC_CFC: //codec consumer
		soc_dai->is_master = true;
		break;
#endif
	case SND_SOC_DAIFMT_CBP_CFC:
	case SND_SOC_DAIFMT_CBC_CFP:
	default:
		dev_err(dai->dev, "Berlin AIO do not support DAI master mask %x\n", fmt);
		return -EINVAL;
	}

	snd_printd("%s: chid %d data_fmt: %d isleftjfy: %d istdm: %d"
			"is_master: %d invbclk: %d invfs: %d sample_period: %d\n",
			__func__,
			i2s_tx->i2s_chid,
			soc_dai->ctrl.data_fmt, soc_dai->ctrl.isleftjfy,
			soc_dai->ctrl.istdm, soc_dai->is_master,
			soc_dai->ctrl.invbclk, soc_dai->ctrl.invfs,
			soc_dai->sample_period);

	if (soc_dai->output_mclk) {
		switch (i2s_tx->i2s_chid)
		{
		case AIO_ID_I2S1_TX:
		case AIO_ID_I2S1_RX:
			aio_set_i2s_clk_enable(soc_dai->aio_handle, AIO_I2S_I2S1_MCLK, 1);
			break;

		case AIO_ID_I2S2_TX:
		case AIO_ID_I2S2_RX:
			aio_set_i2s_clk_enable(soc_dai->aio_handle, AIO_I2S_I2S2_MCLK, 1);
			break;
		default:
			dev_err(dai->dev, "Invalid MCLK OUT i2s_chid=%u\n", i2s_tx->i2s_chid);
			break;
		}
	}

	aio_set_extPortCfg(soc_dai->aio_handle, i2s_tx->i2s_chid,
					soc_dai->eAPRKTMode, soc_dai->bUsePauseResume);
	aio_set_extPortCfg(soc_dai->aio_handle, i2s_rx->i2s_chid,
					soc_dai->eAPRKTMode, soc_dai->bUsePauseResume);
	aud_clk_add(soc_dai->nodeClkId, soc_dai);

	return ret;
}

static int i2s_soc_dai_hw_params(struct snd_pcm_substream *substream,
				   struct snd_pcm_hw_params *params,
				   struct snd_soc_dai *dai)
{
	struct i2s_soc_dai *soc_dai = snd_soc_dai_get_drvdata(dai);
	struct i2s_io_params *i2s_ctrl = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) ?
		&soc_dai->io_params[AIO_I2S_IO_TX] : &soc_dai->io_params[AIO_I2S_IO_RX];
	u32 fs = params_rate(params), chnum = params_channels(params);
	u32 mode = 0;
	int ret, width = params_width(params);
	struct berlin_ss_params ssparams = {};
	struct aud_clk *pClkrt = NULL;

	i2s_ctrl->clkrt = NULL;
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {

		if (!(soc_dai->mode & I2SO_MODE)) {
			dev_err(soc_dai->dev, "not in i2s tx mode, fatal error.\n");
			return -EINVAL;
		}

		if (soc_dai->is_master && !soc_dai->useMMMode) {
			mode = AIO_I2S_STATE_TX_MASTER;
			if (soc_dai->clkSrcSel == AIO_BCLK_SRC_XFEED) {
				//xfeed clk, clock node is choosen below
				mode = AIO_I2S_STATE_TX_MS;
			} else {
				/* use its own modules CLK provider as CLK master */
				pClkrt = aud_get_clk(soc_dai->nodeClkId);
			}
		} else {
			//slave to external/xfeed, CLK provider will be will be choosen if xfeed
			mode = AIO_I2S_STATE_TX_SLAVE;
			if (soc_dai->useMMMode)
				pClkrt = aud_get_clk(soc_dai->nodeClkId);
		}

		//slave to xfeed clk, aud_clk will not be configured
		if (soc_dai->clkSrcSel == AIO_BCLK_SRC_XFEED) {
			pClkrt = aud_get_clk(map_clkId_Xfeed(soc_dai->xFeedClkCfg));

			ret = aio_set_xfeed_mode(soc_dai->aio_handle, i2s_ctrl->i2s_chid,
								soc_dai->xFeedClkCfg, soc_dai->xFeedClkCfg);
			if (ret != 0) {
				dev_err(soc_dai->dev, "fail to set xfeed mode for i2s, ret %d\n", ret);
				return ret;
			}
		}
		aio_set_mode(soc_dai->aio_handle, i2s_ctrl->i2s_chid, mode, false, false);
		if (i2s_soc_dai_set_aio(soc_dai, i2s_ctrl->i2s_chid, fs, width, chnum,
								substream->stream)) {
			dev_err(soc_dai->dev, "params not supported, fatal error.\n");
			return -EINVAL;
		}
		i2s_soc_dai_set_bclk(soc_dai, i2s_ctrl->i2s_chid,
							soc_dai->is_master && !soc_dai->useMMMode,
							soc_dai->ctrl.invbclk ? true : false);
		if (pClkrt) {
			/* clk setting on xfeed clk configuration */
			i2s_ctrl->clkrt = pClkrt; //update selected clock root
			//i2s_soc_dai_set_aio() should get called before clk pClkrt->ops.config()
			ret = pClkrt->ops.config(substream);
			if (ret != 0) {
				dev_err(soc_dai->dev, "clk configuration failed %d\n", ret);
				return -EINVAL;
			}
		} else {
			aio_set_mode(soc_dai->aio_handle, i2s_ctrl->i2s_chid, mode, false, false);
			i2s_soc_dai_set_bclk(soc_dai, i2s_ctrl->i2s_chid,
								soc_dai->is_master && !soc_dai->useMMMode,
								soc_dai->ctrl.invbclk ? true : false);
			snd_printd("chid %d not using any internal clk\n", i2s_ctrl->i2s_chid);
		}

		ssparams.irq_num = 1;
		ssparams.chid_num = 1;
		ssparams.mode = I2SO_MODE;
		ssparams.irq = &i2s_ctrl->i2s_irq;
		ssparams.dev_name = soc_dai->dev_name;
		ret = berlin_pcm_request_dma_irq(substream, &ssparams);
		if (ret == 0)
			i2s_ctrl->irq_requested = true;
		else
			return ret;
	} else {
		if (!(soc_dai->mode & I2SI_MODE)) {
			dev_err(soc_dai->dev, "not in i2s rx mode, fatal error.\n");
			return -EINVAL;
		}

		//Always use clk for RX interface from xfeed configuration
		pClkrt = aud_get_clk(map_clkId_Xfeed(soc_dai->xFeedClkCfg));
		aio_set_loopback_clk_gate(soc_dai->aio_handle, i2s_ctrl->i2s_chid, false);

		if (soc_dai->is_master && soc_dai->useMMMode) {
			mode = AIO_I2S_STATE_RX_MASTER;
			if (soc_dai->clkSrcSel == AIO_BCLK_SRC_XFEED) {
				//xfeed clk, clock node is choosen below
				mode = AIO_I2S_STATE_RX_MS;
			} else {
				/* use its own modules CLK provider as CLK master */
				pClkrt = aud_get_clk(soc_dai->nodeClkId);
			}
		} else {
			//slave to external/xfeed, CLK provider will be will be choosen if xfeed
			mode = AIO_I2S_STATE_RX_SLAVE;
		}

		aio_set_mode(soc_dai->aio_handle, i2s_ctrl->i2s_chid, mode, soc_dai->useMMMode, soc_dai->rxDataMode);
		if (i2s_soc_dai_set_aio(soc_dai, i2s_ctrl->i2s_chid, fs, width, chnum,
								substream->stream)) {
			dev_err(soc_dai->dev, "params not supported, fatal error.\n");
			return -EINVAL;
		}

		if (soc_dai->rxDataMode == AIO_I2S_RX_DATA_LPBK_NEAR_PAD)
			aio_set_invclk(soc_dai->aio_handle, i2s_ctrl->i2s_chid, true);
		/* clk setting on xfeed clk configuration */
		if (pClkrt) {
			i2s_ctrl->clkrt = pClkrt; //update selected clock root
			ret = pClkrt->ops.config(substream);
			if (ret != 0) {
				dev_err(soc_dai->dev, "clk configuration failed %d\n", ret);
				return -EINVAL;
			}
		} else {
			snd_printd("chid %d not configured for any internal clk\n", i2s_ctrl->i2s_chid);
		}

		ret = aio_set_xfeed_mode(soc_dai->aio_handle, i2s_ctrl->i2s_chid,
							soc_dai->xFeedClkCfg, soc_dai->xFeedClkCfg);
		if (ret != 0) {
			snd_printk("fail to set xfeed mode for i2s, ret %d\n", ret);
			return ret;
		}

		ssparams.irq_num = 1;
		ssparams.chid_num = 1;
		ssparams.mode = I2SI_MODE;
		ssparams.interleaved = true;
		ssparams.dummy_data = 0;
		ssparams.enable_mic_mute = !soc_dai->disable_mic_mute;
		ssparams.irq = &i2s_ctrl->i2s_irq;
		ssparams.dev_name = soc_dai->dev_name;
		ret = berlin_pcm_request_dma_irq(substream, &ssparams);

		if (ret == 0)
			i2s_ctrl->irq_requested = true;
	}

	aio_set_aud_ch_mute(soc_dai->aio_handle, i2s_ctrl->i2s_chid, 0, false);
	//Disable flush operation for new stream request, to avoid flush new dma request
	aio_set_aud_ch_flush(soc_dai->aio_handle, i2s_ctrl->i2s_chid, 0, false);

	if (pClkrt) {
		snd_printd("hw ready, %s chid %d uses clk-chid %d\n",
				(substream->stream == SNDRV_PCM_STREAM_PLAYBACK) ? "TX" : "RX",
				i2s_ctrl->i2s_chid, pClkrt->clk_chid);
	} else {
		snd_printd("hw ready, %s chid %d not using any clk\n",
				(substream->stream == SNDRV_PCM_STREAM_PLAYBACK) ? "TX" : "RX",
				i2s_ctrl->i2s_chid);
	}
	return ret;
}

static int i2s_soc_dai_hw_free(struct snd_pcm_substream *substream,
				 struct snd_soc_dai *dai)
{
	struct i2s_soc_dai *soc_dai = snd_soc_dai_get_drvdata(dai);
	struct i2s_io_params *i2s_ctrl = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) ?
		&soc_dai->io_params[AIO_I2S_IO_TX] : &soc_dai->io_params[AIO_I2S_IO_RX];

	aio_set_aud_ch_mute(soc_dai->aio_handle, i2s_ctrl->i2s_chid, 0, true);
	aio_set_aud_ch_flush(soc_dai->aio_handle, i2s_ctrl->i2s_chid, 0, true);
	snd_printd("%s chid %d hw free\n", __func__, i2s_ctrl->i2s_chid);
	if (i2s_ctrl->irq_requested) {
		berlin_pcm_free_dma_irq(substream, 1, &i2s_ctrl->i2s_irq);
		i2s_ctrl->irq_requested = false;
	}

	return 0;
}

int i2s_soc_dai_run(void *i2s_hdl,
				bool isTx, enum aio_i2s_run_mode runMode, u32 uPSC)
{
	struct i2s_soc_dai *soc_dai = (struct i2s_soc_dai *)i2s_hdl;
	struct i2s_io_params *i2s_ctrl = isTx ?
			&soc_dai->io_params[AIO_I2S_IO_TX] : &soc_dai->io_params[AIO_I2S_IO_RX];

	snd_printd("%s runMode %d isPlayback %d chId %d current_state %d\n",
		__func__, runMode, isTx, i2s_ctrl->i2s_chid, i2s_ctrl->statePauseResume);

	/* Prepare clock provider interfaces for APRKT function that enables sync playback,
	 * sync playback decision happens as part of trigger start. In case of non-sync playback,
	 * this does not have any influence on AIO 
         */
	aud_set_aprkt_en_flag(true, (1 << i2s_ctrl->i2s_chid));

	switch (runMode) {
	case AIO_I2S_SET_START_ON_PAUSE:
		if (i2s_ctrl->statePauseResume == AIO_I2S_STATE_DISABLED) {
			aio_i2s_pause(soc_dai->aio_handle, i2s_ctrl->i2s_chid, true, 0);
			aio_en_scr_str(soc_dai->aio_handle, i2s_ctrl->i2s_chid, false);  //stop SCR and STR
			aio_en_scr_str(soc_dai->aio_handle, i2s_ctrl->i2s_chid, true);  //enable SCR and STR
			aio_set_aud_ch_mute(soc_dai->aio_handle, i2s_ctrl->i2s_chid, 0, false);
			aio_set_aud_ch_flush(soc_dai->aio_handle, i2s_ctrl->i2s_chid, 0, false);
			aio_set_aud_ch_en(soc_dai->aio_handle, i2s_ctrl->i2s_chid, 0, true);
			if (isTx)
				aio_enabletxport(soc_dai->aio_handle, i2s_ctrl->i2s_chid, true);
			else
				aio_enablerxport(soc_dai->aio_handle, i2s_ctrl->i2s_chid, true);
			i2s_ctrl->statePauseResume = AIO_I2S_STATE_PAUSE;
		} else {
			snd_printd("%s chId %d already started, state %d\n",
				__func__, i2s_ctrl->i2s_chid, i2s_ctrl->statePauseResume);
		}
	break;
	case AIO_I2S_SET_PAUSE:
		if (i2s_ctrl->statePauseResume == AIO_I2S_STATE_ACTIVE) {
			aio_i2s_pause(soc_dai->aio_handle, i2s_ctrl->i2s_chid, true, uPSC);
			i2s_ctrl->statePauseResume = AIO_I2S_STATE_PAUSE;
			snd_printd("%s chId %d pause at 0x%X samples current 0x%X samples\n",
					__func__, i2s_ctrl->i2s_chid, uPSC,
					aio_get_scr(soc_dai->aio_handle, i2s_ctrl->i2s_chid));
		} else {
			snd_printd("%s chId %d not started yet, please start before pause\n",
				__func__, i2s_ctrl->i2s_chid);
		}
	break;
	case AIO_I2S_SET_START:
		if (i2s_ctrl->statePauseResume == AIO_I2S_STATE_DISABLED) {
			aio_en_scr_str(soc_dai->aio_handle, i2s_ctrl->i2s_chid, false);
			aio_en_scr_str(soc_dai->aio_handle, i2s_ctrl->i2s_chid, true);
			aio_set_aud_ch_mute(soc_dai->aio_handle, i2s_ctrl->i2s_chid, 0, false);
			aio_set_aud_ch_flush(soc_dai->aio_handle, i2s_ctrl->i2s_chid, 0, false);
			aio_set_aud_ch_en(soc_dai->aio_handle, i2s_ctrl->i2s_chid, 0, true);
			if (isTx)
				aio_enabletxport(soc_dai->aio_handle, i2s_ctrl->i2s_chid, true);
			else
				aio_enablerxport(soc_dai->aio_handle, i2s_ctrl->i2s_chid, true);
			i2s_ctrl->statePauseResume = AIO_I2S_STATE_ACTIVE;
		} else {
			snd_printd("%s chId %d already started, state %d\n",
				__func__, i2s_ctrl->i2s_chid, i2s_ctrl->statePauseResume);
		}
	break;
	case AIO_I2S_SET_RESUME:
		if (i2s_ctrl->statePauseResume == AIO_I2S_STATE_PAUSE) {
			//restart SCR and STR
			aio_en_scr_str(soc_dai->aio_handle, i2s_ctrl->i2s_chid, false);
			aio_en_scr_str(soc_dai->aio_handle, i2s_ctrl->i2s_chid, true);
			aio_i2s_pause(soc_dai->aio_handle, i2s_ctrl->i2s_chid, false, 0);
			i2s_ctrl->statePauseResume = AIO_I2S_STATE_ACTIVE;
		} else {
			snd_printd("%s chId %d already started, state %d\n",
				__func__, i2s_ctrl->i2s_chid, i2s_ctrl->statePauseResume);
		}
	break;
	case AIO_I2S_SET_STOP:
		if (i2s_ctrl->statePauseResume != AIO_I2S_STATE_DISABLED) {
			if (isTx)
				aio_enabletxport(soc_dai->aio_handle, i2s_ctrl->i2s_chid, false);
			else
				aio_enablerxport(soc_dai->aio_handle, i2s_ctrl->i2s_chid, false);

			aio_set_aud_ch_mute(soc_dai->aio_handle, i2s_ctrl->i2s_chid, 0, true);
			//set flush before DMA trigger stop, as channel clear expects
			aio_set_aud_ch_flush(soc_dai->aio_handle, i2s_ctrl->i2s_chid, 0, true);
			aio_set_aud_ch_en(soc_dai->aio_handle, i2s_ctrl->i2s_chid, 0, false);
			i2s_ctrl->statePauseResume = AIO_I2S_STATE_DISABLED;
		}
	break;
	}

	snd_printd("%s PR new_state %d IRQSTS %08X\n",
			__func__,
			i2s_ctrl->statePauseResume,
			aio_getirqsts(soc_dai->aio_handle));

	return 0;
}
EXPORT_SYMBOL(i2s_soc_dai_run);

static int i2s_soc_dai_trigger(struct snd_pcm_substream *substream,
				 int cmd, struct snd_soc_dai *dai)
{
	struct i2s_soc_dai *soc_dai = snd_soc_dai_get_drvdata(dai);
	struct snd_pcm_substream *s;
	int stream_linked = 0;
	bool stop = false;

	snd_pcm_group_for_each_entry(s, substream) {
		struct snd_soc_pcm_runtime *rtd = snd_pcm_substream_chip(s);
		struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
		struct i2s_soc_dai *soc_dai = snd_soc_dai_get_drvdata(cpu_dai);
		bool isTX = (s->stream == SNDRV_PCM_STREAM_PLAYBACK);
		struct i2s_io_params *i2s_ctrl = isTX ?
			&soc_dai->io_params[AIO_I2S_IO_TX] : &soc_dai->io_params[AIO_I2S_IO_RX];
		struct aud_clk *pClkrt = i2s_ctrl->clkrt; //choose TX/RX clk root

		switch (cmd) {
		case SNDRV_PCM_TRIGGER_START:
		case SNDRV_PCM_TRIGGER_RESUME:
		case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
			//master / MS / xfeed will act on clk root
			if (pClkrt) {
				snd_printd("%s trigger start chid %d act on CLK chid %d\n",
					__func__, i2s_ctrl->i2s_chid, pClkrt->clk_chid);
				pClkrt->ops.start(s);
				if (!pClkrt->ops.isInUse(s)) {
					i2s_soc_dai_run(soc_dai, isTX, AIO_I2S_SET_START, 0);
				} else {
					/* if same TX & clk are same then need to resume as its started in pause mode*/
					i2s_soc_dai_run(soc_dai, isTX, AIO_I2S_SET_RESUME, 0);
				}
			} else {
				snd_printd("%s trigger start chid %d without CLK root\n",
					__func__, i2s_ctrl->i2s_chid);
				//external slave will simply starts interface
				i2s_soc_dai_run(soc_dai, isTX, AIO_I2S_SET_START, 0);
			}
			break;
		case SNDRV_PCM_TRIGGER_STOP:
		case SNDRV_PCM_TRIGGER_SUSPEND:
		case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
			stop = true;
			if (pClkrt) {
				snd_printd("%s trigger stop chid %d act on CLK chid %d\n",
					__func__, i2s_ctrl->i2s_chid, pClkrt->clk_chid);
				if (!pClkrt->ops.isInUse(s)) {
					i2s_soc_dai_run(soc_dai,
							isTX,
							AIO_I2S_SET_STOP, 0);
				}
				pClkrt->ops.stop(s);
			} else {
				snd_printd("%s trigger stop chid %d without CLK root\n",
					__func__, i2s_ctrl->i2s_chid);
				//external slave will simply stops the interface
				i2s_soc_dai_run(soc_dai,
									isTX,
									AIO_I2S_SET_STOP, 0);
			}
			break;
		default:
			return -EINVAL;
		}
		++stream_linked;
		snd_printd("%s i2s_chid %d stream %d syncPlay 0x%08X cmd %d PR state %d\n",
					__func__,
					i2s_ctrl->i2s_chid,
					stream_linked,
					aud_get_aprkt_en_flag(), cmd,
					i2s_ctrl->statePauseResume);
		snd_pcm_trigger_done(s, substream);
	}

	/*Sync playback start/stop for linked PCM streams */
	if (soc_dai->eAPRKTMode != AIO_I2S_APRKT_OFF)
		aio_set_sync_play(soc_dai->aio_handle, aud_get_aprkt_en_flag(), !stop);

	aud_set_aprkt_en_flag(false, 0);

	return 0;
}

static int i2s_soc_dai_probe(struct snd_soc_dai *dai)
{
	struct i2s_soc_dai *soc_dai = snd_soc_dai_get_drvdata(dai);
	struct i2s_io_params *i2s_tx = &soc_dai->io_params[AIO_I2S_IO_TX];

	switch (i2s_tx->i2s_chid) {
	case AIO_ID_I2S1_TX:
		snd_soc_add_dai_controls(dai, syna_i2s1_controls,
				ARRAY_SIZE(syna_i2s1_controls));
	break;
	case AIO_ID_I2S2_TX:
		snd_soc_add_dai_controls(dai, syna_i2s2_controls,
				ARRAY_SIZE(syna_i2s2_controls));
	break;
	case AIO_ID_I2S3_TX:
		snd_soc_add_dai_controls(dai, syna_i2s3_controls,
				ARRAY_SIZE(syna_i2s3_controls));
	break;
#ifdef AVIO_MYNA2
	case AIO_ID_I2S4_TX:
		snd_soc_add_dai_controls(dai, syna_i2s4_controls,
				ARRAY_SIZE(syna_i2s4_controls));
	break;
	case AIO_ID_I2S5_TX:
		snd_soc_add_dai_controls(dai, syna_i2s5_controls,
				ARRAY_SIZE(syna_i2s5_controls));
	break;
#endif
	default:
		snd_printd("%s error chid %d\n", __func__, i2s_tx->i2s_chid);
	}

	return 0;
}

static struct snd_soc_dai_ops syna_soc_dai_ops = {
	.startup   = i2s_soc_dai_startup,
	.set_fmt   = i2s_soc_dai_setfmt,
	.hw_params = i2s_soc_dai_hw_params,
	.hw_free   = i2s_soc_dai_hw_free,
	.trigger   = i2s_soc_dai_trigger,
	.shutdown  = i2s_soc_dai_shutdown,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0))
	.probe 	   = i2s_soc_dai_probe,
#endif
};

static struct snd_soc_dai_driver i2s_soc_dai_drv = {
	.name = "i2s_tx_rx",
#if (LINUX_VERSION_CODE < KERNEL_VERSION(6, 12, 0))
	.probe     = i2s_soc_dai_probe,
#endif
	.playback = {
		.stream_name = "I2S-Playback",
		.channels_min = 1,
		.channels_max = 8,
		.rates = I2S_PLAYBACK_RATES,
		.formats = I2S_PLAYBACK_FORMATS,
	},
	.capture = {
		.stream_name = "I2S-Capture",
		.channels_min = 1,
		.channels_max = 8,
		.rates = I2S_PLAYBACK_RATES,
		.formats = I2S_PLAYBACK_FORMATS,
	},
	.ops = &syna_soc_dai_ops,
};

static struct snd_soc_dai_driver syna_play_dai = {
	.name = "i2s_tx",
#if (LINUX_VERSION_CODE < KERNEL_VERSION(6, 12, 0))
	.probe     = i2s_soc_dai_probe,
#endif
	.playback = {
		.stream_name = "I2S-Playback",
		.channels_min = 1,
		.channels_max = 8,
		.rates = I2S_PLAYBACK_RATES,
		.formats = I2S_PLAYBACK_FORMATS,
	},
	.ops = &syna_soc_dai_ops,
};

static struct snd_soc_dai_driver syna_record_dai = {
	.name = "i2s_rx",
#if (LINUX_VERSION_CODE < KERNEL_VERSION(6, 12, 0))
	.probe     = i2s_soc_dai_probe,
#endif
	.capture = {
		.stream_name = "I2S-Capture",
		.channels_min = 1,
		.channels_max = 8,
		.rates = I2S_PLAYBACK_RATES,
		.formats = I2S_PLAYBACK_FORMATS,
	},
	.ops = &syna_soc_dai_ops,
};

static const struct snd_soc_component_driver i2s_soc_dai_component = {
	.name = "aio-i2s",
};

static int syna_i2s_soc_dai_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct i2s_soc_dai *soc_dai;
	struct i2s_io_params *i2s_tx, *i2s_rx;
	struct irq_data *irqd;
	const char *prop_val;
	int irq, ret;
	bool isLpbk = false;

	//Defer probe until dependent soc module/s are probed/initialized
	if (!is_avio_driver_initialized())
		return -EPROBE_DEFER;

	soc_dai = devm_kzalloc(dev, sizeof(struct i2s_soc_dai),
			      GFP_KERNEL);
	if (!soc_dai)
		return -ENOMEM;

	soc_dai->dev_name = dev_name(dev);
	soc_dai->dev = dev;
	i2s_tx = &soc_dai->io_params[AIO_I2S_IO_TX];
	i2s_rx = &soc_dai->io_params[AIO_I2S_IO_RX];

	/*open aio handle for alsa*/
	soc_dai->aio_handle = open_aio(soc_dai->dev_name);
	if (unlikely(soc_dai->aio_handle == NULL)) {
		snd_printk("aio_handle:%p  get failed\n", soc_dai->aio_handle);
		return -EBUSY;
	}
	dev_set_drvdata(dev, soc_dai);
	aio_enable_audio_timer(soc_dai->aio_handle, true);

	irq = platform_get_irq_byname(pdev, "tx");
	if (irq < 0) {
		ret = irq;
		goto err;
	}

	soc_dai->mode |= I2SO_MODE;
	i2s_tx->i2s_irq = irq;
	irqd = irq_get_irq_data(irq);
	if (!irqd) {
		ret = -EINVAL;
		goto err;
	}
	i2s_tx->i2s_chid = irqd_to_hwirq(irqd);
	soc_dai->nodeClkId = map_aio_clkId(i2s_tx->i2s_chid);
	soc_dai->xFeedClkCfg = aio_get_bclk_xfeed_mode(soc_dai->aio_handle, i2s_tx->i2s_chid);
	snd_printd("tx irq %d irq_child %d for node %s\n", irq, i2s_tx->i2s_chid, pdev->name);

	irq = platform_get_irq_byname(pdev, "rx");
	if (irq < 0) {
		ret = irq;
		goto err;
	}

	soc_dai->mode |= I2SI_MODE;
	i2s_rx->i2s_irq = irq;
	irqd = irq_get_irq_data(irq);
	if (!irqd) {
		ret = -EINVAL;
		goto err;
	}
	i2s_rx->i2s_chid = irqd_to_hwirq(irqd);
	soc_dai->nodeClkId = map_aio_clkId(i2s_rx->i2s_chid);
	soc_dai->xFeedClkCfg = aio_get_bclk_xfeed_mode(soc_dai->aio_handle, i2s_rx->i2s_chid);
	snd_printd("rx irq %d irq_child %d for node %s\n", irq, i2s_rx->i2s_chid, pdev->name);

	ret = of_property_read_string(np, I2S_CLK_SRC_KCONTROL_NAME, &prop_val);
	if (!ret) {
		int lp;

		soc_dai->clkSrcSel = AIO_BCLK_SRC_APLL;
		for (lp = 0; lp < ARRAY_SIZE(syna_i2s_bclk_src_text); ++lp) {
			if (strcmp(prop_val, syna_i2s_bclk_src_text[lp]) == 0) {
				soc_dai->clkSrcSel = lp;
				break;
			}
		}
	} else {
		soc_dai->clkSrcSel = AIO_BCLK_SRC_APLL;
	}

	ret = of_property_read_string(np, I2S_XFEED_CLK_KCONTROL_NAME, &prop_val);
	if (!ret) {
		int lp;

		for (lp = 0; lp < ARRAY_SIZE(syna_i2s_xfeed_clk_text); ++lp) {
			if (strcmp(prop_val, syna_i2s_xfeed_clk_text[lp]) == 0) {
				soc_dai->xFeedClkCfg = lp;
				break;
			}
		}
	}

	soc_dai->bUsePauseResume = false; /* APRPR playback ON/OFF */
	i2s_tx->statePauseResume = AIO_I2S_STATE_DISABLED;
	i2s_rx->statePauseResume = AIO_I2S_STATE_DISABLED;

	if (soc_dai->mode == (I2SO_MODE|I2SI_MODE)) {
		aio_enabletxport(soc_dai->aio_handle, i2s_tx->i2s_chid, false);
		aio_set_aud_ch_en(soc_dai->aio_handle, i2s_tx->i2s_chid, 0, false);
		aio_enablerxport(soc_dai->aio_handle, i2s_rx->i2s_chid, false);
		aio_set_aud_ch_en(soc_dai->aio_handle, i2s_rx->i2s_chid, 0, false);
		ret = devm_snd_soc_register_component(dev,
					      &i2s_soc_dai_component,
					      &i2s_soc_dai_drv, 1);
	} else if (soc_dai->mode & I2SI_MODE) {
		aio_enablerxport(soc_dai->aio_handle, i2s_rx->i2s_chid, false);
		aio_set_aud_ch_en(soc_dai->aio_handle, i2s_rx->i2s_chid, 0, false);
		/* Register only I2S RX */
		ret = devm_snd_soc_register_component(dev,
					      &i2s_soc_dai_component,
					      &syna_record_dai, 1);
	} else if (soc_dai->mode & I2SO_MODE) {
		aio_enabletxport(soc_dai->aio_handle, i2s_tx->i2s_chid, false);
		aio_set_aud_ch_en(soc_dai->aio_handle, i2s_tx->i2s_chid, 0, false);
		/* Register only I2S TX */
		ret = devm_snd_soc_register_component(dev,
					      &i2s_soc_dai_component,
					      &syna_play_dai, 1);
	} else {
		snd_printk("non valid irq found in dts\n");
		ret = -EINVAL;
		goto err;
	}

	if (ret) {
		snd_printk("failed to register DAI: %d\n", ret);
		goto err;
	}

	soc_dai->disable_mic_mute = of_property_read_bool(np, "disablemicmute");

	soc_dai->rxDataMode = AIO_I2S_RX_DATA_IN_PAD;
	isLpbk = of_property_read_bool(np, I2S_LPBK_KCONTROL_NAME);
	if (isLpbk)
		soc_dai->rxDataMode = AIO_I2S_RX_DATA_LPBK_NEAR_PAD;

	soc_dai->useMMMode = of_property_read_bool(np, I2S_USE_MM_MODE_KCONTROL_NAME);
	soc_dai->output_mclk = of_property_read_bool(np, "output-mclk");
	soc_dai->ctrl.islframe = of_property_read_bool(np, "long-frame");

	if (of_property_read_bool(np, "sync-play"))
		soc_dai->eAPRKTMode = AIO_I2S_APRKT_ON_ASAP;
	else
		soc_dai->eAPRKTMode = AIO_I2S_APRKT_OFF;

	ret = of_property_read_u32(np, "sample-period", &soc_dai->sample_period);
	if (ret)
		soc_dai->sample_period = 32;

	snd_printd("%s: done i2s tx chid %d rx chid %d\n", __func__,
		   i2s_tx->i2s_chid, i2s_rx->i2s_chid);
	return 0;
err:
	close_aio(soc_dai->aio_handle);
	soc_dai->aio_handle = NULL;
	return ret;
}

static RET_TYPE syna_i2s_soc_dai_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct i2s_soc_dai *soc_dai;

	soc_dai = (struct i2s_soc_dai *)dev_get_drvdata(dev);

	/*close aio handle of alsa if have opened*/
	if (soc_dai && soc_dai->aio_handle) {
		aud_clk_remove(soc_dai->nodeClkId);
		close_aio(soc_dai->aio_handle);
		soc_dai->aio_handle = NULL;
	}

	RETURN_VALUE;
}

static const struct of_device_id syna_i2s_dai_dt_ids[] = {
	{ .compatible = "syna,myna-i2s-soc",  },
	{ .compatible = "syna,myna2-i2s-soc",  },
	{ .compatible = "syna,klamath-i2s-soc",  },
	{}
};
MODULE_DEVICE_TABLE(of, syna_i2s_dai_dt_ids);

#ifdef CONFIG_PM_SLEEP

static int berlin_i2s_dai_suspend(struct device *dev)
{
	struct i2s_soc_dai *soc_dai = (struct i2s_soc_dai *)dev_get_drvdata(dev);
	struct i2s_io_params *i2s_tx = &soc_dai->io_params[AIO_I2S_IO_TX];
	struct i2s_io_params *i2s_rx = &soc_dai->io_params[AIO_I2S_IO_RX];

	if (soc_dai->mode & I2SI_MODE) {
		snd_printd("%s i2s_rx %d\n", __func__,  i2s_rx->i2s_chid);
		aio_set_extPortCfg(soc_dai->aio_handle, i2s_rx->i2s_chid,
						AIO_I2S_APRKT_OFF, soc_dai->bUsePauseResume);
		aio_enablerxport(soc_dai->aio_handle, i2s_rx->i2s_chid, false);
		aio_i2s_mclk_ctrl(soc_dai->aio_handle, i2s_rx->i2s_chid, false);
	}
	if (soc_dai->mode & I2SO_MODE) {
		snd_printd("%s i2s_tx %d \n", __func__, i2s_tx->i2s_chid);
			aio_set_extPortCfg(soc_dai->aio_handle, i2s_tx->i2s_chid,
						AIO_I2S_APRKT_OFF, soc_dai->bUsePauseResume);
		aio_enabletxport(soc_dai->aio_handle, i2s_tx->i2s_chid, false);
		aio_i2s_mclk_ctrl(soc_dai->aio_handle, i2s_tx->i2s_chid, false);
	}

	return 0;
}

static int berlin_i2s_dai_resume(struct device *dev)
{
	struct i2s_soc_dai *soc_dai = (struct i2s_soc_dai *)dev_get_drvdata(dev);
	struct i2s_io_params *i2s_tx = &soc_dai->io_params[AIO_I2S_IO_TX];
	struct i2s_io_params *i2s_rx = &soc_dai->io_params[AIO_I2S_IO_RX];

	/* suspend operation reversal of txport and mclk is not done as part of resume.
	 * It will be taken care by the driver during next stream resume/start.
	 */
	if (soc_dai->mode & I2SI_MODE) {
		snd_printd("%s i2s_rx %d\n", __func__,  i2s_rx->i2s_chid);
		aio_set_extPortCfg(soc_dai->aio_handle, i2s_rx->i2s_chid,
					soc_dai->eAPRKTMode, soc_dai->bUsePauseResume);
	}
	if (soc_dai->mode & I2SO_MODE) {
		snd_printd("%s i2s_tx %d \n", __func__, i2s_tx->i2s_chid);
			aio_set_extPortCfg(soc_dai->aio_handle, i2s_tx->i2s_chid,
					soc_dai->eAPRKTMode, soc_dai->bUsePauseResume);
	}
	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(berlin_i2s_dai_pmops, berlin_i2s_dai_suspend,
			 berlin_i2s_dai_resume);

static struct platform_driver syna_i2s_dai_driver = {
	.probe = syna_i2s_soc_dai_probe,
	.remove = syna_i2s_soc_dai_remove,
	.driver = {
		.name = "syna-i2s-soc",
		.of_match_table = syna_i2s_dai_dt_ids,
		.pm = &berlin_i2s_dai_pmops,
	},
};
module_platform_driver(syna_i2s_dai_driver);

MODULE_DESCRIPTION("Synaptics AIO I2S SoC dai");
MODULE_ALIAS("platform:aio-i2s-soc");
MODULE_LICENSE("GPL v2");
