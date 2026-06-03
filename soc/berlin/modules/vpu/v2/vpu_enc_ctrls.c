// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 - 2023 Synaptics Incorporated.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/errno.h>
#include <linux/videodev2.h>
#include <media/v4l2-common.h>

#include "vpu_common.h"
#include "vpu_enc_ctrls.h"
#include "vpu_enc_drv.h"
#include "v4l2_syna_externtion.h"

extern int vepu_debug;
#define vepu_dbg(level, fmt, arg...)                                           \
	v4l2_dbg(level, vepu_debug, &vpu->v4l2_dev, fmt, ##arg)
#define vepu_info(vpu, fmt, arg...) v4l2_info(&vpu->v4l2_dev, fmt, ##arg)
#define vepu_err(vpu, fmt, arg...) v4l2_err(&vpu->v4l2_dev, fmt, ##arg)

#define SYNA_V4L2_CID_H1_DS_W (V4L2_CID_USER_BASE + 0x1170)
#define SYNA_V4L2_CID_H1_DS_H (V4L2_CID_USER_BASE + 0x1171)
#define SYNA_V4L2_CID_H1_SIMULCAST (V4L2_CID_USER_BASE + 0x1172)

#define SYNA_LTR_MAX_CNT 6
#define SYNA_MAX_MB_NUM ((H1_MAX_W * H1_MAX_H) >> 8)
#define MBs(x) ((x + 15) / 16)

// LTR cmd:
// bit0~1: operation, bit2~bit4: use id, bit5~bit7: mark id.
#define VENC_LTR_CMD_MAKR(cmd, markId)                                         \
	do {                                                                   \
		cmd &= ~(0x7 << 5);                                            \
		cmd &= ~(0x3);                                                 \
		cmd |= (((markId & 0x7) << 5) | VENC_REFRESH);                 \
		cmd |= 0x80000000;                                             \
	} while (0)

#define VENC_LTR_CMD_USE(cmd, useId)                                           \
	do {                                                                   \
		cmd &= ~(0x7 << 2);                                            \
		cmd &= ~(0x3);                                                 \
		cmd |= (((useId & 0x7) << 2) | VENC_REFERENCE);                \
		cmd |= 0x80000000;                                             \
	} while (0)

enum vepu_ltr_cmd {
	VEPU_LTR_CMD_START,
	VEPU_LTR_CMD_MARK,
	VEPU_LTR_CMD_USE,
	VEPU_LTR_CMD_END
};

static struct {
	int sar_width;
	int sar_height;
} sar_idc_arrary[] = {
	/* V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_UNSPECIFIED */ { 0, 0 },
	/* V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_1x1 */ { 1, 1 },
	/* V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_12x11 */ { 12, 11 },
	/* V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_10x11 */ { 10, 11 },
	/* V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_16x11 */ { 16, 11 },
	/* V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_40x33 */ { 40, 33 },
	/* V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_24x11 */ { 24, 11 },
	/* V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_20x11 */ { 20, 11 },
	/* V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_32x11 */ { 32, 11 },
	/* V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_80x33 */ { 80, 33 },
	/* V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_18x11 */ { 18, 11 },
	/* V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_15x11 */ { 15, 11 },
	/* V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_64x33 */ { 64, 33 },
	/* V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_160x99 */ { 160, 99 },
	/* V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_4x3 */ { 4, 3 },
	/* V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_3x2 */ { 3, 2 },
	/* V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_2x1 */ { 2, 1 },
};

static int vidioc_venc_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct syna_vcodec_ctx *ctx;
	struct syna_vpu_dev *vpu;
	struct vpu_enc_ctrls *venc_ctrls;
	unsigned long tmp;
	int id;
	int ret = 0;

	ctx = container_of(ctrl->handler, struct syna_vcodec_ctx, ctrl_handler);
	vpu = ctx->vpu;
	venc_ctrls = &ctx->v4l2_ctrls;

	switch (ctrl->id) {
	case V4L2_CID_MPEG_VIDEO_GOP_SIZE:
		vepu_dbg(2, "V4L2_CID_MPEG_VIDEO_GOP_SIZE val = %d", ctrl->val);
		venc_ctrls->gop_size = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_BITRATE_MODE:
		vepu_dbg(2, "V4L2_CID_MPEG_VIDEO_BITRATE_MODE val = %d",
			 ctrl->val);
		venc_ctrls->bitrate_mode = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_BITRATE:
		vepu_dbg(2, "V4L2_CID_MPEG_VIDEO_BITRATE val = %d", ctrl->val);
		venc_ctrls->bitrate = ctrl->val;
		venc_ctrls->apply_dyna_ctrls = true;
		break;
	case V4L2_CID_MPEG_VIDEO_FRAME_RC_ENABLE:
		vepu_dbg(2, "V4L2_CID_MPEG_VIDEO_FRAME_RC_ENABLE val = %d",
			 ctrl->val);
		venc_ctrls->rc_en = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_AU_DELIMITER:
		vepu_dbg(2, "V4L2_CID_MPEG_VIDEO_AU_DELIMITER val = %d",
			 ctrl->val);
		venc_ctrls->au_delimiter = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MODE:
		vepu_dbg(2, "V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MODE val = %d",
			 ctrl->val);
		venc_ctrls->multi_slice_mode = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MAX_MB:
		vepu_dbg(2, "V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MAX_MB val = %d",
			 ctrl->val);
		if (venc_ctrls->multi_slice_mode !=
		    V4L2_MPEG_VIDEO_MULTI_SLICE_MODE_MAX_MB)
			break;
		venc_ctrls->multi_slice_max_mb = ctrl->val;
		venc_ctrls->apply_dyna_ctrls = true;
		break;
	case V4L2_CID_MPEG_VIDEO_CYCLIC_INTRA_REFRESH_MB:
		vepu_dbg(2,
			 "V4L2_CID_MPEG_VIDEO_CYCLIC_INTRA_REFRESH_MB val = %d",
			 ctrl->val);
		venc_ctrls->cyclic_intra_refresh_mb = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_VUI_EXT_SAR_WIDTH:
		vepu_dbg(2,
			 "V4L2_CID_MPEG_VIDEO_H264_VUI_EXT_SAR_WIDTH val = %d",
			 ctrl->val);
		venc_ctrls->h264_vui_ext_sar_width = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_VUI_EXT_SAR_HEIGHT:
		vepu_dbg(2,
			 "V4L2_CID_MPEG_VIDEO_H264_VUI_EXT_SAR_HEIGHT val = %d",
			 ctrl->val);
		venc_ctrls->h264_vui_ext_sar_height = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_VUI_SAR_ENABLE:
		vepu_dbg(2, "V4L2_CID_MPEG_VIDEO_H264_VUI_SAR_ENABLE val = %d",
			 ctrl->val);
		venc_ctrls->h264_vui_sar_en = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_VUI_SAR_IDC:
		vepu_dbg(2, "V4L2_CID_MPEG_VIDEO_H264_VUI_SAR_IDC val = %d",
			 ctrl->val);
		if (venc_ctrls->h264_vui_sar_en) {
			venc_ctrls->h264_vui_sar_idc = ctrl->val;
			switch (ctrl->val) {
			case V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_UNSPECIFIED:
			case V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_1x1:
			case V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_12x11:
			case V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_10x11:
			case V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_16x11:
			case V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_40x33:
			case V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_24x11:
			case V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_20x11:
			case V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_32x11:
			case V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_80x33:
			case V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_18x11:
			case V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_15x11:
			case V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_64x33:
			case V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_160x99:
			case V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_4x3:
			case V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_3x2:
			case V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_2x1:
				venc_ctrls->h264_vui_sar_width =
					sar_idc_arrary[ctrl->val].sar_width;
				venc_ctrls->H264_vui_sar_height =
					sar_idc_arrary[ctrl->val].sar_height;
				break;
			case V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_EXTENDED:
			default:
				break;
			}
		}
		break;
	case V4L2_CID_MPEG_VIDEO_H264_MIN_QP:
		vepu_dbg(2, "V4L2_CID_MPEG_VIDEO_H264_MIN_QP val = %d",
			 ctrl->val);
		venc_ctrls->h264_min_qp = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_VPX_MIN_QP:
		vepu_dbg(2, "V4L2_CID_MPEG_VIDEO_VPX_MIN_QP val = %d",
			 ctrl->val);
		venc_ctrls->vpx_min_qp = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_MAX_QP:
		vepu_dbg(2, "V4L2_CID_MPEG_VIDEO_H264_MAX_QP val = %d",
			 ctrl->val);
		venc_ctrls->h264_max_qp = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_VPX_MAX_QP:
		vepu_dbg(2, "V4L2_CID_MPEG_VIDEO_VPX_MAX_QP val = %d",
			 ctrl->val);
		venc_ctrls->vpx_max_qp = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_I_FRAME_QP:
		vepu_dbg(2, "V4L2_CID_MPEG_VIDEO_H264_I_FRAME_QP val = %d",
			 ctrl->val);
		if (ctrl->val && (ctrl->val < venc_ctrls->h264_min_qp ||
				  ctrl->val > venc_ctrls->h264_max_qp)) {
			ret = -ERANGE;
			break;
		}

		venc_ctrls->h264_i_frame_qp = ctrl->val;
		venc_ctrls->apply_dyna_ctrls = true;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_P_FRAME_QP:
		vepu_dbg(2, "V4L2_CID_MPEG_VIDEO_H264_P_FRAME_QP val = %d",
			 ctrl->val);
		if (ctrl->val && (ctrl->val < venc_ctrls->h264_min_qp ||
				  ctrl->val > venc_ctrls->h264_max_qp)) {
			ret = -ERANGE;
			break;
		}

		venc_ctrls->h264_p_frame_qp = ctrl->val;
		venc_ctrls->apply_dyna_ctrls = true;
		break;
	case V4L2_CID_MPEG_VIDEO_VPX_I_FRAME_QP:
		vepu_dbg(2, "V4L2_CID_MPEG_VIDEO_VPX_I_FRAME_QP val = %d",
			 ctrl->val);
		if (ctrl->val && (ctrl->val < venc_ctrls->vpx_min_qp ||
				  ctrl->val > venc_ctrls->vpx_max_qp)) {
			ret = -ERANGE;
			break;
		}

		venc_ctrls->vpx_i_frame_qp = ctrl->val;
		venc_ctrls->apply_dyna_ctrls = true;
		break;
	case V4L2_CID_MPEG_VIDEO_VPX_P_FRAME_QP:
		vepu_dbg(2, "V4L2_CID_MPEG_VIDEO_VPX_P_FRAME_QP val = %d",
			 ctrl->val);
		if (ctrl->val && (ctrl->val < venc_ctrls->vpx_min_qp ||
				  ctrl->val > venc_ctrls->vpx_max_qp)) {
			ret = -ERANGE;
			break;
		}

		venc_ctrls->vpx_p_frame_qp = ctrl->val;
		venc_ctrls->apply_dyna_ctrls = true;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_8X8_TRANSFORM:
		vepu_dbg(2, "V4L2_CID_MPEG_VIDEO_H264_8X8_TRANSFORM val = %d",
			 ctrl->val);
		venc_ctrls->h264_8x8_transform = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_ENTROPY_MODE:
		vepu_dbg(2, "V4L2_CID_MPEG_VIDEO_H264_ENTROPY_MODE val = %d",
			 ctrl->val);
		venc_ctrls->h264_entropy_mode = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_MB_RC_ENABLE:
		vepu_dbg(2, "V4L2_CID_MPEG_VIDEO_MB_RC_ENABLE val = %d",
			 ctrl->val);
		venc_ctrls->mb_rc_en = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_PROFILE:
		vepu_dbg(2, "V4L2_CID_MPEG_VIDEO_H264_PROFILE val = %d",
			 ctrl->val);
		venc_ctrls->h264_profile = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_LEVEL:
		vepu_dbg(2, "V4L2_CID_MPEG_VIDEO_H264_LEVEL val = %d",
			 ctrl->val);
		venc_ctrls->h264_level = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_VP8_PROFILE:
		vepu_dbg(2, "V4L2_CID_MPEG_VIDEO_VP8_PROFILE val = %d",
			 ctrl->val);
		venc_ctrls->vp8_profile = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_FORCE_KEY_FRAME:
		vepu_dbg(2, "V4L2_CID_MPEG_VIDEO_FORCE_KEY_FRAME");
		venc_ctrls->force_key_frame = 1;
		venc_ctrls->apply_dyna_ctrls = true;
		break;
	case V4L2_CID_MPEG_VIDEO_HEADER_MODE:
		vepu_dbg(2, "V4L2_CID_MPEG_VIDEO_HEADER_MODE val = %d",
			 ctrl->val);
		venc_ctrls->header_mode = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_I_PERIOD:
		vepu_dbg(2, "V4L2_CID_MPEG_VIDEO_H264_I_PERIOD val = %d",
			 ctrl->val);
		break;
	case V4L2_CID_MPEG_VIDEO_LTR_COUNT:
		vepu_dbg(2, "V4L2_CID_MPEG_VIDEO_LTR_COUNT val = %d",
			 ctrl->val);
		if (venc_ctrls->ltr_ctrls.ltr_cnt == 0 && ctrl->val)
			venc_ctrls->ltr_ctrls.ltr_cnt = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_FRAME_LTR_INDEX:
		vepu_dbg(2, "V4L2_CID_MPEG_VIDEO_FRAME_LTR_INDEX val = %d",
			 ctrl->val);
		if (!venc_ctrls->ltr_ctrls.ltr_cnt)
			break;

		if (ctrl->val >= venc_ctrls->ltr_ctrls.ltr_cnt) {
			ret = -ERANGE;
			break;
		}

		venc_ctrls->ltr_ctrls.cmd[0] = VEPU_LTR_CMD_MARK;

		venc_ctrls->ltr_ctrls.mark_id = ctrl->val;
		venc_ctrls->apply_dyna_ctrls = true;
		break;
	case V4L2_CID_MPEG_VIDEO_USE_LTR_FRAMES:
		vepu_dbg(2, "V4L2_CID_MPEG_VIDEO_USE_LTR_FRAMES val = %d",
			 ctrl->val);
		if (!venc_ctrls->ltr_ctrls.ltr_cnt || !ctrl->val)
			break;

		tmp = ctrl->val;
		id = find_first_bit(&tmp, venc_ctrls->ltr_ctrls.ltr_cnt);
		if (id == venc_ctrls->ltr_ctrls.ltr_cnt) {
			ret = -ERANGE;
			break;
		}

		if (!test_bit(id, &venc_ctrls->ltr_ctrls.ltr_usage_mask)) {
			ret = -EINVAL;
			break;
		}

		venc_ctrls->ltr_ctrls.cmd[1] = VEPU_LTR_CMD_USE;

		venc_ctrls->ltr_ctrls.use_id = id;
		venc_ctrls->apply_dyna_ctrls = true;
		break;
	case SYNA_V4L2_CID_H1_DS_W:
		vepu_dbg(2, "SYNA_V4L2_CID_H1_DS_W val = %d", ctrl->val);
		if (ctx->enc_params->frm_width &&
		    ctrl->val >= ctx->enc_params->frm_width) {
			ret = -EINVAL;
			break;
		}
		venc_ctrls->syna_h1_ds_w = ctrl->val;
		break;
	case SYNA_V4L2_CID_H1_DS_H:
		vepu_dbg(2, "SYNA_V4L2_CID_H1_DS_H val = %d", ctrl->val);
		if (ctx->enc_params->frm_height &&
		    ctrl->val >= ctx->enc_params->frm_height) {
			ret = -EINVAL;
			break;
		}
		venc_ctrls->syna_h1_ds_h = ctrl->val;
		break;
	case SYNA_V4L2_CID_H1_SIMULCAST:
		vepu_dbg(2, "SYNA_V4L2_CID_H1_SIMULCAST val = %d", ctrl->val);
		venc_ctrls->syna_h1_simulcast = ctrl->val;
		break;
	case SYNA_V4L2_CID_VENC_ENABLE_METADATA_MV:
		vepu_dbg(2, "SYNA_V4L2_CID_VENC_ENABLE_METADATA_MV val = %d", ctrl->val);
		venc_ctrls->metadata_mv_enable = ctrl->val;
		break;
	default:
		vepu_dbg(2, "unknown ctrl id = %d(%s)\n", ctrl->id,
			 v4l2_ctrl_get_name(ctrl->id));
		ret = -EINVAL;
		break;
	}

	if (ret)
		vepu_err(vpu, "[%s():%d], id:%s, val:%d fail, ret:%d\n",
			 __func__, __LINE__, v4l2_ctrl_get_name(ctrl->id),
			 ctrl->val, ret);

	return ret;
}

