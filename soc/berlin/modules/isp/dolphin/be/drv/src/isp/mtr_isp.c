// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 Synaptics Incorporated
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/math.h>
#include "ispSS_memmap.h"
#include "com_type.h"
#include "ispMISC.h"
#include "mtr.h"
#include "mtr_isp.h"
#include "ispSS_reg.h"
#define MTR_WRITE   0
#define MTR_READ    1
#define CONFIG_DOLPHIN_ISPSS_REGAREA_BASE          0xf9100000
#define ISP_MTR_SENSOR1_OFFSET  0x80

INT32 ISPSS_MTR_Set_Global_Registers(struct ISP_MTR_OBJ *mtrObj,
		enum ISPSS_MTR_PATH_ID path, bool enable)
{
	int res = 0;
	int mtr_flag;
	UINT32 ispRemapReg = CONFIG_DOLPHIN_ISPSS_REGAREA_BASE +
		ISPSS_MEMMAP_GLB_REG_BASE + RA_IspMISC_remap;

	if (mtrObj == NULL)
		return ISPSS_MTR_PARAM_ERROR;

	switch (path) {
	case ISPSS_MTR_PATH_MCM0_RD:
		ISPSS_REG_READ32(ispRemapReg + RA_Remap_Isp1RMtrFlag, &mtr_flag);
		SET_BIT(mtr_flag, enable, ISP_RD2_MCM_RAW_R_BIT, 1);
		ISPSS_REG_WRITE32(ispRemapReg + RA_Remap_Isp1RMtrFlag, mtr_flag);
		ISPSS_REG_WRITE32(ispRemapReg + RA_Remap_MtrTid + 0x4, 0x0);
		break;
	case ISPSS_MTR_PATH_MCM0_WR:
		ISPSS_REG_READ32(ispRemapReg + RA_Remap_Isp1WMtrFlag, &mtr_flag);
		SET_BIT(mtr_flag, enable, ISP_WR4_MCM_S0_W_BIT, 1);
		ISPSS_REG_WRITE32(ispRemapReg + RA_Remap_Isp1WMtrFlag, mtr_flag);
		break;
	case ISPSS_MTR_PATH_MCM1_RD:
		// adding 4 for sensor 1
		ISPSS_REG_READ32(ispRemapReg + RA_Remap_Isp1RMtrFlag + 4, &mtr_flag);
		SET_BIT(mtr_flag, enable, ISP_RD2_MCM_RAW_R_BIT, 1);
		ISPSS_REG_WRITE32(ispRemapReg + RA_Remap_Isp1RMtrFlag + 4, mtr_flag);
		ISPSS_REG_WRITE32(ispRemapReg + RA_Remap_MtrTid + ISP_MTR_SENSOR1_OFFSET + 4, 0x0);
		break;
	case ISPSS_MTR_PATH_MCM1_WR:
		ISPSS_REG_READ32(ispRemapReg + RA_Remap_Isp1WMtrFlag + 4, &mtr_flag);
		SET_BIT(mtr_flag, enable, ISP_WR4_MCM_S1_W_BIT, 1);
		ISPSS_REG_WRITE32(ispRemapReg + RA_Remap_Isp1WMtrFlag + 4, mtr_flag);
		ISPSS_REG_WRITE32(ispRemapReg + RA_Remap_MtrTid + ISP_MTR_SENSOR1_OFFSET, 1);
		break;
	case ISPSS_MTR_PATH_TDNR0_RD:
		ISPSS_REG_READ32(ispRemapReg + RA_Remap_Isp0RMtrFlag, &mtr_flag);
		SET_BIT(mtr_flag, enable, ISP_RD1_TDNR_RAW_R_BIT, 1);
		ISPSS_REG_WRITE32(ispRemapReg + RA_Remap_Isp0RMtrFlag, mtr_flag);
		ISPSS_REG_WRITE32(ispRemapReg + RA_Remap_Isp0RMtrFlag + 4, mtr_flag);
		// Add 0xc for TDNR0 threadId
		ISPSS_REG_WRITE32(ispRemapReg + RA_Remap_MtrTid + 0xc, 0x1);
		break;
	case ISPSS_MTR_PATH_TDNR0_WR:
		ISPSS_REG_READ32(ispRemapReg + RA_Remap_Isp0WMtrFlag, &mtr_flag);
		SET_BIT(mtr_flag, enable, ISP_WR3_TDNR_RAW_W_BIT, 1);
		ISPSS_REG_WRITE32(ispRemapReg + RA_Remap_Isp0WMtrFlag, mtr_flag);
		ISPSS_REG_WRITE32(ispRemapReg + RA_Remap_Isp0WMtrFlag + 4, mtr_flag);
		// Add 0x8 for TDNRW threadId
		ISPSS_REG_WRITE32(ispRemapReg + RA_Remap_MtrTid + 0x8, 0x2);
		break;
	case ISPSS_MTR_PATH_TDNR1_RD:
		ISPSS_REG_READ32(ispRemapReg + RA_Remap_Isp0RMtrFlag + 4, &mtr_flag);
		SET_BIT(mtr_flag, enable, ISP_RD1_TDNR_RAW_R_BIT, 1);
		ISPSS_REG_WRITE32(ispRemapReg + RA_Remap_Isp0RMtrFlag + 4, mtr_flag);
		ISPSS_REG_WRITE32(ispRemapReg + RA_Remap_Isp0RMtrFlag, mtr_flag);
		ISPSS_REG_WRITE32(ispRemapReg + RA_Remap_MtrTid +
				ISP_MTR_SENSOR1_OFFSET + 0xc, 0x1);
		break;
	case ISPSS_MTR_PATH_TDNR1_WR:
		ISPSS_REG_READ32(ispRemapReg + RA_Remap_Isp0WMtrFlag + 4, &mtr_flag);
		SET_BIT(mtr_flag, enable, ISP_WR3_TDNR_RAW_W_BIT, 1);
		ISPSS_REG_WRITE32(ispRemapReg + RA_Remap_Isp0WMtrFlag + 4, mtr_flag);
		ISPSS_REG_WRITE32(ispRemapReg + RA_Remap_Isp0WMtrFlag, mtr_flag);
		ISPSS_REG_WRITE32(ispRemapReg + RA_Remap_MtrTid +
			ISP_MTR_SENSOR1_OFFSET + 0x8, 0x1);
		break;
	case ISPSS_MTR_PATH_MP0_WR:
		ISPSS_REG_READ32(ispRemapReg + RA_Remap_Isp0WMtrFlag, &mtr_flag);
		SET_BIT(mtr_flag, enable, ISP_WR1_MP_Y_W_BIT, 1);
		SET_BIT(mtr_flag, enable, ISP_WR1_MP_CB_W_BIT, 1);
		//ISPSS_REG_WRITE32(ispRemapReg + RA_Remap_Isp0WMtrFlag + 4, mtr_flag);
		ISPSS_REG_WRITE32(ispRemapReg + RA_Remap_Isp0WMtrFlag, mtr_flag);
		ISPSS_REG_WRITE32(ispRemapReg + RA_Remap_MtrTid + 0x10, 0x3); // Y thread Id
		ISPSS_REG_WRITE32(ispRemapReg + RA_Remap_MtrTid + 0x14, 0x4); // CbCr thread Id
		break;
	case ISPSS_MTR_PATH_MP1_WR:
		ISPSS_REG_READ32(ispRemapReg + RA_Remap_Isp0WMtrFlag + 4, &mtr_flag);
		SET_BIT(mtr_flag, enable, ISP_WR1_MP_Y_W_BIT, 1);
		SET_BIT(mtr_flag, enable, ISP_WR1_MP_CB_W_BIT, 1);
		ISPSS_REG_WRITE32(ispRemapReg + RA_Remap_Isp0WMtrFlag + 4, mtr_flag);
		//ISPSS_REG_WRITE32(ispRemapReg + RA_Remap_Isp0WMtrFlag, mtr_flag);
		ISPSS_REG_WRITE32(ispRemapReg + RA_Remap_MtrTid +
			ISP_MTR_SENSOR1_OFFSET + 0x10, 0x3); // Add 0x10 for Y thread Id
		ISPSS_REG_WRITE32(ispRemapReg + RA_Remap_MtrTid +
			ISP_MTR_SENSOR1_OFFSET + 0x14, 0x4); // Add 0x14 for CbCr thread Id
		break;
	case ISPSS_MTR_PATH_SP2_WR0:
		ISPSS_REG_READ32(ispRemapReg + RA_Remap_Isp0WMtrFlag, &mtr_flag);
		SET_BIT(mtr_flag, enable, ISP_WR3_SP2_Y_W_BIT, 1);
		SET_BIT(mtr_flag, enable, ISP_WR3_SP2_CB_W_BIT, 1);
		ISPSS_REG_WRITE32(ispRemapReg + RA_Remap_Isp0WMtrFlag, mtr_flag);
		ISPSS_REG_WRITE32(ispRemapReg + RA_Remap_MtrTid + 0x18, 0x9); // Y thread Id
		ISPSS_REG_WRITE32(ispRemapReg + RA_Remap_MtrTid + 0x1C, 0xa); // CbCr thread Id
		break;
	case ISPSS_MTR_PATH_SP2_WR1:
		ISPSS_REG_READ32(ispRemapReg + RA_Remap_Isp0WMtrFlag + 4, &mtr_flag);
		SET_BIT(mtr_flag, enable, ISP_WR3_SP2_Y_W_BIT, 1);
		SET_BIT(mtr_flag, enable, ISP_WR3_SP2_CB_W_BIT, 1);
		ISPSS_REG_WRITE32(ispRemapReg + RA_Remap_Isp0WMtrFlag + 4, mtr_flag);
		ISPSS_REG_WRITE32(ispRemapReg + RA_Remap_MtrTid + 0x18 +
			ISP_MTR_SENSOR1_OFFSET, 0x9); // Add 0x10 for Y thread Id
		ISPSS_REG_WRITE32(ispRemapReg + RA_Remap_MtrTid + 0x1C +
			ISP_MTR_SENSOR1_OFFSET, 0xa); // Add 0x14 for CbCr thread Id
		break;
	default:
		res = -1;
		break;
	}

