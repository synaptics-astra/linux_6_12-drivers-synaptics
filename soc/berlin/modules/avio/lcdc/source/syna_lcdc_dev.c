// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 Synaptics Incorporated
 */
#include "Galois_memmap.h"
#include "avioGbl.h"
#include "lcdc_cfg_prv.h"
#include "avio_memmap.h"
#include "lcdc.h"
#include "syna_lcdc_drv.h"
#include "syna_lcdc_dev.h"
#include "avio_io.h"
#include "vpp_api.h"

#include "drv_lcdc.h"

struct syna_lcdc_dev *syna_lcdc[SYNA_LCDC_MAX];

static LCDC_CTX     lcdc_ctx;

unsigned int syna_lcdc_read(struct syna_lcdc_dev *dev, unsigned long addr)
{
	unsigned int val;

	GA_REG_WORD32_READ(dev->core_addr + addr, &val);
	return val;
}

void syna_lcdc_write(struct syna_lcdc_dev *dev, unsigned long addr, unsigned int val)
{
	addr += dev->core_addr;

	if (dev->bcm_enable)
		bcmbuf_write(CURR_VBI_BCM_BUF, addr, val);
	else
		GA_REG_WORD32_WRITE(addr, val);
}

static void syna_lcdc_hw_param_update(struct syna_lcdc_dev *dev)
{
	syna_lcdc_write(dev, LCDC_REG_PARUP, 1);

	if (dev->panel->intf_type & SYNA_LCDC_TYPE_DPI_MCU)
		syna_lcdc_write(dev, LCDC_REG_CDISPUPR, 1); /*start sending display command & data*/

	if (dev->panel->intf_type & SYNA_LCDC_INTF_TYPE_DSI)
		/* send single new frame */
		syna_lcdc_write(dev, LCDC_REG_LCDCCR, 1);
}

static void syna_lcdc_semaintr_enable(int intr, int enable)
{
	HDL_semaphore *pSemHandle;
	HDL_dhub2d *vpp_dhubHandle = SYNA_LCDC_VPP_DHUB_HANDLE;
	if (NULL == vpp_dhubHandle) {
		avio_error("invalid vpp dhub Handle\n");
		return;
	}
	pSemHandle = dhub_semaphore(&vpp_dhubHandle->dhub);

	semaphore_cfg(pSemHandle, intr, 1, 0);
	semaphore_clr_full(pSemHandle, intr);
	semaphore_intr_enable(pSemHandle, intr, 0, enable, 0, 0, 0);
}

static void syna_lcdc_wrap_lcdcclk_enable(int lcdcID, int enable)
{
	unsigned int addr;
	T32avioVppGbl_LCDC_CTRL lcdc_ctrl;

	addr = SYNA_MEMMAP_AVIO_VPP_GBL_LCDC_CTRL;
	GA_REG_WORD32_READ(addr, &lcdc_ctrl.u32);

	if (lcdcID == SYNA_LCDC_1) {
		lcdc_ctrl.uLCDC_CTRL_lcdc1_clk_sel = 0;
		lcdc_ctrl.uLCDC_CTRL_lcdc1_ClkEn = enable;
		lcdc_ctrl.uLCDC_CTRL_lcdc1_sysClk_en = enable;

		lcdc_ctrl.uLCDC_CTRL_lcdc1_ClkSel = 2;
		lcdc_ctrl.uLCDC_CTRL_lcdc1_ClkSwitch = 1;
		lcdc_ctrl.uLCDC_CTRL_lcdc1_ClkD3Switch = 0;
	} else if (lcdcID == SYNA_LCDC_2) {
		lcdc_ctrl.uLCDC_CTRL_lcdc2_clk_sel = 1;
		lcdc_ctrl.uLCDC_CTRL_lcdc2_ClkEn = enable;
		lcdc_ctrl.uLCDC_CTRL_lcdc2_sysClk_en = enable;

		lcdc_ctrl.uLCDC_CTRL_lcdc2_ClkSel = 2;
		lcdc_ctrl.uLCDC_CTRL_lcdc2_ClkSwitch = 1;
		lcdc_ctrl.uLCDC_CTRL_lcdc2_ClkD3Switch = 0;
	}

	GA_REG_WORD32_WRITE(addr, lcdc_ctrl.u32);
}

