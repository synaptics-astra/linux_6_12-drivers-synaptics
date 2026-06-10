// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Synaptics Incorporated
 *
 * Author: Jisheng Zhang <jszhang@kernel.org>
 *
 */

#ifndef __SL_SYSMGR_CLK_H
#define __SL_SYSMGR_CLK_H

struct sl_sysmgr_clk_desc {
	const char	*name;
	const char	*parent_name;
	u32		offset;
	u8		div_bits;
};

struct sl_sysmgr_gateclk_desc {
	const char	*name;
	const char	*parent_name;
	u8		bit_idx;
};

int sl_sysmgr_clk_setup(struct platform_device *pdev,
			const struct sl_sysmgr_gateclk_desc *gdesc,
			int gate_clk_n,
			u32 gate_clk_offset,
			const struct sl_sysmgr_clk_desc *descs,
			int clk_n);

#endif
