// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2019-2020 Synaptics Incorporated */

#include "aio_hal.h"
#include "aio_common.h"
#include "i2s_common.h"
#include "avioDhub.h"
#include "avioGbl.h"
#include <linux/delay.h>
#include "avio_sub_module.h"

int aio_setirq(void *hd,
		u32 pri, u32 sec,
		u32 mic, u32 spdif, u32 hdmi)
{
	/*not support*/
	return 0;
}
EXPORT_SYMBOL(aio_setirq);

int aio_selhdport(void *hd, u32 sel)
{
	/*not support*/
	return 0;
}
EXPORT_SYMBOL(aio_selhdport);

int aio_selhdsource(void *hd, u32 sel)
{
	/*not support*/
	return 0;
}
EXPORT_SYMBOL(aio_selhdsource);

int aio_i2s_mclk_ctrl(void *hd, u32 id, u32 en)
{
	struct aio_priv *aio = hd_to_aio(hd);
	T32ACLK_ACLK_CTRL reg;
	u32 address;
	u32 val;

	switch (id) {
	case AIO_ID_I2S1_TX:
	case AIO_ID_I2S1_RX:
		address = RA_AIO_MCLKSEC1;
		break;
	case AIO_ID_I2S2_TX:
	case AIO_ID_I2S2_RX:
		address = RA_AIO_MCLKSEC2;
		break;
	case AIO_ID_I2S3_TX:
	case AIO_ID_I2S3_RX:
		address = RA_AIO_MCLKSEC3;
		break;
	case AIO_ID_I2S4_TX:
	case AIO_ID_I2S4_RX:
		address = RA_AIO_MCLKSEC4;
		break;
	case AIO_ID_I2S5_TX:
	case AIO_ID_I2S5_RX:
		address = RA_AIO_MCLKSEC5;
		break;
	case AIO_ID_PDM_IN:
		address = RA_AIO_MCLKPDM;
		break;
	default:
		pr_err("%s, id(%d) not supported\n", __func__, id);
		return -EINVAL;
	}

	reg.u32 = aio_read(aio, address);
	val = reg.u32;
	reg.uACLK_CTRL_clk_Enable = en ? 1 : 0;

	if (val != reg.u32) {
		aio_write(aio, address, reg.u32);
		dev_dbg(aio->dev, "%s: chid%d mclk en %d\n", __func__, id, en);
	} else {
		dev_dbg(aio->dev, "%s: chid%d mclk  already set to %d\n", __func__, id, en);
	}

	return 0;
}
EXPORT_SYMBOL(aio_i2s_mclk_ctrl);

int aio_i2s_set_clock(void *hd, u32 id, u32 clkSwitch,
	 u32 clkD3Switch, u32 clkSel, u32 pllUsed, u32 en)
{
	struct aio_priv *aio = hd_to_aio(hd);
	T32ACLK_ACLK_CTRL reg;
	u32 address;
	u32 val;

	switch (id) {
	case AIO_ID_I2S1_TX:
	case AIO_ID_I2S1_RX:
		address = RA_AIO_MCLKSEC1;
		break;
	case AIO_ID_I2S2_TX:
	case AIO_ID_I2S2_RX:
		address = RA_AIO_MCLKSEC2;
		break;
	case AIO_ID_I2S3_TX:
	case AIO_ID_I2S3_RX:
		address = RA_AIO_MCLKSEC3;
		break;
	case AIO_ID_I2S4_TX:
	case AIO_ID_I2S4_RX:
		address = RA_AIO_MCLKSEC4;
		break;
	case AIO_ID_I2S5_TX:
	case AIO_ID_I2S5_RX:
		address = RA_AIO_MCLKSEC5;
		break;
	case AIO_ID_PDM_IN:
		address = RA_AIO_MCLKPDM;
		break;
	default:
		pr_err("%s, id(%d) not supported\n", __func__, id);
		return -EINVAL;
	}

	reg.u32 = aio_read(aio, address);
	val = reg.u32;
	reg.uACLK_CTRL_apll0_clk_sel = clkSwitch;
	reg.uACLK_CTRL_apll1_clk_sel = clkSwitch;
	reg.uACLK_CTRL_div_val = clkSel;
	reg.uACLK_CTRL_src_sel = pllUsed;
	reg.uACLK_CTRL_clk_Enable = en ? 1 : 0;

	if (val != reg.u32) {
		/* toggle sw_sync_rst bit to do reset */
		reg.uACLK_CTRL_sw_sync_rst = 0;
		aio_write(aio, address, reg.u32);
		usleep_range(1000, 2000);
		reg.uACLK_CTRL_sw_sync_rst = 1;
		aio_write(aio, address, reg.u32);
		dev_dbg(aio->dev, "%s: chid%d %08x apll %d outf %d apll_div %d en %d\n",
				__func__, id, reg.u32, pllUsed, clkSwitch, clkSel, en);
	} else {
		dev_dbg(aio->dev, "%s: %p@%08x already %08x apll %d apll_div %d\n",
				__func__, aio->pbase, address, reg.u32, pllUsed, clkSel);
	}

	return 0;
}
EXPORT_SYMBOL(aio_i2s_set_clock);