	return res;
}

/***************************************************************************************
 * FUNCTION: Clear cache tag information of ISP MTR
 * PARAMS:   mtrObj - Scl object
 * RETURN:   ISPSS_MTR_SUCCESS - succeed.
 *           SYNA_ISPSS_DNSCL_EBADPARAM - bad input parameters.
 ***************************************************************************************/
static UINT32 ISPSS_MTR_ClearCacheTagInfo(struct ISP_MTR_OBJ *mtrObj, struct BCMBUF *pbcmbuf)
{

	UINT32 uiRetVal = ISPSS_MTR_SUCCESS;
	UINT32 uiRegAddr = 0;
	UINT32 uiAddrOffset = 0;

	if (mtrObj == NULL)
		return ISPSS_MTR_PARAM_ERROR;

	uiRegAddr = mtrObj->base_addr_ispMtr;

	for (uiAddrOffset = RA_ISPMTR_MMU_TAG; uiAddrOffset <=  RA_ISPMTR_MMU_TAG + 0xBC;
			uiAddrOffset += 4) {
		if (pbcmbuf != NULL)
			ISPSS_MTR_CHECK_RETVAL(ISPSS_BCMBUF_Write(pbcmbuf,
				uiRegAddr + uiAddrOffset, 0));
		else
			ISPSS_REG_WRITE32(uiRegAddr + uiAddrOffset, 0);
	}

	for (uiAddrOffset = RA_ISPMTR_META_TAG; uiAddrOffset <= RA_ISPMTR_META_TAG + 0x3C;
			uiAddrOffset += 4) {
		if (pbcmbuf != NULL)
			ISPSS_MTR_CHECK_RETVAL(ISPSS_BCMBUF_Write(pbcmbuf,
				uiRegAddr + uiAddrOffset, 0));
		else
			ISPSS_REG_WRITE32(uiRegAddr + uiAddrOffset, 0);
	}

	return uiRetVal;
}

/***************************************************************************************
 * FUNCTION: Reset ISPSS_BE_SCL3 MTR block and load default values
 * PARAMS:   mtrObj - scl object
 * RETURN:   ISPSS_MTR_SUCCESS - succeed.
 *           SYNA_ISPSS_DNSCL_EBADPARAM - bad input parameters.
 ***************************************************************************************/
UINT32 ISPSS_MTR_Reset(struct ISP_MTR_OBJ *mtrObj, struct BCMBUF *pbcmbuf)
{
	UINT32 uiRetVal = ISPSS_MTR_SUCCESS;

	if (mtrObj == NULL)
		return ISPSS_MTR_PARAM_ERROR;

	ISPSS_MTR_Config(mtrObj, 1, pbcmbuf);
	ISPSS_MTR_ClearCacheTagInfo(mtrObj, pbcmbuf);

	return uiRetVal;
}

/***************************************************************************************
 * FUNCTION: Return MMU cache base and ways details
 * PARAMS:   mtrObj - scl object
 * RETURN:   ISPSS_MTR_SUCCESS - succeed.
 *           SYNA_ISPSS_DNSCL_EBADPARAM - bad input parameters.
 ***************************************************************************************/
static UINT32 ISPSS_MTR_MTRR_GetCacheLines(enum ISPSS_MTR_THREAD_ID eThreadId,
		UINT8 *base, UINT8 *ways)
{
	UINT32 uiRetVal = ISPSS_MTR_SUCCESS;

	/*Tune it based on the system requirements for a product */
	switch (eThreadId) {
	case ISPSS_MTR_THREAD_0:
		*base = 0;
		*ways = 1;
		break;
	case ISPSS_MTR_THREAD_1:
		*base = 2;
		*ways = 1;
		break;
	case ISPSS_MTR_THREAD_2:
		*base = 4;
		*ways = 1;
		break;
	case ISPSS_MTR_THREAD_3:
		*base = 6;
		*ways = 1;
		break;
	case ISPSS_MTR_THREAD_4:
		*base = 8;
		*ways = 1;
		break;
	case ISPSS_MTR_THREAD_5:
		*base = 10;
		*ways = 1;
		break;
	case ISPSS_MTR_THREAD_6:
		*base = 12;
		*ways = 1;
		break;
	case ISPSS_MTR_THREAD_7:
	case ISPSS_MTR_THREAD_8:
	case ISPSS_MTR_THREAD_9:
	case ISPSS_MTR_THREAD_10:
	case ISPSS_MTR_THREAD_11:
	case ISPSS_MTR_THREAD_12:
	case ISPSS_MTR_THREAD_13:
	case ISPSS_MTR_THREAD_14:
	case ISPSS_MTR_THREAD_15:
	case ISPSS_MTR_THREAD_MAX:
	case ISPSS_MTR_THREAD_INVALID:
		*base = 14;
		*ways = 1;
		break;
	}
	return uiRetVal;
}

/***************************************************************************************
 * FUNCTION: Return MMU cache base and ways details
 * PARAMS:   mtrObj - scl object
 * RETURN:   ISPSS_MTR_SUCCESS - succeed.
 *           SYNA_ISPSS_DNSCL_EBADPARAM - bad input parameters.
 ***************************************************************************************/
static UINT32 ISPSS_MTR_MTRW_GetCacheLines(enum ISPSS_MTR_THREAD_ID eThreadId,
		UINT8 *base, UINT8 *ways)
{
	UINT32 uiRetVal = ISPSS_MTR_SUCCESS;
	UINT32 base_val = 14;

	switch (eThreadId) {
	case ISPSS_MTR_THREAD_0:
		*base = base_val;
		*ways = 1;
		break;
	case ISPSS_MTR_THREAD_1:
		*base = base_val + 2;
		*ways = 1;
		break;
	case ISPSS_MTR_THREAD_2:
		*base =  base_val + 4;
		*ways = 1;
		break;
	case ISPSS_MTR_THREAD_3:
		*base =  base_val + 6;
		*ways = 1;
		break;
	case ISPSS_MTR_THREAD_4:
		*base = base_val + 8;
		*ways = 1;
		break;
	case ISPSS_MTR_THREAD_5:
		*base = base_val + 10;
		*ways = 1;
		break;
	case ISPSS_MTR_THREAD_6:
		*base = base_val + 12;
		*ways = 1;
		break;
	case ISPSS_MTR_THREAD_7:
		*base = base_val + 14;
		*ways = 1;
		break;
	case ISPSS_MTR_THREAD_8:
		*base = base_val + 16;
		*ways = 1;
		break;
	case ISPSS_MTR_THREAD_9:
		*base = base_val + 18;
		*ways = 1;
		break;
	case ISPSS_MTR_THREAD_10:
		*base = base_val + 20;
		*ways = 1;
		break;
	default:
		uiRetVal = ISPSS_MTR_ERROR;
		break;
	}

	return uiRetVal;
}

/***************************************************************************************
 * FUNCTION: Configure MTR_R block of ISPMTR
 * PARAMS: mtrObj - scl object
 * RETURN:  ISPSS_MTR_SUCCESS - succeed.
 *          SYNA_ISPSS_DNSCL_EBADPARAM - bad input parameters.
 ***************************************************************************************/
static UINT32 ISPSS_MTR_MTRR_Config(struct ISP_MTR_OBJ *mtrObj,
		struct ISP_MTR_BUF_INFO *pIspBufInfo,
		enum ISPSS_MTR_THREAD_ID eThreadId,
		struct ISPSS_MTR_MTRR_CFG *psIspSSMtrrCfg)
{
	UINT32 uiRetVal = ISPSS_MTR_SUCCESS;
	UINT32 uiRegAddr = 0;
	T32MTRR_CFG_cfg  sMtrrCfgCfg;
	T32MTRR_CFG_base sMtrrCfgBase;
	UINT8 base = 0xFF, ways = 0xFF;

	if (mtrObj == NULL)
		return ISPSS_MTR_PARAM_ERROR;

	ISPSS_MTR_MTRR_GetCacheLines(eThreadId, &base, &ways);

	uiRegAddr = mtrObj->base_addr_ispMtr + RA_ISPMTR_mtrr +
		(eThreadId * sizeof(SIE_MTRR)) + RA_MTRR_mtr;

