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


/*list of irqn and chid to support 16ch TDM */
struct i2s_ch_irq_priv {
	u32  irqn[2];
	u32  chid[2];
	u32  i2s_id[2];
	u32 irqc;
};

struct i2s_dai_priv {
	struct device *dev;
	const char *dev_name;
	u32 mode;
	struct aud_ctrl ctrl;
	bool i2s_requested;
	u32 active_tdms;
	bool is_master;
	bool continuous_clk;
	bool output_mclk;
	bool xfeed;
	u32 xfeed_chid; /* xfeed channel id */
	/*  sample_period: sample period in terms of bclk numbers.
	 *  Typically 32 is used. For some pcm mono format, 16 may be used
	 */
	int  sample_period;
	u32 fs;
	void *aio_handle;
	struct i2s_ch_irq_priv io_params[AIO_I2S_IO_MAX];
	int stream_dir;
};

static void i2s_ch_flush(void *aio_handle, struct i2s_ch_irq_priv *i2s_ctrl, bool en, u32 start)
{
	int i;

	for (i = start; i < i2s_ctrl->irqc; i++)
		aio_set_aud_ch_flush(aio_handle, i2s_ctrl->i2s_id[i], AIO_TSD0, en);
}

static void i2s_ch_en(void *aio_handle, struct i2s_ch_irq_priv *i2s_ctrl, bool en, u32 start)
{
	int i;

	for (i = start; i < i2s_ctrl->irqc; i++)
		aio_set_aud_ch_en(aio_handle, i2s_ctrl->i2s_id[i], AIO_TSD0, en);
}

static void i2s_ch_mute(void *aio_handle, struct i2s_ch_irq_priv *i2s_ctrl, bool en, u32 start)
{
	int i;

	for (i = start; i < i2s_ctrl->irqc; i++)
		aio_set_aud_ch_mute(aio_handle, i2s_ctrl->i2s_id[i], AIO_TSD0, en);
}

static void i2s_set_ctl(void *aio_handle, struct i2s_ch_irq_priv *i2s_ctrl, struct aud_ctrl *ctrl, u32 start)
{
	int ret, i;

	for (i = start; i < i2s_ctrl->irqc; i++) {
		ret = aio_set_ctl_ext(aio_handle, i2s_ctrl->i2s_id[i], ctrl);
		if (ret != 0)
			snd_printk("aio_set_ctl_ext() return error(ret=%d)\n", ret);
	}
}

static void i2s_set_clk_div(void *aio_handle, struct i2s_ch_irq_priv *i2s_ctrl, u32 div, u32 start)
{
	int ret;
	int i;

	for (i = start; i < i2s_ctrl->irqc; i++) {
		if (i2s_ctrl->i2s_id[i] == AIO_ID_PRI_TX) {
			ret = aio_setclkdiv(aio_handle, i2s_ctrl->i2s_id[i], div);
			if (ret != 0)
				snd_printk("aio_setclkdiv() return error(ret=%d)\n", ret);
		}
	}
}

static void i2s_enable_txport(void *aio_handle, struct i2s_ch_irq_priv *i2s_ctrl, bool en, u32 start)
{
	int ret;
	int i;

	for (i = start; i < i2s_ctrl->irqc; i++) {
		ret = aio_enabletxport(aio_handle, i2s_ctrl->i2s_id[i], en);
		if (ret != 0)
			snd_printk("aio_enabletxport() return error(ret=%d)\n", ret);
	}
}

static void i2s_enable_rxport(void *aio_handle, struct i2s_ch_irq_priv *i2s_ctrl, bool en, u32 start)
{
	int ret;
	int i;

	for (i = start; i < i2s_ctrl->irqc; i++) {
		ret = aio_enablerxport(aio_handle, i2s_ctrl->i2s_id[i], en);
		if (ret != 0)
			snd_printk("aio_enablerxport() return error(ret=%d)\n", ret);
	}
}

/*
 * Called synchronously from berlin_playback_trigger_stop after DHub
 * channel clear.  Disables the TX/RX port and flushes the AIO FIFO
 * now that DMA is stopped and safe to flush without deadlock.
 * Flush is immediately deasserted so the FIFO is ready for the
 * next trigger start.
 */
static void i2s_port_disable_cb(void *data)
{
	struct i2s_dai_priv *i2s_dai = data;
	struct i2s_ch_irq_priv *i2s_ctrl = &i2s_dai->io_params[AIO_I2S_IO_TX];
	u32 start = i2s_ctrl->irqc - i2s_dai->active_tdms;

	if (!i2s_dai->continuous_clk) {
		if (i2s_dai->stream_dir == SNDRV_PCM_STREAM_PLAYBACK)
			i2s_enable_txport(i2s_dai->aio_handle, i2s_ctrl, 0, start);
		else if (i2s_dai->stream_dir == SNDRV_PCM_STREAM_CAPTURE)
			i2s_enable_rxport(i2s_dai->aio_handle, i2s_ctrl, 0, start);
	}

	i2s_ch_flush(i2s_dai->aio_handle, i2s_ctrl, 1, start);
	udelay(1); /* Safe to ensure flush assert and then deassert */
	i2s_ch_flush(i2s_dai->aio_handle, i2s_ctrl, 0, start);
}

