// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Synaptics Incorporated
 *
 *
 * Author: Lijun Fan <Lijun.Fan@synaptics.com>
 *
 */

#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/gpio/consumer.h>

#include <linux/delay.h>
#include "avio_core.h"
#include "drm_syna_gem.h"
#include "vpp_api.h"
#include "syna_vpp.h"
#include "drm_syna_drv.h"

#include "syna_drm_priv.h"
#include "syna_vpp_config.h"

#define VPP_GET_MTR_STRIDE(mtrcfg2) (((mtrcfg2 >> 4) & 0x3FFFFF) << (6 + 2))
#define DEFAULT_DEVICE_ROTATION 0
#define MAX_VBUF_INFO 10
#define MAX_ROTATE_BUFFER 3

/* MAX_PLANE_NUM : Number of planes allowed for rotation
 * Note : MAX_NUM_PLANES is defined in vpp_defines.h
 */
#define MAX_PLANE_NUM 2
#define  VPP_MAX_FRAME_TO_RELEASE_LOGO 3

#ifndef CONFIG_SYNA_DRM_DISABLE_ROTATION
long device_rotate;
static int in_use_device_rotate[MAX_NUM_PLANES];
VPP_MEM vpp_rotate_buffer_shm_handle[MAX_PLANE_NUM][MAX_ROTATE_BUFFER];
static phys_addr_t rotate_buffer_kernel_addr[MAX_PLANE_NUM][MAX_ROTATE_BUFFER];
static phys_addr_t rotate_buffer_phy_addr[MAX_PLANE_NUM][MAX_ROTATE_BUFFER];
static int last_rot_frame[MAX_PLANE_NUM+1];
#endif

extern bool en_builtin_frame_on_reset;

typedef struct syna_fl_cleanup_work_t {
	struct drm_device *dev;
	struct delayed_work delay_work;
	int crtcId;
} SYNA_FL_CLEANUP_WORK;

typedef struct __VPP_BUILD_IN_FRAME_INFO__ {
	uint32_t format_type;               //ARGB/YUV
	uint32_t pattern_wid, pattern_hgt;  //checker bar pattern size
	uint32_t frame_wid, frame_hgt;      //checker bar framesize
	uint32_t color1, color2;            //checker bar pattern color
	uint32_t bpp;                       //bytes per pixel
} VPP_BUILD_IN_FRAME_INFO;

//Build-in frame colors
#define VPP_BF_VID_CLR_GREEN    0x40604060
#define VPP_BF_VID_CLR_WHITE    0xb0a0b0a0
#define VPP_BF_VID_CLR_BLACK    0x00800080
#define VPP_BF_GFX_CLR_GREEN    0xFF008000
#define VPP_BF_GFX_CLR_WHITE    0xFFFFFFFF
#define VPP_BF_GFX_CLR_BLACK    0xFF000000
#ifdef VPP_BUILD_IN_FRAME_CHECKER_BAR
#define VPP_BF_VID_CLR_PIX1 VPP_BF_VID_CLR_GREEN
#define VPP_BF_VID_CLR_PIX2 VPP_BF_VID_CLR_WHITE
#define VPP_BF_GFX_CLR_PIX1 VPP_BF_GFX_CLR_GREEN
#define VPP_BF_GFX_CLR_PIX2 VPP_BF_GFX_CLR_WHITE
#else
#define VPP_BF_VID_CLR_PIX1 VPP_BF_VID_CLR_BLACK
#define VPP_BF_VID_CLR_PIX2 VPP_BF_VID_CLR_BLACK
#define VPP_BF_GFX_CLR_PIX1 VPP_BF_GFX_CLR_BLACK
#define VPP_BF_GFX_CLR_PIX2 VPP_BF_GFX_CLR_BLACK
#endif

static const VPP_BUILD_IN_FRAME_INFO vpp_buildin_frame_info[] = {
#ifdef VPP_BUILD_IN_FRAME_GFX_WIDTH
	{ SRCFMT_ARGB32, 32, 36,
		VPP_BUILD_IN_FRAME_GFX_WIDTH, VPP_BUILD_IN_FRAME_GFX_HEIGHT,
		VPP_BF_GFX_CLR_PIX1, VPP_BF_GFX_CLR_PIX2, 4},
#endif
#ifdef VPP_BUILD_IN_FRAME_VID_WIDTH
	{ SRCFMT_YUV422, 32, 36,
		VPP_BUILD_IN_FRAME_VID_WIDTH, VPP_BUILD_IN_FRAME_VID_HEIGHT,
		VPP_BF_VID_CLR_PIX1, VPP_BF_VID_CLR_PIX2, 2},
#endif
#ifdef VPP_BUILD_IN_FRAME_GFX_NULL_WIDTH
	{ SRCFMT_ARGB32, 32, 36,
		VPP_BUILD_IN_FRAME_GFX_NULL_WIDTH, VPP_BUILD_IN_FRAME_GFX_NULL_HEIGHT,
		VPP_BF_GFX_CLR_PIX1, VPP_BF_GFX_CLR_PIX2, 4},
#endif
};

VPP_MEM vpp_buildin_buffer_shm_handle[VPP_BUILD_IN_FRAME_TYPE_MAX];
static void *buildin_buffer_kernel_addr[VPP_BUILD_IN_FRAME_TYPE_MAX];
static phys_addr_t buildin_buffer_phy_addr[VPP_BUILD_IN_FRAME_TYPE_MAX];

VPP_MEM vpp_disp_info_shm_handle[MAX_NUM_PLANES][MAX_VBUF_INFO];
static VBUF_INFO vpp_disp_desc_array[MAX_NUM_PLANES][MAX_VBUF_INFO];
static int vbuf_info_num[MAX_NUM_PLANES] = {0};
static int init_vbuf_info;

static VPP_MEM_LIST *shm_list;
VPP_MEM vpp_dsi_info_shm_handle;
VPP_MEM vpp_resinfo_shm_handle;
VPP_MEM vpp_cmdinfo_shm_handle;

static phys_addr_t last_addr[MAX_NUM_PLANES];
static VBUF_INFO logo_vbuf_info[MAX_CRTC];
static SYNA_FL_CLEANUP_WORK syna_vpp_fl_clean_work[MAX_CRTC];
static void (*syna_vpp_post_process_cb)(struct drm_device *dev, int crtcID, int planeID);

void __weak syna_vpp_pop_fl_frame(int crtcID, int planeID)
{
	return;
}

