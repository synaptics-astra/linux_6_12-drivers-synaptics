// SPDX-License-Identifier: GPL-2.0
/*
 * Synaptics devfreq driver
 *
 * Copyright (C) 2024 Synaptics Incorporated
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/regulator/consumer.h>
#include <linux/pm_opp.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/devfreq.h>
#include <linux/devfreq-event.h>
#include <linux/of.h>
#include <linux/version.h>

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 11, 0))
#define RET void
#define RETURN
#else
#define RET int
#define RETURN return 0
#endif

#define max_clk_num	10

struct devm {
	struct device *dev;
	struct clk *dvfs_clk[max_clk_num], *clk_high, *clk_low;
	struct regulator *regulator_vcore;
	struct devfreq *devfreq;
	struct devfreq_event_dev *edev;
	bool set_voltage;
	unsigned int clk_num;
	unsigned long rate, target_rate, high_freq, low_freq;
	unsigned long volt, target_volt;
};

static int syna_devfreq_get_cur_freq(struct device *dev, unsigned long *freq)
{
	struct devm *priv = dev_get_drvdata(dev);

	*freq = priv->rate;

	return 0;
}

static int syna_devfreq_get_dev_status(struct device *dev,
					struct devfreq_dev_status *stat)
{
	struct devm *priv = dev_get_drvdata(dev);
	struct devfreq_event_data edata;
	int ret;

	ret = devfreq_event_get_event(priv->edev, &edata);
	if(ret < 0)
		return ret;

	stat->current_frequency = priv->rate;
	stat->busy_time = edata.load_count;
	stat->total_time = edata.total_count;

	return 0;
}

static int syna_enable_devclk(struct devm *priv)
{
	int ret, i;

	for (i = 0; i < priv->clk_num; i++) {
		ret = clk_prepare_enable(priv->dvfs_clk[i]);
		if (ret) {
			dev_err(priv->dev, "cannot enable clk, ret = %d\n", ret);
			return ret;
		}
	}

	return 0;
}

static int syna_disable_devclk(struct devm *priv)
{
	int i;
	for (i = 0; i < priv->clk_num; i++)
		clk_disable_unprepare(priv->dvfs_clk[i]);

	return 0;
}

static int syna_set_parent_devclk(struct devm *priv, struct clk *clk_parent)
{
	int ret, i;

	for (i = 0; i < priv->clk_num; i++) {
		ret = clk_set_parent(priv->dvfs_clk[i], clk_parent);
		if (ret) {
			dev_err(priv->dev, "can't set parent to clk, ret = %d\n", ret);
			return ret;
		}
	}

	return 0;
}

static int syna_devfreq_target(struct device *dev, unsigned long *freq, u32 flags)
{
	struct dev_pm_opp *opp;
	unsigned long target_volt, target_rate;
	struct devm *priv = dev_get_drvdata(dev);
	struct clk *clk_parent;
	int ret;

	if (!priv || !priv->clk_num || priv->clk_num > max_clk_num)
		return -EINVAL;

	opp = devfreq_recommended_opp(dev, freq, flags);
	if(IS_ERR(opp)) {
		dev_err(dev, "fail to find opp!\n");
		ret = PTR_ERR(opp);
	}

	target_rate = dev_pm_opp_get_freq(opp);

	if (priv->rate == target_rate) {
		dev_err(dev, "There is no need to update the clock rate and voltage!\n");
		return 0;
	}

	priv->rate = clk_get_rate(priv->dvfs_clk[0]);

	ret = syna_enable_devclk(priv);
	if (ret) {
		dev_err(priv->dev, "cannot enable clk, ret = %d\n", ret);
		return ret;
	}

	ret = syna_disable_devclk(priv);
	if (ret) {
		dev_err(priv->dev, "cannot disable clk, ret = %d\n", ret);
		return ret;
	}

	if (target_rate == priv->high_freq)
		clk_parent = priv->clk_high;
	else
		clk_parent = priv->clk_low;

	ret = syna_set_parent_devclk(priv, clk_parent);
	if (ret) {
		dev_err(priv->dev, "cannot set parent to clk, ret = %d\n", ret);
		return ret;
	}

	ret = syna_enable_devclk(priv);
	if (ret) {
		dev_err(priv->dev, "cannot enable clk, ret = %d\n", ret);
		return ret;
	}

	priv->rate = clk_get_rate(priv->dvfs_clk[0]);

	if (priv->set_voltage == true) {
		target_volt = dev_pm_opp_get_voltage(opp);

		ret = regulator_set_voltage(priv->regulator_vcore, target_volt, target_volt);
		if (ret) {
			dev_err(dev, "cannot set the voltage %lu uv, ret = %d\n", target_volt, ret);
			return ret;
		}

		dev_dbg(dev, "set: target_rate = %lu, priv->rate = %lu, voltage = %lu \n",
				target_rate, priv->rate, target_volt);
	}

	return 0;
}

static struct devfreq_dev_profile syna_devfreq_profile = {
	.polling_ms = 200,
	.target = syna_devfreq_target,
	.get_dev_status = syna_devfreq_get_dev_status,
	.get_cur_freq = syna_devfreq_get_cur_freq,
	.is_cooling_device = true,
};

static int init_opp(struct devm *priv)
{
	int ret;
	struct dev_pm_opp *opp;
	struct device *dev = priv->dev;
	unsigned long freq;

	ret = dev_pm_opp_of_add_table(dev);
	if (ret) {
		dev_err(dev, "Invalid operating-points in device tree.\n");
		return ret;
	}

	priv->rate = clk_get_rate(priv->dvfs_clk[0]);

	opp = devfreq_recommended_opp(dev, &priv->rate, 0);
	if (IS_ERR(opp)) {
		ret = PTR_ERR(opp);
		return ret;
	}

	priv->rate = dev_pm_opp_get_freq(opp);

	syna_devfreq_profile.initial_freq = priv->rate;

	freq = 0;
	if (IS_ERR(opp = dev_pm_opp_find_freq_ceil(dev, &freq)))
		ret = PTR_ERR(opp);

	priv->low_freq = dev_pm_opp_get_freq(opp);

	freq++;
	if (IS_ERR(opp = dev_pm_opp_find_freq_ceil(dev, &freq)))
		ret = PTR_ERR(opp);

	priv->high_freq = dev_pm_opp_get_freq(opp);

	return 0;
}

static int dvfs_probe(struct platform_device *pdev)
{
	struct devm *priv;
	struct device *dev = &pdev->dev;
	const char *clk_name;
	int clk_count, ret, i;

	priv = devm_kzalloc(dev, sizeof(struct devm), GFP_KERNEL);
	if (!priv) {
		dev_err(dev, "fail to alloc memory!!\n");
		return -ENOMEM;
	}

	priv->dev = dev;

	if (device_property_read_bool(dev, "force-set-voltage"))
		priv->set_voltage = true;

	if (priv->set_voltage == true) {
		priv->regulator_vcore = devm_regulator_get(dev, "vcore");
		if (IS_ERR(priv->regulator_vcore)) {
			dev_err(dev, "fail to find vcore!!\n");
			return -EPROBE_DEFER;
		}
	}

	clk_count = of_property_count_strings(dev->of_node, "clock-names");
	if (clk_count < 3 || clk_count > max_clk_num + 2) {
		dev_err(dev, "invalid clock number: %d\n", clk_count);
		return -EINVAL;
	}
	priv->clk_num = clk_count - 2;

	for (i = 0; i < priv->clk_num; i++) {
		ret = of_property_read_string_index(dev->of_node, "clock-names", i, &clk_name);
		if (ret) {
			dev_err(dev, "fail to find clock!!\n");
			return ret;
		}

		priv->dvfs_clk[i] = devm_clk_get(dev, clk_name);
		if (IS_ERR(priv->dvfs_clk[i])) {
			dev_err(dev, "fail to find %s!!\n", clk_name);
			return -EPROBE_DEFER;
		}
	}

	priv->clk_high = devm_clk_get(dev, "clk_high");
	if (IS_ERR(priv->clk_high)) {
		dev_err(dev, "fail to find high freq clk!!\n");
		return -EPROBE_DEFER;
	}

	priv->clk_low = devm_clk_get(dev, "clk_low");
	if (IS_ERR(priv->clk_low)) {
		dev_err(dev, "fail to find low freq clk!!\n");
		return -EPROBE_DEFER;
	}

	ret = init_opp(priv);
	if (ret) {
		dev_err(dev, "fail to init vcore opp table!\n");
		return ret;
	}

	priv->devfreq = devm_devfreq_add_device(dev, &syna_devfreq_profile, DEVFREQ_GOV_USERSPACE, NULL);
	if (IS_ERR(priv->devfreq))
		ret = PTR_ERR(priv->devfreq);

	devm_devfreq_register_opp_notifier(dev, priv->devfreq);

	platform_set_drvdata(pdev, priv);

	return 0;
}

static RET dvfs_remove(struct platform_device *pdev)
{
	struct devm *priv = dev_get_drvdata(&pdev->dev);

	devfreq_event_disable_edev(priv->edev);
	devm_devfreq_unregister_opp_notifier(priv->dev, priv->devfreq);

	dev_pm_opp_of_remove_table(priv->dev);
	RETURN;
}

static const struct of_device_id ids[] = {
	{ .compatible = "syna,dvfs" },
	{ },
};
MODULE_DEVICE_TABLE(of, ids);

static struct platform_driver dvfs_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "syna-dvfs",
		.of_match_table = ids,
	},
	.probe = dvfs_probe,
	.remove = dvfs_remove,
};

module_platform_driver(dvfs_driver);
MODULE_AUTHOR("Kaicheng Xie <Kaicheng.Xie@synaptics.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("syna-dvfs");
