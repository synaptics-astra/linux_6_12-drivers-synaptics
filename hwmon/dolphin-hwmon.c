// SPDX-License-Identifier: GPL-2.0
/*
 * Synaptics DOLPHIN SoC Hardware Monitoring Driver
 *
 * Copyright (C) 2020 Synaptics Incorporated
 * Author: Jisheng Zhang <jszhang@kernel.org>
 */

#include <linux/bitops.h>
#include <linux/hwmon.h>
#include <linux/hwspinlock.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#define CTRL		0x0
#define  ENA		BIT(0)
#define  CLK_EN		BIT(1)
#define  PSAMPLE_SFT	2
#define  PSAMPLE_MSK	0x3
#define  VSAMPLE	BIT(4)
#define  TRIMG_SFT	5
#define  TRIMG_MSK	0x1f
#define  TRIMO_SFT	10
#define  TRIMO_MSK	0x3f
#define  DAT_LT_SFT	16
#define  DAT_LT_MSK	0x1f
#define STATUS		0x4
#define  DATA_RDY	BIT(0)
#define  INT_EN		BIT(1)
#define DATA		0x8
#define CHK_CTRL	0xc
#define DATA_STATUS	0x10

#define MYNA_CTRL	0x0
#define MYNA_STATUS	0x8
#define MYNA_DATA	0xc

#define MYNA2_CTRL	0x0
#define MYNA2_STATUS	0x8
#define MYNA2_DATA	0xc

#define DOLPHIN_HWMON_HWLOCK_TIMEOUT	5000

struct dolphin_hwmon_cfg {
	u8 ctrl;
	u8 status;
	u8 data;
};

struct dolphin_hwmon {
	void __iomem *base;
	const struct dolphin_hwmon_cfg *cfg;
	int irq;
	struct completion read_completion;
	struct mutex lock;
	struct hwspinlock *hwlock;
};

static irqreturn_t dolphin_hwmon_irq(int irq, void *data)
{
	u32 val;
	struct dolphin_hwmon *hwmon = data;
	const struct dolphin_hwmon_cfg *cfg = hwmon->cfg;

	val = readl_relaxed(hwmon->base + cfg->status);
	val &= ~INT_EN;
	writel_relaxed(val, hwmon->base + cfg->status);
	complete(&hwmon->read_completion);
	return IRQ_HANDLED;
}

static int dolphin_read_temp(struct dolphin_hwmon *hwmon, long *temp)
{
	u32 val;
	long t;
	int ret = 0;
	struct completion *completion = &hwmon->read_completion;
	const struct dolphin_hwmon_cfg *cfg = hwmon->cfg;

	mutex_lock(&hwmon->lock);

	if (hwmon->hwlock) {
		ret = hwspin_lock_timeout_raw(hwmon->hwlock, DOLPHIN_HWMON_HWLOCK_TIMEOUT);
		if (ret)
			goto unlock_mutex;
	} else {
		reinit_completion(completion);
		val = readl_relaxed(hwmon->base + cfg->status);
		val |= INT_EN;
		writel_relaxed(val, hwmon->base + cfg->status);
	}

	val = readl_relaxed(hwmon->base + cfg->ctrl);
	val |= ENA;
	writel_relaxed(val, hwmon->base + cfg->ctrl);
	val = readl_relaxed(hwmon->base + cfg->ctrl);
	val |= CLK_EN;
	writel_relaxed(val, hwmon->base + cfg->ctrl);

	if (hwmon->hwlock) {
		ret = read_poll_timeout(readl_relaxed, val, (val & DATA_RDY),
					320, 200000, false, hwmon->base + cfg->status);
		if (ret)
			goto unlock_hwspin;
	} else {
		t = wait_for_completion_interruptible_timeout(completion, HZ);
		if (t <= 0) {
			ret = t ? t : -ETIMEDOUT;
			goto unlock_mutex;
		}
	}

	val = readl_relaxed(hwmon->base + cfg->data);
	t = 18439 * val;
	t /= 1000;
	t = 80705 - t;
	t *= val;
	t /= 1000;
	t = 185010 - t;
	t *= val;
	t /= 1000;
	t = 328430 - t;
	t *= val;
	t /= 1000;
	t -= 48690;
	*temp = t;

	val = readl_relaxed(hwmon->base + cfg->status);
	val &= ~DATA_RDY;
	writel_relaxed(val, hwmon->base + cfg->status);

	val = readl_relaxed(hwmon->base + cfg->ctrl);
	val &= ~ENA;
	writel_relaxed(val, hwmon->base + cfg->ctrl);
	val = readl_relaxed(hwmon->base + cfg->ctrl);
	val &= ~CLK_EN;
	writel_relaxed(val, hwmon->base + cfg->ctrl);

unlock_hwspin:
	if (hwmon->hwlock)
		hwspin_unlock_raw(hwmon->hwlock);

unlock_mutex:
	mutex_unlock(&hwmon->lock);

	return ret;
}