	sMtrrCfgCfg.u32                     = 0;
	sMtrrCfgCfg.ucfg_stride             = psIspSSMtrrCfg->ucfg_stride;
	sMtrrCfgCfg.ucfg_format             = psIspSSMtrrCfg->ucfg_format;
	sMtrrCfgCfg.ucfg_enable             = psIspSSMtrrCfg->ucfg_enable;
	sMtrrCfgCfg.ucfg_mode               = psIspSSMtrrCfg->ucfg_mode;
	sMtrrCfgCfg.ucfg_prefetch_disable   = 0;
	sMtrrCfgCfg.ucfg_cbase              = base;
	sMtrrCfgCfg.ucfg_cways              = ways;
	sMtrrCfgCfg.ucfg_auto_tag_clr       = 0;
	sMtrrCfgCfg.ucfg_mosaic_xnum        = 0;
	sMtrrCfgCfg.ucfg_mosaic_ynum        = 0;
	sMtrrCfgCfg.ucfg_weave              = 0;
	sMtrrCfgCfg.ucfg_ctype               = 0;
	sMtrrCfgCfg.ucfg_hflip              = psIspSSMtrrCfg->ucfg_hflip;
	sMtrrCfgCfg.ucfg_vflip              = psIspSSMtrrCfg->ucfg_vflip;

	sMtrrCfgBase.u32         = 0;
	sMtrrCfgBase.ubase_addr  = psIspSSMtrrCfg->ubase_addr;

	if (pIspBufInfo->pBcmBuf != NULL) {
		ISPSS_MTR_CHECK_RETVAL(ISPSS_BCMBUF_Write(pIspBufInfo->pBcmBuf,
					uiRegAddr + RA_MTRR_CFG_cfg, sMtrrCfgCfg.u32));
		ISPSS_MTR_CHECK_RETVAL(ISPSS_BCMBUF_Write(pIspBufInfo->pBcmBuf,
					uiRegAddr + RA_MTRR_CFG_base, sMtrrCfgBase.u32));
	} else {
		ISPSS_REG_WRITE32(uiRegAddr + RA_MTRR_CFG_cfg, sMtrrCfgCfg.u32);
		ISPSS_REG_WRITE32(uiRegAddr + RA_MTRR_CFG_base, sMtrrCfgBase.u32);
	}

	pr_debug("stride=0x%x, format=0x%x, enable = 0x%x mode=0x%x,",
			psIspSSMtrrCfg->ucfg_stride, psIspSSMtrrCfg->ucfg_format,
			psIspSSMtrrCfg->ucfg_enable, psIspSSMtrrCfg->ucfg_mode);
	pr_debug(" hflip=0x%x, vflip=0x%x, base_addr=0x%x\n",
			psIspSSMtrrCfg->ucfg_hflip, psIspSSMtrrCfg->ucfg_vflip,
			psIspSSMtrrCfg->ubase_addr);
	pr_debug("%d %s >> uiRegAddr = %x sMtrrCfgCfg.u32 = %x, sMtrrCfgBase.u32 = %x\n",
			__LINE__, __func__, uiRegAddr, sMtrrCfgCfg.u32, sMtrrCfgBase.u32);

	return uiRetVal;
}

/***************************************************************************************
 * FUNCTION: Configure MTR_R block of ISPMTR
 * PARAMS: mtrObj - scl object
 * RETURN:  ISPSS_MTR_SUCCESS - succeed.
 *          SYNA_ISPSS_DNSCL_EBADPARAM - bad input parameters.
 ***************************************************************************************/
static UINT32 ISPSS_MTR_MTRW_Config(struct ISP_MTR_OBJ *mtrObj,
		struct ISP_MTR_BUF_INFO *pIspBufInfo,
		enum ISPSS_MTR_THREAD_ID eThreadId,
		struct ISPSS_MTR_MTRR_CFG *psIspSclmtrMtrwCfg)
{
	UINT32 uiRetVal = ISPSS_MTR_SUCCESS;
	UINT32 uiRegAddr = 0;
	T32MTRW_CFG_cfg  sMtrwCfgCfg;
	T32MTRW_CFG_base sMtrwCfgBase;
	UINT8 base = 0xFF, ways = 0xFF;

	if (mtrObj == NULL)
		return ISPSS_MTR_PARAM_ERROR;

	ISPSS_MTR_MTRW_GetCacheLines(eThreadId, &base, &ways);

	uiRegAddr = mtrObj->base_addr_ispMtr + RA_ISPMTR_mtrw +
		(eThreadId * sizeof(SIE_MTRR)) + RA_MTRW_mtr;

	sMtrwCfgCfg.u32                     = 0;
	sMtrwCfgCfg.ucfg_stride             = psIspSclmtrMtrwCfg->ucfg_stride;
	sMtrwCfgCfg.ucfg_format             = psIspSclmtrMtrwCfg->ucfg_format;
	sMtrwCfgCfg.ucfg_enable             = psIspSclmtrMtrwCfg->ucfg_enable;
	sMtrwCfgCfg.ucfg_mode               = psIspSclmtrMtrwCfg->ucfg_mode;
	sMtrwCfgCfg.ucfg_weave              = 0;
	//sMtrwCfgCfg.ucfg_ctype               = 0;
	sMtrwCfgCfg.ucfg_hflip              = psIspSclmtrMtrwCfg->ucfg_hflip;
	sMtrwCfgCfg.ucfg_vflip              = psIspSclmtrMtrwCfg->ucfg_vflip;

	sMtrwCfgBase.u32         = 0;
	sMtrwCfgBase.ubase_addr  = psIspSclmtrMtrwCfg->ubase_addr;

	if (pIspBufInfo->pBcmBuf != NULL) {
		ISPSS_MTR_CHECK_RETVAL(ISPSS_BCMBUF_Write(pIspBufInfo->pBcmBuf,
					uiRegAddr + RA_MTRW_CFG_cfg, sMtrwCfgCfg.u32));
		ISPSS_MTR_CHECK_RETVAL(ISPSS_BCMBUF_Write(pIspBufInfo->pBcmBuf,
					uiRegAddr + RA_MTRW_CFG_base, sMtrwCfgBase.u32));
	} else {
		ISPSS_REG_WRITE32(uiRegAddr + RA_MTRW_CFG_cfg, sMtrwCfgCfg.u32);
		ISPSS_REG_WRITE32(uiRegAddr + RA_MTRW_CFG_base, sMtrwCfgBase.u32);
	}

	pr_debug("stride=0x%x, format=0x%x, enable = 0x%x mode=0x%x,",
			psIspSclmtrMtrwCfg->ucfg_stride, psIspSclmtrMtrwCfg->ucfg_format,
			psIspSclmtrMtrwCfg->ucfg_enable, psIspSclmtrMtrwCfg->ucfg_mode);
	pr_debug(" hflip=0x%x, vflip=0x%x, base_addr=0x%x\n",
			psIspSclmtrMtrwCfg->ucfg_hflip, psIspSclmtrMtrwCfg->ucfg_vflip,
			psIspSclmtrMtrwCfg->ubase_addr);
	pr_debug("%d %s >> uiRegAddr = %x sMtrwCfgCfg.u32 = %x, sMtrwCfgBase.u32 = %x\n",
			__LINE__, __func__, uiRegAddr, sMtrwCfgCfg.u32, sMtrwCfgBase.u32);

	return uiRetVal;
}

/***************************************************************************************
 * FUNCTION: Configure ISPMTR MMU with frame descriptor
 * PARAMS: mtrObj - scl object
 * RETURN:  ISPSS_MTR_SUCCESS - succeed.
 *          SYNA_ISPSS_DNSCL_EBADPARAM - bad input parameters.
 ***************************************************************************************/
static UINT32 ISPSS_MTR_MMU_Config(struct ISP_MTR_OBJ *mtrObj,
		struct ISP_MTR_BUF_INFO *pIspBufInfo,
		enum ISPSS_MTR_THREAD_ID eThreadId,
		struct ISPSS_MTR_MMU_CFG *psIspSSMtrMmuCfg, int mtrRdWr)
{
	UINT32 uiRetVal = ISPSS_MTR_SUCCESS;
	UINT32 uiRegAddr = 0;
	T32MMU_CFG_pbm   sMmuCfgPbm;
	T32MMU_CFG_vm    sMmuCfgVm;
	T32MMU_CFG_cdesc sMmuCfgCdesc;
	UINT8 base = 0;
	UINT8 ways = 0;

	if (mtrObj == NULL)
		return ISPSS_MTR_PARAM_ERROR;

	if (mtrRdWr == MTR_WRITE) {
		uiRegAddr = mtrObj->base_addr_ispMtr + RA_ISPMTR_mtrw +
			(eThreadId * sizeof(SIE_MTRW)) + RA_MTRW_mmu;
		ISPSS_MTR_MTRW_GetCacheLines(eThreadId, &base, &ways);
	} else {
		uiRegAddr = mtrObj->base_addr_ispMtr + RA_ISPMTR_mtrr +
			(eThreadId * sizeof(SIE_MTRW)) + RA_MTRR_mmu;
		ISPSS_MTR_MTRR_GetCacheLines(eThreadId, &base, &ways);
	}

	sMmuCfgPbm.u32              = 0;
	sMmuCfgPbm.upbm_shy_bw      = psIspSSMtrMmuCfg->upbm_shy_bw;
	sMmuCfgPbm.upbm_shy_pos     = psIspSSMtrMmuCfg->upbm_shy_pos;
	sMmuCfgPbm.upbm_stride_64B  = psIspSSMtrMmuCfg->upbm_stride_64B;
	sMmuCfgPbm.upbm_tileMode    = psIspSSMtrMmuCfg->upbm_tileMode;
	sMmuCfgPbm.upbm_pm_enable   = psIspSSMtrMmuCfg->upbm_pm_enable;
	sMmuCfgPbm.upbm_shuffle_en  = psIspSSMtrMmuCfg->upbm_shuffle_en;
	sMmuCfgPbm.upbm_bm_enable   = psIspSSMtrMmuCfg->upbm_bm_enable;
	sMmuCfgPbm.upbm_weave       = 0;

