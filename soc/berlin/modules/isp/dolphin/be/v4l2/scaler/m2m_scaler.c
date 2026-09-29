// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 Synaptics Incorporated
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/version.h>

#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mem2mem.h>

#include "common.h"
#include "m2m_scaler.h"
#include "isp_dma_heap.h"
#include "scaler_dbg.h"

#define USE_ISP_DMA_HEAP 1

#ifndef USE_ISP_DMA_HEAP
#include <media/videobuf2-dma-contig.h>
#endif

#define SUPPORTED_NUM_PLANES            2

#define SCALER_OUTPUT_DEF_WIDTH         1920
#define SCALER_OUTPUT_DEF_HEIGHT        1080

#define SCALER_DEF_WIDTH_4K             3840
#define SCALER_DEF_HEIGHT_4K            2160

#define SCALER_CAPTURE_DEF_WIDTH        640
#define SCALER_CAPTURE_DEF_HEIGHT       480

#define V4L2_CID_USER_BERLIN_BASE       (V4L2_CID_USER_BASE + 0x1090)
#define V4L2_CID_IOMMU_OUTPUT_BUFFER    (V4L2_CID_USER_BERLIN_BASE + 0)
#define V4L2_CID_IOMMU_CAPTURE_BUFFER   (V4L2_CID_USER_BERLIN_BASE + 1)
#define V4L2_CID_OUTPUT_BUFFER_TYPE     (V4L2_CID_USER_BERLIN_BASE + 2)

#define SCALER_MAX_CTRL_NUM             10
#define SCALER_DELAYED_WORK_DELAY       (30 * 1000)

#define Y_PLANE                         0
#define UV_PLANE                        1
#define SYNA_SCALER_MAX_BUF_SLOT        (32U)
#define CONTIGOUS_MEMORY_TYPE           0
#define NON_CONTIGOUS_MEMORY_TYPE       1

#define SYNA_DRIVER_NAME                "syna-scaler"
#define SYNA_CARD_TYPE                  "dolphin-isp"
#define SYNA_SCALER_NAME                "syna,dolphin-scaler"

#define fh_to_ctx(__fh) container_of(__fh, struct m2m_scaler_ctx, fh)

int m2m_scaler_max_instance = SCALER_MAX_INSTANCE;

enum m2m_scaler_dev_flags {
	SCALER_M2M_OPEN,            /* Driver opened */
	SCALER_M2M_RUNNING,         /* HW device running */
};

static const struct m2m_scaler_fmt m2m_scaler_formats[] = {
	{
		.pixelformat    = V4L2_PIX_FMT_RGB24,
		.nb_planes      = 1,
		.bpp            = 24,  // RGB24 has 24 bits per pixel
		.bpp_plane0     = 8,   // 8 bits for each channel (R, G, B)
		.w_align        = 2,
		.h_align        = 2
	},
	{
		.pixelformat    = V4L2_PIX_FMT_NV12M,
		.nb_planes      = 2,
		.bpp            = 12,
		.bpp_plane0     = 8,
		.w_align        = 2,
		.h_align        = 2
	},
};

static const struct frame_info scaler_output_dflt_fmt = {
	.width          = SCALER_OUTPUT_DEF_WIDTH,
	.height         = SCALER_OUTPUT_DEF_HEIGHT,
	.fmt            = &m2m_scaler_formats[0],
	.field          = V4L2_FIELD_NONE,
	.bytesperline   = SCALER_OUTPUT_DEF_WIDTH,
	.sizeimage      = (SCALER_OUTPUT_DEF_WIDTH * SCALER_OUTPUT_DEF_HEIGHT),
	.crop           = {0, 0, SCALER_OUTPUT_DEF_WIDTH, SCALER_OUTPUT_DEF_HEIGHT},
	.paddr          = {0, 0, 0, 0}
};

static const struct frame_info scaler_capture_dflt_fmt = {
	.width          = SCALER_CAPTURE_DEF_WIDTH,
	.height         = SCALER_CAPTURE_DEF_HEIGHT,
	.fmt            = &m2m_scaler_formats[0],
	.field          = V4L2_FIELD_NONE,
	.bytesperline   = SCALER_CAPTURE_DEF_WIDTH,
	.sizeimage      = (SCALER_CAPTURE_DEF_WIDTH * SCALER_CAPTURE_DEF_HEIGHT),
	.crop           = {0, 0, SCALER_CAPTURE_DEF_WIDTH, SCALER_CAPTURE_DEF_HEIGHT},
	.paddr          = {0, 0, 0, 0}
};

static const struct m2m_scaler_fmt *m2m_scaler_find_fmt(u32 pixelformat)
{
	const struct m2m_scaler_fmt *fmt;
	unsigned int i;

	if (pixelformat == V4L2_PIX_FMT_NV12)
		pixelformat = V4L2_PIX_FMT_NV12M;

	for (i = 0; i < ARRAY_SIZE(m2m_scaler_formats); i++) {
		fmt = &m2m_scaler_formats[i];
		if (fmt->pixelformat == pixelformat)
			return fmt;
	}

	return NULL;
}

static struct frame_info *ctx_get_selection_frame(struct m2m_scaler_ctx *ctx,
		enum v4l2_buf_type type)
{
	if (type == V4L2_BUF_TYPE_VIDEO_OUTPUT)
		return &ctx->src;
	else
		return ERR_PTR(-EINVAL);
}

static struct frame_info *ctx_get_frame(struct m2m_scaler_ctx *ctx,
		enum v4l2_buf_type type)
{
	switch (type) {
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		return &ctx->src;
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
		return &ctx->dst;
	default:
		dev_dbg(ctx->m2m_scaler_dev->dev,
				"Wrong buffer/video queue type (%d)\n", type);
		break;
	}

	return ERR_PTR(-EINVAL);
}

static void m2m_scaler_job_finish(struct m2m_scaler_ctx *ctx, int vb_state)
{
	struct vb2_v4l2_buffer *src_vb, *dst_vb;

	if (WARN(!ctx, "Null hardware context\n"))
		return;

	if (WARN(!ctx->fh.m2m_ctx, "Null hardware m2m ctx context\n"))
		return;

	dev_dbg(ctx->m2m_scaler_dev->dev, "%s is invoked\n", __func__);

	if (scaler_dump_debugfs)
		scaler_dbg_perf_job_end(ctx->m2m_scaler_dev, ctx->m2m_scaler_ctx->instance_id);

	src_vb = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
	dst_vb = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);

	if (src_vb && dst_vb) {
		dst_vb->vb2_buf.timestamp = src_vb->vb2_buf.timestamp;
		dst_vb->timecode = src_vb->timecode;
		dst_vb->flags &= ~V4L2_BUF_FLAG_TSTAMP_SRC_MASK;
		dst_vb->flags |= src_vb->flags &
			V4L2_BUF_FLAG_TSTAMP_SRC_MASK;

		if (src_vb->vb2_buf.state == VB2_BUF_STATE_ACTIVE)
			v4l2_m2m_buf_done(src_vb, vb_state);
		if (dst_vb->vb2_buf.state == VB2_BUF_STATE_ACTIVE)
			v4l2_m2m_buf_done(dst_vb, vb_state);

	}

	/* In case of error , need to clean up */
	if (vb_state == VB2_BUF_STATE_ERROR) {

		if (src_vb && src_vb->vb2_buf.state == VB2_BUF_STATE_ACTIVE)
			v4l2_m2m_buf_done(src_vb, vb_state);
		if (dst_vb && dst_vb->vb2_buf.state == VB2_BUF_STATE_ACTIVE)
			v4l2_m2m_buf_done(dst_vb, vb_state);

	}

	if (src_vb || dst_vb)
		v4l2_m2m_job_finish(ctx->m2m_scaler_dev->m2m.m2m_dev,
				ctx->fh.m2m_ctx);
	clear_bit(SCALER_M2M_RUNNING, &ctx->m2m_scaler_dev->state);
	complete(&ctx->work_done);
}

static void delayed_m2m_scaler_work_function(struct work_struct *work)
{
	struct delayed_work *pdelay = container_of(work,
			struct delayed_work, work);
	struct m2m_scaler_ctx *ctx = container_of(pdelay,
			struct m2m_scaler_ctx,  scaler_delayed_work);

	pr_info("%s: timedout events\n", __func__);
	m2m_scaler_job_finish(ctx, VB2_BUF_STATE_ERROR);
}

