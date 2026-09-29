// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 - 2023 Synaptics Incorporated
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "ispDhub.h"
#include "ispSS_be_scl_module.h"
#include "ispSS_be_Mtr.h"
#include "mtr.h"
#include "ispSS_reg.h"
#include "ispSS_memmap.h"
#include "ispMISC.h"

#define MTR_BE_WRITE   0
#define MTR_BE_READ    1
#define ISP_BE_GLB_REMAP_ADDR (MEMMAP_ISP_REG_BASE + ISPSS_MEMMAP_GLB_REG_BASE + RA_IspMISC_remap)
#define GET_DHUB_CHANNEL_MAP_ID(y_channel, uv_channel) ((1 << y_channel) + (1 << uv_channel))
#define MTRTIDSUGGVAL 0x80

const int globalMTRThredID[ISPSS_BE_MTR_PATH_MAX][ISP_CHANNEL_MAX] = { {0x30, 0x34},
	{0x4C, 0x50},
	{0x44, 0x48},
	{0x28, 0x2C},
	{0x20, 0x24},
	{0x5C, 0x60},
	{0x54, 0x58}
};

/***************************************************************************************
 * FUNCTION: Set global registers for MTR
 * PARAMS:   ispObj - Scl object
 * RETURN:   ISPSS_MTR_SUCCESS - succeed.
 *           ISPSS_BE_MTR_ERROR - bad input parameters.
 ***************************************************************************************/
UINT32 ISPSS_BE_MTR_Set_Global_Registers(int path, int enable)
{
	UINT32 uiRetVal = ISPSS_BE_MTR_SUCCESS;
	UINT32 ispTsbFlagReg = ISP_BE_GLB_REMAP_ADDR + RA_Remap_TsbMtrFlag;
	UINT32 isFwrFlagReg  = ISP_BE_GLB_REMAP_ADDR + RA_Remap_FwrMtrFlag;
	UINT32 ispMtrid = ISP_BE_GLB_REMAP_ADDR + RA_Remap_MtrTid;

	UINT32 mtrFlag = 0;
	UINT32 y_channel = 0, uv_channel = 1;

	ISPSS_REG_READ32(ispTsbFlagReg, &mtrFlag);

	switch (path) {
	case ISPSS_BE_DNSCL_RD:
		ISPSS_REG_WRITE32(ispMtrid + (globalMTRThredID[path][y_channel]),
									ISPSS_BE_MTR_THREAD_2);
		ISPSS_REG_WRITE32(ispMtrid + (globalMTRThredID[path][uv_channel]),
									ISPSS_BE_MTR_THREAD_3);
		if (enable == false) {
			mtrFlag &= ~(GET_DHUB_CHANNEL_MAP_ID(ispDhubChMap_TSB_Dnscl_R0,
						ispDhubChMap_TSB_Dnscl_R1));
		} else {
			mtrFlag |= (GET_DHUB_CHANNEL_MAP_ID(ispDhubChMap_TSB_Dnscl_R0,
						ispDhubChMap_TSB_Dnscl_R1));
		}
		ISPSS_REG_WRITE32(ispTsbFlagReg, mtrFlag);
		break;
	case ISPSS_BE_ROTATE_RD:
		ISPSS_REG_WRITE32(ispMtrid + (globalMTRThredID[path][y_channel]),
								ISPSS_BE_MTR_THREAD_8);
		ISPSS_REG_WRITE32(ispMtrid + (globalMTRThredID[path][uv_channel]),
								ISPSS_BE_MTR_THREAD_9);
		ISPSS_REG_WRITE32(ispMtrid + ((globalMTRThredID[path][y_channel]) +
					MTRTIDSUGGVAL), ISPSS_BE_MTR_THREAD_8);
		ISPSS_REG_WRITE32(ispMtrid + ((globalMTRThredID[path][uv_channel]) +
					MTRTIDSUGGVAL), ISPSS_BE_MTR_THREAD_9);
		if (enable == false) {
			mtrFlag &= ~(GET_DHUB_CHANNEL_MAP_ID(ispDhubChMap_FWR_Rot_R0,
					ispDhubChMap_FWR_Rot_R1));
		} else {
			mtrFlag |= (GET_DHUB_CHANNEL_MAP_ID(ispDhubChMap_FWR_Rot_R0,
						ispDhubChMap_FWR_Rot_R1));
		}
		ISPSS_REG_WRITE32(isFwrFlagReg, mtrFlag);
		break;
	case ISPSS_BE_DEWARP_RD:
		ISPSS_REG_WRITE32(ispMtrid + (globalMTRThredID[path][y_channel]),
						ISPSS_BE_MTR_THREAD_5); //DWP Read Client 0
		ISPSS_REG_WRITE32(ispMtrid + (globalMTRThredID[path][uv_channel]),
						ISPSS_BE_MTR_THREAD_6); //DWP Read client 1
		ISPSS_REG_WRITE32(ispMtrid + ((globalMTRThredID[path][y_channel]) +
					MTRTIDSUGGVAL), ISPSS_BE_MTR_THREAD_5);
		ISPSS_REG_WRITE32(ispMtrid + ((globalMTRThredID[path][uv_channel]) +
					MTRTIDSUGGVAL), ISPSS_BE_MTR_THREAD_6);
		if (enable == false) {
			mtrFlag &= ~(GET_DHUB_CHANNEL_MAP_ID(ispDhubChMap_FWR_Dwrp_R0,
						ispDhubChMap_FWR_Dwrp_R1));
		} else {
			mtrFlag |= (GET_DHUB_CHANNEL_MAP_ID(ispDhubChMap_FWR_Dwrp_R0,
						ispDhubChMap_FWR_Dwrp_R1));
		}
		ISPSS_REG_WRITE32(isFwrFlagReg, mtrFlag);
		break;
	case ISPSS_BE_DNSCL_WR:
		ISPSS_REG_WRITE32(ispMtrid + (globalMTRThredID[path][y_channel]),
							ISPSS_BE_MTR_THREAD_11);
		ISPSS_REG_WRITE32(ispMtrid + (globalMTRThredID[path][uv_channel]),
							ISPSS_BE_MTR_THREAD_12);
		if (enable == false) {
			mtrFlag &= ~(GET_DHUB_CHANNEL_MAP_ID(ispDhubChMap_TSB_Dnscl_W0,
						ispDhubChMap_TSB_Dnscl_W1));
		} else {
			mtrFlag |= (GET_DHUB_CHANNEL_MAP_ID(ispDhubChMap_TSB_Dnscl_W0,
						ispDhubChMap_TSB_Dnscl_W1));
		}
		ISPSS_REG_WRITE32(ispTsbFlagReg, mtrFlag); //SCL TSB channel mapping
		break;
	case ISPSS_BE_TILE_WR:
		ISPSS_REG_WRITE32(ispMtrid + (globalMTRThredID[path][y_channel]),
						ISPSS_BE_MTR_THREAD_5);
		ISPSS_REG_WRITE32(ispMtrid + (globalMTRThredID[path][uv_channel]),
						ISPSS_BE_MTR_THREAD_6);
		ISPSS_REG_WRITE32(ispMtrid + ((globalMTRThredID[path][y_channel]) +
					MTRTIDSUGGVAL), ISPSS_BE_MTR_THREAD_5);
		ISPSS_REG_WRITE32(ispMtrid + ((globalMTRThredID[path][uv_channel]) +
					MTRTIDSUGGVAL), ISPSS_BE_MTR_THREAD_6);
		if (enable == false) {
			mtrFlag &= ~(GET_DHUB_CHANNEL_MAP_ID(ispDhubChMap_TSB_Tile_W0,
						ispDhubChMap_TSB_Tile_W1));
		} else {
			mtrFlag |= (GET_DHUB_CHANNEL_MAP_ID(ispDhubChMap_TSB_Tile_W0,
						ispDhubChMap_TSB_Tile_W1));
		}
		ISPSS_REG_WRITE32(ispTsbFlagReg, mtrFlag); //TILE  TSB channel mapping
		break;
	case ISPSS_BE_ROTATE_WR:
		ISPSS_REG_WRITE32(ispMtrid + (globalMTRThredID[path][y_channel]),
					ISPSS_BE_MTR_THREAD_7);
		ISPSS_REG_WRITE32(ispMtrid + (globalMTRThredID[path][uv_channel]),
					ISPSS_BE_MTR_THREAD_8);
		ISPSS_REG_WRITE32(ispMtrid + ((globalMTRThredID[path][y_channel]) +
					MTRTIDSUGGVAL), ISPSS_BE_MTR_THREAD_7);
		ISPSS_REG_WRITE32(ispMtrid + ((globalMTRThredID[path][uv_channel]) +
					MTRTIDSUGGVAL), ISPSS_BE_MTR_THREAD_8);
		if (enable == false) {
			mtrFlag &= ~(GET_DHUB_CHANNEL_MAP_ID(ispDhubChMap_FWR_Rot_W0,
						ispDhubChMap_FWR_Rot_W1));
		} else {
			mtrFlag |= (GET_DHUB_CHANNEL_MAP_ID(ispDhubChMap_FWR_Rot_W0,
						ispDhubChMap_FWR_Rot_W1));
		}
		ISPSS_REG_WRITE32(isFwrFlagReg, mtrFlag);
		break;
	case ISPSS_BE_DEWARP_WR:
		ISPSS_REG_WRITE32(ispMtrid + (globalMTRThredID[path][y_channel]),
					ISPSS_BE_MTR_THREAD_13);
		ISPSS_REG_WRITE32(ispMtrid + (globalMTRThredID[path][uv_channel]),
					ISPSS_BE_MTR_THREAD_14);
		ISPSS_REG_WRITE32(ispMtrid + ((globalMTRThredID[path][y_channel]) +
					MTRTIDSUGGVAL), ISPSS_BE_MTR_THREAD_13);
		ISPSS_REG_WRITE32(ispMtrid + ((globalMTRThredID[path][uv_channel]) +
					MTRTIDSUGGVAL), ISPSS_BE_MTR_THREAD_14);
		if (enable == false) {
			mtrFlag &= ~(GET_DHUB_CHANNEL_MAP_ID(ispDhubChMap_FWR_Dwrp_W0,
						ispDhubChMap_FWR_Dwrp_W1));
		} else {
			mtrFlag |= (GET_DHUB_CHANNEL_MAP_ID(ispDhubChMap_FWR_Dwrp_W0,
						ispDhubChMap_FWR_Dwrp_W1));
		}
		ISPSS_REG_WRITE32(isFwrFlagReg, mtrFlag);
		break;
	default:
		uiRetVal = ISPSS_BE_MTR_ERROR;
		break;
	}
	return uiRetVal;
}
EXPORT_SYMBOL(ISPSS_BE_MTR_Set_Global_Registers);