static struct snd_kcontrol_new berlin_i2s_ctrls[] = {
	//TODO: add dai control here
};


static void i2s_iosel_set_fsync(void *aio_handle, struct i2s_ch_irq_priv *i2s_ctrl,
								bool sel, bool inv, u32 start)
{
	int ret, i;

	for (i = start; i < i2s_ctrl->irqc; i++) {
		if (i2s_ctrl->i2s_id[i] == AIO_ID_PRI_TX) {
			ret = aio_set_fsync(aio_handle, i2s_ctrl->i2s_id[i], sel, inv);
			if (ret != 0)
				snd_printk("aio_set_fsync(sel=%d, inv=%d) error(ret=%d)\n",
					sel, inv, ret);
		} else if (i2s_ctrl->i2s_id[i] == AIO_ID_SEC_TX) {
			ret = aio_set_fsync(aio_handle, i2s_ctrl->i2s_id[i], 0, inv);
			if (ret != 0)
				snd_printk("aio_set_fsync(sel=%d, inv=%d) error(ret=%d)\n",
					sel, inv, ret);
		}
	}
}

static void i2s_iosel_set_bclk(void *aio_handle, struct i2s_ch_irq_priv *i2s_ctrl,
			   bool en, bool inv, u32 start)
{
	int ret, i;

	for (i = start; i < i2s_ctrl->irqc; i++) {
		/* BCLK generated from (MCLK)
		 * 0: Bit clock is from external
		 * 1: Bit clock generated internally using Master Clock (MCLK) (default)
		 */
		if (i2s_ctrl->i2s_id[i] == AIO_ID_PRI_TX) {
			ret = aio_set_bclk_sel(aio_handle, i2s_ctrl->i2s_id[i], en);
			if (ret != 0)
				snd_printk("aio_set_bclk_sel() return error(ret=%d)\n", ret);

			ret = aio_set_bclk_inv(aio_handle, i2s_ctrl->i2s_id[i], inv);
			if (ret != 0)
				snd_printk("aio_set_bclk_inv() return error(ret=%d)\n", ret);

			aio_set_xfeed_mode(aio_handle, i2s_ctrl->i2s_id[i], 0, 0);
			aio_set_slave_mode(aio_handle, i2s_ctrl->i2s_id[i], 0);
		} else if (i2s_ctrl->i2s_id[i] == AIO_ID_SEC_TX) {
			ret = aio_set_bclk_sel(aio_handle, i2s_ctrl->i2s_id[i], 0);
			if (ret != 0)
				snd_printk("aio_set_bclk_sel() return error(ret=%d)\n", ret);

			ret = aio_set_bclk_inv(aio_handle, i2s_ctrl->i2s_id[i], inv);
			if (ret != 0)
				snd_printk("aio_set_bclk_inv() return error(ret=%d)\n", ret);
			aio_set_xfeed_mode(aio_handle, i2s_ctrl->i2s_id[i], 1, 1);
			aio_set_slave_mode(aio_handle, i2s_ctrl->i2s_id[i], 1);
			aio_set_i2s_clk_enable(aio_handle, AIO_I2S_I2S3_BCLK, 0);
			aio_set_i2s_clk_enable(aio_handle, AIO_I2S_I2S3_LRCK, 0);
		}
	}
}

static void i2s_set_mclk_src(void *aio_handle, struct i2s_ch_irq_priv *i2s_ctrl,
				u32 d3_switch, u32 div, u32 pllUsed, bool en, u32 start)
{
	int ret, i;

	for (i = start; i < i2s_ctrl->irqc; i++) {
		/* mclk select */
		if (i2s_ctrl->i2s_id[i] == AIO_ID_PRI_TX) {
			ret = aio_i2s_set_clock(aio_handle, i2s_ctrl->i2s_id[i], 1,
				d3_switch, div, pllUsed, en);
			if (ret != 0)
				snd_printk("aio_i2s_set_clock() return error(ret=%d)\n", ret);

		} else if (i2s_ctrl->i2s_id[i] == AIO_ID_SEC_TX) {
			ret = aio_i2s_set_clock(aio_handle, i2s_ctrl->i2s_id[i], 1,
				d3_switch, div, pllUsed, 0);
			if (ret != 0)
				snd_printk("aio_i2s_set_clock() return error(ret=%d)\n", ret);
		}
	}
}

static void mic_mode_sel(void *aio_handle, struct i2s_ch_irq_priv *i2s_ctrl)
{
	int tsd, i;

	for (i = 0; i < i2s_ctrl->irqc; i++) {
		tsd = aio_get_tsd_from_chid(aio_handle, i2s_ctrl->i2s_id[i]);
		if (tsd < MAX_TSD)
			aio_set_pdmmicsel(aio_handle, 1);
	}
}