static int m2m_scaler_scaler_callback(struct scaler_api_req *req)
{
	struct m2m_scaler_ctx	*ctx;
	int ret = 0;

	if (req == NULL) {
		ret = -EINVAL;
		dev_err(ctx->m2m_scaler_dev->dev, "SCALER Res is NULL\n");
		goto mem_error;
	}

	ctx = (struct m2m_scaler_ctx *)req->pUserData;
	if (ctx) {
		/* cancel the delayed work as response has come */
		cancel_delayed_work(&ctx->scaler_delayed_work);
		m2m_scaler_job_finish(ctx, VB2_BUF_STATE_DONE);
	} else {
		ret = -EINVAL;
		dev_err(ctx->m2m_scaler_dev->dev, "Invalid ctx\n");
		goto ctx_error;
	}

ctx_error:
	kfree(req);
mem_error:
	return ret;
}

#ifdef BYPASS_SCALER_DRIVER
static void m2m_scaler_copy_bufs(struct m2m_scaler_ctx *ctx)
{
	struct frame_info *src, *dst;

	src = &ctx->src;
	dst = &ctx->dst;

	memcpy(dst->vaddr, src->vaddr,
			((src->bytesperline * src->height) * 3) / 2);
	dev_info(ctx->m2m_scaler_dev->dev,
			"src vaddr: %x, dst addr: %x\n",
			src->vaddr, dst->vaddr);
}
#else
static int m2m_scaler_process_bufs(struct m2m_scaler_ctx *ctx)
{
	struct scaler_api_req *req = NULL;
	int ret = 0;

	req = kzalloc(sizeof(*req), GFP_KERNEL);
	if (!req) {
		ret = -ENOMEM;
		dev_err(ctx->m2m_scaler_dev->dev, "fails memory for SCALER request\n");
		goto mem_error;
	}

	req->pUserData = (void *)ctx;
	req->src = &ctx->src;
	req->dst = &ctx->dst;
	req->io_mmu_buffer_output = ctx->io_mmu_buffer_output;
	req->io_mmu_buffer_capture = ctx->io_mmu_buffer_capture;

	ret = scaler_api_pushBuf(ctx->m2m_scaler_ctx, req);

	if (ret) {
		ret = -EFAULT;
		dev_err(ctx->m2m_scaler_dev->dev, "failed to push scaler request\n");
		goto scaler_error;
	}
	return 0;

scaler_error:
	kfree(req);
mem_error:
	return ret;
}
#endif

#ifdef USE_WORKER_PROCESS_SCALER
static void m2m_scaler_work_function(struct work_struct *work)
{
	struct m2m_scaler_ctx *ctx = container_of(work, struct m2m_scaler_ctx,  scaler_work);

	m2m_scaler_process_bufs(ctx);
}
#endif

static inline unsigned long get_physical_address(bool is_io_mmu_enable,
		void *cookie) {
	if (is_io_mmu_enable)
		return (unsigned long) isp_dma_heap_get_pagetbl_phyaddr(cookie);
	else
		return (unsigned long) isp_dma_heap_get_phyaddr(cookie);
}

static int m2m_scaler_get_addr(struct m2m_scaler_ctx *ctx, struct vb2_buffer *vb,
		struct frame_info *frame, short is_io_mmu_enable, short buffer_type,
		void **paddr, void **vaddr)
{
	if (!vb || !frame)
		return -EINVAL;

#ifdef USE_ISP_DMA_HEAP
	paddr[0] = (void *) get_physical_address(is_io_mmu_enable,
			isp_dma_heap_plane_cookie(vb, Y_PLANE));
#else
	paddr[0] = (void *) vb2_dma_contig_plane_dma_addr(vb, Y_PLANE);
#endif

	if (buffer_type == NON_CONTIGOUS_MEMORY_TYPE)
#ifdef USE_ISP_DMA_HEAP
		paddr[1] = (void *) get_physical_address(is_io_mmu_enable,
				isp_dma_heap_plane_cookie(vb, UV_PLANE));
#else
	    paddr[1] = (void *) vb2_dma_contig_plane_dma_addr(vb, UV_PLANE);
#endif
	else
		paddr[1] = (paddr[0] +
				frame->bytesperline * frame->height);

#ifdef BYPASS_SCALER_DRIVER
	*vaddr = vb2_plane_vaddr(vb, 0);
#endif

	dev_dbg(ctx->m2m_scaler_dev->dev,
			"Vb2 buffer paddr %lx %lx\n", (unsigned long) paddr[0],
			(unsigned long) paddr[1]);

	return 0;
}

static int m2m_scaler_get_bufs(struct m2m_scaler_ctx *ctx)
{
	struct frame_info *src, *dst;
	struct vb2_v4l2_buffer *src_vb, *dst_vb;
	int ret;

	src = &ctx->src;
	dst = &ctx->dst;

	src_vb = v4l2_m2m_next_src_buf(ctx->fh.m2m_ctx);
	ret = m2m_scaler_get_addr(ctx, &src_vb->vb2_buf, src,
			ctx->io_mmu_buffer_output, ctx->output_buffer_memory_type,
			src->paddr, &src->vaddr);

	if (ret)
		return ret;

	dst_vb = v4l2_m2m_next_dst_buf(ctx->fh.m2m_ctx);
	ret = m2m_scaler_get_addr(ctx, &dst_vb->vb2_buf, dst,
			ctx->io_mmu_buffer_capture, NON_CONTIGOUS_MEMORY_TYPE,
			dst->paddr, &dst->vaddr);

	if (ret)
		return ret;

	dst_vb->vb2_buf.timestamp = src_vb->vb2_buf.timestamp;

	return 0;
}

static void m2m_scaler_device_run(void *priv)
{
	struct m2m_scaler_ctx *ctx = priv;
	struct m2m_scaler_dev *scaler;
	int err = 0;

	scaler = ctx->m2m_scaler_dev;
	dev_dbg(scaler->dev, "%s is invoked\n", __func__);

	if (scaler_dump_debugfs)
		scaler_dbg_perf_job_begin(scaler, ctx->m2m_scaler_ctx->instance_id);

	err = m2m_scaler_get_bufs(ctx);
	if (err) {
		dev_err(scaler->dev, "cannot get address\n");
		goto out;
	}

	set_bit(SCALER_M2M_RUNNING, &scaler->state);

#ifdef BYPASS_SCALER_DRIVER
	m2m_scaler_copy_bufs(ctx);
#else
#ifdef USE_WORKER_PROCESS_SCALER
	schedule_work(&ctx->scaler_work);
#else
	err = m2m_scaler_process_bufs(ctx);
#endif
#endif
	/* Schedule the delayed work to run after specified time  */
	schedule_delayed_work(&ctx->scaler_delayed_work,
			msecs_to_jiffies(SCALER_DELAYED_WORK_DELAY));
out:
	if (err) {
		m2m_scaler_job_finish(ctx, VB2_BUF_STATE_ERROR);
		clear_bit(SCALER_M2M_RUNNING, &scaler->state);
	}
}

static void m2m_scaler_abort_func(void *priv)
{
	struct m2m_scaler_ctx *ctx = priv;

	wait_for_completion_killable(&ctx->work_done);
	scaler_print("Delayed Work Module Unloaded\n");
}

static const struct v4l2_m2m_ops scaler_m2m_ops = {
	.device_run     = m2m_scaler_device_run,
	.job_abort		= m2m_scaler_abort_func
};

