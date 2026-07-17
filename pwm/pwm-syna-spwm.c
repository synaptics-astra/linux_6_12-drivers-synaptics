// SPDX-License-Identifier: GPL-2.0
/*
 * Synaptics SPWM driver
 *
 * Copyright (C) 2026 Synaptics Incorporated
 *
 * Author: Jisheng Zhang <jszhang@kernel.org>
 *
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/hwspinlock.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pwm.h>
#include <linux/reset.h>
#include <linux/slab.h>

#define SYNA_SPWM_HWLOCK_TIMEOUT	5000

#define SL261X_SPWM_32BIT_CNTIDX	3
#define SL261X_SPWM_CNTNUM		12
#define SL261X_SPWM_CDIVNUM		6
#define SL261X_SPWM_DSCCFGNUM		3

#define SL261X_SPWM_COUNTER_SZ		0x64
#define SL261X_SPWM_COUNTER(n)		((n) * SL261X_SPWM_COUNTER_SZ)
#define SL261X_SPWM_CLKDIV_SZ		0x8
#define SL261X_SPWM_CLKDIV(n)		(SL261X_SPWM_COUNTER(SL261X_SPWM_CNTNUM) + (n) * SL261X_SPWM_CLKDIV_SZ)
#define SL261X_SPWM_DSCCFG_SZ		0xc
#define SL261X_SPWM_DSCCFG(n)		(SL261X_SPWM_CLKDIV(SL261X_SPWM_CDIVNUM) + (n) * SL261X_SPWM_DSCCFG_SZ)
#define DIV_CMD		SL261X_SPWM_DSCCFG(SL261X_SPWM_DSCCFGNUM)
#define  DIV_CMD_DIV_SEL		GENMASK(3, 0)
#define  DIV_CMD_PA_DIV_SEL		GENMASK(11, 8)
#define  DIV_CMD_DISABLE		BIT(30)
#define  DIV_CMD_ENABLE			BIT(31)

/* COUNTER regs */
#define CLK_CTRL			0x0
#define  CLK_CTRL_CLKDIV_SEL		GENMASK(3, 0)
#define CTRL				0x4
#define  CTRL_AUTO_RELOAD_CC0		BIT(0)
#define  CTRL_AUTO_RELOAD_CC1		BIT(1)
#define  CTRL_AUTO_RELOAD_PERIOD	BIT(2)
#define  CTRL_AUTO_RELOAD_LINE_SEL	BIT(3)
#define  CTRL_CC0_MATCH_UP_EN		BIT(4)
#define  CTRL_CC0_MATCH_DOWN_EN		BIT(5)
#define  CTRL_CC1_MATCH_UP_EN		BIT(6)
#define  CTRL_CC1_MATCH_DOWN_EN		BIT(7)
#define  CTRL_PWM_IMM_KILL		BIT(8)
#define  CTRL_PWM_STOP_ON_KILL		BIT(9)
#define  CTRL_UP_DOWN_MODE		GENMASK(17, 16)
#define  CTRL_COUNT_UP			0
#define  CTRL_ONE_SHOT			BIT(18)
#define  CTRL_MODE			GENMASK(26, 24)
#define  CTRL_MODE_PWM			4
#define  CTRL_ENABLED			BIT(31)
#define STATUS				0x8
#define CC0				0xc
#define CC1				0x10
#define PERIOD				0x14
#define COUNTER				0x18
#define PWM_CTRL			0x1c
#define  PWM_CTRL_CC0_MATCH_MODE	GENMASK(1, 0)
#define  PWM_CTRL_OVERFLOW_MODE		GENMASK(3, 2)
#define  PWM_CTRL_UNDERFLOW_MODE	GENMASK(5, 4)
#define  PWM_CTRL_CC1_MATCH_MODE	GENMASK(7, 6)
#define  PWM_CTRL_MODE_SET		0
#define  PWM_CTRL_MODE_CLEAR		1
#define  PWM_CTRL_MODE_INVERT		2
#define  PWM_CTRL_MODE_NO_CHANGE	3
#define LINE_SEL			0x20
#define  LINE_SEL_PWM			0x32
#define CC0_BUF				0x24
#define CC1_BUF				0x28
#define PERIOD_BUF			0x2c
#define LINE_SEL_BUF			0x30
#define TRIN_CTRL0			0x34
#define  TRIN_CTRL0_CAPTURE0_SEL	GENMASK(7, 0)
#define  TRIN_CTRL0_COUNT_SEL		GENMASK(15, 8)
#define  TRIN_CTRL0_RELOAD_SEL		GENMASK(23, 16)
#define  TRIN_CTRL0_STOP_SEL		GENMASK(31, 24)
#define  TRIN_0				0
#define  TRIN_1				1
#define TRIN_CTRL1			0x38
#define  TRIN_CTRL1_START_SEL		GENMASK(7, 0)
#define  TRIN_CTRL1_CAPTURE1_SEL	GENMASK(15, 8)
#define  TRIN_CTRL1_CAPTURE0_INV	BIT(16)
#define  TRIN_CTRL1_COUNT_INV		BIT(17)
#define  TRIN_CTRL1_RELOAD_INV		BIT(18)
#define  TRIN_CTRL1_STOP_INV		BIT(19)
#define  TRIN_CTRL1_START_INV		BIT(20)
#define  TRIN_CTRL1_CAPTURE1_INV	BIT(21)
#define TRIN_EDGE_SEL			0x3c
#define  TRIN_EDGE_SEL_CAPTURE0_EDGE	GENMASK(1, 0)
#define  TRIN_EDGE_SEL_COUNT_EDGE	GENMASK(3, 2)
#define  TRIN_EDGE_SEL_RELOAD_EDGE	GENMASK(5, 4)
#define  TRIN_EDGE_SEL_STOP_EDGE	GENMASK(7, 6)
#define  TRIN_EDGE_SEL_START_EDGE	GENMASK(9, 8)
#define  TRIN_EDGE_SEL_CAPTURE1_EDGE	GENMASK(11, 10)
#define  RISING_EDGE			0
#define  FALLING_EDGE			1
#define  ANY_EDGE			2
#define  NO_EDGE_DET			3
#define TROUT_SEL			0x40
#define  TROUT_SEL_OUT0			GENMASK(2, 0)
#define  TROUT_SEL_OUT1			GENMASK(6, 4)
#define  TROUT_SEL_OVERFLOW		0
#define  TROUT_SEL_UNDERFLOW		1
#define  TROUT_SEL_TC			2
#define  TROUT_SEL_CC0_MATCH		3
#define  TROUT_SEL_CC1_MATCH		4
#define  TROUT_SEL_LINE_OUT		5
#define  TROUT_SEL_DISABLED		7
#define TR_CMD				0x44
#define  TR_CMD_CAPTURE0		BIT(0)
#define  TR_CMD_RELOAD			BIT(2)
#define  TR_CMD_STOP			BIT(3)
#define  TR_CMD_START			BIT(4)
#define  TR_CMD_CAPTURE1		BIT(5)
#define INTR_MASK			0x50
#define  INTR_TC			BIT(0)
#define  INTR_CC0_MATCH			BIT(1)
#define  INTR_CC1_MATCH			BIT(2)
#define DEADTIME			0x58
#define SEMA_CTRL			0x5c