u32 aio_get_tsd_from_chid(void *hd, u32 chid)
{
	return AIO_TSD0;
}
EXPORT_SYMBOL(aio_get_tsd_from_chid);

int aio_set_xfeed_mode(void *hd, s32 i2sid, u32 lrclk, u32 bclk)
{
	struct aio_priv *aio = hd_to_aio(hd);
	T32AIO_XFEED1 reg1;
	T32AIO_XFEED2 reg2;
	u32 offset;

	offset = RA_AIO_XFEED1;
	reg1.u32 = aio_read(aio, offset);

	switch (i2sid) {
	case AIO_ID_I2S1_TX:
	case AIO_ID_I2S1_RX:
		reg1.uXFEED1_I2S1_LRCKIO_MODE = lrclk;
		reg1.uXFEED1_I2S1_BCLKIO_MODE = bclk;
		break;
	case AIO_ID_I2S2_TX:
	case AIO_ID_I2S2_RX:
		reg1.uXFEED1_I2S2_LRCKIO_MODE = lrclk;
		reg1.uXFEED1_I2S2_BCLKIO_MODE = bclk;
		break;
	case AIO_ID_I2S3_TX:
	case AIO_ID_I2S3_RX:
		reg1.uXFEED1_I2S3_LRCKIO_MODE = lrclk;
		reg1.uXFEED1_I2S3_BCLKIO_MODE = bclk;
		break;
	default:
		offset = RA_AIO_XFEED2;
		reg2.u32 = aio_read(aio, offset);
		switch (i2sid) {
		case AIO_ID_I2S4_TX:
		case AIO_ID_I2S4_RX:
			reg2.uXFEED2_I2S4_LRCKIO_MODE = lrclk;
			reg2.uXFEED2_I2S4_BCLKIO_MODE = bclk;
			break;
		case AIO_ID_I2S5_TX:
		case AIO_ID_I2S5_RX:
			reg2.uXFEED2_I2S5_LRCKIO_MODE = lrclk;
			reg2.uXFEED2_I2S5_BCLKIO_MODE = bclk;
			break;
		default:
			pr_err("%s, id(%d) is not supported\n", __func__, i2sid);
			return -EINVAL;
		}
		aio_write(aio, offset, reg2.u32);
		dev_dbg(aio->dev, "%s, chid %d xfeed2 %04X\n", __func__, i2sid, lrclk);
		return 0;
	}
	aio_write(aio, offset, reg1.u32);
	dev_dbg(aio->dev, "%s, chid %d xfeed1 %04X\n", __func__, i2sid, lrclk);
	return 0;
}
EXPORT_SYMBOL(aio_set_xfeed_mode);