static int m2m_scaler_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct m2m_scaler_ctx *ctx;
	int ret = 0;

	ctx = container_of(ctrl->handler, struct m2m_scaler_ctx,
			ctrl_handler);

	if (ctrl->flags & V4L2_CTRL_FLAG_INACTIVE)
		goto out;

	switch (ctrl->id) {
	case V4L2_CID_IOMMU_OUTPUT_BUFFER:
		ctx->io_mmu_buffer_output = ctrl->val;
		dev_info(ctx->m2m_scaler_dev->dev, "io_mmu_buffer_output = %d\n",
				ctx->io_mmu_buffer_output);
		//force non-contiguous memory if output iommu is enabled
		if (ctrl->val)
			ctx->output_buffer_memory_type = NON_CONTIGOUS_MEMORY_TYPE;
		break;
	case V4L2_CID_IOMMU_CAPTURE_BUFFER:
		ctx->io_mmu_buffer_capture = ctrl->val;
		dev_info(ctx->m2m_scaler_dev->dev, "io_mmu_buffer_capture = %d\n",
				ctx->io_mmu_buffer_capture);
		break;
	case V4L2_CID_OUTPUT_BUFFER_TYPE:
		ctx->output_buffer_memory_type = ctrl->val;
		//force non-contiguous memory if output iommu is enabled
		if (!ctrl->val && ctx->io_mmu_buffer_output) {
			dev_warn(ctx->m2m_scaler_dev->dev, "fallback to non-contiguous memory\n");
			ctx->output_buffer_memory_type = NON_CONTIGOUS_MEMORY_TYPE;
		}
		dev_info(ctx->m2m_scaler_dev->dev, "output buffer type = %d\n",
				ctx->output_buffer_memory_type);
		break;
	default:
		dev_err(ctx->m2m_scaler_dev->dev, "unknown control %d\n", ctrl->id);
		ret = -EINVAL;
	}

	dev_info(ctx->m2m_scaler_dev->dev, "output buffer: io_mmu = %d\n",
			ctx->io_mmu_buffer_output);
	dev_info(ctx->m2m_scaler_dev->dev, "capture buffer: io_mmu = %d\n",
			ctx->io_mmu_buffer_capture);
	dev_info(ctx->m2m_scaler_dev->dev, "output buffer type = %d\n",
			ctx->output_buffer_memory_type);
out:
	return ret;
}

static const struct v4l2_ctrl_ops scaler_c_ops = {
	.s_ctrl = m2m_scaler_s_ctrl,
};

static struct v4l2_ctrl_config io_mmu_output_buffer = {
	.ops = &scaler_c_ops,
	.id = V4L2_CID_IOMMU_OUTPUT_BUFFER,
	.name = "IO MMU OUTPUT BUFFER",
	.type = V4L2_CTRL_TYPE_BOOLEAN,
	.min = 0,
	.max = 1,
	.step = 1,
	.def = 1, /* 0 -io_mmu disable , 1 -io_mmu enable */
};

static struct v4l2_ctrl_config io_mmu_capture_buffer = {
	.ops = &scaler_c_ops,
	.id = V4L2_CID_IOMMU_CAPTURE_BUFFER,
	.name = "IO MMU CAPTURE BUFFER",
	.type = V4L2_CTRL_TYPE_BOOLEAN,
	.min = 0,
	.max = 1,
	.step = 1,
	.def = 1, /* 0 -io_mmu disable , 1 -io_mmu enable */
};

static struct v4l2_ctrl_config output_buffer_memory_type = {
	.ops = &scaler_c_ops,
	.id = V4L2_CID_OUTPUT_BUFFER_TYPE,
	.name = "OUTPUT BUFFER TYPE",
	.type = V4L2_CTRL_TYPE_BOOLEAN,
	.min = 0,
	.max = 1,
	.step = 1,
	.def = 1, /* 0 -contigous , 1 - non contigoues */
};
static int m2m_scaler_ctrls_create(struct m2m_scaler_ctx *ctx)
{
	int err = 0;

	if (ctx->ctrls_rdy)
		return 0;

	v4l2_ctrl_handler_init(&ctx->ctrl_handler, SCALER_MAX_CTRL_NUM);

	ctx->m2m_scaler_ctrls.io_mmu_buffer_output_ctrl = v4l2_ctrl_new_custom(&ctx->ctrl_handler,
			&io_mmu_output_buffer, NULL);
	ctx->m2m_scaler_ctrls.io_mmu_buffer_capture_ctrl = v4l2_ctrl_new_custom(&ctx->ctrl_handler,
			&io_mmu_capture_buffer, NULL);
	ctx->m2m_scaler_ctrls.output_buffer_memory_type_ctrl =
			v4l2_ctrl_new_custom(&ctx->ctrl_handler, &output_buffer_memory_type, NULL);

	if (ctx->ctrl_handler.error) {
		err = ctx->ctrl_handler.error;

		v4l2_ctrl_handler_free(&ctx->ctrl_handler);
		return err;
	}

	ctx->io_mmu_buffer_output = io_mmu_output_buffer.def;
	ctx->io_mmu_buffer_capture = io_mmu_capture_buffer.def;
	ctx->output_buffer_memory_type = output_buffer_memory_type.def;
	ctx->ctrls_rdy = true;

	return 0;
}

static void m2m_scaler_ctrls_delete(struct m2m_scaler_ctx *ctx)
{
	if (ctx->ctrls_rdy) {
		v4l2_ctrl_handler_free(&ctx->ctrl_handler);
		ctx->ctrls_rdy = false;
	}
}

static int m2m_scaler_queue_setup(struct vb2_queue *vq,
		unsigned int *nb_buf, unsigned int *nb_planes,
		unsigned int sizes[], struct device *alloc_devs[])
{
	struct m2m_scaler_ctx *ctx = vb2_get_drv_priv(vq);
	struct frame_info *frame = ctx_get_frame(ctx, vq->type);
	enum memory_type_t mem_type;
	int i = 0;

	if (IS_ERR(frame)) {
		dev_err(ctx->m2m_scaler_dev->dev, "Invalid frame (%p)\n", frame);
		return PTR_ERR(frame);
	}

	if (!frame->fmt) {
		dev_err(ctx->m2m_scaler_dev->dev, "Invalid format\n");
		return -EINVAL;
	}

	if (*nb_planes) {
		//validate number of planes
		if (*nb_planes != SUPPORTED_NUM_PLANES)
			return  -EINVAL;
		if (sizes[0] < frame->sizeimage)
			return  -EINVAL;
		if (vq->max_num_buffers >= SYNA_SCALER_MAX_BUF_SLOT)
			return -ENOBUFS;
	} else {
		/* Assign the allocate device for buffer creation */
		if (frame->fmt->pixelformat == V4L2_PIX_FMT_RGB24)
			*nb_planes = 1;
		else
			*nb_planes = 2;
	}
	switch (vq->type) {
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		mem_type = ctx->io_mmu_buffer_output ?
			SHM_NONSECURE_NON_CONTIG : SHM_NONSECURE_CONTIG;
		if (frame->fmt->pixelformat != V4L2_PIX_FMT_RGB24)
			*nb_planes = ctx->output_buffer_memory_type ? 2 : 1;
		break;
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
		mem_type = ctx->io_mmu_buffer_capture ?
		SHM_NONSECURE_NON_CONTIG : SHM_NONSECURE_CONTIG;
		break;
	default:
		dev_err(ctx->m2m_scaler_dev->dev, "invalid queue type: %d\n", vq->type);
		return -EINVAL;
	}
	for (i = 0; i < *nb_planes; ++i) {
		if (*nb_planes == 1)
			sizes[i] = frame->sizeimage;
		else
			sizes[i] = frame->sizeimage >> i;
		alloc_devs[i] = ctx->m2m_scaler_dev->alloc_dev[mem_type];
	}
	return 0;
}

static int m2m_scaler_buf_prepare(struct vb2_buffer *vb)
{
	struct m2m_scaler_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct frame_info *frame = ctx_get_frame(ctx, vb->vb2_queue->type);

	if (IS_ERR(frame)) {
		dev_err(ctx->m2m_scaler_dev->dev, "Invalid frame (%p)\n", frame);
		return PTR_ERR(frame);
	}

	if (vb->vb2_queue->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		vb2_set_plane_payload(vb, 0, frame->sizeimage);
	return 0;
}

static void m2m_scaler_buf_finish(struct vb2_buffer *vb)
{
	if (scaler_dump_debugfs) {
		struct m2m_scaler_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);

		if (vb->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
			scaler_dbg_perf_queue_end(ctx->m2m_scaler_dev,
					ctx->m2m_scaler_ctx->instance_id);
	}
}

static void m2m_scaler_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct m2m_scaler_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	int i = 0;

	if (V4L2_TYPE_IS_OUTPUT(vb->type)) {
		for (i = 0; i < vb->num_planes; ++i) {
			/* return to V4L2 any 0-size buffer so it can be dequeued by user */
			if (!vb2_get_plane_payload(vb, i)) {
				dev_info(ctx->m2m_scaler_dev->dev, "0 data buffer, skip it\n");
				vb2_buffer_done(vb, VB2_BUF_STATE_DONE);
				return;
			}
		}
	}

	if (ctx->fh.m2m_ctx)
		v4l2_m2m_buf_queue(ctx->fh.m2m_ctx, vbuf);
}