	sMmuCfgVm.u32                  = 0;
	sMmuCfgVm.uvm_enable           = psIspSSMtrMmuCfg->uvm_enable;
	sMmuCfgVm.uvm_prefecth_disable = 0;
	sMmuCfgVm.uvm_mode             = 1;
	sMmuCfgVm.uvm_mTyp             = 0;
	sMmuCfgVm.uvm_delta            = 0;
	sMmuCfgVm.uvm_base             = psIspSSMtrMmuCfg->uvm_base;
	sMmuCfgVm.uvm_auto_tag_clr     = 0;

	sMmuCfgCdesc.u32          = 0;
	sMmuCfgCdesc.ucdesc_base  = base;
	sMmuCfgCdesc.ucdesc_ways  = ways;
	sMmuCfgCdesc.ucdesc_rpTyp = 1;

	if (pIspBufInfo->pBcmBuf != NULL) {
		ISPSS_MTR_CHECK_RETVAL(ISPSS_BCMBUF_Write(pIspBufInfo->pBcmBuf,
					uiRegAddr + RA_MMU_CFG_pbm, sMmuCfgPbm.u32));
		ISPSS_MTR_CHECK_RETVAL(ISPSS_BCMBUF_Write(pIspBufInfo->pBcmBuf,
					uiRegAddr + RA_MMU_CFG_vm, sMmuCfgVm.u32));
		ISPSS_MTR_CHECK_RETVAL(ISPSS_BCMBUF_Write(pIspBufInfo->pBcmBuf,
					uiRegAddr + RA_MMU_CFG_cdesc, sMmuCfgCdesc.u32));
	} else {
		ISPSS_REG_WRITE32(uiRegAddr + RA_MMU_CFG_pbm, sMmuCfgPbm.u32);
		ISPSS_REG_WRITE32(uiRegAddr + RA_MMU_CFG_vm, sMmuCfgVm.u32);
		ISPSS_REG_WRITE32(uiRegAddr + RA_MMU_CFG_cdesc, sMmuCfgCdesc.u32);
	}

	pr_debug("shy_bw=0x%x, shy_pos=0x%x, stride_64B=0x%x, tilemode = 0x%x pm_en=0x%x,",
			psIspSSMtrMmuCfg->upbm_shy_bw, psIspSSMtrMmuCfg->upbm_shy_pos,
			psIspSSMtrMmuCfg->upbm_stride_64B, psIspSSMtrMmuCfg->upbm_tileMode,
			psIspSSMtrMmuCfg->upbm_pm_enable);
	pr_debug(" shuffle_en=0x%x, bm_en=0x%x, vm_en=0x%x, vbase=0x%x\n",
			psIspSSMtrMmuCfg->upbm_shuffle_en, psIspSSMtrMmuCfg->upbm_bm_enable,
			psIspSSMtrMmuCfg->uvm_enable, psIspSSMtrMmuCfg->uvm_base);
	pr_debug("%s uiRegAddr = %x, sMmuCfgPbm.u32 = %x, sMmuCfgVm.u32 = %x, gCdesc.u32 = %x\n",
			__func__, uiRegAddr, sMmuCfgPbm.u32,
			sMmuCfgVm.u32, sMmuCfgCdesc.u32);

	return uiRetVal;
}

/***************************************************************************************
 * FUNCTION: Set the input window for MTR_R IP
 * PARAMS:   mtrObj - scl object
 * RETURN:   ISPSS_MTR_SUCCESS - succeed.
 *           SYNA_ISPSS_DNSCL_EBADPARAM - bad input parameters.
 ***************************************************************************************/
static UINT32 ISPSS_MTR_MTRR_SetROI(struct ISP_MTR_OBJ *mtrObj,
		struct ISP_MTR_BUF_INFO *pIspBufInfo,
		enum ISPSS_MTR_THREAD_ID eThreadId, struct ISPSS_WIN *mtrRWin)
{
	UINT32 uiRetVal = ISPSS_MTR_SUCCESS;
	UINT32 uiRegAddr = 0;
	T32MTRR_roix sMtrrRoix;
	T32MTRR_roiy sMtrrRoiy;
	UINT32 mtrR_Y_factor = 1;

	if (mtrObj == NULL)
		return ISPSS_MTR_PARAM_ERROR;

	uiRegAddr = mtrObj->base_addr_ispMtr + RA_ISPMTR_mtrr + (eThreadId * sizeof(SIE_MTRR));

	sMtrrRoix.u32       = 0;
	sMtrrRoiy.u32       = 0;
	switch (pIspBufInfo->m_srcfmt) {
	case ISP_SRCFMT_YUV422SP:
	case ISP_SRCFMT_YUV420SP:
	case ISP_SRCFMT_YUV422PACK:
	case ISP_SRCFMT_YUV444PACK:
		if (pIspBufInfo->m_bits_per_pixel == 8) {
			sMtrrRoix.uroix_xs  = mtrRWin->x/64;
			sMtrrRoix.uroix_xm  = (mtrRWin->width - 1)/64;
		} else if (pIspBufInfo->m_bits_per_pixel == 10) {
			sMtrrRoix.uroix_xs  = (mtrRWin->x)*10/8/64;
			sMtrrRoix.uroix_xm  = (mtrRWin->width - 1)*10/8/64;
		}
		break;
	case ISP_SRCFMT_YUV422SP_DWA:
	case ISP_SRCFMT_YUV420SP_DWA:
		sMtrrRoix.uroix_xs  = mtrRWin->x/48;
		sMtrrRoix.uroix_xm  = (mtrRWin->width - 1)/48;
		break;
	case ISP_SRCFMT_YUV420SP_V8H8:
	case ISP_SRCFMT_YUV420SP_V8H6:
	case ISP_SRCFMT_YUV422SP_V8H8:
	case ISP_SRCFMT_YUV422SP_V8H6:
		sMtrrRoix.uroix_xs  = mtrRWin->x/48;
		sMtrrRoix.uroix_xm  = (mtrRWin->width - 1)/48;
		break;
	case ISP_SRCFMT_RGB888PACK:
		sMtrrRoix.uroix_xs  = (mtrRWin->x)/16;
		sMtrrRoix.uroix_xm  = (mtrRWin->width - 1)/16;
		break;
	case ISP_SRCFMT_BAYER_10BIT:
		sMtrrRoix.uroix_xs  = (mtrRWin->x)*10/8/64;
		sMtrrRoix.uroix_xm  = (mtrRWin->width - 1)*10/8/64;
		break;
	case ISP_SRCFMT_BAYER_10BIT_DWA:
		sMtrrRoix.uroix_xs  = (mtrRWin->x)/48;
		sMtrrRoix.uroix_xm  = (mtrRWin->width - 1)/48;
		break;
	case ISP_SRCFMT_BAYER_12BIT:
		sMtrrRoix.uroix_xs  = (mtrRWin->x)*12/8/64;
		sMtrrRoix.uroix_xm  = (mtrRWin->width - 1)*12/8/64;
		break;
	case ISP_SRCFMT_BAYER_12BIT_DWA:
		sMtrrRoix.uroix_xs  = (mtrRWin->x)/40;
		sMtrrRoix.uroix_xm  = (mtrRWin->width - 1)/40;
		break;
	case ISP_SRCFMT_BAYER_8BIT:
	case ISP_SRCFMT_BAYER_16BIT:
		sMtrrRoix.uroix_xs  = (mtrRWin->x)/32;
		sMtrrRoix.uroix_xm  = (mtrRWin->width - 1)/32;
		break;
	default:
		sMtrrRoix.uroix_xs  = 0;
		sMtrrRoix.uroix_xm  = 0;
		uiRetVal = ISPSS_MTR_ERROR;
		break;
	}

	sMtrrRoiy.uroiy_ys  = mtrRWin->y / mtrR_Y_factor;
	sMtrrRoiy.uroiy_ym  = (mtrRWin->height - 1) / mtrR_Y_factor;

	if (pIspBufInfo->pBcmBuf != NULL) {
		ISPSS_MTR_CHECK_RETVAL(ISPSS_BCMBUF_Write(pIspBufInfo->pBcmBuf,
					uiRegAddr + RA_MTRR_roix, sMtrrRoix.u32));
		ISPSS_MTR_CHECK_RETVAL(ISPSS_BCMBUF_Write(pIspBufInfo->pBcmBuf,
					uiRegAddr + RA_MTRR_roiy, sMtrrRoiy.u32));
	} else {
		ISPSS_REG_WRITE32(uiRegAddr + RA_MTRR_roix, sMtrrRoix.u32);
		ISPSS_REG_WRITE32(uiRegAddr + RA_MTRR_roiy, sMtrrRoiy.u32);
	}

	pr_debug("%d %s >> uiRegAddr = %x, sMtrwRoix.u32 = %x, sMtrwRoiy.u32 = %x\n",
			__LINE__, __func__, uiRegAddr, sMtrrRoix.u32, sMtrrRoiy.u32);

	return uiRetVal;
}

/***************************************************************************************
 * FUNCTION: Set the input window for MTR_W IP
 * PARAMS:   mtrObj - scl object
 * RETURN:   ISPSS_MTR_SUCCESS - succeed.
 *           SYNA_ISPSS_DNSCL_EBADPARAM - bad input parameters.
 ***************************************************************************************/
