/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2021 - 2023 Synaptics Incorporated
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __SCALER_DRV_API_H__
#define __SCALER_DRV_API_H__

#define MTR_CONTENT_WIDTH_ALIGNMENT     256
#define MTR_CONTENT_HEIGHT_ALIGNMENT    64

#define MTR_ACTIVE_WIDTH_ALIGNMENT      64
#define MTR_ACTIVE_HEIGHT_ALIGNMENT     4

#define MTR_OUTPUT_WIDTH_ALIGNMENT      256
#define MTR_OUTPUT_HEIGHT_ALIGNMENT     8

/**
 * struct m2m_scaler_fmt - driver's internal color format data
 * @pixelformat:fourcc code for this format
 * @nb_planes:  number of planes  (ex: [0]=RGB/Y - [1]=Cb/Cr, ...)
 * @bpp:        bits per pixel (general)
 * @bpp_plane0: byte per pixel for the 1st plane
 * @w_align:    width alignment in pixel (multiple of)
 * @h_align:    height alignment in pixel (multiple of)
 */
struct m2m_scaler_fmt {
	u32         pixelformat;
	u8          nb_planes;
	u8          bpp;
	u8          bpp_plane0;
	u8          w_align;
	u8          h_align;
};

/**
 * struct frame_info - frame properties
 *
 * @width:      frame width (including padding)
 * @height:     frame height (including padding)
 * @fmt:        pointer to frame format descriptor
 * @field:      frame / field type
 * @bytesperline: stride of the 1st plane
 * @sizeimage:  image size in bytes
 * @crop:       crop area
 * @mtr_mode:   support io mmu for the frame;
 * @paddr:      image physical addresses per plane ([0]=RGB/Y - [1]=Cb/Cr, ...)
 * @vaddr:      virtual address of the planes
 */
struct frame_info {
	u32                         width;
	u32                         height;
	const struct m2m_scaler_fmt *fmt;
	enum v4l2_field             field;
	u32                         bytesperline;
	u32                         sizeimage;
	struct v4l2_rect            crop;
	u8                          mtr_mode;
	void                        *paddr[4];
	void                        *vaddr;
};

/**
 * struct scaler_api_req - SCALER Request properties
 *
 * @pUserData:    Private data will be used during callback
 * @src:          source frame properties
 * @dst:          destination frame properties
 */

struct scaler_api_req {
	void                  *pUserData;
	struct frame_info     *src;
	struct frame_info     *dst;
	int                   io_mmu_buffer_output;
	int                   io_mmu_buffer_capture;
};

/**
 * struct scaler_api_cfg - SCALER configuration properties
 *
 * @cbFunc:       Callback function once frame is processed
 */

struct scaler_api_cfg {
	/**
	 * @brief when SCALER HAL finish one requestor, it callback this function.
	 *
	 * @param Req      reutrned Req
	 */
	int (*cbFunc)(struct scaler_api_req *Req);
};

struct scaler_drv_ctx {
	struct scaler_api_cfg   cfg;
	int                     instance_id;
};

struct scaler_api_push_buf_ctx {
	struct scaler_drv_ctx *pscaler_drv_ctx;
	struct scaler_api_req *req;
};

HRESULT scaler_api_open(struct scaler_api_cfg cfg, void **ppScaler);
HRESULT scaler_api_init(void);
HRESULT scaler_api_deinit(void);
HRESULT scaler_api_pushBuf(struct scaler_drv_ctx *pscaler_drv_ctx, struct scaler_api_req *req);
HRESULT scaler_api_close(void *pScaler);

#endif  // __SCALER_DRV_API_H__