static int m2m_scaler_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct m2m_scaler_ctx *ctx = q->drv_priv;

	v4l2_m2m_update_start_streaming_state(ctx->fh.m2m_ctx, q);

	return 0;
}

static void m2m_scaler_stop_streaming(struct vb2_queue *q)
{
	struct m2m_scaler_ctx *ctx = q->drv_priv;
	struct vb2_v4l2_buffer *src_vb, *dst_vb;

	v4l2_m2m_update_stop_streaming_state(ctx->fh.m2m_ctx, q);

	if (V4L2_TYPE_IS_OUTPUT(q->type)) {
		while ((src_vb = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx)))
			v4l2_m2m_buf_done(src_vb, VB2_BUF_STATE_ERROR);
	} else {
		while ((dst_vb = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx))) {
			vb2_set_plane_payload(&dst_vb->vb2_buf, 0, 0);
			v4l2_m2m_buf_done(dst_vb, VB2_BUF_STATE_ERROR);
		}
	}
}

static const struct vb2_ops scaler_qops = {
	.queue_setup     = m2m_scaler_queue_setup,
	.buf_prepare     = m2m_scaler_buf_prepare,
	.buf_finish      = m2m_scaler_buf_finish,
	.buf_queue       = m2m_scaler_buf_queue,
	.wait_prepare    = vb2_ops_wait_prepare,
	.wait_finish     = vb2_ops_wait_finish,
	.stop_streaming  = m2m_scaler_stop_streaming,
	.start_streaming = m2m_scaler_start_streaming,
};

static int m2m_queue_init(void *priv,
		struct vb2_queue *src_vq, struct vb2_queue *dst_vq)
{
	struct m2m_scaler_ctx *ctx = priv;
	int ret;

	memset(src_vq, 0, sizeof(*src_vq));

	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;

	src_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	src_vq->drv_priv = ctx;
	src_vq->ops = &scaler_qops;
#ifdef USE_ISP_DMA_HEAP
	src_vq->mem_ops = &isp_dma_contig_memops;
#else
	src_vq->mem_ops = &vb2_dma_contig_memops;
#endif
	src_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	src_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	src_vq->lock = &ctx->lock;
	src_vq->dev = ctx->m2m_scaler_dev->v4l2_dev.dev;

	ret = vb2_queue_init(src_vq);
	if (ret)
		return ret;

	memset(dst_vq, 0, sizeof(*dst_vq));
	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

	dst_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	dst_vq->drv_priv = ctx;
	dst_vq->ops = &scaler_qops;
#ifdef USE_ISP_DMA_HEAP
	dst_vq->mem_ops = &isp_dma_contig_memops;
#else
	dst_vq->mem_ops = &vb2_dma_contig_memops;
#endif
	dst_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	dst_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	dst_vq->lock = &ctx->lock;
	dst_vq->dev = ctx->m2m_scaler_dev->v4l2_dev.dev;

	return vb2_queue_init(dst_vq);
}

static int m2m_scaler_querycap(struct file *file, void *fh,
		struct v4l2_capability *cap)
{
	strscpy(cap->driver, SYNA_DRIVER_NAME, sizeof(cap->driver));
	strscpy(cap->card, SYNA_CARD_TYPE, sizeof(cap->card));
	snprintf(cap->bus_info, sizeof(cap->bus_info),
			"platform: %s", SYNA_SCALER_NAME);
	return 0;
}

static void m2m_scaler_check_contigous_memory_required(struct m2m_scaler_ctx *ctx, int type,
		struct v4l2_pix_format_mplane *pix_mp)
{
	if (ctx->output_buffer_memory_type == CONTIGOUS_MEMORY_TYPE &&
			type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE &&
			pix_mp->pixelformat == V4L2_PIX_FMT_NV12M) {
		pix_mp->num_planes = 1;
		pix_mp->pixelformat = V4L2_PIX_FMT_NV12;
	}
}

static int m2m_scaler_enum_fmt(struct file *file, void *fh, struct v4l2_fmtdesc *f)
{
	const struct m2m_scaler_fmt *fmt;
	struct m2m_scaler_ctx *ctx = fh_to_ctx(fh);

	if (f->index >= ARRAY_SIZE(m2m_scaler_formats))
		return -EINVAL;

	fmt = &m2m_scaler_formats[f->index];
	f->pixelformat = fmt->pixelformat;

	if (ctx->output_buffer_memory_type == CONTIGOUS_MEMORY_TYPE &&
			f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE &&
			fmt->pixelformat == V4L2_PIX_FMT_NV12M) {
		f->pixelformat = V4L2_PIX_FMT_NV12;
	}

	return 0;
}

static void print_v4l2_pix_format_mplane(struct v4l2_pix_format_mplane *pix_mp)
{
	int i;

	if (pix_mp == NULL) {
		scaler_print("The structure pointer is NULL.\n");
		return;
	}

	scaler_print("Width: %u\n", pix_mp->width);
	scaler_print("Height: %u\n", pix_mp->height);
	scaler_print("Pixel Format: %u\n", pix_mp->pixelformat);
	scaler_print("Field: %u\n", pix_mp->field);
	scaler_print("Colorspace: %u\n", pix_mp->colorspace);
	scaler_print("ycbcr_enc: %u\n", pix_mp->ycbcr_enc);
	scaler_print("Quantization: %u\n", pix_mp->quantization);
	scaler_print("Flags: %u\n", pix_mp->flags);
	scaler_print("Transfer Func: %u\n", pix_mp->xfer_func);
	scaler_print("Number of planes: %u\n", pix_mp->num_planes);

	for (i = 0; i < pix_mp->num_planes; ++i) {
		scaler_print("Plane %d sizeimage: %u\n",
				i, pix_mp->plane_fmt[i].sizeimage);
		scaler_print("Plane %d bytesperline: %u\n",
				i, pix_mp->plane_fmt[i].bytesperline);
	}
}

static int m2m_scaler_g_fmt_mp(struct file *file, void *fh, struct v4l2_format *fmt)
{
	struct m2m_scaler_ctx *ctx = fh_to_ctx(fh);
	struct v4l2_pix_format_mplane *pix_mp;
	struct frame_info *frame = NULL;
	u32 orig_width, orig_height;
	u32 align_width, align_height;
	int ret = 0;

	frame = ctx_get_frame(ctx, fmt->type);
	if (frame == NULL || frame->fmt == NULL) {
		ret = -EINVAL;
		goto done;
	}

	pix_mp = &fmt->fmt.pix_mp;
	pix_mp->pixelformat = frame->fmt->pixelformat;
	pix_mp->width = frame->width;
	pix_mp->height = frame->height;
	pix_mp->field = frame->field;

	if ((fmt->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE && ctx->io_mmu_buffer_output) ||
			(fmt->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE &&
			 ctx->io_mmu_buffer_capture)) {

		orig_width = ALIGN(pix_mp->width, MTR_ACTIVE_WIDTH_ALIGNMENT);
		orig_height = ALIGN(pix_mp->height, MTR_ACTIVE_HEIGHT_ALIGNMENT);

		align_width = (fmt->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) ?
			MTR_CONTENT_WIDTH_ALIGNMENT : MTR_OUTPUT_WIDTH_ALIGNMENT;
		align_height = (fmt->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) ?
			MTR_CONTENT_HEIGHT_ALIGNMENT : MTR_OUTPUT_HEIGHT_ALIGNMENT;

		v4l2_fill_pixfmt_mp(pix_mp,
				pix_mp->pixelformat,
				ALIGN(pix_mp->width, align_width),
				ALIGN(pix_mp->height, align_height));

		// Restore original width and height after adjustment
		pix_mp->width = orig_width;
		pix_mp->height = orig_height;
	}
	pix_mp->num_planes = frame->fmt->nb_planes;

	m2m_scaler_check_contigous_memory_required(ctx, fmt->type, pix_mp);
	print_v4l2_pix_format_mplane(pix_mp);

	dev_dbg(ctx->m2m_scaler_dev->dev, "%s: with w:%d h:%d str:%d size %d\n",
			__func__, frame->width, frame->height,
			frame->bytesperline, frame->sizeimage);

done:
	return ret;
}