static UINT32 ISPSS_MTR_MTRW_SetROI(struct ISP_MTR_OBJ *mtrObj,
		struct ISP_MTR_BUF_INFO *pIspBufInfo,
		enum ISPSS_MTR_THREAD_ID eThreadId, struct ISPSS_WIN *mtrWWin)
{
	UINT32 uiRetVal = ISPSS_MTR_SUCCESS;
	UINT32 uiRegAddr = 0;
	T32MTRW_roix sMtrwRoix;
	T32MTRW_roiy sMtrwRoiy;
	UINT32 mtrW_Y_factor = 1;

	if (mtrObj == NULL)
		return ISPSS_MTR_PARAM_ERROR;

	uiRegAddr = mtrObj->base_addr_ispMtr + RA_ISPMTR_mtrw + (eThreadId * sizeof(SIE_MTRW));
	sMtrwRoix.u32       = 0;
	sMtrwRoiy.u32       = 0;

	switch (pIspBufInfo->m_srcfmt) {
	case ISP_SRCFMT_YUV422SP:
	case ISP_SRCFMT_YUV420SP:
	case ISP_SRCFMT_YUV422PACK:
	case ISP_SRCFMT_YUV444PACK:
		if (pIspBufInfo->m_bits_per_pixel == 8) {
			sMtrwRoix.uroix_xs  = mtrWWin->x/64;
			sMtrwRoix.uroix_xm  = (mtrWWin->width - 1)/64;
		} else if (pIspBufInfo->m_bits_per_pixel == 10) {
			sMtrwRoix.uroix_xs  = (mtrWWin->x)*10/8/64;
			sMtrwRoix.uroix_xm  = (mtrWWin->width - 1)*10/8/64;
		}
		break;
	case ISP_SRCFMT_YUV422SP_DWA:
	case ISP_SRCFMT_YUV420SP_DWA:
		sMtrwRoix.uroix_xs  = mtrWWin->x/48;
		sMtrwRoix.uroix_xm  = (mtrWWin->width - 1)/48;
		break;
	case ISP_SRCFMT_YUV420SP_V8H8:
	case ISP_SRCFMT_YUV420SP_V8H6:
	case ISP_SRCFMT_YUV422SP_V8H8:
	case ISP_SRCFMT_YUV422SP_V8H6:
		sMtrwRoix.uroix_xs  = mtrWWin->x/48;
		sMtrwRoix.uroix_xm  = (mtrWWin->width - 1)/48;
		break;
	case ISP_SRCFMT_RGB888PACK:
		sMtrwRoix.uroix_xs  = (mtrWWin->x)/16;
		sMtrwRoix.uroix_xm  = (mtrWWin->width - 1)/16;
		break;
	case ISP_SRCFMT_BAYER_10BIT:
		sMtrwRoix.uroix_xs  = (mtrWWin->x)*10/8/64;
		sMtrwRoix.uroix_xm  = (mtrWWin->width - 1)*10/8/64;
		break;
	case ISP_SRCFMT_BAYER_10BIT_DWA:
		sMtrwRoix.uroix_xs  = (mtrWWin->x)/48;
		sMtrwRoix.uroix_xm  = (mtrWWin->width - 1)/48;
		break;
	case ISP_SRCFMT_BAYER_12BIT:
		sMtrwRoix.uroix_xs  = (mtrWWin->x)*12/8/64;
		sMtrwRoix.uroix_xm  = (mtrWWin->width - 1)*12/8/64;
		break;
	case ISP_SRCFMT_BAYER_12BIT_DWA:
		sMtrwRoix.uroix_xs  = (mtrWWin->x)/40;
		sMtrwRoix.uroix_xm  = (mtrWWin->width - 1)/40;
		break;
	case ISP_SRCFMT_BAYER_8BIT:
	case ISP_SRCFMT_BAYER_16BIT:
		sMtrwRoix.uroix_xs  = (mtrWWin->x)/32;
		sMtrwRoix.uroix_xm  = (mtrWWin->width - 1)/32;
		break;
	default:
		sMtrwRoix.uroix_xs  = 0;
		sMtrwRoix.uroix_xm  = 0;
		uiRetVal = ISPSS_MTR_ERROR;
		break;
	}

	sMtrwRoiy.uroiy_ys  = mtrWWin->y / mtrW_Y_factor;
	sMtrwRoiy.uroiy_ym  = (mtrWWin->height - 1) / mtrW_Y_factor;

	if (pIspBufInfo->pBcmBuf != NULL) {
		ISPSS_MTR_CHECK_RETVAL(ISPSS_BCMBUF_Write(pIspBufInfo->pBcmBuf,
					uiRegAddr + RA_MTRW_roix, sMtrwRoix.u32));
		ISPSS_MTR_CHECK_RETVAL(ISPSS_BCMBUF_Write(pIspBufInfo->pBcmBuf,
					uiRegAddr + RA_MTRW_roiy, sMtrwRoiy.u32));
	} else {
		ISPSS_REG_WRITE32(uiRegAddr + RA_MTRW_roix, sMtrwRoix.u32);
		ISPSS_REG_WRITE32(uiRegAddr + RA_MTRW_roiy, sMtrwRoiy.u32);
	}

	return uiRetVal;
}

/***************************************************************************************
 * FUNCTION: Prgrame MTRR block
 * PARAMS:   mtrObj - scl object
 * RETURN:   ISPSS_MTR_SUCCESS - succeed.
 *           SYNA_ISPSS_DNSCL_EBADPARAM - bad input parameters.
 ***************************************************************************************/
static UINT32 ISPSS_MTR_MTRR_Program(struct ISP_MTR_OBJ *mtrObj,
		struct ISP_MTR_BUF_INFO *pIspBufInfo,
		enum ISPSS_MTR_THREAD_ID eThreadId, BOOL kickOffEn,
		struct ISPSS_MTR_MTRR_Prog *psIspSSMtrrProg)
{
	UINT32 uiRetVal = ISPSS_MTR_SUCCESS;

	if (mtrObj == NULL)
		return ISPSS_MTR_PARAM_ERROR;

	if (kickOffEn == FALSE) {
		mtrObj->uiIspMtrKickoff = (mtrObj->uiIspMtrKickoff & ~(0x1 << eThreadId));
		goto exit;
	} else {
		mtrObj->uiIspMtrKickoff = (mtrObj->uiIspMtrKickoff | (0x1 << eThreadId));
	}

	ISPSS_MTR_CHECK_RETVAL(ISPSS_MTR_MTRR_Config(mtrObj, pIspBufInfo, eThreadId,
				&psIspSSMtrrProg->ispSSMtrrCfg));
	ISPSS_MTR_CHECK_RETVAL(ISPSS_MTR_MMU_Config(mtrObj, pIspBufInfo, eThreadId,
				&psIspSSMtrrProg->ispSSMtrMmuCfg, MTR_READ));

exit:
	return uiRetVal;
}

/***************************************************************************************
 * FUNCTION: Prgrame MTRW block
 * PARAMS:   mtrObj - scl object
 * RETURN:   ISPSS_MTR_SUCCESS - succeed.
 *           SYNA_ISPSS_DNSCL_EBADPARAM - bad input parameters.
 ***************************************************************************************/
static UINT32 ISPSS_MTR_MTRW_Program(struct ISP_MTR_OBJ *mtrObj,
		struct ISP_MTR_BUF_INFO *pIspBufInfo,
		enum ISPSS_MTR_THREAD_ID eThreadId, BOOL kickOffEn,
		struct ISPSS_MTR_MTRR_Prog *psIspSSMtrwProg)
{
	UINT32 uiRetVal = ISPSS_MTR_SUCCESS;

	if (mtrObj == NULL)
		return ISPSS_MTR_PARAM_ERROR;

	if (kickOffEn == FALSE) {
		mtrObj->uiIspMtrKickoff = (mtrObj->uiIspMtrKickoff & ~(0x1 << eThreadId));
		goto exit;
	} else {
		// for write thread
		mtrObj->uiIspMtrKickoff = (mtrObj->uiIspMtrKickoff | (0x1 << (16 + eThreadId)));
	}
	ISPSS_MTR_CHECK_RETVAL(ISPSS_MTR_MTRW_Config(mtrObj, pIspBufInfo, eThreadId,
				&psIspSSMtrwProg->ispSSMtrrCfg));
	ISPSS_MTR_CHECK_RETVAL(ISPSS_MTR_MMU_Config(mtrObj, pIspBufInfo, eThreadId,
				&psIspSSMtrwProg->ispSSMtrMmuCfg, MTR_WRITE));

exit:
	return uiRetVal;
}

/***************************************************************************************
 * FUNCTION: ISPMTR system level configuration
 * PARAMS:   mtrObj - scl object
 * RETURN:   ISPSS_MTR_SUCCESS - succeed.
 *           SYNA_ISPSS_DNSCL_EBADPARAM - bad input parameters.
 * PARAMS:
 * RETURN:
 ***************************************************************************************/
