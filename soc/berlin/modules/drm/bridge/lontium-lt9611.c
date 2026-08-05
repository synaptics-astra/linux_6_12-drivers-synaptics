// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 * Copyright (c) 2019-2020. Linaro Limited.
 * Copyright (C) 2024 Synaptics Incorporated
 */

#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>

#include <sound/hdmi-codec.h>
#include <video/display_timing.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>

#include "syna_bridge.h"
#include "avio_core.h"

#define EDID_SEG_SIZE	256
#define EDID_LEN	32
#define EDID_LOOP	8
#define KEY_DDC_ACCS_DONE 0x02
#define DDC_NO_ACK	0x50

#define LT9611_4LANES	0

/*
 * Define LT9611_OPTIMIZE_INIT_TIME in Makefile to skip debug video check
 * and reduce init time by ~500ms. Undefine to match mainline lt9611 driver.
 */

struct lt9611 {
	struct device *dev;
	struct regmap *regmap;
	bool ac_mode;
	bool power_on;
	struct i2c_client *client;
	u32 vic;

	struct gpio_desc *reset_gpio;
	struct gpio_desc *enable_gpio;
};
static struct lt9611 *lt9611;
static struct i2c_board_info bridge_i2c_info = {
	I2C_BOARD_INFO("lt9611", 0x3b),
};

static struct i2c_client *client;
static struct device *lt9611_dev;
static struct i2c_adapter *adapter;

#define LT9611_PAGE_CONTROL	0xff

static const struct regmap_range_cfg lt9611_ranges[] = {
	{
		.name = "register_range",
		.range_min =  0,
		.range_max = 0x85ff,
		.selector_reg = LT9611_PAGE_CONTROL,
		.selector_mask = 0xff,
		.selector_shift = 0,
		.window_start = 0,
		.window_len = 0x100,
	},
};

static const struct regmap_config lt9611_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0xffff,
	.ranges = lt9611_ranges,
	.num_ranges = ARRAY_SIZE(lt9611_ranges),
};

void lt9611_bridge_modeset(struct display_timing *synaPanelTimings);

static int lt9611_mipi_input_analog(struct lt9611 *lt9611)
{
	const struct reg_sequence reg_cfg[] = {
		{ 0x8106, 0x60 }, /* port A rx current */
		{ 0x810a, 0xfe }, /* port A ldo voltage set */
		{ 0x810b, 0xbf }, /* enable port A lprx */
		{ 0x8111, 0x40 }, /* port B rx current */
		{ 0x8115, 0xfe }, /* port B ldo voltage set */
		{ 0x8116, 0xbf }, /* enable port B lprx */

		{ 0x811c, 0x03 }, /* PortA clk lane no-LP mode */
		{ 0x8120, 0x03 }, /* PortB clk lane with-LP mode */
	};

	return regmap_multi_reg_write(lt9611->regmap, reg_cfg, ARRAY_SIZE(reg_cfg));
}

static int lt9611_mipi_input_digital(struct lt9611 *lt9611,
				     const struct drm_display_mode *mode)
{
	struct reg_sequence reg_cfg[] = {
		{ 0x8300, LT9611_4LANES },
		{ 0x830a, 0x00 },
		{ 0x824f, 0x80 },
		{ 0x8250, 0x10 },
		{ 0x8302, 0x0a },
		{ 0x8306, 0x0a },
	};

	if (mode->hdisplay == 3840)
		reg_cfg[1].def = 0x03;

	return regmap_multi_reg_write(lt9611->regmap, reg_cfg, ARRAY_SIZE(reg_cfg));
}