static void syna_lcdc_wrap_vpll_enable(int lcdc_id, int enable)
{
	unsigned int addr;
	T32avioVppGbl_AVPLLA_CLK_EN avpll_clk_en;

	addr = SYNA_MEMMAP_AVIO_VPP_GBL_AVPLLA_CLK_EN;
	GA_REG_WORD32_READ(addr, &avpll_clk_en.u32);
	if (enable)
		avpll_clk_en.uAVPLLA_CLK_EN_ctrl |= (1 << lcdc_id);
	else
		avpll_clk_en.uAVPLLA_CLK_EN_ctrl &= ~(1 << lcdc_id);

	GA_REG_WORD32_WRITE(addr, avpll_clk_en.u32);
}

static void syna_lcdc_wrap_vpll_pwron(int lcdc_id, int pwron)
{
	unsigned int addr;
	T32avioVppGbl_SWPDWN_CTRL avpll_ctrl;

	addr = SYNA_MEMMAP_AVIO_VPP_GBL_SWPDOWN_CTRL;
	GA_REG_WORD32_READ(addr, &avpll_ctrl.u32);
	if (lcdc_id == SYNA_LCDC_1)
		avpll_ctrl.uSWPDWN_CTRL_VPLL0_PD = !pwron;
	else if (lcdc_id == SYNA_LCDC_2)
		avpll_ctrl.uSWPDWN_CTRL_VPLL1_PD = !pwron;

	GA_REG_WORD32_WRITE(addr, avpll_ctrl.u32);
}

static void syna_lcdc_wrap_clkgating_disable(void)
{
	unsigned int addr;
	T32avioVppGbl_CTRL gbl_ctrl;

	addr = SYNA_MEMMAP_AVIO_VPP_GBL_CTRL;
	GA_REG_WORD32_READ(addr, &gbl_ctrl.u32);

	SYNA_LCDC_AIODHUB_CG_DISABLE(gbl_ctrl);
	SYNA_LCDC_VPPDHUB_CG_DISABLE(gbl_ctrl);

	GA_REG_WORD32_WRITE(addr, gbl_ctrl.u32);
}

static void syna_lcdc_wrap_pll_clk_ctrl(int lcdcID, int enable)
{
	syna_lcdc_wrap_vpll_pwron(lcdcID, enable);
	syna_lcdc_wrap_vpll_enable(lcdcID, enable);
	syna_lcdc_wrap_lcdcclk_enable(lcdcID, enable);
}

static void syna_lcdc_dev_init_lcdc(int lcdcID, struct syna_lcdc_dev *dev)
{
	// Reset the Default for LCDC configuration
	dev->en_intr_handler = 0;
	dev->m_srcfmt = -1;

	//Set the interrupt driving the BCM Channel
	BCM_SCHED_SetMux(lcdcID, dev->intrNo);
}

static void syna_lcdc_dev_init_clk_intr(void)
{
	unsigned int addr;

	// Program the DSI Clk control
	addr = SYNA_MEMMAP_AVIO_VPP_GBL_LCDC2_CTRL;
	GA_REG_WORD32_WRITE(addr, 0x39);

	syna_lcdc_wrap_clkgating_disable();
	syna_lcdc_cfg_wrap_interrupt_enable();
}