void syna_vpp_wait_vsync(int Id)
{
	int ret = 0;
	long use_hw_vsync = syna_debugfs_get_hw_vsync_val();

	struct timespec64 start;
	struct timespec64 end;
	struct timespec64 dts;
	long long delta = 0;
	int frame_num = 0;

	if (use_hw_vsync) {
		if (use_hw_vsync == 2)
			ktime_get_real_ts64(&start);

		ret = wrap_MV_VPP_WaitVsync(Id);

		if (use_hw_vsync == 2) {
			ktime_get_real_ts64(&end);
			dts = timespec64_sub(end, start);
			delta = timespec64_to_ns(&dts) / 1000;
			frame_num++;
			if (frame_num % 100 == 0)
				DRM_ERROR("%s use time %lld us",
						__func__, delta);
		}

		if (ret < 0) {
			DRM_ERROR("%s %d wait_vpp_vsyn(%d) fail as: %d\n",
				   __func__, __LINE__, Id, ret);
		}
	} else {
		usleep_range(16000, 17000);
	}
}

static VOID convert_mtr_info(MTR_BUFF_DESC *mtr_desc, UINT32 *mtr_cfg)
{
	mtr_desc->m_MtrrCfgCfg_stride 	= mtr_cfg[0] & 0x0F;
	mtr_desc->m_MtrrCfgCfg_format 	= (mtr_cfg[0] >> 4) & 0x0F;
	mtr_desc->m_MtrrCfgCfg_mode   	= (mtr_cfg[0] >>9) & 0x1F;
	mtr_desc->m_MtrrCfgCfg_weave   	= (mtr_cfg[0] >>28) & 0x1;
	mtr_desc->m_MtrrCfgBase_addr  	= mtr_cfg[1];
	mtr_desc->m_MmuCfgPbm_shy_bw  	= mtr_cfg[2] & 0x7;
	mtr_desc->m_MmuCfgPbm_shy_pos 	= (mtr_cfg[2] >>3) & 0x1;
	mtr_desc->m_MmuCfgPbm_pm_enable = (mtr_cfg[2] >> 27) & 0x1;
	mtr_desc->m_MmuCfgPbm_shuffle_en= (mtr_cfg[2] >> 28) & 0x1;
	mtr_desc->m_MmuCfgPbm_bm_enable = (mtr_cfg[2] >> 29) & 0x1;
	/* TODO: Need to enabled m_MmuCfgPbm_weave , when during IOMMU bringup.
	 * Right now can not test below statement. */
	// mtr_desc->m_MmuCfgPbm_weave	 = (mtr_cfg[2] >> 30) & 0x1;
	mtr_desc->m_MmuCfgPbm_weave	= 0;
	mtr_desc->m_MmuCfgVm_enable = mtr_cfg[3] & 0x1;
	mtr_desc->m_MmuCfgVm_mode 	= (mtr_cfg[3] >> 2) & 0x7;
}

static void syna_vpp_convert_frame_info(VPP_VBUF *pVppBuf, UINT32 srcfmt, INT32 x,
				   INT32 y, INT32 width, INT32 height,
				   ARCH_PTR_TYPE m_pbuf_start, UINT32 m_pbuf_start_uv)
{
	pVppBuf->m_srcfmt = srcfmt;
	pVppBuf->m_buf_pbuf_start_UV = 0;
	pVppBuf->m_buf_stride_UV = 0;

	if (srcfmt == SRCFMT_YUV420SP) {
		pVppBuf->m_bytes_per_pixel = 2;
		pVppBuf->m_order = ORDER_UYVY;
		pVppBuf->m_buf_stride = width;
		pVppBuf->m_buf_stride_UV = width;
		pVppBuf->m_buf_pbuf_start_UV = m_pbuf_start_uv;
		pVppBuf->m_buf_stride_UV = width;
		pVppBuf->m_buf_size = (height * pVppBuf->m_buf_stride * 3) / 2;
		pVppBuf->m_bits_per_pixel = 8;
    } else if (srcfmt == SRCFMT_YUV422) {
		pVppBuf->m_bytes_per_pixel = 2;
		pVppBuf->m_order = ORDER_UYVY;
		pVppBuf->m_buf_stride = width * pVppBuf->m_bytes_per_pixel;
		pVppBuf->m_buf_size = height * pVppBuf->m_buf_stride;
		//Indicate the bitdepth of the frame, if 8bit, is 8, if 10bit, is 10
		pVppBuf->m_bits_per_pixel = 8;
	} else if (srcfmt == SRCFMT_RGB565) {
		pVppBuf->m_bytes_per_pixel = 2;
		pVppBuf->m_buf_stride = width * pVppBuf->m_bytes_per_pixel;
		pVppBuf->m_buf_size = height * pVppBuf->m_buf_stride;
		//Indicate the bitdepth of the frame, if 8bit, is 8, if 10bit, is 10
		pVppBuf->m_bits_per_pixel = pVppBuf->m_bytes_per_pixel * 8;
	}  else if (srcfmt == SRCFMT_RGB888) {
		pVppBuf->m_bytes_per_pixel = 3;
		pVppBuf->m_buf_stride = width * pVppBuf->m_bytes_per_pixel;
		pVppBuf->m_buf_size = height * pVppBuf->m_buf_stride;
		//Indicate the bitdepth of the frame, if 8bit, is 8, if 10bit, is 10
		pVppBuf->m_bits_per_pixel = pVppBuf->m_bytes_per_pixel * 8;
	} else {
		pVppBuf->m_bytes_per_pixel = 4;
		pVppBuf->m_buf_stride = width * pVppBuf->m_bytes_per_pixel;
		pVppBuf->m_buf_size = height * pVppBuf->m_buf_stride;
		//Indicate the bitdepth of the frame, if 8bit, is 8, if 10bit, is 10
		pVppBuf->m_bits_per_pixel = pVppBuf->m_bytes_per_pixel * 8;
	}

	pVppBuf->m_pbuf_start = (ARCH_PTR_TYPE) m_pbuf_start;
	pVppBuf->m_content_offset = 0;
	pVppBuf->m_content_width = width;
	pVppBuf->m_content_height = height;

	pVppBuf->m_active_left = x;
	pVppBuf->m_active_top = y;
	pVppBuf->m_active_width = width;
	pVppBuf->m_active_height = height;
	pVppBuf->m_disp_offset = 0;
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

	pVppBuf->m_flags = 1;

	pVppBuf->builtinFrame = 0;

	pVppBuf->m_hBD = (UINT32) 0;
	pVppBuf->m_colorprimaries = 0;
}

