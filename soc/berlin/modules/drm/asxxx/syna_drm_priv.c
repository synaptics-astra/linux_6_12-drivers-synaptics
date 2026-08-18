// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 Synaptics Incorporated
 *
 *
 * Author: Shanmugam Ramachandran <Shanmugam.Ramachandran@synaptics.com>
 *
 */

#include "drm_syna_drv.h"

#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_atomic_helper.h>
#include "syna_vpp.h"
#include "vpp_mem.h"
#include "vpp_api.h"

#include <drm/drm_panel.h>
#include <video/display_timing.h>

#include "dsih_displays.h"
#include "dsih_core.h"
#include "dsih_api.h"
#include "includes.h"

static void syna_vpp_convert_mipi_resinfo_to_lcdc(VPP_MIPI_CONFIG_PARAMS *pMipiRescfg,
			SYNA_LCDC_CONFIG *plcdccfg)
{
	memset(plcdccfg, 0, sizeof(SYNA_LCDC_CONFIG));
	plcdccfg->xres = pMipiRescfg->infoparams.resInfo.active_width;
	plcdccfg->right_margin = pMipiRescfg->infoparams.resInfo.hfrontporch;
	plcdccfg->hsync_len = pMipiRescfg->infoparams.resInfo.hsyncwidth;
	plcdccfg->left_margin = pMipiRescfg->infoparams.resInfo.hbackporch;
	plcdccfg->yres = pMipiRescfg->infoparams.resInfo.active_height;
	plcdccfg->lower_margin = pMipiRescfg->infoparams.resInfo.vfrontporch;
	plcdccfg->vsync_len = pMipiRescfg->infoparams.resInfo.vsyncwidth;
	plcdccfg->upper_margin = pMipiRescfg->infoparams.resInfo.vbackporch;
	plcdccfg->pixclock = pMipiRescfg->infoparams.resInfo.freq;

	if (pMipiRescfg->initparams.color_coding == COLOR_CODE_24BIT)
		plcdccfg->bits_per_pixel = 24;
	else if (pMipiRescfg->initparams.color_coding >= COLOR_CODE_16BIT_CONFIG1 &&
			pMipiRescfg->initparams.color_coding <= COLOR_CODE_16BIT_CONFIG3)
		plcdccfg->bits_per_pixel = 16;
	else if (pMipiRescfg->initparams.color_coding >= COLOR_CODE_18BIT_CONFIG1 &&
			pMipiRescfg->initparams.color_coding <= COLOR_CODE_18BIT_CONFIG2)
		plcdccfg->bits_per_pixel = 18;
}

static const ENUM_PLANE_ID syna_primary_plane_id[MAX_CRTC] = {
	PLANE_GFX0,
	PLANE_GFX1,
};

int syna_is_scaling_allowed(struct syna_drm_private *dev_priv,
				ENUM_PLANE_ID plane_id, ENUM_CPCB_ID cpcb_id)
{
	//Scaling not supported in asxxx
	return 0;
}

VPP_BUILD_IN_FRAME_TYPE syna_get_buidin_frame_type(ENUM_PLANE_ID plane_id)
{
	return VPP_BUILD_IN_FRAME_TYPE_GFX;
}

void syna_vpp_dev_init_priv(struct drm_device *dev)
{
	/* Buildin frame not required for ASXX - Single Plane/Single CRTC system */
}