int aio_get_bclk_xfeed_mode(void *hd, s32 i2sid)
{
	struct aio_priv *aio = hd_to_aio(hd);
	T32AIO_XFEED1 reg1;
	T32AIO_XFEED2 reg2;
	u32 offset, val = 0;

	offset = RA_AIO_XFEED1;
	reg1.u32 = aio_read(aio, offset);

	switch (i2sid) {
	case AIO_ID_I2S1_TX:
	case AIO_ID_I2S1_RX:
		val = reg1.uXFEED1_I2S1_BCLKIO_MODE;
		break;
	case AIO_ID_I2S2_TX:
	case AIO_ID_I2S2_RX:
		val = reg1.uXFEED1_I2S2_BCLKIO_MODE;
		break;
	case AIO_ID_I2S3_TX:
	case AIO_ID_I2S3_RX:
		val = reg1.uXFEED1_I2S3_BCLKIO_MODE;
		break;
	default:
		offset = RA_AIO_XFEED2;
		reg2.u32 = aio_read(aio, offset);
		switch (i2sid) {
		case AIO_ID_I2S4_TX:
		case AIO_ID_I2S4_RX:
			val = reg2.uXFEED2_I2S4_BCLKIO_MODE;
			break;
		case AIO_ID_I2S5_TX:
		case AIO_ID_I2S5_RX:
			val = reg2.uXFEED2_I2S5_BCLKIO_MODE;
			break;
		default:
			pr_err("%s, id(%d) is not supported\n", __func__, i2sid);
			return -EINVAL;
		}
		return val;
	}
	return val;
}
EXPORT_SYMBOL(aio_get_bclk_xfeed_mode);

int aio_set_i2s_clk_enable(void *hd, u32 clk_idx, bool enable)
{
	struct aio_handle *handle = hd;
	struct aio_priv *aio = hd_to_aio(hd);
	T32avioGbl_CTRL0 reg;

	dev_dbg(aio->dev, "%s: %s (clk_idx:%d, enable:%d)\n", handle->name, __func__, clk_idx, enable);

	reg.u32 = aio_read_gbl(aio, RA_avioGbl_CTRL0);

	switch (clk_idx) {
	case AIO_I2S_I2S1_MCLK:
		reg.uCTRL0_I2S1_MCLK_OEN = enable;
		break;
	case AIO_I2S_I2S2_MCLK:
		reg.uCTRL0_I2S2_MCLK_OEN = enable;
		break;
	case AIO_PDM_CLK:
		reg.uCTRL0_PDM_CLK_OEN  = enable;
		break;
	default:
		pr_err("clk_idx(%d) not support %s\n", clk_idx, __func__);
		return -EINVAL;
	}

	aio_write_gbl(aio, RA_avioGbl_CTRL0, reg.u32);
	return 0;
}
EXPORT_SYMBOL(aio_set_i2s_clk_enable);

int aio_i2s_clk_sync_reset(void *hd, u32 id)
{
	struct aio_priv *aio = hd_to_aio(hd);
	T32ACLK_ACLK_CTRL reg;
	u32 address;

	switch (id) {
	case AIO_ID_I2S1_TX:
	case AIO_ID_I2S1_RX:
		address = RA_AIO_MCLKSEC1;
		break;
	case AIO_ID_I2S2_TX:
	case AIO_ID_I2S2_RX:
		address = RA_AIO_MCLKSEC2;
		break;
	case AIO_ID_I2S3_TX:
	case AIO_ID_I2S3_RX:
		address = RA_AIO_MCLKSEC3;
		break;
	case AIO_ID_I2S4_TX:
	case AIO_ID_I2S4_RX:
		address = RA_AIO_MCLKSEC4;
		break;
	case AIO_ID_I2S5_TX:
	case AIO_ID_I2S5_RX:
		address = RA_AIO_MCLKSEC5;
		break;
	case AIO_ID_PDM_IN:
		address = RA_AIO_MCLKPDM;
		break;
	default:
		pr_err("%s, id(%d) not supported\n", __func__, id);
		return -EINVAL;
	}

	reg.u32 = aio_read(aio, address);

	/* toggle sw_sync_rst bit to do reset */
	reg.uACLK_CTRL_sw_sync_rst = 0;
	aio_write(aio, address, reg.u32);
	usleep_range(1000, 2000);
	reg.uACLK_CTRL_sw_sync_rst = 1;
	aio_write(aio, address, reg.u32);
	dev_dbg(aio->dev, "%s: %p@%08x --> %08x\n", __func__, aio->pbase, address, reg.u32);

	return 0;
}
EXPORT_SYMBOL(aio_i2s_clk_sync_reset);