static const struct v4l2_ctrl_ops vpu_enc_ctrl_ops = {
	.s_ctrl = vidioc_venc_s_ctrl,
};

static const struct v4l2_ctrl_config h1_ds_w_cfg = {
	.ops = &vpu_enc_ctrl_ops,
	.id = SYNA_V4L2_CID_H1_DS_W,
	.name = "Width of SYNA H1 downscale output",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min = 0,
	.max = H1_MAX_W,
	.step = MB_DIM,
	.def = 0,
};

static const struct v4l2_ctrl_config h1_ds_h_cfg = {
	.ops = &vpu_enc_ctrl_ops,
	.id = SYNA_V4L2_CID_H1_DS_H,
	.name = "Height of SYNA H1 downscale output",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min = 0,
	.max = H1_MAX_H,
	.step = MB_DIM,
	.def = 0,
};

static const struct v4l2_ctrl_config h1_simulcast_cfg = {
	.ops = &vpu_enc_ctrl_ops,
	.id = SYNA_V4L2_CID_H1_SIMULCAST,
	.name = "SYNA H1 simulcast output",
	.type = V4L2_CTRL_TYPE_BOOLEAN,
	.min = 0,
	.max = 1,
	.step = 1,
	.def = 0,
};

static const struct v4l2_ctrl_config h1_metadata_mv_cfg = {
	.ops = &vpu_enc_ctrl_ops,
	.id = SYNA_V4L2_CID_VENC_ENABLE_METADATA_MV,
	.name = "SYNA H1 metadata enable MV",
	.type = V4L2_CTRL_TYPE_BOOLEAN,
	.min = 0,
	.max = 1,
	.step = 1,
	.def = 0,
};