static void syna_lcdc_set_hw_init(struct syna_lcdc_dev *dev)
{
	int dispir = 0, pancsr;
	unsigned int gpsel = 0;
	T32LCDC_CTRL5 lcdc_ctrl;
	struct syna_lcdc_panel_t *panel = dev->panel;

	lcdc_ctrl.u32 = syna_lcdc_read(dev, RA_LCDC_CTRL5);

	if (panel->intf_type & SYNA_LCDC_TYPE_DPI_MCU)
		syna_lcdc_write(dev, LCDC_REG_LCDCCR, 2); /* FIF0 Reset */

	/* clear all pending interrupts (may exist from u-boot) */
	syna_lcdc_write(dev, LCDC_REG_INTSR,syna_lcdc_read(dev, LCDC_REG_INTSR)); //clear all interrupts

	syna_lcdc_write(dev, LCDC_REG_INTER, INT_FRAME_DONE);

	if (panel->intf_type & SYNA_LCDC_TYPE_DPI_MCU) {
		dispir = 4; //CPU type LCD
		gpsel = 0;
		if ((panel->bits_per_pixel != 8) && (panel->bits_per_pixel != 9)) {
			dispir |= 0x20; //CPU type MODE16
		}

		lcdc_ctrl.uCTRL5_full_level_14_en = 0;
		if (panel->bits_per_pixel == 9) {
			lcdc_ctrl.uCTRL5_cpu_mode18 = 1; //cpu_mode18
		}
		else if (panel->bits_per_pixel == 16) {
			switch (panel->mode) {
				case SYNA_LCDC_MODE_3:
					lcdc_ctrl.uCTRL5_cpu_mux_exp_en = 1;
					lcdc_ctrl.uCTRL5_cpu_mode16as24 = 1;
					break;
				case SYNA_LCDC_MODE_2:
					lcdc_ctrl.uCTRL5_cpu_mux_exp_en = 1;
					lcdc_ctrl.uCTRL5_cpu_mode16as18 = 1;//cpu_mux_exp_en | cpu_mode16as18
					break;
				case SYNA_LCDC_MODE_1:
					lcdc_ctrl.uCTRL5_cpu_mux_exp_en = 1;
					lcdc_ctrl.uCTRL5_cpu_cmd_shift1 = 1;//cpu_mux_exp_en | cpu_cmd_shift1
					break;
				case SYNA_LCDC_MODE_0:
				default:
					lcdc_ctrl.uCTRL5_cpu_mux_exp_en = 1; //cpu_mux_exp_en
					break;
			}
		}
		else if (panel->bits_per_pixel == 18) {
			switch (panel->mode) {
				case SYNA_LCDC_MODE_1:
					lcdc_ctrl.uCTRL5_cpu_mux_exp_en = 1;
					lcdc_ctrl.uCTRL5_cpu_cmd_shift1 = 1;
					lcdc_ctrl.uCTRL5_cpu_mode18 = 1;
					break;
				case SYNA_LCDC_MODE_0:
				default:
					lcdc_ctrl.uCTRL5_cpu_mux_exp_en = 1;
					lcdc_ctrl.uCTRL5_cpu_mode18 = 1;
					break;
			}
		}
		else if (panel->bits_per_pixel == 24) {
			switch (panel->mode) {
				case SYNA_LCDC_MODE_1:
					lcdc_ctrl.uCTRL5_cpu_mux_exp_en = 1;
					lcdc_ctrl.uCTRL5_cpu_cmd_shift1 = 1;
					break;
				case SYNA_LCDC_MODE_0:
				default:
					lcdc_ctrl.uCTRL5_cpu_mux_exp_en = 1;
					lcdc_ctrl.uCTRL5_cpu_mode18 = 1;
					break;
			}
		}
	}
	else if (panel->intf_type & SYNA_LCDC_TYPE_DSI_CMD)
		dispir = LCDC_DSI_CMD_MODE;
	else {
		if (panel->bits_per_pixel == 24) {
			dispir = 1;
			gpsel = 0;
		}
		else if (panel->bits_per_pixel == 18) {
			switch (panel->mode) {
				case SYNA_LCDC_MODE_3:
					gpsel = (1 << 1); //GPMUX18B
					lcdc_ctrl.uCTRL5_cpu_mux_exp_en = 1;
					break;
				case SYNA_LCDC_MODE_2:
					gpsel = (1 << 4); //LBPPM
					break;
				case SYNA_LCDC_MODE_1:
					gpsel = (1 << 6)| (1 << 4); //ROUND666 | LBPPM
					break;
				 case SYNA_LCDC_MODE_0:
				default:
					gpsel = 0;
					break;
			}
		}
		else if (panel->bits_per_pixel == 16) {
			switch (panel->mode) {
				case SYNA_LCDC_MODE_3:
					gpsel = (1 << 2); //GPMUX16B
					lcdc_ctrl.uCTRL5_cpu_mux_exp_en = 1;
					break;
				case SYNA_LCDC_MODE_2:
					//LBPPM | ROUND565
					gpsel = (1 << 4) | (1 << 5);
					break;
				case SYNA_LCDC_MODE_1:
					gpsel = 0;
					lcdc_ctrl.uCTRL5_b16as18 = 1; //b16as18
					break;
				case SYNA_LCDC_MODE_0:
				default:
					gpsel = 0;
					//contiguous16
					lcdc_ctrl.uCTRL5_contiguous16 = 1;
					break;
			}
		}
	}
	if (panel->ext_te)
		dispir |= LCDC_WAIT_FOR_TE;

	syna_lcdc_write(dev, LCDC_REG_DISPIR, dispir);
	syna_lcdc_write(dev, LCDC_REG_GPSELR, gpsel);
	syna_lcdc_write(dev, RA_LCDC_CTRL5, lcdc_ctrl.u32);

	pancsr = (panel->iclk) | (panel->rgb_swap << 4);
	syna_lcdc_write(dev, LCDC_REG_PANCSR, pancsr);
}

