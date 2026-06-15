// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 Synaptics Incorporated
 *
 *
 * Author: Prem Anand <prem.anand@synaptics.com>
 *
 */

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>
#include <linux/backlight.h>
#include <linux/of.h>

#include <video/display_timing.h>
#include <video/of_display_timing.h>
#include <video/videomode.h>

#include <drm/drm_crtc.h>
#include <drm/drm_device.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>

#include "panel/panel.h"
#include "bridge/syna_bridge.h"
#include "avio_common.h"
#include "avio_core.h"
#include "drm_syna_drv.h"

/*
 * Panel takes some time to register, we will defer only 2 times for the sub-panel registration
 */
#define SUB_PANEL_MAX_DEFER_COUNT 2

typedef struct panel_timing_info_t {
	unsigned int hact;
	unsigned int hfp;
	unsigned int hsa;
	unsigned int hbp;
	unsigned int vact;
	unsigned int vfp;
	unsigned int vsa;
	unsigned int vbp;
	unsigned int pixelclock;
} PANEL_TIMING_INFO;

typedef struct panel_desc_t {
	PANEL_TIMING_INFO   panel_timing;
	int                 outformat;
	int                 bpp;
	int                 byteclock;
	int                 cmdsize;
	unsigned char		*cmd;
	int                 standby_cmdsize;
	unsigned char		*standby_cmd;

	struct gpio_desc 	*mipibl;
	struct regulator	*supply;
	const char *compatible_panel;
	struct drm_panel *sub_panel;
	void                *pMipiDsiInfo;  /* Pointer to MIPI DSI info for standby commands */
} SYNA_PANEL_DESC;

static struct display_timing synaPanelTimings;
static SYNA_PANEL_DESC  	synaPanelInfo;
static 	struct drm_panel *dsi_panel;
static int sub_panel_defer_count = 0;

void syna_panel_dsi_set_mipi_info(void *pMipiDsiInfo);

static const struct of_device_id platform_of_match[] = {
	{
		.compatible = "syna,drm-dsi",
	},
};

static int syna_panel_dsi_get_timings (struct drm_panel *panel, unsigned int num_timings,
			   struct display_timing *dptimings)
{
	memcpy(dptimings, &synaPanelTimings, sizeof(struct display_timing));

	return 0;
}

static int syna_panel_dsi_enable(struct drm_panel *panel)
{
	int err;
	avio_fastlogo_info display_info;

	display_info = avio_get_fastlogo_status();

	if (synaPanelInfo.supply) {
		err = regulator_enable(synaPanelInfo.supply);
		if (err < 0)
			pr_info("failed to enable supply: %d\n", err);
	}

	if (!display_info.u.status)
		syna_dsi_panel_send_cmd(synaPanelInfo.cmdsize, synaPanelInfo.cmd);

	if (!IS_ERR_OR_NULL(synaPanelInfo.sub_panel))
		drm_panel_enable(synaPanelInfo.sub_panel);

	return 0;
}

static int syna_panel_dsi_disable(struct drm_panel *panel)
{
	int err;

	/* Send standby commands before disabling panel */
	syna_dsi_send_standby_commands();

	if (synaPanelInfo.supply) {
		err = regulator_disable(synaPanelInfo.supply);
		if (err < 0)
			pr_info("failed to disable supply: %d\n", err);
	}

	if (!IS_ERR_OR_NULL(synaPanelInfo.sub_panel))
		drm_panel_disable(synaPanelInfo.sub_panel);

	return 0;
}

static int syna_panel_dsi_prepare(struct drm_panel *panel)
{
	int err;
	avio_fastlogo_info display_info;

	if (synaPanelInfo.supply) {
		if (!regulator_is_enabled(synaPanelInfo.supply)) {
			err = regulator_enable(synaPanelInfo.supply);
			if (err < 0) {
				pr_err("failed to enable supply: %d\n", err);
				return err;
			}
		}
	}

	display_info = avio_get_fastlogo_status();

	syna_bridge_enable();

	/* Release MIPI from Reset */
	avio_module_mipirst_set_gpio_val(0);

	syna_bridge_modeset(&synaPanelTimings);

	if (!IS_ERR_OR_NULL(synaPanelInfo.sub_panel))
		drm_panel_prepare(synaPanelInfo.sub_panel);

	return 0;
}

