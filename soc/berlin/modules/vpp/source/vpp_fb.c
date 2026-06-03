// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2021 Synaptics Incorporated */

#include <linux/slab.h>
#include "vpp_fb.h"
#include "vpp_api.h"

static VBUF_INFO **gDescArray;
static int gDescIndex;
static VPP_MEM_LIST *vpp_shm_list;
extern vpp_config_params vpp_config_param;

static void MV_VPP_DsiCopyFrameBuffer(VBUF_INFO *vbufinfo, VPP_MEM *shm_handle, int y,
			int width, int height, int stride, int bpp)
{
	int i, j;
	unsigned char *ptr = shm_handle->k_addr;
	unsigned int *last_row = (uint32_t *)(vbufinfo->m_bufferID + (height * (width - 1) * bpp));
	uint32_t *src_p = (uint32_t *)(ptr + y * stride);
	uint32_t *dst_p;

	for (i = 0; i < height; i++) {
		dst_p = last_row + i;
		for (j = 0; j < width; j++, src_p++) {
			*dst_p = *src_p;
			dst_p -= height;
		}
	}
}

static void MV_VPP_CopyFrameBuffer(VBUF_INFO *vbufinfo, VPP_MEM *shm_handle, int y,
			int width, int height, int stride, int bpp, int disp_width, int disp_height)
{
	if (disp_width > disp_height) {
		unsigned char *ptr = shm_handle->k_addr;

		memcpy(vbufinfo->m_bufferID, ptr + y * stride, height * stride);
	} else {
		MV_VPP_DsiCopyFrameBuffer(vbufinfo, shm_handle, y, width, height, stride, bpp);
	}
}

static void destroy_vbuf_desc(VBUF_INFO *pVBufInfo)
{
	if (pVBufInfo) {
		if (pVBufInfo->m_bufferID != NULL)
			VPP_MEM_FreeMemory(vpp_shm_list, VPP_MEM_TYPE_DMA, pVBufInfo->hShm_fb);
		if (pVBufInfo->pVppVbufInfo_virt)
			VPP_MEM_FreeMemory(vpp_shm_list, VPP_MEM_TYPE_DMA, pVBufInfo->hShm_vbuf);
		kfree(pVBufInfo);
	}
}

static void build_frames(VBUF_INFO *vbufinfo, void *vbuf,
	int srcfmt, int bit_depth, int x, int y, int width,
	int height, int progressive, int pattern_type, bool IsPatt)
{
	unsigned int datasize;

	/* set other fields of logo frame descriptor */
	if (srcfmt == SRCFMT_YUV422) {
		vbufinfo->m_bytes_per_pixel = 2;
		vbufinfo->m_srcfmt = SRCFMT_YUV422;
		vbufinfo->m_order = ORDER_UYVY;
	} else {
		vbufinfo->m_bytes_per_pixel = 4;
		//MSB(4th) byte is always alpha(0x00) in android buffer
		//use ARGB32 for VPP TA and XRGB32 for fastlogo.ta
		vbufinfo->m_srcfmt = (!VPP_Is_Recovery_Mode()) ? SRCFMT_ARGB32 : SRCFMT_XRGB32;
	#ifdef VPP_ENABLE_ANDROID_FROMAT_BGRA_8888
		//ABGR_8888 or BGRA_8888
		vbufinfo->m_order = ORDER_BGRA;
	#else
		//RGBX_8888
		vbufinfo->m_order = ORDER_RGBA;
	#endif
	}

	datasize = width * height * vbufinfo->m_bytes_per_pixel;
	vbufinfo->m_content_width = width;
	vbufinfo->m_content_height = height;
	vbufinfo->m_buf_stride = vbufinfo->m_content_width *
					vbufinfo->m_bytes_per_pixel;
	vbufinfo->m_buf_size = (int)datasize;
	vbufinfo->m_flags = 1;
	vbufinfo->m_disp_offset = 0;
	vbufinfo->m_buf_stride = vbufinfo->m_content_width *
					vbufinfo->m_bytes_per_pixel;
	vbufinfo->m_active_width = width;
	vbufinfo->m_active_height = height;
	vbufinfo->m_active_left = x;
	vbufinfo->m_active_top = y;
	vbufinfo->m_content_offset = 0;
	if (IsPatt && vbuf) {
		if (srcfmt == SRCFMT_YUV422)
			MV_VPP_make_frame_data(1, vbuf, 32, 36, width,
				height, 0x40604060, 0xb0a0b0a0);
		else
			MV_VPP_make_frame_data(0, vbuf, 32, 36, width,
				height, 0xFF000000, 0xFF000000);
	}
}