/***************************************************************************************
 * FUNCTION: Set global registers for MTR
 * PARAMS:   ispObj - Scl object
 * RETURN:   ISPSS_MTR_SUCCESS - succeed.
 *           ISPSS_BE_MTR_ERROR - bad input parameters.
 ***************************************************************************************/
UINT32 ISPSS_BE_MTR_Set_Global_Registers_bcm(struct BCMBUF *pbcmbuf, int path)
{
	UINT32 uiRetVal = ISPSS_BE_MTR_SUCCESS;
	UINT32 ispTsbFlagReg = ISP_BE_GLB_REMAP_ADDR + RA_Remap_TsbMtrFlag;
	UINT32 isFwrFlagReg  = ISP_BE_GLB_REMAP_ADDR + RA_Remap_FwrMtrFlag;
	UINT32 ispMtrid = ISP_BE_GLB_REMAP_ADDR + RA_Remap_MtrTid;

	switch (path) {
	case ISPSS_BE_DNSCL_RD:
		ISPSS_BCMBUF_Write(pbcmbuf, ispMtrid + 0x30, 0x2);
		ISPSS_BCMBUF_Write(pbcmbuf, ispMtrid + 0x34, 0x3);
		ISPSS_BCMBUF_Write(pbcmbuf, ispTsbFlagReg, 0x30);
		break;
	case ISPSS_BE_ROTATE_RD:
		ISPSS_BCMBUF_Write(pbcmbuf, ispMtrid + 0x4C, 0x8);
		ISPSS_BCMBUF_Write(pbcmbuf, ispMtrid + 0x50, 0x9);
		ISPSS_BCMBUF_Write(pbcmbuf, isFwrFlagReg, 0x18);
		break;
	case ISPSS_BE_DEWARP_RD:
		ISPSS_BCMBUF_Write(pbcmbuf, ispMtrid + 0x44, 0x5); //DWP Read Client 0
		ISPSS_BCMBUF_Write(pbcmbuf, ispMtrid + (0x44+0x80), 0x5); //DWP Read Client 0
		ISPSS_BCMBUF_Write(pbcmbuf, ispMtrid + 0x48, 0x6); //DWP Read client 1
		ISPSS_BCMBUF_Write(pbcmbuf, ispMtrid + (0x48+0x80), 0x6); //DWP Read client 1
		//DWP FWR channel id mapping //Disabling matrix channel
		ISPSS_BCMBUF_Write(pbcmbuf, isFwrFlagReg, 0x6);
		break;
	case ISPSS_BE_DNSCL_WR:
		ISPSS_BCMBUF_Write(pbcmbuf, ispMtrid + 0x28, 0xB);
		ISPSS_BCMBUF_Write(pbcmbuf, ispMtrid + 0x2C, 0xC);
		ISPSS_BCMBUF_Write(pbcmbuf, ispTsbFlagReg, 0x3c); //SCL TSB channel mapping
		break;
	case ISPSS_BE_TILE_WR:
		ISPSS_BCMBUF_Write(pbcmbuf, ispMtrid + 0x20, 0x5);
		ISPSS_BCMBUF_Write(pbcmbuf, ispMtrid + 0x24, 0x6);
		ISPSS_BCMBUF_Write(pbcmbuf, ispTsbFlagReg, 0x3); //TILE  TSB channel mapping
		break;
	case ISPSS_BE_ROTATE_WR:
		ISPSS_BCMBUF_Write(pbcmbuf, ispMtrid + 0x5C, 0x7);
		ISPSS_BCMBUF_Write(pbcmbuf, ispMtrid + 0x60, 0x8);
		ISPSS_BCMBUF_Write(pbcmbuf, isFwrFlagReg, 0x180);
		break;
	case ISPSS_BE_DEWARP_WR:
		ISPSS_BCMBUF_Write(pbcmbuf, ispMtrid + 0x54, 0xD);
		ISPSS_BCMBUF_Write(pbcmbuf, ispMtrid + 0x58, 0xE);
		ISPSS_BCMBUF_Write(pbcmbuf, isFwrFlagReg, 0x60);
		break;
	default:
		uiRetVal = ISPSS_BE_MTR_ERROR;
		return uiRetVal;
	}
	return uiRetVal;
}
EXPORT_SYMBOL(ISPSS_BE_MTR_Set_Global_Registers_bcm);