static int syna_panel_dsi_unprepare(struct drm_panel *panel)
{
	int err;

	syna_bridge_disable();

	/* Reset the MIPI */
	avio_module_mipirst_set_gpio_val(1);

	if (synaPanelInfo.supply) {
		if (regulator_is_enabled(synaPanelInfo.supply)) {
			err = regulator_disable(synaPanelInfo.supply);
			if (err < 0) {
				pr_err("failed to disable supply: %d\n", err);
				return err;
			}
		}
	}
	if (!IS_ERR_OR_NULL(synaPanelInfo.sub_panel))
		drm_panel_unprepare(synaPanelInfo.sub_panel);

	return 0;
}

static int syna_panel_dsi_get_modes(struct drm_panel *panel, struct drm_connector *connector) {

	struct drm_display_mode *dmode;

	dmode = drm_mode_create(connector->dev);
	if (dmode) {
		dmode->hdisplay = synaPanelTimings.hactive.typ;
		dmode->hsync_start = dmode->hdisplay + synaPanelTimings.hfront_porch.typ;
		dmode->hsync_end =  dmode->hsync_start + synaPanelTimings.hsync_len.typ;
		dmode->htotal = dmode->hsync_end + synaPanelTimings.hback_porch.typ;

		dmode->vdisplay =  synaPanelTimings.vactive.typ;
		dmode->vsync_start = dmode->vdisplay + synaPanelTimings.vfront_porch.typ;
		dmode->vsync_end = dmode->vsync_start + synaPanelTimings.vsync_len.typ;
		dmode->vtotal = dmode->vsync_end + synaPanelTimings.vback_porch.typ;
		dmode->clock =  synaPanelTimings.pixelclock.typ ;

		drm_mode_set_name(dmode);
		dmode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;

		drm_mode_probed_add(connector, dmode);

		connector->display_info.bus_formats = &synaPanelInfo.outformat;
		connector->display_info.num_bus_formats = 1;
		connector->display_info.bpc = synaPanelInfo.bpp;
	}

	return 0;
}

static const struct drm_panel_funcs syna_panel_dsi_funcs = {
	.prepare = syna_panel_dsi_prepare,
	.unprepare = syna_panel_dsi_unprepare,
	.enable = syna_panel_dsi_enable,
	.disable = syna_panel_dsi_disable,
	.get_timings = syna_panel_dsi_get_timings,
	.get_modes = syna_panel_dsi_get_modes,
};

