// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2018-2020 Synaptics Incorporated */

#include <linux/kernel.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include "aud_clk.h"
#include "berlin_pcm.h"
#include "berlin_util.h"
#include "i2s_dai.h"

struct aud_clk_priv {
	struct aud_clk *clks[I2S_CLK_ID_MAX];
	u32 aprktEnFlag;
};

//private
static struct aud_clk_priv gaudClk;

//////////////////////////////////////////////////////////////////////////////////////////////

//below configuration is based on MYNA chip
static u32 map_clk_tx_port(enum aud_clk_id clkId)
{
	switch (clkId) {
	case I2S_CLK_ID_I2S1:
		return AIO_ID_I2S1_TX;
	case I2S_CLK_ID_I2S2:
		return AIO_ID_I2S2_TX;
	case I2S_CLK_ID_I2S3:
		return AIO_ID_I2S3_TX;
#ifdef AVIO_MYNA2
	case I2S_CLK_ID_I2S4:
		return AIO_ID_I2S4_TX;
	case I2S_CLK_ID_I2S5:
		return AIO_ID_I2S5_TX;
#endif //AVIO_MYNA2
	default:
		return AIO_ID_I2S_MAX_NUM;
	}
	return AIO_ID_I2S_MAX_NUM;
}

static int aud_berlin_set_pll(void *aio_handle, u32 apll_id, u32 apll)
{
	if (apll_id >= AIO_APLL_NUM) {
		pr_err("apll%d not supported", apll_id);
		return 0;
	}

	if (aio_get_clk_rate(aio_handle, apll_id) != apll) {
		aio_clk_enable(aio_handle, apll_id, false);
		aio_set_clk_rate(aio_handle, apll_id, apll);
		aio_clk_enable(aio_handle, apll_id, true);
		pr_debug("set apll%d to %u", apll_id, apll);
	} else {
		pr_debug("apll%d already set %u", apll_id, apll);
	}

	return 0;
}