static void syna_lcdc_config_tg(struct syna_lcdc_dev *dev)
{
	int  xres, yres;
	SYNA_LCDC_PANEL *panel = dev->panel;

	syna_lcdc_write(dev, LCDC_REG_LCDCCR, 0);
	syna_lcdc_write(dev, LCDC_REG_DISPCR, 0);

	xres = panel->hsync_len + panel->left_margin +
		panel->xres + panel->right_margin;
	yres = panel->vsync_len + panel->upper_margin +
		panel->yres + panel->lower_margin;

	syna_lcdc_write(dev, LCDC_REG_VSTR, panel->vsync_len - 1);
	syna_lcdc_write(dev, LCDC_REG_VFTR, panel->upper_margin);
	syna_lcdc_write(dev, LCDC_REG_VATR, panel->yres - 1 + panel->vskip);
	syna_lcdc_write(dev, LCDC_REG_VETR, panel->lower_margin);
	syna_lcdc_write(dev, LCDC_REG_HSTR, panel->hsync_len - 1);
	syna_lcdc_write(dev, LCDC_REG_HFTR, panel->left_margin - 1);
	syna_lcdc_write(dev, LCDC_REG_HADSTR, panel->xres - 1 + panel->hskip);
	syna_lcdc_write(dev, LCDC_REG_HAPWR, panel->xres - 1 + panel->hskip);
	syna_lcdc_write(dev, LCDC_REG_HETR, panel->right_margin - 1);

	syna_lcdc_write(dev, LCDC_REG_INDXSR, panel->xres - 1);
	syna_lcdc_write(dev, LCDC_REG_INDYSR, panel->yres - 1);

	/* display position */
	syna_lcdc_write(dev, LCDC_REG_DISPXSPOSR, 0);
	syna_lcdc_write(dev, LCDC_REG_DISPXEPOSR, panel->xres - 1);
	syna_lcdc_write(dev, LCDC_REG_DISPYEPOS1R, panel->yres - 1);

	/* input buffer */
	//start DMA command
	if (dev->panel->intf_type & SYNA_LCDC_TYPE_DPI_MCU) {
		syna_lcdc_write(dev, LCDC_REG_CMDFSR, LCDC_CMD_SIZE); //CMDSIZE
		syna_lcdc_write(dev, LCDC_REG_GP0A_H_HI, 0x0E);
		syna_lcdc_write(dev, LCDC_REG_GP0A_V_ST, 0x0E);
		syna_lcdc_write(dev, LCDC_REG_GP0A_H_LO, 0x06);
		syna_lcdc_write(dev, LCDC_REG_GP0B_H_HI, 0x00);
		syna_lcdc_write(dev, LCDC_REG_GP0B_V_ST, 0x00);
		syna_lcdc_write(dev, LCDC_REG_GP0B_H_LO, 0x0B);
		syna_lcdc_write(dev, LCDC_REG_GP0B_V_END, 0x0B);
		syna_lcdc_write(dev, LCDC_REG_GP0BCNTR, 0xA800);
		syna_lcdc_write(dev, LCDC_REG_GP1B_H_HI, 0x02);
		syna_lcdc_write(dev, LCDC_REG_GP1B_V_ST, 0x03);
		syna_lcdc_write(dev, LCDC_REG_GP1B_H_LO, 0x07);
		syna_lcdc_write(dev, LCDC_REG_GP1B_V_END, 0x07);
		syna_lcdc_write(dev, LCDC_REG_GP1BCNTR, 0x20A0);
		syna_lcdc_write(dev, LCDC_REG_GP2_H_HI, 0x02);
		syna_lcdc_write(dev, LCDC_REG_GP2_V_ST, 0x03);
		syna_lcdc_write(dev, LCDC_REG_GP2_H_LO, 0x08);
		syna_lcdc_write(dev, LCDC_REG_GP2_V_END, 0x07);
		syna_lcdc_write(dev, LCDC_REG_GP2CNTR, 0xA800);
		syna_lcdc_write(dev, LCDC_REG_GP3_H_HI, 0x01);
		syna_lcdc_write(dev, LCDC_REG_GP3_V_ST, 0x02);
		syna_lcdc_write(dev, LCDC_REG_GP3_H_LO, 0x09);
		syna_lcdc_write(dev, LCDC_REG_GP3_V_END, 0x08);
		syna_lcdc_write(dev, LCDC_REG_GP3CNTR, 0xA8A0);
		syna_lcdc_write(dev, LCDC_REG_CTLTR0, 0x800);
		syna_lcdc_write(dev, LCDC_REG_GP0A_V_END, 0x0B);
		syna_lcdc_write(dev, LCDC_REG_GP0ACNTR, 0xA800);
		syna_lcdc_write(dev, LCDC_REG_GP_HMAXR, 0x103);
		syna_lcdc_write(dev, LCDC_REG_GP_VMAXR, 0x9C);
	}
	syna_lcdc_dlr_handler(dev);

	syna_lcdc_hw_param_update(dev);

	syna_lcdc_write(dev, LCDC_REG_DISPCR, 1);
	syna_lcdc_write(dev, LCDC_REG_LCDCCR, 1);
}

