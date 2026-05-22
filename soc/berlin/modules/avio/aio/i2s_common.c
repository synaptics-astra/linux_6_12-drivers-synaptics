// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2019-2020 Synaptics Incorporated */

#include "i2s_common.h"
#include "aio_hal.h"
#include "aio_common.h"
#include "avioDhub.h"
#include "avio_sub_module.h"

static atomic_t aio_ch_en[AIO_ID_MAX_NUM];

enum aio_ch_operation {
	AIO_CH_EN,
	AIO_CH_MUTE,
	AIO_CH_FLUSH
};

static u32 aio_get_audch_ctrl(void *hd, u32 id, u32 tsd, u32 op)
{
	struct aio_priv *aio = hd_to_aio(hd);
	u32 offset, setting = 0;
	T32AUDCH_CTRL reg;

	offset = aio_get_offset_from_ch(id, tsd);

	if (offset == 0)
		return 0;
	offset += RA_AUDCH_CTRL;
	reg.u32 = aio_read(aio, offset);
	switch (op) {
	case AIO_CH_EN:
		setting = reg.uCTRL_ENABLE;
		break;
	case AIO_CH_MUTE:
		setting = reg.uCTRL_MUTE;
		break;
	case AIO_CH_FLUSH:
		setting = reg.uCTRL_FLUSH;
		break;
	default:
		break;
	}

	return setting;
}

static void aio_set_audch_ctrl(void *hd, u32 id, u32 tsd, u32 op, u32 setting)
{
	struct aio_priv *aio = hd_to_aio(hd);
	u32 offset;
	T32AUDCH_CTRL reg;

	offset = aio_get_offset_from_ch(id, tsd);

	if (offset == 0)
		return;
	offset += RA_AUDCH_CTRL;
	reg.u32 = aio_read(aio, offset);
	switch (op) {
	case AIO_CH_EN:
		reg.uCTRL_ENABLE = setting;
		break;
	case AIO_CH_MUTE:
		reg.uCTRL_MUTE = setting;
		break;
	case AIO_CH_FLUSH:
		reg.uCTRL_FLUSH = setting;
		break;
	default:
		break;
	}
	aio_write(aio, offset, reg.u32);
}

bool aio_get_aud_ch_en(void *hd, u32 id, u32 tsd)
{
	return aio_get_audch_ctrl(hd, id, tsd, AIO_CH_EN);
}
EXPORT_SYMBOL(aio_get_aud_ch_en);

void aio_set_aud_ch_en(void *hd, u32 id, u32 tsd, bool enable)
{
	/* This enable FSYNC/LRCK on the audio port */
	aio_set_audch_ctrl(hd, id, tsd, AIO_CH_EN, enable);
	pr_debug("%s: id:%d, tsd:%d, enable:%d\n", __func__, id, tsd, enable);
}
EXPORT_SYMBOL(aio_set_aud_ch_en);

void aio_set_i2s_ch_en(u32 id, bool enable)
{
	/* Atomically set channel enable state */
	atomic_set(&aio_ch_en[id], enable ? 1 : 0);
}
EXPORT_SYMBOL(aio_set_i2s_ch_en);

bool aio_get_i2s_ch_en(u32 id)
{
	/* Atomically get channel enable state and return bool*/
	return atomic_read(&aio_ch_en[id]) != 0;
}
EXPORT_SYMBOL(aio_get_i2s_ch_en);

void aio_set_aud_ch_mute(void *hd, u32 id, u32 tsd, u32 mute)
{
	pr_debug("%s , get ch %d mute\n", __func__, id);
	aio_set_audch_ctrl(hd, id, tsd, AIO_CH_MUTE, mute);
}
EXPORT_SYMBOL(aio_set_aud_ch_mute);

void aio_set_aud_ch_flush(void *hd, u32 id, u32 tsd, u32 flush)
{
	pr_debug("%s , get ch %d flush\n", __func__, id);
	aio_set_audch_ctrl(hd, id, tsd, AIO_CH_FLUSH, flush);
}
EXPORT_SYMBOL(aio_set_aud_ch_flush);

static u32 aio_get_ctrl_address(u32 id)
{
	u32 address;

	address = aio_get_ctrl_base_addr(id);
	if (address == INVALID_ADDRESS)
		return address;

	address += RA_PRIAUD_CTRL;
	return address;
}

void aio_set_data_fmt(void *hd, u32 id, u32 data_fmt)
{
	struct aio_priv *aio = hd_to_aio(hd);
	u32 address = aio_get_ctrl_address(id);
	T32PRIAUD_CTRL reg;

	dev_info(aio->dev, "%s , id %d fmt %d address 0x%X\n", __func__, id, data_fmt, address);
	if (address == INVALID_ADDRESS)
		return;

	reg.u32 = aio_read(aio, address);
	switch (data_fmt) {
	case AIO_I2S_MODE:
		reg.uCTRL_TFM = PRIAUD_CTRL_TFM_I2S;
		reg.uCTRL_LEFTJFY = data_fmt >> 4;
		break;
	case AIO_LEFT_JISTIFIED_MODE:
		reg.uCTRL_TFM = PRIAUD_CTRL_TFM_JUSTIFIED;
		reg.uCTRL_LEFTJFY = data_fmt >> 4;
		break;
	case AIO_RIGHT_JISTIFIED_MODE:
		reg.uCTRL_TFM = PRIAUD_CTRL_TFM_JUSTIFIED;
		reg.uCTRL_LEFTJFY = data_fmt >> 4;
		break;
	default:
		return;
	}

	aio_write(aio, address, reg.u32);
}
EXPORT_SYMBOL(aio_set_data_fmt);