/* CLKDIV regs */
#define DIVCTL				0x0
#define  DIVCTL_MSK			GENMASK(23, 0)
#define DIVSTS				0x4
#define  DIVSTS_EN			BIT(0)

struct syna_spwm_chip {
	void __iomem *base;
	void __iomem *ctrl_base;
	u32 clkdiv;
	u32 out_sels[SL261X_SPWM_CNTNUM];
	struct clk *cfg_clk;
	struct clk *peri_clk;
	struct reset_control *cfg_rst;
	struct reset_control *peri_rst;
	struct hwspinlock *hwlock;
	struct device *dev;
};

static inline u32 rdl(struct syna_spwm_chip *priv, u32 off)
{
	return readl_relaxed(priv->base + off);
}

static inline void wrl(struct syna_spwm_chip *priv, u32 off, u32 value)
{
	writel_relaxed(value, priv->base + off);
}

static inline u32 counter_rdl(struct syna_spwm_chip *priv, unsigned int num, u32 off)
{
	return readl_relaxed(priv->base + SL261X_SPWM_COUNTER(num) + off);
}

static inline void counter_wrl(struct syna_spwm_chip *priv, unsigned int num, u32 off, u32 value)
{
	writel_relaxed(value, priv->base + SL261X_SPWM_COUNTER(num) + off);
}

static inline u32 clkdiv_rdl(struct syna_spwm_chip *priv, unsigned int num, u32 off)
{
	return readl_relaxed(priv->base + SL261X_SPWM_CLKDIV(num) + off);
}

static inline void clkdiv_wrl(struct syna_spwm_chip *priv, unsigned int num, u32 off, u32 value)
{
	writel_relaxed(value, priv->base + SL261X_SPWM_CLKDIV(num) + off);
}