/***************************************************************************************
 * FUNCTION: Return MMU cache base and ways details
 * PARAMS:   Bcm buffer pointer
 * RETURN:   ISPSS_BE_MTR_SUCCESS - succeed.
 *           ISPSS_BE_MTR_ERROR - bad input parameters.
 ***************************************************************************************/
static UINT32 ISPSS_BE_MTR_MTRR_GetCacheLines(enum ISPSS_BE_MTR_THREAD_ID eThreadId, UINT8 *base,
		UINT8 *ways)
{
	UINT32 uiRetVal = ISPSS_BE_MTR_SUCCESS;

	/*Tune it based on the system requirements for a product */
	switch (eThreadId) {
	case ISPSS_BE_MTR_THREAD_0:
		*base = 0;
		*ways = 1;
		break;
	case ISPSS_BE_MTR_THREAD_1:
		*base = 2;
		*ways = 1;
		break;
	case ISPSS_BE_MTR_THREAD_2:
		*base = 4;
		*ways = 1;
		break;
	case ISPSS_BE_MTR_THREAD_3:
		*base = 6;
		*ways = 1;
		break;
	case ISPSS_BE_MTR_THREAD_4:
		*base = 8;
		*ways = 1;
		break;
	case ISPSS_BE_MTR_THREAD_5:
		*base = 10;
		*ways = 1;
		break;
	case ISPSS_BE_MTR_THREAD_6:
		*base = 12;
		*ways = 1;
		break;
	case ISPSS_BE_MTR_THREAD_7:
		*base = 14;
		*ways = 1;
		break;
	case ISPSS_BE_MTR_THREAD_8:
		*base = 16;
		*ways = 1;
		break;
	case ISPSS_BE_MTR_THREAD_9:
		*base = 18;
		*ways = 1;
		break;
	case ISPSS_BE_MTR_THREAD_10:
		*base = 20;
		*ways = 1;
		break;
	case ISPSS_BE_MTR_THREAD_11:
		*base = 22;
		*ways = 1;
		break;
	case ISPSS_BE_MTR_THREAD_12:
		*base = 24;
		*ways = 1;
		break;
	case ISPSS_BE_MTR_THREAD_13:
		*base = 26;
		*ways = 1;
		break;
	case ISPSS_BE_MTR_THREAD_14:
		*base = 28;
		*ways = 1;
		break;
	case ISPSS_BE_MTR_THREAD_15:
		*base = 30;
		*ways = 1;
		break;
	default:
		*base = 0;
		*ways = 0;
		uiRetVal = ISPSS_BE_MTR_ERROR;
		break;
	}

	return uiRetVal;
}
/***************************************************************************************
 * FUNCTION: Return MMU cache base and ways details
 * PARAMS:   Bcm buffer pointer
 * RETURN:   ISPSS_BE_MTR_SUCCESS - succeed.
 *           ISPSS_BE_MTR_ERROR - bad input parameters.
 ***************************************************************************************/
static UINT32 ISPSS_BE_MTR_MTRW_GetCacheLines(enum ISPSS_BE_MTR_THREAD_ID eThreadId, UINT8 *base,
		UINT8 *ways)
{
	UINT32 uiRetVal = ISPSS_BE_MTR_SUCCESS;
	UINT8 base_val;

	base_val = 16;

	/*Tune it based on the system requirements for a product */
	switch (eThreadId) {
	case ISPSS_BE_MTR_THREAD_0:
		*base = base_val;
		*ways = 1;
		break;
	case ISPSS_BE_MTR_THREAD_1:
		*base = base_val + 2;
		*ways = 1;
		break;
	case ISPSS_BE_MTR_THREAD_2:
		*base = base_val + 4;
		*ways = 1;
		break;
	case ISPSS_BE_MTR_THREAD_3:
		*base = base_val + 6;
		*ways = 1;
		break;
	case ISPSS_BE_MTR_THREAD_4:
		*base = base_val + 8;
		*ways = 1;
		break;
	case ISPSS_BE_MTR_THREAD_5:
		*base = base_val + 10;
		*ways = 1;
		break;
	case ISPSS_BE_MTR_THREAD_6:
		*base = base_val + 12;
		*ways = 1;
		break;
	case ISPSS_BE_MTR_THREAD_7:
		*base = base_val + 14;
		*ways = 1;
		break;
	case ISPSS_BE_MTR_THREAD_8:
		*base = base_val + 16;
		*ways = 1;
		break;
	case ISPSS_BE_MTR_THREAD_9:
		*base = base_val + 18;
		*ways = 1;
		break;
	case ISPSS_BE_MTR_THREAD_10:
		*base = base_val + 20;
		*ways = 1;
		break;
	case ISPSS_BE_MTR_THREAD_11:
		*base = base_val + 22;
		*ways = 1;
		break;
	case ISPSS_BE_MTR_THREAD_12:
		*base = base_val + 24;
		*ways = 1;
		break;
	case ISPSS_BE_MTR_THREAD_13:
		*base = base_val + 26;
		*ways = 1;
		break;
	case ISPSS_BE_MTR_THREAD_14:
		*base = base_val + 28;
		*ways = 1;
		break;
	case ISPSS_BE_MTR_THREAD_15:
		*base = base_val + 30;
		*ways = 1;
		break;
	default:
		uiRetVal = ISPSS_BE_MTR_ERROR;
		break;
	}

	return uiRetVal;
}

/***************************************************************************************
 * FUNCTION: Configure MTR_R block of ISPMTR
 * PARAMS: Bcm buffer pointer
 * RETURN:  ISPSS_BE_MTR_SUCCESS - succeed.
 *          ISPSS_BE_MTR_ERROR - bad input parameters.
 ***************************************************************************************/
static UINT32 ISPSS_BE_MTR_MTRR_Config(struct BCMBUF *pbcmbuf,
		enum ISPSS_BE_MTR_THREAD_ID eThreadId,
		struct ISPSS_BE_MTR_MTRR_CFG *psIspmtrMtrrCfg)
{
	UINT32 uiRetVal = ISPSS_BE_MTR_SUCCESS;
	UINT32 uiRegAddr = 0;
	T32MTRR_CFG_cfg  sMtrrCfgCfg;
	T32MTRR_CFG_base sMtrrCfgBase;
	UINT8 base = 0xFF, ways = 0xFF;

	ISPSS_BE_MTR_MTRR_GetCacheLines(eThreadId, &base, &ways);

	uiRegAddr = ISP_BE_MTR_BASE_ADDR + RA_ISPMTR_mtrr +
			(eThreadId * sizeof(SIE_MTRR)) + RA_MTRR_mtr;