static void lt9611_mipi_video_setup(struct lt9611 *lt9611,
				    const struct drm_display_mode *mode)
{
	u32 h_total, hactive, hsync_len, hfront_porch, hsync_porch;
	u32 v_total, vactive, vsync_len, vfront_porch, vsync_porch;

	h_total = mode->htotal;
	v_total = mode->vtotal;

	hactive = mode->hdisplay;
	hsync_len = mode->hsync_end - mode->hsync_start;
	hfront_porch = mode->hsync_start - mode->hdisplay;
	hsync_porch = hsync_len + mode->htotal - mode->hsync_end;

	vactive = mode->vdisplay;
	vsync_len = mode->vsync_end - mode->vsync_start;
	vfront_porch = mode->vsync_start - mode->vdisplay;
	vsync_porch = vsync_len + mode->vtotal - mode->vsync_end;

	regmap_write(lt9611->regmap, 0x830d, (u8)(v_total / 256));
	regmap_write(lt9611->regmap, 0x830e, (u8)(v_total % 256));

	regmap_write(lt9611->regmap, 0x830f, (u8)(vactive / 256));
	regmap_write(lt9611->regmap, 0x8310, (u8)(vactive % 256));

	regmap_write(lt9611->regmap, 0x8311, (u8)(h_total / 256));
	regmap_write(lt9611->regmap, 0x8312, (u8)(h_total % 256));

	regmap_write(lt9611->regmap, 0x8313, (u8)(hactive / 256));
	regmap_write(lt9611->regmap, 0x8314, (u8)(hactive % 256));

	regmap_write(lt9611->regmap, 0x8315, (u8)(vsync_len % 256));
	regmap_write(lt9611->regmap, 0x8316, (u8)(hsync_len % 256));

	regmap_write(lt9611->regmap, 0x8317, (u8)(vfront_porch % 256));

	regmap_write(lt9611->regmap, 0x8318, (u8)(vsync_porch % 256));

	regmap_write(lt9611->regmap, 0x8319, (u8)(hfront_porch % 256));

	regmap_write(lt9611->regmap, 0x831a, (u8)(hsync_porch / 256));
	regmap_write(lt9611->regmap, 0x831b, (u8)(hsync_porch % 256));
}

static void lt9611_pcr_setup(struct lt9611 *lt9611, const struct drm_display_mode *mode)
{
	const struct reg_sequence reg_cfg[] = {
		{ 0x830b, 0x01 },
		{ 0x830c, 0x10 },
		{ 0x8348, 0x00 },
		{ 0x8349, 0x81 },

		/* stage 1 */
		{ 0x8321, 0x4a },
		{ 0x8324, 0x71 },
		{ 0x8325, 0x30 },
		{ 0x832a, 0x01 },

		/* stage 2 */
		{ 0x834a, 0x40 },
		{ 0x831d, 0x10 },

		/* MK limit */
		{ 0x832d, 0x38 },
		{ 0x8331, 0x08 },
	};
	const struct reg_sequence reg_cfg2[] = {
		{ 0x830b, 0x03 },
		{ 0x830c, 0xd0 },
		{ 0x8348, 0x03 },
		{ 0x8349, 0xe0 },
		{ 0x8324, 0x72 },
		{ 0x8325, 0x00 },
		{ 0x832a, 0x01 },
		{ 0x834a, 0x10 },
		{ 0x831d, 0x10 },
		{ 0x8326, 0x37 },
	};

	regmap_multi_reg_write(lt9611->regmap, reg_cfg, ARRAY_SIZE(reg_cfg));

	switch (mode->hdisplay) {
	case 640:
		regmap_write(lt9611->regmap, 0x8326, 0x14);
		break;
	case 1920:
		regmap_write(lt9611->regmap, 0x8326, 0x37);
		break;
	case 3840:
		regmap_multi_reg_write(lt9611->regmap, reg_cfg2, ARRAY_SIZE(reg_cfg2));
		break;
	}

	/* pcr rst */
	regmap_write(lt9611->regmap, 0x8011, 0x5a);
	regmap_write(lt9611->regmap, 0x8011, 0xfa);
}

static void lt9611_pll_setup(struct lt9611 *lt9611, const struct drm_display_mode *mode)
{
	unsigned int pclk = mode->clock;
	const struct reg_sequence reg_cfg[] = {
		/* txpll init */
		{ 0x8123, 0x40 },
		{ 0x8124, 0x64 },
		{ 0x8125, 0x80 },
		{ 0x8126, 0x55 },
		{ 0x812c, 0x37 },
		{ 0x812f, 0x01 },
		{ 0x8126, 0x55 },
		{ 0x8127, 0x66 },
		{ 0x8128, 0x88 },
		{ 0x812a, 0x20 },
	};

	regmap_multi_reg_write(lt9611->regmap, reg_cfg, ARRAY_SIZE(reg_cfg));

	if (pclk > 150000)
		regmap_write(lt9611->regmap, 0x812d, 0x88);
	else if (pclk > 70000)
		regmap_write(lt9611->regmap, 0x812d, 0x99);
	else
		regmap_write(lt9611->regmap, 0x812d, 0xaa);

	/*
	 * first divide pclk by 2 first
	 *  - write divide by 64k to 19:16 bits which means shift by 17
	 *  - write divide by 256 to 15:8 bits which means shift by 9
	 *  - write remainder to 7:0 bits, which means shift by 1
	 */
	regmap_write(lt9611->regmap, 0x82e3, pclk >> 17); /* pclk[19:16] */
	regmap_write(lt9611->regmap, 0x82e4, pclk >> 9);  /* pclk[15:8]  */
	regmap_write(lt9611->regmap, 0x82e5, pclk >> 1);  /* pclk[7:0]   */

	regmap_write(lt9611->regmap, 0x82de, 0x20);
	regmap_write(lt9611->regmap, 0x82de, 0xe0);

	regmap_write(lt9611->regmap, 0x8016, 0xf1);
	regmap_write(lt9611->regmap, 0x8016, 0xf3);

	return;
}

