// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Synaptics Incorporated
 *
 * Author: Jisheng Zhang <jszhang@kernel.org>
 *
 */

#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/version.h>

#include "sl-sysmgr-clk.h"

#define CLK_EN		BIT(0)
#define DIV_CHNG_EN	BIT(1)

struct sl_sysmgr_gateclk {
	struct clk_hw hw;
	void __iomem *base;
	u8 bit_idx;
};

struct sl_sysmgr_clk {
	struct clk_hw hw;
	void __iomem *base;
	u8 div_bits;
};

struct sl_sysmgr_clk_priv {
	struct sl_sysmgr_gateclk *gclks;
	struct sl_sysmgr_clk *clks;
	u32 gclk_n;
	u32 clk_n;
};

static DEFINE_SPINLOCK(sl_sysmgr_gateclk_lock);

#define to_sl_sysmgr_gateclk(hw)	container_of(hw, struct sl_sysmgr_gateclk, hw)
#define to_sl_sysmgr_clk(hw)		container_of(hw, struct sl_sysmgr_clk, hw)

static int sl_sysmgr_gateclk_enable(struct clk_hw *hw)
{
	u32 val;
	struct sl_sysmgr_gateclk *clk = to_sl_sysmgr_gateclk(hw);

	guard(spinlock)(&sl_sysmgr_gateclk_lock);

	val = readl_relaxed(clk->base);
	if (!(val & BIT(clk->bit_idx))) {
		val |= BIT(clk->bit_idx);
		writel_relaxed(val, clk->base);
	}

	return 0;
}

static int sl_sysmgr_gateclk_is_enabled(struct clk_hw *hw)
{
	u32 val;
	struct sl_sysmgr_gateclk *clk = to_sl_sysmgr_gateclk(hw);

	guard(spinlock)(&sl_sysmgr_gateclk_lock);

	val = readl_relaxed(clk->base);
	val &= BIT(clk->bit_idx);

	return val ? 1 : 0;
}

static const struct clk_ops sl_sysmgr_gateclk_ops = {
	.enable		= sl_sysmgr_gateclk_enable,
	.is_enabled	= sl_sysmgr_gateclk_is_enabled,
};

static int
sl_sysmgr_gateclk_register(struct device *dev, struct sl_sysmgr_gateclk *gclk,
			   const char *name, const char *parent_names,
			   u8 bit_idx, void __iomem *base)
{
	struct clk_init_data init;

	init.name = name;
	init.ops = &sl_sysmgr_gateclk_ops;
	init.parent_names = &parent_names;
	init.num_parents = 1;
	init.flags = 0;

	gclk->bit_idx = bit_idx;
	gclk->base = base;
	gclk->hw.init = &init;

	return devm_clk_hw_register(dev, &gclk->hw);
}

static unsigned long
sl_sysmgr_clk_recalc_rate(struct clk_hw *hw, unsigned long parent_rate)
{
	u32 val, div;
	struct sl_sysmgr_clk *clk = to_sl_sysmgr_clk(hw);

	val = readl_relaxed(clk->base);
	div = (val >> 2) & ((1 << clk->div_bits) - 1);
	++div;

	return parent_rate / div;
}

static int sl_sysmgr_clk_enable(struct clk_hw *hw)
{
	u32 val;
	struct sl_sysmgr_clk *clk = to_sl_sysmgr_clk(hw);

	val = readl_relaxed(clk->base);
	if (!(val & CLK_EN)) {
		val |= CLK_EN;
		writel_relaxed(val, clk->base);
	}

	return 0;
}

static int sl_sysmgr_clk_is_enabled(struct clk_hw *hw)
{
	u32 val;
	struct sl_sysmgr_clk *clk = to_sl_sysmgr_clk(hw);

	val = readl_relaxed(clk->base);
	val &= CLK_EN;

	return val ? 1 : 0;
}

static const struct clk_ops sl_sysmgr_clk_ops = {
	.recalc_rate	= sl_sysmgr_clk_recalc_rate,
	.enable		= sl_sysmgr_clk_enable,
	.is_enabled	= sl_sysmgr_clk_is_enabled,
};

static int
sl_sysmgr_clk_register(struct device *dev, struct sl_sysmgr_clk *sclk,
		       const char *name, const char *parent_names,
		       u8 div_bits, void __iomem *base)
{
	struct clk_init_data init;

	init.name = name;
	init.ops = &sl_sysmgr_clk_ops;
	init.parent_names = &parent_names;
	init.num_parents = 1;
	init.flags = 0;

	sclk->div_bits = div_bits;
	sclk->base = base;
	sclk->hw.init = &init;

	return devm_clk_hw_register(dev, &sclk->hw);
}

int sl_sysmgr_clk_setup(struct platform_device *pdev,
			const struct sl_sysmgr_gateclk_desc *gdescs,
			int gclk_n,
			u32 gclk_offset,
			const struct sl_sysmgr_clk_desc *descs,
			int clk_n)
{
	int i, ret;
	void __iomem *base;
	struct clk_hw_onecell_data *clk_data;
	struct sl_sysmgr_clk_priv *priv;
	struct resource *res;

	clk_data = devm_kzalloc(&pdev->dev, struct_size(clk_data, hws, gclk_n + clk_n), GFP_KERNEL);
	if (!clk_data)
		return -ENOMEM;
	clk_data->num = gclk_n + clk_n;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->gclks = devm_kcalloc(&pdev->dev, gclk_n, sizeof(*priv->gclks), GFP_KERNEL);
	if (!priv->gclks)
		return -ENOMEM;

	priv->clks = devm_kcalloc(&pdev->dev, clk_n, sizeof(*priv->clks), GFP_KERNEL);
	if (!priv->clks)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -EINVAL;

	base = devm_ioremap(&pdev->dev, res->start, resource_size(res));
	if (WARN_ON(!base))
		return -ENOMEM;

	for (i = 0; i < gclk_n; i++) {
		ret = sl_sysmgr_gateclk_register(&pdev->dev, &priv->gclks[i],
						 gdescs[i].name, gdescs[i].parent_name,
						 gdescs[i].bit_idx, base + gclk_offset);
		if (ret)
			return ret;

		clk_data->hws[i] = &priv->gclks[i].hw;
	}
	priv->gclk_n = gclk_n;

	for (i = 0; i < clk_n; i++) {
		ret = sl_sysmgr_clk_register(&pdev->dev, &priv->clks[i],
					     descs[i].name, descs[i].parent_name,
					     descs[i].div_bits, base + descs[i].offset);
		if (ret)
			return ret;

		clk_data->hws[i + gclk_n] = &priv->clks[i].hw;
	}
	priv->clk_n = clk_n;

	platform_set_drvdata(pdev, priv);

	return devm_of_clk_add_hw_provider(&pdev->dev, of_clk_hw_onecell_get, clk_data);
}
EXPORT_SYMBOL_GPL(sl_sysmgr_clk_setup);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Jisheng Zhang <jszhang@kernel.org>");
MODULE_DESCRIPTION("SysMgr domain clk driver for Synaptics SL SoCs");