#ifndef CONFIG_SYNA_DRM_DISABLE_ROTATION
static VOID rotate_display_buffer(void *kernel_vir_dst_addr,
								void *kernel_vir_src_addr,
								int width, int height)
{
	switch (device_rotate) {
	default:
		break;
	case 180:
		{
			int y = 0;

			for (y = 0; y < height; y++) {
				char *dst =
					(char *)kernel_vir_dst_addr + 4 * width * y;
				char *src =
					(char *)kernel_vir_src_addr +
					4 * width * (height - y - 1);
				memcpy((void *)dst, (void *)src, 4 * width);
			}
			break;
		}
	case 90:
		{
			int tmp = width;

			width = height;
			height = tmp;

			break;
		}
	case 270:
		{
			int x = 0, y = 0;
			int tmp = width;

			unsigned long long *src_long =
				(unsigned long long *)kernel_vir_src_addr;
			unsigned long long *dst_long =
				(unsigned long long *)kernel_vir_dst_addr;
			unsigned long long a, b, c, d;

			for (y = 0; y < height; y += 2) {
				unsigned long long *start_y0 =
					src_long + y * width / 2;
				unsigned long long *start_y1 =
					src_long + (y + 1) * width / 2;
				for (x = 0; x < width; x += 2) {
					a = *(start_y0 + x / 2);
					b = *(start_y1 + x / 2);

					c = a >> 32LL;
					a = a & 0x00000000FFFFFFFFLL;

					d = b >> 32LL;
					b = b & 0x00000000FFFFFFFFLL;
					*(dst_long +
					  ((width - 1 - (x)) * height +
					   y) / 2) = (b << 32) | (a);
					*(dst_long +
					  ((width - 1 - (x + 1)) * height +
					   y) / 2) = (d << 32) | (c);
				}
			}

			width = height;
			height = tmp;

			break;
		}
	}
}
#endif

static void syna_vpp_init(struct drm_device *dev)
{
	int i, ret;
	int plane;
	struct syna_drm_private *dev_priv = dev->dev_private;
	VBUF_INFO *vbufinfo;
	VPP_MEM *shm_handle;

	DRM_DEBUG_DRIVER("%s:%d\n", __func__, __LINE__);

#ifndef CONFIG_SYNA_DRM_DISABLE_ROTATION
	device_rotate = DEFAULT_DEVICE_ROTATION;
#endif

	shm_list = dev_priv->mem_list;

	for (plane = 0; plane < MAX_NUM_PLANES; plane++) {
		for (i = 0; i < MAX_VBUF_INFO; i++) {
			DRM_DEBUG_DRIVER("Init %d\n", i);
			vbufinfo = &vpp_disp_desc_array[plane][i];
			shm_handle = &vpp_disp_info_shm_handle[plane][i];

			shm_handle->size = VPP_SHM_4K_ALIGN_ROUNDUP(sizeof(VPP_VBUF));
			ret = VPP_MEM_AllocateMemory(dev_priv->mem_list, VPP_MEM_TYPE_DMA,
					shm_handle, 0);
			if (ret != 0) {
				DRM_ERROR("%s %d  gem alloc failed!\n", __func__, __LINE__);
				return;
			}

			vbufinfo->hShm_vbuf = shm_handle;
			vbufinfo->pVppVbufInfo_virt = shm_handle->k_addr;
			vbufinfo->pVppVbufInfo_phy = (phys_addr_t)shm_handle->p_addr;

			DRM_DEBUG_DRIVER("Init vpp_disp_info_phys_addr[%d][%d]=%lx\n",
					 plane, i, (phys_addr_t)shm_handle->p_addr);
		}
	}

#ifndef CONFIG_SYNA_DRM_DISABLE_ROTATION
	for (plane = 0; plane < MAX_PLANE_NUM; plane++) {
		in_use_device_rotate[plane] = -1;
		for (i = 0; i < MAX_ROTATE_BUFFER; i++) {
			DRM_DEBUG_DRIVER("Init %d\n", i);
			vpp_rotate_buffer_shm_handle[plane][i].size = SYNA_WIDTH_MAX * SYNA_HEIGHT_MAX * 4;
			ret = VPP_MEM_AllocateMemory(dev_priv->mem_list, VPP_MEM_TYPE_DMA,
						&vpp_rotate_buffer_shm_handle[plane][i], 0);
			if (ret != 0) {
				DRM_ERROR("%s %d  gem alloc failed!\n", __func__, __LINE__);
				return;
			}
			rotate_buffer_kernel_addr[plane][i] =
				(phys_addr_t)vpp_rotate_buffer_shm_handle[plane][i].k_addr;
			rotate_buffer_phy_addr[plane][i] =
				(phys_addr_t)vpp_rotate_buffer_shm_handle[plane][i].p_addr;

			DRM_DEBUG_DRIVER
				("Init vpp_disp_info_phys_addr[%d][%d]=%lx\n",
				 plane, i, rotate_buffer_phy_addr[plane][i]);
		}
	}
#endif

	for (i = 0; i < VPP_BUILD_IN_FRAME_TYPE_MAX; i++) {
		uint32_t frame_type;
		const VPP_BUILD_IN_FRAME_INFO *bframe_info = &vpp_buildin_frame_info[i];

		DRM_DEBUG_DRIVER("Init Buildin frame type - %d, wxhxb - %dx%dx%d\n",
				i, bframe_info->frame_wid, bframe_info->frame_hgt,
				bframe_info->bpp);

		vpp_buildin_buffer_shm_handle[i].size = bframe_info->frame_wid *
				bframe_info->frame_hgt * bframe_info->bpp;

		ret = VPP_MEM_AllocateMemory(dev_priv->mem_list, VPP_MEM_TYPE_DMA,
				&vpp_buildin_buffer_shm_handle[i], 0);
		if (ret != 0) {
			DRM_ERROR("%s %d  gem alloc failed!\n", __func__, __LINE__);
			return;
		}

		buildin_buffer_kernel_addr[i] =
			(void*)vpp_buildin_buffer_shm_handle[i].k_addr;
		buildin_buffer_phy_addr[i] =
			(phys_addr_t)vpp_buildin_buffer_shm_handle[i].p_addr;

		frame_type = (bframe_info->format_type == SRCFMT_YUV422) ? 1 : 0;
		MV_VPP_make_frame_data(frame_type, buildin_buffer_kernel_addr[i],
			bframe_info->pattern_wid, bframe_info->pattern_hgt,
			bframe_info->frame_wid, bframe_info->frame_hgt,
			bframe_info->color1, bframe_info->color2);

		DRM_DEBUG_DRIVER
			("Init Buildin frame vpp_disp_info_phys_addr[%d]=%lx\n",
			 i,  buildin_buffer_phy_addr[i]);
	}
}