static int m2m_scaler_g_fmt_out_mp(struct file *file, void *fh, struct v4l2_format *fmt)
{
	return m2m_scaler_g_fmt_mp(file, fh, fmt);
}

static int m2m_scaler_g_fmt_cap_mp(struct file *file, void *fh, struct v4l2_format *fmt)
{
	return m2m_scaler_g_fmt_mp(file, fh, fmt);
}

static int m2m_scaler_try_fmt_out_mp(struct file *file, void *fh, struct v4l2_format *fmt)
{
	struct m2m_scaler_ctx *ctx = fh_to_ctx(fh);
	struct v4l2_pix_format_mplane *pix_mp = &fmt->fmt.pix_mp;
	const struct m2m_scaler_fmt *format = NULL;
	struct frame_info *frame = NULL;
	u32 orig_width, orig_height;
	int ret = 0;

	if (ctx->output_buffer_memory_type == CONTIGOUS_MEMORY_TYPE &&
			pix_mp->pixelformat == V4L2_PIX_FMT_NV12) {
		dev_dbg(ctx->m2m_scaler_dev->dev, "contigous buffer req\n");
	} else {
		/* Check if the hardware supports the requested format, use the default
		 * * format otherwise.
		 */
		format = m2m_scaler_find_fmt(pix_mp->pixelformat);
		if (!format) {
			dev_warn(ctx->m2m_scaler_dev->dev, "Unknown format 0x%x\n",
					pix_mp->pixelformat);
			m2m_scaler_g_fmt_mp(file, fh, fmt);
			return ret;
		}
		pix_mp->pixelformat = format->pixelformat;
	}

	frame =  &ctx->src;
	if (frame == NULL)
		return -EINVAL;

	if (ctx->io_mmu_buffer_output) {
		orig_width = ALIGN(pix_mp->width, MTR_ACTIVE_WIDTH_ALIGNMENT);
		orig_height = ALIGN(pix_mp->height, MTR_ACTIVE_HEIGHT_ALIGNMENT);
		v4l2_fill_pixfmt_mp(pix_mp,
				pix_mp->pixelformat,
				ALIGN(pix_mp->width, MTR_CONTENT_WIDTH_ALIGNMENT),
				ALIGN(pix_mp->height, MTR_CONTENT_HEIGHT_ALIGNMENT));
		pix_mp->width = orig_width;
		pix_mp->height = orig_height;
	} else {
		v4l2_fill_pixfmt_mp(pix_mp,
				pix_mp->pixelformat,
				pix_mp->width,
				pix_mp->height);
	}
	pix_mp->field = V4L2_FIELD_NONE;

	m2m_scaler_check_contigous_memory_required(ctx, fmt->type, pix_mp);
	print_v4l2_pix_format_mplane(pix_mp);

	return ret;
}

static int m2m_scaler_try_fmt_cap_mp(struct file *file, void *fh, struct v4l2_format *fmt)
{
	struct m2m_scaler_ctx *ctx = fh_to_ctx(fh);
	struct v4l2_pix_format_mplane *pix_mp = &fmt->fmt.pix_mp;
	const struct m2m_scaler_fmt *format = NULL;
	struct frame_info *frame  = NULL;
	u32 orig_width, orig_height;
	int ret = 0;

	/* Check if the hardware supports the requested format, use the default
	 * format otherwise.
	 */
	format = m2m_scaler_find_fmt(pix_mp->pixelformat);
	if (!format) {
		dev_warn(ctx->m2m_scaler_dev->dev, "Unknown format 0x%x\n",
				pix_mp->pixelformat);
		m2m_scaler_g_fmt_mp(file, fh, fmt);
		return ret;
	}

	pix_mp->pixelformat = format->pixelformat;

	frame  =  &ctx->dst;
	if (frame == NULL)
		return -EINVAL;

	if (ctx->io_mmu_buffer_capture) {
		orig_width = ALIGN(pix_mp->width, MTR_ACTIVE_WIDTH_ALIGNMENT);
		orig_height = ALIGN(pix_mp->height, MTR_ACTIVE_HEIGHT_ALIGNMENT);
		v4l2_fill_pixfmt_mp(pix_mp,
				pix_mp->pixelformat,
				ALIGN(pix_mp->width, MTR_OUTPUT_WIDTH_ALIGNMENT),
				ALIGN(pix_mp->height, MTR_OUTPUT_HEIGHT_ALIGNMENT));
		pix_mp->width = orig_width;
		pix_mp->height = orig_height;
	} else {
		v4l2_fill_pixfmt_mp(pix_mp,
				pix_mp->pixelformat,
				pix_mp->width,
				pix_mp->height);
	}
	pix_mp->field = V4L2_FIELD_NONE;

	print_v4l2_pix_format_mplane(pix_mp);

	return ret;
}

