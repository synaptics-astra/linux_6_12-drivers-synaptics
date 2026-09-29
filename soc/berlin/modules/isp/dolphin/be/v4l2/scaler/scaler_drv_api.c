// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 - 2023 Synaptics Incorporated
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/videodev2.h>
#include <linux/mutex.h>
#include <linux/delay.h>

#include "ispbe_api.h"
#include "ispbe_err.h"
#include "ispBE_common.h"
#include "common.h"
#include "scaler_drv_api.h"
#include "scaler_dbg.h"

#undef ENABLE_SCALER_REG_DUMP
#define ALIGN_SIZE(value, alignment) (((value) + (alignment - 1)) & ~(alignment - 1))
#define DNS_TIMEOUT_COUNTER 100

static void scalerIntrHandlerCallback(struct ISP_BE_RQST_MSG *rqstMsg)
{
	struct scaler_api_push_buf_ctx *pPushBufCtx = NULL;
	struct scaler_drv_ctx *pscaler_drv_ctx = NULL;

	scaler_print("[SCALER INTR] (rqstMsg=%p) (param=%p)\n",
			rqstMsg, rqstMsg->user_data);

	pPushBufCtx = (struct scaler_api_push_buf_ctx *)rqstMsg->user_data;
	if (pPushBufCtx == NULL) {
		pr_err("%s: Error: no SCALER ctx found\n", __func__);
		goto msg_error;
	}

	pscaler_drv_ctx = (struct scaler_drv_ctx *)pPushBufCtx->pscaler_drv_ctx;

	if (pscaler_drv_ctx == NULL) {
		pr_err("%s: Major error found with no SCALER ctx\n", __func__);
		goto ctx_error;
	}

	pscaler_drv_ctx->cfg.cbFunc(pPushBufCtx->req);
	scaler_print("[CB to COMP] Done\n");

ctx_error:
	kfree(pPushBufCtx);
msg_error:
	kfree(rqstMsg);
}

HRESULT scaler_api_init(void)
{
	struct ISPBE_CA_DRV_CTX drv_ctx;
	HRESULT result = SUCCESS;

	scaler_print("[INIT]\n");

	ISPSS_BE_DNSCL3_Probe(&drv_ctx);

	result = ISPBE_MODULE_Init(ISPBE_MODULE_DNSCL3, drv_ctx, NULL);
	if (result != SUCCESS) {
		pr_err("[INIT] Failed (result=0x%x)\n", result);
		goto err_scaler_fail;
	}

	scaler_print("[INIT] Success\n");
	return SUCCESS;

err_scaler_fail:
	return result;
}

HRESULT scaler_api_deinit(void)
{
	HRESULT result = SUCCESS;

	scaler_print("[DEINIT]\n");

	result = ISPBE_MODULE_Destroy(ISPBE_MODULE_DNSCL3);
	if (result != SUCCESS) {
		pr_err("[DEINIT] Failed (result=0x%x)\n", result);
		return result;
	}

	scaler_print("[DEINIT] success\n");
	return SUCCESS;
}

HRESULT scaler_api_open(struct scaler_api_cfg cfg, void **ppScaler)
{
	HRESULT result  = FAILURE;
	struct scaler_drv_ctx *pscaler_drv_ctx = NULL;

	scaler_print("[OPEN]\n");

	pscaler_drv_ctx = kzalloc(sizeof(*pscaler_drv_ctx), GFP_KERNEL);
	WARN_ON(!pscaler_drv_ctx);

	result = ISPBE_MODULE_Open(ISPBE_MODULE_DNSCL3, &pscaler_drv_ctx->instance_id,
			0, scalerIntrHandlerCallback);
	if (result != SUCCESS) {
		pr_err("[OPEN] Failed (result=0x%x)\n", result);
		return result;
	}

	scaler_print("Client ID : 0x%x\n", pscaler_drv_ctx->instance_id);
	pscaler_drv_ctx->cfg = cfg;
	scaler_print("[Open]pscaler_drv_ctx=%p cb=%p\n",
			pscaler_drv_ctx, pscaler_drv_ctx->cfg.cbFunc);
	result  = SUCCESS;

	*ppScaler = pscaler_drv_ctx;

	scaler_print("[OPEN] Success\n");
	return result;
}