	sMtrrCfgCfg.u32                     = 0;
	sMtrrCfgCfg.ucfg_stride             = psIspmtrMtrrCfg->ucfg_stride;
	sMtrrCfgCfg.ucfg_format             = psIspmtrMtrrCfg->ucfg_format;
	sMtrrCfgCfg.ucfg_enable             = psIspmtrMtrrCfg->ucfg_enable;
	sMtrrCfgCfg.ucfg_mode               = psIspmtrMtrrCfg->ucfg_mode;
	sMtrrCfgCfg.ucfg_prefetch_disable   = 0;
	sMtrrCfgCfg.ucfg_cbase              = base;
	sMtrrCfgCfg.ucfg_cways              = ways;
	sMtrrCfgCfg.ucfg_auto_tag_clr       = 0;//1
	sMtrrCfgCfg.ucfg_mosaic_xnum        = 0;
	sMtrrCfgCfg.ucfg_mosaic_ynum        = 0;
	sMtrrCfgCfg.ucfg_weave              = 0;
	sMtrrCfgCfg.ucfg_hflip              = psIspmtrMtrrCfg->ucfg_hflip;
	sMtrrCfgCfg.ucfg_vflip              = psIspmtrMtrrCfg->ucfg_vflip;

	sMtrrCfgBase.u32         = 0;
	sMtrrCfgBase.ubase_addr  = psIspmtrMtrrCfg->ubase_addr;

	ISPSS_BE_MTR_CHECK_RETVAL(ISPSS_BCMBUF_Write(pbcmbuf, uiRegAddr + RA_MTRR_CFG_cfg,
				sMtrrCfgCfg.u32));
	ISPSS_BE_MTR_CHECK_RETVAL(ISPSS_BCMBUF_Write(pbcmbuf, uiRegAddr + RA_MTRR_CFG_base,
				sMtrrCfgBase.u32));

	return uiRetVal;
}

/***************************************************************************************
 * FUNCTION: Configure MTR_W block of ISPMTR
 * PARAMS: Bcm buffer pointer
 * RETURN:  ISPSS_BE_MTR_SUCCESS - succeed.
 *          ISPSS_BE_MTR_ERROR - bad input parameters.
 ***************************************************************************************/
static UINT32 ISPSS_BE_MTR_MTRW_Config(struct BCMBUF *pbcmbuf,
		enum ISPSS_BE_MTR_THREAD_ID eThreadId,
		struct ISPSS_BE_MTR_MTRR_CFG *psIspSclmtrMtrwCfg)
{
	UINT32 uiRetVal = ISPSS_BE_MTR_SUCCESS;
	UINT32 uiRegAddr = 0;
	T32MTRW_CFG_cfg  sMtrwCfgCfg;
	T32MTRW_CFG_base sMtrwCfgBase;

	uiRegAddr = ISP_BE_MTR_BASE_ADDR + RA_ISPMTR_mtrw +
				(eThreadId * sizeof(SIE_MTRW)) + RA_MTRW_mtr;
	sMtrwCfgCfg.u32                     = 0;
	sMtrwCfgCfg.ucfg_stride             = psIspSclmtrMtrwCfg->ucfg_stride;
	sMtrwCfgCfg.ucfg_format             = psIspSclmtrMtrwCfg->ucfg_format;
	sMtrwCfgCfg.ucfg_enable             = psIspSclmtrMtrwCfg->ucfg_enable;
	sMtrwCfgCfg.ucfg_mode               = psIspSclmtrMtrwCfg->ucfg_mode;
	sMtrwCfgCfg.ucfg_weave              = 0;
	sMtrwCfgCfg.ucfg_hflip              = psIspSclmtrMtrwCfg->ucfg_hflip;
	sMtrwCfgCfg.ucfg_vflip              = psIspSclmtrMtrwCfg->ucfg_vflip;

	sMtrwCfgBase.u32         = 0;
	sMtrwCfgBase.ubase_addr  = psIspSclmtrMtrwCfg->ubase_addr;

	ISPSS_BE_MTR_CHECK_RETVAL(ISPSS_BCMBUF_Write(pbcmbuf, uiRegAddr +
						RA_MTRW_CFG_cfg, sMtrwCfgCfg.u32));
	ISPSS_BE_MTR_CHECK_RETVAL(ISPSS_BCMBUF_Write(pbcmbuf, uiRegAddr +
						RA_MTRW_CFG_base, sMtrwCfgBase.u32));

	return uiRetVal;
}
/***************************************************************************************
 * FUNCTION: Configure ISPMTR MMU with frame descriptor
 * PARAMS: Bcm buffer pointer
 * RETURN:  ISPSS_BE_MTR_SUCCESS - succeed.
 *          ISPSS_BE_MTR_ERROR - bad input parameters.
 ***************************************************************************************/
static UINT32 ISPSS_BE_MTR_MMU_Config(struct BCMBUF *pbcmbuf,
		enum ISPSS_BE_MTR_THREAD_ID eThreadId,
		struct ISPSS_BE_MTR_MMU_CFG *psIspmtrMtrrMmuCfg, int mtrRdWr)
{
	UINT32 uiRetVal = ISPSS_BE_MTR_SUCCESS;
	UINT32 uiRegAddr = 0;
	T32MMU_CFG_pbm   sMmuCfgPbm;
	T32MMU_CFG_vm    sMmuCfgVm;
	T32MMU_CFG_cdesc sMmuCfgCdesc;
	UINT8 base = 0xFF, ways = 0xFF;

	if (mtrRdWr == MTR_BE_WRITE) {
		uiRegAddr = ISP_BE_MTR_BASE_ADDR + RA_ISPMTR_mtrw +
			(eThreadId * sizeof(SIE_MTRW)) + RA_MTRW_mmu;
		ISPSS_BE_MTR_MTRW_GetCacheLines(eThreadId, &base, &ways);
	} else {
		uiRegAddr = ISP_BE_MTR_BASE_ADDR + RA_ISPMTR_mtrr +
			(eThreadId * sizeof(SIE_MTRR)) + RA_MTRR_mmu;
		ISPSS_BE_MTR_MTRR_GetCacheLines(eThreadId, &base, &ways);
	}

	sMmuCfgPbm.u32              = 0;
	sMmuCfgPbm.upbm_shy_bw      = psIspmtrMtrrMmuCfg->upbm_shy_bw;
	sMmuCfgPbm.upbm_shy_pos     = psIspmtrMtrrMmuCfg->upbm_shy_pos;
	sMmuCfgPbm.upbm_stride_64B  = psIspmtrMtrrMmuCfg->upbm_stride_64B;
	sMmuCfgPbm.upbm_tileMode    = psIspmtrMtrrMmuCfg->upbm_tileMode;
	sMmuCfgPbm.upbm_pm_enable   = psIspmtrMtrrMmuCfg->upbm_pm_enable;
	sMmuCfgPbm.upbm_shuffle_en  = psIspmtrMtrrMmuCfg->upbm_shuffle_en;
	sMmuCfgPbm.upbm_bm_enable   = psIspmtrMtrrMmuCfg->upbm_bm_enable;
	sMmuCfgPbm.upbm_weave       = 0;
	sMmuCfgVm.u32                  = 0;
	sMmuCfgVm.uvm_enable           = psIspmtrMtrrMmuCfg->uvm_enable;
	sMmuCfgVm.uvm_prefecth_disable = 0;
	sMmuCfgVm.uvm_mode             = 1;
	sMmuCfgVm.uvm_mTyp             = 0;
	sMmuCfgVm.uvm_delta            = 0;
	sMmuCfgVm.uvm_base             = psIspmtrMtrrMmuCfg->uvm_base;
	sMmuCfgVm.uvm_auto_tag_clr     = 0;
	sMmuCfgCdesc.u32               = 0;
	sMmuCfgCdesc.ucdesc_base       = base;
	sMmuCfgCdesc.ucdesc_ways       = ways;
	sMmuCfgCdesc.ucdesc_rpTyp      = 1;