static int m2m_scaler_s_fmt_mp(struct file *file, void *fh, struct v4l2_format *fmt)
{
	struct m2m_scaler_ctx *ctx = fh_to_ctx(fh);
	struct vb2_queue *vq;
	struct frame_info *frame = NULL;
	struct v4l2_pix_format_mplane *pix_mp = &fmt->fmt.pix_mp;
	int ret;

	/* Try setting the format and validate based on buffer type */
	if (fmt->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		ret = m2m_scaler_try_fmt_out_mp(file, fh, fmt);
	else if (fmt->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		ret = m2m_scaler_try_fmt_cap_mp(file, fh, fmt);
	if (ret < 0) {
		dev_err(ctx->m2m_scaler_dev->dev, "Cannot set format\n");
		return ret;
	}

	frame = ctx_get_frame(ctx, fmt->type);
	if (frame == NULL)
		return -EINVAL;

	/* Get the V4L2 queue based on the buffer type */
	vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, fmt->type);
	if (vb2_is_streaming(vq)) {
		dev_err(ctx->m2m_scaler_dev->dev, "queue (%d) busy\n", fmt->type);
		return -EBUSY;
	}

	/* Get pixel format details and validate */
	frame->fmt = m2m_scaler_find_fmt(pix_mp->pixelformat);
	if (!frame->fmt) {
		dev_warn(ctx->m2m_scaler_dev->dev,
			"Unknown format 0x%x, fallback to default\n",
			pix_mp->pixelformat);
		frame->fmt = &m2m_scaler_formats[0];
	}

	/* Set frame parameters based on the pixel format */
	frame->width = pix_mp->width;
	frame->height = pix_mp->height;
	frame->bytesperline = pix_mp->plane_fmt[0].bytesperline;
	frame->sizeimage = pix_mp->plane_fmt[0].sizeimage;
	frame->field = pix_mp->field;

	/* Set crop parameters based on frame format */
	frame->crop.width = frame->width;
	frame->crop.height = frame->height;
	frame->crop.left = 0;
	frame->crop.top = 0;

	m2m_scaler_check_contigous_memory_required(ctx, fmt->type, pix_mp);

	dev_dbg(ctx->m2m_scaler_dev->dev, "%s: with w:%d h:%d stride:%d size %d\n",
			__func__, frame->width, frame->height,
			frame->bytesperline, frame->sizeimage);

	print_v4l2_pix_format_mplane(pix_mp);
	return ret;
}

static int m2m_scaler_s_fmt_out_mp(struct file *file, void *fh, struct v4l2_format *fmt)
{
	return m2m_scaler_s_fmt_mp(file, fh, fmt);
}

static int m2m_scaler_s_fmt_cap_mp(struct file *file, void *fh, struct v4l2_format *fmt)
{
	return m2m_scaler_s_fmt_mp(file, fh, fmt);
}

static int m2m_scaler_ioctl_qbuf(struct file *file, void *priv,
		struct v4l2_buffer *buf)
{
	struct v4l2_fh *fh = file->private_data;

	if (scaler_dump_debugfs) {
		struct m2m_scaler_ctx *ctx = fh_to_ctx(fh);
		u32 ready_bufs = 0;

		if (buf->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
			ready_bufs = v4l2_m2m_num_src_bufs_ready(ctx->fh.m2m_ctx);
			scaler_dbg_perf_queue_begin(ctx->m2m_scaler_dev,
					ready_bufs, ctx->m2m_scaler_ctx->instance_id);
		}
	}

	return v4l2_m2m_qbuf(file, fh->m2m_ctx, buf);
}

static int is_rect_enclosed(struct v4l2_rect *a, struct v4l2_rect *b)
{
	/* Return 1 if a is enclosed in b, or 0 otherwise. */
	if (a->left < b->left || a->top < b->top)
		return 0;

	if (a->left + a->width > b->left + b->width)
		return 0;

	if (a->top + a->height > b->top + b->height)
		return 0;

	return 1;
}

static int m2m_scaler_g_selection(struct file *file, void *fh,
		struct v4l2_selection *s)
{
	struct frame_info *frame = NULL;
	struct m2m_scaler_ctx *ctx = fh_to_ctx(fh);

	frame = ctx_get_selection_frame(ctx, s->type);
	if (IS_ERR(frame)) {
		dev_dbg(ctx->m2m_scaler_dev->dev, "Invalid frame\n");
		return PTR_ERR(frame);
	}

	/* Crop not supported with RGB888 */
	if (frame->fmt->pixelformat == V4L2_PIX_FMT_RGB24)
		return -EINVAL;

	switch (s->type) {
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		switch (s->target) {
		case V4L2_SEL_TGT_CROP:
			/* cropped (composed) frame */
			s->r = frame->crop;
			break;
		case V4L2_SEL_TGT_CROP_DEFAULT:
			/* complete frame */
			s->r.left = 0;
			s->r.top = 0;
			s->r.width = frame->width;
			s->r.height = frame->height;
			break;
		default:
			dev_dbg(ctx->m2m_scaler_dev->dev,
					"Output Invalid target: 0x%X\n", s->target);
			return -EINVAL;
		}
		break;
	default:
		dev_dbg(ctx->m2m_scaler_dev->dev, "Invalid type: %d\n", s->type);
		return -EINVAL;
	}

	return 0;
}

static int m2m_scaler_s_selection(struct file *file, void *fh,
		struct v4l2_selection *s)
{
	struct frame_info *frame = NULL;
	struct m2m_scaler_ctx *ctx = fh_to_ctx(fh);
	struct v4l2_rect *in, out;

	bool valid = false;

	if (((s->type == V4L2_BUF_TYPE_VIDEO_OUTPUT) ||
				(s->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)) &&
			(s->target == V4L2_SEL_TGT_CROP))
		valid = true;

	if (!valid) {
		dev_dbg(ctx->m2m_scaler_dev->dev,
				"Invalid type: %d / target: 0x%x\n", s->type, s->target);
		return -EINVAL;
	}

	frame = ctx_get_selection_frame(ctx, s->type);
	if (IS_ERR(frame)) {
		dev_dbg(ctx->m2m_scaler_dev->dev, "Invalid frame\n");
		return PTR_ERR(frame);
	}

	/* Crop not supported with RGB888 */
	if (frame->fmt->pixelformat == V4L2_PIX_FMT_RGB24)
		return -EINVAL;

	in = &s->r;
	out = *in;

	/* Align and check origin */
	out.left = ALIGN(in->left, MTR_ACTIVE_HEIGHT_ALIGNMENT);
	out.top = ALIGN(in->top, MTR_ACTIVE_HEIGHT_ALIGNMENT);

	if ((out.left < 0) || (out.left >= frame->width) ||
			(out.top < 0) || (out.top >= frame->height)) {
		dev_err(ctx->m2m_scaler_dev->dev,
				"Invalid crop: %dx%d@(%d,%d) vs frame: %dx%d\n",
				out.width, out.height, out.left, out.top,
				frame->width, frame->height);
		return -EINVAL;
	}

	/* Align and check size */
	if (ctx->io_mmu_buffer_output)
		out.width = ALIGN(in->width, MTR_ACTIVE_WIDTH_ALIGNMENT);
	else
		out.width = ALIGN(in->width, MTR_ACTIVE_HEIGHT_ALIGNMENT);
	out.height = ALIGN(in->height, MTR_ACTIVE_HEIGHT_ALIGNMENT);

	if (((out.left + out.width) > frame->width) ||
			((out.top + out.height) > frame->height)) {
		dev_err(ctx->m2m_scaler_dev->dev,
				"Invalid crop: %dx%d@(%d,%d) vs frame: %dx%d\n",
				out.width, out.height, out.left, out.top,
				frame->width, frame->height);
		return -EINVAL;
	}

	/* Checks adjust constraints flags */
	if (s->flags & V4L2_SEL_FLAG_LE && !is_rect_enclosed(&out, in))
		return -ERANGE;

	if (s->flags & V4L2_SEL_FLAG_GE && !is_rect_enclosed(in, &out))
		return -ERANGE;

	if ((out.left != in->left) || (out.top != in->top) ||
			(out.width != in->width) || (out.height != in->height)) {
		dev_info(ctx->m2m_scaler_dev->dev,
				"%s crop updated: %dx%d@(%d,%d) -> %dx%d@(%d,%d)\n",
				__func__, in->width, in->height, in->left, in->top,
				out.width, out.height, out.left, out.top);
		*in = out;
	}

	ctx->cropping_enable = 1;

	dev_info(ctx->m2m_scaler_dev->dev,
			"crop: %dx%d@(%d,%d) vs frame: %dx%d\n",
			out.width, out.height, out.left, out.top,
			frame->width, frame->height);

	frame->crop = out;
	return 0;
}

static int m2m_scaler_streamon(struct file *file, void *priv,
		enum v4l2_buf_type type)
{
	struct v4l2_fh *fh = file->private_data;
	struct m2m_scaler_ctx *ctx = fh_to_ctx(fh);

	/* DNS block doesn't support color conversion if src fromat and
	 * dst format is different  then send error while streaming
	 */
	if (ctx->src.fmt->pixelformat != ctx->dst.fmt->pixelformat) {
		dev_err(ctx->m2m_scaler_dev->dev, "%s: No color conversion src fmt %d dst fmt %d\n",
			__func__, ctx->src.fmt->pixelformat, ctx->dst.fmt->pixelformat);
		return -EINVAL;
	}

	/* DNS block supports Upscale only in output path, not in capture.
	 * Upscaling is only allowed when crop is enabled. else send an error while streamon
	 */
	if ((type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE && ctx->cropping_enable == 0) &&
		(ctx->src.width < ctx->dst.width || ctx->src.height < ctx->dst.height)) {
		dev_err(ctx->m2m_scaler_dev->dev, "%s: No up Scaling src w=%dxh=%d dst w=%dxh=%d",
			__func__, ctx->src.width, ctx->src.height, ctx->dst.width, ctx->dst.height);
		return -EINVAL;
	}

	/* DNS block doesn't support SRC  beyond 3840x2160 */
	if ((ctx->src.width > SCALER_DEF_WIDTH_4K) ||
			(ctx->src.height > SCALER_DEF_HEIGHT_4K)) {
		dev_err(ctx->m2m_scaler_dev->dev, "%s: src not allowed w=%d src h=%d\n",
				__func__, ctx->src.width, ctx->src.height);
		return -EINVAL;
	}

	/* DNS block doesn't support DST beyond 1920x1080 */
	if ((ctx->dst.width > SCALER_OUTPUT_DEF_WIDTH) ||
			 (ctx->dst.height > SCALER_OUTPUT_DEF_HEIGHT)) {
		dev_err(ctx->m2m_scaler_dev->dev, "%s: scaling not allowed w=%d dst h=%d\n",
				__func__, ctx->dst.width, ctx->dst.height);
		return -EINVAL;
	}

	return v4l2_m2m_streamon(file, fh->m2m_ctx, type);
}

static const struct v4l2_ioctl_ops scaler_ioctl_ops = {
	.vidioc_querycap                = m2m_scaler_querycap,
	.vidioc_enum_fmt_vid_cap        = m2m_scaler_enum_fmt,
	.vidioc_enum_fmt_vid_out        = m2m_scaler_enum_fmt,
	.vidioc_g_fmt_vid_out_mplane    = m2m_scaler_g_fmt_out_mp,
	.vidioc_g_fmt_vid_cap_mplane    = m2m_scaler_g_fmt_cap_mp,
	.vidioc_try_fmt_vid_out_mplane	= m2m_scaler_try_fmt_out_mp,
	.vidioc_try_fmt_vid_cap_mplane	= m2m_scaler_try_fmt_cap_mp,
	.vidioc_s_fmt_vid_out_mplane	= m2m_scaler_s_fmt_out_mp,
	.vidioc_s_fmt_vid_cap_mplane    = m2m_scaler_s_fmt_cap_mp,
	.vidioc_g_selection             = m2m_scaler_g_selection,
	.vidioc_s_selection             = m2m_scaler_s_selection,
	.vidioc_reqbufs                 = v4l2_m2m_ioctl_reqbufs,
	.vidioc_create_bufs             = v4l2_m2m_ioctl_create_bufs,
	.vidioc_expbuf                  = v4l2_m2m_ioctl_expbuf,
	.vidioc_querybuf                = v4l2_m2m_ioctl_querybuf,
	.vidioc_qbuf                    = m2m_scaler_ioctl_qbuf,
	.vidioc_dqbuf                   = v4l2_m2m_ioctl_dqbuf,
	.vidioc_prepare_buf             = v4l2_m2m_ioctl_prepare_buf,
	.vidioc_streamon                = m2m_scaler_streamon,
	.vidioc_streamoff               = v4l2_m2m_ioctl_streamoff,
	.vidioc_subscribe_event         = v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event       = v4l2_event_unsubscribe,
};

/*
 * m2m_scaler_open - Open function for the scaler V4L2 driver
 *
 * This function is called when a user-space application opens the
 * scaler device.
 *
 * Parameters:
 * @file: Pointer to the file structure representing the opened device file
 *
 * Returns:
 * 0 on success, negative error code on failure
 */
static int m2m_scaler_open(struct file *file)
{
	struct m2m_scaler_dev *scaler = video_drvdata(file);
	struct m2m_scaler_ctx *ctx = NULL;
	struct scaler_api_cfg cfg;
	int ret;

	/* Acquire the scaler device lock */
	if (mutex_lock_interruptible(&scaler->lock))
		return -ERESTARTSYS;

	/* Check if device is already opened */
	if (scaler->ref_count > (m2m_scaler_max_instance - 1)) {
		pr_err("Device is already opened %d times.\n", m2m_scaler_max_instance);
		ret = -EBUSY;
		goto unlock;
	}

	/* Increment the reference count */
	scaler->ref_count++;

	/* Allocate memory for both context and node */
	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx) {
		ret = -ENOMEM;
		goto unlock;
	}
	ctx->m2m_scaler_dev = scaler;

	/* Initialize the V4L2 file hadle */
	v4l2_fh_init(&ctx->fh, scaler->m2m.vdev);

	/* Create scaler controls for the context */
	ret = m2m_scaler_ctrls_create(ctx);
	if (ret) {
		dev_err(scaler->dev, "Failed to create control\n");
		goto error_fh;
	}

	/* Use separate control handler per file handle */
	ctx->fh.ctrl_handler = &ctx->ctrl_handler;
	file->private_data = &ctx->fh;
	v4l2_fh_add(&ctx->fh);

	/* Set default formats for source and destination */
	ctx->src = scaler_output_dflt_fmt;
	ctx->dst = scaler_capture_dflt_fmt;
	ctx->cropping_enable = 0;

	/* Setup the device context for mem2mem mode */
	ctx->fh.m2m_ctx = v4l2_m2m_ctx_init(scaler->m2m.m2m_dev, ctx,
			m2m_queue_init);
	if (IS_ERR(ctx->fh.m2m_ctx)) {
		dev_err(scaler->dev, "Failed to initialize m2m context\n");
		ret = PTR_ERR(ctx->fh.m2m_ctx);
		goto error_ctx_fail;
	}

	cfg.cbFunc = m2m_scaler_scaler_callback;

	/* Open SCALER for the context */
	ret = scaler_api_open(cfg, (void **)&ctx->m2m_scaler_ctx);
	if (ret) {
		dev_err(scaler->dev, "SCALER open failed !!\n");
		goto error_scaler_fail;
	}

	scaler_dbg_clear(scaler, ctx->m2m_scaler_ctx->instance_id);

	/* Increment the reference count of the mem2mem (m2m) instance in the scaler device */
	scaler->m2m.refcnt++;

	set_bit(SCALER_M2M_OPEN, &scaler->state);

	/* Initialize the delayed work for the context */
	INIT_DELAYED_WORK(&ctx->scaler_delayed_work, delayed_m2m_scaler_work_function);

#ifdef USE_WORKER_PROCESS_SCALER
	INIT_WORK(&ctx->scaler_work, m2m_scaler_work_function);
#endif

	/* Each context has been assigned with each own lock */
	mutex_init(&ctx->lock);

	init_completion(&ctx->work_done);

	dev_dbg(scaler->dev, "driver opened, ctx = 0x%p\n", ctx);

	/* Release the scaler device lock */
	mutex_unlock(&scaler->lock);

	/* Return success status */
	return 0;

error_scaler_fail:
	v4l2_m2m_ctx_release(ctx->fh.m2m_ctx);
error_ctx_fail:
	m2m_scaler_ctrls_delete(ctx);
error_fh:
	v4l2_fh_del(&ctx->fh);
	v4l2_fh_exit(&ctx->fh);
	kfree(ctx);
unlock:
	/* Release the scaler device lock */
	mutex_unlock(&scaler->lock);

	/* Return the error code */
	return ret;
}