int vpu_enc_ctrls_init(struct v4l2_ctrl_handler *handler)
{
	const struct v4l2_ctrl_ops *ops = &vpu_enc_ctrl_ops;

	v4l2_ctrl_handler_init(handler, 32);

	v4l2_ctrl_new_std(handler, ops, V4L2_CID_MIN_BUFFERS_FOR_OUTPUT, 1, 1,
			  1, 1);
	v4l2_ctrl_new_std(handler, ops, V4L2_CID_MPEG_VIDEO_H264_8X8_TRANSFORM,
			  0, 1, 1, 0);
	v4l2_ctrl_new_std(handler, ops, V4L2_CID_MPEG_VIDEO_BITRATE,
			  SYNA_VENC_BITRATE_MIN, SYNA_VENC_BITRATE_MAX, 1,
			  40000);
	v4l2_ctrl_new_std(handler, ops, V4L2_CID_MPEG_VIDEO_FRAME_RC_ENABLE, 0,
			  1, 1, 1);
	v4l2_ctrl_new_std(handler, ops, V4L2_CID_MPEG_VIDEO_AU_DELIMITER, 0, 1,
			  1, 0);
	v4l2_ctrl_new_std(handler, ops,
			  V4L2_CID_MPEG_VIDEO_H264_VUI_EXT_SAR_WIDTH, 0, 65535,
			  1, 0);
	v4l2_ctrl_new_std(handler, ops,
			  V4L2_CID_MPEG_VIDEO_H264_VUI_EXT_SAR_HEIGHT, 0, 65535,
			  1, 0);
	v4l2_ctrl_new_std(handler, ops, V4L2_CID_MPEG_VIDEO_H264_VUI_SAR_ENABLE,
			  0, 1, 1, 0);
	v4l2_ctrl_new_std(handler, ops, V4L2_CID_MPEG_VIDEO_H264_MIN_QP, 0, 51,
			  1, 0);
	v4l2_ctrl_new_std(handler, ops, V4L2_CID_MPEG_VIDEO_H264_MAX_QP, 0, 51,
			  1, 51);
	v4l2_ctrl_new_std(handler, ops, V4L2_CID_MPEG_VIDEO_H264_I_FRAME_QP, 0,
			  51, 1, 0);
	v4l2_ctrl_new_std(handler, ops, V4L2_CID_MPEG_VIDEO_H264_P_FRAME_QP, 0,
			  51, 1, 0);
	v4l2_ctrl_new_std(handler, ops, V4L2_CID_MPEG_VIDEO_VPX_MIN_QP, 0,
			  127, 1, 0);
	v4l2_ctrl_new_std(handler, ops, V4L2_CID_MPEG_VIDEO_VPX_MAX_QP, 0,
			  127, 1, 127);
	v4l2_ctrl_new_std(handler, ops, V4L2_CID_MPEG_VIDEO_VPX_I_FRAME_QP, 0,
			  127, 1, 0);
	v4l2_ctrl_new_std(handler, ops, V4L2_CID_MPEG_VIDEO_VPX_P_FRAME_QP, 0,
			  127, 1, 0);
	v4l2_ctrl_new_std(handler, ops, V4L2_CID_MPEG_VIDEO_GOP_SIZE, 0, 65535,
			  1, 30);
	v4l2_ctrl_new_std(handler, ops, V4L2_CID_MPEG_VIDEO_MB_RC_ENABLE, 0, 1,
			  1, 0);
	v4l2_ctrl_new_std(handler, ops, V4L2_CID_MPEG_VIDEO_FORCE_KEY_FRAME, 0,
			  1, 0, 0);
	v4l2_ctrl_new_std(handler, ops, V4L2_CID_MPEG_VIDEO_LTR_COUNT, 0,
			  SYNA_LTR_MAX_CNT, 1, 0);
	v4l2_ctrl_new_std(handler, ops, V4L2_CID_MPEG_VIDEO_FRAME_LTR_INDEX, 0,
			  SYNA_LTR_MAX_CNT - 1, 1, 0);
	v4l2_ctrl_new_std(handler, ops, V4L2_CID_MPEG_VIDEO_USE_LTR_FRAMES, 0,
			  (0x1 << SYNA_LTR_MAX_CNT) - 1, 0, 0);
	v4l2_ctrl_new_std(handler, ops, V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MAX_MB,
			  0, SYNA_MAX_MB_NUM, 1, 0);
	v4l2_ctrl_new_std(handler, ops,
			  V4L2_CID_MPEG_VIDEO_CYCLIC_INTRA_REFRESH_MB, 0,
			  SYNA_MAX_MB_NUM, 1, 0);

	v4l2_ctrl_new_std_menu(handler, ops,
			       V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MODE,
			       V4L2_MPEG_VIDEO_MULTI_SLICE_MODE_MAX_BYTES,
			       ~(0x3), V4L2_MPEG_VIDEO_MULTI_SLICE_MODE_SINGLE);

	v4l2_ctrl_new_std_menu(handler, ops,
			       V4L2_CID_MPEG_VIDEO_H264_VUI_SAR_IDC,
			       V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_EXTENDED, 0,
			       V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_UNSPECIFIED);

	v4l2_ctrl_new_std_menu(handler, ops, V4L2_CID_MPEG_VIDEO_BITRATE_MODE,
			       V4L2_MPEG_VIDEO_BITRATE_MODE_CBR, 0,
			       V4L2_MPEG_VIDEO_BITRATE_MODE_CBR);

	v4l2_ctrl_new_std_menu(
		handler, ops, V4L2_CID_MPEG_VIDEO_HEADER_MODE,
		V4L2_MPEG_VIDEO_HEADER_MODE_JOINED_WITH_1ST_FRAME, 0,
		V4L2_MPEG_VIDEO_HEADER_MODE_JOINED_WITH_1ST_FRAME);

	v4l2_ctrl_new_std_menu(handler, ops,
			       V4L2_CID_MPEG_VIDEO_H264_ENTROPY_MODE,
			       V4L2_MPEG_VIDEO_H264_ENTROPY_MODE_CABAC, 0,
			       V4L2_MPEG_VIDEO_H264_ENTROPY_MODE_CAVLC);

	v4l2_ctrl_new_std_menu(handler, ops, V4L2_CID_MPEG_VIDEO_H264_PROFILE,
			       V4L2_MPEG_VIDEO_H264_PROFILE_HIGH, 0x2,
			       V4L2_MPEG_VIDEO_H264_PROFILE_HIGH);

	v4l2_ctrl_new_std_menu(handler, ops, V4L2_CID_MPEG_VIDEO_H264_LEVEL,
			       V4L2_MPEG_VIDEO_H264_LEVEL_5_1, 0,
			       V4L2_MPEG_VIDEO_H264_LEVEL_4_0);

	v4l2_ctrl_new_std_menu(handler, ops, V4L2_CID_MPEG_VIDEO_VP8_PROFILE,
			       V4L2_MPEG_VIDEO_VP8_PROFILE_0, 0,
			       V4L2_MPEG_VIDEO_VP8_PROFILE_0);

	v4l2_ctrl_new_custom(handler, &h1_ds_w_cfg, NULL);
	v4l2_ctrl_new_custom(handler, &h1_ds_h_cfg, NULL);
	v4l2_ctrl_new_custom(handler, &h1_simulcast_cfg, NULL);

	v4l2_ctrl_new_custom(handler, &h1_metadata_mv_cfg, NULL);

	if (handler->error)
		return handler->error;

	v4l2_ctrl_handler_setup(handler);

	return 0;
}

