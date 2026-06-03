// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 - 2023 Synaptics Incorporated
 * Author: Hsia-Jun(Randy) Li <randy.li@synaptics.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <media/v4l2-event.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-dma-contig.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>

#include <vpu_mfd.h>
#include "vpu_common.h"
#include "vpu_dmaheap.h"
#include "vpu_srv.h"
#include "vpu_fw.h"
#include "vpu_fw_data.h"
#include "vpu_kernel_compatibility_wrap.h"
#include "vpu_enc_drv.h"
#include "vpu_enc_ctrls.h"

/* NOTE: one second is not enough, how comes */
#define ENC_H1_TIMEOUT_DELAY	(2000)
#define SYNA_DEFAULT_FRAMERATE_NUM	(60000U)
#define SYNA_DEFAULT_FRAMERATE_DENOM	(1001U)
#define SYNA_DEFAULT_REFER_NUM		2
#define SYNA_FRAME_BUF_MIN_NUM 2U
#define SYNA_FRAME_BUF_MAX_NUM 12U
#define SYNA_ES_BUF_MIN_NUM (SYNA_FRAME_BUF_MIN_NUM << 1)
#define SYNA_ES_BUF_MAX_NUM (SYNA_FRAME_BUF_MAX_NUM << 1)
#define SYNA_ES_BUF_SIZE_H1 (2 * 1024 * 1024)

#define SYNA_H1_ENC_NAME "berlin-h1-venc"
#define SYNA_HANTRO_H1_STR "berlin-hantro-h1"

#define TEST_BIT(bit_idx, mask) ((0x1 << (bit_idx)) & (mask))
#define SET_BIT(bit_idx, mask) ((mask) |= (0x1 << (bit_idx)))
#define CLR_BIT(bit_idx, mask) ((mask) &= ~(0x1 << (bit_idx)))