UINT32 ISPSS_MTR_Config(struct ISP_MTR_OBJ *mtrObj, int config, struct BCMBUF *pbcmbuf)
{
	UINT32 uiRetVal = ISPSS_MTR_SUCCESS;
	UINT32 uiRegAddr = 0;
	T32ISPMTR_cfg sIspmtrCfg;

	if (mtrObj == NULL)
		return ISPSS_MTR_PARAM_ERROR;

	uiRegAddr = mtrObj->base_addr_ispMtr;

	sIspmtrCfg.u32 = 0;
	if (config) {
#ifdef USE_MTR_QOS_ENABLE
		sIspmtrCfg.ucfg_mtrr_flow_ctrl = 0;
		sIspmtrCfg.ucfg_mtrr_qos_en    = 0;
		sIspmtrCfg.ucfg_mtrr_qos       = 0;
		sIspmtrCfg.ucfg_mtrr_dis_mtid  = 1;
		sIspmtrCfg.ucfg_mmu_pageSz     = 0;
		sIspmtrCfg.ucfg_mmu_qos_en     = 0;
		sIspmtrCfg.ucfg_mmu_qos        = 0;
		sIspmtrCfg.ucfg_mmu_banks      = 1;
		sIspmtrCfg.ucfg_mmu_sysSz      = 0;
		sIspmtrCfg.ucfg_mmu_tarSz      = 0;
#else
		sIspmtrCfg.ucfg_mtrr_flow_ctrl = 0;
		sIspmtrCfg.ucfg_mtrr_qos_en    = 1;
		sIspmtrCfg.ucfg_mtrr_qos       = 0xf;
		sIspmtrCfg.ucfg_mtrr_dis_mtid  = 1;
		sIspmtrCfg.ucfg_mmu_pageSz     = 0;
		sIspmtrCfg.ucfg_mmu_qos_en     = 1;
		sIspmtrCfg.ucfg_mmu_qos        = 0xf;
		sIspmtrCfg.ucfg_mmu_banks      = 1;
		sIspmtrCfg.ucfg_mmu_sysSz      = 0;
		sIspmtrCfg.ucfg_mmu_tarSz      = 0;
#endif
	}

	if (pbcmbuf != NULL)
		ISPSS_MTR_CHECK_RETVAL(ISPSS_BCMBUF_Write(pbcmbuf,
					uiRegAddr + RA_ISPMTR_cfg, sIspmtrCfg.u32));
	else
		ISPSS_REG_WRITE32(uiRegAddr + RA_ISPMTR_cfg, sIspmtrCfg.u32);

	return uiRetVal;
}

/***************************************************************************************
 * FUNCTION: Kickoff the active threads for next interrupt
 * PARAMS:   mtrObj - scl object
 * RETURN:   ISPSS_MTR_SUCCESS - succeed.
 *           SYNA_ISPSS_DNSCL_EBADPARAM - bad input parameters.
 ***************************************************************************************/
UINT32 ISPSS_MTR_kickOff(struct ISP_MTR_OBJ *mtrObj, struct ISP_MTR_BUF_INFO *pIspBufInfo)
{
	UINT32 uiRetVal = ISPSS_MTR_SUCCESS;
	UINT32 uiRegAddr = 0;
	UINT32 uiRegVal;
	INT32 i  = 0;

	uiRegAddr = mtrObj->base_addr_ispMtr + RA_ISPMTR_kickoffR0;
	if (mtrObj == NULL)
		return ISPSS_MTR_PARAM_ERROR;

	/* Kickoff should be done after all the configs. All thread kickoff should go together */
	for (i = 0; i < (ISPSS_MTR_THREAD_MAX*2); i++) {
		if (mtrObj->uiIspMtrKickoff&(1<<i)) {
			//uiRegVal = mtrObj->uiIspMtrKickoff;
			uiRegVal = 1; // kickoff value 1
			if (pIspBufInfo->pBcmBuf != NULL) {
				ISPSS_MTR_CHECK_RETVAL(ISPSS_BCMBUF_Write(pIspBufInfo->pBcmBuf,
							uiRegAddr, 1));
				ISPSS_MTR_CHECK_RETVAL(ISPSS_BCMBUF_Write(pIspBufInfo->pBcmBuf,
							uiRegAddr, 0));
			} else {
				ISPSS_REG_WRITE32(uiRegAddr, uiRegVal);
				ISPSS_REG_WRITE32(uiRegAddr, 0);
			}
			pr_debug("Kickoff Reg: 0x%08x, Val: 0x%08x\n", uiRegAddr, uiRegVal);
		}
		uiRegAddr += 4;
	}

	return uiRetVal;
}

/***************************************************************************************
 * FUNCTION:Return thread IDs for Luma and croma
 * PARAMS:  mtrObj - scl object
 * RETURN:  ISPSS_MTR_SUCCESS - succeed.
 *          SYNA_ISPSS_DNSCL_EBADPARAM - bad input parameters.
 ***************************************************************************************/
static UINT32 ISPSS_GetThreadIDs(enum ISPSS_MTR_PATH_ID path,
		UINT8 *threadID_Y, UINT8 *threadID_UV)
{
	UINT32 uiRetVal = ISPSS_MTR_SUCCESS;

	switch (path) {
	case ISPSS_MTR_PATH_MCM0_RD:
	case ISPSS_MTR_PATH_MCM1_RD:
	case ISPSS_MTR_PATH_MCM0_WR:
		*threadID_Y  = (UINT8)ISPSS_MTR_THREAD_0;
		break;
	case ISPSS_MTR_PATH_MCM1_WR:
		*threadID_Y  = (UINT8)ISPSS_MTR_THREAD_1;
		break;
	case ISPSS_MTR_PATH_TDNR0_RD:
	case ISPSS_MTR_PATH_TDNR1_RD:
		*threadID_Y  = (UINT8)ISPSS_MTR_THREAD_1;
		break;
	case ISPSS_MTR_PATH_TDNR0_WR:
	case ISPSS_MTR_PATH_TDNR1_WR:
		*threadID_Y  = (UINT8)ISPSS_MTR_THREAD_2;
		break;
	case ISPSS_MTR_PATH_TILE_RD://TODO:
	case ISPSS_MTR_PATH_MP0_WR:
	case ISPSS_MTR_PATH_MP1_WR:
		*threadID_Y  = (UINT8)ISPSS_MTR_THREAD_3;
		*threadID_UV = (UINT8)ISPSS_MTR_THREAD_4;
		break;
	case ISPSS_MTR_PATH_SP2_WR0:
	case ISPSS_MTR_PATH_SP2_WR1:
		*threadID_Y  = (UINT8)ISPSS_MTR_THREAD_9;
		*threadID_UV = (UINT8)ISPSS_MTR_THREAD_10;
		break;
	default:
		uiRetVal = ISPSS_MTR_ERROR;
		break;
	}

	return uiRetVal;
}

/***************************************************************************************
 * FUNCTION: Update stride value based on source format
 * PARAMS:   mtrObj - scl object
 * RETURN:   ISPSS_MTR_SUCCESS - succeed.
 *           SYNA_ISPSS_DNSCL_EBADPARAM - bad input parameters.
 ***************************************************************************************/
UINT32 ISPSS_MTR_GetStride(struct ISP_MTR_BUF_INFO *pIspBufInfo, UINT32 *stride_64B)
{
	UINT32 pictureWidth = pIspBufInfo->m_active_width;
	UINT32 ret = ISPSS_MTR_SUCCESS;

	switch (pIspBufInfo->m_srcfmt) {
	case ISP_SRCFMT_YUV422SP:
	case ISP_SRCFMT_YUV420SP:
	case ISP_SRCFMT_YUV422PACK:
		//        case ISP_SRCFMT_YUV444PACK:
		if (pIspBufInfo->m_bits_per_pixel == 8)
			*stride_64B = DIV_ROUND_UP(pictureWidth, 256) * 4;
		else if (pIspBufInfo->m_bits_per_pixel == 10)
			*stride_64B = DIV_ROUND_UP(((pictureWidth * 10) * 8), 256) * 4;
		break;
	case ISP_SRCFMT_YUV422SP_DWA:
	case ISP_SRCFMT_YUV420SP_DWA:
		*stride_64B = DIV_ROUND_UP(pictureWidth, 192) * 4;
		break;
	case ISP_SRCFMT_YUV420SP_V8H8:
	case ISP_SRCFMT_YUV420SP_V8H6:
	case ISP_SRCFMT_YUV422SP_V8H8:
	case ISP_SRCFMT_YUV422SP_V8H6:
		*stride_64B = (pIspBufInfo->m_buf_stride_UV >> (ISPSS_MTR_PBM_STRIDE_ALIGN +
						(ISPSS_MTR_TILEHEIGHT_ALIGN)));
		break;
	case ISP_SRCFMT_RGB888PACK:
	case ISP_SRCFMT_YUV444PACK:
		//*stride_64B = (DIV_ROUND_UP(pictureWidth/64)) * 4;
		*stride_64B = (DIV_ROUND_UP((pictureWidth * 3), 256)) * 4;
		break;
	case ISP_SRCFMT_BAYER_8BIT:
		*stride_64B = DIV_ROUND_UP(pictureWidth, 256) * 4;
		break;
	case ISP_SRCFMT_BAYER_10BIT:
		*stride_64B = (DIV_ROUND_UP(pictureWidth * 10 / 8, 256)) * 4;
		break;
	case ISP_SRCFMT_BAYER_10BIT_DWA:
		*stride_64B = (DIV_ROUND_UP(pictureWidth, 192)) * 4;
		break;
	case ISP_SRCFMT_BAYER_12BIT:
		*stride_64B = (DIV_ROUND_UP(pictureWidth * 12 / 8, 256)) * 4;
		break;
	case ISP_SRCFMT_BAYER_12BIT_DWA:
		*stride_64B = (DIV_ROUND_UP(pictureWidth, 160)) * 4;
		break;
	case ISP_SRCFMT_BAYER_16BIT:
		*stride_64B = (DIV_ROUND_UP(pictureWidth, 128)) * 4;
		break;
	default:
		stride_64B = 0;
		ret = ISPSS_MTR_ERROR;
		break;
	}

	return ret;
}