static void mic_set_mm_mode(void *aio_handle, struct i2s_ch_irq_priv *i2s_ctrl, bool en)
{
	int ret, i;

	for (i = 0; i < i2s_ctrl->irqc; i++) {
		ret = aio_set_mic_mm_mode(aio_handle,  i2s_ctrl->i2s_id[i], en);
		if (ret != 0)
			snd_printk("aio_set_mic_mm_mode(en=%d) error(ret=%d)\n", en, ret);
	}
}

static void mic_set_ws_prd(void *aio_handle, struct i2s_ch_irq_priv *i2s_ctrl,
			u32 highP, u32 totalP, u32 wsInv)
{
	int ret, i;

	for (i = 0; i < i2s_ctrl->irqc; i++) {
		ret = aio_set_mic_ws_prd(aio_handle, i2s_ctrl->i2s_id[i], highP, totalP, wsInv);
		if (ret != 0)
			snd_printk("aio_set_mic_ws_prd(highP=%d, totalP=%d, wsInv=%d) error(ret=%d)\n",
					highP, totalP, wsInv, ret);
	}
}

/*
 * Round channel count up to next power-of-2 TDM slot count.
 * TDM hardware requires slot counts that produce integer BCLK dividers.
 */
static u32 roundup_tdm_slots(u32 ch)
{
	if (ch <= 2) return 2;
	if (ch <= 4) return 4;
	return 8;
}

/*
 * Applies output configuration of |berlin_pcm| to i2s.
 * Must be called with instance spinlock held.
 * Only one dai instance for playback, so no spin_lock needed
 */
static void i2s_set_aio(struct i2s_dai_priv *i2s_dai, int stream,
			   u32 fs, int width, int chnum, u32 mclkrate,
			   u32 active_tdms)
{
	struct i2s_ch_irq_priv *i2s_ctrl = (stream == SNDRV_PCM_STREAM_PLAYBACK) ?
		&i2s_dai->io_params[AIO_I2S_IO_TX] : &i2s_dai->io_params[AIO_I2S_IO_RX];
	u32 start = i2s_ctrl->irqc - active_tdms;
	unsigned int cfm, dfm;
	struct aud_ctrl ctrl;
	unsigned int bclk;
	int i;
	u32 xfeed = 0; // Clock cross feed

	/* Change AIO_24DFM to AIO_32DFM */
	dfm = berlin_get_sample_resolution((width == 24 ? 32 : width));

	/* Alghough h/w supports AIO_24CFM, but 24 is not multiples of 2.
	 * There could be some restriction on clock generation for certain
	 * frequency with AIO_24CFM. Change AIO_24CFM to AIO_32CFG instead
	 */
	cfm = berlin_get_sample_period_in_bclk(i2s_dai->sample_period == 24 ?
						32 : i2s_dai->sample_period);
	if (i2s_dai->ctrl.istdm) {
		/* TDM: round up is needed for bclk dividers */
		u32 tdm_ch = (active_tdms > 1) ? 8 : roundup_tdm_slots(chnum);
		bclk = fs * i2s_dai->sample_period * tdm_ch;
		ctrl.chcnt = tdm_ch;
	} else {
		/* i2s mode: each I2S_DO[0:3] supports 2 channels */
		bclk = fs * i2s_dai->sample_period * 2;
		ctrl.chcnt = (active_tdms > 1) ? 8 : chnum;
	}
	ctrl.sample_resolution	= dfm;
	ctrl.sample_period_in_bclk	= cfm;
	ctrl.data_fmt	= i2s_dai->ctrl.data_fmt;
	ctrl.isleftjfy	= i2s_dai->ctrl.isleftjfy;
	ctrl.invbclk	= i2s_dai->ctrl.invbclk;
	ctrl.invfs	= i2s_dai->ctrl.invfs;
	ctrl.msb	= true;
	ctrl.istdm	= i2s_dai->ctrl.istdm;
	ctrl.islframe	= i2s_dai->ctrl.islframe;

	/* Only configure active TDM Lane which are calculated on number of channels*/
	i2s_set_ctl(i2s_dai->aio_handle, i2s_ctrl, &ctrl, start);

	snd_printd("%s: chnum: %d active_tdms: %d\n", __func__, chnum, active_tdms);
	aio_setirq(i2s_dai->aio_handle, 1, 1, 0, 0, 0);
	for (i = start; i < i2s_ctrl->irqc; i++) {
		if (i2s_dai->ctrl.istdm) {
			/* TDM */
			aio_set_interleaved_mode(i2s_dai->aio_handle, i2s_ctrl->i2s_id[i], 0, 0);
		} else {
			/* i2s */
			if (chnum == 2)
				aio_set_interleaved_mode(i2s_dai->aio_handle,
					i2s_ctrl->i2s_id[i], 0, 0);
			else if (chnum == 4)
				aio_set_interleaved_mode(i2s_dai->aio_handle,
					i2s_ctrl->i2s_id[i], 1, (1<<2));
			else if (chnum == 6)
				aio_set_interleaved_mode(i2s_dai->aio_handle,
					i2s_ctrl->i2s_id[i], 2, (1<<2) | (2<<4));
			else if (chnum == 8)
				aio_set_interleaved_mode(i2s_dai->aio_handle,
					i2s_ctrl->i2s_id[i], 3, (1<<2) | (2<<4) | (3<<6));
			else
				dev_err(i2s_dai->dev,
					"not supported chnum: %d in I2S mode\n", chnum);
		}
	}

	if (i2s_dai->xfeed) {
		int i;
		/* Handle both record and playback clk selection */
		for (i = start; i < i2s_ctrl->irqc; i++) {

			xfeed = (i2s_dai->is_master) ? 0 : 1; /* 0: Master mode, 1: Slave mode */

			if (((i2s_ctrl->i2s_id[i] == AIO_ID_PRI_TX) &&\
				(i2s_dai->xfeed_chid == AIO_ID_MIC1_RX)) ||\
				((i2s_ctrl->i2s_id[i] == AIO_ID_MIC1_RX) &&\
				(i2s_dai->xfeed_chid == AIO_ID_PRI_TX)))
					xfeed |= 2; /* 2: Xfeed Master, 3: Xfeed Slave */

			aio_set_xfeed_mode(i2s_dai->aio_handle, i2s_ctrl->i2s_id[i], xfeed, xfeed);
			aio_set_bclk_sel(i2s_dai->aio_handle, i2s_ctrl->i2s_id[i],
				i2s_dai->xfeed_chid == i2s_ctrl->i2s_id[i] ? 1 : 0);
			aio_set_bclk_inv(i2s_dai->aio_handle, i2s_ctrl->i2s_id[i],
				i2s_dai->ctrl.invbclk);
			aio_set_fsync(i2s_dai->aio_handle, i2s_ctrl->i2s_id[i],
				i2s_dai->xfeed_chid == i2s_ctrl->i2s_id[i] ? 1 : 0,
				i2s_dai->ctrl.invfs);
		}
	} else {
		i2s_set_clk_div(i2s_dai->aio_handle, i2s_ctrl, berlin_get_bclk_div(mclkrate, bclk), start);
		i2s_iosel_set_bclk(i2s_dai->aio_handle, i2s_ctrl, 1, i2s_dai->ctrl.invbclk, start);
		i2s_iosel_set_fsync(i2s_dai->aio_handle, i2s_ctrl, 1, i2s_dai->ctrl.invfs, start);
	}
}