#define vepu_dbg(level, fmt, arg...)	\
		v4l2_dbg(level, vepu_debug, &vpu->v4l2_dev, fmt, ##arg)
#define vepu_info(vpu, fmt, arg...)	\
		v4l2_info(&vpu->v4l2_dev, fmt, ##arg)
#define vepu_err(vpu, fmt, arg...)	\
		v4l2_err(&vpu->v4l2_dev, fmt, ##arg)

int vepu_debug = 0;
module_param(vepu_debug, int, 0660);
MODULE_PARM_DESC(vepu_debug, "video encoder debug level");

static const struct v4l2_event enc_eos_event = {
	.type = V4L2_EVENT_EOS
};

static const struct v4l2_frmsize_stepwise syna_h1_framesizes = {
	H1_MIN_W, H1_MAX_W, 2,
	H1_MIN_H, H1_MAX_H, 2,
};

static const struct hantro_fmt syna_venc_fmts[] = {
	{
	 .fourcc = V4L2_PIX_FMT_YUV420,
	 .codec_mode = HANTRO_MODE_NONE,
	 .enc_fmt = VENC_YUV420_PLANE,
	 .version = SYNA_VPU_ENC_HW,
	 },
	{
	 .fourcc = V4L2_PIX_FMT_YUV420M,
	 .codec_mode = HANTRO_MODE_NONE,
	 .enc_fmt = VENC_YUV420_PLANE,
	 .version = SYNA_VPU_ENC_HW,
	 },
	{
	 .fourcc = V4L2_PIX_FMT_NV12M,
	 .codec_mode = HANTRO_MODE_NONE,
	 .enc_fmt = VENC_YUV420_SP,
	 .version = SYNA_VPU_ENC_HW,
	 },
	{
	 .fourcc = V4L2_PIX_FMT_YUYV,
	 .codec_mode = HANTRO_MODE_NONE,
	 .enc_fmt = VENC_YUV422_YUYV,
	 .version = SYNA_VPU_ENC_HW,
	 },
	{
	 .fourcc = V4L2_PIX_FMT_H264,
	 .codec_mode = HANTRO_MODE_H264_ENC,
	 .enc_fmt = SYNA_H1_ENC_H264,
	 .version = SYNA_VPU_ENC_HW,
	 .max_depth = 1,
	 .frmsize = {
		     .min_width = H1_MIN_W,
		     .max_width = H1_MAX_W,
		     .step_width = MB_DIM,
		     .min_height = H1_MIN_H,
		     .max_height = H1_MAX_H,
		     .step_height = MB_DIM,
		     },
	 },
	{
	 .fourcc = V4L2_PIX_FMT_VP8,
	 .codec_mode = HANTRO_MODE_VP8_ENC,
	 .enc_fmt = SYNA_H1_ENC_VP8,
	 .version = SYNA_DOLPHIN_BIT,
	 .max_depth = 1,
	 .frmsize = {
		     .min_width = H1_MIN_W,
		     .max_width = H1_MAX_W,
		     .step_width = MB_DIM,
		     .min_height = H1_MIN_H,
		     .max_height = H1_MAX_H,
		     .step_height = MB_DIM,
		     },
	 },
};

#define VNEC_NUM_FORMATS	ARRAY_SIZE(syna_venc_fmts)

/* return positive number and zero are good */
static int vepu_alloc_recon_buf(struct syna_vcodec_ctx *ctx)
{
	struct syna_vpu_dev *vpu = ctx->vpu;
	struct syna_vpu_tz_buf *vpu_buf;
	struct v4l2_pix_format_mplane pix_mp;
	struct vb2_queue *dst_vq;
	struct vb2_buffer *vb;
	dma_addr_t memid;
	size_t size;
	int ref_buf_num;
	int ret;
	int i;

	if (test_bit(SYNA_VPU_POOL_ON_AUX, &ctx->status))
		return ctx->n_recon_pool;

	/* NOTE: it is a tile format */
	ret = v4l2_fill_pixfmt_mp(&pix_mp, V4L2_PIX_FMT_NV12,
				  ctx->enc_params->src_width,
				  ctx->enc_params->src_height);
	if (ret)
		return ret;

	size = 0;
	for (i = 0; i < pix_mp.num_planes; i++)
		size += pix_mp.plane_fmt[i].sizeimage;

	/* NOTE: need this append memory to meet HW requirement, 1920x1080, 1280x720.. */
	size = ALIGN(size, SZ_4K) + SZ_32K;

	dst_vq = v4l2_m2m_get_dst_vq(ctx->fh.m2m_ctx);
	ref_buf_num = SYNA_DEFAULT_REFER_NUM;
	if (ctx->v4l2_ctrls.ltr_ctrls.ltr_cnt)
		ref_buf_num += ctx->v4l2_ctrls.ltr_ctrls.ltr_cnt;

	for (i = 0; i < ref_buf_num; i++) {
		vb = &(ctx->recon_buffers[i]);
		vb->planes[0].mem_priv = syna_dh_bm_alloc(cma_dh_dev, size);
		if (IS_ERR_OR_NULL(vb->planes[0].mem_priv)) {
			ret = PTR_ERR(vb->planes[0].mem_priv);
			vepu_err(vpu, "can't allocate aux buffer: 0x%08x\n",
				 ret);
			goto failed;
		}
		vb->planes[0].length = size;
		vb->num_planes = 1;
		/* NOTE: dst_vq is a fake queue for recon vb, assign a real queue if
		   v4l2 framework support more then 2 queues */
		vb->vb2_queue = dst_vq;
		ctx->n_recon_pool++;
	}

	for (i = 0; i < ctx->n_recon_pool; i++) {
		vb = &(ctx->recon_buffers[i]);

		vpu_buf = ctx->aux_pool;
		vpu_buf += i;

		memset(vpu_buf->planes, 0,
		       sizeof(struct syna_tz_generic_buf) << 3);

		vpu_buf->buf_type = ENCODER_PICTURE;
		memid = syna_vpu_bm_dh_plane_memid(vb, 0);
		vpu_buf->planes[0].type = CONTIGUOUS_MEM;
		vpu_buf->planes[0].memid = memid;
		vpu_buf->planes[0].size = vb2_plane_size(vb, 0);
		vpu_buf->planes[0].gfp_flags = BERLIN_GFP_FLAG_NO_MAPPING;
		ret = syna_vpu_register_buf(ctx, SYNA_VPU_FW_VENC_RECON, i);

		if (ret) {
			vepu_err(vpu,
				 "can't register aux buffer 0x%llx: 0x%08x\n",
				 vpu_buf->planes[0].addr, ret);
			goto failed;
		}
	}

	set_bit(SYNA_VPU_POOL_ON_AUX, &ctx->status);

	return ctx->n_recon_pool;
failed:
	for (i = ctx->n_recon_pool - 1; i >= 0; i--) {
		vb = &(ctx->recon_buffers[i]);
		vb2_syna_dh_bm_put(vb->planes[i].mem_priv);
		vb->planes[i].mem_priv = NULL;
	}
	ctx->n_recon_pool = 0;
	return ret;
}

static void vepu_release_recon_buf(struct syna_vcodec_ctx *ctx)
{
	struct syna_vpu_dev *vpu = ctx->vpu;
	struct vb2_buffer *vb;
	int ret;
	int i;

	if (!test_bit(SYNA_VPU_POOL_ON_AUX, &ctx->status))
		return;

	for (i = ctx->n_recon_pool - 1; i >= 0; i--) {
		vb = &(ctx->recon_buffers[i]);

		ret = syna_vpu_unregister_buf(ctx, SYNA_VPU_FW_VENC_RECON, i);
		if (ret)
			vepu_err(vpu, "can't release an aux buffer: 0x%08x\n",
				 ret);

		vb2_syna_dh_bm_put(vb->planes[0].mem_priv);
		vb->planes[0].mem_priv = NULL;
	}

	clear_bit(SYNA_VPU_POOL_ON_AUX, &ctx->status);
}

static int vepu_push_all_recon_buf(struct syna_vcodec_ctx *ctx)
{
	int ret = -ENOMEM;
	int i;

	for (i = 0; i < ctx->n_recon_pool; i++) {
		if (!test_bit(i, &ctx->recon_slots)) {
			ret = syna_venc_push_ref_buf(ctx, i);
			if (ret)
				return ret;
			set_bit(i, &ctx->recon_slots);
		}
	}

	return ret;
}

static int vepu_push_free_recon_buf(struct syna_vcodec_ctx *ctx)
{
	int ret = -ENOMEM;
	int i;

	for (i = 0; i < ctx->n_recon_pool; i++) {
		if (!test_bit(i, &ctx->recon_slots)) {
			ret = syna_venc_push_ref_buf(ctx, i);
			if (ret)
				return ret;

			set_bit(i, &ctx->recon_slots);
			break;
		}
	}

	return ret;
}

static const struct hantro_fmt *hantro_get_default_fmt(const struct
						       syna_vcodec_ctx *ctx,
						       bool bitstream)
{
	const struct hantro_fmt *fmt;
	unsigned int i;

	for (i = 0; i < VNEC_NUM_FORMATS; i++) {
		fmt = &syna_venc_fmts[i];
		if ((bitstream == (fmt->codec_mode != HANTRO_MODE_NONE)) &&
		    IS_HW_MATCH(ctx->vpu, fmt->version))
			return fmt;
	}

	return NULL;
}

static int vidioc_enum_fmt(struct file *file, struct v4l2_fmtdesc *f,
			   bool capture)
{
	struct syna_vpu_dev *vpu = video_drvdata(file);
	const struct hantro_fmt *fmt;
	unsigned int i, j = 0;
	bool mode_none;

	if (f->index >= VNEC_NUM_FORMATS)
		return -EINVAL;

	for (i = 0; i < VNEC_NUM_FORMATS; i++) {
		fmt = &syna_venc_fmts[i];
		mode_none = (fmt->codec_mode == HANTRO_MODE_NONE);

		if (capture == mode_none)
			continue;
		if (!IS_HW_MATCH(vpu, fmt->version))
			continue;
		if (j == f->index) {
			f->pixelformat = fmt->fourcc;
			return 0;
		}
		++j;
	}
	return -EINVAL;
}

static const struct hantro_fmt *syna_venc_find_format(u32 fourcc,
						      struct syna_vpu_dev *vpu)
{
	const struct hantro_fmt *fmt;
	unsigned int i;

	for (i = 0; i < VNEC_NUM_FORMATS; i++) {
		fmt = &syna_venc_fmts[i];
		if ((fourcc == fmt->fourcc) && (IS_HW_MATCH(vpu, fmt->version)))
			return fmt;
	}

	return NULL;
}

static int vidioc_enum_framesizes(struct file *file, void *fh,
				  struct v4l2_frmsizeenum *fsize)
{
	const struct hantro_fmt *fmt;
	struct syna_vcodec_ctx *ctx = fh_to_ctx(fh);

	if (fsize->index != 0)
		return -EINVAL;

	fmt = syna_venc_find_format(fsize->pixel_format, ctx->vpu);
	if (!fmt)
		return -EINVAL;

	fsize->type = V4L2_FRMSIZE_TYPE_STEPWISE;
	/*
	 * coded format currently set on CAPTURE
	 */
	if (fmt->frmsize.max_width > 0)
		fsize->stepwise = fmt->frmsize;
	else
		/* TODO: support yuv down sample if scalar down encode needed */
		fsize->stepwise = syna_h1_framesizes;

	return 0;
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void *priv,
				   struct v4l2_fmtdesc *f)
{
	return vidioc_enum_fmt(file, f, true);
}

static int vidioc_enum_fmt_vid_out(struct file *file, void *priv,
				   struct v4l2_fmtdesc *f)
{
	return vidioc_enum_fmt(file, f, false);
}

static int vidioc_venc_querycap(struct file *file, void *priv,
				struct v4l2_capability *cap)
{
	struct syna_vpu_dev *vpu = video_drvdata(file);

	strscpy(cap->driver, SYNA_H1_ENC_NAME, sizeof(cap->driver));
	strscpy(cap->card, SYNA_HANTRO_H1_STR, sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info),
		 "platform: %s", vpu->dev->driver->name);

	return 0;
}

static int vidioc_venc_s_parm(struct file *file, void *priv,
			      struct v4l2_streamparm *a)
{
	struct syna_vcodec_ctx *ctx = fh_to_ctx(priv);
	struct v4l2_fract *timeperframe = &a->parm.output.timeperframe;
	struct syna_venc_strm_config *p = ctx->enc_params;

	if (a->type != V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		return -EINVAL;

	if (timeperframe->numerator == 0 || timeperframe->denominator == 0) {
		timeperframe->numerator = SYNA_DEFAULT_FRAMERATE_DENOM;
		timeperframe->denominator = SYNA_DEFAULT_FRAMERATE_NUM;
	}

	p->frame_rate_num = timeperframe->denominator;
	p->frame_rate_den = timeperframe->numerator;

	a->parm.output.capability = V4L2_CAP_TIMEPERFRAME;

	return 0;
}

static int vidioc_venc_g_parm(struct file *file, void *priv,
			      struct v4l2_streamparm *a)
{
	struct syna_vcodec_ctx *ctx = fh_to_ctx(priv);
	struct syna_venc_strm_config *p = ctx->enc_params;

	if (a->type != V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		return -EINVAL;

	a->parm.output.capability = V4L2_CAP_TIMEPERFRAME;
	a->parm.output.timeperframe.denominator = p->frame_rate_num;
	a->parm.output.timeperframe.numerator = p->frame_rate_den;

	return 0;
}

static int hantro_try_fmt_ds(struct syna_vcodec_ctx *ctx,
			     struct v4l2_pix_format_mplane *pix_mp)
{
	int ret;
	int i;
	struct v4l2_pix_format_mplane ds_pix_mp;
	unsigned int sizeimage = 0;

	ret = v4l2_fill_pixfmt_mp(&ds_pix_mp, V4L2_PIX_FMT_YUV422P,
				  ctx->v4l2_ctrls.syna_h1_ds_w,
				  ctx->v4l2_ctrls.syna_h1_ds_h);
	if (ret)
		return ret;

	for (i = 0; i < ds_pix_mp.num_planes; ++i)
		sizeimage += ds_pix_mp.plane_fmt[i].sizeimage;

	sizeimage = ALIGN(sizeimage, SZ_4K);

	++pix_mp->num_planes;
	pix_mp->plane_fmt[pix_mp->num_planes - 1].sizeimage = sizeimage;

	return 0;
}

static int hantro_try_fmt_cap(struct syna_vcodec_ctx *ctx,
			      struct v4l2_pix_format_mplane *pix_mp)
{
	int ret;
	int mb_num;

	pix_mp->field = V4L2_FIELD_NONE;
	pix_mp->num_planes = 1;
	pix_mp->plane_fmt[0].bytesperline = 0;
	pix_mp->flags = 0;

	if (pix_mp->plane_fmt[0].sizeimage < SYNA_ES_BUF_SIZE_H1)
		pix_mp->plane_fmt[0].sizeimage = SYNA_ES_BUF_SIZE_H1;

	pix_mp->plane_fmt[0].sizeimage =
		ALIGN(pix_mp->plane_fmt[0].sizeimage, SZ_4K);

	if (ctx->v4l2_ctrls.syna_h1_ds_w && ctx->v4l2_ctrls.syna_h1_ds_h) {
		ret = hantro_try_fmt_ds(ctx, pix_mp);
		if (ret)
			return ret;
	}

	if (ctx->v4l2_ctrls.metadata_mv_enable) {
		pix_mp->num_planes++;
		mb_num = MB_WIDTH(1920) * MB_HEIGHT(1088);
		pix_mp->plane_fmt[pix_mp->num_planes - 1].sizeimage =
			ALIGN(mb_num * 16, SZ_4K);
	}

	return 0;
}

static int hantro_try_fmt_out(struct syna_vcodec_ctx *ctx,
			      struct v4l2_pix_format_mplane *pix_mp)
{
	const struct hantro_fmt *fmt, *vpu_fmt;

	fmt = syna_venc_find_format(pix_mp->pixelformat, ctx->vpu);
	if (!fmt) {
		fmt = hantro_get_default_fmt(ctx, false);
		pix_mp->pixelformat = fmt->fourcc;
	}

	vpu_fmt = ctx->vpu_dst_fmt;

	pix_mp->field = V4L2_FIELD_NONE;
	v4l2_apply_frmsize_constraints(&pix_mp->width, &pix_mp->height,
				       &vpu_fmt->frmsize);

	v4l2_fill_pixfmt_mp(pix_mp, fmt->fourcc, pix_mp->width, pix_mp->height);
	pix_mp->plane_fmt[0].sizeimage =
		ALIGN(pix_mp->plane_fmt[0].sizeimage, SZ_4K);
	pix_mp->plane_fmt[1].sizeimage =
		ALIGN(pix_mp->plane_fmt[1].sizeimage, SZ_4K);
	pix_mp->plane_fmt[2].sizeimage =
		ALIGN(pix_mp->plane_fmt[2].sizeimage, SZ_4K);

	return 0;
}

static int vidioc_venc_s_fmt_cap(struct file *file, void *priv,
				 struct v4l2_format *f)
{
	struct syna_vcodec_ctx *ctx = fh_to_ctx(priv);
	struct syna_vpu_dev *vpu = ctx->vpu;
	struct syna_venc_strm_config *p = ctx->enc_params;
	struct v4l2_pix_format_mplane *pix_mp = &f->fmt.pix_mp;
	struct vb2_queue *vq;
	const struct hantro_fmt *fmt;

	vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, f->type);
	if (!vq) {
		vepu_err(vpu, "fail to get vq");
		return -EINVAL;
	}

	if (vb2_is_busy(vq)) {
		vepu_err(vpu, "queue busy");
		return -EBUSY;
	}

	fmt = syna_venc_find_format(pix_mp->pixelformat, ctx->vpu);
	if (!fmt) {
		fmt = hantro_get_default_fmt(ctx, true);
		pix_mp->pixelformat = fmt->fourcc;
	}

	p->format = fmt->enc_fmt;
	hantro_try_fmt_cap(ctx, pix_mp);
	switch (pix_mp->pixelformat) {
	case V4L2_PIX_FMT_VP8:
		p->vp8_strm_mode = 2;
		break;
	}
	ctx->vpu_dst_fmt = fmt;
	ctx->dst_fmt = *pix_mp;
	vpu_enc_ctrls_h1_internal(ctx->enc_params);

	return 0;
}

static int vidioc_venc_s_fmt_out(struct file *file, void *priv,
				 struct v4l2_format *f)
{
	struct syna_vcodec_ctx *ctx = fh_to_ctx(priv);
	struct syna_vpu_dev *vpu = ctx->vpu;
	struct syna_venc_strm_config *p = ctx->enc_params;
	struct v4l2_pix_format_mplane *pix_mp = &f->fmt.pix_mp;
	struct vb2_queue *vq;
	int ret;
	const struct hantro_fmt *fmt;

	vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, f->type);
	if (!vq) {
		vepu_err(vpu, "fail to get vq");
		return -EINVAL;
	}

	/*
	 * The encoder doesn't admit a format change if
	 * there are OUTPUT buffers allocated.
	 */
	if (vb2_is_busy(vq)) {
		vepu_err(vpu, "queue busy");
		return -EBUSY;
	}

	fmt = syna_venc_find_format(pix_mp->pixelformat, ctx->vpu);
	if (!fmt) {
		fmt = hantro_get_default_fmt(ctx, false);
		pix_mp->pixelformat = fmt->fourcc;
	}

	if (pix_mp->quantization == V4L2_QUANTIZATION_FULL_RANGE)
		p->video_full_range_flag = 1;

	p->frm_width = pix_mp->width;
	p->frm_height = pix_mp->height;

	/* NOTE: make sure we have default dst format */
	ret = hantro_try_fmt_out(ctx, pix_mp);
	if (ret)
		return ret;

	p->src_yuv_format = fmt->enc_fmt;
	p->src_width = pix_mp->width;
	p->src_height = pix_mp->height;
	p->dis_stride = pix_mp->plane_fmt[0].bytesperline;
	p->dis_buf_size = pix_mp->plane_fmt[0].sizeimage;
	p->dis_buf_size += pix_mp->plane_fmt[1].sizeimage;
	p->dis_buf_size += pix_mp->plane_fmt[2].sizeimage;
	p->dis_buf_size += pix_mp->plane_fmt[3].sizeimage;

	ctx->vpu_src_fmt = fmt;
	ctx->src_fmt = *pix_mp;

	return 0;
}

static int vidioc_venc_g_fmt(struct file *file, void *priv,
			     struct v4l2_format *f)
{
	struct v4l2_pix_format_mplane *pix = &f->fmt.pix_mp;
	struct syna_vcodec_ctx *ctx = fh_to_ctx(priv);
	bool capture = (!V4L2_TYPE_IS_OUTPUT(f->type));

	if (capture)
		*pix = ctx->dst_fmt;
	else
		*pix = ctx->src_fmt;

	return 0;
}

static int vidioc_try_fmt_vid_cap_mplane(struct file *file, void *priv,
					 struct v4l2_format *f)
{
	return hantro_try_fmt_cap(fh_to_ctx(priv), &f->fmt.pix_mp);
}

static int vidioc_try_fmt_vid_out_mplane(struct file *file, void *priv,
					 struct v4l2_format *f)
{
	return hantro_try_fmt_out(fh_to_ctx(priv), &f->fmt.pix_mp);
}

static int vidioc_venc_qbuf(struct file *file, void *priv,
			    struct v4l2_buffer *buf)
{
	struct syna_vcodec_ctx *ctx = fh_to_ctx(priv);

	// TA will take care of cache ops on ES buf once buf is finished.
	if (buf->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		buf->flags |= V4L2_BUF_FLAG_NO_CACHE_INVALIDATE;

	return v4l2_m2m_qbuf(file, ctx->fh.m2m_ctx, buf);
}

static int vidioc_venc_dqbuf(struct file *file, void *priv,
			     struct v4l2_buffer *buf)
{
	struct syna_vcodec_ctx *ctx = fh_to_ctx(priv);

	return v4l2_m2m_dqbuf(file, ctx->fh.m2m_ctx, buf);
}

static int vidioc_encoder_cmd(struct file *file, void *priv,
			      struct v4l2_encoder_cmd *ec)
{
	struct syna_vcodec_ctx *ctx = fh_to_ctx(priv);
	struct syna_vpu_dev *vpu = ctx->vpu;
	struct vb2_queue *src_vq, *dst_vq;
	int ret;

	ret = v4l2_m2m_ioctl_try_encoder_cmd(file, priv, ec);
	if (ret)
		return ret;

	vepu_dbg(1, "encoder cmd=%u", ec->cmd);

	dst_vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx,
				 V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
	switch (ec->cmd) {
	case V4L2_ENC_CMD_STOP:
		src_vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx,
					 V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
		if (!vb2_is_streaming(src_vq)) {
			vepu_dbg(1, "Output stream is off. No need to flush.");
			return 0;
		}
		if (!vb2_is_streaming(dst_vq)) {
			vepu_dbg(1, "Capture stream is off. No need to flush.");
			return 0;
		}

		wait_for_completion(&ctx->work_done);

		/* I just want to call v4l2_update_last_buf_state() */
		ret = v4l2_m2m_encoder_cmd(file, ctx->fh.m2m_ctx, ec);

		if (v4l2_m2m_has_stopped(ctx->fh.m2m_ctx))
			v4l2_event_queue_fh(&ctx->fh, &enc_eos_event);
		break;
	case V4L2_ENC_CMD_START:
		vb2_clear_last_buffer_dequeued(dst_vq);
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

static int
vidioc_venc_subscribe_event(struct v4l2_fh *fh,
			    const struct v4l2_event_subscription *sub)
{
	return v4l2_event_subscribe(fh, sub, 2, NULL);
}

static int
vidioc_venc_unsubscribe_event(struct v4l2_fh *fh,
			      const struct v4l2_event_subscription *sub)
{
	return v4l2_event_unsubscribe(fh, sub);
}

const struct v4l2_ioctl_ops syna_venc_ioctl_ops = {
	.vidioc_streamon = v4l2_m2m_ioctl_streamon,
	.vidioc_streamoff = v4l2_m2m_ioctl_streamoff,

	.vidioc_reqbufs = v4l2_m2m_ioctl_reqbufs,
	.vidioc_querybuf = v4l2_m2m_ioctl_querybuf,
	.vidioc_qbuf = vidioc_venc_qbuf,
	.vidioc_dqbuf = vidioc_venc_dqbuf,

	.vidioc_querycap = vidioc_venc_querycap,
	.vidioc_enum_fmt_vid_cap = vidioc_enum_fmt_vid_cap,
	.vidioc_enum_fmt_vid_out = vidioc_enum_fmt_vid_out,
	.vidioc_enum_framesizes = vidioc_enum_framesizes,

	.vidioc_try_fmt_vid_cap_mplane = vidioc_try_fmt_vid_cap_mplane,
	.vidioc_try_fmt_vid_out_mplane = vidioc_try_fmt_vid_out_mplane,
	.vidioc_expbuf = v4l2_m2m_ioctl_expbuf,
	.vidioc_subscribe_event = vidioc_venc_subscribe_event,
	.vidioc_unsubscribe_event = vidioc_venc_unsubscribe_event,

	.vidioc_s_parm = vidioc_venc_s_parm,
	.vidioc_g_parm = vidioc_venc_g_parm,
	.vidioc_s_fmt_vid_cap_mplane = vidioc_venc_s_fmt_cap,
	.vidioc_s_fmt_vid_out_mplane = vidioc_venc_s_fmt_out,

	.vidioc_g_fmt_vid_cap_mplane = vidioc_venc_g_fmt,
	.vidioc_g_fmt_vid_out_mplane = vidioc_venc_g_fmt,

	.vidioc_create_bufs = v4l2_m2m_ioctl_create_bufs,
	.vidioc_prepare_buf = v4l2_m2m_ioctl_prepare_buf,

	.vidioc_encoder_cmd = vidioc_encoder_cmd,
	.vidioc_try_encoder_cmd = v4l2_m2m_ioctl_try_encoder_cmd,
};

static int vb2ops_syna_vpu_queue_setup(struct vb2_queue *vq,
				       unsigned int *nbuffers,
				       unsigned int *nplanes,
				       unsigned int sizes[],
				       struct device *alloc_devs[])
{
	struct syna_vcodec_ctx *ctx = vb2_get_drv_priv(vq);
	struct syna_vpu_dev *vpu = ctx->vpu;
	unsigned int q_num_bufs = vb2_get_num_buffers(vq);
	struct v4l2_pix_format_mplane *pixfmt;
	uint32_t max_buf_num;
	unsigned int i;

	switch (vq->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
		max_buf_num = SYNA_ES_BUF_MAX_NUM;
		pixfmt = &ctx->dst_fmt;
		break;
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		max_buf_num = SYNA_FRAME_BUF_MAX_NUM;
		pixfmt = &ctx->src_fmt;
		break;
	default:
		vepu_err(vpu, "invalid queue type: %d\n", vq->type);
		return -EINVAL;
	}

	if (*nplanes) {
		if (*nplanes != pixfmt->num_planes)
			return -EINVAL;

		if (*nplanes < pixfmt->num_planes)
			*nplanes = pixfmt->num_planes;

		if (q_num_bufs > max_buf_num)
			return -ENOBUFS;

		if (q_num_bufs + *nbuffers > max_buf_num) {
			*nbuffers = 0;
			vepu_err(
				vpu,
				"[%s():%d], queue type:%d, run out of buffers\n",
				__func__, __LINE__, vq->type);
		}
	} else {
		*nplanes = pixfmt->num_planes;

		if (vq->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
			*nbuffers = clamp(*nbuffers, SYNA_FRAME_BUF_MIN_NUM,
					  SYNA_FRAME_BUF_MAX_NUM);
		else
			*nbuffers = clamp(*nbuffers, SYNA_ES_BUF_MIN_NUM,
					  SYNA_ES_BUF_MAX_NUM);
	}

	alloc_devs[0] = cma_dh_dev;
	alloc_devs[1] = cma_dh_dev;
	alloc_devs[2] = cma_dh_dev;

	*nplanes = pixfmt->num_planes;
	for (i = 0; i < pixfmt->num_planes; ++i)
		sizes[i] = pixfmt->plane_fmt[i].sizeimage;

	return 0;
}

static int vb2ops_venc_buf_out_validate(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);

	vbuf->field = V4L2_FIELD_NONE;
	return 0;
}

/*
 * called from _prepare_dmabuf() when reacquired == true
 * and __vb2_queue_alloc() for MMAP
 */
static int vb2ops_syna_vpu_buf_init(struct vb2_buffer *vb)
{
	struct syna_vcodec_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct syna_vpu_dev *vpu = ctx->vpu;
	struct vb2_queue *vq = vb->vb2_queue;
	struct syna_vpu_tz_buf *vpu_buf;
	dma_addr_t memid;
	u32 i, idx;
	enum syna_vpu_fw_enc_buf buftype;
	int ret;
	int num_planes;
	int plane_mv = -1;

	idx = vb->index;

	if (V4L2_TYPE_IS_OUTPUT(vq->type)) {
		vpu_buf = ctx->input_pool;
		vpu_buf += idx;
		vpu_buf->buf_type = ENCODER_PICTURE;
		buftype = SYNA_VPU_FW_VENC_VID;
		vpu_buf->planes[0].gfp_flags = BERLIN_GFP_FLAG_NO_MAPPING;
		vpu_buf->planes[1].gfp_flags = BERLIN_GFP_FLAG_NO_MAPPING;
		vpu_buf->planes[2].gfp_flags = BERLIN_GFP_FLAG_NO_MAPPING;
		num_planes = vb->num_planes;
	} else {
		vpu_buf = ctx->output_pool;
		vpu_buf += idx;
		vpu_buf->buf_type = ENCODER_STREAM;
		buftype = SYNA_VPU_FW_VENC_STRM;
		num_planes = 1;
	}

	memset(vpu_buf->planes, 0, sizeof(struct syna_tz_generic_buf) << 2);
	for (i = 0; i < num_planes; i++) {
		memid = syna_vpu_bm_dh_plane_memid(vb, i);

		vpu_buf->planes[i].type = CONTIGUOUS_MEM;
		vpu_buf->planes[i].memid = memid;
		vpu_buf->planes[i].size = vb2_plane_size(vb, i);
	}

	if (!V4L2_TYPE_IS_OUTPUT(vq->type) && vb->num_planes >= 2) {
		if (ctx->v4l2_ctrls.metadata_mv_enable) {
			plane_mv = vb->num_planes - 1;
			memid = syna_vpu_bm_dh_plane_memid(vb, plane_mv);

			vpu_buf->planes[1].type = CONTIGUOUS_MEM;
			vpu_buf->planes[1].memid = memid;
			vpu_buf->planes[1].size = vb2_plane_size(vb, plane_mv);
		}
	}

	ret = syna_vpu_register_buf(ctx, buftype, idx);
	if (ret) {
		vepu_err(vpu, "can't register buffer: 0x%08x\n", ret);
		return -EINVAL;
	}
	if (plane_mv == 1) // no downscale buffer plane
		return 0;

	if (!V4L2_TYPE_IS_OUTPUT(vq->type) && vb->num_planes >= 2) {
		vpu_buf = ctx->ds_pool;
		vpu_buf += idx;
		vpu_buf->buf_type = ENCODER_PICTURE;
		buftype = SYNA_VPU_FW_VENC_DS;

		memid = syna_vpu_bm_dh_plane_memid(vb, 1);

		vpu_buf->planes[0].type = CONTIGUOUS_MEM;
		vpu_buf->planes[0].memid = memid;
		vpu_buf->planes[0].size = vb2_plane_size(vb, 1);

		ret = syna_vpu_register_buf(ctx, buftype, idx);
		if (ret) {
			vepu_err(vpu, "reg ds buf fail: 0x%08x\n", ret);
			return ret;
		}
	}

	return 0;
}

static int
hantro_buf_plane_check(struct vb2_buffer *vb,
		       struct v4l2_pix_format_mplane *pixfmt)
{
	unsigned int sz;
	int i;

	for (i = 0; i < pixfmt->num_planes; i++) {
		sz = pixfmt->plane_fmt[i].sizeimage;
		if (vb2_plane_size(vb, i) < sz) {
			pr_err("syna_vpu: plane %d is too small\n", i);
			return -EINVAL;
		}
	}

	return 0;
}

static int vb2ops_venc_buf_prepare(struct vb2_buffer *vb)
{
	struct vb2_queue *vq = vb->vb2_queue;
	struct syna_vcodec_ctx *ctx = vb2_get_drv_priv(vq);
	struct v4l2_pix_format_mplane *pix_fmt;
	int ret;

	if (V4L2_TYPE_IS_OUTPUT(vq->type)) {
		pix_fmt = &ctx->src_fmt;
		ret = hantro_buf_plane_check(vb, pix_fmt);
		if (ret)
			return ret;
	} else {
		vb2_set_plane_payload(vb, 0, 0);
	}

	return 0;
}

/* It would happen before put memory */
static void vb2ops_syna_vpu_buf_cleanup(struct vb2_buffer *vb)
{
	struct vb2_queue *vq = vb->vb2_queue;
	struct syna_vcodec_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct syna_vpu_dev *vpu = ctx->vpu;
	struct syna_vpu_tz_buf *vpu_buf;
	enum syna_vpu_fw_enc_buf buftype;
	u32 i;
	int ret;

	i = vb->index;

	if (V4L2_TYPE_IS_OUTPUT(vq->type)) {
		vpu_buf = ctx->input_pool;
		vpu_buf += i;
		buftype = SYNA_VPU_FW_VENC_VID;
	} else {
		vpu_buf = ctx->output_pool;
		vpu_buf += i;
		buftype = SYNA_VPU_FW_VENC_STRM;
	}

	ret = syna_vpu_unregister_buf(ctx, buftype, i);
	if (ret)
		vepu_err(vpu, "failed to unregister buffer: 0x%08x\n", ret);

	if (!V4L2_TYPE_IS_OUTPUT(vq->type) && vb->num_planes == 2) {
		buftype = SYNA_VPU_FW_VENC_DS;

		ret = syna_vpu_unregister_buf(ctx, buftype, i);
		if (ret)
			vepu_err(vpu, "unreg ds buf fail: 0x%08x\n", ret);
	}
}

static int syna_vepu_push_queued_cap(struct syna_vcodec_ctx *ctx)
{
	struct v4l2_m2m_ctx *m2m_ctx = ctx->fh.m2m_ctx;
	struct v4l2_m2m_buffer *buf, *n;
	int ret;

	v4l2_m2m_for_each_dst_buf_safe(m2m_ctx, buf, n) {
		ret = syna_venc_push_es_buf(ctx, buf->vb.vb2_buf.index);
		if (ret) {
			v4l2_m2m_dst_buf_remove_by_buf(m2m_ctx, &buf->vb);
			v4l2_m2m_buf_done(&buf->vb, VB2_BUF_STATE_ERROR);
			return ret;
		}
	}

	return 0;
}

static int vepu_configure_stream(struct syna_vcodec_ctx *ctx)
{
	struct syna_vpu_ctrl *ctrl = syna_vpu_get_ctrl_shm_buffer(ctx);
	int ret = -EBUSY;

	if (!test_bit(SYNA_VPU_STATUS_SET_FMT, &ctx->status)) {
		vpu_enc_ctrls_v4l2_to_h1(&ctx->v4l2_ctrls,
					 ctx->dst_fmt.pixelformat, 0,
					 ctx->src_fmt.width, ctx->enc_params,
					 NULL, &ctrl->h1_extra_cfg);
		ret = syna_venc_configure_stream(ctx);
		syna_vepu_push_queued_cap(ctx);
		vepu_push_all_recon_buf(ctx);
		set_bit(SYNA_VPU_STATUS_SET_FMT, &ctx->status);
	}

	return ret;
}

static int vb2ops_venc_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct syna_vcodec_ctx *ctx = vb2_get_drv_priv(q);
	int ret;

	v4l2_m2m_update_start_streaming_state(ctx->fh.m2m_ctx, q);

	/* Do the initialization when both start_streaming have been called */
	if (V4L2_TYPE_IS_OUTPUT(q->type)) {
		if (!vb2_start_streaming_called(&ctx->fh.m2m_ctx->cap_q_ctx.q))
			return 0;
	} else {
		if (!vb2_start_streaming_called(&ctx->fh.m2m_ctx->out_q_ctx.q))
			return 0;
	}

	ret = vepu_alloc_recon_buf(ctx);
	if (ret <= 0)
		return -EBUSY;

	ret = vepu_configure_stream(ctx);

	ctx->sequence_out = 0;
	ctx->sequence_cap = 0;

	return ret;
}

static void vb2ops_venc_stop_streaming(struct vb2_queue *q)
{
	struct syna_vcodec_ctx *ctx = vb2_get_drv_priv(q);
	struct syna_vcodec_ctx *next_ctx;
	struct vb2_v4l2_buffer *src_buf, *dst_buf;

	v4l2_m2m_update_stop_streaming_state(ctx->fh.m2m_ctx, q);

	if (V4L2_TYPE_IS_OUTPUT(q->type)) {
		while ((src_buf = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx)))
			v4l2_m2m_buf_done(src_buf, VB2_BUF_STATE_ERROR);
	} else {
		/* the encoder will be reset */
		vpu_srv_release_out(ctx->vpu->srv, ctx, &next_ctx);
		vepu_release_recon_buf(ctx);

		while ((dst_buf = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx))) {
			vb2_set_plane_payload(&dst_buf->vb2_buf, 0, 0);
			v4l2_m2m_buf_done(dst_buf, VB2_BUF_STATE_ERROR);
		}

		if (next_ctx)
			v4l2_m2m_try_schedule(next_ctx->fh.m2m_ctx);
	}
}

static void vb2ops_venc_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_queue *vq = vb->vb2_queue;
	struct syna_vcodec_ctx *ctx = vb2_get_drv_priv(vq);
	struct syna_vpu_ctrl *ctrl = syna_vpu_get_ctrl_shm_buffer(ctx);
	struct vb2_v4l2_buffer *vb2_v4l2 = to_vb2_v4l2_buffer(vb);
	struct syna_vpu_tz_buf *vpu_buf;
	unsigned int i;

	if (V4L2_TYPE_IS_OUTPUT(vq->type)) {
		vpu_buf = ctx->input_pool;
		vpu_buf += vb->index;

		if (vb2_v4l2->flags & V4L2_BUF_FLAG_KEYFRAME) {
			ctx->v4l2_ctrls.apply_dyna_ctrls = true;
			ctx->v4l2_ctrls.force_key_frame = 1;
		}

		if (ctx->v4l2_ctrls.apply_dyna_ctrls) {
			vpu_enc_ctrls_v4l2_to_h1(
				&ctx->v4l2_ctrls, ctx->dst_fmt.pixelformat,
				ctx->input_pic_seq, ctx->src_fmt.width, NULL,
				&ctrl->venc_2ndpass_para[vb->index], NULL);
			SET_BIT(vb->index, ctrl->venc_2ndpass_para_stats);
		}
		++ctx->input_pic_seq;

		for (i = 0; i < vb->num_planes; i++) {
			vpu_buf->bytesused[i] = vb2_get_plane_payload(vb, i);
			if (vb2_syna_bm_has_cache_carer(vb, i))
				vpu_buf->planes[i].gfp_flags |=
					BERLIN_GFP_FLAG_NO_CACHE_CLEAN;
		}

	} else if (test_bit(SYNA_VPU_STATUS_SET_FMT, &ctx->status)) {
		if (vb2_is_streaming(vb->vb2_queue) &&
		    v4l2_m2m_dst_buf_is_last(ctx->fh.m2m_ctx)) {
			syna_v4l2_m2m_last_buf_done(ctx->fh.m2m_ctx, vb2_v4l2);
			return;
		}

		vpu_buf = ctx->output_pool;
		vpu_buf += vb->index;

		if (syna_venc_push_es_buf(ctx, vb->index))
			return;

		++ctx->input_es_seq;

		if (ctx->v4l2_ctrls.syna_h1_ds_w &&
		    ctx->v4l2_ctrls.syna_h1_ds_h && ctx->input_es_seq > 1 &&
		    syna_venc_push_ds_buf(ctx, vb->index))
			return;
	}

	v4l2_m2m_buf_queue(ctx->fh.m2m_ctx, vb2_v4l2);
}

static const struct vb2_ops syan_venc_vb2_ops = {
	.queue_setup = vb2ops_syna_vpu_queue_setup,
	.wait_prepare = vb2_ops_wait_prepare,
	.wait_finish = vb2_ops_wait_finish,
	.buf_out_validate = vb2ops_venc_buf_out_validate,
	.buf_init = vb2ops_syna_vpu_buf_init,
	.buf_prepare = vb2ops_venc_buf_prepare,
	.buf_cleanup = vb2ops_syna_vpu_buf_cleanup,
	.start_streaming = vb2ops_venc_start_streaming,
	.stop_streaming = vb2ops_venc_stop_streaming,
	.buf_queue = vb2ops_venc_buf_queue,
};

static int venc_job_ready(void *priv)
{
	struct syna_vcodec_ctx *ctx = priv;
	struct syna_vpu_dev *vpu = ctx->vpu;

	return !vpu_srv_push_to_pending(vpu->srv, ctx);
}

static void venc_device_run(void *priv)
{
	struct syna_vcodec_ctx *ctx = priv;
	struct syna_vpu_dev *vpu = ctx->vpu;

	queue_work(vpu->vcodec_workq, &vpu->job_work);
}

static void venc_job_abort(void *priv)
{
	struct syna_vcodec_ctx *ctx = priv;

	wait_for_completion_killable(&ctx->work_done);
}

static const struct v4l2_m2m_ops vpu_m2m_h1_ops = {
	.job_ready = venc_job_ready,
	.device_run = venc_device_run,
	.job_abort = venc_job_abort,
};

#if IS_ENABLED(CONFIG_OPTEE)
#else
static TEEC_Result syna_venc_h1_callback(TEEC_Session * session,
					 uint32_t commandID,
					 TEEC_Operation * operation,
					 void *userdata)
{
	struct syna_vpu_dev *vpu = userdata;
	int ret;

	ret = vpu_srv_wait_isr(vpu->srv, ENC_H1_TIMEOUT_DELAY);
	/* NOTE: anyway to report the timeout ? */

	return 0;
}
#endif
static void syna_venc_h1_worker(struct work_struct *work)
{
	struct syna_vpu_dev *vpu =
	    container_of(work, struct syna_vpu_dev, job_work);
	struct v4l2_m2m_dev *m2m_dev = vpu->m2m_dev;
	struct vb2_queue *out_q;
	struct syna_vcodec_ctx *ctx, *pending_ctx;
	struct vb2_v4l2_buffer *src_buf, *dst_buf;
	struct vb2_buffer *vb_buf;
	struct syna_vpu_ctrl *ctrl;
	struct syna_vpu_tz_buf *vpu_buf;
	uint32_t idx;
	bool flushing;
	bool no_free_recon = false;
	int ret;
	int i, plane_mv;

	ctx = v4l2_m2m_get_curr_priv(m2m_dev);
	vpu_srv_prepare_to_run(vpu->srv, ctx);

	src_buf = v4l2_m2m_next_src_buf(ctx->fh.m2m_ctx);
	/*
	 * NOTE: inform hw to encode as possible as it could, no waiting
	 * for future input
	 */
	ctrl = syna_vpu_get_ctrl_shm_buffer(ctx);
	memset(&ctrl->status, 0, sizeof(ctrl->status));

	if (syna_venc_push_video_buf(ctx, src_buf->vb2_buf.index)) {
		v4l2_m2m_buf_done(src_buf, VB2_BUF_STATE_ERROR);
		vepu_err(vpu, "failed to push frame\n");
		goto bail;
	}

retry:
	if (-ENOMEM == vepu_push_free_recon_buf(ctx))
		no_free_recon = true;

	if (vpu_srv_clear_pending_isr(vpu->srv))
		vepu_err(vpu, "has an handled irq, HW may in a bad state\n");

	pm_runtime_get(vpu->dev);

	down_write(&vpu->resource_rwsem);
	ret = syna_venc_encode_stream(ctx);
	up_write(&vpu->resource_rwsem);

	pm_runtime_put(vpu->dev);

	if (vpu_srv_has_pending_isr(vpu->srv))
		vepu_err(vpu, "still in a wrong state\n");

	if (ret) {
		v4l2_m2m_buf_done(src_buf, VB2_BUF_STATE_ERROR);
		goto bail;
	}

	if (ctrl->status.flags & BERLIN_VPU_STATUS_WAIT_AUX_BUF) {
		if (no_free_recon) {
			v4l2_m2m_buf_done(src_buf, VB2_BUF_STATE_ERROR);
			vepu_err(vpu, "no free recon\n");
			/* NOTE: reset fw and release the current switch point if needed */
			goto bail;
		}
		goto retry;
	}

	v4l2_m2m_src_buf_remove_by_buf(ctx->fh.m2m_ctx, src_buf);
	flushing = v4l2_m2m_is_last_draining_src_buf(ctx->fh.m2m_ctx, src_buf);

	for (i = 0; i < ctrl->sbuf.pop; i++) {
		idx = ctrl->sbuf.index[i];
		dst_buf = v4l2_m2m_dst_buf_remove_by_idx(ctx->fh.m2m_ctx, idx);

		if (!dst_buf) {
			vepu_err(vpu, "%d is not in CAPTURE queue\n", idx);
			continue;
		}

		vpu_buf = ctx->output_pool;
		vpu_buf += idx;
		vb2_set_plane_payload(&dst_buf->vb2_buf, 0,
				      vpu_buf->bytesused[0]);

		if (ctrl->dsbuf.pop) {
			if (vpu_buf->flags & BERLIN_BUF_FLAG_SEQ_HEADER) {
				vb2_set_plane_payload(&dst_buf->vb2_buf, 1, 0);
			} else {
				vb2_set_plane_payload(
					&dst_buf->vb2_buf, 1,
					ctx->dst_fmt.plane_fmt[1].sizeimage);
			}
		}

		if (ctx->v4l2_ctrls.metadata_mv_enable &&
		    ctx->dst_fmt.num_planes >= 2) {
			plane_mv = ctx->dst_fmt.num_planes - 1;
			if (vpu_buf->bytesused[1]) {
				vepu_dbg(5, "motion vector %d, 0x%x q%d\n", idx,
					 vpu_buf->bytesused[1],
					 dst_buf->sequence);
				vb2_set_plane_payload(&dst_buf->vb2_buf,
						      plane_mv,
						      vpu_buf->bytesused[1]);
			}
		}

		dst_buf->sequence = ctx->sequence_cap++;
		dst_buf->field = V4L2_FIELD_NONE;
		/* FIXME: better timestamp assignment mechanism */
		dst_buf->vb2_buf.timestamp = src_buf->vb2_buf.timestamp;

		if (flushing && (i == (ctrl->sbuf.pop - 1))) {
			syna_v4l2_m2m_last_buf_done(ctx->fh.m2m_ctx, dst_buf);
			vepu_dbg(5, "marked last dst buf %d, 0x%x q%d\n", idx,
				 vpu_buf->bytesused[0], dst_buf->sequence);
			break;
		}
		vepu_dbg(5, "es done %d, 0x%x q%d\n", idx,
			 vpu_buf->bytesused[0], dst_buf->sequence);
		v4l2_m2m_buf_done(dst_buf, VB2_BUF_STATE_DONE);
	}

	out_q = v4l2_m2m_get_src_vq(ctx->fh.m2m_ctx);
	for (i = 0; i < ctrl->dbuf.pop; i++) {
		idx = ctrl->dbuf.index[i];
		vb_buf = vb2_get_buffer(out_q, idx);
		if (!vb_buf) {
			vepu_err(vpu, "%d is not in OUTPUT queue\n", idx);
			continue;
		}

		src_buf = to_vb2_v4l2_buffer(vb_buf);

		if (TEST_BIT(src_buf->vb2_buf.index,
			     ctrl->venc_2ndpass_para_stats)) {
			vpu_enc_ctrls_update_ltr_usage(&ctrl->
						       venc_2ndpass_para
						       [src_buf->vb2_buf.index],
						       &ctx->v4l2_ctrls.
						       ltr_ctrls.
						       ltr_usage_mask);
			memset(&ctrl->venc_2ndpass_para[src_buf->vb2_buf.index],
			       0x0, sizeof(struct syna_venc_2ndpass_param));
			CLR_BIT(src_buf->vb2_buf.index,
				ctrl->venc_2ndpass_para_stats);
		}

		src_buf->sequence = ctx->sequence_out++;
		v4l2_m2m_buf_done(src_buf, VB2_BUF_STATE_DONE);
	}

	for (i = 0; i < ctrl->rbuf.pop; i++) {
		clear_bit(ctrl->rbuf.index[i], &ctx->recon_slots);
	}

	if (ctrl->status.flags & BERLIN_VPU_STATUS_DONE) {
		pending_ctx = vpu_srv_schedule_pending(vpu->srv);
		if (pending_ctx)
			v4l2_m2m_try_schedule(pending_ctx->fh.m2m_ctx);
	} else {
		vpu_srv_schedule_no_yield(vpu->srv);
	}

bail:
	v4l2_m2m_job_finish(vpu->m2m_dev, ctx->fh.m2m_ctx);
	complete(&ctx->work_done);
}

static int
queue_init(void *priv, struct vb2_queue *src_vq, struct vb2_queue *dst_vq)
{
	struct syna_vcodec_ctx *ctx = priv;
	int ret;

	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	src_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	src_vq->drv_priv = ctx;
	src_vq->ops = &syan_venc_vb2_ops;
	src_vq->mem_ops = &syna_bm_dh_memops;
	src_vq->dma_attrs = DMA_ATTR_ALLOC_SINGLE_PAGES |
	    DMA_ATTR_NO_KERNEL_MAPPING;
	src_vq->buf_struct_size = 0;
	src_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	src_vq->allow_cache_hints = 1;
	src_vq->lock = &ctx->vpu->mutex;
	src_vq->dev = ctx->vpu->v4l2_dev.dev;

	ret = vb2_queue_init(src_vq);
	if (ret)
		return ret;

	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	dst_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	dst_vq->drv_priv = ctx;
	dst_vq->ops = &syan_venc_vb2_ops;
	dst_vq->buf_struct_size = 0;
	dst_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	dst_vq->lock = &ctx->vpu->mutex;
	dst_vq->dev = ctx->vpu->v4l2_dev.dev;
	dst_vq->bidirectional = true;
	dst_vq->allow_cache_hints = 1;
	dst_vq->mem_ops = &syna_bm_dh_memops;
	dst_vq->dma_attrs = DMA_ATTR_ALLOC_SINGLE_PAGES |
	    DMA_ATTR_NO_KERNEL_MAPPING;

	return vb2_queue_init(dst_vq);
}

/* v4l2_file_operations */
static int vepu_driver_open(struct file *filp)
{
	struct syna_vpu_auxiliary_device *auxdev = NULL;
	struct syna_vpu_dev *vpu = NULL;
	struct syna_vcodec_ctx *ctx = NULL;
	struct syna_vpu_ctrl *ctrl = NULL;
	struct syna_vpu_fw_info *fw_info = NULL;
	size_t ctrl_buf_size, cfg_buf_size, ctx_buf_size;
	dma_addr_t *memid;
	int ret;

	vpu = video_drvdata(filp);
	auxdev = container_of(vpu->dev, struct syna_vpu_auxiliary_device, dev);
	ret = syna_mfd_request_connect(auxdev);
	if (ret)
		return ret;

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx) {
		ret = -ENOMEM;
		goto failed;
	}

	INIT_LIST_HEAD(&ctx->service_link);
	fw_info = &vpu->fw_data.fw_info;
	ctx->vpu = vpu;

	cfg_buf_size = ALIGN(sizeof(struct syna_venc_strm_config), PAGE_SIZE);
	ret = syna_vpu_tee_alloc(vpu->tee_ctx, cfg_buf_size, (void **)&ctx->cfg_shm);
	if (ret || !syna_vpu_get_cfg_shm_buffer(ctx)) {
		vepu_err(vpu, "can't allocate cfg shm\n");
		ret = -ENOMEM;
		goto failed;
	}
	ctx->enc_params = syna_vpu_get_cfg_shm_buffer(ctx);

	ctrl_buf_size = ALIGN(sizeof(struct syna_vpu_ctrl), PAGE_SIZE);
	ret = syna_vpu_tee_alloc(vpu->tee_ctx, ctrl_buf_size, (void **)&ctx->ctrl_shm);
	if (ret || !syna_vpu_get_ctrl_shm_buffer(ctx)) {
		vepu_err(vpu, "can't allocate instance shm\n");
		ret = -ENOMEM;
		goto failed_shm;
	}
	ctrl = syna_vpu_get_ctrl_shm_buffer(ctx);
	ctrl->ctrl_buf.addr = syna_vpu_get_ctrl_shm_paddr(ctx);
	ctrl->ctrl_buf.size = ctrl_buf_size;

	ctx_buf_size = ALIGN(BERLIN_VPU_CTX_RESERVED_SIZE, PAGE_SIZE) +
	    ALIGN(fw_info->venc_strm_context_size, PAGE_SIZE) +
	    ALIGN(fw_info->venc_strm_hw_context_size, PAGE_SIZE) +
	    fw_info->venc_pool_size;

	ctx_buf_size = ALIGN(ctx_buf_size, PAGE_SIZE);

	/* NOTE: can move to secure */
	ctx->ctx_buf = syna_dh_bm_alloc(cma_dh_dev, ctx_buf_size);
	if (!ctx->ctx_buf) {
		vepu_err(vpu, "can't allocate secure instance context\n");
		ret = -ENOMEM;
		goto failed_sec_ctx;
	}
	memid = vb2_syna_bm_cookie(NULL, ctx->ctx_buf);

	ctrl->ctx_buf.type = CONTIGUOUS_MEM;
	ctrl->ctx_buf.memid = (uintptr_t) * memid;
	ctrl->ctx_buf.offset = 0;
	ctrl->ctx_buf.size = ctx_buf_size;

	ret = syna_venc_create_instance(ctx);
	if (ret) {
		vepu_err(vpu, "Failed to create instance(%d)", ret);
		goto fw_inst_failed;
	}

	ctx->input_pool = &ctrl->inbufs;
	ctx->output_pool = &ctrl->outbufs;
	ctx->aux_pool = &ctrl->auxbufs;
	ctx->ds_pool = &ctrl->dsbufs;

	init_completion(&ctx->work_done);

	v4l2_fh_init(&ctx->fh, video_devdata(filp));
	ctx->fh.m2m_ctx = v4l2_m2m_ctx_init(vpu->m2m_dev, ctx, queue_init);
	if (IS_ERR(ctx->fh.m2m_ctx)) {
		ret = PTR_ERR(ctx->fh.m2m_ctx);
		goto err_ctx_free;
	}

	filp->private_data = &ctx->fh;
	v4l2_fh_add(&ctx->fh);

	ret = vpu_enc_ctrls_init(&ctx->ctrl_handler);
	if (ret) {
		vepu_err(vpu, "Failed to setup controls(%d)", ret);
		goto err_fh_free;
	}
	ctx->fh.ctrl_handler = &ctx->ctrl_handler;
	ctx->vpu_dst_fmt = hantro_get_default_fmt(ctx, true);

	return 0;

err_fh_free:
	v4l2_fh_del(&ctx->fh);
	v4l2_fh_exit(&ctx->fh);
err_ctx_free:
	syna_venc_destroy_instance(ctx);
fw_inst_failed:
	vb2_syna_dh_bm_put(ctx->ctx_buf);
failed_sec_ctx:
	syna_vpu_tee_release(ctx->ctrl_shm);
failed_shm:
	syna_vpu_tee_release(ctx->cfg_shm);
failed:
	kfree(ctx);
	syna_mfd_disconnect(auxdev);
	return ret;
}