/*
 * m2m_scaler_release - Release function for the scaler V4L2 driver
 *
 * This function is called when a user-space application releases its
 * access to the scaler device.
 *
 * Parameters:
 * @file: Pointer to the file structure representing the opened device file
 *
 * Returns:
 * 0 on success, negative error code on failure
 */
static int m2m_scaler_release(struct file *file)
{
	struct m2m_scaler_ctx *ctx = fh_to_ctx(file->private_data);
	struct m2m_scaler_dev *scaler = ctx->m2m_scaler_dev;

	dev_dbg(scaler->dev, "%s is invoked\n", __func__);

	/* Acquire the scaler device lock */
	mutex_lock(&scaler->lock);

	/* Decrement the reference count */
	scaler->ref_count--;

	/* Destroy context specific lock */
	mutex_destroy(&ctx->lock);

#ifdef USE_WORKER_PROCESS_SCALER
	cancel_work_sync(&ctx->scaler_work);
#endif

	/* Cancel the delayed work associated with the context */
	cancel_delayed_work_sync(&ctx->scaler_delayed_work);

	/* Close SCALER associated with the context */
	scaler_api_close(ctx->m2m_scaler_ctx);

	/* Release the V4L2 mem2mem (m2m) context associated with the context file handle */
	v4l2_m2m_ctx_release(ctx->fh.m2m_ctx);

	/* Remove the context file handle from the V4L2 framework */
	v4l2_fh_del(&ctx->fh);

	/* Clean up the context file handle resources */
	v4l2_fh_exit(&ctx->fh);

	/* Delete the scaler controls associated with the context */
	m2m_scaler_ctrls_delete(ctx);

	/* Decrement the reference count of the mem2mem (m2m) instance in the scaler device */
	if (--scaler->m2m.refcnt <= 0)
		clear_bit(SCALER_M2M_OPEN, &scaler->state);

	/* Free the memory allocated for the context */
	kfree(ctx);

	/* Release the scaler device lock */
	mutex_unlock(&scaler->lock);

	/* Return success status */
	return 0;
}

static const struct v4l2_file_operations scaler_fops = {
	.owner          = THIS_MODULE,
	.open           = m2m_scaler_open,
	.release        = m2m_scaler_release,
	.poll           = v4l2_m2m_fop_poll,
	.unlocked_ioctl = video_ioctl2,
	.mmap           = v4l2_m2m_fop_mmap,
};