	ISPSS_BE_MTR_CHECK_RETVAL(ISPSS_BCMBUF_Write(pbcmbuf, uiRegAddr +
						RA_MMU_CFG_pbm, sMmuCfgPbm.u32));
	ISPSS_BE_MTR_CHECK_RETVAL(ISPSS_BCMBUF_Write(pbcmbuf, uiRegAddr +
						RA_MMU_CFG_vm, sMmuCfgVm.u32));
	ISPSS_BE_MTR_CHECK_RETVAL(ISPSS_BCMBUF_Write(pbcmbuf, uiRegAddr +
						RA_MMU_CFG_cdesc, sMmuCfgCdesc.u32));

	return uiRetVal;
}

/***************************************************************************************
 * FUNCTION: Prgrame MTRR block
 * PARAMS:   Bcm buffer pointer
 * RETURN:   ISPSS_BE_MTR_SUCCESS - succeed.
 *           ISPSS_BE_MTR_ERROR - bad input parameters.
 ***************************************************************************************/
static void ISPSS_BE_MTR_MTRR_Program(struct BCMBUF *pbcmbuf, struct ISPSS_BE_MTR *psIspmtr,
		enum ISPSS_BE_MTR_THREAD_ID eThreadId, BOOL kickOffEn,
		struct ISPSS_BE_MTR_MTRR_Prog *psIspmtrMtrrProg)
{

	if (kickOffEn == false)
		psIspmtr->uiIspmtrKickoff = (psIspmtr->uiIspmtrKickoff & ~(0x1 << eThreadId));
	else
		psIspmtr->uiIspmtrKickoff = (psIspmtr->uiIspmtrKickoff | (0x1 << eThreadId));
	ISPSS_BE_MTR_MTRR_Config(pbcmbuf, eThreadId, &psIspmtrMtrrProg->ispmtrMtrrCfg);
	ISPSS_BE_MTR_MMU_Config(pbcmbuf, eThreadId, &psIspmtrMtrrProg->ispmtrMmuCfg, MTR_BE_READ);

}
/***************************************************************************************
 * FUNCTION: Prgrame MTRW block
 * PARAMS:   Bcm buffer pointer
 * RETURN:   ISPSS_BE_MTR_SUCCESS - succeed.
 *           ISPSS_BE_MTR_ERROR - bad input parameters.
 ***************************************************************************************/
static void ISPSS_BE_MTR_MTRW_Program(struct BCMBUF *pbcmbuf, struct ISPSS_BE_MTR *psIspmtr,
		enum ISPSS_BE_MTR_THREAD_ID eThreadId, BOOL kickOffEn,
		struct ISPSS_BE_MTR_MTRR_Prog *psIspmtrMtrwProg)
{

	if (kickOffEn == false)
		psIspmtr->uiIspmtrKickoff = (psIspmtr->uiIspmtrKickoff & ~(0x1 << eThreadId));
	else
		psIspmtr->uiIspmtrKickoff = (psIspmtr->uiIspmtrKickoff | (0x1 << (16 + eThreadId)));
	if (psIspmtrMtrwProg->ispmtrMtrwCfg.ucfg_enable) //MTR enable
		ISPSS_BE_MTR_MTRW_Config(pbcmbuf, eThreadId, &psIspmtrMtrwProg->ispmtrMtrwCfg);

	ISPSS_BE_MTR_MMU_Config(pbcmbuf, eThreadId,
				&psIspmtrMtrwProg->ispmtrMmuCfg, MTR_BE_WRITE);

}

/***************************************************************************************
 * FUNCTION: Kickoff the active threads for next interrupt
 * PARAMS:   Bcm buffer pointer
 * RETURN:   ISPSS_BE_MTR_SUCCESS - succeed.
 *           ISPSS_BE_MTR_ERROR - bad input parameters.
 ***************************************************************************************/
UINT32 ISPSS_BE_MTR_kickOff(struct BCMBUF *pbcmbuf, UINT32 kickOff)
{
	UINT32 uiRetVal = ISPSS_BE_MTR_SUCCESS;
	UINT32 uiRegAddr = 0;
	INT32 i  = 0;

	uiRegAddr = ISP_BE_MTR_BASE_ADDR + RA_ISPMTR_kickoffR0;

	/*Kickoff shold be done after all the configs. And all thread kickoff should to together */
	for (i = 0; i < (ISPSS_BE_MTR_THREAD_MAX)*2; i++) {
		if (kickOff & (1<<i)) {
			ISPSS_BE_MTR_CHECK_RETVAL(ISPSS_BCMBUF_Write(pbcmbuf, uiRegAddr, 1));
			ISPSS_BE_MTR_CHECK_RETVAL(ISPSS_BCMBUF_Write(pbcmbuf, uiRegAddr, 0));
		}
		uiRegAddr += 4;
	}

	return uiRetVal;
}
EXPORT_SYMBOL(ISPSS_BE_MTR_kickOff);

/***************************************************************************************
 * FUNCTION:Return thread IDs for Luma and croma
 * PARAMS:  Bcm buffer pointer
 * RETURN:  ISPSS_BE_MTR_SUCCESS - succeed.
 *          ISPSS_BE_MTR_ERROR - bad input parameters.
 ***************************************************************************************/
static UINT32 ISPSS_BE_MTR_GetThreadIDs(UINT8 *threadID_Y, UINT8 *threadID_UV, int path)
{
	UINT32 uiRetVal = ISPSS_BE_MTR_SUCCESS;

	switch (path) {
	case ISPSS_BE_DNSCL_RD:
		*threadID_Y  = ISPSS_BE_MTR_THREAD_2;
		*threadID_UV = ISPSS_BE_MTR_THREAD_3;
		break;
	case ISPSS_BE_ROTATE_RD:
		*threadID_Y  = ISPSS_BE_MTR_THREAD_8;
		*threadID_UV = ISPSS_BE_MTR_THREAD_9;
		break;
	case ISPSS_BE_DEWARP_RD:
		*threadID_Y  = ISPSS_BE_MTR_THREAD_5;
		*threadID_UV = ISPSS_BE_MTR_THREAD_6;
		//Add for dewarp matrix,if required
		break;
	case ISPSS_BE_DNSCL_WR:
		*threadID_Y  = ISPSS_BE_MTR_THREAD_11;
		*threadID_UV = ISPSS_BE_MTR_THREAD_12;
		break;
	case ISPSS_BE_TILE_WR:
		*threadID_Y  = ISPSS_BE_MTR_THREAD_5;
		*threadID_UV = ISPSS_BE_MTR_THREAD_6;
		break;
	case ISPSS_BE_ROTATE_WR:
		*threadID_Y  = ISPSS_BE_MTR_THREAD_7;
		*threadID_UV = ISPSS_BE_MTR_THREAD_8;
		break;
	case ISPSS_BE_DEWARP_WR:
		*threadID_Y  = ISPSS_BE_MTR_THREAD_13;
		*threadID_UV = ISPSS_BE_MTR_THREAD_14;
		break;
	default:
		uiRetVal = ISPSS_BE_MTR_ERROR;
		break;
	}
	return uiRetVal;
}
/***************************************************************************************
 * FUNCTION: Update stride value based on source format
 * PARAMS:   pbcmbuf - BCM buffer pointer
 * RETURN:   ISPSS_BE_MTR_SUCCESS - succeed.
 *           ISPSS_BE_EBADPARAM - bad input parameters.
 ***************************************************************************************/