void vpu_enc_ctrls_deinit(struct v4l2_ctrl_handler *handler)
{
	v4l2_ctrl_handler_free(handler);
}

/* Encoder slice size is in unit of MB row. */
static void v4l2_ctrls_truncate_slice_size(struct vpu_enc_ctrls *enc_ctrls,
					   u32 frm_width)
{
	if (!enc_ctrls->multi_slice_max_mb)
		return;

	if (enc_ctrls->multi_slice_max_mb < (frm_width >> 4))
		enc_ctrls->multi_slice_max_mb = 1;
	else
		enc_ctrls->multi_slice_max_mb /= (frm_width >> 4);
}

static void v4l2_ctrls_transfer_profile_level(struct vpu_enc_ctrls *enc_ctrls,
					      u32 frm_width, u32 frm_height)
{
	u32 min_level;
	u32 macro_blocks;

	switch (enc_ctrls->h264_profile) {
	case V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE:
		enc_ctrls->h264_profile = VENC_PROFILE_BASE;
		break;
	case V4L2_MPEG_VIDEO_H264_PROFILE_MAIN:
		enc_ctrls->h264_profile = VENC_PROFILE_MAIN;
		break;
	case V4L2_MPEG_VIDEO_H264_PROFILE_HIGH:
		enc_ctrls->h264_profile = VENC_PROFILE_HIGH;
		break;
	default:
		enc_ctrls->h264_profile = VENC_PROFILE_BASE;
		break;
	}

	if (enc_ctrls->h264_level >= V4L2_MPEG_VIDEO_H264_LEVEL_5_0) {
		enc_ctrls->h264_level -= V4L2_MPEG_VIDEO_H264_LEVEL_5_0;
		enc_ctrls->h264_level += VENC_LEVEL_5_0;
	} else if (enc_ctrls->h264_level >= V4L2_MPEG_VIDEO_H264_LEVEL_4_0) {
		enc_ctrls->h264_level -= V4L2_MPEG_VIDEO_H264_LEVEL_4_0;
		enc_ctrls->h264_level += VENC_LEVEL_4_0;
	} else if (enc_ctrls->h264_level >= V4L2_MPEG_VIDEO_H264_LEVEL_3_0) {
		enc_ctrls->h264_level -= V4L2_MPEG_VIDEO_H264_LEVEL_3_0;
		enc_ctrls->h264_level += VENC_LEVEL_3_0;
	} else if (enc_ctrls->h264_level >= V4L2_MPEG_VIDEO_H264_LEVEL_2_0) {
		enc_ctrls->h264_level -= V4L2_MPEG_VIDEO_H264_LEVEL_2_0;
		enc_ctrls->h264_level += VENC_LEVEL_2_0;
	} else {
		enc_ctrls->h264_level += VENC_LEVEL_1_0;
	}

	macro_blocks = MBs(frm_width) * MBs(frm_height);

	if (macro_blocks > 22080)
		min_level = VENC_LEVEL_5_1;
	else if (macro_blocks > 8704)
		min_level = VENC_LEVEL_5_0;
	else if (macro_blocks > 8192)
		min_level = VENC_LEVEL_4_2;
	else if (macro_blocks > 5120)
		min_level = VENC_LEVEL_4_0;
	else if (macro_blocks > 3600)
		min_level = VENC_LEVEL_3_2;
	else if (macro_blocks > 1620)
		min_level = VENC_LEVEL_3_1;
	else if (macro_blocks > 792)
		min_level = VENC_LEVEL_2_2;
	else if (macro_blocks > 396)
		min_level = VENC_LEVEL_2_1;
	else if (macro_blocks > 99)
		min_level = VENC_LEVEL_1_1;
	else
		min_level = VENC_LEVEL_1_0;

	if (enc_ctrls->h264_level < min_level)
		enc_ctrls->h264_level = min_level;
}