void syna_read_config_priv(struct syna_drm_private *dev_priv)
{
	struct drm_device *dev = dev_priv->dev;
	vpp_config_params *p_vpp_config_param = &dev_priv->vpp_config_param;
	SYNA_LCDC_CONFIG *lcdcConfig = NULL;
	struct device_node *lcdc_node;
	int mode, rgbswap;

	lcdc_node = of_find_compatible_node(NULL, NULL, "syna,drm-lcdc");

	if (lcdc_node) {
		lcdcConfig = devm_kmalloc(dev->dev, sizeof(SYNA_LCDC_CONFIG), GFP_KERNEL);
		if (lcdcConfig) {
			memset(lcdcConfig, 0, sizeof(SYNA_LCDC_CONFIG));
			of_property_read_u32(lcdc_node, "hact", &lcdcConfig->xres);
			of_property_read_u32(lcdc_node, "hfp", &lcdcConfig->right_margin);
			of_property_read_u32(lcdc_node, "hsa", &lcdcConfig->hsync_len);
			of_property_read_u32(lcdc_node, "hbp", &lcdcConfig->left_margin);
			of_property_read_u32(lcdc_node, "vact", &lcdcConfig->yres);
			of_property_read_u32(lcdc_node, "vfp", &lcdcConfig->lower_margin);
			of_property_read_u32(lcdc_node, "vsa", &lcdcConfig->vsync_len);
			of_property_read_u32(lcdc_node, "vbp", &lcdcConfig->upper_margin);
			of_property_read_u32(lcdc_node, "pixclockKhz", &lcdcConfig->pixclock);
			of_property_read_u32(lcdc_node, "bits_per_pixel", &lcdcConfig->bits_per_pixel);
			of_property_read_u32(lcdc_node, "busformat", &mode);
			of_property_read_u32(lcdc_node, "rgbswap", &rgbswap);

			lcdcConfig->mode = mode;
			lcdcConfig->rgb_swap = rgbswap;
			p_vpp_config_param->active_planes = (1 << PLANE_GFX0);
		}
	}
	p_vpp_config_param->lcdc_config_params = lcdcConfig;

	if (p_vpp_config_param->mipi_resinfo_params) {
		p_vpp_config_param->mipi_lcdc_config_params = devm_kmalloc(dev->dev,
							sizeof(SYNA_LCDC_CONFIG), GFP_KERNEL);
		if (p_vpp_config_param->mipi_lcdc_config_params) {
			syna_vpp_convert_mipi_resinfo_to_lcdc(p_vpp_config_param->mipi_resinfo_params,
				p_vpp_config_param->mipi_lcdc_config_params);
			p_vpp_config_param->active_planes |= (1 << PLANE_GFX1);
		}
	}
}

int syna_modeset_createEntries(struct syna_drm_private *dev_priv)
{
	struct drm_device *dev = dev_priv->dev;
	int err = 0;
	unsigned int plane_possible_crtc_mask;
	enum drm_plane_type plane_type;
	int panelId;
	int encoderId;

	for (encoderId = 0, panelId = 0; panelId < MAX_PANELS; panelId++) {
		dev_priv->encoder[panelId] = syna_encoder_create(dev, panelId, panelId, (1 << encoderId));

		if (IS_ERR(dev_priv->encoder[panelId])) {
			DRM_ERROR("failed to create a encoder %d\n", panelId);
		} else {
			dev_priv->connector[panelId] = syna_lcdc_connector_create(dev);

			if (IS_ERR(dev_priv->connector[panelId])) {
				DRM_ERROR("failed to create an Connector %d\n", panelId);
			} else {
				err = drm_connector_attach_encoder(dev_priv->connector[panelId],
								dev_priv->encoder[panelId]);

				if (err) {
					DRM_ERROR
						("failed to attach [ENCODER:%d:%s] to [CONNECTOR:%d:%s] (err=%d)\n",
						dev_priv->encoder[panelId]->base.id,
						dev_priv->encoder[panelId]->name,
						dev_priv->connector[panelId]->base.id,
						dev_priv->connector[panelId]->name, err);
					goto err_syna_modeset_createEntries;
				}
			}

			/* Note: one plane(GFX) per crtc/panel --
			 * so panel & plane are used inter-changeably
			 */
			plane_type = DRM_PLANE_TYPE_PRIMARY;
			plane_possible_crtc_mask = (1 << encoderId);
			dev_priv->plane[panelId] = syna_plane_create(dev,
					plane_possible_crtc_mask, panelId, plane_type);
			if (IS_ERR(dev_priv->plane[panelId])) {
				DRM_ERROR("failed to create a %d plane\n", plane_type);
				err = PTR_ERR(dev_priv->plane[panelId]);
				goto err_syna_modeset_createEntries;
			}

			dev_priv->crtc[panelId] = syna_crtc_create(dev, encoderId, dev_priv->plane[panelId]);
			if (IS_ERR(dev_priv->crtc[panelId])) {
				DRM_ERROR("failed to create a CRTC\n");
				err = PTR_ERR(dev_priv->crtc[panelId]);
				goto err_syna_modeset_createEntries;
			}
			encoderId++;
		}
	}

err_syna_modeset_createEntries:
	return err;
}

int syna_vpp_get_bm_details(struct dma_buf *dma_buf,
		       struct bm_pt_param *pt_param,
		       struct berlin_meta **bm_meta)
{
	return -1;
}

int syna_dsi_panel_send_cmd (unsigned int cmdsize, unsigned char *pcmd)
{
	return dsi_panel_send_cmd(cmdsize, pcmd);
}

void syna_push_buildin_frame(u32 plane)
{
	syna_vpp_push_buildin_null_frame(plane);
}