HRESULT scaler_api_close(void *pScaler)
{
	HRESULT result  = SUCCESS;
	struct scaler_drv_ctx *pscaler_drv_ctx = (struct scaler_drv_ctx *)pScaler;
	UINT8 uWaitCnt = DNS_TIMEOUT_COUNTER;

	WARN_ON(!pscaler_drv_ctx);
	scaler_print("[CLOSE]\n");

	while (1) {
		result = ISPBE_MODULE_Close(ISPBE_MODULE_DNSCL3, pscaler_drv_ctx->instance_id);

		/*Wait for HW to become Idle */
		if (result == ISPSS_EHARDWAREBUSY && uWaitCnt > 0) {
			usleep_range(500, 1000);
			uWaitCnt--;
			continue;
		} else
			break;
	}

	if (result != SUCCESS) {
		pr_err("[CLOSE] Failed to shutdown BE HW\n");
		kfree(pscaler_drv_ctx);
		return result;
	}

	kfree(pscaler_drv_ctx);

	scaler_print("[CLOSE] Success\n");
	return SUCCESS;
}

static HRESULT scaler_disp2be_srcFmt(UINT32 dispFmt, enum ISPBE_ENUM_FMT *pBeFmt)
{
	HRESULT result = SUCCESS;

	switch (dispFmt) {
	case V4L2_PIX_FMT_NV16:
		*pBeFmt = ISPSS_SRCFMT_YUV422SP;
		break;
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV12M:
		*pBeFmt = ISPSS_SRCFMT_YUV420SP;
		break;
	case V4L2_PIX_FMT_YUV24:
		*pBeFmt = ISPSS_SRCFMT_YUV444P;
		break;
	case V4L2_PIX_FMT_YUV422P:
		*pBeFmt = ISPSS_SRCFMT_YUV422P;
		break;
	case V4L2_PIX_FMT_RGB444:
		*pBeFmt = ISPSS_SRCFMT_RGB444;
		break;
	case V4L2_PIX_FMT_RGB24:
		*pBeFmt = ISPSS_SRCFMT_RGB888;
		break;
	default:
		result = ISPSS_SRCFMT_MAX;
		break;
	}
	return result;
}

static HRESULT scaler_disp2be_bitDepth(UINT32 dispBpp, enum ISPBE_ENUM_BIT_DEPTH *pBeBpp)
{
	HRESULT result = SUCCESS;

	switch (dispBpp) {
	case 8:
		*pBeBpp = ISPSS_BIT_DEPTH_8BIT;
		break;
	case 10:
		*pBeBpp = ISPSS_BIT_DEPTH_10BIT;
		break;
	default:
		result = ISPSS_SRCFMT_MAX;
		break;
	}
	return  result;
}

static UINT32 getStride(int fmt, UINT32 uiWidth,
		UINT8 uDepth)
{
	UINT32 uiStride = 0;

	switch (fmt) {
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV12M:
	case V4L2_PIX_FMT_NV16:
		uiStride = (uiWidth * (uDepth))/8;
		break;
	case V4L2_PIX_FMT_YVU444M:
	case V4L2_PIX_FMT_RGB444:
	case V4L2_PIX_FMT_RGB24:
		uiStride = uiWidth * 3;
	default:
		break;
	}
	return uiStride;
}

static UINT32 scaler_getYUVSize(UINT32 dispFmt, UINT32 uiWidth, UINT32 height,
		UINT8 uiDepth, UINT32 *pYSize, UINT32 *pUVSize)
{
	HRESULT result = SUCCESS;
	UINT32 uiStride = 0;

	uiStride = getStride(dispFmt, uiWidth, uiDepth);

	if (!uiStride)
		result = ISPSS_SRCFMT_MAX;

	switch (dispFmt) {
	case V4L2_PIX_FMT_NV12:
	case V4L2_PIX_FMT_NV12M:
		*pYSize = (uiStride * height);
		*pUVSize = (uiStride * height)/2;
		break;
	case V4L2_PIX_FMT_NV16:
		*pYSize = uiStride * height;
		*pUVSize = uiStride * height;
		break;
	case V4L2_PIX_FMT_YVU444M:
	case V4L2_PIX_FMT_RGB444:
	case V4L2_PIX_FMT_RGB24:
		*pYSize = uiStride * height;
		*pUVSize = 0;
		break;
	default:
		result = ISPSS_SRCFMT_MAX;
		break;
	}

	return result;
}

static HRESULT scaler_fillRequest(struct scaler_drv_ctx *pscaler_drv_ctx,
		struct ISP_BE_RQST_MSG *pRqstMsg, struct scaler_api_req *req)
{
	HRESULT result  = SUCCESS;
	struct ISP_BE_DNSCL_RQST_MSG *pscaler_api_reqMsg = NULL;
	struct scaler_api_push_buf_ctx  *pPushBufCtx = NULL;

	scaler_print("[FILL REQUEST]\n");

	pRqstMsg->m_BuffID       = pscaler_drv_ctx->instance_id;
	pscaler_api_reqMsg = &pRqstMsg->dnscl;