static struct syna_lcdc_dev *syna_lcdc_create(int num, SYNA_LCDC_PANEL *panel) {

	struct syna_lcdc_dev *dev;

	if (!panel)
		return NULL;

	dev = (struct syna_lcdc_dev *)kzalloc(sizeof(struct syna_lcdc_dev), GFP_KERNEL);
	if (!dev)
		return NULL;

	dev->core_addr = SYNA_LCDC_GET_BASE_ADDRESS(num);

	//Save panel device pointer
	dev->panel = panel;
	dev->vpp_mem_list = lcdc_ctx.vpp_mem_lcdc_list;

	if (SYNA_LCDC_OK != syna_lcdc_dlr_create(dev, num)) {
		kfree (dev);
		return NULL;
	}

	return dev;
}

int syna_lcdc_pushframe(int lcdcID, void *pnew)
{
	/* push frame into plane frame queue. */
	if (frmq_push(&(syna_lcdc[lcdcID]->inputq), pnew) == 0)
		return (SYNA_LCDC_EFRAMEQFULL);

	if (!syna_lcdc[lcdcID]->en_intr_handler) {
		syna_lcdc[lcdcID]->is_first_frame = 1;
		syna_lcdc[lcdcID]->en_intr_handler = 1;
	}

	return SYNA_LCDC_OK;
}

static void syna_lcdc_set_gamma(struct syna_lcdc_dev *dev)
{
	int i;
	unsigned int val;

	/* Find non-zero gamma value in LUT. If not found, disable gamma */
	void *p_gamma = memchr_inv(dev->u8Gamma, 0, sizeof(dev->u8Gamma));
	bool b_new_gamma_en = p_gamma ? 1 : 0;

	if (b_new_gamma_en) {
		/* Each register contains two 8-bit gamma values */
		for (i = 0; i < 16; i++) {
			val = (dev->u8Gamma[i * 2 + 1] << 8) | dev->u8Gamma[i * 2];
			GA_REG_WORD32_WRITE(dev->core_addr + LCDC_REG_GC0R + (i * 4), val);
		}
		GA_REG_WORD32_WRITE(dev->core_addr + LCDC_REG_GC0R + (i * 4), dev->u8Gamma[32]);
	}

	if (dev->b_gamma_en != b_new_gamma_en) {
		dev->b_gamma_en = b_new_gamma_en;
		GA_REG_WORD32_WRITE(dev->core_addr + LCDC_REG_GCER, b_new_gamma_en);
	}
}