#ifndef LT9611_OPTIMIZE_INIT_TIME
static int lt9611_read_video_check(struct lt9611 *lt9611, unsigned int reg, unsigned int *val)
{
	unsigned int temp, temp2;
	int ret;

	ret = regmap_read(lt9611->regmap, reg, &temp);
	if (ret)
		return ret;
	temp <<= 8;
	ret = regmap_read(lt9611->regmap, reg + 1, &temp2);
	if (ret)
		return ret;

	*val = temp + temp2;

	return (temp + temp2);
}

static int lt9611_video_check(struct lt9611 *lt9611)
{
	u32 v_total, vactive, hactive_a, hactive_b, h_total_sysclk;

	/* top module video check */
	/* vactive */
	if (lt9611_read_video_check(lt9611, 0x8282, &vactive) < 0)
		goto end;

	/* v_total */
	if (lt9611_read_video_check(lt9611, 0x826c, &v_total) < 0)
		goto end;

	/* h_total_sysclk */
	if (lt9611_read_video_check(lt9611, 0x8286, &h_total_sysclk) < 0)
		goto end;

	/* hactive_a */
	if (lt9611_read_video_check(lt9611, 0x8382, &hactive_a) < 0)
		goto end;

	hactive_a = hactive_a / 3;

	/* hactive_b */
	if (lt9611_read_video_check(lt9611, 0x8386, &hactive_b) < 0)
		goto end;

	hactive_b = hactive_b / 3;

	dev_info(lt9611->dev,
		 "video check: hactive_a=%d, hactive_b=%d, vactive=%d, v_total=%d, h_total_sysclk=%d\n",
		 hactive_a, hactive_b, vactive, v_total, h_total_sysclk);

	return 0;

end:
	dev_err(lt9611->dev, "read video check error\n");
	return -1;
}
#endif

static void lt9611_hdmi_tx_digital(struct lt9611 *lt9611)
{
	regmap_write(lt9611->regmap, 0x8443, 0x46 - lt9611->vic);
	regmap_write(lt9611->regmap, 0x8447, lt9611->vic);
	regmap_write(lt9611->regmap, 0x843d, 0x0a); /* UD1 infoframe */

	regmap_write(lt9611->regmap, 0x82d6, 0x8c);
	regmap_write(lt9611->regmap, 0x82d7, 0x04);
}

static void lt9611_hdmi_tx_phy(struct lt9611 *lt9611)
{
	struct reg_sequence reg_cfg[] = {
		{ 0x8130, 0x6a },
		{ 0x8131, 0x44 }, /* HDMI DC mode */
		{ 0x8132, 0x4a },
		{ 0x8133, 0x0b },
		{ 0x8134, 0x00 },
		{ 0x8135, 0x00 },
		{ 0x8136, 0x00 },
		{ 0x8137, 0x44 },
		{ 0x813f, 0x0f },
		{ 0x8140, 0xa0 },
		{ 0x8141, 0xa0 },
		{ 0x8142, 0xa0 },
		{ 0x8143, 0xa0 },
		{ 0x8144, 0x0a },
	};

	/* HDMI AC mode */
	if (lt9611->ac_mode)
		reg_cfg[2].def = 0x73;

	regmap_multi_reg_write(lt9611->regmap, reg_cfg, ARRAY_SIZE(reg_cfg));
}

static int lt9611_power_on(struct lt9611 *lt9611)
{
	int ret;
	const struct reg_sequence seq[] = {
		/* LT9611_System_Init */
		{ 0x8101, 0x18 }, /* sel xtal clock */

		/* timer for frequency meter */
		{ 0x821b, 0x69 }, /* timer 2 */
		{ 0x821c, 0x78 },
		{ 0x82cb, 0x69 }, /* timer 1 */
		{ 0x82cc, 0x78 },

		/* irq init */
		{ 0x8251, 0x01 },

		/* power consumption for work */
		{ 0x8004, 0xf0 },
		{ 0x8006, 0xf0 },
		{ 0x800a, 0x80 },
		{ 0x800b, 0x40 },
		{ 0x800d, 0xef },
		{ 0x8011, 0xfa },
	};

	if (lt9611->power_on)
		return 0;

	ret = regmap_multi_reg_write(lt9611->regmap, seq, ARRAY_SIZE(seq));
	if (!ret)
		lt9611->power_on = true;

	return ret;
}