static int syna_spwm_apply(struct pwm_chip *chip, struct pwm_device *pwm,
			   const struct pwm_state *state)
{
	struct syna_spwm_chip *priv = pwmchip_get_drvdata(chip);
	u32 val, ctrl_val;
	u64 cycles, freq;

	if (state->polarity != PWM_POLARITY_NORMAL)
		return -EINVAL;

	if (!state->enabled) {
		if (pwm->state.enabled) {
			val = counter_rdl(priv, pwm->hwpwm, CTRL);
			val &= ~CTRL_ENABLED;
			counter_wrl(priv, pwm->hwpwm, CTRL, val);
		}
		return 0;
	}

	freq = clk_get_rate(priv->peri_clk);
	if (pwm->state.enabled) {
		if (state->period != pwm->state.period ||
		    state->duty_cycle != pwm->state.duty_cycle) {
			cycles = freq * state->period;
			do_div(cycles, NSEC_PER_SEC);
			if (cycles > U32_MAX)
				return -ERANGE;
			--cycles;
			counter_wrl(priv, pwm->hwpwm, PERIOD_BUF, cycles);

			cycles = freq * state->duty_cycle;
			do_div(cycles, NSEC_PER_SEC);
			if (cycles > U32_MAX)
				return -ERANGE;
			counter_wrl(priv, pwm->hwpwm, CC0_BUF, cycles);

			counter_wrl(priv, pwm->hwpwm, TR_CMD, TR_CMD_CAPTURE0);
			return 0;
		}
	}

	val = FIELD_PREP(CLK_CTRL_CLKDIV_SEL, priv->clkdiv);
	counter_wrl(priv, pwm->hwpwm, CLK_CTRL, val);

	ctrl_val = FIELD_PREP(CTRL_MODE, CTRL_MODE_PWM);
	counter_wrl(priv, pwm->hwpwm, CTRL, ctrl_val);

	cycles = freq * state->period;
	do_div(cycles, NSEC_PER_SEC);
	if (cycles > U32_MAX)
		return -ERANGE;
	--cycles;
	counter_wrl(priv, pwm->hwpwm, PERIOD, cycles);

	counter_wrl(priv, pwm->hwpwm, LINE_SEL, LINE_SEL_PWM);
	counter_wrl(priv, pwm->hwpwm, COUNTER, 0);

	val = FIELD_PREP(TRIN_CTRL0_CAPTURE0_SEL, TRIN_0) |
	      FIELD_PREP(TRIN_CTRL0_COUNT_SEL, TRIN_1) |
	      FIELD_PREP(TRIN_CTRL0_RELOAD_SEL, TRIN_0) |
	      FIELD_PREP(TRIN_CTRL0_STOP_SEL, TRIN_0);
	counter_wrl(priv, pwm->hwpwm, TRIN_CTRL0, val);
	val = FIELD_PREP(TRIN_CTRL1_START_SEL, TRIN_0) |
	      FIELD_PREP(TRIN_CTRL1_CAPTURE1_SEL, TRIN_0) |
	      FIELD_PREP(TRIN_CTRL1_CAPTURE0_INV, TRIN_0);
	counter_wrl(priv, pwm->hwpwm, TRIN_CTRL1, val);

	val = FIELD_PREP(TRIN_EDGE_SEL_CAPTURE0_EDGE, NO_EDGE_DET) |
	      FIELD_PREP(TRIN_EDGE_SEL_COUNT_EDGE, NO_EDGE_DET) |
	      FIELD_PREP(TRIN_EDGE_SEL_RELOAD_EDGE, NO_EDGE_DET) |
	      FIELD_PREP(TRIN_EDGE_SEL_STOP_EDGE, NO_EDGE_DET) |
	      FIELD_PREP(TRIN_EDGE_SEL_START_EDGE, NO_EDGE_DET) |
	      FIELD_PREP(TRIN_EDGE_SEL_CAPTURE1_EDGE, NO_EDGE_DET);
	counter_wrl(priv, pwm->hwpwm, TRIN_EDGE_SEL, val);

	val = FIELD_PREP(TROUT_SEL_OUT0, TROUT_SEL_OVERFLOW) |
	      FIELD_PREP(TROUT_SEL_OUT1, TROUT_SEL_OVERFLOW);
	counter_wrl(priv, pwm->hwpwm, TROUT_SEL, val);

	counter_wrl(priv, pwm->hwpwm, SEMA_CTRL, 0);
	counter_wrl(priv, pwm->hwpwm, INTR_MASK, INTR_TC | INTR_CC0_MATCH | INTR_CC1_MATCH);

	ctrl_val |= CTRL_AUTO_RELOAD_CC0 | CTRL_AUTO_RELOAD_PERIOD | CTRL_CC0_MATCH_UP_EN;
	counter_wrl(priv, pwm->hwpwm, CTRL, ctrl_val);

	cycles = freq * state->duty_cycle;
	do_div(cycles, NSEC_PER_SEC);
	if (cycles > U32_MAX)
		return -ERANGE;
	counter_wrl(priv, pwm->hwpwm, CC0, cycles);
	counter_wrl(priv, pwm->hwpwm, CC1, 0);

	val = FIELD_PREP(PWM_CTRL_CC0_MATCH_MODE, PWM_CTRL_MODE_CLEAR) |
	      FIELD_PREP(PWM_CTRL_OVERFLOW_MODE, PWM_CTRL_MODE_SET) |
	      FIELD_PREP(PWM_CTRL_UNDERFLOW_MODE, PWM_CTRL_MODE_NO_CHANGE) |
	      FIELD_PREP(PWM_CTRL_CC1_MATCH_MODE, PWM_CTRL_MODE_NO_CHANGE);
	counter_wrl(priv, pwm->hwpwm, PWM_CTRL, val);

	counter_wrl(priv, pwm->hwpwm, DEADTIME, 0);

	ctrl_val |= CTRL_ENABLED;
	counter_wrl(priv, pwm->hwpwm, CTRL, ctrl_val);

	counter_wrl(priv, pwm->hwpwm, TR_CMD, TR_CMD_START);

	return 0;
}

