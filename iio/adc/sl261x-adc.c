// SPDX-License-Identifier: GPL-2.0
/*
 * Synaptics SL261X ADC driver
 *
 * Copyright (C) 2026 Synaptics Incorporated
 *
 * Jisheng Zhang <jszhang@kernel.org>
 */

#include <linux/delay.h>
#include <linux/hwspinlock.h>
#include <linux/iio/iio.h>
#include <linux/iio/driver.h>
#include <linux/iio/machine.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/reset.h>
#include <linux/slab.h>
#include <linux/units.h>

#define CTRL				0x0
#define  CTRL_PD			BIT(0)
#define  CTRL_RESETN			BIT(1)
#define  CTRL_SEL			GENMASK(4, 2)
#define  CTRL_CONV_MODE			GENMASK(6, 5)
#define  CTRL_CONV_21CYCLE		0x0
#define  CTRL_CONV_20CYCLE		0x1
#define  CTRL_CONV_17CYCLE		0x2
#define  CTRL_SEL_EN			BIT(7)
#define  CTRL_EN			BIT(8)
#define  CTRL_START_CONV		BIT(9)
#define  CTRL_ADC_CONV_TYPE		GENMASK(11, 10)
#define  CTRL_ADC_CONV_SINGLE		0
#define  CTRL_ADC_CONV_CONTI		1
#define  CTRL_ADC_CONV_TRIG		2
#define  CTRL_BGCLK_DIV			GENMASK(15, 12)
#define  CTRL_CONT_CONV_INTV		GENMASK(23, 16)
#define  CTRL_CON_DATA_LT		GENMASK(29, 24)
#define CTRL1				0x4
#define  CTRL1_CHOPPER_EN		BIT(0)
#define  CTRL1_BANDGAP_VOL_BUF_EN	BIT(1)
#define  CTRL1_REF_VOL_BUF_EN		BIT(2)
#define  CTRL1_BYPASS_FILTER		BIT(3)
#define  CTRL1_BIAS			GENMASK(6, 4)
#define  CTRL1_BIAS_10UA		0x0
#define  CTRL1_BIAS_8P9UA		0x1
#define  CTRL1_BIAS_8UA			0x2
#define  CTRL1_BIAS_7P3UA		0x3
#define  CTRL1_BIAS_20UA		0x4
#define  CTRL1_BIAS_16UA		0x5
#define  CTRL1_BIAS_13P3UA		0x6
#define  CTRL1_BIAS_11P4UA		0x7
#define  CTRL1_VOL_FROM_BANDGAP		BIT(7)
#define DATA_ULIMIT			0x8
#define DATA_LLIMIT			0xc
#define STATUS				0x10
#define  CONV_DONE			BIT(0)
#define  ULIMIT_FAIL			BIT(1)
#define  LLIMIT_FAIL			BIT(2)
#define DATA				0x18

#define SL261X_ADC_HWLOCK_TIMEOUT	5000

struct sl261x_adc_priv {
	void __iomem		*base;
	u32			vref_mv;
	struct mutex		lock;
	struct hwspinlock	*hwlock;
	struct device		*dev;
	struct reset_control	*crstn;
	struct reset_control	*prstn;
};

#define ADC_CHANNEL(n, t)						\
	{								\
		.channel		= n,				\
		.datasheet_name		= "channel"#n,			\
		.type			= t,				\
		.indexed		= 1,				\
		.info_mask_separate	= BIT(IIO_CHAN_INFO_RAW),	\
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),	\
	}

static const struct iio_chan_spec sl261x_adc_channels[] = {
	ADC_CHANNEL(0, IIO_VOLTAGE),
	ADC_CHANNEL(1, IIO_VOLTAGE),
	ADC_CHANNEL(2, IIO_VOLTAGE),
	ADC_CHANNEL(3, IIO_VOLTAGE),
	ADC_CHANNEL(4, IIO_VOLTAGE),
	ADC_CHANNEL(5, IIO_VOLTAGE),
	ADC_CHANNEL(7, IIO_VOLTAGE),
	IIO_CHAN_SOFT_TIMESTAMP(8),
};