void syna_vpp_exit(struct drm_device *dev)
{
	int i;
	int plane;
	struct syna_drm_private *dev_priv = dev->dev_private;
	VPP_MEM_LIST *vpp_mem_list = dev_priv->mem_list;

	if (!init_vbuf_info)
		return;

	init_vbuf_info = 0;

#ifndef CONFIG_SYNA_DRM_DISABLE_ROTATION
	device_rotate = DEFAULT_DEVICE_ROTATION;
#endif

	for (plane = 0; plane < MAX_NUM_PLANES; plane++) {
		for (i = 0; i < MAX_VBUF_INFO; i++) {
			VPP_MEM_FreeMemory(shm_list, VPP_MEM_TYPE_DMA,
					&vpp_disp_info_shm_handle[plane][i]);
		}
	}

#ifndef CONFIG_SYNA_DRM_DISABLE_ROTATION
	for (plane = 0; plane < MAX_PLANE_NUM; plane++) {
		in_use_device_rotate[plane] = -1;
		for (i = 0; i < MAX_ROTATE_BUFFER; i++) {
				VPP_MEM_FreeMemory(shm_list, VPP_MEM_TYPE_DMA,
					&vpp_rotate_buffer_shm_handle[plane][i]);
		}
	}
#endif

	for (plane = 0; plane < MAX_PLANE_NUM; plane++) {
		VPP_MEM_FreeMemory(shm_list, VPP_MEM_TYPE_DMA,
			&vpp_buildin_buffer_shm_handle[plane]);
	}

	VPP_MEM_FreeMemory(vpp_mem_list, VPP_MEM_TYPE_DMA,
			&dev_priv->vpp_config_param.vpp_dsi_info_shm_handle);
	VPP_MEM_FreeMemory(vpp_mem_list, VPP_MEM_TYPE_DMA,
			&dev_priv->vpp_config_param.vpp_cmdinfo_shm_handle);
	VPP_MEM_FreeMemory(vpp_mem_list, VPP_MEM_TYPE_DMA,
			&dev_priv->vpp_config_param.vpp_resinfo_shm_handle);

	wrap_MV_VPP_DeInit();
}

bool syna_vpp_clocks_set(struct device *dev,
			 void __iomem *syna_reg, u32 clock_in_mhz,
			 u32 hdisplay, u32 vdisplay)
{
	DRM_DEBUG_DRIVER("%s:%d\n", __func__, __LINE__);

	return true;
}

void syna_vpp_set_updates_enabled(struct device *dev, void __iomem *syna_reg,
				  bool enable)
{
	DRM_DEBUG_DRIVER("Set updates: %s\n", enable ? "enable" : "disable");
	/* nothing to do here */
}

void syna_vpp_set_syncgen_enabled(struct device *dev, void __iomem *syna_reg,
				  bool enable)
{
	DRM_DEBUG_DRIVER("%s:%d\n", __func__, __LINE__);
	dev_info(dev, "Set syncgen: %s\n", enable ? "enable" : "disable");
}

void syna_vpp_set_powerdwn_enabled(struct device *dev, void __iomem *syna_reg,
				   bool enable)
{
	DRM_DEBUG_DRIVER("%s:%d\n", __func__, __LINE__);

	dev_info(dev, "Set powerdwn: %s\n", enable ? "enable" : "disable");
}

void syna_vpp_set_vblank_enabled(struct device *dev, void __iomem *syna_reg,
				 bool enable)
{
	DRM_DEBUG_DRIVER("%s:%d\n", __func__, __LINE__);

	dev_info(dev, "Set vblank: %s\n", enable ? "enable" : "disable");
}

bool syna_vpp_check_and_clear_vblank(struct device *dev,
					 void __iomem *syna_reg)
{
	return true;
}

void syna_vpp_set_plane_enabled(struct device *dev, void __iomem *syna_reg,
				u32 plane, bool enable)
{
	DRM_DEBUG_DRIVER("%s:%d\n", __func__, __LINE__);

	dev_info(dev, "Set plane %u: %s\n",
		 plane, enable ? "enable" : "disable");
}

void syna_vpp_reset_buffers(struct syna_gem_object *syna_obj)
{
	int i;
	DRM_DEBUG_DRIVER("Reset buffers\n");

	//No action, if buffer address is not valid
	if (!syna_obj->phyaddr)
		return;

	for (i = 0; i < MAX_NUM_PLANES; i++) {
		if (last_addr[i] == syna_obj->phyaddr) {
			if (en_builtin_frame_on_reset)
				syna_push_buildin_frame(i);
			last_addr[i] = (phys_addr_t)NULL;
			DRM_DEBUG_DRIVER("plane-%d, buffer released/reset-%lx\n", i,
								syna_obj->phyaddr);
			break;
		}
	}
}

void syna_vpp_reset_planes(struct device *dev, void __iomem *syna_reg)
{
	dev_info(dev, "Reset planes\n");

	syna_vpp_set_plane_enabled(dev, syna_reg, 0, false);
}

void syna_vpp_isr_process(struct drm_device *dev)
{
	struct syna_drm_private *dev_priv = dev->dev_private;
	SYNA_FBCON_START_WORK *fbcon_start_work = dev_priv->fbcon_start_work;

	if (!dev_priv->is_fbconsole_enabled && fbcon_start_work != NULL) {
		dev_priv->is_fbconsole_enabled = 1;
		schedule_work(&fbcon_start_work->drm_work);

		//Remove/disable isr process after fbcon is configured late
		dev_priv->syna_vpp_isr_process = NULL;
	}
}

static void syna_vpp_post_process_init_cb(struct drm_device *dev, int crtcID, int plane)
{
	struct syna_drm_private *dev_priv = dev->dev_private;
	SYNA_FBCON_START_WORK *fbcon_start_work_temp;
	int i, fl_freed_count;

	/* Handle fbconsole delayed start initialization */
	if (dev_priv->is_fb_delayed_start && !dev_priv->is_fbconsole_enabled && !dev_priv->fbcon_start_work) {
		fbcon_start_work_temp =
			kmalloc(sizeof(SYNA_FBCON_START_WORK), GFP_KERNEL);

		if (!fbcon_start_work_temp)
			DRM_ERROR("FBConsole Failed Alloc mem\n");
		else {
			dev_priv->syna_vpp_isr_process = syna_vpp_isr_process;
			fbcon_start_work_temp->dev = dev;
			INIT_WORK(&fbcon_start_work_temp->drm_work, syna_fbcon_start_work);
			dev_priv->fbcon_start_work = fbcon_start_work_temp;
		}
	}

	/* Free the fastlogo Frame buffer After ~6VBI for 60fps */
	if (!dev_priv->is_fl_frame_freed[crtcID])
		syna_vpp_fl_clear(dev, crtcID, plane);

	if (!dev_priv->syna_vpp_isr_process) {
		for (i = 0, fl_freed_count =0; i < MAX_CRTC; i++) {
			if (dev_priv->is_fl_frame_freed[i])
				fl_freed_count++;

			if (fl_freed_count == MAX_CRTC)
				syna_vpp_post_process_cb = NULL;
		}
	}
}

