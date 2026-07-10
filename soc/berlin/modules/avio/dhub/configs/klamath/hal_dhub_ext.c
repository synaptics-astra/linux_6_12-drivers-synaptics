// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2025 Synaptics Incorporated
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/stddef.h>
#include <linux/export.h>
#include <linux/kernel.h>

#include "ctypes.h"
#include "avio_io.h"
#include "hal_dhub.h"
#include "drv_dhub.h"
#include "Galois_memmap.h"
#include "avioGbl.h"
#include "vppGbl.h"

void dhub2nd_suspend(void *hdl, int enable)
{
	DHUB_CTX *hDhubCtx = (DHUB_CTX *)hdl;
	unsigned int vppSramPwrCtl;
	unsigned int aioSramPwrCtl;
	unsigned int avioGblCtrl;
	const unsigned int baseAddr		= hDhubCtx->avio_gbl_base;
	const unsigned int vppSramPwrCtrlAddr	= baseAddr + RA_vppGbl_VPP128bDHUB_SRAMPWR + RA_SRAMPWR_ctrl;
	const unsigned int aioSramPwrCtrlAddr	= baseAddr + RA_aioGbl_AIO64bDHUB_SRAMPWR + RA_SRAMPWR_ctrl;
	const unsigned int avioGblCtrlAddr	= baseAddr + RA_vppGbl_CTRL;

	/* Skip power control if seamless switch is confirmed during suspend */
	if (enable && avio_lcdc_is_lp_enabled())
		goto skip_power_ctrl_done;

	AVIO_REG_WORD32_READ(vppSramPwrCtrlAddr, &vppSramPwrCtl);
	AVIO_REG_WORD32_READ(aioSramPwrCtrlAddr, &aioSramPwrCtl);
	AVIO_REG_WORD32_READ(avioGblCtrlAddr, &avioGblCtrl);

	if (enable) {
		vppSramPwrCtl	|= 0x7;
		aioSramPwrCtl	|= 0x7;
		avioGblCtrl	|= 0x5;
	} else {
		vppSramPwrCtl	&= ~0x7;
		aioSramPwrCtl	&= ~0x7;
		avioGblCtrl	&= ~0x5;
	}
	AVIO_REG_WORD32_WRITE(vppSramPwrCtrlAddr, vppSramPwrCtl);
	AVIO_REG_WORD32_WRITE(aioSramPwrCtrlAddr, aioSramPwrCtl);
	AVIO_REG_WORD32_WRITE(avioGblCtrlAddr, avioGblCtrl);

skip_power_ctrl_done:
	drv_dhub_suspend_dhub(enable, hDhubCtx);
}
