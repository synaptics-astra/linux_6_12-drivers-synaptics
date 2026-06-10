// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 Synaptics Incorporated
 *
 * Author: Jisheng Zhang <jszhang@kernel.org>
 *
 */

#include <linux/clk-provider.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <linux/module.h>

#include "clk.h"
#include "sl-sysmgr-clk.h"

static const struct gateclk_desc sl261x_gates[] = {
	{ "usb0coreclk",	"perifsysclk",	0 },
	{ "sdiosysclk",		"perifsysclk",	1 },
	{ "emmcsysclk",		"perifsysclk",	2 },
	{ "gpuaxiclk",		"hpcclk",	3 },
	{ "gethrgmiisysclk",	"perifsysclk",	4 },
	{ "sdio1sysclk",	"perifsysclk",	5 },
	{ "usb1coreclk",	"perifsysclk",	6 },
	{ "gethrgmii1sysclk",	"perifsysclk",	7 },
	{ "usb0phyrefclk",	"perifsysclk",	8 },
	{ "usb1phyrefclk",	"perifsysclk",	9 },
	{ "apbuart0clk",	"apbcoreclk",	10 },
	{ "apbuart1clk",	"apbcoreclk",	11 },
	{ "apbuart2clk",	"apbcoreclk",	12 },
	{ "apbuart3lck",	"apbcoreclk",	13 },
	{ "apbi2c0clk",		"apbcoreclk",	14 },
	{ "apbi2c1lck",		"apbcoreclk",	15 },
	{ "apbspi0clk",		"apbcoreclk",	16 },
	{ "apbspi1clk",		"apbcoreclk",	17 },
	{ "apbspi2clk",		"apbcoreclk",	18 },
	{ "apbspi3clk",		"apbcoreclk",	19 },
	{ "apbgpioclk",		"apbcoreclk",	20 },
	{ "apbtimersclk",	"apbcoreclk",	21 },
	{ "apbsyscntclk",	"apbcoreclk",	22 },
	{ "apbwdtclk",		"apbcoreclk",	23 },
	{ "apbdmaclk",		"apbcoreclk",	24 },
};

static int sl261x_gateclk_setup(struct platform_device *pdev)
{
	return berlin_gateclk_setup(pdev, sl261x_gates, ARRAY_SIZE(sl261x_gates));
}

static const struct clk_desc sl261x_descs[] = {
	{ "cpufastrefclk",		0x0, CLK_IS_CRITICAL },
	{ "memfastrefclk",		0x4, CLK_IS_CRITICAL },
	{ "cfgclk",			0x8, CLK_IS_CRITICAL },
	{ "sysclk",			0xc, CLK_IS_CRITICAL },
	{ "perifsysclk",		0x10, CLK_IS_CRITICAL },
	{ "apbcoreclk",			0x14, CLK_IS_CRITICAL },
	{ "apbserclk",			0x18, CLK_IS_CRITICAL },
	{ "atbclk",			0x1c, CLK_IS_CRITICAL },
	{ "hpcclk",			0x20, CLK_IS_CRITICAL },
	{ "emmcclk",			0x24 },
	{ "sd0clk",			0x28 },
	{ "sd1clk",			0x2c },
	{ "gethrgmiiclk",		0x30 },
	{ "gethrgmii1clk",		0x34 },
	{ "ge0_ptp_refclk",		0x38 },
	{ "ge1_ptp_refclk",		0x3c },
	{ "usb2testclk",		0x40 },
	{ "usb2test480mg0clk",		0x44 },
	{ "usb2test480mg1clk",		0x48 },
	{ "usb2test480mg2clk",		0x4c },
	{ "usb2test100mg0clk",		0x50 },
	{ "usb2test100mg1clk",		0x54 },
	{ "usb2test100mg2clk",		0x58 },
	{ "usb2test100mg3clk",		0x5c },
	{ "periftest125mg0clk",		0x60 },
	{ "periftest200mg0clk",		0x64 },
	{ "periftest200mg1clk",		0x68 },
	{ "gpuclk",			0x6c },
	{ "npuclk",			0x70 },
	{ "aviosysclk",			0x74, CLK_IS_CRITICAL },
	{ "aiosysclk",			0x78, CLK_IS_CRITICAL },
	{ "avio_lcdc2scanclk",		0x7c, CLK_IS_CRITICAL },
	{ "avio_ipiclk",		0x80, CLK_IS_CRITICAL },
	{ "avio_pclk",			0x84, CLK_IS_CRITICAL },
	{ "avio_dphyrxtxescclk",	0x88, CLK_IS_CRITICAL },
	{ "aviofpllclk",		0x8c, CLK_IS_CRITICAL },
	{ "avio_rx_scanbyteclk",	0x90, CLK_IS_CRITICAL },
	{ "avio_rx_scantestclk",	0x94, CLK_IS_CRITICAL },
};

static int sl261x_clk_setup(struct platform_device *pdev)
{
	return berlin_clk_setup(pdev, sl261x_descs, ARRAY_SIZE(sl261x_descs));
}