static int v4l2_ctrls_to_h1_strm_config(struct vpu_enc_ctrls *enc_ctrls,
					u32 codec_fmt, u32 frm_width,
					struct syna_venc_strm_config *config)
{
	v4l2_ctrls_truncate_slice_size(enc_ctrls, frm_width);
	v4l2_ctrls_transfer_profile_level(enc_ctrls, config->frm_width,
					  config->frm_height);

	config->gop_size = enc_ctrls->gop_size;
	config->bitPerSecond = enc_ctrls->bitrate;
	config->sliceSize = enc_ctrls->multi_slice_max_mb;
	config->pictureRc = enc_ctrls->rc_en;
	config->mbRc = enc_ctrls->mb_rc_en;
	config->cirInterval = enc_ctrls->cyclic_intra_refresh_mb;
	if (enc_ctrls->ltr_ctrls.ltr_cnt) {
		config->ref_num_in_buf = enc_ctrls->ltr_ctrls.ltr_cnt + 1;
		// LTR require cabac and higher profile and level.
		// enable transform_8x8 to increase profile.
		config->entropy_mode = 1;
		config->transform_8x8_flag = 1;
		enc_ctrls->h264_level = VENC_LEVEL_5_1;
	} else {
		config->transform_8x8_flag = enc_ctrls->h264_8x8_transform;
		config->entropy_mode = enc_ctrls->h264_entropy_mode;
	}