static int dolphin_hwmon_read(struct device *dev, enum hwmon_sensor_types type,
			    u32 attr, int channel, long *temp)
{
	struct dolphin_hwmon *hwmon = dev_get_drvdata(dev);

	switch (attr) {
	case hwmon_temp_input:
		return dolphin_read_temp(hwmon, temp);
	default:
		return -EOPNOTSUPP;
	}
}

static umode_t
dolphin_hwmon_is_visible(const void *data, enum hwmon_sensor_types type,
		       u32 attr, int channel)
{
	if (type != hwmon_temp)
		return 0;

	switch (attr) {
	case hwmon_temp_input:
		return 0444;
	default:
		return 0;
	}
}

static const struct hwmon_channel_info *dolphin_hwmon_info[] = {
	HWMON_CHANNEL_INFO(chip,
			HWMON_C_REGISTER_TZ),
	HWMON_CHANNEL_INFO(temp,
			HWMON_T_INPUT),
	NULL
};

static const struct hwmon_ops dolphin_hwmon_ops = {
	.is_visible = dolphin_hwmon_is_visible,
	.read = dolphin_hwmon_read,
};

static const struct hwmon_chip_info dolphin_chip_info = {
	.ops = &dolphin_hwmon_ops,
	.info = dolphin_hwmon_info,
};

static int dolphin_hwmon_probe(struct platform_device *pdev)
{
	int ret;
	u32 val;
	struct resource *res;
	struct device *hwmon_dev;
	struct dolphin_hwmon *hwmon;
	struct device *dev = &pdev->dev;

	hwmon = devm_kzalloc(dev, sizeof(*hwmon), GFP_KERNEL);
	if (!hwmon)
		return -ENOMEM;

	hwmon->cfg = of_device_get_match_data(dev);
	if (!hwmon->cfg)
		return -EINVAL;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	hwmon->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(hwmon->base))
		return PTR_ERR(hwmon->base);

	/* hwspinlock is optional */
	ret = of_hwspin_lock_get_id(pdev->dev.of_node, 0);
	if (ret < 0) {
		if (ret == -EPROBE_DEFER)
			return ret;
	} else {
		hwmon->hwlock = devm_hwspin_lock_request_specific(dev, ret);
	}

	hwmon->irq = platform_get_irq(pdev, 0);
	if (hwmon->irq < 0) {
		dev_err(&pdev->dev, "Failed to get platform irq: %d\n",
			hwmon->irq);
		return hwmon->irq;
	}

	init_completion(&hwmon->read_completion);
	mutex_init(&hwmon->lock);

	val = readl_relaxed(hwmon->base + hwmon->cfg->status);
	val &= ~INT_EN;
	writel_relaxed(val, hwmon->base + hwmon->cfg->status);

	ret = devm_request_irq(dev, hwmon->irq, dolphin_hwmon_irq, 0,
			       pdev->name, hwmon);
	if (ret) {
		dev_err(dev, "Failed to request irq: %d\n", ret);
		return ret;
	}

	hwmon_dev = devm_hwmon_device_register_with_info(dev,
							 "dolphin",
							 hwmon,
							 &dolphin_chip_info,
							 NULL);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct dolphin_hwmon_cfg dolphin_cfg = {
	.ctrl = CTRL,
	.status = STATUS,
	.data = DATA,
};

static const struct dolphin_hwmon_cfg myna_cfg = {
	.ctrl = MYNA_CTRL,
	.status = MYNA_STATUS,
	.data = MYNA_DATA,
};

static const struct dolphin_hwmon_cfg myna2_cfg = {
	.ctrl = MYNA2_CTRL,
	.status = MYNA2_STATUS,
	.data = MYNA2_DATA,
};

static const struct of_device_id dolphin_hwmon_match[] = {
	{
		.compatible = "syna,dolphin-hwmon",
		.data = &dolphin_cfg
	},
	{
		.compatible = "syna,myna-hwmon",
		.data = &myna_cfg
	},
	{
		.compatible = "syna,myna2-hwmon",
		.data = &myna2_cfg
	},
	{},
};
MODULE_DEVICE_TABLE(of, dolphin_hwmon_match);

static struct platform_driver dolphin_hwmon_driver = {
	.probe = dolphin_hwmon_probe,
	.driver = {
		.name = "dolphin-hwmon",
		.of_match_table = dolphin_hwmon_match,
	},
};
module_platform_driver(dolphin_hwmon_driver);

MODULE_AUTHOR("Jisheng Zhang<jszhang@kernel.org>");
MODULE_DESCRIPTION("Synaptics DOLPHIN SoC hardware monitor");
MODULE_LICENSE("GPL v2");
