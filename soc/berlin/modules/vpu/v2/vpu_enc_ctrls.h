/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (C) 2022 - 2023 Synaptics Incorporated.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __VPU_ENC_CTRLS_H__
#define __VPU_ENC_CTRLS_H__

#include <linux/types.h>
#include <media/v4l2-ctrls.h>

#include "vpu_fw_data.h"

struct ltr_ctrls {
	u32 cmd[2];
	u32 ltr_cnt;
	u32 mark_id;
	u32 use_id;
	unsigned long ltr_usage_mask;
};

struct vpu_enc_ctrls {
	u32 apply_dyna_ctrls;

	u32 bitrate_mode;
	u32 bitrate;
	u32 gop_size;
	u32 rc_en;
	u32 mb_rc_en;
	u32 force_key_frame;
	u32 header_mode;

	u32 au_delimiter;
	u32 h264_profile;
	u32 h264_level;
	u32 h264_entropy_mode;
	u32 h264_8x8_transform;
	u32 h264_min_qp;
	u32 h264_max_qp;
	u32 h264_i_frame_qp;
	u32 h264_p_frame_qp;
	u32 h264_vui_ext_sar_width;
	u32 h264_vui_ext_sar_height;
	u32 h264_vui_sar_en;
	u32 h264_vui_sar_idc;
	u32 h264_vui_sar_width;
	u32 H264_vui_sar_height;

	u32 vp8_profile;
	u32 vpx_min_qp;
	u32 vpx_max_qp;
	u32 vpx_i_frame_qp;
	u32 vpx_p_frame_qp;

	struct ltr_ctrls ltr_ctrls;
	u32 multi_slice_mode;
	u32 multi_slice_max_mb;
	u32 cyclic_intra_refresh_mb;
	u32 syna_h1_ds_w;
	u32 syna_h1_ds_h;
	bool syna_h1_simulcast;
	bool metadata_mv_enable;
};

int vpu_enc_ctrls_init(struct v4l2_ctrl_handler *handler);
void vpu_enc_ctrls_deinit(struct v4l2_ctrl_handler *handler);

int vpu_enc_ctrls_v4l2_to_h1(struct vpu_enc_ctrls *enc_ctrls, u32 codec_fmt,
			     u32 pic_seq, u32 frm_width,
			     struct syna_venc_strm_config *config,
			     struct syna_venc_2ndpass_param *para,
			     struct h1_ext_cfg *ext_cfg);
int vpu_enc_ctrls_adjust_default(struct vpu_enc_ctrls *enc_ctrls,
				 u32 codec_fmt);
int vpu_enc_ctrls_h1_internal(struct syna_venc_strm_config *config);
int vpu_enc_ctrls_update_ltr_usage(struct syna_venc_2ndpass_param *para,
				   unsigned long *ltr_usage_mask);

#endif // __VPU_ENC_CTRLS_H__