void syna_lcdc_hw_config(int lcdcID, SYNA_LCDC_PANEL *panelcfg)
{
	avio_fastlogo_info display_info;
	int use_vbi;

	if (!syna_lcdc[lcdcID]->isTGConfig) {
		HDL_dhub2d *vpp_dhubHandle = SYNA_LCDC_VPP_DHUB_HANDLE;
		if (NULL == vpp_dhubHandle) {
			avio_error("invalid vpp dhub Handle\n");
			return;
		}
		memcpy(syna_lcdc[lcdcID]->panel, panelcfg, sizeof(SYNA_LCDC_PANEL));

		display_info = avio_get_fastlogo_status();

		syna_lcdc[lcdcID]->dhubID = (int)(long long)&vpp_dhubHandle;
		if (!display_info.u.status)
			syna_lcdc_wrap_pll_clk_ctrl(lcdcID, 1);

		// FIX ME: Caller Should Differentiate CPU type or RGB i/f
		syna_lcdc[lcdcID]->panel->intf_type = SYNA_LCDC_TYPE_DPI_RGB;
		syna_lcdc[lcdcID]->isTGConfig = 1;
		syna_lcdc[lcdcID]->bcm_enable = 1;
		syna_lcdc[lcdcID]->bcm_autopush_en = 1;
		syna_lcdc[lcdcID]->update_flags = 0;

		use_vbi = display_info.u.status ? 1 : 0;
		if (!display_info.u.status) {
			syna_bcmbuf_flip(syna_lcdc[lcdcID]);
			syna_lcdc_set_hw_init(syna_lcdc[lcdcID]);
			syna_lcdc_config_tg(syna_lcdc[lcdcID]);
			syna_bcmbuf_submit(syna_lcdc[lcdcID], use_vbi); //use VBI
		} else {
			GA_REG_WORD32_WRITE(syna_lcdc[lcdcID]->core_addr + LCDC_REG_INTER, INT_FRAME_DONE);
		}

		syna_lcdc_semaintr_enable(syna_lcdc[lcdcID]->intrID, 1);
	}
}

void syna_lcdc_irq(int intrMask)
{
	struct syna_lcdc_dev *dev;
	int stat, i;

	for (i = 0; i < SYNA_LCDC_MAX; i++) {
		if (intrMask & ((1 << (syna_lcdc[i]->intrID)))) {
			dev = syna_lcdc[i];
			/* read & clear active interrupts */

			stat = syna_lcdc_read(dev, LCDC_REG_INTSR);
			GA_REG_WORD32_WRITE(dev->core_addr + LCDC_REG_INTSR, stat);

			if (stat & INT_FRAME_DONE || dev->is_first_frame) {
				if (dev->en_intr_handler) {
					dev->is_first_frame = 0;
					syna_bcmbuf_flip(dev);
					syna_lcdc_dlr_handler(dev);
					syna_lcdc_hw_param_update(dev); //TO handle input change
					syna_bcmbuf_submit(dev, 1);
				}
			}
		}
	}
}

static void syna_lcdc_destroy(int lcdcId)
{
	syna_lcdc_dlr_destroy(syna_lcdc[lcdcId]);
	kfree(syna_lcdc[lcdcId]->panel);
	kfree(syna_lcdc[lcdcId]);
}

static void syna_lcdc_dev_deinit(void)
{
	int i;

	for (i = 0; i < SYNA_LCDC_MAX; i++) {
		syna_lcdc_destroy(i);
	}
}

static int syna_lcdc_dev_init(void)
{
	int i;
	SYNA_LCDC_PANEL *syna_panel_config;
	unsigned int addr;
	avio_fastlogo_info display_info;

	addr = SYNA_MEMMAP_AVIO_VPP_GBL_BASE + RA_avioVppGbl_VPLL0_WRAP + RA_VPLL_WRAP_VPLL_CTRL;

	display_info = avio_get_fastlogo_status();
	if (!display_info.u.status) {
		/*FIXME: configure based on resolution configuration*/
		GA_REG_WORD32_WRITE(addr, 0x820);

		addr = SYNA_MEMMAP_AVIO_VPP_GBL_BASE + RA_avioVppGbl_VPPL1_WRAP + RA_VPLL_WRAP_VPLL_CTRL;
		GA_REG_WORD32_WRITE(addr, 0x820);

		syna_lcdc_dev_init_clk_intr();
	}

	for (i = 0; i < SYNA_LCDC_MAX; i++) {
		syna_panel_config = (SYNA_LCDC_PANEL *) kmalloc(sizeof(struct syna_lcdc_dev), GFP_KERNEL);
		if (syna_panel_config == NULL) {
			avio_error("failed to alloc panel mem lcdc\n");
			return SYNA_LCDC_EBADPARAM;
		}

		syna_lcdc[i] = syna_lcdc_create(i, syna_panel_config);
		if (syna_lcdc[i] == NULL) {
			avio_error("failed to create lcdc\n");
			if (i == 1)
				syna_lcdc_destroy(0);
			kfree (syna_panel_config);
			return SYNA_LCDC_EBADPARAM;
		}
		syna_lcdc_dev_init_lcdc(i, syna_lcdc[i]);
	}

	return SYNA_LCDC_OK;
}