/***************************************************************************************
 * FUNCTION: Update frame descriptor to ISP MTR registers
 * PARAMS:   mtrObj - scl object
 * RETURN:   ISPSS_MTR_SUCCESS - succeed.
 *           SYNA_ISPSS_DNSCL_EBADPARAM - bad input parameters.
 ***************************************************************************************/
UINT32 ISPSS_MTR_UpdateDesc(struct ISP_MTR_OBJ *mtrObj, struct ISP_MTR_BUF_INFO *pIspBufInfo,
		enum ISPSS_MTR_PATH_ID path)
{
	int ret;

	if (path <= ISPSS_MTR_PATH_DWNSCLR_RD)
		ret = ISPSS_MTR_UpdateReadDesc(mtrObj, pIspBufInfo);
	else
		ret = ISPSS_MTR_UpdateWriteDesc(mtrObj, pIspBufInfo);

	return ret;
}

/***************************************************************************************
 * FUNCTION: Update frame descriptor to ISP MTR registers
 * PARAMS:   mtrObj - scl object
 * RETURN:   ISPSS_MTR_SUCCESS - succeed.
 *           SYNA_ISPSS_DNSCL_EBADPARAM - bad input parameters.
 ***************************************************************************************/
UINT32 ISPSS_MTR_UpdateReadDesc(struct ISP_MTR_OBJ *mtrObj, struct ISP_MTR_BUF_INFO *pIspBufInfo)
{
	UINT32 uiRetVal = ISPSS_MTR_SUCCESS;
	struct ISPSS_MTR_MTRR_Prog sIspMtrProg;
	UINT8 threadID_Y = 0xFF, threadID_UV = 0xFF;
	struct ISPSS_WIN mtrRWin;
	BOOL kickOffEn = 1;
	UINT32 stride_64B = 0;

	if (mtrObj == NULL)
		return ISPSS_MTR_PARAM_ERROR;

	ISPSS_GetThreadIDs(mtrObj->path, &threadID_Y, &threadID_UV);
	/* TODO: For tiler formats set value 1 */
	sIspMtrProg.ispSSMtrMmuCfg.upbm_tileMode = 0;
	ISPSS_MTR_GetStride(pIspBufInfo, &stride_64B);
	sIspMtrProg.ispSSMtrrCfg.ucfg_stride = pIspBufInfo->m_mtr_buf_desc_Y.m_MtrrCfgCfg_stride;
	sIspMtrProg.ispSSMtrrCfg.ucfg_format = pIspBufInfo->m_mtr_buf_desc_Y.m_MtrrCfgCfg_format;
	sIspMtrProg.ispSSMtrrCfg.ucfg_enable = pIspBufInfo->m_is_compressed & 0x1;
	sIspMtrProg.ispSSMtrrCfg.ucfg_mode = pIspBufInfo->m_mtr_buf_desc_Y.m_MtrrCfgCfg_mode;
	sIspMtrProg.ispSSMtrrCfg.ucfg_hflip = 0;
	sIspMtrProg.ispSSMtrrCfg.ucfg_vflip = 0;
	sIspMtrProg.ispSSMtrrCfg.ubase_addr =
		(pIspBufInfo->m_mtr_buf_desc_Y.m_MtrrCfgBase_addr);
	sIspMtrProg.ispSSMtrMmuCfg.upbm_shy_bw =
		pIspBufInfo->m_mtr_buf_desc_Y.m_MmuCfgPbm_shy_bw;
	sIspMtrProg.ispSSMtrMmuCfg.upbm_shy_pos =
		pIspBufInfo->m_mtr_buf_desc_Y.m_MmuCfgPbm_shy_pos;
	sIspMtrProg.ispSSMtrMmuCfg.upbm_stride_64B =  stride_64B;
	sIspMtrProg.ispSSMtrMmuCfg.upbm_pm_enable =
		pIspBufInfo->m_mtr_buf_desc_Y.m_MmuCfgPbm_pm_enable;
	sIspMtrProg.ispSSMtrMmuCfg.upbm_shuffle_en =
		pIspBufInfo->m_mtr_buf_desc_Y.m_MmuCfgPbm_shuffle_en;
	sIspMtrProg.ispSSMtrMmuCfg.upbm_bm_enable =
		pIspBufInfo->m_mtr_buf_desc_Y.m_MmuCfgPbm_bm_enable;
	sIspMtrProg.ispSSMtrMmuCfg.uvm_enable = pIspBufInfo->m_mtr_buf_desc_Y.m_MmuCfgVm_enable;
	sIspMtrProg.ispSSMtrMmuCfg.uvm_base =
		((UINT32)pIspBufInfo->m_pbuf_start >> ISPSS_MTR_BASE_ADDR_ALIGN);
	ISPSS_MTR_MTRR_Program(mtrObj, pIspBufInfo, threadID_Y, kickOffEn, &sIspMtrProg);

	if (pIspBufInfo->m_srcfmt <= ISP_SRCFMT_YUV422SP_V8H6) {
		sIspMtrProg.ispSSMtrrCfg.ucfg_stride =
			pIspBufInfo->m_mtr_buf_desc_UV.m_MtrrCfgCfg_stride;
		sIspMtrProg.ispSSMtrrCfg.ucfg_format =
			pIspBufInfo->m_mtr_buf_desc_UV.m_MtrrCfgCfg_format;
		sIspMtrProg.ispSSMtrrCfg.ucfg_mode =
			pIspBufInfo->m_mtr_buf_desc_UV.m_MtrrCfgCfg_mode;
		sIspMtrProg.ispSSMtrrCfg.ucfg_hflip = 0;
		sIspMtrProg.ispSSMtrrCfg.ucfg_vflip = 0;
		sIspMtrProg.ispSSMtrrCfg.ubase_addr =
			(pIspBufInfo->m_mtr_buf_desc_UV.m_MtrrCfgBase_addr);
		sIspMtrProg.ispSSMtrMmuCfg.upbm_shy_bw =
			pIspBufInfo->m_mtr_buf_desc_UV.m_MmuCfgPbm_shy_bw;
		sIspMtrProg.ispSSMtrMmuCfg.upbm_shy_pos =
			pIspBufInfo->m_mtr_buf_desc_UV.m_MmuCfgPbm_shy_pos;
		sIspMtrProg.ispSSMtrMmuCfg.upbm_stride_64B =  stride_64B;
		sIspMtrProg.ispSSMtrMmuCfg.upbm_pm_enable =
			pIspBufInfo->m_mtr_buf_desc_UV.m_MmuCfgPbm_pm_enable;
		sIspMtrProg.ispSSMtrMmuCfg.upbm_shuffle_en =
			pIspBufInfo->m_mtr_buf_desc_UV.m_MmuCfgPbm_shuffle_en;
		sIspMtrProg.ispSSMtrMmuCfg.upbm_bm_enable =
			pIspBufInfo->m_mtr_buf_desc_UV.m_MmuCfgPbm_bm_enable;
		sIspMtrProg.ispSSMtrMmuCfg.uvm_enable =
			pIspBufInfo->m_mtr_buf_desc_UV.m_MmuCfgVm_enable;
		sIspMtrProg.ispSSMtrMmuCfg.uvm_base =
			((UINT32)pIspBufInfo->m_buf_pbuf_start_UV >> ISPSS_MTR_BASE_ADDR_ALIGN);
		ISPSS_MTR_MTRR_Program(mtrObj, pIspBufInfo, threadID_UV, kickOffEn, &sIspMtrProg);
	}
	if (pIspBufInfo->m_is_compressed) {
		mtrRWin.x       = pIspBufInfo->leftCrop;
		mtrRWin.y       = pIspBufInfo->topCrop;
		mtrRWin.width   = pIspBufInfo->m_active_width;
		mtrRWin.height  = pIspBufInfo->m_active_height;
		ISPSS_MTR_MTRR_SetROI(mtrObj, pIspBufInfo, threadID_Y, &mtrRWin);
		mtrRWin.height  = pIspBufInfo->m_active_height/2;
		ISPSS_MTR_MTRR_SetROI(mtrObj, pIspBufInfo, threadID_UV, &mtrRWin);
	}
	pr_debug("%d %s >>  Y Base_addr = %x, stride_64B = %x, vbase = %lx,\n",
			__LINE__, __func__, pIspBufInfo->m_mtr_buf_desc_Y.m_MtrrCfgBase_addr,
			pIspBufInfo->m_buf_stride, (unsigned long)pIspBufInfo->m_pbuf_start);
	pr_debug("%d %s >>  UV Base_addr = %x, stride_64B = %x, vbase = %lx,\n",
		__LINE__, __func__, pIspBufInfo->m_mtr_buf_desc_UV.m_MtrrCfgBase_addr,
		pIspBufInfo->m_buf_stride_UV, (unsigned long)pIspBufInfo->m_buf_pbuf_start_UV);

	return uiRetVal;
}

/***************************************************************************************
 * FUNCTION: Update frame descriptor to ISP MTR registers
 * PARAMS:   mtrObj - scl object
 * RETURN:   ISPSS_MTR_SUCCESS - succeed.
 *           SYNA_ISPSS_DNSCL_EBADPARAM - bad input parameters.
 ***************************************************************************************/