static int berlin_i2s_startup(struct snd_pcm_substream *substream,
				 struct snd_soc_dai *dai)
{
	struct i2s_dai_priv *i2s_dai = snd_soc_dai_get_drvdata(dai);

	snd_printk("i2s start...\n");
	aio_setirq(i2s_dai->aio_handle, 0, 0, 0, 0, 0);
	i2s_dai->stream_dir = substream->stream;
	return 0;
}

static void berlin_i2s_shutdown(struct snd_pcm_substream *substream,
				   struct snd_soc_dai *dai)
{
	int i;
	struct i2s_dai_priv *i2s_dai = snd_soc_dai_get_drvdata(dai);
	struct i2s_ch_irq_priv *i2s_ctrl = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) ?
		&i2s_dai->io_params[AIO_I2S_IO_TX] : &i2s_dai->io_params[AIO_I2S_IO_RX];

	for (i = 0; i < i2s_ctrl->irqc; i++)
		aio_i2s_clk_sync_reset(i2s_dai->aio_handle, i2s_ctrl->i2s_id[i]);

	aio_setirq(i2s_dai->aio_handle, 0, 0, 0, 0, 0);
	snd_printk("i2s shutdown...\n");
}

static int berlin_i2s_setfmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct i2s_dai_priv *i2s_dai = snd_soc_dai_get_drvdata(dai);
	struct i2s_ch_irq_priv *i2s_tx = &i2s_dai->io_params[AIO_I2S_IO_TX];
	struct i2s_ch_irq_priv *i2s_rx = &i2s_dai->io_params[AIO_I2S_IO_RX];
	int ret = 0;

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		i2s_dai->ctrl.data_fmt  = 2;
		i2s_dai->ctrl.istdm    = false;
		i2s_dai->ctrl.isleftjfy = true;   /* don't care if data_fmt = 2 */
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		i2s_dai->ctrl.data_fmt  = 1;
		i2s_dai->ctrl.istdm    = false;
		i2s_dai->ctrl.isleftjfy = true;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		i2s_dai->ctrl.data_fmt  = 1;
		i2s_dai->ctrl.istdm    = false;
		i2s_dai->ctrl.isleftjfy = false;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		i2s_dai->ctrl.data_fmt  = 1;
		i2s_dai->ctrl.istdm    = true;
		i2s_dai->ctrl.isleftjfy = true;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		i2s_dai->ctrl.data_fmt  = 2;
		i2s_dai->ctrl.istdm    = true;
		i2s_dai->ctrl.isleftjfy = true;  /* don't care if data_fmt = 2 */
		break;
	default:
		dev_err(dai->dev, "Unknown DAI format mask %x\n", fmt);
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		i2s_dai->ctrl.invbclk = false;
		i2s_dai->ctrl.invfs   = false;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		i2s_dai->ctrl.invbclk = false;
		i2s_dai->ctrl.invfs   = true;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		i2s_dai->ctrl.invbclk = true;
		i2s_dai->ctrl.invfs   = false;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		i2s_dai->ctrl.invbclk = true;
		i2s_dai->ctrl.invfs   = true;
		break;
	default:
		dev_err(i2s_dai->dev, "Unknown DAI invert mask 0x%x\n", fmt);
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		i2s_dai->is_master = false;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		i2s_dai->is_master = true;
		break;
	case SND_SOC_DAIFMT_CBM_CFS:
	case SND_SOC_DAIFMT_CBS_CFM:
	default:
		dev_err(dai->dev, "Do not support DAI master mask %x\n", fmt);
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_CLOCK_MASK) {
	case SND_SOC_DAIFMT_CONT:
		i2s_dai->continuous_clk = true;
		break;
	case SND_SOC_DAIFMT_GATED:
		i2s_dai->continuous_clk = false;
		break;
	default:
		dev_err(dai->dev, "Do not support DAI clock mask 0x%x\n", fmt);
		return -EINVAL;
	}

	snd_printd("%s: data_fmt: %d isleftjfy: %d istdm: %d is_master: %d"
		"invbclk: %d invfs: %d continuous clk: %d irqc[tx:%d rx:%d]\n",
				__func__,
				i2s_dai->ctrl.data_fmt, i2s_dai->ctrl.isleftjfy,
				i2s_dai->ctrl.istdm, i2s_dai->is_master,
				i2s_dai->ctrl.invbclk, i2s_dai->ctrl.invfs,
				i2s_dai->continuous_clk,
				i2s_tx->irqc,
				i2s_rx->irqc);

	snd_printd("%s: sample_period: %d", __func__, i2s_dai->sample_period);

	if (i2s_dai->continuous_clk) {
		i2s_ch_en(i2s_dai->aio_handle, i2s_tx, 1, 0);
		i2s_ch_en(i2s_dai->aio_handle, i2s_rx, 1, 0);
	}
	if (i2s_dai->output_mclk)
		aio_set_i2s_clk_enable(i2s_dai->aio_handle, AIO_I2S_I2S1_MCLK, 1);

	return ret;
}