static inline u32 rdl(struct sl261x_adc_priv *priv, u32 offset)
{
	return readl_relaxed(priv->base + offset);
}

static inline void wrl(struct sl261x_adc_priv *priv, u32 offset, u32 data)
{
	writel_relaxed(data, priv->base + offset);
}

static int sl261x_adc_read(struct iio_dev *indio_dev, int channel)
{
	u32 val;
	int ret;
	struct sl261x_adc_priv *priv = iio_priv(indio_dev);

	guard(mutex)(&priv->lock);

	if (priv->hwlock) {
		ret = hwspin_lock_timeout_raw(priv->hwlock, SL261X_ADC_HWLOCK_TIMEOUT);
		if (ret) {
			dev_err(priv->dev, "timeout to get the hwspinlock\n");
			return ret;
		}
	}

	val = rdl(priv, CTRL);
	val &= ~CTRL_SEL;
	val |= FIELD_PREP(CTRL_SEL, channel);
	wrl(priv, CTRL, val);

	val = rdl(priv, CTRL);
	val &= ~CTRL_ADC_CONV_TYPE;
	val |= FIELD_PREP(CTRL_ADC_CONV_TYPE, CTRL_ADC_CONV_SINGLE);
	val |= CTRL_START_CONV;
	wrl(priv, CTRL, val);
	udelay(1);

	ret = read_poll_timeout_atomic(rdl, val, (val & CONV_DONE),
				       1, 100, false, priv, STATUS);
	if (ret)
		goto timeout;

	if (val & (ULIMIT_FAIL | LLIMIT_FAIL)) {
		ret = -EIO;
		goto timeout;
	}

	val = rdl(priv, DATA);
	ret = val & 0xfff;

	val = rdl(priv, STATUS);
	val |= CONV_DONE;
	wrl(priv, STATUS, val);

	val = rdl(priv, CTRL);
	val &= ~CTRL_START_CONV;
	wrl(priv, CTRL, val);

timeout:
	if (priv->hwlock)
		hwspin_unlock_raw(priv->hwlock);

	return ret;
}

static int sl261x_adc_read_raw(struct iio_dev *indio_dev,
				struct iio_chan_spec const *chan, int *val,
				int *val2, long mask)
{
	int tmp;
	struct sl261x_adc_priv *priv = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		if (chan->type != IIO_VOLTAGE)
			return -EINVAL;

		tmp = sl261x_adc_read(indio_dev, chan->channel);
		if (tmp < 0)
			return tmp;

		*val = tmp;
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*val = priv->vref_mv;
		*val2 = 12;
		return IIO_VAL_FRACTIONAL_LOG2;
	default:
		break;
	}

	return -EINVAL;
}

static const struct iio_info sl261x_adc_info = {
	.read_raw	= sl261x_adc_read_raw,
};