int syna_panel_dsi_init(struct platform_device *pdev)
{
	int err = 0;
	long err_code;
	struct drm_panel *p = NULL;
	struct device *mipi_dev;
	struct device_node *np, *sub_np;
	struct regulator *supply;
	struct gpio_desc *mipibl;
	struct backlight_device *backlight;

	mipi_dev = devm_kmalloc(&pdev->dev, sizeof(struct device), GFP_KERNEL);
	if (!mipi_dev)
		return -ENOMEM;

	mipi_dev->of_node = of_find_compatible_node(NULL, NULL, "syna,drm-dsi");
	if (!mipi_dev->of_node)
		return -ENODEV;

	dsi_panel = devm_kzalloc(&pdev->dev, sizeof(struct drm_panel), GFP_KERNEL);
	if (!dsi_panel)
		return -ENOMEM;

	/* Find & obtain the handle to the compatible panel specified in drm/dsi-panel node */
	of_property_read_string(mipi_dev->of_node, "compatible_panel", &synaPanelInfo.compatible_panel);

	if (synaPanelInfo.compatible_panel) {
		sub_np = of_find_compatible_node(NULL, NULL, synaPanelInfo.compatible_panel);
		if (!sub_np || !of_device_is_available(sub_np)) {
			pr_info("syna_drm: sub-panel not in DT or disabled, proceeding without it\n");
			p = NULL;
			sub_panel_defer_count = 0;  /* Reset counter when device is absent */
		} else {
			p = of_drm_find_panel(sub_np);
			if (IS_ERR(p)) {
				err_code = PTR_ERR(p);
				if (err_code == -EPROBE_DEFER) {
					if (sub_panel_defer_count < SUB_PANEL_MAX_DEFER_COUNT) {
						sub_panel_defer_count++;
						pr_info("syna_drm: sub-panel not ready (defer attempt %d/%d), deferring\n",
							sub_panel_defer_count, SUB_PANEL_MAX_DEFER_COUNT);
						of_node_put(sub_np);
						of_node_put(mipi_dev->of_node);
						return -EPROBE_DEFER;
					} else {
						pr_warn("syna_drm: sub-panel defer limit reached, proceeding without it\n");
						p = NULL;
					}
				} else {
					pr_warn("syna_drm: sub-panel lookup failed (err=%ld), proceeding without it\n", err_code);
					p = NULL;
				}
			} else {
				/* Panel found successfully, reset counter for next boot */
				sub_panel_defer_count = 0;
			}
		}
	}

	err = avio_module_mipirst_get_gpio_handle(&pdev->dev,
							of_fwnode_handle(mipi_dev->of_node),
							GPIOD_ASIS);
	if (err == -EPROBE_DEFER)
		return err;

	mipibl = devm_fwnode_gpiod_get_index(&pdev->dev,
				of_fwnode_handle(mipi_dev->of_node),
				"mipibl", 0,
				GPIOD_ASIS, "mipibl");

	if (IS_ERR(mipibl)) {
		/* Ignore the error other than probe defer */
		if (PTR_ERR(mipibl) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		else
			mipibl = NULL;
	}

	np = of_parse_phandle(mipi_dev->of_node, "backlight", 0);
	if (np) {
		backlight = of_find_backlight_by_node(np);
		of_node_put(np);
		if (IS_ERR(backlight)) {
			if (PTR_ERR(backlight) == -EPROBE_DEFER) {
				pr_info("External Backlight Device not ready (err=%ld), deferring\n", PTR_ERR(backlight));
				return -EPROBE_DEFER;
			}
			else {
				backlight = NULL;
				pr_info("No external Backlight Device found, proceeding without it\n");
			}
		}
	} else {
		pr_info("No external Backlight Device\n");
		backlight = NULL;
	}

	/* If there is no regulator device registered, the framework will return
	 * -EPROBE_DEFER, which would lead to continuous probe deferral. To avoid
	 * infinite defer loops, do not propagate defer. Assume module load order
	 * is handled during boot.
	 */
	supply = devm_regulator_get_optional(&pdev->dev, "power");
	if (IS_ERR(supply)) {
		pr_info("External Regulator not available (err=%ld), continuing without it\n", PTR_ERR(supply));
		supply = NULL;
	}

	of_property_read_u32(mipi_dev->of_node, "ACTIVE_WIDTH", &synaPanelInfo.panel_timing.hact);
	of_property_read_u32(mipi_dev->of_node, "HFP", &synaPanelInfo.panel_timing.hfp);
	of_property_read_u32(mipi_dev->of_node, "HSYNCWIDTH", &synaPanelInfo.panel_timing.hsa);
	of_property_read_u32(mipi_dev->of_node, "HBP", &synaPanelInfo.panel_timing.hbp);
	of_property_read_u32(mipi_dev->of_node, "ACTIVE_HEIGHT", &synaPanelInfo.panel_timing.vact);
	of_property_read_u32(mipi_dev->of_node, "VFP", &synaPanelInfo.panel_timing.vfp);
	of_property_read_u32(mipi_dev->of_node, "VSYNCWIDTH", &synaPanelInfo.panel_timing.vsa);
	of_property_read_u32(mipi_dev->of_node, "VBP", &synaPanelInfo.panel_timing.vbp);
	of_property_read_u32(mipi_dev->of_node, "FREQ", &synaPanelInfo.panel_timing.pixelclock);

	of_property_read_u32(mipi_dev->of_node, "bits_per_pixel", &synaPanelInfo.bpp);
	of_property_read_u32(mipi_dev->of_node, "busformat", &synaPanelInfo.outformat);

	synaPanelInfo.cmdsize = of_property_count_u8_elems(mipi_dev->of_node, "command");

	if (synaPanelInfo.cmdsize ) {
		synaPanelInfo.cmd = devm_kzalloc(&pdev->dev, synaPanelInfo.cmdsize, GFP_KERNEL);
		if (!synaPanelInfo.cmd)
			return -ENOMEM;
	}

	of_property_read_u8_array(mipi_dev->of_node, "command", synaPanelInfo.cmd, synaPanelInfo.cmdsize);

	/* Parse standby commands if present */
	synaPanelInfo.standby_cmdsize = of_property_count_u8_elems(mipi_dev->of_node, "standby-cmds");
	if (synaPanelInfo.standby_cmdsize > 0) {
		synaPanelInfo.standby_cmd = devm_kzalloc(&pdev->dev, synaPanelInfo.standby_cmdsize, GFP_KERNEL);
		if (!synaPanelInfo.standby_cmd)
			return -ENOMEM;
		of_property_read_u8_array(mipi_dev->of_node, "standby-cmds", synaPanelInfo.standby_cmd, synaPanelInfo.standby_cmdsize);
	} else {
		synaPanelInfo.standby_cmd = NULL;
		synaPanelInfo.standby_cmdsize = 0;
	}

	synaPanelTimings.hactive.min = synaPanelTimings.hactive.typ = \
					synaPanelTimings.hactive.max = synaPanelInfo.panel_timing.hact;
	synaPanelTimings.hfront_porch.min = synaPanelTimings.hfront_porch.typ = \
					synaPanelTimings.hfront_porch.max = synaPanelInfo.panel_timing.hfp;
	synaPanelTimings.hsync_len.min = synaPanelTimings.hsync_len.typ = \
					synaPanelTimings.hsync_len.max = synaPanelInfo.panel_timing.hsa;
	synaPanelTimings.hback_porch.min = synaPanelTimings.hback_porch.typ = \
					synaPanelTimings.hback_porch.max = synaPanelInfo.panel_timing.hbp;

	synaPanelTimings.vactive.min = synaPanelTimings.vactive.typ = \
					synaPanelTimings.vactive.max = synaPanelInfo.panel_timing.vact;
	synaPanelTimings.vfront_porch.min =  synaPanelTimings.vfront_porch.typ = \
					synaPanelTimings.vfront_porch.max = synaPanelInfo.panel_timing.vfp;
	synaPanelTimings.vsync_len.min = synaPanelTimings.vsync_len.typ = \
					synaPanelTimings.vsync_len.max = synaPanelInfo.panel_timing.vsa;
	synaPanelTimings.vback_porch.min = synaPanelTimings.vback_porch.typ = \
					synaPanelTimings.vback_porch.max = synaPanelInfo.panel_timing.vbp;

	synaPanelTimings.pixelclock.min = synaPanelTimings.pixelclock.max = \
					synaPanelTimings.pixelclock.typ = synaPanelInfo.panel_timing.pixelclock;

	of_node_put(mipi_dev->of_node);

	avio_module_mipirst_set_gpio_val(0);
	if (mipibl)
		gpiod_direction_output(mipibl, 1);

	drm_panel_init(dsi_panel, mipi_dev, &syna_panel_dsi_funcs,
					DRM_MODE_CONNECTOR_DSI);

	drm_panel_add(dsi_panel);

	err = syna_bridge_init(pdev);
	if (err) {
		pr_warn("syna_panel_dsi_init: bridge init failed err=%d\n", err);
		/* Ignore bridge missing error - as bridge not compulsory */
		err = 0;
	}


	synaPanelInfo.sub_panel = p;

	synaPanelInfo.supply = supply;

	synaPanelInfo.mipibl = mipibl;

	dsi_panel->backlight = backlight;

	return err;
}

void syna_panel_dsi_set_mipi_info(void *pMipiDsiInfo)
{
	synaPanelInfo.pMipiDsiInfo = pMipiDsiInfo;
}

/**
 * syna_dsi_send_init_commands - Send MIPI DSI init commands to power on panel
 *
 * Sends the init command sequence configured in device tree to the panel.
 * Uses init commands parsed in panel_dsi.c from device tree.
 */
void syna_dsi_send_init_commands(void)
{
	if (!synaPanelInfo.cmd || synaPanelInfo.cmdsize <= 0)
		return;

	syna_dsi_panel_send_cmd(synaPanelInfo.cmdsize, synaPanelInfo.cmd);

	return;
}

/**
 * syna_dsi_send_standby_commands - Send MIPI DSI standby commands to panel
 *
 * Sends the standby command sequence configured in device tree to the panel.
 * Uses standby commands parsed in panel_dsi.c from device tree.
 */
void syna_dsi_send_standby_commands(void)
{
	if (!synaPanelInfo.standby_cmd || synaPanelInfo.standby_cmdsize <= 0)
		return;

	syna_dsi_panel_send_cmd(synaPanelInfo.standby_cmdsize, synaPanelInfo.standby_cmd);

	return;
}

void syna_panel_dsi_deinit(void)
{
	syna_bridge_deinit();
	kfree(synaPanelInfo.cmd);

	if (dsi_panel->backlight)
		put_device(&dsi_panel->backlight->dev);

	return;
}