static int berlin_i2s_hw_params(struct snd_pcm_substream *substream,
				   struct snd_pcm_hw_params *params,
				   struct snd_soc_dai *dai)
{
	struct i2s_dai_priv *i2s_dai = snd_soc_dai_get_drvdata(dai);
	struct i2s_ch_irq_priv *i2s_ctrl = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) ?
		&i2s_dai->io_params[AIO_I2S_IO_TX] : &i2s_dai->io_params[AIO_I2S_IO_RX];
	u32 fs = params_rate(params), chnum = params_channels(params);
	const struct mclk_info *mclk = NULL;
	int ret;
	struct berlin_ss_params ssparams = {};
	u32 tdm_start;

	if ((!(i2s_dai->mode & I2SO_MODE)) && (!(i2s_dai->mode & I2SI_MODE))) {
		dev_err(i2s_dai->dev, "not in i2s mode, fatal error. mode=%u\n", i2s_dai->mode);
		return -EINVAL;
	}

	if (aio_i2s_get_mclk_cfg(fs, i2s_dai->sample_period, &mclk) && !mclk) {
		snd_printk("fail to get mclk config");
		return -EINVAL;
	}

	berlin_set_pll(i2s_dai->aio_handle, mclk->apll_id, mclk->apllrate);

	i2s_dai->fs = fs;

	i2s_dai->active_tdms = (chnum > 8) ? i2s_ctrl->irqc : 1;

	ssparams.irq_num = i2s_dai->active_tdms;
	ssparams.chid_num = i2s_dai->active_tdms;
	ssparams.mode = I2SO_MODE;
	ssparams.irq = (unsigned int *)&i2s_ctrl->irqn[i2s_ctrl->irqc - i2s_dai->active_tdms];
	ssparams.dev_name = i2s_dai->dev_name;
	ssparams.dai_stop_fn = i2s_port_disable_cb;
	ssparams.dai_stop_data = i2s_dai;

	ret = berlin_pcm_request_dma_irq(substream, &ssparams);
	if (ret == 0)
		i2s_dai->i2s_requested = true;
	else
		return ret;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		/* mclk — only configure active TDM Lanes */
		tdm_start = i2s_ctrl->irqc - i2s_dai->active_tdms;
		int k;
		for (k = tdm_start; k < i2s_ctrl->irqc; k++)
			aio_i2s_set_clock(i2s_dai->aio_handle, i2s_ctrl->i2s_id[k], 1,
				mclk->d3_switch, mclk->plldiv, mclk->apll_id,
				(i2s_ctrl->i2s_id[k] == AIO_ID_PRI_TX) ? 1 : 0);
	} else {
		mic_mode_sel(i2s_dai->aio_handle, i2s_ctrl);

		if (i2s_dai->is_master) {
			u32 period;
			unsigned int bclk;

			if (i2s_dai->ctrl.istdm) {
				/* TDM */
				bclk = fs * i2s_dai->sample_period * (chnum > 8 ? 8 : roundup_tdm_slots(chnum));
			} else {
				/* i2s mode: each I2S_DO[0:3] supports 2 channels */
				bclk = fs * i2s_dai->sample_period * 2;
			}

			/* Basically supports period of 16 and 32 only */
			period = i2s_dai->sample_period == 24 ? 32 : i2s_dai->sample_period;

			if (i2s_dai->ctrl.istdm) {
				u32 tdm_ch = (chnum > 8) ? 8 : roundup_tdm_slots(chnum);
				mic_set_ws_prd(i2s_dai->aio_handle, i2s_ctrl, 1,
					(tdm_ch * period) - 1, 0);
			} else {
				/* i2s mode: each I2S_DI[0:3] supports 2 channels */
				mic_set_ws_prd(i2s_dai->aio_handle, i2s_ctrl, period - 1,
					(2 * period) - 1, 1);
			}
			mic_set_mm_mode(i2s_dai->aio_handle, i2s_ctrl, 1);
			i2s_set_clk_div(i2s_dai->aio_handle, i2s_ctrl,
							berlin_get_bclk_div(mclk->mclkrate, bclk), 0);
		} else {
			mic_set_mm_mode(i2s_dai->aio_handle, i2s_ctrl, 0);
		}
	}

	tdm_start = i2s_ctrl->irqc - i2s_dai->active_tdms;
	i2s_ch_mute(i2s_dai->aio_handle, i2s_ctrl, 0, tdm_start);
	i2s_ch_flush(i2s_dai->aio_handle, i2s_ctrl, 0, tdm_start);
	i2s_set_aio(i2s_dai, substream->stream, fs, params_width(params), chnum,
		    mclk->mclkrate, i2s_dai->active_tdms);
	i2s_ch_en(i2s_dai->aio_handle, i2s_ctrl, 1, tdm_start);
	aio_set_i2s_ch_en(AIO_ID_SEC_TX, 1);

	snd_printd("i2s hw ready\n");
	return ret;
}