	config->aud_on = enc_ctrls->au_delimiter;
	if (enc_ctrls->h264_vui_sar_idc ==
	    V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_EXTENDED) {
		config->sar_width = enc_ctrls->h264_vui_ext_sar_width;
		config->sar_height = enc_ctrls->h264_vui_ext_sar_height;
	} else {
		config->sar_width = enc_ctrls->h264_vui_sar_width;
		config->sar_height = enc_ctrls->H264_vui_sar_height;
	}

	if (codec_fmt == V4L2_PIX_FMT_H264) {
		config->qpMin = enc_ctrls->h264_min_qp;
		config->qpMax = enc_ctrls->h264_max_qp;
		config->profile_idc = enc_ctrls->h264_profile;
		config->level_idc = enc_ctrls->h264_level;
	} else if (codec_fmt == V4L2_PIX_FMT_VP8) {
		config->qpMin = enc_ctrls->vpx_min_qp;
		config->qpMax = enc_ctrls->vpx_max_qp;
		config->profile_idc = enc_ctrls->vp8_profile;
	}

	if (enc_ctrls->syna_h1_ds_w && enc_ctrls->syna_h1_ds_h) {
		config->ds_width = enc_ctrls->syna_h1_ds_w;
		config->ds_height = enc_ctrls->syna_h1_ds_h;
		config->simulcast = enc_ctrls->syna_h1_simulcast;
	}

