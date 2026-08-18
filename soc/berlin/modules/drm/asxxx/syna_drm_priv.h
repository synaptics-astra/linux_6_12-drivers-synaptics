// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 Synaptics Incorporated
 *
 *
 * Author: Prem Anand N <prem.anand@synaptics.com>
 *
 */
#if !defined(__SYNA_DRM_PRIV_H__)
#define __SYNA_DRM_PRIV_H__


#include "panel/panel.h"
#include "syna_fl_info.h"

#define MAX_CRTC	MAX_PANELS
#define VOUT_DEVICE VOUT_TFT
#define VPP_GET_PLANE_ROTATE_INDX(INDX, PLANE)	 (INDX = PLANE);

#define VPP_BUILD_IN_FRAME_GFX_WIDTH    720
#define VPP_BUILD_IN_FRAME_GFX_HEIGHT   480
#define VPP_BUILD_IN_FRAME_GFX_NULL_WIDTH    8
#define VPP_BUILD_IN_FRAME_GFX_NULL_HEIGHT   8

#define VPP_SUPPORT_SCALAR	0
#define LOGO_BYTES_PER_PIXEL	3
#define LOGO_SRC_FMT		SRCFMT_RGB888
#define IS_LOGO_VIDEO_FMT	0

typedef enum __VPP_BUILD_IN_FRAME_TYPE__ {
	VPP_BUILD_IN_FRAME_TYPE_GFX,
	VPP_BUILD_IN_FRAME_TYPE_GFX_NULL,
	VPP_BUILD_IN_FRAME_TYPE_MAX,
} VPP_BUILD_IN_FRAME_TYPE;

#define GET_MAX_CRTC_FOR_MODE(mode) MAX_CRTC

VPP_BUILD_IN_FRAME_TYPE syna_get_buidin_frame_type(ENUM_PLANE_ID plane_id);
struct drm_encoder *syna_encoder_create(struct drm_device *dev,
					ENUM_VOUT_CONNECTOR vout_id, ENUM_CPCB_ID cpcb_id,
					int possible_crtc_mask);
void syna_push_default_buildin_frames_for_crtc(int crtc_id);
void syna_push_buildin_frame(u32 plane);
void syna_push_builtin_frames(void);
int syna_vpp_get_disp_info(struct drm_device *dev, int crtc_ndx, fastlogo_info_t *fl_info);
int syna_crtc_set_property(struct drm_crtc *crtc,
					struct drm_crtc_state *state,
				  struct drm_property *property,
				  uint64_t val);
int syna_crtc_get_property(struct drm_crtc *crtc,
				  const struct drm_crtc_state *state,
				  struct drm_property *property,
				  uint64_t *val);
int syna_create_brightness_prop(struct drm_device *dev, struct drm_crtc *crtc);
int syna_panel_check_availability(struct platform_device *pdev, struct device *mipi_dev);

#endif /* !defined(__SYNA_DRM_PRIV_H__) */