static int berlin_i2s_hw_free(struct snd_pcm_substream *substream,
				 struct snd_soc_dai *dai)
{
	struct i2s_dai_priv *i2s_dai = snd_soc_dai_get_drvdata(dai);
	struct i2s_ch_irq_priv *i2s_ctrl = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) ?
		&i2s_dai->io_params[AIO_I2S_IO_TX] : &i2s_dai->io_params[AIO_I2S_IO_RX];
	const struct mclk_info *mclk = NULL;

	snd_printd("%s\n", __func__);

	if (aio_i2s_get_mclk_cfg(i2s_dai->fs, i2s_dai->sample_period, &mclk) && !mclk) {
		snd_printk("fail to get mclk config");
		return -EINVAL;
	}

	i2s_ch_flush(i2s_dai->aio_handle, i2s_ctrl, 0, 0);

	if (!i2s_dai->continuous_clk)
		i2s_ch_en(i2s_dai->aio_handle, i2s_ctrl, 0, 0);

	if (i2s_dai->i2s_requested && i2s_dai->active_tdms > 0) {
		u32 start = i2s_ctrl->irqc - i2s_dai->active_tdms;
		berlin_pcm_free_dma_irq(substream, i2s_dai->active_tdms,
			(unsigned int *)&i2s_ctrl->irqn[start]);
		i2s_dai->i2s_requested = false;
	}

	i2s_ch_en(i2s_dai->aio_handle, i2s_ctrl, 0, 0);
	i2s_iosel_set_bclk(i2s_dai->aio_handle, i2s_ctrl, 0, i2s_dai->ctrl.invbclk, 0);
	i2s_iosel_set_fsync(i2s_dai->aio_handle, i2s_ctrl, 0, i2s_dai->ctrl.invfs, 0);
	i2s_set_mclk_src(i2s_dai->aio_handle, i2s_ctrl, mclk->d3_switch, mclk->plldiv, mclk->apll_id, 0, 0);
	aio_set_i2s_ch_en(AIO_ID_SEC_TX, 0);
	return 0;
}