static int lt9611_read_device_rev(struct lt9611 *lt9611)
{
	unsigned int rev;
	int ret;

	regmap_write(lt9611->regmap, 0x80ee, 0x01);
	ret = regmap_read(lt9611->regmap, 0x8002, &rev);
	if (ret)
		dev_err(lt9611->dev, "failed to read revision: %d\n", ret);
	else
		dev_info(lt9611->dev, "LT9611 revision: 0x%x\n", rev);

	return ret;
}

static void lt9611_modeset(struct lt9611 *lt9611,
				const struct drm_display_mode *mode)
{
	regmap_write(lt9611->regmap, 0x80ee, 0x01);

	lt9611_mipi_input_digital(lt9611, mode);
	lt9611_pll_setup(lt9611, mode);
	lt9611_mipi_video_setup(lt9611, mode);
	lt9611_pcr_setup(lt9611, mode);
}

static int lt9611_init(struct lt9611 *lt9611)
{
	int ret;

	if (lt9611_read_device_rev(lt9611))
		dev_err(lt9611->dev, "failed to read chip rev\n");

	ret = lt9611_power_on(lt9611);
	if (ret) {
		dev_err(lt9611->dev, "power on failed\n");
		return ret;
	}

	if (lt9611_mipi_input_analog(lt9611))
		dev_info(lt9611->dev, "MIPI analog config error\n");

	lt9611_hdmi_tx_digital(lt9611);
	lt9611_hdmi_tx_phy(lt9611);

#ifndef LT9611_OPTIMIZE_INIT_TIME
	msleep(500);
	lt9611_video_check(lt9611);
#endif

	/* Enable HDMI output */
	return regmap_write(lt9611->regmap, 0x8130, 0xea);
}

static void lt9611_bridge_deinit(void)
{
	i2c_unregister_device(client);
	i2c_put_adapter(adapter);
}

void lt9611_bridge_modeset(struct display_timing *synaPanelTimings)
{
	struct drm_display_mode dmode;

	if (lt9611->power_on != true)
		return;

	dmode.hdisplay = synaPanelTimings->hactive.typ;
	dmode.hsync_start = dmode.hdisplay + synaPanelTimings->hfront_porch.typ;
	dmode.hsync_end =  dmode.hsync_start + synaPanelTimings->hsync_len.typ;
	dmode.htotal = dmode.hsync_end + synaPanelTimings->hback_porch.typ;

	dmode.vdisplay =  synaPanelTimings->vactive.typ;
	dmode.vsync_start = dmode.vdisplay + synaPanelTimings->vfront_porch.typ;
	dmode.vsync_end = dmode.vsync_start + synaPanelTimings->vsync_len.typ;
	dmode.vtotal = dmode.vsync_end + synaPanelTimings->vback_porch.typ;
	dmode.clock =  synaPanelTimings->pixelclock.typ;

	switch (dmode.vdisplay) {
	case 1080:
		lt9611->vic = 16;
		break;
	default:
		pr_err("Resolution not supported\n");
		return;
	}

	lt9611_modeset(lt9611, &dmode);
}

static void lt9611_disable(struct lt9611 *lt9611)
{
	// power off sequence from mainline driver.
	const struct reg_sequence lt9611_poweroff_setup[] = {
		{ 0x8130, 0x6a }, //Disable HDMI output
	};

	if (lt9611->power_on)
		regmap_multi_reg_write(lt9611->regmap, lt9611_poweroff_setup, ARRAY_SIZE(lt9611_poweroff_setup));
}

static int lt9611_bridge_disable(void)
{
	if (lt9611) {
		lt9611_disable(lt9611);
		if (!IS_ERR(lt9611->reset_gpio))
			gpiod_set_value_cansleep(lt9611->reset_gpio, 1);
		if (!IS_ERR(lt9611->enable_gpio))
			gpiod_direction_output(lt9611->enable_gpio, 0);
		lt9611->power_on = false;
	}

	return 0;
}