/* TCF: half period of FSYNC (sampling rate) in terms of number of bit-clocks */
void aio_set_width_word(void *hd, u32 id, u32 width_word)
{
	struct aio_priv *aio = hd_to_aio(hd);
	u32 address = aio_get_ctrl_address(id);
	T32PRIAUD_CTRL reg;

	if (address == INVALID_ADDRESS)
		return;
	reg.u32 = aio_read(aio, address);
	reg.uCTRL_TCF = width_word;
	aio_write(aio, address, reg.u32);
}
EXPORT_SYMBOL(aio_set_width_word);

/* TDM: channel resolution (number of valid bits in a half period of FSYNC) */
void aio_set_width_sample(void *hd, u32 id, u32 width_sample)
{
	struct aio_priv *aio = hd_to_aio(hd);
	u32 address = aio_get_ctrl_address(id);
	T32PRIAUD_CTRL reg;

	if (address == INVALID_ADDRESS)
		return;
	reg.u32 = aio_read(aio, address);
	reg.uCTRL_TDM = width_sample;
	aio_write(aio, address, reg.u32);
}
EXPORT_SYMBOL(aio_set_width_sample);

void aio_set_invclk(void *hd, u32 id, bool invclk)
{
	struct aio_priv *aio = hd_to_aio(hd);
	u32 address = aio_get_ctrl_address(id);
	T32PRIAUD_CTRL reg;

	if (address == INVALID_ADDRESS)
		return;
	reg.u32 = aio_read(aio, address);
	reg.uCTRL_INVCLK = invclk;
	aio_write(aio, address, reg.u32);
}
EXPORT_SYMBOL(aio_set_invclk);

void aio_set_invfs(void *hd, u32 id, bool invfs)
{
	struct aio_priv *aio = hd_to_aio(hd);
	u32 address = aio_get_ctrl_address(id);
	T32PRIAUD_CTRL reg;

	if (address == INVALID_ADDRESS)
		return;
	reg.u32 = aio_read(aio, address);
	reg.uCTRL_INVFS = invfs;
	aio_write(aio, address, reg.u32);
}
EXPORT_SYMBOL(aio_set_invfs);

void __weak aio_set_tdm(void *hd, u32 id, bool en, u32 chcnt, u32 wshigh)
{
	pr_debug("%s stub", __func__);
}

void aio_set_ctl(void *hd,
		u32 id, u32 data_fmt,
		u32 width_word, u32 width_sample)
{
	switch (id) {

	case AIO_ID_MIC_RX:
		aio_set_invfs(hd, id, PRIAUD_CTRL_INVFS_NORMAL);
		aio_set_invclk(hd, id, PRIAUD_CTRL_INVCLK_NORMAL);
		break;
	case AIO_ID_PRI_TX:
		aio_set_invfs(hd, id, PRIAUD_CTRL_INVFS_INVERTED);
		aio_set_invclk(hd, id, PRIAUD_CTRL_INVCLK_INVERTED);
		break;
	case AIO_ID_SEC_TX:
		aio_set_invfs(hd, id, PRIAUD_CTRL_INVFS_INVERTED);
		aio_set_invclk(hd, id, PRIAUD_CTRL_INVCLK_INVERTED);
		break;
	case AIO_ID_HDMI_TX:
		aio_set_invfs(hd, id, PRIAUD_CTRL_INVFS_INVERTED);
		aio_set_invclk(hd, id, PRIAUD_CTRL_INVCLK_INVERTED);
		break;
	case AIO_ID_MIC2_RX:
		aio_set_invfs(hd, id, PRIAUD_CTRL_INVFS_NORMAL);
		aio_set_invclk(hd, id, PRIAUD_CTRL_INVCLK_INVERTED);
		break;
	case AIO_ID_MIC6_RX:
		aio_set_invfs(hd, id, PRIAUD_CTRL_INVFS_NORMAL);
		aio_set_invclk(hd, id, PRIAUD_CTRL_INVCLK_INVERTED);
		break;
	default:
		pr_err("%s , %d not support\n", __func__, id);
		return;
	}

	aio_set_data_fmt(hd, id, data_fmt);
	aio_set_width_sample(hd, id, width_sample);
	aio_set_width_word(hd, id, width_word);
}
EXPORT_SYMBOL(aio_set_ctl);