	enc_ctrls->apply_dyna_ctrls = false;
	config->outputMbInfo = enc_ctrls->metadata_mv_enable;

	return 0;
}

static int vpu_enc_ctrls_to_h1_2ndpass(struct vpu_enc_ctrls *enc_ctrls,
				       u32 codec_fmt, u32 pic_seq,
				       u32 frm_width,
				       struct syna_venc_2ndpass_param *para)
{
	bool i_frame = false;

	v4l2_ctrls_truncate_slice_size(enc_ctrls, frm_width);

	if (enc_ctrls->force_key_frame) {
		para->pic_type = VENC_PIC_TYPE_FORCE_IDR;
		i_frame = true;
		enc_ctrls->force_key_frame = 0;
	} else if (enc_ctrls->gop_size) {
		i_frame = (pic_seq % enc_ctrls->gop_size == 0);
	}

	if (enc_ctrls->bitrate) {
		para->bitPerSecond = enc_ctrls->bitrate;
		enc_ctrls->bitrate = 0;
	}

	if (enc_ctrls->multi_slice_max_mb) {
		para->sliceSize = enc_ctrls->multi_slice_max_mb;
		enc_ctrls->multi_slice_max_mb = 0;
	}

	switch (enc_ctrls->bitrate_mode) {
	case V4L2_MPEG_VIDEO_BITRATE_MODE_VBR:
		if (i_frame)
			para->pic_qp = codec_fmt == V4L2_PIX_FMT_H264 ?
						     enc_ctrls->h264_i_frame_qp :
						     enc_ctrls->vpx_i_frame_qp;
		else
			para->pic_qp = codec_fmt == V4L2_PIX_FMT_H264 ?
						     enc_ctrls->h264_p_frame_qp :
						     enc_ctrls->vpx_p_frame_qp;
		break;
	case V4L2_MPEG_VIDEO_BITRATE_MODE_CBR:
		if (i_frame) {
			para->pic_qp = codec_fmt == V4L2_PIX_FMT_H264 ?
						     enc_ctrls->h264_i_frame_qp :
						     enc_ctrls->vpx_i_frame_qp;
			enc_ctrls->h264_i_frame_qp = 0;
			enc_ctrls->vpx_i_frame_qp = 0;
		} else {
			para->pic_qp = codec_fmt == V4L2_PIX_FMT_H264 ?
						     enc_ctrls->h264_p_frame_qp :
						     enc_ctrls->vpx_p_frame_qp;
			enc_ctrls->h264_p_frame_qp = 0;
			enc_ctrls->vpx_p_frame_qp = 0;
		}
		break;
	default:
		break;
	}