static UINT32 ISPSS_BE_MTR_GetStride(UINT32 m_srcfmt, UINT32 *stride_64B, UINT32 width,
		UINT32 m_bits_per_pixel)
{

	UINT32 pictureWidth = width;
	UINT32 uiRetVal = ISPSS_BE_MTR_SUCCESS;

	switch (m_srcfmt) {
	case ISPSS_SRCFMT_YUV422SP:
	case ISPSS_SRCFMT_YUV420SP:
	case ISPSS_SRCFMT_YUV422P:
	case ISPSS_SRCFMT_YUV444P:
		if (m_bits_per_pixel == 8)
			*stride_64B = DIV_ROUND_UP(pictureWidth, 256) * 4;
		else
			*stride_64B = DIV_ROUND_UP(((pictureWidth*10) / 8), 256) * 4;
		break;
	case ISPSS_SRCFMT_RGB888:
		*stride_64B = DIV_ROUND_UP(pictureWidth * 3, 256) * 4;
		break;
	case ISPSS_SRCFMT_YUV422SP_DWA:
	case ISPSS_SRCFMT_YUV420SP_DWA:
		*stride_64B = DIV_ROUND_UP(pictureWidth, 192) * 4;
		break;
	case ISPSS_SRCFMT_YUV420SP_TILED_V8H8:
	case ISPSS_SRCFMT_YUV422SP_TILED_V8H8:
		*stride_64B = DIV_ROUND_UP(pictureWidth, 32) * 4;
		break;
	case ISPSS_SRCFMT_YUV420SP_TILED_V8H6:
	case ISPSS_SRCFMT_YUV422SP_TILED_V8H6:
		*stride_64B = DIV_ROUND_UP(pictureWidth, 24) * 4;
		break;
	case ISPSS_SRCFMT_YUV420SP_TILED_V6H8:
	case ISPSS_SRCFMT_YUV422SP_TILED_V6H8:
		*stride_64B = DIV_ROUND_UP(pictureWidth, 32) * 4;
		break;
	default:
		*stride_64B = 0;
		uiRetVal = ISPSS_BE_MTR_ERROR;
		break;
	}
	return uiRetVal;
}
/***************************************************************************************
 * FUNCTION: Get MTR FMT and Mode
 * PARAMS:
 * RETURN:   ISPSS_BE_MTR_SUCCESS - succeed.
 *           ISPSS_BE_EBADPARAM - bad input parameters.
 ***************************************************************************************/
static UINT32 ISPSS_BE_MTR_GetMtrFmtandMode(UINT32 m_srcfmt, UINT32 m_bits_per_pixel,
		UINT32 *fmt, UINT32 *mode, bool luma)
{

	UINT32 uiRetVal = ISPSS_BE_MTR_SUCCESS;

	switch (m_srcfmt) {
	case ISPSS_SRCFMT_YUV422SP:
	case ISPSS_SRCFMT_YUV420SP:
	case ISPSS_SRCFMT_YUV422P:
	case ISPSS_SRCFMT_YUV444P:
	case ISPSS_SRCFMT_YUV422SP_DWA:
	case ISPSS_SRCFMT_YUV420SP_DWA:
	case ISPSS_SRCFMT_RGB888:
		if (m_bits_per_pixel == 8) {
			*fmt  = MTR_header_meta_format_V1H4V16;
			*mode = luma ? E_MTR_mode_8b_v1_y : E_MTR_mode_8b_v1_uv;
		} else {
			*fmt  = MTR_header_meta_format_V1H32V2;/*V1H32V2*/
			*mode = luma ? E_MTR_mode_d10b_v1_y : E_MTR_mode_d10b_v1_uv;
		}
		break;
	case ISPSS_SRCFMT_YUV420SP_TILED_V8H8:
	case ISPSS_SRCFMT_YUV422SP_TILED_V8H8:
	case ISPSS_SRCFMT_YUV420SP_TILED_V8H6:
	case ISPSS_SRCFMT_YUV422SP_TILED_V8H6:
	case ISPSS_SRCFMT_YUV420SP_TILED_V6H8:
	case ISPSS_SRCFMT_YUV422SP_TILED_V6H8:
	default:
		uiRetVal = ISPSS_BE_MTR_ERROR;
		break;
	}
	return uiRetVal;
}

/***************************************************************************************
 * FUNCTION: Update frame descriptor to ISP MTR registers
 * PARAMS:   Bcm buffer pointer
 * RETURN:   ISPSS_BE_MTR_SUCCESS - succeed.
 *           ISPSS_BE_MTR_ERROR - bad input parameters.
 ***************************************************************************************/
static UINT32 ISPSS_BE_MTR_UpdateReadDesc(struct BCMBUF *pbcmbuf,
		struct ISPSS_BE_MTR *psIspmtr, int path)
{
	UINT32 uiRetVal = ISPSS_BE_MTR_SUCCESS;
	struct ISPSS_BE_MTR_MTRR_Prog sIspmtrMtrProg;
	UINT8 threadID_Y = 0xFF, threadID_UV = 0xFF;
	BOOL kickOffEn = 0;
	UINT32 stride_64B = 0;

	ISPSS_BE_MTR_GetThreadIDs(&threadID_Y, &threadID_UV, path);
	psIspmtr->m_mtrR_chkSemId_Y  = threadID_Y;
	psIspmtr->m_mtrR_chkSemId_UV = threadID_UV;

	if (psIspmtr->m_srcfmt <= ISPSS_SRCFMT_YUV420SP_DWA)
		sIspmtrMtrProg.ispmtrMmuCfg.upbm_tileMode = 0;
	else
		sIspmtrMtrProg.ispmtrMmuCfg.upbm_tileMode = 1;

	if (psIspmtr->m_mtrR_mode)
		kickOffEn = 1;
	else
		kickOffEn = 0;

	ISPSS_BE_MTR_GetStride(psIspmtr->m_srcfmt, &stride_64B,
				psIspmtr->m_content_width, psIspmtr->m_bits_per_pixel);