static int berlin_i2s_trigger(struct snd_pcm_substream *substream,
				 int cmd, struct snd_soc_dai *dai)
{
	struct i2s_dai_priv *i2s_dai = snd_soc_dai_get_drvdata(dai);
	struct i2s_ch_irq_priv *i2s_ctrl = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) ?
			&i2s_dai->io_params[AIO_I2S_IO_TX] : &i2s_dai->io_params[AIO_I2S_IO_RX];
	u32 start = i2s_ctrl->irqc - i2s_dai->active_tdms;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		i2s_ch_mute(i2s_dai->aio_handle, i2s_ctrl, 0, start);
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			i2s_enable_txport(i2s_dai->aio_handle, i2s_ctrl, 1, start);
		else
			i2s_enable_rxport(i2s_dai->aio_handle, i2s_ctrl, 1, start);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		if (!i2s_dai->continuous_clk)
			i2s_ch_mute(i2s_dai->aio_handle, i2s_ctrl, 1, start);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int berlin_i2s_dai_probe(struct snd_soc_dai *dai)
{
	snd_soc_add_dai_controls(dai, berlin_i2s_ctrls,
				 ARRAY_SIZE(berlin_i2s_ctrls));
	return 0;
}

static struct snd_soc_dai_ops syna_i2s_dai_i2s_ops = {
	.startup   = berlin_i2s_startup,
	.set_fmt   = berlin_i2s_setfmt,
	.hw_params = berlin_i2s_hw_params,
	.hw_free   = berlin_i2s_hw_free,
	.trigger   = berlin_i2s_trigger,
	.shutdown  = berlin_i2s_shutdown,
#if (KERNEL_VERSION(6, 12, 0) <= LINUX_VERSION_CODE)
	.probe = berlin_i2s_dai_probe,
#endif
};

static struct snd_soc_dai_driver i2s_soc_dai_drv = {
	.name = "i2s_trx",
#if (KERNEL_VERSION(6, 12, 0) > LINUX_VERSION_CODE)
	.probe = berlin_i2s_dai_probe,
#endif
	.playback = {
		.stream_name = "I2S-Playback",
		.channels_min = 1,
		.channels_max = 16,
		.rates = I2S_PLAYBACK_RATES,
		.formats = I2S_PLAYBACK_FORMATS,
	},
	.capture = {
		.stream_name = "I2S-Capture",
		.channels_min = 1,
		.channels_max = 16,
		.rates = I2S_PLAYBACK_RATES,
		.formats = I2S_PLAYBACK_FORMATS,
	},
	.ops = &syna_i2s_dai_i2s_ops,
};

static struct snd_soc_dai_driver syna_play_dai = {
	.name = "i2s_tx",
#if (KERNEL_VERSION(6, 12, 0) > LINUX_VERSION_CODE)
	.probe = berlin_i2s_dai_probe,
#endif
	.playback = {
		.stream_name = "I2S-Playback",
		.channels_min = 1,
		.channels_max = 16,
		.rates = I2S_PLAYBACK_RATES,
		.formats = I2S_PLAYBACK_FORMATS,
	},
	.ops = &syna_i2s_dai_i2s_ops,
};

static struct snd_soc_dai_driver syna_record_dai = {
	.name = "i2s_rx",
#if (KERNEL_VERSION(6, 12, 0) > LINUX_VERSION_CODE)
	.probe = berlin_i2s_dai_probe,
#endif
	.capture = {
		.stream_name = "I2S-Capture",
		.channels_min = 1,
		.channels_max = 16,
		.rates = I2S_PLAYBACK_RATES,
		.formats = I2S_PLAYBACK_FORMATS,
	},
	.ops = &syna_i2s_dai_i2s_ops,
};

static const struct snd_soc_component_driver i2s_soc_dai_component = {
	.name = "aio-i2s",
};