	pRqstMsg->active.x   = req->src->crop.left;
	pRqstMsg->active.y   = req->src->crop.top;
	pRqstMsg->active.width  = req->src->crop.width;
	pRqstMsg->active.height = req->src->crop.height;

	pRqstMsg->content.width  = req->src->width;
	pRqstMsg->content.height = req->src->height;
	pRqstMsg->outwin.width   = req->dst->width;
	pRqstMsg->outwin.height  = req->dst->height;

	result = scaler_disp2be_bitDepth(req->src->fmt->bpp_plane0,
			(enum ISPBE_ENUM_BIT_DEPTH *)&pRqstMsg->in_frame_bit_depth);
	if (result != SUCCESS) {
		pr_err("result: %d Invalid In bit depth[%d]\n", result, req->src->fmt->bpp_plane0);
		goto error;
	}

	pRqstMsg->in_frame_bit_depth = req->src->fmt->bpp_plane0;

	result = scaler_disp2be_srcFmt(req->src->fmt->pixelformat,
			(enum ISPBE_ENUM_FMT *)&pRqstMsg->in_frame_fmt);
	if (result != SUCCESS) {
		pr_err("result: %d Invalid Infmt[%d]\n", result, req->src->fmt->pixelformat);
		goto error;
	}


	result = scaler_disp2be_bitDepth(req->dst->fmt->bpp_plane0,
			(enum ISPBE_ENUM_BIT_DEPTH *)&pRqstMsg->out_frame_bit_depth);
	if (result != SUCCESS) {
		pr_err("result: %d Invalid Out bitdepth[%d]\n", result, req->dst->fmt->bpp_plane0);
		goto error;
	}
	pRqstMsg->out_frame_bit_depth = req->dst->fmt->bpp_plane0;

	result = scaler_disp2be_srcFmt(req->dst->fmt->pixelformat,
			(enum ISPBE_ENUM_FMT *)&pRqstMsg->out_frame_fmt);
	if (result != SUCCESS) {
		pr_err("result: %d Invalid Outfmt[%d]\n", result, req->dst->fmt->pixelformat);
		goto error;
	}

	pRqstMsg->pBcmBuf      = NULL;

	pRqstMsg->out_mtrAddr_Y      = 0;
	pRqstMsg->out_mtrAddr_UV     = 0;
	pRqstMsg->OutputFrameAddr_Y  = req->dst->paddr[0];
	pRqstMsg->OutputFrameAddr_UV = req->dst->paddr[1];

	pRqstMsg->in_mtrAddr_Y       = 0;
	pRqstMsg->in_mtrAddr_UV      = 0;
	pRqstMsg->InputFrameAddr_Y  =  req->src->paddr[0];
	pRqstMsg->InputFrameAddr_UV =  req->src->paddr[1];

	if (req->io_mmu_buffer_output)
		pRqstMsg->in_mtrMode         = 2;
	else
		pRqstMsg->in_mtrMode         = 0;

	if (req->io_mmu_buffer_capture) {
		pRqstMsg->out_mtrMode        = 2;
		/* MMU: output stride is 256 aligned */
		pRqstMsg->out_frame_Ystride = ALIGN_SIZE(req->dst->width,
				MTR_CONTENT_WIDTH_ALIGNMENT);
	} else {
		pRqstMsg->out_mtrMode        = 0;
		/* non-IOMMU: output stride expects bytesperline */
		pRqstMsg->out_frame_Ystride = req->dst->bytesperline;
	}

	if (pRqstMsg->in_mtrMode) {
		/*  MMU input width is 256 aligned */
		pRqstMsg->content.width  = ALIGN_SIZE(req->src->width, MTR_CONTENT_WIDTH_ALIGNMENT);
		/*  DHUB crop expects ActiveW to be multiple of 64, whereas height to be
		 *   multiple of 4
		 */
		pRqstMsg->active.width  = ALIGN_SIZE(pRqstMsg->active.width,
							MTR_ACTIVE_WIDTH_ALIGNMENT);
		pRqstMsg->active.height  = ALIGN_SIZE(pRqstMsg->active.height,
							MTR_ACTIVE_HEIGHT_ALIGNMENT);
	}
	result = scaler_getYUVSize(req->src->fmt->pixelformat, pRqstMsg->content.width,
			req->src->height, req->src->fmt->bpp_plane0,
			&pRqstMsg->in_frame_Ysize, &pRqstMsg->in_frame_UVsize);
	if (result != SUCCESS) {
		pr_err("result: %d Invalid Size found for [%dx%d] depth[%d] fmt:%d\n",
			result, req->src->crop.width, req->src->crop.height,
			req->src->fmt->bpp_plane0, req->src->fmt->pixelformat);
		goto error;
	}