static int create_new_frame(int width, int height, int progressive,
	int src_fmt, int bit_depth, int pattern_type,
	VBUF_INFO **frame)
{

	VBUF_INFO *vbufinfo;
	VPP_MEM *shm_handle_fb;
	VPP_MEM *shm_handle_vbuf;
	int result;
	bool fb_allocated = false;

	*frame = NULL;
	shm_handle_fb = NULL;

	shm_handle_vbuf = kzalloc(sizeof(VPP_MEM), GFP_KERNEL | __GFP_NOWARN);
	if (!shm_handle_vbuf) {
		return MV_VPP_ENOMEM;
	}

	vbufinfo  = kzalloc(sizeof(VBUF_INFO), GFP_KERNEL | __GFP_NOWARN);
	if (!vbufinfo)
		goto err_free_handles;

	//Allocate memory for copy/duplicate framebuffer
	if (vpp_config_param.enable_frame_buf_copy) {
		shm_handle_fb = kzalloc(sizeof(VPP_MEM), GFP_KERNEL | __GFP_NOWARN);
		if (!shm_handle_fb)
			goto err_free_handles;

		if (src_fmt == SRCFMT_YUV422)
			shm_handle_fb->size = width * height * 2;
		else
			shm_handle_fb->size = width * height * 4;

		result = VPP_MEM_AllocateMemory(vpp_shm_list, VPP_MEM_TYPE_DMA,
					shm_handle_fb, GFP_KERNEL | __GFP_NOWARN);
		if (result != VPP_MEM_ERROR_TYPE_OK) {
			pr_err("share memory allocation failed\n");
			goto err_free_vbufinfo;
		}
		fb_allocated = true;

		vbufinfo->hShm_fb = shm_handle_fb;
		vbufinfo->m_pbuf_start = (ARCH_PTR_TYPE)shm_handle_fb->p_addr;
		vbufinfo->m_bufferID = shm_handle_fb->k_addr;
	}

	//initialize VBUFINFO members
	build_frames(vbufinfo,
			shm_handle_fb ? shm_handle_fb->k_addr : NULL,
			src_fmt, bit_depth,
			0, 0, width, height,
			progressive, pattern_type, 1);

	//Allocate memory for VBUFINFO
	shm_handle_vbuf->size = sizeof(VPP_VBUF);
	result = VPP_MEM_AllocateMemory(vpp_shm_list, VPP_MEM_TYPE_DMA,
				shm_handle_vbuf, GFP_KERNEL | __GFP_NOWARN);
	if (result != VPP_MEM_ERROR_TYPE_OK) {
		pr_err("share memory allocation failed\n");
		goto err_free_fb_and_handles;
	}

	vbufinfo->hShm_vbuf = shm_handle_vbuf;
	vbufinfo->pVppVbufInfo_virt = shm_handle_vbuf->k_addr;
	vbufinfo->pVppVbufInfo_phy = shm_handle_vbuf->p_addr;

	*frame = vbufinfo;
	return MV_VPP_OK;

err_free_fb_and_handles:
	if (vpp_config_param.enable_frame_buf_copy && fb_allocated)
		VPP_MEM_FreeMemory(vpp_shm_list, VPP_MEM_TYPE_DMA, shm_handle_fb);

err_free_vbufinfo:
	kfree(vbufinfo);

err_free_handles:
	kfree(shm_handle_fb);
	kfree(shm_handle_vbuf);
	return MV_VPP_ENOMEM;
}

static void *MV_VPP_FB_GetFrame(VPP_MEM *shm_handle, int x,
			int y, int w, int h, int stride)
{
	int disp_width, disp_height;
	VBUF_INFO *vbufinfo = gDescArray[gDescIndex];
	vbufinfo = gDescArray[gDescIndex];
	MV_VPP_GetOutResolutionSize(CPCB_1, &disp_width, &disp_height);
	if (vbufinfo) {
		if (vpp_config_param.enable_frame_buf_copy) {
			MV_VPP_CopyFrameBuffer(vbufinfo, shm_handle, y, w, h, stride, vbufinfo->m_bytes_per_pixel, disp_width, disp_height);
		} else {
			vbufinfo->m_pbuf_start =  (ARCH_PTR_TYPE)shm_handle->p_addr;
		}

		VPP_MEM_FlushCache(vpp_shm_list, shm_handle, h*stride);
	}
	gDescIndex++;
	if (gDescIndex == vpp_config_param.fb_count)
		gDescIndex = 0;
	return vbufinfo;
}