UINT32 ISPSS_MTR_UpdateWriteDesc(struct ISP_MTR_OBJ *mtrObj, struct ISP_MTR_BUF_INFO *pIspBufInfo)
{
	UINT32 uiRetVal = ISPSS_MTR_SUCCESS;
	struct ISPSS_MTR_MTRR_Prog sIspMtrProg;
	UINT8 threadID_Y = 0xFF, threadID_UV = 0xFF;
	struct ISPSS_WIN mtrWWin;
	BOOL kickOffEn = 1;
	UINT32 stride_64B = 0;

	if (mtrObj == NULL)
		return ISPSS_MTR_PARAM_ERROR;

	ISPSS_GetThreadIDs(mtrObj->path, &threadID_Y, &threadID_UV);
	/* Only considering Non tiler o/p */
	sIspMtrProg.ispSSMtrMmuCfg.upbm_tileMode = 0;

	ISPSS_MTR_GetStride(pIspBufInfo, &stride_64B);
	sIspMtrProg.ispSSMtrrCfg.ucfg_stride = pIspBufInfo->m_mtr_buf_desc_Y.m_MtrrCfgCfg_stride;
	sIspMtrProg.ispSSMtrrCfg.ucfg_format = pIspBufInfo->m_mtr_buf_desc_Y.m_MtrrCfgCfg_format;
	sIspMtrProg.ispSSMtrrCfg.ucfg_enable = pIspBufInfo->m_is_compressed & 0x1;
	sIspMtrProg.ispSSMtrrCfg.ucfg_mode   = pIspBufInfo->m_mtr_buf_desc_Y.m_MtrrCfgCfg_mode;
	sIspMtrProg.ispSSMtrrCfg.ucfg_hflip  = 0;
	sIspMtrProg.ispSSMtrrCfg.ucfg_vflip  = 0;
	sIspMtrProg.ispSSMtrrCfg.ubase_addr  = (pIspBufInfo->m_mtr_buf_desc_Y.m_MtrrCfgBase_addr);
	sIspMtrProg.ispSSMtrMmuCfg.upbm_shy_bw =
		pIspBufInfo->m_mtr_buf_desc_Y.m_MmuCfgPbm_shy_bw;
	sIspMtrProg.ispSSMtrMmuCfg.upbm_shy_pos =
		pIspBufInfo->m_mtr_buf_desc_Y.m_MmuCfgPbm_shy_pos;
	sIspMtrProg.ispSSMtrMmuCfg.upbm_stride_64B = stride_64B;
	sIspMtrProg.ispSSMtrMmuCfg.upbm_pm_enable =
		pIspBufInfo->m_mtr_buf_desc_Y.m_MmuCfgPbm_pm_enable;
	sIspMtrProg.ispSSMtrMmuCfg.upbm_shuffle_en =
		pIspBufInfo->m_mtr_buf_desc_Y.m_MmuCfgPbm_shuffle_en;
	sIspMtrProg.ispSSMtrMmuCfg.upbm_bm_enable =
		pIspBufInfo->m_mtr_buf_desc_Y.m_MmuCfgPbm_bm_enable;
	sIspMtrProg.ispSSMtrMmuCfg.uvm_enable =
		pIspBufInfo->m_mtr_buf_desc_Y.m_MmuCfgVm_enable;
	sIspMtrProg.ispSSMtrMmuCfg.uvm_base =
		((UINT32)pIspBufInfo->m_pbuf_start >> ISPSS_MTR_BASE_ADDR_ALIGN);
	ISPSS_MTR_MTRW_Program(mtrObj, pIspBufInfo, threadID_Y, kickOffEn, &sIspMtrProg);

	if (pIspBufInfo->m_srcfmt <= ISP_SRCFMT_YUV_MAX) {
		sIspMtrProg.ispSSMtrrCfg.ucfg_stride =
			pIspBufInfo->m_mtr_buf_desc_UV.m_MtrrCfgCfg_stride;
		sIspMtrProg.ispSSMtrrCfg.ucfg_format =
			pIspBufInfo->m_mtr_buf_desc_UV.m_MtrrCfgCfg_format;
		sIspMtrProg.ispSSMtrrCfg.ucfg_mode   =
			pIspBufInfo->m_mtr_buf_desc_UV.m_MtrrCfgCfg_mode;
		sIspMtrProg.ispSSMtrrCfg.ucfg_hflip  = 0;
		sIspMtrProg.ispSSMtrrCfg.ucfg_vflip  = 0;
		sIspMtrProg.ispSSMtrrCfg.ubase_addr  =
			(pIspBufInfo->m_mtr_buf_desc_UV.m_MtrrCfgBase_addr);
		sIspMtrProg.ispSSMtrMmuCfg.upbm_shy_bw =
			pIspBufInfo->m_mtr_buf_desc_UV.m_MmuCfgPbm_shy_bw;
		sIspMtrProg.ispSSMtrMmuCfg.upbm_shy_pos =
			pIspBufInfo->m_mtr_buf_desc_UV.m_MmuCfgPbm_shy_pos;
		sIspMtrProg.ispSSMtrMmuCfg.upbm_stride_64B = stride_64B;
		sIspMtrProg.ispSSMtrMmuCfg.upbm_pm_enable =
			pIspBufInfo->m_mtr_buf_desc_UV.m_MmuCfgPbm_pm_enable;
		sIspMtrProg.ispSSMtrMmuCfg.upbm_shuffle_en =
			pIspBufInfo->m_mtr_buf_desc_UV.m_MmuCfgPbm_shuffle_en;
		sIspMtrProg.ispSSMtrMmuCfg.upbm_bm_enable =
			pIspBufInfo->m_mtr_buf_desc_UV.m_MmuCfgPbm_bm_enable;
		sIspMtrProg.ispSSMtrMmuCfg.uvm_enable =
			pIspBufInfo->m_mtr_buf_desc_UV.m_MmuCfgVm_enable;
		sIspMtrProg.ispSSMtrMmuCfg.uvm_base =
			((UINT32)pIspBufInfo->m_buf_pbuf_start_UV >> ISPSS_MTR_BASE_ADDR_ALIGN);
		ISPSS_MTR_MTRW_Program(mtrObj, pIspBufInfo, threadID_UV, kickOffEn, &sIspMtrProg);
	}

	if (pIspBufInfo->m_is_compressed) {
		mtrWWin.x       = pIspBufInfo->leftCrop;
		mtrWWin.y       = pIspBufInfo->topCrop;
		mtrWWin.width   = pIspBufInfo->m_active_width;
		mtrWWin.height  = pIspBufInfo->m_active_height;
		ISPSS_MTR_MTRW_SetROI(mtrObj, pIspBufInfo, threadID_Y, &mtrWWin);
		if (pIspBufInfo->m_srcfmt <= ISP_SRCFMT_YUV_MAX) {
			mtrWWin.height  = pIspBufInfo->m_active_height/2;
			ISPSS_MTR_MTRW_SetROI(mtrObj, pIspBufInfo, threadID_UV, &mtrWWin);
		}
	}

	pr_debug("%d %s >>  Y Base_addr = %x, stride_64B = %x, vbase = %x\n",
			__LINE__, __func__, pIspBufInfo->m_mtr_buf_desc_Y.m_MtrrCfgBase_addr,
			pIspBufInfo->m_buf_stride, pIspBufInfo->m_pbuf_start);
	pr_debug("%d %s >>  UV Base_addr = %x, stride_64B = %x, vbase = %x\n",
			__LINE__, __func__, pIspBufInfo->m_mtr_buf_desc_UV.m_MtrrCfgBase_addr,
			pIspBufInfo->m_buf_stride_UV, pIspBufInfo->m_buf_pbuf_start_UV);

	return uiRetVal;
}

/***************************************************************************************
 * FUNCTION: Dump the registers for debugging
 * PARAMS:   mtrObj - scl object
 * RETURN:   ISPSS_MTR_SUCCESS - succeed.
 *           SYNA_ISPSS_DNSCL_EBADPARAM - bad input parameters.
 ***************************************************************************************/
UINT32 ISPSS_MTR_PrintRegisters(struct ISP_MTR_OBJ *mtrObj)
{
	UINT32 reg;
	UINT32 val;
	UINT32 mtrLastReg = CONFIG_DOLPHIN_ISPSS_REGAREA_BASE +
		ISP_MTR_REG_BASE + RA_ISPMTR_cfg + 0x384; /* last thread address */
	UINT32 ispRemapReg = CONFIG_DOLPHIN_ISPSS_REGAREA_BASE +
		ISPSS_MEMMAP_GLB_REG_BASE + RA_IspMISC_remap;

	if (mtrObj == NULL)
		return ISPSS_MTR_PARAM_ERROR;

	reg = ispRemapReg;

	/* Print global remap registers */
	while (reg <= ispRemapReg + RA_Remap_BCMMtrFlag) {
		ISPSS_REG_READ32(reg, &val);
		pr_err("Debug:: Register:%08x Value:%08x\n", reg, val);
		reg = reg + 4;
	}

	/* Print from first register to last MTR register */
	reg = CONFIG_DOLPHIN_ISPSS_REGAREA_BASE + ISP_MTR_REG_BASE + RA_ISPMTR_cfg;
	while (reg <= mtrLastReg) {
		ISPSS_REG_READ32(reg, &val);
		pr_err("Debug:: Register:%08x Value:%08x\n", reg, val);
		reg += 4;
	}

	return 0;
}