	result = scaler_getYUVSize(req->dst->fmt->pixelformat, pRqstMsg->out_frame_Ystride,
			req->dst->height, req->dst->fmt->bpp_plane0,
			&pRqstMsg->out_frame_Ysize, &pRqstMsg->out_frame_UVsize);
	if (result != SUCCESS) {
		pr_err("result: %d Invalid Size found for [%dx%d] depth[%d] fmt:%d\n",
			result, req->dst->width, req->dst->height,
			req->dst->fmt->bpp_plane0, req->dst->fmt->pixelformat);
		goto error;
	}


	scaler_print("In<%d> Size found for Active[%d x %d x %d x%d] Content[%d x %d]",
			pscaler_drv_ctx->instance_id, pRqstMsg->active.x, pRqstMsg->active.y,
			pRqstMsg->active.width, pRqstMsg->active.height,
			req->src->width, req->src->height);
	scaler_print("depth[%d] fmt:%d YAddr[%lx] UVAddr[%lx] Ysize(%d) UVSize(%d) ",
			req->src->fmt->bpp_plane0, pRqstMsg->in_frame_fmt,
			(unsigned long)pRqstMsg->InputFrameAddr_Y,
			(unsigned long)pRqstMsg->InputFrameAddr_UV,
			pRqstMsg->in_frame_Ysize, pRqstMsg->in_frame_UVsize);
	scaler_print(" inMtrMode:%d [%d %d]\n",
			pRqstMsg->in_mtrMode, pRqstMsg->in_mtrAddr_Y, pRqstMsg->in_mtrAddr_UV);
	scaler_print("Out<%d> Size found for Out[%dx%d] depth[%d] fmt:%d",
			pscaler_drv_ctx->instance_id, pRqstMsg->outwin.width,
			pRqstMsg->outwin.height, req->dst->fmt->bpp_plane0,
			pRqstMsg->out_frame_fmt);
	scaler_print("YAddr[%lx] UVAddr[%lx] Ysize(%d) UVSize(%d) mtrMode[%d] [%d %d]\n",
			(unsigned long)pRqstMsg->OutputFrameAddr_Y,
			(unsigned long)pRqstMsg->OutputFrameAddr_UV,
			pRqstMsg->out_frame_Ysize, pRqstMsg->out_frame_UVsize,
			pRqstMsg->out_mtrMode, pRqstMsg->out_mtrAddr_Y, pRqstMsg->out_mtrAddr_UV);


	pRqstMsg->bcm_prepared = 0;
	pscaler_api_reqMsg->isInputFrmIsp = 0;
	pscaler_api_reqMsg->isEnableSclBypass = 0;

	pRqstMsg->SubmitHw     = 1;
	pRqstMsg->WaitforIntr  = 0;

	pPushBufCtx = kzalloc(sizeof(*pPushBufCtx), GFP_KERNEL);
	if (pPushBufCtx == NULL)
		goto error;

	pPushBufCtx->pscaler_drv_ctx = pscaler_drv_ctx;
	pPushBufCtx->req     = req;

	pRqstMsg->user_data = (void *)pPushBufCtx;

	scaler_print("[FILL REQUEST] Success (pRqstMsg=%p) (param=%p)\n",
			pRqstMsg, pRqstMsg->user_data);
	return SUCCESS;
error:
	pr_err("[FILL REQUEST] Failed (result=0x%x)\n", result);
	return result;
}

HRESULT scaler_api_pushBuf(struct scaler_drv_ctx *pscaler_drv_ctx,
		struct scaler_api_req *req)
{
	HRESULT result = -1;
	struct ISP_BE_RQST_MSG *pRqstMsg = NULL;

	pRqstMsg = kzalloc(sizeof(struct ISP_BE_RQST_MSG), GFP_KERNEL);
	if (pRqstMsg == NULL)
		goto error_alloc;

	result = scaler_fillRequest(pscaler_drv_ctx, pRqstMsg, req);
	if (result != SUCCESS)
		goto error_fill;

	result = ISPBE_MODULE_PushRequest(ISPBE_MODULE_DNSCL3,
			pscaler_drv_ctx->instance_id, pRqstMsg);
	if (result != SUCCESS)
		goto error_push;

	scaler_print("[PUSH] Success\n");

	return SUCCESS;

error_push:
	kfree(pRqstMsg->user_data);
error_fill:
	kfree(pRqstMsg);
error_alloc:
	pr_err("[PUSH] Failed (result=0x%x)\n", result);

	return result;
}