static int vepu_driver_release(struct file *filp)
{
	struct syna_vcodec_ctx *ctx = fh_to_ctx(filp->private_data);
	struct syna_vpu_dev *vpu = ctx->vpu;
	struct syna_vpu_auxiliary_device *auxdev = NULL;
	int ret;

	v4l2_m2m_ctx_release(ctx->fh.m2m_ctx);
	v4l2_fh_del(&ctx->fh);
	v4l2_fh_exit(&ctx->fh);
	vpu_enc_ctrls_deinit(&ctx->ctrl_handler);

	syna_venc_destroy_instance(ctx);

	vb2_syna_dh_bm_put(ctx->ctx_buf);
	syna_vpu_tee_release(ctx->ctrl_shm);
	syna_vpu_tee_release(ctx->cfg_shm);

	auxdev = container_of(vpu->dev, struct syna_vpu_auxiliary_device, dev);
	ret = syna_mfd_disconnect(auxdev);

	kfree(ctx);

	return ret;
}

static const struct v4l2_file_operations syna_venc_fops = {
	.owner = THIS_MODULE,
	.open = vepu_driver_open,
	.release = vepu_driver_release,
	.poll = v4l2_m2m_fop_poll,
	.unlocked_ioctl = video_ioctl2,
	.mmap = v4l2_m2m_fop_mmap,
};