static int drv_lcdc_init(void *ctxt)
{
	LCDC_CTX    *pLCDC_Ctxt = ctxt;

	avio_trace("%s:%d:\n", __func__, __LINE__);

	pLCDC_Ctxt->vpp_mem_lcdc_list = devm_kzalloc(pLCDC_Ctxt->dev, sizeof(VPP_MEM_LIST), GFP_KERNEL);
	if (!pLCDC_Ctxt->vpp_mem_lcdc_list) {
		avio_error("Mem List alloc failed\n");
		return E_OUTOFMEMORY;
	}

	pLCDC_Ctxt->vpp_mem_lcdc_list->dev = pLCDC_Ctxt->dev;
	VPP_MEM_InitMemory(pLCDC_Ctxt->vpp_mem_lcdc_list);

	return syna_lcdc_dev_init();
}

static void drv_lcdc_exit(void *ctxt)
{
	LCDC_CTX    *pLCDC_Ctxt = ctxt;

	syna_lcdc_dev_deinit();
	VPP_MEM_DeInitMemory(pLCDC_Ctxt->vpp_mem_lcdc_list);
}

static int drv_lcdc_suspend(void *ctxt)
{
	avio_trace("%s:%d:\n", __func__, __LINE__);

	syna_lcdc_suspend(1);

	return 0;
}

static int drv_lcdc_resume(void *ctxt)
{
	avio_trace("%s:%d:\n", __func__, __LINE__);

	syna_lcdc_suspend(0);

	return 0;
}

static const AVIO_MODULE_FUNC_TABLE lcdc_drv_fops = {
	.init = drv_lcdc_init,
	.exit = drv_lcdc_exit,
	.save_state = drv_lcdc_suspend,
	.restore_state = drv_lcdc_resume
};

int avio_module_drv_lcdc_probe(struct platform_device *dev)
{
	avio_trace("%s:%d:\n", __func__, __LINE__);

	lcdc_ctx.dev = &dev->dev;
	avio_sub_module_register(AVIO_MODULE_TYPE_LCDC, LCDC_MODULE_NAME,
			&lcdc_ctx, &lcdc_drv_fops);

	return 0;
}


static void syna_lcdc_TG_Reset (struct syna_lcdc_dev *dev)
{
	syna_lcdc_write(dev, LCDC_REG_LCDCCR, 0);
	syna_lcdc_write(dev, LCDC_REG_VSTR, 0);
	syna_lcdc_write(dev, LCDC_REG_VFTR, 0);
	syna_lcdc_write(dev, LCDC_REG_VATR, 0);
	syna_lcdc_write(dev, LCDC_REG_VETR, 0);
	syna_lcdc_write(dev, LCDC_REG_HSTR, 0);
	syna_lcdc_write(dev, LCDC_REG_HFTR, 0);
	syna_lcdc_write(dev, LCDC_REG_HADSTR, 0);
	syna_lcdc_write(dev, LCDC_REG_HAPWR, 0);
	syna_lcdc_write(dev, LCDC_REG_HETR, 0);

	syna_lcdc_write(dev, LCDC_REG_INDXSR, 0);
	syna_lcdc_write(dev, LCDC_REG_INDYSR, 0);
	syna_lcdc_write(dev, LCDC_REG_INTER, 0);
}

int syna_lcdc_suspend(int enable)
{
	int i;

	for (i = 0; i < SYNA_LCDC_MAX; i++) {
		if (syna_lcdc[i]->isTGConfig) {
			if (enable) {
				syna_lcdc[i]->bcm_enable = 0;
				syna_lcdc_TG_Reset(syna_lcdc[i]);
				syna_lcdc_wrap_pll_clk_ctrl(i, !enable);
			} else {
				syna_lcdc_dev_init_clk_intr();
				syna_lcdc_wrap_pll_clk_ctrl(i, !enable);
				syna_lcdc_dev_init_lcdc(i, syna_lcdc[i]);

				/* Reset the flag to enable Re-Trigger of
				 * TG config from user.
				 */
				syna_lcdc[i]->isTGConfig = 0;
			}
		}
	}

	return 0;
}