u32 aio_get_ctrl_base_addr(u32 id)
{
	u32 address = aio_get_reg_base_addr(id);

	switch (id) {
	case AIO_ID_I2S1_TX:
	case AIO_ID_I2S2_TX:
	case AIO_ID_I2S3_TX:
	case AIO_ID_I2S4_TX:
	case AIO_ID_I2S5_TX:
		address += RA_SEC_SECAUD;
		break;
	case AIO_ID_I2S1_RX:
	case AIO_ID_I2S2_RX:
	case AIO_ID_I2S3_RX:
	case AIO_ID_I2S4_RX:
	case AIO_ID_I2S5_RX:
		address += RA_MIC2_MICCTRL;
		break;
	default:
		/* will return AIO PRI address*/
		pr_warn("%s: id(%d) not supported\n", __func__, id);
		return INVALID_ADDRESS;
	}
	return address;
}
EXPORT_SYMBOL(aio_get_ctrl_base_addr);

u32 aio_get_reg_base_addr(u32 id)
{
	u32 chIdxI2SRegBase[] = {
	RA_AIO_SEC1, RA_AIO_MIC21, RA_AIO_SEC2, RA_AIO_MIC22, RA_AIO_SEC3, RA_AIO_MIC23,
	RA_AIO_SEC4, RA_AIO_MIC24, -1, RA_AIO_SEC5, RA_AIO_MIC25};
	u32 address = INVALID_ADDRESS;

	if (id >= ARRAY_SIZE(chIdxI2SRegBase)) {
		pr_warn("%s: id(%d) not supported\n", __func__, id);
		return address;
	}

	address = chIdxI2SRegBase[id];
	return address;
}

int aio_set_bclk_sel(void *hd, u32 id, u32 sel)
{
	struct aio_priv *aio = hd_to_aio(hd);
	u32 address;
	T32IOSELTX_SECBCLK1 reg_i2s1;
	T32IOSELTX_SECBCLK2 reg_i2s2;
	T32IOSELTX_SECBCLK3 reg_i2s3;
	T32IOSELTX_SECBCLK4 reg_i2s4;
	T32IOSELTX_SECBCLK5 reg_i2s5;

	/*
	 * sel is 2 bits number,
	 * 1 Bit Clock can be generated from Master Clock;
	 * 0 can be taken directly from the input.
	 */
	if ((aio == NULL) || IS_INVALID_ID(id) || IS_INVALID_2BITS_VAL(sel))
		return -EINVAL;


	switch (id) {
	case AIO_ID_I2S1_TX:
		address = RA_AIO_IOSELTX + RA_IOSELTX_SECBCLK1;
		reg_i2s1.u32 = aio_read(aio, address);
		reg_i2s1.uSECBCLK1_SEL = sel;
		aio_write(aio, address, reg_i2s1.u32);
		reg_i2s1.u32 = aio_read(aio, address);
		break;
	case AIO_ID_I2S2_TX:
		address = RA_AIO_IOSELTX + RA_IOSELTX_SECBCLK2;
		reg_i2s2.u32 = aio_read(aio, address);
		reg_i2s2.uSECBCLK2_SEL = sel;
		aio_write(aio, address, reg_i2s2.u32);
		reg_i2s2.u32 = aio_read(aio, address);
		break;
	case AIO_ID_I2S3_TX:
		address = RA_AIO_IOSELTX + RA_IOSELTX_SECBCLK3;
		reg_i2s3.u32 = aio_read(aio, address);
		reg_i2s3.uSECBCLK3_SEL = sel;
		aio_write(aio, address, reg_i2s3.u32);
		reg_i2s3.u32 = aio_read(aio, address);
		break;
	case AIO_ID_I2S4_TX:
		address = RA_AIO_IOSELTX + RA_IOSELTX_SECBCLK4;
		reg_i2s4.u32 = aio_read(aio, address);
		reg_i2s4.uSECBCLK4_SEL = sel;
		aio_write(aio, address, reg_i2s4.u32);
		reg_i2s4.u32 = aio_read(aio, address);
		break;
	case AIO_ID_I2S5_TX:
		address = RA_AIO_IOSELTX + RA_IOSELTX_SECBCLK5;
		reg_i2s5.u32 = aio_read(aio, address);
		reg_i2s5.uSECBCLK5_SEL = sel;
		aio_write(aio, address, reg_i2s5.u32);
		reg_i2s5.u32 = aio_read(aio, address);
		break;
	default:
		pr_err("%s id(%d) not supportd", __func__, id);
		break;
	}
	dev_dbg(aio->dev, "%s i2s idx %d BLCK select %d", __func__, id, sel);
	return 0;
}
EXPORT_SYMBOL(aio_set_bclk_sel);