static void sl261x_adc_init(struct sl261x_adc_priv *priv)
{
	u32 val;

	reset_control_deassert(priv->crstn);
	reset_control_deassert(priv->prstn);

	val = rdl(priv, CTRL);
	if (val & CTRL_EN)
		return;

	/* release reset */
	val |= CTRL_RESETN;
	wrl(priv, CTRL, val);
	udelay(1);

	/* release power down */
	val = rdl(priv, CTRL);
	val &= ~CTRL_PD;
	wrl(priv, CTRL, val);

	/* set lower and upper limit */
	wrl(priv, DATA_ULIMIT, 0xfff);
	wrl(priv, DATA_LLIMIT, 0);

	/*
	 * set conv mode, BG clock div, continuous conversion interval
	 * and data latch delay
	 */
	val = rdl(priv, CTRL);
	val &= ~CTRL_CONV_MODE;
	val |= FIELD_PREP(CTRL_CONV_MODE, CTRL_CONV_21CYCLE);
	val &= ~CTRL_BGCLK_DIV;
	val |= FIELD_PREP(CTRL_BGCLK_DIV, 3);
	val &= ~CTRL_CONT_CONV_INTV;
	val |= FIELD_PREP(CTRL_CONT_CONV_INTV, 64);
	val &= ~CTRL_CON_DATA_LT;
	val |= FIELD_PREP(CTRL_CON_DATA_LT, 21);
	wrl(priv, CTRL, val);

	/* enable input selection */
	val = rdl(priv, CTRL);
	val |= CTRL_SEL_EN;
	wrl(priv, CTRL, val);
	udelay(1);

	/* enable adc */
	val = rdl(priv, CTRL);
	val |= CTRL_EN;
	wrl(priv, CTRL, val);

	/* Toggle BIT7 for for 1.8V mode */
	val = rdl(priv, CTRL1);
	val &= ~CTRL1_VOL_FROM_BANDGAP;
	wrl(priv, CTRL1, val);
	val |= CTRL1_VOL_FROM_BANDGAP;
	wrl(priv, CTRL1, val);

	val = FIELD_PREP(CTRL1_BIAS, CTRL1_BIAS_7P3UA);
	wrl(priv, CTRL1, val);
}

static int sl261x_adc_probe(struct platform_device *pdev)
{
	int ret;
	struct sl261x_adc_priv *priv;
	struct iio_dev *indio_dev;
	struct resource *res;
	struct device *dev = &pdev->dev;
	struct device_node *np = pdev->dev.of_node;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*priv));
	if (!indio_dev)
		return -ENOMEM;

	priv = iio_priv(indio_dev);
	priv->dev = dev;
	platform_set_drvdata(pdev, indio_dev);

	ret = devm_regulator_get_enable_read_voltage(dev, "vref");
	if (ret < 0)
		return ret;
	priv->vref_mv = ret / MILLI;

	/* hwspinlock is optional */
	ret = of_hwspin_lock_get_id(np, 0);
	if (ret < 0) {
		if (ret == -EPROBE_DEFER)
			return ret;
	} else {
		priv->hwlock = devm_hwspin_lock_request_specific(dev, ret);
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	priv->crstn = devm_reset_control_get_optional(dev, "crstn");
	if (IS_ERR(priv->crstn)) {
		dev_err(dev, "failed to get crstn\n");
		return PTR_ERR(priv->crstn);
	}

	priv->prstn = devm_reset_control_get_optional(dev, "prstn");
	if (IS_ERR(priv->prstn)) {
		dev_err(dev, "failed to get prstn\n");
		return PTR_ERR(priv->prstn);
	}

	sl261x_adc_init(priv);

	mutex_init(&priv->lock);

	indio_dev->dev.parent = dev;
	indio_dev->name = dev_name(dev);
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &sl261x_adc_info;

	indio_dev->channels = sl261x_adc_channels;
	indio_dev->num_channels = ARRAY_SIZE(sl261x_adc_channels);

	return devm_iio_device_register(dev, indio_dev);
}

static int sl261x_adc_resume(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct sl261x_adc_priv *priv = iio_priv(indio_dev);

	sl261x_adc_init(priv);
	return 0;
}

static const struct dev_pm_ops sl261x_adc_pm_ops = {
	SYSTEM_SLEEP_PM_OPS(NULL, sl261x_adc_resume)
};

static const struct of_device_id sl261x_adc_match[] = {
	{ .compatible = "syna,sl261x-adc", },
	{ },
};
MODULE_DEVICE_TABLE(of, sl261x_adc_match);

static struct platform_driver sl261x_adc_driver = {
	.driver	= {
		.name		= "sl261x-adc",
		.of_match_table	= sl261x_adc_match,
		.pm		= pm_ptr(&sl261x_adc_pm_ops),
	},
	.probe	= sl261x_adc_probe,
};
module_platform_driver(sl261x_adc_driver);

MODULE_AUTHOR("Jisheng Zhang <jszhang@kernel.org>");
MODULE_DESCRIPTION("Synaptics SL261X ADC driver");
MODULE_LICENSE("GPL v2");