static int syna_spwm_get_state(struct pwm_chip *chip, struct pwm_device *pwm,
			       struct pwm_state *state)
{
	struct syna_spwm_chip *priv = pwmchip_get_drvdata(chip);
	u64 rate = clk_get_rate(priv->peri_clk);
	u32 val;

	state->polarity = PWM_POLARITY_NORMAL;

	val = counter_rdl(priv, pwm->hwpwm, PERIOD);
	if (pwm->hwpwm < SL261X_SPWM_32BIT_CNTIDX)
		val &= GENMASK(16, 0);
	++val;
	/*
	 * val 16/32 bits, multiply NSEC_PER_SEC, won't overflow.
	 */
	state->period = DIV64_U64_ROUND_UP((u64)val * NSEC_PER_SEC, rate);

	val = counter_rdl(priv, pwm->hwpwm, CTRL);
	state->enabled = !!(val & CTRL_ENABLED);

	val = counter_rdl(priv, pwm->hwpwm, CC0);
	if (pwm->hwpwm < SL261X_SPWM_32BIT_CNTIDX)
		val &= GENMASK(16, 0);
	/*
	 * val 16/32 bits, multiply NSEC_PER_SEC, won't overflow.
	 */
	state->duty_cycle = DIV64_U64_ROUND_UP((u64)val * NSEC_PER_SEC, rate);

	return 0;
}

static const struct pwm_ops syna_spwm_ops = {
	.apply = syna_spwm_apply,
	.get_state = syna_spwm_get_state,
};

static int syna_spwm_clkdiv_enable(struct syna_spwm_chip *priv)
{
	u32 val;
	int ret;

	val = clkdiv_rdl(priv, priv->clkdiv, DIVSTS);
	if (!(val & DIVSTS_EN)) {
		val = FIELD_PREP(DIVCTL_MSK, 0);
		clkdiv_wrl(priv, priv->clkdiv, DIVCTL, val);

		if (priv->hwlock) {
			ret = hwspin_lock_timeout_raw(priv->hwlock, SYNA_SPWM_HWLOCK_TIMEOUT);
			if (ret) {
				dev_err(priv->dev, "timeout to get the hwspinlock\n");
				return ret;
			}
		}

		val = FIELD_PREP(DIV_CMD_DIV_SEL, priv->clkdiv);
		val |= FIELD_PREP(DIV_CMD_PA_DIV_SEL, 0xf);
		wrl(priv, DIV_CMD, val);
		val |= DIV_CMD_ENABLE;
		wrl(priv, DIV_CMD, val);

		ret = read_poll_timeout_atomic(clkdiv_rdl, val, (val & DIVSTS_EN),
					       1, 100, false, priv, priv->clkdiv, DIVSTS);

		if (priv->hwlock)
			hwspin_unlock_raw(priv->hwlock);

		if (ret)
			return ret;
	}

	return 0;
}