int aio_set_bclk_inv(void *hd, u32 id, u32 inv)
{
	struct aio_priv *aio = hd_to_aio(hd);
	u32 address;
	T32IOSELTX_SECBCLK1 reg_i2s1;
	T32IOSELTX_SECBCLK2 reg_i2s2;
	T32IOSELTX_SECBCLK3 reg_i2s3;
	T32IOSELTX_SECBCLK4 reg_i2s4;
	T32IOSELTX_SECBCLK5 reg_i2s5;

	/*
	 * inv is 1 bit number,
	 */
	if ((aio == NULL) || IS_INVALID_ID(id) || IS_INVALID_1BIT_VAL(inv))
		return -EINVAL;

	switch (id) {
	case AIO_ID_I2S1_TX:
		address = RA_AIO_IOSELTX + RA_IOSELTX_SECBCLK1;
		reg_i2s1.u32 = aio_read(aio, address);
		reg_i2s1.uSECBCLK1_INV = inv;
		aio_write(aio, address, reg_i2s1.u32);
		reg_i2s1.u32 = aio_read(aio, address);
		break;
	case AIO_ID_I2S2_TX:
		address = RA_AIO_IOSELTX + RA_IOSELTX_SECBCLK2;
		reg_i2s2.u32 = aio_read(aio, address);
		reg_i2s2.uSECBCLK2_INV = inv;
		aio_write(aio, address, reg_i2s2.u32);
		reg_i2s2.u32 = aio_read(aio, address);
		break;
	case AIO_ID_I2S3_TX:
		address = RA_AIO_IOSELTX + RA_IOSELTX_SECBCLK3;
		reg_i2s3.u32 = aio_read(aio, address);
		reg_i2s3.uSECBCLK3_INV = inv;
		aio_write(aio, address, reg_i2s3.u32);
		reg_i2s3.u32 = aio_read(aio, address);
		break;
	case AIO_ID_I2S4_TX:
		address = RA_AIO_IOSELTX + RA_IOSELTX_SECBCLK4;
		reg_i2s4.u32 = aio_read(aio, address);
		reg_i2s4.uSECBCLK4_INV = inv;
		aio_write(aio, address, reg_i2s4.u32);
		reg_i2s4.u32 = aio_read(aio, address);
		break;
	case AIO_ID_I2S5_TX:
		address = RA_AIO_IOSELTX + RA_IOSELTX_SECBCLK5;
		reg_i2s5.u32 = aio_read(aio, address);
		reg_i2s5.uSECBCLK5_INV = inv;
		aio_write(aio, address, reg_i2s5.u32);
		reg_i2s5.u32 = aio_read(aio, address);
		break;
	default:
		pr_err("%s id(%d) not supportd", __func__, id);
		break;
	}
	dev_dbg(aio->dev, "%s i2s idx %d BLCK inv %d", __func__, id, inv);
	return 0;
}
EXPORT_SYMBOL(aio_set_bclk_inv);