static const struct sl_sysmgr_gateclk_desc sl261x_sysmgr_gates[] = {
	{ "clk_cfg_uart0",	"clk_mcu_apb",	0 },
	{ "clk_ser_uart0",	"clk_mcu_ref",	1 },
	{ "clk_cfg_uart1",	"clk_mcu_apb",	2 },
	{ "clk_cfg_uart2",	"clk_mcu_apb",	3 },
	{ "clk_cfg_uart3",	"clk_mcu_apb",	4 },
	{ "clk_cfg_gpio",	"clk_mcu_apb",	5 },
	{ "clk_cfg_i2cm0",	"clk_mcu_apb",	6 },
	{ "clk_cfg_i2cm1",	"clk_mcu_apb",	7 },
	{ "clk_cfg_spim",	"clk_mcu_apb",	8 },
	{ "clk_cfg_spis",	"clk_mcu_apb",	9 },
	{ "clk_cfg_i3c",	"clk_mcu_apb",	10 },
	{ "clk_cfg_xspi",	"clk_mcu_apb",	11 },
	{ "clk_axi_xspi",	"clk_mcu_axi",	12 },
	{ "clk_cfg_pvt",	"clk_mcu_apb",	13 },
	{ "clk_cfg_adc",	"clk_mcu_apb",	14 },
	{ "clk_cfg_pwm",	"clk_mcu_apb",	15 },
	{ "clk_bus_can0",	"clk_mcu_axi",	16 },
	{ "clk_bus_can1",	"clk_mcu_axi",	17 },
	{ "clk_cfg_pdm",	"clk_mcu_apb",	18 },
};

static const struct sl_sysmgr_clk_desc sl261x_sysmgr_clks[] = {
	{ "clk_ser_uart1",	"clk_mcu_pll",		0x4,	4 },
	{ "clk_ser_uart2",	"clk_mcu_pll",		0x8,	4 },
	{ "clk_ser_uart3",	"clk_mcu_pll",		0xc,	4 },
	{ "clk_deb_gpio",	"clk_mcu_pll",		0x10,	4 },
	{ "clk_ser_i2cm0",	"clk_mcu_pll",		0x14,	4 },
	{ "clk_ser_i2cm1",	"clk_mcu_pll",		0x18,	4 },
	{ "clk_ser_spim",	"clk_mcu_pll",		0x1c,	4 },
	{ "clk_ser_spis",	"clk_mcu_pll",		0x20,	4 },
	{ "clk_ser_i3c",	"clk_mcu_pll",		0x24,	4 },
	{ "clk_ser_xspi",	"clk_mcu_io_pll",	0x28,	4 },
	{ "clk_hs_pvt",		"clk_mcu_pll",		0x2c,	4 },
	{ "clk_core_adc",	"clk_mcu_pll",		0x30,	8 },
	{ "clk_per_pwm",	"clk_mcu_pll",		0x34,	4 },
	{ "clk_ser_can0",	"clk_mcu_pll",		0x38,	4 },
	{ "clk_ser_can1",	"clk_mcu_pll",		0x3c,	4 },
	{ "clk_ser_pdm",	"clk_mcu_pll",		0x40,	8 },
	{ "clk_ref_calib",	"clk_mcu_pll",		0x44,	8 },
};

static int sl261x_sysmgr_clk_setup(struct platform_device *pdev)
{
	return sl_sysmgr_clk_setup(pdev, sl261x_sysmgr_gates, ARRAY_SIZE(sl261x_sysmgr_gates), 0,
				   sl261x_sysmgr_clks, ARRAY_SIZE(sl261x_sysmgr_clks));
}

static const struct of_device_id sl261x_clks_match_table[] = {
	{ .compatible = "syna,sl261x-clk",
	  .data = sl261x_clk_setup },
	{ .compatible = "syna,sl261x-gateclk",
	  .data = sl261x_gateclk_setup },
	{ .compatible = "syna,sl261x-sysmgr-clk",
	  .data = sl261x_sysmgr_clk_setup },
	{ }
};
MODULE_DEVICE_TABLE(of, sl261x_clks_match_table);

static int sl261x_clks_probe(struct platform_device *pdev)
{
	int (*clk_setup)(struct platform_device *pdev);
	int ret;

	clk_setup = of_device_get_match_data(&pdev->dev);
	if (!clk_setup)
		return -ENODEV;

	ret = clk_setup(pdev);
	if (ret)
		return ret;

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int sl261x_clks_suspend(struct device *dev)
{
	if (device_is_compatible(dev, "syna,sl261x-clk"))
		return berlin_clk_suspend(dev);

	return 0;
}

static int sl261x_clks_resume(struct device *dev)
{
	if (device_is_compatible(dev, "syna,sl261x-clk"))
		return berlin_clk_resume(dev);

	return 0;
}
#endif

static const struct dev_pm_ops sl261x_clks_pmops = {
	SET_SYSTEM_SLEEP_PM_OPS(sl261x_clks_suspend, sl261x_clks_resume)
};

static struct platform_driver sl261x_clks_driver = {
	.probe		= sl261x_clks_probe,
	.driver		= {
		.name	= "syna-sl261x-clks",
		.of_match_table = sl261x_clks_match_table,
		.pm	= &sl261x_clks_pmops,
	},
};
module_platform_driver(sl261x_clks_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Jisheng Zhang <jszhang@kernel.org>");
MODULE_DESCRIPTION("Synaptics sl261x clks Driver");