static int aud_clk_config(struct snd_pcm_substream *s)
{
	struct snd_soc_pcm_runtime *rtd = snd_pcm_substream_chip(s);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	struct i2s_soc_dai *soc_dai = snd_soc_dai_get_drvdata(cpu_dai);
	struct i2s_io_params *i2s_ctrl = (s->stream == SNDRV_PCM_STREAM_PLAYBACK) ?
			&soc_dai->io_params[AIO_I2S_IO_TX] : &soc_dai->io_params[AIO_I2S_IO_RX];
	struct aud_clk *clkroot = i2s_ctrl->clkrt;
	struct i2s_soc_dai *clk_soc_dai = (struct i2s_soc_dai *)clkroot->clk_soc_dai;
	struct aud_ctrl *clkrtCtrl, *clkConsumer;
	bool ret = 0, mclk_on = true, bclk_master = false;
	u32 apll_outf = AIO_APLL_OUT, plldiv = 1;
	u32 clk_chid = clkroot->clk_chid;
	u32 apll_id = 0, aud_clk_mode;
	const struct mclk_info *mclk = NULL;
	struct aud_ctrl ctrl;

	clkrtCtrl = &clkroot->clkCfg.aCtrl;
	clkConsumer = &soc_dai->ctrl;

	snd_printd("%s clk_id %d config chId %d clkstate %d\n",
			__func__, clkroot->clkId, clk_chid, clkroot->clkstate);

	if (clkroot->clkstate == AIO_I2S_CLK_IN_USE) {
		/*Validate clk related params to confirm sharing clk with two consumer are good to go*/
		if (clkrtCtrl->chcnt != clkConsumer->chcnt ||
			clkrtCtrl->fs != clkConsumer->fs ||
			clkrtCtrl->bclk != clkConsumer->bclk ||
			clkrtCtrl->data_fmt != clkConsumer->data_fmt ||
			clkrtCtrl->istdm != clkConsumer->istdm ||
			clkroot->clkCfg.bclk_src_sel != clk_soc_dai->clkSrcSel) {
			snd_printk("%s clk root ctrl param differs, can't use this clock\n", __func__);
			ret = -EINVAL;
			goto exit;
		}
		snd_printd("%s clk root ctrl param are same, good to continue\n", __func__);
		ret = 0;
		goto exit;
	}

	//use CLK divisor settings from CLK consumer
	memcpy(&clkroot->clkCfg.aCtrl, &soc_dai->ctrl, sizeof(struct aud_ctrl));
	//use CLK source settings from CLK provider
	clkroot->clkCfg.bclk_src_sel = clk_soc_dai->clkSrcSel;

	switch (clkroot->clkCfg.bclk_src_sel) {
	case AIO_BCLK_SRC_APLL:
		if (aio_i2s_get_mclk_cfg(soc_dai->ctrl.fs, soc_dai->sample_period, &mclk) || !mclk) {
			pr_err("failed to find mclk cfg");
			ret = -EINVAL;
			goto exit;
		}

		plldiv = mclk->plldiv;
		apll_outf = mclk->outf;
		apll_id = mclk->apll_id;
		mclk_on = true;
		if (clk_soc_dai->is_master) {
			bclk_master = true;
			aud_clk_mode =  clkroot->useTxClk ? AIO_I2S_STATE_TX_MASTER : AIO_I2S_STATE_RX_MASTER;
		} else {
			bclk_master = false;
			aud_clk_mode =  clkroot->useTxClk ? AIO_I2S_STATE_TX_SLAVE : AIO_I2S_STATE_RX_SLAVE;
		}
		aud_berlin_set_pll(soc_dai->aio_handle, apll_id, mclk->apllrate);
		break;
	case AIO_BCLK_SRC_XFEED:
		//CLK provider may receive CLK consumer
		mclk_on = false;
		plldiv = 1; //MCLK not used its disabled
		bclk_master = false;
		if (clk_soc_dai->is_master)
			aud_clk_mode = clkroot->useTxClk ? AIO_I2S_STATE_TX_MS : AIO_I2S_STATE_RX_MS;
		else
			aud_clk_mode = clkroot->useTxClk ? AIO_I2S_STATE_TX_SLAVE : AIO_I2S_STATE_RX_SLAVE;
		break;
	case AIO_BCLK_SRC_PAD:
		mclk_on = false;
		plldiv = 1;  //MCLK not used its disabled
		bclk_master = false;
		aud_clk_mode = clkroot->useTxClk ? AIO_I2S_STATE_TX_SLAVE : AIO_I2S_STATE_RX_SLAVE;
		break;
	default:
		ret = -EINVAL;
		goto exit;
	}

	if (clk_chid != i2s_ctrl->i2s_chid)
		aio_set_mode(soc_dai->aio_handle, clk_chid, aud_clk_mode, 0, 0);

	aio_i2s_set_clock(soc_dai->aio_handle, clk_chid, apll_outf, 0, plldiv, apll_id, mclk_on);

	ctrl = soc_dai->ctrl;
	if ((s->stream == SNDRV_PCM_STREAM_PLAYBACK) && (ctrl.chcnt > 1) &&
		((ctrl.istdm == false) && (ctrl.data_fmt == 2))) {
		ctrl.invfs = !ctrl.invfs;
	}
	//set i2s_set_aio() directly using CLK consumer settings
	aio_set_ctl_ext(soc_dai->aio_handle, clk_chid, &ctrl);
	aio_set_pcm_mono(soc_dai->aio_handle, clk_chid, (soc_dai->ctrl.chcnt == 1));

	if (clkroot->useTxClk) {
		//choose BCLK source external/APLL
		i2s_soc_dai_set_bclk(soc_dai, clk_chid, bclk_master,
							soc_dai->ctrl.invbclk ? true : false);
	}

	//update BCLK divisor if APLL CLK source
	if (bclk_master) {
		aio_setclkdiv(soc_dai->aio_handle, clk_chid,
					berlin_get_bclk_div(mclk->mclkrate, soc_dai->ctrl.bclk));

		//if RX master, MM mode
		if (!clkroot->useTxClk) {
			u32 period;
			/* Basically supports period of 16 and 32 only */
			period = soc_dai->sample_period == 24 ? 32 : soc_dai->sample_period;
			if (soc_dai->ctrl.istdm) {
				aio_set_mic_ws_prd(soc_dai->aio_handle, clk_chid, 1, (soc_dai->ctrl.chcnt * period) - 1, 0);
			} else {
				/* i2s mode: supports 2 channels */
				aio_set_mic_ws_prd(soc_dai->aio_handle, clk_chid, period - 1, (2 * period) - 1, 1);
			}

			//RX master, set TX port CLK divider as well
			aio_setclkdiv(soc_dai->aio_handle, map_clk_tx_port(clkroot->clkId),
						berlin_get_bclk_div(mclk->mclkrate, soc_dai->ctrl.bclk));
		}
	}

	snd_printd("%s configured clk on chId %d clk_mode %d mclk_on %d bclk_master %d fs %d bclk %d\n",
			__func__,
			clk_chid,
			aud_clk_mode,
			mclk_on, bclk_master,
			soc_dai->ctrl.fs,
			soc_dai->ctrl.bclk);
	clkroot->clkstate = AIO_I2S_CLK_CONFIGURED;
exit:
	return ret;
}

