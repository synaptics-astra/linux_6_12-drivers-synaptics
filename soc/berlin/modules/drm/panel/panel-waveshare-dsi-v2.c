// SPDX-License-Identifier: GPL-2.0
/*
 * Adapter panel driver for the Waveshare 10.1" DSI Touch (A) on
 * Synaptics Astra (syna_drm) platforms.
 *
 * The Synaptics syna_drm DSI host wraps the panel in its own drm_panel
 * (see drivers/synaptics/.../drm/panel/panel_dsi.c) and sends the DCS
 * init sequence itself from the device-tree "command" property. It then
 * looks up an optional sub-panel via of_drm_find_panel() using the
 * "compatible_panel" string.
 *
 * The upstream raspberrypi/linux panel-waveshare-dsi-v2 driver is a
 * mipi_dsi_device, which is incompatible with syna_drm's sub-panel
 * model. This driver fills the gap: it probes as a platform device,
 * grabs the iovcc / avdd / reset GPIOs that the Waveshare panel MCU
 * (waveshare-panel-regulator) exposes, runs the JD9365 power-on
 * sequence, and registers a drm_panel so syna_drm can find it.
 *
 * The DCS init bytes are transmitted by syna_drm via the dsi_panel
 * "command" property, not by this driver.
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#include <drm/drm_connector.h>
#include <drm/drm_panel.h>
#include "avio_fl_info.h"

struct ws_touch_panel {
	struct drm_panel base;
	struct device *dev;

	struct gpio_desc *reset;
	struct gpio_desc *iovcc;
	struct gpio_desc *avdd;

	bool powered;
};

static inline struct ws_touch_panel *to_ws(struct drm_panel *panel)
{
	return container_of(panel, struct ws_touch_panel, base);
}


static void ws_touch_power_on(struct ws_touch_panel *p)
{
	if (p->powered)
		return;

	if (p->iovcc) {
		gpiod_set_value_cansleep(p->iovcc, 1);
		msleep(20);
	}
	if (p->avdd) {
		gpiod_set_value_cansleep(p->avdd, 1);
		msleep(20);
	}
	if (p->reset) {
		gpiod_set_value_cansleep(p->reset, 0);
		msleep(60);
		gpiod_set_value_cansleep(p->reset, 1);
		msleep(60);
	}

	p->powered = true;
}

static void ws_touch_power_off(struct ws_touch_panel *p)
{
	if (!p->powered)
		return;

	if (p->reset) {
		gpiod_set_value_cansleep(p->reset, 0);
		msleep(20);
	}
	if (p->avdd) {
		gpiod_set_value_cansleep(p->avdd, 0);
		msleep(20);
	}
	if (p->iovcc) {
		gpiod_set_value_cansleep(p->iovcc, 0);
		msleep(20);
	}

	p->powered = false;
}

static int ws_touch_prepare(struct drm_panel *panel)
{
	struct ws_touch_panel *p = to_ws(panel);

	dev_info(p->dev, "prepare\n");
	/*
	 * panel_dsi.c calls drm_panel_prepare() on the sub-panel AFTER it
	 * sends the DCS init sequence, so we cannot power on here — the panel
	 * must already be up when the DCS bytes arrive.  Power was asserted at
	 * probe() and is kept on; this callback is intentionally a no-op.
	 */
	return 0;
}

static int ws_touch_unprepare(struct drm_panel *panel)
{
	struct ws_touch_panel *p = to_ws(panel);

	dev_info(p->dev, "unprepare\n");
	/*
	 * Keep the panel powered across unprepare/prepare cycles.  If we power
	 * off here, the next prepare() cannot re-power before panel_dsi.c
	 * sends DCS (same ordering constraint as above).  The backlight is
	 * gated separately via display_mcu; power is removed only at remove().
	 */
	return 0;
}

static int ws_touch_enable(struct drm_panel *panel)
{
	struct ws_touch_panel *p = to_ws(panel);

	dev_info(p->dev, "enable\n");
	return 0;
}

static int ws_touch_disable(struct drm_panel *panel)
{
	struct ws_touch_panel *p = to_ws(panel);

	dev_info(p->dev, "disable\n");
	return 0;
}

static const struct drm_panel_funcs ws_touch_funcs = {
	.prepare = ws_touch_prepare,
	.unprepare = ws_touch_unprepare,
	.enable = ws_touch_enable,
	.disable = ws_touch_disable,
};

static int ws_touch_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ws_touch_panel *p;
	avio_fastlogo_info display_info;
	enum gpiod_flags GPIO_ID;

	p = devm_kzalloc(dev, sizeof(*p), GFP_KERNEL);
	if (!p)
		return -ENOMEM;

	p->dev = dev;

	display_info = avio_get_fastlogo_status();
	if (!display_info.u.status)
		GPIO_ID = GPIOD_OUT_LOW;
	else
		GPIO_ID = GPIOD_ASIS;

	p->reset = devm_gpiod_get_optional(dev, "reset", GPIO_ID);
	if (IS_ERR(p->reset))
		return dev_err_probe(dev, PTR_ERR(p->reset),
				     "failed to get reset gpio\n");

	p->iovcc = devm_gpiod_get_optional(dev, "iovcc", GPIO_ID);
	if (IS_ERR(p->iovcc))
		return dev_err_probe(dev, PTR_ERR(p->iovcc),
				     "failed to get iovcc gpio\n");

	p->avdd = devm_gpiod_get_optional(dev, "avdd", GPIO_ID);
	if (IS_ERR(p->avdd))
		return dev_err_probe(dev, PTR_ERR(p->avdd),
				     "failed to get avdd gpio\n");

	platform_set_drvdata(pdev, p);

	drm_panel_init(&p->base, dev, &ws_touch_funcs, DRM_MODE_CONNECTOR_DSI);

	/*
	 * Power on now, before drm_panel_add() makes this panel visible to the
	 * DRM core.  panel_dsi.c sends the DCS init sequence in its prepare()
	 * callback before calling drm_panel_prepare() on us, so the rails and
	 * reset must be asserted before the first DRM modeset, not lazily in
	 * our own prepare().
	 */
	ws_touch_power_on(p);

	drm_panel_add(&p->base);

	dev_info(dev, "waveshare 10.1\" dsi touch (a) panel registered\n");
	return 0;
}

static void ws_touch_remove(struct platform_device *pdev)
{
	struct ws_touch_panel *p = platform_get_drvdata(pdev);

	drm_panel_remove(&p->base);
	ws_touch_power_off(p);
}

static const struct of_device_id ws_touch_of_match[] = {
	{ .compatible = "waveshare,10.1-dsi-touch-a" },
	{}
};
MODULE_DEVICE_TABLE(of, ws_touch_of_match);

static struct platform_driver ws_touch_driver = {
	.probe = ws_touch_probe,
	.remove = ws_touch_remove,
	.driver = {
		.name = "panel-waveshare-dsi-v2",
		.of_match_table = ws_touch_of_match,
	},
};
module_platform_driver(ws_touch_driver);

MODULE_AUTHOR("Michal Oleszczyk <michal.oleszczyk@grinn-global.com>");
MODULE_DESCRIPTION("Waveshare DSI Touch panel adapter for syna_drm");
MODULE_LICENSE("GPL");