int AVIO_MEM_IsReady(void)
{
	return VPP_MEM_IsReady();
}

int syna_lcdc_update_gamma_table(int lcdcID, const void *data,
								 unsigned int length)
{
	struct syna_lcdc_dev *dev;
	uint16_t *gamma16;
	uint8_t val;
	int i;

	dev = syna_lcdc[SYNA_LCDC_GET_DEV_NDX(lcdcID)];
	if (!dev) {
		pr_err("LCDC%d not initialized\n", lcdcID);
		return -EINVAL;
	}

	if (data == NULL && length == 0) {
		memset(dev->u8Gamma, 0, sizeof(dev->u8Gamma));
		dev->update_flags |= SYNA_LCDC_GAMMA;
		goto update_gamma_table_exit;
	}

	gamma16 = (uint16_t *)data;

	if (!gamma16) {
		return -EINVAL;
	}

	/*
	 * Convert DRM gamma LUT (256 entries, 16-bit) to LCDC format (33 entries, 8-bit).
	 * DRM provides drm_color_lut with 4 u16s per entry (r, g, b, reserved).
	 * Sample every 8th entry (indices 0, 8, 16, ..., 248) for first 32 values,
	 * plus entry 255 for the last value.
	 */
	for (i = 0; i < SYNA_LCDC_GAMMA_LUT_ENTRRIES - 1; i++) {
		val = gamma16[i * 8 * 4] & 0xff;
		if (dev->u8Gamma[i] != val) {
			dev->u8Gamma[i] = val;
			dev->update_flags |= SYNA_LCDC_GAMMA;
		}
	}
	val = gamma16[255 * 4] & 0xff;
	if (dev->u8Gamma[SYNA_LCDC_GAMMA_LUT_ENTRRIES - 1] != val) {
		dev->u8Gamma[SYNA_LCDC_GAMMA_LUT_ENTRRIES - 1] = val;
		dev->update_flags |= SYNA_LCDC_GAMMA;
	}

update_gamma_table_exit:
	//TBD: move to ISR and convert Direct write to BCM write
	if (dev->update_flags & SYNA_LCDC_GAMMA) {
		dev->update_flags &= ~SYNA_LCDC_GAMMA;
		syna_lcdc_set_gamma(dev);
	}

	return 0;
}


static int syna_lcdc_set_brightness(struct syna_lcdc_dev *dev,
							 SYNA_LCDC_BRIGHT_CH reg, u8 val)
{

	GA_REG_WORD32_WRITE(dev->core_addr + LCDC_REG_RBCR + reg * 4, val & 0xff);

	return 0;
}

int syna_lcdc_update_brightness(int lcdcID, int channel, uint64_t val)
{
	struct syna_lcdc_dev *dev;
	int64_t signed_val = (int64_t)val;
	uint8_t reg_val;
	int i;

	dev = syna_lcdc[SYNA_LCDC_GET_DEV_NDX(lcdcID)];
	if (!dev) {
		pr_err("LCDC%d not initialized\n", lcdcID);
		return -EINVAL;
	}

	/* Convert from signed range [-128,127] to unsigned [0,255] */
	reg_val = signed_val + 128;

	if (channel == SYNA_LCDC_BRIGHT_CH_ALL) {
		for (i = 0; i < SYNA_LCDC_BRIGHTNESS_LUT_ENTRIES; i++) {
			syna_lcdc_set_brightness(dev, i, reg_val);
			dev->brightness[i] = val;
		}
	} else if (channel >= SYNA_LCDC_BRIGHT_CH_R && channel <= SYNA_LCDC_BRIGHT_CH_B) {
		syna_lcdc_set_brightness(dev, channel, reg_val);
		dev->brightness[channel] = val;
	} else {
		return -EINVAL;
	}

	return 0;
}

int syna_lcdc_get_brightness(int lcdcID, int channel, uint64_t *val)
{
	struct syna_lcdc_dev *dev;

	dev = syna_lcdc[SYNA_LCDC_GET_DEV_NDX(lcdcID)];
	if (!dev) {
		pr_err("LCDC%d not initialized\n", lcdcID);
		return -EINVAL;
	}

	if (channel > SYNA_LCDC_BRIGHT_CH_ALL)
		return -EINVAL;

	*val = dev->brightness[channel];

	return 0;
}