static bool aud_clk_in_use(struct snd_pcm_substream *s)
{
	struct snd_soc_pcm_runtime *rtd = snd_pcm_substream_chip(s);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	struct i2s_soc_dai *soc_dai = snd_soc_dai_get_drvdata(cpu_dai);
	struct i2s_io_params *i2s_ctrl = (s->stream == SNDRV_PCM_STREAM_PLAYBACK) ?
			&soc_dai->io_params[AIO_I2S_IO_TX] : &soc_dai->io_params[AIO_I2S_IO_RX];
	struct aud_clk *clkroot = i2s_ctrl->clkrt;
	u32 clk_chid = clkroot->clk_chid;

	return (clkroot->clkstate == AIO_I2S_CLK_IN_USE) && (clk_chid == i2s_ctrl->i2s_chid);
}

static int aud_clk_start(struct snd_pcm_substream *s)
{
	struct snd_soc_pcm_runtime *rtd = snd_pcm_substream_chip(s);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	struct i2s_soc_dai *soc_dai = snd_soc_dai_get_drvdata(cpu_dai);
	struct i2s_io_params *i2s_ctrl = (s->stream == SNDRV_PCM_STREAM_PLAYBACK) ?
			&soc_dai->io_params[AIO_I2S_IO_TX] : &soc_dai->io_params[AIO_I2S_IO_RX];
	struct aud_clk *clkroot = i2s_ctrl->clkrt;
	u32 clk_chid = clkroot->clk_chid;

	if (clkroot->clkstate == AIO_I2S_CLK_IN_USE) {
		clkroot->ref_count += 1;
		snd_printd("%s clk already started ref_count %d\n", __func__, clkroot->ref_count);
		return 0;
	}

	if (clkroot->clkstate == AIO_I2S_CLK_CONFIGURED) {
		aio_i2s_mclk_ctrl(soc_dai->aio_handle, clk_chid, true);
		if (clk_chid == i2s_ctrl->i2s_chid)
			i2s_soc_dai_run(clkroot->clk_soc_dai, clkroot->useTxClk, AIO_I2S_SET_START, 0);
		else
			i2s_soc_dai_run(clkroot->clk_soc_dai, clkroot->useTxClk, AIO_I2S_SET_START_ON_PAUSE, 0);
		snd_printd("%s clk started on chId %d\n", __func__, clk_chid);
		clkroot->ref_count = 1;
		clkroot->clkstate = AIO_I2S_CLK_IN_USE;
	} else {
		snd_printk("%s clk chId %d not in right state %d\n", __func__, clk_chid, clkroot->clkstate);
	}

	return 0;
}