int aio_set_mode(void *hd, u32 id, u32 portMode, u32 clkMode, u32 dataMode)
{
	struct aio_priv *aio = hd_to_aio(hd);
	u32 address;
	T32SEC_SECPORT reg_secport;
	T32MIC2_RXPORT reg_mic2port;

	switch (id) {
	case AIO_ID_I2S1_TX:
	case AIO_ID_I2S2_TX:
	case AIO_ID_I2S3_TX:
	case AIO_ID_I2S4_TX:
	case AIO_ID_I2S5_TX:
		address = aio_get_reg_base_addr(id) + RA_SEC_SECPORT;
		reg_secport.u32 = aio_read(aio, address);
		reg_secport.uSECPORT_MODE = portMode;
		aio_write(aio, address, reg_secport.u32);
		dev_dbg(aio->dev, "%s TX idx %d mode %d\n", __func__, id, portMode);
		break;
	case AIO_ID_I2S1_RX:
	case AIO_ID_I2S2_RX:
	case AIO_ID_I2S3_RX:
	case AIO_ID_I2S4_RX:
	case AIO_ID_I2S5_RX:
		address = aio_get_reg_base_addr(id) + RA_MIC2_RXPORT;
		reg_mic2port.u32 = aio_read(aio, address);
		reg_mic2port.uRXPORT_sw_sync_rst = false;
		aio_write(aio, address, reg_mic2port.u32);
		usleep_range(1000, 2000);
		reg_mic2port.uRXPORT_MODE = portMode;
		reg_mic2port.uRXPORT_ILB = clkMode;
		reg_mic2port.uRXPORT_LBD = dataMode;
		reg_mic2port.uRXPORT_sw_sync_rst = true;
		aio_write(aio, address, reg_mic2port.u32);
		dev_dbg(aio->dev, "%s RX idx %d mode %d clkMode 0x%02X dataMode 0x%02X\n",
				__func__, id, portMode, clkMode, dataMode);
		break;
	default:
		pr_err("%s chid%d not supported\n", __func__, id);
		return -EINVAL;
	}
	return 0;
}
EXPORT_SYMBOL(aio_set_mode);

int aio_set_pcm_mono(void *hd, u32 id, bool en)
{
	struct aio_priv *aio = hd_to_aio(hd);
	u32 address;
	TPRIAUD_CTRL1 reg;

	if (aio == NULL)
		return -EINVAL;

	address = aio_get_ctrl_base_addr(id) + RA_PRIAUD_CTRL1;

	reg.u32[0] = aio_read(aio, address);
	reg.uCTRL1_PCM_MONO_CH = en;
	aio_write(aio, address, reg.u32[0]);
	return 0;
}
EXPORT_SYMBOL(aio_set_pcm_mono);

/* WS (Fsync/LRCK) period setting when receiver is set to Master Mode and WS generator
 * acts as a standalone generator.
 *
 *  highP: High Cycle value for the FSYNC generation. writing 31 to this means the FSYNC
 *         is high for 32 BCLK
 *
 *  totalP: Total FSYNC Period. Writing 63 to this means the total FSYNC Period is 64 BCLK
 *
 *  wsInv:  To invert the FSYNC
 *      0 : Left Channel is High, Right Channel Low (LJ/RJ/TDM)
 *      1 : Right Channel is High, Left Channel Low (I2S)
 *
 */