static void convert_frame_info(VBUF_INFO *pVbufInfo, VPP_VBUF *pVppBuf)
{
	pVppBuf->m_pbuf_start = (ARCH_PTR_TYPE)pVbufInfo->m_pbuf_start;
	pVppBuf->m_srcfmt = pVbufInfo->m_srcfmt;
	pVppBuf->m_order = pVbufInfo->m_order;
	pVppBuf->m_bytes_per_pixel = pVbufInfo->m_bytes_per_pixel;
	pVppBuf->m_content_offset = pVbufInfo->m_content_offset;
	pVppBuf->m_content_width = pVbufInfo->m_content_width;
	pVppBuf->m_content_height = pVbufInfo->m_content_height;
	pVppBuf->m_buf_stride = pVbufInfo->m_buf_stride;

	pVppBuf->m_active_left = pVbufInfo->m_active_left;
	pVppBuf->m_active_top = pVbufInfo->m_active_top;
	pVppBuf->m_active_width = pVbufInfo->m_active_width;
	pVppBuf->m_active_height = pVbufInfo->m_active_height;
	pVppBuf->m_disp_offset = pVbufInfo->m_disp_offset;
	pVppBuf->m_is_frame_seq = 1;

	pVppBuf->m_is_top_field_first = 0;
	pVppBuf->m_is_repeat_first_field = 0;
	pVppBuf->m_is_progressive_pic = 1;
	pVppBuf->m_pixel_aspect_ratio = 0;
	pVppBuf->m_frame_rate_num = 0;
	pVppBuf->m_frame_rate_den = 0;

	pVppBuf->m_is_compressed = 0;
	pVppBuf->m_luma_left_ofst = 0;
	pVppBuf->m_luma_top_ofst = 0;
	pVppBuf->m_chroma_left_ofst = 0;
	pVppBuf->m_chroma_top_ofst = 0;

	//Indicate the bitdepth of the frame, if 8bit, is 8, if 10bit, is 10
	pVppBuf->m_bits_per_pixel = 8;
	pVppBuf->m_buf_pbuf_start_UV = 0;
	pVppBuf->m_buf_stride_UV = 0;

	pVppBuf->m_flags = pVbufInfo->m_flags;

	pVppBuf->builtinFrame = 0;

	pVppBuf->m_hBD = (unsigned int)0;
	pVppBuf->m_colorprimaries = 0;
}

static int create_global_desc_array(void)
{
	unsigned char uiCnt;
	int Ret = 0;
	int width, height;
	int uiPlaneId = PLANE_GFX1;

	MV_VPP_GetInputFrameSize(uiPlaneId, &width, &height);

	gDescArray = kzalloc(vpp_config_param.fb_count * sizeof(VBUF_INFO *), GFP_KERNEL | __GFP_NOWARN);
	if (gDescArray == NULL)
		return MV_VPP_ENOMEM;

	for (uiCnt = 0; uiCnt < vpp_config_param.fb_count; uiCnt++) {
		Ret = create_new_frame(width, height, 1,
			((!VPP_Is_Recovery_Mode()) ? SRCFMT_ARGB32 : SRCFMT_XRGB32),
			INPUT_BIT_DEPTH_8BIT, 0,
			&(gDescArray[uiCnt]));
		if (Ret != MV_VPP_OK) {
			pr_err("%s fails\n", __func__);
			goto create_global_desc_array_exit;
		}
	}
	return 0;

create_global_desc_array_exit:
	for (uiCnt = 0; uiCnt < vpp_config_param.fb_count; uiCnt++) {
		if (gDescArray[uiCnt])
			destroy_vbuf_desc(gDescArray[uiCnt]);
	}
	kfree(gDescArray);
	return Ret;
}