static int syna_spwm_config_out_sel(struct syna_spwm_chip *priv, u32 chan_idx)
{
	u32 val;
	int ret, lsb = priv->out_sels[chan_idx] % 6 * 4;
	void __iomem *reg = priv->ctrl_base + priv->out_sels[chan_idx] / 6 * 4;

	if (priv->hwlock) {
		ret = hwspin_lock_timeout_raw(priv->hwlock, SYNA_SPWM_HWLOCK_TIMEOUT);
		if (ret) {
			dev_err(priv->dev, "timeout to get the hwspinlock\n");
			return ret;
		}
	}

	val = readl_relaxed(reg);
	val &= ~(0xff << lsb);
	val |= (chan_idx << lsb);
	writel_relaxed(val, reg);

	if (priv->hwlock)
		hwspin_unlock_raw(priv->hwlock);

	return 0;
}

static int syna_spwm_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct syna_spwm_chip *priv;
	struct device_node *child;
	struct pwm_chip *chip;
	u32 chan_idx, out_select;
	struct resource *res;
	int ret;

	chip = devm_pwmchip_alloc(&pdev->dev, SL261X_SPWM_CNTNUM, sizeof(*priv));
	if (IS_ERR(chip))
		return PTR_ERR(chip);
	priv = pwmchip_get_drvdata(chip);
	priv->dev = &pdev->dev;
	platform_set_drvdata(pdev, priv);

	/* hwspinlock is optional */
	ret = of_hwspin_lock_get_id(np, 0);
	if (ret < 0) {
		if (ret == -EPROBE_DEFER)
			return ret;
	} else {
		priv->hwlock = devm_hwspin_lock_request_specific(&pdev->dev, ret);
	}

	priv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!res)
		return dev_err_probe(&pdev->dev, -ENODEV, "no spwm ctrl resource\n");
	priv->ctrl_base = devm_ioremap(&pdev->dev, res->start, resource_size(res));
	if (!priv->ctrl_base)
		return -ENOMEM;

	priv->cfg_rst = devm_reset_control_get(&pdev->dev, "cfg");
	if (IS_ERR(priv->cfg_rst))
		return dev_err_probe(&pdev->dev, PTR_ERR(priv->cfg_rst),
				     "failed to get sys reset\n");
	reset_control_deassert(priv->cfg_rst);

	priv->peri_rst = devm_reset_control_get(&pdev->dev, "peri");
	if (IS_ERR(priv->peri_rst))
		return dev_err_probe(&pdev->dev, PTR_ERR(priv->peri_rst),
				     "failed to get peri reset\n");
	reset_control_deassert(priv->peri_rst);

	priv->cfg_clk = devm_clk_get_enabled(&pdev->dev, "cfg");
	if (IS_ERR(priv->cfg_clk))
		return dev_err_probe(&pdev->dev, PTR_ERR(priv->cfg_clk),
				     "failed to get cfg clock\n");

	priv->peri_clk = devm_clk_get_enabled(&pdev->dev, "peri");
	if (IS_ERR(priv->peri_clk))
		return dev_err_probe(&pdev->dev, PTR_ERR(priv->peri_clk),
				     "failed to get peri clock\n");

	ret = of_property_read_u32(np, "syna,clkdiv-id", &priv->clkdiv);
	if (ret < 0)
		return dev_err_probe(&pdev->dev, ret, "missing clock divider id\n");
	if (priv->clkdiv >= SL261X_SPWM_CDIVNUM)
		return dev_err_probe(&pdev->dev, -EINVAL, "invalid clock divider id\n");

	ret = syna_spwm_clkdiv_enable(priv);
	if (ret)
		return ret;

	for_each_child_of_node(np, child) {
		if (of_property_read_u32(child, "reg", &chan_idx))
			continue;

		if (chan_idx >= SL261X_SPWM_CNTNUM) {
			dev_err(&pdev->dev, "Invalid channel index %d\n", chan_idx);
			of_node_put(child);
			return -EINVAL;
		}

		if (!of_property_read_u32(child, "syna,output-select", &out_select)) {
			priv->out_sels[chan_idx] = out_select;
			ret = syna_spwm_config_out_sel(priv, chan_idx);
			if (ret)
				return ret;
		}
	}

	chip->ops = &syna_spwm_ops;

	return devm_pwmchip_add(&pdev->dev, chip);
}

static const struct of_device_id syna_spwm_dt_ids[] = {
	{.compatible = "syna,sl261x-spwm",},
	{/* sentinel */}
};
MODULE_DEVICE_TABLE(of, syna_spwm_dt_ids);

static struct platform_driver syna_spwm_driver = {
	.driver = {
		.name = "syna-spwm",
		.of_match_table = syna_spwm_dt_ids,
	},
	.probe = syna_spwm_probe,
};
module_platform_driver(syna_spwm_driver);

MODULE_AUTHOR("Jisheng Zhang <jszhang@kernel.org>");
MODULE_DESCRIPTION("Synaptics SPWM driver");
MODULE_LICENSE("GPL v2");