static int lt9611_bridge_enable(void)
{
	int ret;

	if (!lt9611 || lt9611->power_on)
		return 0;

	if (!IS_ERR(lt9611->enable_gpio))
		gpiod_direction_output(lt9611->enable_gpio, 1);
	if (!IS_ERR(lt9611->reset_gpio))
		gpiod_set_value_cansleep(lt9611->reset_gpio, 0);

	ret = lt9611_init(lt9611);
	if (ret)
		dev_err(lt9611->dev, "LT9611 enable init failed\n");

	return ret;
}

int syna_bridge_probe(struct platform_device *pdev, SYNA_BRIDGE_FUNC_TABLE *psyna_bridge_funcs)
{
	int bridge_i2c_bus;
	int ret;
	avio_fastlogo_info display_info;

	lt9611_dev = devm_kmalloc(&pdev->dev, sizeof(struct device), GFP_KERNEL);
	if (!lt9611_dev) {
		ret = -ENOMEM;
		goto EXIT_DEFAULT;
	}

	lt9611_dev->of_node = of_find_compatible_node(NULL, NULL, "syna-bridge,lt9611");
	if (IS_ERR_OR_NULL(lt9611_dev->of_node)) {
		pr_warn("bridge node not found\n");
		ret = -ENODEV;
		goto EXIT_DEFAULT;
	}

	if (!of_device_is_available(lt9611_dev->of_node)) {
		pr_info("lt9611: bridge node is disabled, skipping\n");
		ret = -ENODEV;
		goto EXIT_DEFAULT;
	}

	of_property_read_u32(lt9611_dev->of_node, "i2c_bus", &bridge_i2c_bus);

	adapter = i2c_get_adapter(bridge_i2c_bus);
	if (!adapter) {
		pr_err("Failed to get adapter for bus %d\n", bridge_i2c_bus);
		ret = -EFAULT;
		goto EXIT_DEFAULT;
	}

	client = i2c_new_client_device(adapter, &bridge_i2c_info);
	if (IS_ERR(client)) {
		pr_err("Failed to create client %s at bus %d\n",
				bridge_i2c_info.type, bridge_i2c_bus);

		ret = PTR_ERR(client);
		goto EXIT_DEFAULT;
	}

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "device doesn't support I2C\n");
		ret = -ENODEV;
		goto EXIT_STAGE_I2C_INIT;
	}

	lt9611 = devm_kzalloc(&client->dev, sizeof(*lt9611), GFP_KERNEL);
	if (!lt9611) {
		ret = -ENOMEM;
		goto EXIT_STAGE_I2C_INIT;
	}

	lt9611->dev = &client->dev;
	lt9611->client = client;

	lt9611->regmap = devm_regmap_init_i2c(client, &lt9611_regmap_config);
	if (IS_ERR(lt9611->regmap)) {
		dev_err(lt9611->dev, "regmap i2c init failed\n");
		ret = PTR_ERR(lt9611->regmap);
		goto EXIT_STAGE_I2C_INIT;
	}

	display_info = avio_get_fastlogo_status();

	lt9611->enable_gpio = devm_fwnode_gpiod_get(&pdev->dev,
					of_fwnode_handle(lt9611_dev->of_node),
					"enable", GPIOD_ASIS, "lt9611-enable");
	if (PTR_ERR(lt9611->enable_gpio) == -EPROBE_DEFER)
		return -EPROBE_DEFER;

	if (!IS_ERR(lt9611->enable_gpio))
		gpiod_direction_output(lt9611->enable_gpio, 1);

	lt9611->reset_gpio = devm_fwnode_gpiod_get(&pdev->dev,
						of_fwnode_handle(lt9611_dev->of_node),
						"reset",
						GPIOD_ASIS, "lt9611-reset");
	if (PTR_ERR(lt9611->reset_gpio) == -EPROBE_DEFER)
		return -EPROBE_DEFER;

	if (!IS_ERR(lt9611->reset_gpio))
		gpiod_direction_output(lt9611->reset_gpio, 0);

	if (!display_info.u.status)
	{
		if(lt9611_init(lt9611)) {
			ret = -EFAULT;
			goto EXIT_STAGE_I2C_INIT;
		}
	}

	psyna_bridge_funcs->modeset = lt9611_bridge_modeset;
	psyna_bridge_funcs->deinit = lt9611_bridge_deinit;
	psyna_bridge_funcs->disable = lt9611_bridge_disable;
	psyna_bridge_funcs->enable = lt9611_bridge_enable;

	return 0;

EXIT_STAGE_I2C_INIT:
	i2c_unregister_device(client);
	i2c_put_adapter(adapter);

EXIT_DEFAULT:

	return ret;
}