int aio_set_mic_ws_prd(void *hd, u32 id, u32 highP, u32 totalP, u32 wsInv)
{
	struct aio_priv *aio = hd_to_aio(hd);
	u32 address;
	T32MIC2_MM_MODE reg;

	if (aio == NULL)
		return -EINVAL;

	address = aio_get_reg_base_addr(id) + RA_MIC2_MM_MODE;
	reg.u32 = aio_read(aio, address);
	reg.uMM_MODE_WS_TOTAL_PRD = totalP;
	reg.uMM_MODE_WS_HIGH_PRD = highP;
	reg.uMM_MODE_WS_INV = wsInv;
	aio_write(aio, address, reg.u32);
	dev_dbg(aio->dev, "%s chid %d MM mode settings totalP %d highP %d wsInv %d",
			__func__, id, totalP, highP, wsInv);
	return 0;
}
EXPORT_SYMBOL(aio_set_mic_ws_prd);

int aio_misc_sw_rst(void *hd, u32 option, u32 val)
{
	struct aio_priv *aio = hd_to_aio(hd);
	TAIO_SW_RST reg;
	u32 address = RA_AIO_SW_RST;

	reg.u32[0] = aio_read(aio, address);
	reg.uSW_RST_REFCLK = val;
	aio_write(aio, address, reg.u32[0]);

	return 0;
}
EXPORT_SYMBOL(aio_misc_sw_rst);

//tsd(unused) all SEC/MIC can be configured using TSD0/RSD0
u32 aio_get_offset_from_ch(u32 id, u32 tsd)
{
	u32 offset = 0;

	switch (id) {
	case AIO_ID_I2S1_TX:
	case AIO_ID_I2S2_TX:
	case AIO_ID_I2S3_TX:
	case AIO_ID_I2S4_TX:
	case AIO_ID_I2S5_TX:
		offset = aio_get_reg_base_addr(id) + RA_SEC_TSD0_SEC;
		break;
	case AIO_ID_I2S1_RX:
	case AIO_ID_I2S2_RX:
	case AIO_ID_I2S3_RX:
	case AIO_ID_I2S4_RX:
	case AIO_ID_I2S5_RX:
		offset = aio_get_reg_base_addr(id) + RA_MIC2_RSD0;
		break;
	default:
		offset = 0;
		pr_err("%s i2s%d not supported\n", __func__, id);
		break;
	}
	return offset;
}
EXPORT_SYMBOL(aio_get_offset_from_ch);

int aio_enablerxport(void *hd, u32 id, bool enable)
{
	struct aio_priv *aio = hd_to_aio(hd);
	u32 address;
	T32MIC2_RXPORT reg_micrxport;

	switch (id) {
	case AIO_ID_I2S1_RX:
	case AIO_ID_I2S2_RX:
	case AIO_ID_I2S3_RX:
	case AIO_ID_I2S4_RX:
	case AIO_ID_I2S5_RX:
		address = aio_get_reg_base_addr(id) + RA_MIC2_RXPORT;
		reg_micrxport.u32 = aio_read(aio, address);
		reg_micrxport.uRXPORT_ENABLE = enable;
		reg_micrxport.uRXPORT_sw_sync_rst = 1;
		aio_write(aio, address, reg_micrxport.u32);
		break;
	default:
		pr_err("%s chid %d not supported\n", __func__, id);
		return -EINVAL;
	}

	dev_dbg(aio->dev, "%s %s chid %d", __func__, enable ? "enable" : "disable", id);
	return 0;
}
EXPORT_SYMBOL(aio_enablerxport);

int aio_enabletxport(void *hd, u32 id, bool enable)
{
	struct aio_priv *aio = hd_to_aio(hd);
	u32 address;
	T32SEC_SECPORT reg_secport;

	switch (id) {
	case AIO_ID_I2S1_TX:
	case AIO_ID_I2S2_TX:
	case AIO_ID_I2S3_TX:
	case AIO_ID_I2S4_TX:
	case AIO_ID_I2S5_TX:
		address = aio_get_reg_base_addr(id) + RA_SEC_SECPORT;
		reg_secport.u32 = aio_read(aio, address);
		reg_secport.uSECPORT_ENABLE = enable;
		aio_write(aio, address, reg_secport.u32);
		break;
	default:
		pr_err("%s chid %d not supported\n", __func__, id);
		return -EINVAL;
	}

	dev_dbg(aio->dev, "%s %s chid %d", __func__, enable ? "enable" : "disable", id);
	return 0;
}
EXPORT_SYMBOL(aio_enabletxport);