int syna_vpp_get_disp_info(struct drm_device *dev, int crtc_ndx, fastlogo_info_t *fl_info)
{
	struct syna_drm_private *dev_priv = dev->dev_private;
	struct display_timing dptimings;

	dev_priv->panel[crtc_ndx] = ((crtc_ndx == 0) ?\
			of_drm_find_panel(of_find_compatible_node(NULL, NULL, "syna,drm-lcdc")):\
			of_drm_find_panel(of_find_compatible_node(NULL, NULL, "syna,drm-dsi")));

	if (!fl_info || IS_ERR(dev_priv->panel[crtc_ndx]))
		return -1;

	if (dev_priv->panel[crtc_ndx]->funcs && dev_priv->panel[crtc_ndx]->funcs->get_timings) {
		dev_priv->panel[crtc_ndx]->funcs->get_timings(dev_priv->panel[crtc_ndx],
			1,
			&dptimings);

		fl_info->width = dptimings.hactive.typ;
		fl_info->height = dptimings.vactive.typ;
		return 0;
	} else {
		return -1;
	}
}

static const char * const syna_brightness_props[] = {
	"R brightness",
	"G brightness",
	"B brightness",
	"brightness",
};

static int syna_brightness_prop_to_channel(const char *name, int *channel)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(syna_brightness_props); i++) {
		if (strcmp(name, syna_brightness_props[i]) == 0) {
			*channel = i;
			return 0;
		}
	}
	return -EINVAL;
}

int syna_crtc_set_property(struct drm_crtc *crtc, struct drm_crtc_state *state,
						   struct drm_property *property, uint64_t val)
{
	struct syna_crtc *syna_crtc = to_syna_crtc(crtc);
	int channel;
	int ret;

	if (!syna_crtc) {
		DRM_ERROR("syna crtc is NULL!!\n");
		return -EINVAL;
	}

	if (!(ret = syna_brightness_prop_to_channel(property->name, &channel))) {
		ret = syna_vpp_update_brightness(syna_crtc->number, channel, val);
		if (ret) {
			DRM_ERROR("failed to update brightness for property %s\n",
					  property->name);
		}
	}

	return ret;
}

int syna_crtc_get_property(struct drm_crtc *crtc,
						   const struct drm_crtc_state *state,
						   struct drm_property *property, uint64_t *val)
{
	struct syna_crtc *syna_crtc = to_syna_crtc(crtc);
	int channel;
	int ret;

	if (!syna_crtc) {
		DRM_ERROR("syna crtc is NULL!!\n");
		return -EINVAL;
	}

	if (!(ret = syna_brightness_prop_to_channel(property->name, &channel))) {
		ret = syna_vpp_get_brightness(syna_crtc->number, channel, val);
		if (ret) {
			DRM_ERROR("failed to get brightness for property %s\n",
					  property->name);
		}
	}

	return ret;
}


int syna_create_brightness_prop(struct drm_device *dev, struct drm_crtc *crtc)
{
	struct drm_property *prop;
	int i;

	for (i = 0; i < ARRAY_SIZE(syna_brightness_props); i++) {
		prop = drm_property_create_signed_range(dev, 0,
				syna_brightness_props[i], -128, 127);
		if (!prop)
			return -ENOMEM;
		drm_object_attach_property(&crtc->base, prop, 0);
	}

	return 0;
}

/*
 * Validate the panel/display configuration.
 *
 * Returns -ENODEV if no display nodes are available.
 * Returns 0 when a valid display is available.
 *
 * SL1620: At least one display (TFT or DSI) must be available.
 *         If either TFT or DSI is present, validation succeeds.
 *
 * SL2610: DSI is the only display. If DSI is not available,
 *         validation fails with -ENODEV.
 */
int syna_panel_check_availability(struct platform_device *pdev, struct device *mipi_dev)
{
	struct device_node *lcdc_node;
	int lcdc_node_available = 0;
	int mipi_node_available = 0;

	if (!pdev || !mipi_dev)
		return -EINVAL;

	mipi_dev->of_node = of_find_compatible_node(NULL, NULL, SYNA_DRM_DSI_COMPATIBLE);

	if (!mipi_dev->of_node || !of_device_is_available(mipi_dev->of_node))
		DRM_DEBUG_DRIVER("DSI node is unavailable\n");
	else
		mipi_node_available = 1;

	lcdc_node = of_find_compatible_node(NULL, NULL, SYNA_DRM_LCDC_COMPATIBLE);
	if (!lcdc_node || !of_device_is_available(lcdc_node))
		DRM_DEBUG_DRIVER("LCDC node is unavailable\n");
	else
		lcdc_node_available = 1;

	of_node_put(lcdc_node);
	of_node_put(mipi_dev->of_node);

	if (!mipi_node_available && !lcdc_node_available)
		return -ENODEV;

	return 0;
}