static void mute_aio(struct aio_priv *aio, u32 id)
{
	u32 tsd;
	T32AUDCH_CTRL reg;
	u32 address;
	u32 tsd_end = AIO_TSD3;

	switch (id) {
	case AIO_ID_PRI_TX:
	case AIO_ID_MIC_RX:
	case AIO_ID_MIC4_RX:
	case AIO_ID_MIC5_RX:
	case AIO_ID_MIC6_RX:
		tsd_end = AIO_TSD3;
		break;

	case AIO_ID_SEC_TX:
	case AIO_ID_HDMI_TX:
	case AIO_ID_SPDIF_TX:
	case AIO_ID_MIC2_RX:
		tsd_end = AIO_TSD1;
		break;
	default:
		break;
	}

	for (tsd = AIO_TSD0; tsd < tsd_end; tsd++) {
		address = (aio_get_offset_from_ch(id, tsd) +
				   RA_AUDCH_CTRL);
		reg.u32 = aio_read(aio, address);
		reg.uCTRL_MUTE = AUDCH_CTRL_MUTE_MUTE_ON;
		reg.uCTRL_ENABLE = AUDCH_CTRL_ENABLE_DISABLE;
		aio_write(aio, address, reg.u32);
	}
}

int aio_reset(void *hd)
{
	struct aio_priv *aio = hd_to_aio(hd);

	aio_enabletxport(hd, AIO_ID_PRI_TX, 0);
	aio_enablerxport(hd, AIO_ID_MIC1_RX, 0);
	aio_enablerxport(hd, AIO_ID_MIC2_RX, 0);
	aio_enablerxport(hd, AIO_ID_MIC6_RX, 0);

	mute_aio(aio, AIO_ID_PRI_TX);
	mute_aio(aio, AIO_ID_SEC_TX);
	mute_aio(aio, AIO_ID_MIC_RX);
	mute_aio(aio, AIO_ID_MIC2_RX);
	mute_aio(aio, AIO_ID_MIC6_RX);
	mute_aio(aio, AIO_ID_HDMI_TX);
	return 0;
}
EXPORT_SYMBOL(aio_reset);

int aio_setclkdiv(void *hd, u32 id, u32 div)
{
	struct aio_priv *aio = hd_to_aio(hd);
	u32 address;
	T32PRIAUD_CLKDIV reg;

	address = aio_get_ctrl_base_addr(id);
	if (address == INVALID_ADDRESS || !div)
		return -EFAULT;

	address += RA_PRIAUD_CLKDIV;
	reg.u32 = aio_read(aio, address);
	/*
	 * Audio Master Clock (MCLK) Divider register
	 * decides the ratio between MCLK and Audio Bit Clock (BCLK).
	 */
	reg.uCLKDIV_SETTING = div;
	aio_write(aio, address, reg.u32);
	return 0;
}
EXPORT_SYMBOL(aio_setclkdiv);

int __weak aio_setspdifclk(void *hd, u32 div)
{
	pr_debug("%s , stub\n", __func__);
	return 0;
}
EXPORT_SYMBOL(aio_setspdifclk);

int __weak aio_setspdif_en(void *hd, bool enable)
{
	pr_debug("%s , stub\n", __func__);
	return 0;
}
EXPORT_SYMBOL(aio_setspdif_en);

int aio_getirqsts(void *hd)
{
	struct aio_priv *aio = hd_to_aio(hd);
	T32AIO_IRQSTS reg;

	reg.u32 = aio_read(aio, RA_AIO_IRQSTS);
	return reg.u32;
}
EXPORT_SYMBOL(aio_getirqsts);

static void aio_set_aud_ctrl(struct aio_priv *aio, u32 address, struct aud_ctrl *ctrl)
{
	T32PRIAUD_CTRL reg;

	reg.u32 = aio_read(aio, address);

	reg.uCTRL_LEFTJFY = !ctrl->isleftjfy;
	reg.uCTRL_INVCLK = ctrl->invbclk;
	reg.uCTRL_INVFS = ctrl->invfs;
	reg.uCTRL_TLSB = !ctrl->msb;
	reg.uCTRL_TDM = ctrl->sample_resolution;
	reg.uCTRL_TCF = ctrl->sample_period_in_bclk;
	reg.uCTRL_TFM = ctrl->data_fmt;
	reg.uCTRL_TDMMODE = ctrl->istdm;
	if (ctrl->istdm) {
		/* 0: short frame, 1: long frame */
		reg.uCTRL_TDMWSHIGH = ctrl->islframe;
		/* 2/4/6/8 channels */
		reg.uCTRL_TDMCHCNT = (ctrl->chcnt > 1) ? (ctrl->chcnt >> 1) - 1 : 0;
	}

	aio_write(aio, address, reg.u32);
}

int aio_set_ctl_ext(void *hd,
		u32 id, struct aud_ctrl *ctrl)
{
	struct aio_priv *aio = hd_to_aio(hd);
	u32 address;

	if ((!aio) || (!ctrl) || IS_INVALID_ID(id))
		return -EINVAL;

	address = aio_get_ctrl_address(id);
	aio_set_aud_ctrl(aio, address, ctrl);

	return 0;
}
EXPORT_SYMBOL(aio_set_ctl_ext);

int __weak aio_set_interleaved_mode(void *hd, u32 id, u32 src, u32 ch_map)
{
	pr_debug("%s , stub\n", __func__);
	return 0;
}