static void destroy_global_desc_array(void)
{
	unsigned char uiCnt;

	for (uiCnt = 0; uiCnt < vpp_config_param.fb_count; uiCnt++) {
		if (gDescArray[uiCnt])
			destroy_vbuf_desc(gDescArray[uiCnt]);
	}
	memset(gDescArray, 0, vpp_config_param.fb_count * sizeof(VBUF_INFO *));
	kfree(gDescArray);
}

void MV_VPP_FB_Deinit(void) {

	MV_VPP_Deinit();

	destroy_global_desc_array();
}
EXPORT_SYMBOL(MV_VPP_FB_Deinit);

int MV_VPP_FB_Init(VPP_MEM_LIST *mem_list) {
	int res;

	vpp_shm_list = mem_list;

	res = MV_VPP_Init(mem_list, vpp_config_param);
	if (res != MV_VPP_OK) {
		pr_info("MV_VPP_FB_Init:MV_VPP_Init - %d FAILED\n",res);
		goto EXIT_DESTROY;
	}

	res = create_global_desc_array();
	if (res != MV_VPP_OK) {
		pr_info("MV_VPP_FB_Init:create_global_desc_array - %d FAILED\n",res);
		goto EXIT_DESTROY;
	}

    return res;
EXIT_DESTROY:
	MV_VPP_Deinit();
	return res;
}
EXPORT_SYMBOL(MV_VPP_FB_Init);

void MV_VPP_FB_ConfigParams(vpp_config_params *param)
{
	vpp_plane_info pinfo = {};
	int maxFrameSizeNdx;
	int uiPlaneId = PLANE_GFX1;
	mrvl_frame_size frameSize[] = {
		{1280, 720},
		{1920, 1080}
	};

	memcpy(&vpp_config_param, param, sizeof(vpp_config_param));

	maxFrameSizeNdx = ARRAY_SIZE(frameSize);
	vpp_config_param.frame_size_ndx = vpp_config_param.frame_size_ndx >= maxFrameSizeNdx ? 0 : vpp_config_param.frame_size_ndx;

	pinfo.change_flag = 0;
	pinfo.frame_size.width = frameSize[vpp_config_param.frame_size_ndx].width;
	pinfo.frame_size.height = frameSize[vpp_config_param.frame_size_ndx].height;
	VPP_PLANE_INFO_CFLAG_SET(pinfo.change_flag, FRAMESIZE);
	MV_VPP_UpdatePlaneInfo(uiPlaneId, &pinfo);
}
EXPORT_SYMBOL(MV_VPP_FB_ConfigParams);

void MV_VPP_FB_DisplayFrame(VPP_MEM *shm_handle, int x,
			int y, int w, int h, int stride)
{
	VBUF_INFO *pVbufInfo = NULL;

	pVbufInfo = MV_VPP_FB_GetFrame(shm_handle, x, y, w, h, stride);
	if (pVbufInfo) {
		int uiPlaneId = PLANE_GFX1;

		VPP_VBUF *pVppBuf_virt = pVbufInfo->pVppVbufInfo_virt;
		phys_addr_t pVppBuf_phy = pVbufInfo->pVppVbufInfo_phy;

		pr_debug("%s:%d: pVppBuf_virt:%p, pVppBuf_phy:%x, x=%x, y=%x\n",
				__func__, __LINE__, pVppBuf_virt, (UINT32)(phys_addr_t)pVppBuf_phy, x, y);
		convert_frame_info(pVbufInfo, pVppBuf_virt);
		pr_debug("%s:%d: pVppBuf_virt:%p, pVppBuf_phy:%x, x=%x, y=%x, "
				"src_fmt:%d, src_order:%d\n", __func__, __LINE__,
				pVppBuf_virt, (UINT32)(phys_addr_t)pVppBuf_phy, x, y,
				pVppBuf_virt->m_srcfmt, pVppBuf_virt->m_order);

		VPP_MEM_FlushCache(vpp_shm_list, pVbufInfo->hShm_vbuf, sizeof(VPP_VBUF));
		MV_VPP_DisplayFrame(uiPlaneId, 0, pVbufInfo);
	}
}
EXPORT_SYMBOL(MV_VPP_FB_DisplayFrame);

void MV_VPP_FB_GetInputFrameSize(int *width, int *height)
{
	ENUM_PLANE_ID uiPlaneId = PLANE_GFX1;

	MV_VPP_GetInputFrameSize(uiPlaneId, width, height);
}
EXPORT_SYMBOL(MV_VPP_FB_GetInputFrameSize);