static int i2s_dai_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct i2s_dai_priv *i2s_dai;
	struct i2s_ch_irq_priv *i2s_tx, *i2s_rx;
	int irq, ret;

	//Defer probe until dependent soc module/s are probed/initialized
	if (!is_avio_driver_initialized())
		return -EPROBE_DEFER;

	i2s_dai = devm_kzalloc(dev, sizeof(struct i2s_dai_priv),
			      GFP_KERNEL);
	if (!i2s_dai)
		return -ENOMEM;

	i2s_tx = &i2s_dai->io_params[AIO_I2S_IO_TX];
	i2s_rx = &i2s_dai->io_params[AIO_I2S_IO_RX];
	i2s_tx->irqc = 0;
	i2s_rx->irqc = 0;
	i2s_dai->xfeed = false;
	i2s_dai->dev_name = dev_name(dev);
	i2s_dai->dev = dev;

	/*open aio handle for alsa*/
	i2s_dai->aio_handle = open_aio(i2s_dai->dev_name);
	if (unlikely(i2s_dai->aio_handle == NULL)) {
		snd_printk("aio_handle:%p  get failed\n", i2s_dai->aio_handle);
		return -EBUSY;
	}

	dev_set_drvdata(dev, i2s_dai);

	i2s_dai->output_mclk = of_property_read_bool(np, "output-mclk");
	i2s_dai->ctrl.islframe = of_property_read_bool(np, "long-frame");

    /* interrupt-names = "pri_tx", "sec_tx", "pri_rx", "sec_rx"; */
	irq = platform_get_irq_byname(pdev, "sec_tx");
	if (!(irq < 0)) {
		i2s_tx->irqn[i2s_tx->irqc] = irq;
		i2s_tx->chid[i2s_tx->irqc] = irqd_to_hwirq(irq_get_irq_data(irq));
		i2s_tx->i2s_id[i2s_tx->irqc] = AIO_ID_SEC_TX;
		i2s_tx->irqc++;
		i2s_dai->mode |= I2SO_MODE;
	}

	irq = platform_get_irq_byname(pdev, "pri_tx");
	if (!(irq < 0)) {
		i2s_tx->irqn[i2s_tx->irqc] = irq;
		i2s_tx->chid[i2s_tx->irqc] = irqd_to_hwirq(irq_get_irq_data(irq));
		i2s_tx->i2s_id[i2s_tx->irqc] = AIO_ID_PRI_TX;
		i2s_tx->irqc++;
		i2s_dai->mode |= I2SO_MODE;
	}

	irq = platform_get_irq_byname(pdev, "pri_rx");
	if (!(irq < 0)) {
		i2s_rx->irqn[i2s_rx->irqc] = irq;
		i2s_rx->chid[i2s_rx->irqc] = irqd_to_hwirq(irq_get_irq_data(irq));
		i2s_rx->i2s_id[i2s_rx->irqc] = AIO_ID_MIC1_RX;
		i2s_rx->irqc++;
		i2s_dai->mode |= I2SI_MODE;
	}

	irq = platform_get_irq_byname(pdev, "sec_rx");
	if (!(irq < 0)) {
		i2s_rx->irqn[i2s_rx->irqc] = irq;
		i2s_rx->chid[i2s_rx->irqc] = irqd_to_hwirq(irq_get_irq_data(irq));
		i2s_rx->i2s_id[i2s_rx->irqc] = AIO_ID_MIC2_RX;
		i2s_rx->irqc++;
		i2s_dai->mode |= I2SI_MODE;
	}

	snd_printd("get irq %d for node %s\n", irq, pdev->name);

	ret = of_property_read_u32(np, "sample-period", &i2s_dai->sample_period);
	if (ret)
		i2s_dai->sample_period = 32;

	ret = of_property_read_u32(np, "xfeed_chid", &i2s_dai->xfeed_chid);
	if (ret)
		i2s_dai->xfeed_chid = i2s_tx->chid[0]; /* xfeed channel id pick TX PRI */

	if (i2s_dai->mode == (I2SO_MODE|I2SI_MODE)) {
		ret = devm_snd_soc_register_component(dev,
							&i2s_soc_dai_component,
							&i2s_soc_dai_drv, 1);
		if (ret) {
			snd_printk("failed to register DAI: %d\n", ret);
			close_aio(i2s_dai->aio_handle);
			i2s_dai->aio_handle = NULL;
			return ret;
		}
	} else if (i2s_dai->mode & I2SI_MODE) {
		ret = devm_snd_soc_register_component(dev,
							&i2s_soc_dai_component,
							&syna_record_dai, 1);
		if (ret) {
			snd_printk("failed to register DAI: %d\n", ret);
			close_aio(i2s_dai->aio_handle);
			i2s_dai->aio_handle = NULL;
			return ret;
		}
	} else if (i2s_dai->mode & I2SO_MODE) {
		ret = devm_snd_soc_register_component(dev,
							&i2s_soc_dai_component,
							&syna_play_dai, 1);
		if (ret) {
			snd_printk("failed to register DAI: %d\n", ret);
			close_aio(i2s_dai->aio_handle);
			i2s_dai->aio_handle = NULL;
			return ret;
		}
	} else {
		snd_printk("non valid irq found in dts\n");
		return -EINVAL;
	}

	snd_printd("%s: i2s tdm support [tx %dch rx %dch]\n", __func__,
			i2s_tx->irqc*8, i2s_rx->irqc*8);

	return ret;
}

static RET_TYPE i2s_dai_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct i2s_dai_priv *i2s_dai;

	i2s_dai = (struct i2s_dai_priv *)dev_get_drvdata(dev);

	/*close aio handle of alsa if have opened*/
	if (i2s_dai && i2s_dai->aio_handle) {
		close_aio(i2s_dai->aio_handle);
		i2s_dai->aio_handle = NULL;
	}

	RETURN_VALUE;
}

static const struct of_device_id i2s_dai_dt_ids[] = {
	{ .compatible = "syna,platypus-i2s-16ch",  },
	{}
};
MODULE_DEVICE_TABLE(of, i2s_dai_dt_ids);

static struct platform_driver i2s_dai_driver = {
	.probe = i2s_dai_probe,
	.remove = i2s_dai_remove,
	.driver = {
		.name = "syna-i2s-16ch",
		.of_match_table = i2s_dai_dt_ids,
	},
};
module_platform_driver(i2s_dai_driver);

MODULE_DESCRIPTION("Synaptics I2S ALSA transceiver support up to 16ch TDM");
MODULE_ALIAS("platform:i2s-dai");
MODULE_LICENSE("GPL v2");