void syna_vpp_set_surface(struct drm_device *dev, int crtcID, void __iomem *syna_reg,
			  u32 plane, struct drm_framebuffer *fb,
			  u32 posx, u32 posy)
{
	struct syna_framebuffer *syna_fb = to_syna_framebuffer(fb);
	struct syna_gem_object *syna_obj, *syna_obj_uv;
	unsigned int pitch, pitch_uv;
	uint32_t fb_cpp, fb_cpp_uv;
	uint32_t width, height, stride, format;
	phys_addr_t disp_phyaddr, disp_phyaddr_uv;

	int VPP_Format = 0;
	int order = ORDER_BGRA;
	void *kernel_vir_src_addr = NULL;
#ifndef CONFIG_SYNA_DRM_DISABLE_ROTATION
	u32 rot_plane_ndx = 0;
	void *kernel_vir_dst_addr = NULL;
#endif
	int VPP_video_format = 0;
	int VPP_mmu_enabled = 0;
	struct bm_pt_param pt_param;
	struct dma_buf *dma_buf;
	VDEC_META_INFO kernel_vir_meta_addr = {0};
	struct berlin_meta *bm_meta_y = NULL, *bm_meta_uv = NULL;
	VPP_VBUF *curr_vpp_vbuf;
	VBUF_INFO *curr_disp_desc;
	int ret;
	syna_obj = (struct syna_gem_object *)(syna_fb->obj[0]);
	pitch = fb->pitches[0];
	fb_cpp = fb->format->cpp[0];

	if (fb->format->is_yuv && fb->format->num_planes > 1) {
		pitch_uv = fb->pitches[1];
		fb_cpp_uv = fb->format->cpp[1];
		syna_obj_uv = (struct syna_gem_object *)(syna_fb->obj[1]);
	} else {
		pitch_uv = 0;
		fb_cpp_uv = 0;
		syna_obj_uv = (struct syna_gem_object *)NULL;
	}
	width = fb->width;
	height = fb->height;
	stride = pitch;
	format = syna_drm_fb_format(fb);

	DRM_DEBUG_DRIVER
		("Set surface: size=%dx%d stride=%d format=%d address=0x%llx\n",
		 width, height, stride, format, (u64)syna_obj);

	if (!init_vbuf_info) {
		init_vbuf_info = 1;
		if (!VPP_Is_Recovery_Mode())
			syna_vpp_init(dev);
	}

	switch (format) {
	case DRM_FORMAT_ARGB8888:
		VPP_Format = SRCFMT_ARGB32;
		order = ORDER_BGRA;
		DRM_DEBUG_DRIVER("%s:%d Disp as DRM_FORMAT_ARGB8888\n",
				 __func__, __LINE__);
		break;
	case DRM_FORMAT_ABGR8888:
		VPP_Format = SRCFMT_ARGB32;
		order = ORDER_RGBA;
		DRM_DEBUG_DRIVER("%s:%d Disp as DRM_FORMAT_ABGR8888\n",
				 __func__, __LINE__);
		break;
	case DRM_FORMAT_XRGB8888:
		VPP_Format = SRCFMT_XRGB32;
		order = ORDER_BGRA;
		DRM_DEBUG_DRIVER("%s:%d Disp as DRM_FORMAT_XRGB8888\n",
				 __func__, __LINE__);
		break;
	case DRM_FORMAT_XBGR8888:
		VPP_Format = SRCFMT_XRGB32;
		order = ORDER_RGBA;
		DRM_DEBUG_DRIVER("%s:%d Disp as DRM_FORMAT_XBGR8888\n",
				 __func__, __LINE__);
		break;
	case DRM_FORMAT_RGB888:
		VPP_Format = SRCFMT_RGB888;
		order = ORDER_BGRA;
		DRM_DEBUG_DRIVER("%s:%d Disp as DRM_FORMAT_RGB888\n",
				 __func__, __LINE__);
		break;
	case DRM_FORMAT_BGR888:
		VPP_Format = SRCFMT_RGB888;
		order = ORDER_RGBA;
		DRM_DEBUG_DRIVER("%s:%d Disp as DRM_FORMAT_BGR888\n",
				 __func__, __LINE__);
		break;
	case DRM_FORMAT_RGB565:
		VPP_Format = SRCFMT_RGB565;
		order = ORDER_BGRA;
		DRM_DEBUG_DRIVER("%s:%d Disp as DRM_FORMAT_RGB565\n",
				 __func__, __LINE__);
		break;
	case DRM_FORMAT_BGR565:
		VPP_Format = SRCFMT_RGB565;
		order = ORDER_RGBA;
		DRM_DEBUG_DRIVER("%s:%d Disp as DRM_FORMAT_BGR565\n",
				 __func__, __LINE__);
		break;
	case DRM_FORMAT_NV12:
	case DRM_FORMAT_NV21:
		VPP_video_format = 1;
		VPP_Format = SRCFMT_YUV420SP;
		if (format == DRM_FORMAT_NV12) {
			order = ORDER_UYVY;
			DRM_DEBUG_DRIVER("%s:%d Disp as DRM_FORMAT_NV12\n",
				 __func__, __LINE__);
		} else {
			order = ORDER_VYUY;
			DRM_DEBUG_DRIVER("%s:%d Disp as DRM_FORMAT_NV21\n",
				 __func__, __LINE__);
		}
		break;
	case DRM_FORMAT_UYVY:
	case DRM_FORMAT_VYUY:
	case DRM_FORMAT_YUYV:
	case DRM_FORMAT_YVYU:
		VPP_video_format = 1;
		VPP_Format = SRCFMT_YUV422;
		order = ORDER_UYVY;
		DRM_DEBUG_DRIVER("%s:%d Disp as DRM_FORMAT_UYVY\n",
				 __func__, __LINE__);
		if (format == DRM_FORMAT_UYVY) {
			order = ORDER_UYVY;
			DRM_DEBUG_DRIVER("%s:%d Disp as DRM_FORMAT_UYVY\n",
				 __func__, __LINE__);
		} else if (format == DRM_FORMAT_VYUY) {
			order = ORDER_VYUY;
			DRM_DEBUG_DRIVER("%s:%d Disp as DRM_FORMAT_VYUY\n",
				 __func__, __LINE__);
		} else if (format == DRM_FORMAT_YUYV) {
			order = ORDER_YUYV;
			DRM_DEBUG_DRIVER("%s:%d Disp as DRM_FORMAT_YUYV\n",
				 __func__, __LINE__);
		} else if (format == DRM_FORMAT_YVYU) {
			order = ORDER_YVYU;
			DRM_DEBUG_DRIVER("%s:%d Disp as DRM_FORMAT_YVYU\n",
				 __func__, __LINE__);
		}
		break;
	default:
		DRM_ERROR("%s:%d Unknown request format=%d\n",
				 __func__, __LINE__, format);
		BUG_ON(1);
		break;
	}

	DRM_DEBUG_DRIVER("Display frame: planeID=%d x=%x y=%d w=%d, h=%d, phyaddr=%lx\n",
		 plane, posx, posy, width, height, syna_obj->phyaddr);

	if (plane >= MAX_NUM_PLANES) {
		DRM_ERROR("Push frame nn wrong plane\n");
		return;
	}

	if (last_addr[plane] != syna_obj->phyaddr) {
		last_addr[plane] = syna_obj->phyaddr;
	} else {
		DRM_DEBUG_DRIVER("Push the same frame to plane-%d\n", plane);
	}

	/*Have rotate, get a convert target buffer */
	disp_phyaddr = syna_obj->phyaddr;


	dma_buf = syna_obj->dma_buf ? syna_obj->dma_buf : syna_obj->shm_handle.handle;
	//Get the bm_pt_param & berlin_meta details from dma_buf
	ret = syna_vpp_get_bm_details(dma_buf, &pt_param, &bm_meta_y);
	if (!ret) {
		disp_phyaddr = (dma_addr_t) pt_param.phy_addr;
		VPP_mmu_enabled = 1;
		if (bm_meta_y)
			memcpy(&kernel_vir_meta_addr.uiLumaMtrrCfg[0],
				&bm_meta_y->mtrr.mtrcfg[0],
				sizeof(kernel_vir_meta_addr.uiLumaMtrrCfg));
	}

	if (syna_obj_uv)
		disp_phyaddr_uv = syna_obj_uv->phyaddr;
	else
		disp_phyaddr_uv = 0;
	/*
	 * User space specifies 'x' and 'y' and this is used to tell the display
	 * to scan out from part way through a buffer.
	 */
	disp_phyaddr += ((posy * pitch) + (posx * fb_cpp));
	disp_phyaddr += fb->offsets[0];
	if (syna_obj_uv) {
		dma_buf = syna_obj_uv->dma_buf ?
				syna_obj_uv->dma_buf : syna_obj_uv->shm_handle.handle;
		//Get the bm_pt_param & berlin_meta details from dma_buf
		ret = syna_vpp_get_bm_details(dma_buf, &pt_param, &bm_meta_uv);
		if (!ret) {
			disp_phyaddr_uv = (dma_addr_t) pt_param.phy_addr;
			if (bm_meta_uv)
				memcpy(&kernel_vir_meta_addr.uiChromaMtrrCfg[0],
					&bm_meta_uv->mtrr.mtrcfg[0],
					sizeof(kernel_vir_meta_addr.uiChromaMtrrCfg));
		}

		disp_phyaddr_uv += ((posy * pitch_uv) + (posx * fb_cpp_uv));
		disp_phyaddr_uv += fb->offsets[1];
	}

	kernel_vir_src_addr = syna_obj->kernel_vir_addr;

#ifndef CONFIG_SYNA_DRM_DISABLE_ROTATION
	VPP_GET_PLANE_ROTATE_INDX(rot_plane_ndx, plane);

	if (device_rotate != 0 && rot_plane_ndx != 0) {
		u32 rot_frame_ndx;
		last_rot_frame[rot_plane_ndx]++;
		last_rot_frame[rot_plane_ndx] = last_rot_frame[rot_plane_ndx] % MAX_ROTATE_BUFFER;
		rot_frame_ndx = last_rot_frame[rot_plane_ndx];
		//Index starts from 0, So decrement by 1
		rot_plane_ndx--;
		disp_phyaddr = rotate_buffer_phy_addr[rot_plane_ndx][rot_frame_ndx];
		kernel_vir_dst_addr =
			(void *)rotate_buffer_kernel_addr[rot_plane_ndx][rot_frame_ndx];
	}

	if (device_rotate != 0 && rot_plane_ndx != 0)
		rotate_display_buffer(kernel_vir_dst_addr, kernel_vir_src_addr, width, height);
#endif

	curr_disp_desc = &vpp_disp_desc_array[plane][vbuf_info_num[plane]];
	curr_vpp_vbuf = curr_disp_desc->pVppVbufInfo_virt;

#ifdef SYNA_VPP_FORCE_PIP_FORMAT_ORDER
	if (plane == PLANE_PIP) {
		syna_vpp_convert_frame_info(curr_vpp_vbuf, SRCFMT_ARGB32,
				   0, 0, width, height, (ARCH_PTR_TYPE)disp_phyaddr, disp_phyaddr_uv);
		curr_vpp_vbuf->m_order = ORDER_ARGB;
		DRM_DEBUG_DRIVER
			("Hack for PIP: Always use SRCFMT_ARGB32+ORDER_ARGB\n");
	} else
#endif
	{
		syna_vpp_convert_frame_info(curr_vpp_vbuf, VPP_Format, 0, 0,
				   width, height, (ARCH_PTR_TYPE)disp_phyaddr, disp_phyaddr_uv);
		curr_vpp_vbuf->m_order = order;
		if (VPP_mmu_enabled) {
			if (bm_meta_y || bm_meta_uv) {
				//bit 0 - MTR on/off, bit 1 - MMU on/off
				curr_vpp_vbuf->m_is_compressed = bm_meta_y->mtrr.mtr_flags;
				curr_vpp_vbuf->m_buf_stride =
					VPP_GET_MTR_STRIDE(kernel_vir_meta_addr.uiLumaMtrrCfg[2]);
				curr_vpp_vbuf->m_buf_stride_UV =
					VPP_GET_MTR_STRIDE(kernel_vir_meta_addr.uiChromaMtrrCfg[2]);

				convert_mtr_info(&curr_vpp_vbuf->m_mtr_buf_desc_Y,
							&kernel_vir_meta_addr.uiLumaMtrrCfg[0]);
				convert_mtr_info(&curr_vpp_vbuf->m_mtr_buf_desc_UV,
							&kernel_vir_meta_addr.uiChromaMtrrCfg[0]);
			} else {
				DRM_ERROR("meta data not found for MMU frame, plane-%d\n", plane);
			}
		}
	}

#ifndef CONFIG_SYNA_DRM_DISABLE_ROTATION
	if (in_use_device_rotate[plane] != device_rotate) {
		DRM_DEBUG_DRIVER
			("[DRM] device rotate is change!! pre:%d update:%ld\n",
			 in_use_device_rotate[plane], device_rotate);
		in_use_device_rotate[plane] = device_rotate;
	}
#endif

	MV_VPP_DisplayFrame(plane, VPP_video_format, (void *)curr_disp_desc);

	vbuf_info_num[plane] = (vbuf_info_num[plane] + 1) % MAX_VBUF_INFO;

	if (syna_vpp_post_process_cb)
		syna_vpp_post_process_cb(dev, crtcID, plane);
}

