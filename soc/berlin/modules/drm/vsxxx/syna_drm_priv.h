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

#include "syna_fl_info.h"

#define MAX_CRTC	MAX_NUM_CPCBS
#define VOUT_DEVICE VOUT_DSI

#define VPP_BUILD_IN_FRAME_ENABLE
#define VPP_BUILD_IN_FRAME_GFX_WIDTH    720
#define VPP_BUILD_IN_FRAME_GFX_HEIGHT   480
#define VPP_BUILD_IN_FRAME_VID_WIDTH    720
#define VPP_BUILD_IN_FRAME_VID_HEIGHT   480
#define VPP_BUILD_IN_FRAME_GFX_NULL_WIDTH    8
#define VPP_BUILD_IN_FRAME_GFX_NULL_HEIGHT   8

#define VPP_SUPPORT_SCALAR	1
#define LOGO_SRC_FMT		SRCFMT_YUV422
#define LOGO_BYTES_PER_PIXEL	2
#define IS_LOGO_VIDEO_FMT	1

typedef enum __VPP_BUILD_IN_FRAME_TYPE__ {
	VPP_BUILD_IN_FRAME_TYPE_GFX,
	VPP_BUILD_IN_FRAME_TYPE_VID,
	VPP_BUILD_IN_FRAME_TYPE_GFX_NULL,
	VPP_BUILD_IN_FRAME_TYPE_MAX,
} VPP_BUILD_IN_FRAME_TYPE;

#define VPP_GET_PLANE_ROTATE_INDX(INDX, PLANE)	\
			{ \
				if ((PLANE == PLANE_PIP) ||\
					(PLANE == PLANE_GFX1)) \
					INDX = PLANE; \
			}

#define GET_MAX_CRTC_FOR_MODE(mode) \
	((mode != VPP_VOUT_DUAL_MODE_PIP) ? 1 : MAX_CRTC)

void syna_push_default_buildin_frames_for_crtc(int crtc_id);
void syna_push_buildin_frame(u32 plane);
void syna_push_builtin_frames(void);
VPP_BUILD_IN_FRAME_TYPE syna_get_buidin_frame_type(ENUM_PLANE_ID plane_id);
struct drm_encoder *syna_tmds_encoder_create(struct drm_device *dev,
					ENUM_VOUT_CONNECTOR vout_id, ENUM_CPCB_ID cpcb_id);
int syna_dsi_panel_send_cmd (unsigned int cmdsize, unsigned char *pcmd);
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