/*
 * m2m_scaler_register_device - Register the scaler device with V4L2 and video-dev
 *
 * This function initializes the scaler device, sets up its parameters,
 * and registers it with both the V4L2 and video-dev frameworks.
 *
 * Parameters:
 * @scaler: Pointer to the scaler device structure
 *
 * Returns:
 * 0 on success, negative error code on failure
 */
static int m2m_scaler_register_device(struct m2m_scaler_dev *scaler)
{
	int ret;

	/* Check for null pointer */
	if (!scaler)
		return -ENODEV;

	/* Initialize video device structure parameters */
	scaler->vdev.fops        = &scaler_fops;
	scaler->vdev.ioctl_ops   = &scaler_ioctl_ops;
	scaler->vdev.release     = video_device_release_empty;
	scaler->vdev.lock        = NULL;
	scaler->vdev.vfl_dir     = VFL_DIR_M2M;
	scaler->vdev.v4l2_dev    = &scaler->v4l2_dev;
	scaler->vdev.device_caps = V4L2_CAP_STREAMING | V4L2_CAP_VIDEO_M2M_MPLANE;
	snprintf(scaler->vdev.name, sizeof(scaler->vdev.name), "%s.%d",
			SCALER_NAME, scaler->id);

	video_set_drvdata(&scaler->vdev, scaler);

	scaler->m2m.vdev = &scaler->vdev;
	scaler->m2m.m2m_dev = v4l2_m2m_init(&scaler_m2m_ops);
	if (IS_ERR(scaler->m2m.m2m_dev)) {
		dev_err(scaler->dev, "failed to initialize v4l2-m2m device\n");
		return PTR_ERR(scaler->m2m.m2m_dev);
	}

	/* Register the video device with V4L2 and video-dev frameworks */
	ret = video_register_device(&scaler->vdev, VFL_TYPE_VIDEO, -1);
	if (ret) {
		dev_err(scaler->dev,
				"%s(): failed to register video device\n", __func__);
		v4l2_m2m_release(scaler->m2m.m2m_dev);
		return ret;
	}

	/* Return success status */
	return 0;
}

/*
 * m2m_scaler_unregister_device - Unregister the scaler device from V4L2 and video-dev
 *
 * This function unregisters the scaler device from both the V4L2 and video-dev frameworks
 * and releases associated resources.
 *
 * Parameters:
 * @scaler: Pointer to the scaler device structure
 */
static void m2m_scaler_unregister_device(struct m2m_scaler_dev *scaler)
{
	/* Check for null pointer */
	if (!scaler)
		return;

	/* Release scaler mem2mem (m2m) context */
	if (scaler->m2m.m2m_dev)
		v4l2_m2m_release(scaler->m2m.m2m_dev);

	/* Unregister the video device from V4L2 and video-dev frameworks */
	video_unregister_device(scaler->m2m.vdev);
}

/*
 * m2m_scaler_remove - Remove function for the scaler platform driver
 *
 * This function is called when the scaler platform driver is being removed.
 *
 * Parameters:
 * @pdev: Pointer to the platform device structure
 *
 * Returns:
 * 0 on success, negative error code on failure
 */
#if (KERNEL_VERSION(6, 0, 0) <= LINUX_VERSION_CODE)
static void m2m_scaler_remove(struct platform_device *pdev)
#else
static int m2m_scaler_remove(struct platform_device *pdev)
#endif
{
	int ret;
	struct m2m_scaler_dev *scaler = platform_get_drvdata(pdev);

	/* Unregister the scaler device from the V4L2 framework */
	m2m_scaler_unregister_device(scaler);

	/* Unregister the V4L2 device */
	v4l2_device_unregister(&scaler->v4l2_dev);

	/* Deinitialize the SCALER module */
	ret = scaler_api_deinit();
	if (ret != 0) {
		dev_err(&pdev->dev, "SCALER deinit failed!!\n");
#if (KERNEL_VERSION(6, 0, 0) <= LINUX_VERSION_CODE)
		return;
#else
		return ret;
#endif
	}

	scaler_dbg_remove(scaler);

	/* Release any memory resources allocated */
	isp_dma_heap_dev_release();

	/* Destroy the scaler device lock */
	mutex_destroy(&scaler->lock);

	/* Log driver unload information */
	dev_dbg(&pdev->dev, "%s driver unloaded\n", pdev->name);

#if (KERNEL_VERSION(6, 0, 0) > LINUX_VERSION_CODE)
	/* Return success status */
	return 0;
#endif
}

/*
 * m2m_scaler_probe - Probe function for the scaler platform driver
 *
 * This function is called when a scaler platform device is detected
 * and being probed.
 *
 * Parameters:
 * @pdev: Pointer to the platform device structure
 *
 * Returns:
 * 0 on success, negative error code on failure
 */
static int m2m_scaler_probe(struct platform_device *pdev)
{
	struct m2m_scaler_dev *scaler;
	struct device *dev = &pdev->dev;

	int ret;

	/* Log driver invoked */
	dev_dbg(dev, "%s is invoked\n", __func__);

	/* Allocate memory for the scaler device structure */
	scaler = devm_kzalloc(dev, sizeof(struct m2m_scaler_dev), GFP_KERNEL);
	if (!scaler)
		return -ENOMEM;

	/* Initialize scaler device parameters */
	scaler->pdev = pdev;
	scaler->dev = dev;
	platform_set_drvdata(pdev, scaler);

	/* Initialize the scaler device lock */
	mutex_init(&scaler->lock);

	/* Retrieve the scaler ID from device tree or platform data */
	if (dev->of_node)
		scaler->id = of_alias_get_id(pdev->dev.of_node, SCALER_NAME);
	else
		scaler->id = pdev->id;

	/* Initialize SCALER module */
	ret = scaler_api_init();
	if (ret) {
		dev_err(dev, "SCALER init failed %d!!\n", ret);
		goto err_scaler_fail;
	}

	/* Debug */
	scaler_dbg_create(scaler);

	/* Create alloc device for creating DMA memory */
	ret = isp_dma_heap_dev_alloc((scaler->alloc_dev));
	if (ret) {
		pr_err("%s(): failed to create allocate evice\n", __func__);
		goto memory_init_failed;
	}

	/* Register the scaler device with the V4L2 framework */
	ret = v4l2_device_register(dev, &scaler->v4l2_dev);
	if (ret) {
		dev_err(dev, "failed to register\n");
		goto err_dev_fail;
	}

	/* Register the scaler device with the video-dev framework */
	ret = m2m_scaler_register_device(scaler);
	if (ret) {
		dev_err(dev, "failed to register\n");
		goto err_reg_fail;
	}

	/* Log successful registration information */
	dev_info(dev, "%s%d registered as /dev/video%d\n", SCALER_NAME,
			scaler->id, scaler->vdev.num);

	/* Initialize the reference count */
	scaler->ref_count = 0;

	/* Return success status */
	return 0;

err_reg_fail:
	/* Unregister the V4L2 device */
	v4l2_device_unregister(&scaler->v4l2_dev);
err_dev_fail:
	/* Deinitialize SCALER */
	scaler_api_deinit();
memory_init_failed:
	scaler_dbg_remove(scaler);
	/* Release any memory resources allocated */
	isp_dma_heap_dev_release();
err_scaler_fail:
	/* Free allocated memory for the scaler device structure */
	mutex_destroy(&scaler->lock);
	devm_kfree(dev, scaler);

	/* Return the error code */
	return ret;
}

/* Compatible string for device tree match */
static const struct of_device_id scaler_match_types[] = {
	{.compatible = "syna,dolphin-isp-scaler"},
	{}
};

MODULE_DEVICE_TABLE(of, scaler_match_types);

static struct platform_driver scaler_driver = {
	.probe          = m2m_scaler_probe,
	.remove         = m2m_scaler_remove,
	.driver         = {
		.name           = SCALER_NAME,
		.of_match_table = scaler_match_types,
	},
};

module_platform_driver(scaler_driver);

MODULE_AUTHOR("Synaptics");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("V4L2 Scaler Driver");