	sIspmtrMtrProg.ispmtrMtrrCfg.ucfg_enable		= 0;
	sIspmtrMtrProg.ispmtrMmuCfg.upbm_stride_64B		=  stride_64B;
	sIspmtrMtrProg.ispmtrMmuCfg.upbm_shy_bw		=
				psIspmtr->m_mtr_buf_desc_Y.m_MmuCfgPbm_shy_bw;
	sIspmtrMtrProg.ispmtrMmuCfg.upbm_shy_pos		=
				psIspmtr->m_mtr_buf_desc_Y.m_MmuCfgPbm_shy_pos;
	sIspmtrMtrProg.ispmtrMmuCfg.upbm_pm_enable		=
				psIspmtr->m_mtr_buf_desc_Y.m_MmuCfgPbm_pm_enable;
	sIspmtrMtrProg.ispmtrMmuCfg.upbm_shuffle_en		=
				psIspmtr->m_mtr_buf_desc_Y.m_MmuCfgPbm_shuffle_en;
	sIspmtrMtrProg.ispmtrMmuCfg.upbm_bm_enable		=
				psIspmtr->m_mtr_buf_desc_Y.m_MmuCfgPbm_bm_enable;
	sIspmtrMtrProg.ispmtrMmuCfg.uvm_enable		=
				psIspmtr->m_mtr_buf_desc_Y.m_MmuCfgVm_enable;

	if (path == ISPSS_BE_DEWARP_RD)
		sIspmtrMtrProg.ispmtrMmuCfg.uvm_base		=
						(UINT64)psIspmtr->m_pbuf_start;
	else
		sIspmtrMtrProg.ispmtrMmuCfg.uvm_base = (UINT64)(psIspmtr->m_pbuf_start) >>
						(UINT64)ISPSS_BE_MTR_BASE_ADDR_ALIGN;

	ISPSS_BE_MTR_MTRR_Program(pbcmbuf, psIspmtr, threadID_Y, kickOffEn, &sIspmtrMtrProg);

	if (psIspmtr->m_mtrR_mode & 1) {
		sIspmtrMtrProg.ispmtrMtrrCfg.ucfg_stride = stride_64B;
		ISPSS_BE_MTR_GetMtrFmtandMode(psIspmtr->m_srcfmt, psIspmtr->m_bits_per_pixel,
				(UINT32 *)&sIspmtrMtrProg.ispmtrMtrrCfg.ucfg_format,
				(UINT32 *)&sIspmtrMtrProg.ispmtrMtrrCfg.ucfg_mode, true);
		sIspmtrMtrProg.ispmtrMtrrCfg.ucfg_enable  = psIspmtr->m_mtrR_mode & 0x1;
		sIspmtrMtrProg.ispmtrMtrrCfg.ucfg_hflip  = 0;
		sIspmtrMtrProg.ispmtrMtrrCfg.ucfg_vflip  = 0;
		sIspmtrMtrProg.ispmtrMtrrCfg.ubase_addr  =
				(psIspmtr->m_mtr_buf_desc_UV.m_MtrrCfgBase_addr);
	}
	sIspmtrMtrProg.ispmtrMmuCfg.upbm_shy_bw		=
				psIspmtr->m_mtr_buf_desc_UV.m_MmuCfgPbm_shy_bw;
	sIspmtrMtrProg.ispmtrMmuCfg.upbm_shy_pos		=
				psIspmtr->m_mtr_buf_desc_UV.m_MmuCfgPbm_shy_pos;
	/*sIspmtrMtrProg.ispmtrMmuCfg.upbm_stride_64B     =
	 * (psIspmtr->m_buf_stride_UV >> (ISPSS_BE_MTR_PBM_STRIDE_ALIGN+
	 * (ISPSS_BE_MTR_TILEHEIGHT_ALIGN)));
	 */

	if (path == ISPSS_BE_DNSCL_RD)
		sIspmtrMtrProg.ispmtrMmuCfg.upbm_stride_64B =  stride_64B;
	else
		sIspmtrMtrProg.ispmtrMmuCfg.upbm_stride_64B =  stride_64B;
	sIspmtrMtrProg.ispmtrMmuCfg.upbm_pm_enable		=
				psIspmtr->m_mtr_buf_desc_UV.m_MmuCfgPbm_pm_enable;
	sIspmtrMtrProg.ispmtrMmuCfg.upbm_shuffle_en		=
				psIspmtr->m_mtr_buf_desc_UV.m_MmuCfgPbm_shuffle_en;
	sIspmtrMtrProg.ispmtrMmuCfg.upbm_bm_enable		=
				psIspmtr->m_mtr_buf_desc_UV.m_MmuCfgPbm_bm_enable;
	sIspmtrMtrProg.ispmtrMmuCfg.uvm_enable		=
				psIspmtr->m_mtr_buf_desc_UV.m_MmuCfgVm_enable;
	if (path == ISPSS_BE_DEWARP_RD)
		sIspmtrMtrProg.ispmtrMmuCfg.uvm_base	= (UINT32)psIspmtr->m_buf_pbuf_start_UV;
	else
		sIspmtrMtrProg.ispmtrMmuCfg.uvm_base = ((UINT32)psIspmtr->m_buf_pbuf_start_UV >>
				ISPSS_BE_MTR_BASE_ADDR_ALIGN);
	ISPSS_BE_MTR_MTRR_Program(pbcmbuf, psIspmtr, threadID_UV, kickOffEn, &sIspmtrMtrProg);

	return uiRetVal;
}

/***************************************************************************************
 * FUNCTION: Update frame descriptor to ISP MTRW registers
 * PARAMS:   Bcm buffer pointer
 * RETURN:   ISPSS_BE_MTR_SUCCESS - succeed.
 *           ISPSS_BE_MTR_ERROR - bad input parameters.
 ***************************************************************************************/
static UINT32 ISPSS_BE_MTR_UpdateWriteDesc(struct BCMBUF *pbcmbuf,
		struct ISPSS_BE_MTR *psIspmtr, int path)
{
	UINT32 uiRetVal = ISPSS_BE_MTR_SUCCESS;
	struct ISPSS_BE_MTR_MTRR_Prog sIspmtrMtrProg;
	UINT8 threadID_Y = 0xFF, threadID_UV = 0xFF;
	BOOL kickOffEn = 0;
	UINT32 stride_64B = 0;

	ISPSS_BE_MTR_GetThreadIDs(&threadID_Y, &threadID_UV, path);
	psIspmtr->m_mtrR_chkSemId_Y  = threadID_Y;
	psIspmtr->m_mtrR_chkSemId_UV = threadID_UV;
	memset(&sIspmtrMtrProg.ispmtrMtrwCfg, 0, sizeof(sIspmtrMtrProg.ispmtrMtrwCfg));
	if (psIspmtr->m_srcfmt <= ISPSS_SRCFMT_YUV420SP_DWA)
		sIspmtrMtrProg.ispmtrMmuCfg.upbm_tileMode = 0;
	else
		sIspmtrMtrProg.ispmtrMmuCfg.upbm_tileMode = 1;

	if (psIspmtr->m_mtrR_mode)
		kickOffEn = 1;
	else
		kickOffEn = 0;

	ISPSS_BE_MTR_GetStride(psIspmtr->m_srcfmt, &stride_64B,
					psIspmtr->m_buf_stride, psIspmtr->m_bits_per_pixel);