static const struct syna_vpu_fw_ops vepu_fw_ops = {
	.fw_inst_swap = syna_vpu_ctx_switch_inst,
	.release = syna_venc_close_stream,
	.switch_in = syna_venc_stream_switch_in,
	.switch_out = syna_venc_stream_switch_out,
};

int syna_vepu_h1_init(struct syna_vpu_dev *vpu)
{
	struct video_device *vfd;
	int ret;

	switch (vpu->fw_data.fw_info.hw_version) {
	case 0x00070000:
		vpu->hw_ver_bit = SYNA_DOLPHIN_BIT;
		break;
	case 0x00070001:
		vpu->hw_ver_bit = SYNA_PLATYPUS_BIT;
		break;
	default:
		dev_err(vpu->dev, "unsupported hw ver: 0x%08x\n",
			vpu->fw_data.fw_info.hw_version);
		return -ENOTTY;
	}

	vpu->fw_ops = &vepu_fw_ops;
	INIT_WORK(&vpu->job_work, syna_venc_h1_worker);

#if IS_ENABLED(CONFIG_OPTEE)
#else
	if (TEEC_RegisterCallback(&vpu->tee_session, syna_venc_h1_callback, vpu)) {
		dev_err(vpu->dev, "can't register venc callback\n");
		return -EINVAL;
	}
#endif
	mutex_init(&vpu->mutex);

	snprintf(vpu->v4l2_dev.name, sizeof(vpu->v4l2_dev.name), "%s",
		 "[SYNA_V4L2_H1_VENC]");
	ret = v4l2_device_register(vpu->dev, &vpu->v4l2_dev);
	if (ret) {
		dev_err(vpu->dev, "Failed to register v4l2 device\n");
		return ret;
	}

	/* We just use the buffer ready queue */
	vpu->m2m_dev = v4l2_m2m_init(&vpu_m2m_h1_ops);
	if (IS_ERR((__force void *)vpu->m2m_dev)) {
		vepu_err(vpu, "Failed to init m2m device\n");
		ret = PTR_ERR((__force void *)vpu->m2m_dev);
		goto failed_m2m_init;
	}

	vfd = video_device_alloc();
	if (!vfd) {
		vepu_err(vpu, "Failed to allocate video device\n");
		goto failed_vfd_alloc;
	}

	strscpy(vfd->name, "syna-h1-encoder", sizeof(vfd->name));
	vfd->fops = &syna_venc_fops;
	vfd->release = video_device_release;
	vfd->lock = &vpu->mutex;
	vfd->v4l2_dev = &vpu->v4l2_dev;
	vfd->vfl_dir = VFL_DIR_M2M;
	vfd->device_caps = V4L2_CAP_STREAMING | V4L2_CAP_VIDEO_M2M_MPLANE;
	vfd->ioctl_ops = &syna_venc_ioctl_ops;

	ret = video_register_device(vfd, VFL_TYPE_VIDEO, -1);
	if (ret)
		goto failed_venc_release;

	vpu->vfd = vfd;
	video_set_drvdata(vfd, vpu);

	return 0;

failed_venc_release:
	video_device_release(vfd);
failed_vfd_alloc:
	v4l2_m2m_release(vpu->m2m_dev);
failed_m2m_init:
	v4l2_device_unregister(&vpu->v4l2_dev);
	return ret;
}

int syna_vepu_h1_finalize(struct syna_vpu_dev *vpu)
{
	if (vpu->m2m_dev)
		v4l2_m2m_release(vpu->m2m_dev);

	if (vpu->vfd)
		video_unregister_device(vpu->vfd);

	v4l2_device_unregister(&vpu->v4l2_dev);

	return 0;
}