	if (enc_ctrls->ltr_ctrls.cmd[0]) {
		VENC_LTR_CMD_MAKR(para->ltrf, enc_ctrls->ltr_ctrls.mark_id);
		enc_ctrls->ltr_ctrls.cmd[0] = 0;
	}

	if (enc_ctrls->ltr_ctrls.cmd[1]) {
		VENC_LTR_CMD_USE(para->ltrf, enc_ctrls->ltr_ctrls.use_id);
		enc_ctrls->ltr_ctrls.cmd[1] = 0;
	}

	enc_ctrls->apply_dyna_ctrls = false;

	return 0;
}

static int vpu_enc_ctrls_to_h1_ext(struct vpu_enc_ctrls *enc_ctrls,
				   struct h1_ext_cfg *ext_cfg)
{
	ext_cfg->header_mode = enc_ctrls->header_mode;

	return 0;
}

int vpu_enc_ctrls_v4l2_to_h1(struct vpu_enc_ctrls *enc_ctrls, u32 codec_fmt,
			     u32 pic_seq, u32 frm_width,
			     struct syna_venc_strm_config *config,
			     struct syna_venc_2ndpass_param *para,
			     struct h1_ext_cfg *ext_cfg)
{
	int ret = 0;

	if (config) {
		ret = v4l2_ctrls_to_h1_strm_config(enc_ctrls, codec_fmt,
						   frm_width, config);
		if (ret)
			return ret;
	}

	if (para) {
		ret = vpu_enc_ctrls_to_h1_2ndpass(enc_ctrls, codec_fmt, pic_seq,
						  frm_width, para);
		if (ret)
			return ret;
	}

	if (ext_cfg) {
		ret = vpu_enc_ctrls_to_h1_ext(enc_ctrls, ext_cfg);
		if (ret)
			return ret;
	}

	return 0;
}

int vpu_enc_ctrls_h1_internal(struct syna_venc_strm_config *config)
{
	config->intraQpDelta = -5;
	config->quarter_mv = 1;
	/* __RCGOP_IP IP... only */
	config->gop_type = 1;
	/* If on, just output glolal sps/pps */
	config->avc_on = 1;
	if (config->format == SYNA_H1_ENC_H264)
		config->qpHdr = 26;
	else if (config->format == SYNA_H1_ENC_VP8)
		config->qpHdr = -1;

	return 0;
}

int vpu_enc_ctrls_update_ltr_usage(struct syna_venc_2ndpass_param *para,
				   unsigned long *ltr_usage_mask)
{
	int mark_id;

	if (para->ltrf & 0x2) {
		mark_id = (para->ltrf >> 5) & 0x7;
		set_bit(mark_id, ltr_usage_mask);
	}

	return 0;
}