void syna_vpp_push_buildin_null_frame(u32 plane)
{
	VPP_VBUF *curr_vpp_vbuf;
	VBUF_INFO *curr_disp_desc;
	//Hardcode the frame type to GFX_NULL
	VPP_BUILD_IN_FRAME_TYPE ftype = VPP_BUILD_IN_FRAME_TYPE_GFX_NULL;
	const VPP_BUILD_IN_FRAME_INFO *bframe_info = &vpp_buildin_frame_info[ftype];
	int VPP_video_format =
		(bframe_info->format_type == SRCFMT_YUV422 ? 1 : 0);
	//Ensure null frame as either wid/hgt = 0
	u32 width = bframe_info->frame_wid;
	u32 height = 0;
	phys_addr_t disp_phyaddr =
		(phys_addr_t)vpp_buildin_buffer_shm_handle[ftype].p_addr;

	curr_disp_desc = &vpp_disp_desc_array[plane][vbuf_info_num[plane]];
	curr_vpp_vbuf = curr_disp_desc->pVppVbufInfo_virt;
	syna_vpp_convert_frame_info(curr_vpp_vbuf,
				   bframe_info->format_type, 0, 0,
				   width, height, (ARCH_PTR_TYPE)disp_phyaddr, (phys_addr_t)0);

	MV_VPP_DisplayFrame(plane, VPP_video_format, (void *)curr_disp_desc);

	vbuf_info_num[plane] = (vbuf_info_num[plane] + 1) % MAX_VBUF_INFO;
}