int aio_misc_set_loopback_clk_gate(void *hd, u32 idx, u32 en)
{
	struct aio_priv *aio = hd_to_aio(hd);
	TAIO_CLK_GATE_EN reg;
	u32 address = RA_AIO_CLK_GATE_EN;

	reg.u32[0] = aio_read(aio, address);
	reg.uCLK_GATE_EN_GBL = en;
	aio_write(aio, address, reg.u32[0]);
	dev_dbg(aio->dev, "%s clk gate en %d", __func__, en);
	return 0;
}
EXPORT_SYMBOL(aio_misc_set_loopback_clk_gate);

static int aio_i2s_mic2_lpbk(struct aio_priv *aio, u32 idx,
				 u8 chan_num, u8 dummy_data)
{
	T32PRIAUD_CLKDIV reg_clkdiv;
	T32PRIAUD_CTRL reg_mic4ctrl;
	T32PRIAUD_CTRL reg_prictrl;
	T32PRIAUD_CLKDIV reg_tx_clkDiv;
	u32 i2sx_rx_address = 0;
	u32 i2sx_tx_address = 0;

	switch (idx) {
	case AIO_ID_I2S1_RX:
		i2sx_rx_address =  aio_get_ctrl_base_addr(idx);
		i2sx_tx_address = RA_AIO_SEC1 + RA_SEC_SECAUD;
		break;
	case AIO_ID_I2S2_RX:
		i2sx_rx_address = aio_get_ctrl_base_addr(idx);
		i2sx_tx_address = RA_AIO_SEC2 + RA_SEC_SECAUD;
		break;
	case AIO_ID_I2S3_RX:
		i2sx_rx_address =  aio_get_ctrl_base_addr(idx);
		i2sx_tx_address = RA_AIO_SEC3 + RA_SEC_SECAUD;
		break;
	case AIO_ID_I2S4_RX:
		i2sx_rx_address =  aio_get_ctrl_base_addr(idx);
		i2sx_tx_address = RA_AIO_SEC4 + RA_SEC_SECAUD;
		break;
	case AIO_ID_I2S5_RX:
		i2sx_rx_address = aio_get_ctrl_base_addr(idx);
		i2sx_tx_address = RA_AIO_SEC5 + RA_SEC_SECAUD;
		break;
	}

	reg_tx_clkDiv.u32 = aio_read(aio, i2sx_tx_address+RA_PRIAUD_CLKDIV);
	reg_clkdiv.uCLKDIV_SETTING = reg_tx_clkDiv.uCLKDIV_SETTING;
	aio_write(aio, i2sx_rx_address+RA_PRIAUD_CLKDIV, reg_clkdiv.u32);

	reg_prictrl.u32 = aio_read(aio, i2sx_tx_address + RA_PRIAUD_CTRL);
	reg_mic4ctrl.u32 = reg_prictrl.u32;
	reg_mic4ctrl.uCTRL_TDM = reg_prictrl.uCTRL_TDM;
	reg_mic4ctrl.uCTRL_TCF = reg_prictrl.uCTRL_TCF;
	reg_mic4ctrl.uCTRL_TDMMODE = reg_prictrl.uCTRL_TDMMODE;
	aio_write(aio, i2sx_rx_address + RA_PRIAUD_CTRL, reg_mic4ctrl.u32);
	dev_dbg(aio->dev, "%s i2s Tx2Rx lpbk %d, ch %d TDM Mode %d",
		__func__, idx, chan_num, reg_mic4ctrl.uCTRL_TDMMODE);
	return 0;
}

int aio_configure_loopback(void *hd, u32 id, u8 chan_num, u8 dummy_data)
{
	struct aio_priv *aio = hd_to_aio(hd);
	int ret = 0;

	ret = aio_i2s_mic2_lpbk(aio, id, chan_num, dummy_data);

	return ret;
}
EXPORT_SYMBOL(aio_configure_loopback);