static int aud_clk_stop(struct snd_pcm_substream *s)
{
	struct snd_soc_pcm_runtime *rtd = snd_pcm_substream_chip(s);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	struct i2s_soc_dai *soc_dai = snd_soc_dai_get_drvdata(cpu_dai);
	struct i2s_io_params *i2s_ctrl = (s->stream == SNDRV_PCM_STREAM_PLAYBACK) ?
			&soc_dai->io_params[AIO_I2S_IO_TX] : &soc_dai->io_params[AIO_I2S_IO_RX];
	struct aud_clk *clkroot = i2s_ctrl->clkrt;
	u32 clk_chid = clkroot->clk_chid;

	clkroot->ref_count -= 1; //decrement counter
	if (clkroot->clkstate == AIO_I2S_CLK_IN_USE && clkroot->ref_count > 0) {
		if (i2s_ctrl->i2s_chid == clk_chid)
			i2s_soc_dai_run(clkroot->clk_soc_dai, clkroot->useTxClk,
			AIO_I2S_SET_PAUSE, berlin_get_pause_count(s));
		snd_printd("%s clk in use ref_count %d\n", __func__, clkroot->ref_count);
		return 0;
	}

	if (clkroot->clkstate == AIO_I2S_CLK_IN_USE) {
		aio_i2s_mclk_ctrl(soc_dai->aio_handle, clk_chid, false);
		i2s_soc_dai_run(clkroot->clk_soc_dai, clkroot->useTxClk,
							AIO_I2S_SET_STOP, 0);
		snd_printd("%s clk stopped chId %d\n", __func__, clk_chid);
		clkroot->clkstate = AIO_I2S_CLK_CONFIGURED;
	}

	return 0;
}

void *aud_clk_add(enum aud_clk_id clkId, void *clk_soc_dai)
{
	struct i2s_soc_dai *soc_dai = (struct i2s_soc_dai *)clk_soc_dai;
	struct aud_clk *pCLK = NULL;
	//TODO: add mutex
	if (clkId == I2S_CLK_ID_INVALID || clkId >= I2S_CLK_ID_MAX)
		return NULL;

	pCLK = gaudClk.clks[clkId-1];
	if (pCLK)
		goto ret;

	pCLK = kzalloc(sizeof(struct aud_clk), GFP_KERNEL);
	if (!pCLK)
		return ERR_PTR(-ENOMEM);

	pCLK->ref_count = 0;
	pCLK->clkstate = AIO_I2S_CLK_READY;
	pCLK->clkId = clkId;
	pCLK->useTxClk = !soc_dai->useMMMode; //choose CLK generator based on MM mode selection
	pCLK->clk_chid = pCLK->useTxClk ?
		soc_dai->io_params[AIO_I2S_IO_TX].i2s_chid : soc_dai->io_params[AIO_I2S_IO_RX].i2s_chid;
	pCLK->clk_soc_dai = soc_dai;

	/* func*/
	pCLK->ops.config = aud_clk_config;
	pCLK->ops.isInUse = aud_clk_in_use;
	pCLK->ops.start = aud_clk_start;
	pCLK->ops.stop = aud_clk_stop;
	gaudClk.clks[clkId-1] = pCLK;
	snd_printd("%s i2s clk %d added, chId %d\n", __func__, clkId, pCLK->clk_chid);
ret:
	return pCLK;
}

bool aud_clk_remove(enum aud_clk_id clkId)
{
	struct aud_clk *pCLK = NULL;
	//TODO: add mutex
	if (clkId == I2S_CLK_ID_INVALID || clkId >= I2S_CLK_ID_MAX)
		return NULL;
	pCLK = gaudClk.clks[clkId-1];
	if (!pCLK)
		goto ret;

	kfree(pCLK);
	gaudClk.clks[clkId-1] = NULL;
ret:
	return 0;
}

struct aud_clk *aud_get_clk(enum aud_clk_id clkId)
{
	struct aud_clk *pCLK = NULL;
	struct i2s_soc_dai *soc_dai;
	//TODO: add mutex
	if (clkId == I2S_CLK_ID_INVALID || clkId >= I2S_CLK_ID_MAX)
		return NULL;

	pCLK = gaudClk.clks[clkId-1];
	if (!pCLK) {
		snd_printk("%s error: CLK not configured as master...\n", __func__);
		return NULL;
	}
	soc_dai = (struct i2s_soc_dai *)pCLK->clk_soc_dai;

	snd_printd("%s clkId %d as root, isMaster %d bclk_src %d\n",
		__func__, clkId, soc_dai->is_master, soc_dai->clkSrcSel);

	/* CLK provider can be slave or master and
	 * the same clk will be to cross feed to other clk consumer as clk provider
	 */
	return pCLK;
}

void aud_set_aprkt_en_flag(bool bSetReset, u32 value)
{
	if (bSetReset)
		gaudClk.aprktEnFlag |= value;
	else
		gaudClk.aprktEnFlag = 0;
}

u32 aud_get_aprkt_en_flag(void)
{
	return gaudClk.aprktEnFlag;
}