	sIspmtrMtrProg.ispmtrMtrwCfg.ucfg_enable   = 0;
	if (psIspmtr->m_mtrR_mode & 1) {
		sIspmtrMtrProg.ispmtrMtrwCfg.ucfg_stride = stride_64B;
		sIspmtrMtrProg.ispmtrMtrwCfg.ucfg_enable = psIspmtr->m_mtrR_mode & 0x1;
		ISPSS_BE_MTR_GetMtrFmtandMode(psIspmtr->m_srcfmt, psIspmtr->m_bits_per_pixel,
				(UINT32 *)&sIspmtrMtrProg.ispmtrMtrwCfg.ucfg_format,
				(UINT32 *)&sIspmtrMtrProg.ispmtrMtrwCfg.ucfg_mode, true);
		sIspmtrMtrProg.ispmtrMtrwCfg.ucfg_hflip  = 0;
		sIspmtrMtrProg.ispmtrMtrwCfg.ucfg_vflip  = 0;
		sIspmtrMtrProg.ispmtrMtrwCfg.ubase_addr  =
					(psIspmtr->m_mtr_buf_desc_Y.m_MtrrCfgBase_addr);
	}

	sIspmtrMtrProg.ispmtrMmuCfg.upbm_shy_bw		=
				psIspmtr->m_mtr_buf_desc_Y.m_MmuCfgPbm_shy_bw;
	sIspmtrMtrProg.ispmtrMmuCfg.upbm_shy_pos		=
				psIspmtr->m_mtr_buf_desc_Y.m_MmuCfgPbm_shy_pos;
	sIspmtrMtrProg.ispmtrMmuCfg.upbm_stride_64B		=	stride_64B;
	sIspmtrMtrProg.ispmtrMmuCfg.upbm_pm_enable		=
				psIspmtr->m_mtr_buf_desc_Y.m_MmuCfgPbm_pm_enable;
	sIspmtrMtrProg.ispmtrMmuCfg.upbm_shuffle_en		=
				psIspmtr->m_mtr_buf_desc_Y.m_MmuCfgPbm_shuffle_en;
	sIspmtrMtrProg.ispmtrMmuCfg.upbm_bm_enable		=
				psIspmtr->m_mtr_buf_desc_Y.m_MmuCfgPbm_bm_enable;
	sIspmtrMtrProg.ispmtrMmuCfg.uvm_enable		=
				psIspmtr->m_mtr_buf_desc_Y.m_MmuCfgVm_enable;
	sIspmtrMtrProg.ispmtrMmuCfg.uvm_base		=
				(UINT64)psIspmtr->m_pbuf_start >> ISPSS_BE_MTR_BASE_ADDR_ALIGN;
	ISPSS_BE_MTR_MTRW_Program(pbcmbuf, psIspmtr,
					threadID_Y, kickOffEn, &sIspmtrMtrProg);

	sIspmtrMtrProg.ispmtrMtrwCfg.ucfg_enable   = 0;
	if (psIspmtr->m_mtrR_mode & 1) {
		sIspmtrMtrProg.ispmtrMtrwCfg.ucfg_stride = stride_64B;
		sIspmtrMtrProg.ispmtrMtrwCfg.ucfg_enable = psIspmtr->m_mtrR_mode & 0x1;
		ISPSS_BE_MTR_GetMtrFmtandMode(psIspmtr->m_srcfmt, psIspmtr->m_bits_per_pixel,
				(UINT32 *)&sIspmtrMtrProg.ispmtrMtrwCfg.ucfg_format,
				(UINT32 *)&sIspmtrMtrProg.ispmtrMtrwCfg.ucfg_mode, false);
		sIspmtrMtrProg.ispmtrMtrwCfg.ucfg_hflip  = 0;
		sIspmtrMtrProg.ispmtrMtrwCfg.ucfg_vflip  = 0;
		sIspmtrMtrProg.ispmtrMtrwCfg.ubase_addr		=
				(psIspmtr->m_mtr_buf_desc_UV.m_MtrrCfgBase_addr);
	}

	sIspmtrMtrProg.ispmtrMmuCfg.upbm_shy_bw		=
				psIspmtr->m_mtr_buf_desc_UV.m_MmuCfgPbm_shy_bw;
	sIspmtrMtrProg.ispmtrMmuCfg.upbm_shy_pos		=
				psIspmtr->m_mtr_buf_desc_UV.m_MmuCfgPbm_shy_pos;
	sIspmtrMtrProg.ispmtrMmuCfg.upbm_stride_64B		=  stride_64B;
	sIspmtrMtrProg.ispmtrMmuCfg.upbm_pm_enable		=
				psIspmtr->m_mtr_buf_desc_UV.m_MmuCfgPbm_pm_enable;
	sIspmtrMtrProg.ispmtrMmuCfg.upbm_shuffle_en		=
				psIspmtr->m_mtr_buf_desc_UV.m_MmuCfgPbm_shuffle_en;
	sIspmtrMtrProg.ispmtrMmuCfg.upbm_bm_enable		=
				psIspmtr->m_mtr_buf_desc_UV.m_MmuCfgPbm_bm_enable;
	sIspmtrMtrProg.ispmtrMmuCfg.uvm_enable		=
				psIspmtr->m_mtr_buf_desc_UV.m_MmuCfgVm_enable;
	sIspmtrMtrProg.ispmtrMmuCfg.uvm_base		=
				((UINT32)psIspmtr->m_buf_pbuf_start_UV >>
				 ISPSS_BE_MTR_BASE_ADDR_ALIGN);

	ISPSS_BE_MTR_MTRW_Program(pbcmbuf, psIspmtr, threadID_UV, kickOffEn, &sIspmtrMtrProg);

	return uiRetVal;
}

/***************************************************************************************
 * FUNCTION: Update frame descriptor to ISP MTR registers
 * PARAMS:   pbcmbuf - BCM buffer pointer
 ***************************************************************************************/
void ISPSS_BE_MTR_UpdateDesc(struct BCMBUF *pbcmbuf, struct ISPSS_BE_MTR *psIspmtr, int path)
{
	if (path <= ISPSS_BE_DEWARP_RD)
		ISPSS_BE_MTR_UpdateReadDesc(pbcmbuf, psIspmtr, path);
	else
		ISPSS_BE_MTR_UpdateWriteDesc(pbcmbuf, psIspmtr, path);
}
EXPORT_SYMBOL(ISPSS_BE_MTR_UpdateDesc);

/***************************************************************************************
 * FUNCTION: Set global registers for MTR for Scaler
 * PARAMS:   Bcm buffer pointer, in_mtrMode and out_mtrMode
 * RETURN:   output of the register write
 ***************************************************************************************/
UINT32 ISPSS_BE_MTR_Set_Dhub_Channel_Scaling(struct BCMBUF *pbcmbuf,
		UINT8 in_mtrMode, UINT8 out_mtrMode)
{
	UINT32 uiRetVal = ISPSS_BE_MTR_SUCCESS;
	UINT32 ispTsbFlagReg = ISP_BE_GLB_REMAP_ADDR + RA_Remap_TsbMtrFlag;

	if (in_mtrMode)
		uiRetVal |= 0x30;
	if (out_mtrMode)
		uiRetVal |= 0xc;

	ISPSS_BCMBUF_Write(pbcmbuf, ispTsbFlagReg, uiRetVal);
	uiRetVal = 0;
	ISPSS_REG_READ32(ispTsbFlagReg, &uiRetVal);
	return uiRetVal;
}
EXPORT_SYMBOL(ISPSS_BE_MTR_Set_Dhub_Channel_Scaling);