void syna_vpp_push_buildin_frame(u32 plane)
{
	VPP_VBUF *curr_vpp_vbuf;
	VBUF_INFO *curr_disp_desc;
	VPP_BUILD_IN_FRAME_TYPE ftype = syna_get_buidin_frame_type(plane);
	const VPP_BUILD_IN_FRAME_INFO *bframe_info = &vpp_buildin_frame_info[ftype];
	int VPP_video_format =
		(bframe_info->format_type == SRCFMT_YUV422 ? 1 : 0);
	u32 width = bframe_info->frame_wid;
	u32 height = bframe_info->frame_hgt;
	phys_addr_t disp_phyaddr =
		(phys_addr_t)vpp_buildin_buffer_shm_handle[ftype].p_addr;

	curr_disp_desc = &vpp_disp_desc_array[plane][vbuf_info_num[plane]];
	curr_vpp_vbuf = curr_disp_desc->pVppVbufInfo_virt;
	syna_vpp_convert_frame_info(curr_vpp_vbuf,
				   bframe_info->format_type, 0, 0,
				   width, height, (ARCH_PTR_TYPE)disp_phyaddr, (phys_addr_t)0);

	MV_VPP_DisplayFrame(plane, VPP_video_format, (void *)curr_disp_desc);
	vbuf_info_num[plane] = (vbuf_info_num[plane] + 1) % MAX_VBUF_INFO;
}

int syna_vpp_dev_init(struct drm_device *dev)
{
	int ret = 0;
	struct syna_drm_private *dev_priv = dev->dev_private;
	VPP_MEM_LIST *vpp_mem_list = dev_priv->mem_list;

	ret = syna_read_config(dev_priv);
	if (!ret) {
		if (VPP_Is_Recovery_Mode()) {
			syna_vpp_init(dev);
			ret = MV_VPP_Init(vpp_mem_list, dev_priv->vpp_config_param);
			if (!ret)
				syna_vpp_dev_init_priv(dev);
		}
	}
	syna_vpp_post_process_cb = syna_vpp_post_process_init_cb;

	return ret;
}

void syna_vpp_mode_set(struct device *dev, void __iomem *syna_reg,
			   u32 h_display, u32 v_display,
			   u32 hbps, u32 ht, u32 has,
			   u32 hlbs, u32 hfps, u32 hrbs,
			   u32 vbps, u32 vt, u32 vas,
			   u32 vtbs, u32 vfps, u32 vbbs, bool nhsync, bool nvsync)
{
	DRM_DEBUG_DRIVER("%s:%d\n", __func__, __LINE__);

	dev_info(dev, "Set mode: %dx%d\n", h_display, v_display);
	dev_info(dev, " ht: %d hbps %d has %d hlbs %d hfps %d hrbs %d\n",
		 ht, hbps, has, hlbs, hfps, hrbs);
	dev_info(dev, " vt: %d vbps %d vas %d vtbs %d vfps %d vbbs %d\n",
		 vt, vbps, vas, vtbs, vfps, vbbs);
}

void syna_vpp_load_config(int devID, void *pconfig)
{
	wrap_MV_VPP_LoadConfigTable(VOUT_DEVICE, devID, pconfig);
}

static void syna_vpp_free_fl_frame(struct work_struct *work)
{
	struct delayed_work *dwork = container_of(work, struct delayed_work, work);
	SYNA_FL_CLEANUP_WORK *fswork = container_of(dwork, SYNA_FL_CLEANUP_WORK, delay_work);
	struct syna_drm_private *dev_priv = fswork->dev->dev_private;
	int crtcID = fswork->crtcId;
	VPP_MEM_LIST *vpp_mem_list = dev_priv->mem_list;

	if (dev_priv->vpp_fl_descr_handle[crtcID]) {
		VPP_MEM_FreeMemory(vpp_mem_list, VPP_MEM_TYPE_DMA,
			dev_priv->vpp_fl_descr_handle[crtcID]);
		dev_priv->vpp_fl_descr_handle[crtcID] = NULL;
	}

	if (dev_priv->vpp_fastlogo_buf_handle[crtcID]) {
		VPP_MEM_FreeMemory(vpp_mem_list, VPP_MEM_TYPE_DMA,
			dev_priv->vpp_fastlogo_buf_handle[crtcID]);
		dev_priv->vpp_fastlogo_buf_handle[crtcID] = NULL;
	}
}

void __weak syna_push_builtin_frames(void)
{
	return;
}
void __weak syna_push_default_buildin_frames_for_crtc(int crtc_id)
{
	return;
}

void syna_vpp_push_fastlogo_frame(struct drm_device *dev)
{
	struct syna_drm_private *dev_priv = dev->dev_private;
	VPP_MEM_LIST *vpp_mem_list = dev_priv->mem_list;
	int ret, i;
	fastlogo_info_t fl_info;
	VPP_WIN vpp_res_info;
	int crtcs;
	int virt_crtc_ndx;

	if (!VPP_Is_Recovery_Mode() || !dev_priv->vpp_config_param.logo_enable) {
		for (i = 0; i < MAX_CRTC; i++)
			dev_priv->is_fl_frame_freed[i] = 1;

		syna_push_builtin_frames();
		return;
	}

	crtcs = GET_MAX_CRTC_FOR_MODE(dev_priv->vpp_config_param.display_mode);

	for (i = 0; i < crtcs; i++) {
		if (!MV_VPP_IsValidCrtcIndex(i)) {
			dev_priv->is_fl_frame_freed[i] = 1;
			continue;
		}

		virt_crtc_ndx = MV_VPP_GetVirtCrtcIndex(i);

		if (!syna_vpp_get_disp_info(dev, i, &fl_info)) {
			dev_priv->vpp_fastlogo_buf_handle[i] = devm_kmalloc(dev->dev, sizeof(VPP_MEM), GFP_KERNEL);
			dev_priv->vpp_fl_descr_handle[i] = devm_kmalloc(dev->dev, sizeof(VPP_MEM), GFP_KERNEL);

			if (dev_priv->vpp_fastlogo_buf_handle[i] && dev_priv->vpp_fl_descr_handle[i]) {
				/* Allocate memory and load U-Boot logo */
				ret = syna_load_uboot_logo(
					vpp_mem_list,
					dev_priv->vpp_fastlogo_buf_handle[i],
					virt_crtc_ndx,
					&vpp_res_info);

				if (!ret) {
					memset(dev_priv->vpp_fl_descr_handle[i], 0, sizeof(VPP_MEM));
					dev_priv->vpp_fl_descr_handle[i]->size = VPP_SHM_4K_ALIGN_ROUNDUP(sizeof(VPP_VBUF));
					ret = VPP_MEM_AllocateMemory(dev_priv->mem_list, VPP_MEM_TYPE_DMA,
							dev_priv->vpp_fl_descr_handle[i], 0);
					if (ret) {
						pr_err("Failed to Alloc mem\n");
						goto err_memory_cleanup;
					}

					dev_priv->vpp_fl_descr_handle[i]->teeShm = NULL;
					logo_vbuf_info[i].hShm_vbuf = (void *) dev_priv->vpp_fl_descr_handle[i];
					logo_vbuf_info[i].pVppVbufInfo_virt = dev_priv->vpp_fl_descr_handle[i]->k_addr;
					logo_vbuf_info[i].pVppVbufInfo_phy = dev_priv->vpp_fl_descr_handle[i]->p_addr;
					syna_vpp_convert_frame_info(logo_vbuf_info[i].pVppVbufInfo_virt,
								LOGO_SRC_FMT, 0, 0,
								vpp_res_info.width,
								vpp_res_info.height,
								(ARCH_PTR_TYPE) dev_priv->vpp_fastlogo_buf_handle[i]->p_addr,
								(phys_addr_t)0);

					MV_VPP_DisplayFrame(i, IS_LOGO_VIDEO_FMT, &logo_vbuf_info[i]);
					syna_vpp_fl_clean_work[i].dev = dev;
					INIT_DELAYED_WORK(&syna_vpp_fl_clean_work[i].delay_work, syna_vpp_free_fl_frame);
				} else {
					syna_push_default_buildin_frames_for_crtc(i);
					VPP_MEM_FreeMemory(vpp_mem_list, VPP_MEM_TYPE_DMA,
						dev_priv->vpp_fastlogo_buf_handle[i]);
					dev_priv->vpp_fastlogo_buf_handle[i] = NULL;
					dev_priv->is_fl_frame_freed[i] = 1;
				}
			}
		} else {
			/* If frame not pushed, force assume logo display is
			 * complete and freed to move to next stage/step
			 */
			dev_priv->is_fl_frame_freed[i] = 1;
		}
	}

	return;

err_memory_cleanup:
	for (i = 0; i  < MAX_CRTC; i++)
		syna_vpp_fl_clear(dev, i, i);
}

VPP_MEM_LIST *syna_vpp_get_shm_list(void)
{
	return shm_list;
}

void syna_vpp_fl_clear(struct drm_device *dev, int crtcID, int planeID)
{
	struct syna_drm_private *dev_priv = dev->dev_private;

	/* In Multi plane system, if first frame received on non-logo plane,
	 * Then pop/recycle the FL frame from logo plane
	 */
	syna_vpp_pop_fl_frame(crtcID, planeID);

	dev_priv->is_fl_frame_freed[crtcID] = 1;
	syna_vpp_fl_clean_work[crtcID].crtcId = crtcID;

	schedule_delayed_work(&syna_vpp_fl_clean_work[crtcID].delay_work,
		msecs_to_jiffies(VPP_FRAME_FREE_DELAY_MS));
}

static void syna_rcu_fbcon_cleanup(struct rcu_head *rcu)
{
	SYNA_FBCON_START_WORK *fbcon_start_work_temp =
		container_of(rcu, SYNA_FBCON_START_WORK, rcu);
	struct syna_drm_private *dev_priv = fbcon_start_work_temp->dev->dev_private;

	DRM_DEBUG_DRIVER("RCU callback executed: freeing memory\n");
	kfree(dev_priv->fbcon_start_work);
}

/* Work Queue to avoid the Timeout warning or Deadlock
 * while Fbconsole is enabled
  */
void syna_fbcon_start_work(struct work_struct *work)
{
	SYNA_FBCON_START_WORK *fbcon_start_work_temp =
		container_of(work, SYNA_FBCON_START_WORK, drm_work);

	SYNA_DRM_FBDEV_SETUP(fbcon_start_work_temp->dev, 32);

	// Schedule RCU-safe cleanup
	call_rcu(&fbcon_start_work_temp->rcu, syna_rcu_fbcon_cleanup);
}

int syna_vpp_update_gamma(struct drm_device *dev, int Id, const void *data,
			  unsigned int length)
{
	return wrap_MV_VPPOBJ_UpdateGamma(Id, data, length);
}

int syna_vpp_update_brightness(int Id, int channel, uint64_t val)
{
	return wrap_MV_VPPOBJ_UpdateBrightness(Id, channel, val);
}

int syna_vpp_get_brightness(int Id, int channel, uint64_t *val)
{
	return wrap_MV_VPPOBJ_GetBrightness(Id, channel, val);
}
