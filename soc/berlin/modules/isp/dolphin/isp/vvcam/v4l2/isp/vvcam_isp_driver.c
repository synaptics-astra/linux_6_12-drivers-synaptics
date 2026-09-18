// SPDX-License-Identifier: MIT OR GPL-2.0-or-later
/****************************************************************************
*
*    The MIT License (MIT)
*
*    Copyright (c) 2014 - 2024 Vivante Corporation
*
*    Permission is hereby granted, free of charge, to any person obtaining a
*    copy of this software and associated documentation files (the "Software"),
*    to deal in the Software without restriction, including without limitation
*    the rights to use, copy, modify, merge, publish, distribute, sublicense,
*    and/or sell copies of the Software, and to permit persons to whom the
*    Software is furnished to do so, subject to the following conditions:
*
*    The above copyright notice and this permission notice shall be included in
*    all copies or substantial portions of the Software.
*
*    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
*    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
*    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
*    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
*    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
*    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
*    DEALINGS IN THE SOFTWARE.
*
*****************************************************************************
*
*    The GPL License (GPL)
*
*    Copyright (C) 2014 - 2024 Vivante Corporation
*
*    This program is free software; you can redistribute it and/or
*    modify it under the terms of the GNU General Public License
*    as published by the Free Software Foundation; either version 2
*    of the License, or (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program; if not, write to the Free Software Foundation,
*    Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
*
*****************************************************************************
*
*    Note: This software is released under dual MIT and GPL licenses. A
*    recipient may use this file under the terms of either the MIT license or
*    GPL License. If you wish to use only one license not the other, you can
*    indicate your decision by deleting one of the above license notices in your
*    version of this file.
*
*****************************************************************************/


#include <linux/module.h>
#include <linux/version.h>
#include <linux/platform_device.h>
#include <linux/of_reserved_mem.h>
#include <linux/of_graph.h>
#include <linux/vmalloc.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/pm_runtime.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mc.h>
#include <media/videobuf2-dma-contig.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-mediabus.h>
#include <media/v4l2-ctrls.h>

#include "vvcam_v4l2_common.h"
#include "vvcam_isp_driver.h"
#include "vvcam_isp_event.h"
#include "vvcam_isp_ctrl.h"
#include "vvcam_isp_procfs.h"
#ifdef VVCAM_SUBDEV_PLATFORM_REGISTER
#include "vvcam_isp_platform.h"
#endif
#ifdef DOLPHIN
#include "mtr_isp_wrap.h"
#include "ispSS_shm.h"
#include "isp_bcm.h"
#include "ispbe_api.h"
#include "isp_dma_heap.h"
#include "ispSS_reg.h"
#include <linux/delay.h>
#include "mcm_event.h"
#endif

#define VVCAM_ISP_DEFAULT_SENSOR        "imx258"
#define VVCAM_ISP_DEFAULT_SENSOR_MODE   0
#define VVCAM_ISP_DEFAULT_SENSOR_XML    "IMX258.xml"
#define VVCAM_ISP_DEFAULT_SENSOR_MANU_JSON    "ISP_Manual.json"
#define VVCAM_ISP_DEFAULT_SENSOR_AUTO_JSON    "ISP_Auto.json"
#ifdef DOLPHIN
#define VVCAM_ISP_DEFAULT_I2C_BUS_ID   3
#define VVCAM_ISP_DEFAULT_MIPI_ID   0
#define RAW_4K_MAX_SIZE (3840*2160*2) // 4k 16bit bayer
#define ISP_SINK_PAD(pad_num) (((pad_num) / VVCAM_ISP_PORT_PAD_NR) * VVCAM_ISP_PORT_PAD_NR)
#endif

struct vvcam_isp_mbus_fmt vvcam_isp_mp_fmts[] = {
    {
        .code = MEDIA_BUS_FMT_YUYV8_2X8,   /*NV16*/
    },
    {
        .code = MEDIA_BUS_FMT_YUYV8_1_5X8, /*NV12*/
    },
    {
        .code = MEDIA_BUS_FMT_YUYV8_1X16,  /*YUYV*/
    },
    {
        .code = MEDIA_BUS_FMT_BGR888_1X24,  /*BGR24*/
    },
    {
        .code = MEDIA_BUS_FMT_RGB888_1X24,  /*RGB24*/
    },
#ifndef DOLPHIN
    {
        .code = MEDIA_BUS_FMT_YUYV10_2X10, /*P010*/
    },
#endif
};

struct vvcam_isp_mbus_fmt vvcam_isp_sp_fmts[] = {
    {
        .code = MEDIA_BUS_FMT_YUYV8_2X8,   /*NV16*/
    },
    {
        .code = MEDIA_BUS_FMT_YUYV8_1_5X8, /*NV12*/
    },
    {
        .code = MEDIA_BUS_FMT_YUYV8_1X16,  /*YUYV*/
    },
    {
        .code = MEDIA_BUS_FMT_BGR888_1X24,  /*BGR24*/
    },
    {
        .code = MEDIA_BUS_FMT_RGB888_1X24,  /*RGB24*/
    },
#ifndef DOLPHIN
    {
        .code = MEDIA_BUS_FMT_YUYV10_2X10, /*P010*/
    },
#endif
};

struct vvcam_isp_mbus_fmt vvcam_isp_raw_fmts[] = {
    {
        .code = MEDIA_BUS_FMT_SRGGB10_1X10,  /*SRGGB10*/
    },
    {
        .code = MEDIA_BUS_FMT_SGBRG10_1X10,  /*SGBRG10*/
    },
    {
        .code = MEDIA_BUS_FMT_SBGGR10_1X10,  /*SBGGR10*/
    },
    {
        .code = MEDIA_BUS_FMT_SGRBG10_1X10,  /*SGRBG10*/
    },
    {
        .code = MEDIA_BUS_FMT_SRGGB12_1X12,  /*SRGGB12*/
    },
    {
        .code = MEDIA_BUS_FMT_SGBRG12_1X12,  /*SGBRG12*/
    },
    {
        .code = MEDIA_BUS_FMT_SBGGR12_1X12,  /*SBGGR12*/
    },
    {
        .code = MEDIA_BUS_FMT_SGRBG12_1X12,  /*SGRBG12*/
    },
#ifdef DOLPHIN
    {
        .code = MEDIA_BUS_FMT_RGB888_1X24,   /*RGB24*/
    },
    {
        .code = MEDIA_BUS_FMT_YUV8_1X24,     /*YUV24*/
    },
#endif
};

#ifdef DOLPHIN
uint32_t vvcam_isp_get_mtr_path(uint32_t pad_index)
{
    uint32_t path = ISPSS_MTR_PATH_MP0_WR;
    uint8_t channel = pad_index % VVCAM_ISP_PORT_PAD_NR;
    uint8_t port = pad_index / VVCAM_ISP_PORT_PAD_NR;

    switch (channel) {
    case VVCAM_ISP_PORT_PAD_SOURCE_MP:
        path = ISPSS_MTR_PATH_MP0_WR + port;
        break;
    case VVCAM_ISP_PORT_PAD_SOURCE_SP1:
        path = ISPSS_MTR_PATH_INVALID;
        break;
    case VVCAM_ISP_PORT_PAD_SOURCE_SP2:
        path = ISPSS_MTR_PATH_SP2_WR0 + port;
        break;
    default:
        path = ISPSS_MTR_PATH_INVALID;
        break;
    }

    return path;
}
EXPORT_SYMBOL(vvcam_isp_get_mtr_path);

static void get_pad_range(int port_id, uint8_t *pad_start, uint8_t *pad_end)
{
    if (port_id == -1) {
        *pad_start = VVCAM_ISP_PORT_PAD_SINK;
        *pad_end = VVCAM_ISP_PORT_NR * VVCAM_ISP_PORT_PAD_NR;
    } else {
        *pad_start = VVCAM_ISP_PORT_PAD_SOURCE_MP + port_id * VVCAM_ISP_PORT_PAD_NR;
        *pad_end = VVCAM_ISP_PORT_PAD_NR + port_id * VVCAM_ISP_PORT_PAD_NR;
    }
}

static int process_3dnr_buffers(struct vvcam_isp_dev *isp_dev, void *bcm_buf, int port_id)
{
    int ret = 0;
    uint32_t mtr_write_path = ISPSS_MTR_PATH_TDNR0_WR + port_id;
    uint32_t mtr_read_path = ISPSS_MTR_PATH_TDNR0_RD + port_id;

    ret = ISPSS_MTR_ConfigureMtr(NULL, // TDNR path doesn't need format to be passed
                                    isp_dev->dnr3_buf[port_id].phy_address,
                                    (unsigned long)NULL,
                                    mtr_write_path,
                                    bcm_buf);
    if (ret == -1) {
        pr_err("%s: 3DNR MTR Write configuration failed\n", __func__);
        goto out; // Do not configure read path if write path fails
    }

    ret = ISPSS_MTR_ConfigureMtr(NULL, // TDNR path doesn't need format to be passed
                                    isp_dev->dnr3_buf[port_id].phy_address,
                                    (unsigned long)NULL,
                                    mtr_read_path,
                                    bcm_buf);
    if (ret == -1) {
        pr_err("%s: 3DNR MTR Read configuration failed\n", __func__);
        goto out;
    }

out:
    return ret;
}

static int process_pad_buffers(struct vvcam_isp_pad_data *cur_pad,
                        uint8_t pad, void *bcm_buf)
{
    struct vvcam_vb2_buffer *pos, *next;
    int ret = 0;
    bool found = false;

    if (!cur_pad->mmu_enabled)
        return 0;

    mutex_lock(&cur_pad->q_lock);
    list_for_each_entry_safe(pos, next, &cur_pad->queue, list) {
        if (!pos->is_pushed_queue) {
            ret = ISPSS_MTR_ConfigureMtr(&cur_pad->v4l2_format,
                                         pos->planes[0].dma_addr,
                                         pos->planes[1].dma_addr,
                                         vvcam_isp_get_mtr_path(pad),
                                         bcm_buf);
            if (ret == -1)
                pr_err("%s: MTR configuration failed for pad %d\n", __func__, pad);
            else {
                pos->is_pushed_queue = 1;
                found = true;
            }
            break;
        }
    }
    mutex_unlock(&cur_pad->q_lock);

    return found ? ret : -1;
}

static uint8_t get_streaming_port(struct vvcam_isp_dev *isp_dev, uint8_t pad_start, uint8_t pad_end)
{
    uint8_t pad;
    uint8_t port_id;

    for (pad = pad_start; pad < pad_end; pad++) {
        if (isp_dev->pad_data[pad].stream)
            break;
    }
    if (pad < VVCAM_ISP_PORT_PAD_NR)
        port_id = 0;
    else
        port_id = 1;

    return port_id;
}

static int push_buf(struct vvcam_isp_dev *isp_dev, int port_id)
{
    void *bcm_buf;
    uint8_t pad_start, pad_end, pad;
    int ret = -1;
    bool is_bcm_immediate = (isp_dev->mcm_mode == ISP_MCM_MODE_ENABLED) ? true : false;
    uint8_t cur_stream_port;

    /* Check the BCM Queue to be used whether its free. Same BCM
     * Queue is used for all paths, hence path dummy path here.
     * Only for non MCM interrupt based Queue is used
     * hence the check is done for non MCM mode.
     * For MCM mode it will be Q12 and api will ensure
     * Q12 is free before pushing
     */
    if (is_isp_bcm_buffer_full(-1) &&
            isp_dev->mcm_mode == ISP_MCM_MODE_DISABLED) {
        pr_err("%s: BCM buffer full\n", __func__);
        return 0;
    }

    bcm_buf = isp_bcm_get_next_bcmbuf();
    if (!bcm_buf) {
        pr_err("%s: Failed to allocate BCM buffer\n", __func__);
        return -ENOMEM;
    }

    /* Ensure all the pad where IO MMU is enabled,
     * Configure all those pad's address into BCM buffer
     */
    get_pad_range(port_id, &pad_start, &pad_end);
    for (pad = pad_start; pad < pad_end; pad++) {
        if (!isp_dev->pad_data[pad].mmu_enabled ||
                !isp_dev->pad_data[pad].stream)
            continue;
        ret = process_pad_buffers(&isp_dev->pad_data[pad], pad, bcm_buf);
        if (ret != 0)
            break;
    }

    if (isp_dev->dnr3_mmu_enable) {
        if (port_id == -1)
            cur_stream_port = get_streaming_port(isp_dev, pad_start, pad_end);
        else
            cur_stream_port = port_id;

        ret = process_3dnr_buffers(isp_dev, bcm_buf, cur_stream_port);
        if (ret != 0)
            pr_err("%s: process_3dnr_buffers err\n", __func__);
    }

    if(ret) {
        /* Since there is nothing needed to be done as,
         * no IOMMU is enabled or some error.
         * reset the BCM buffer for next time usages
         */
        ISPSS_BCMBUF_Reset(bcm_buf);
        return 0;
    }
    /* Submit the configured BCM buffer with all possible,
     * IOMMU enabled pad's physical address for MTR cofig.
     * Since we use same BCM buf for all the possible ISP paths,
     * pass a dummy path here
     */

    mutex_lock(&isp_dev->mlock);
    if (isp_dev->mtr_state != ISP_MTR_IDLE_STATE)
        ret = isp_bcm_commit(bcm_buf, is_bcm_immediate, -1, NOBLOCK);
    mutex_unlock(&isp_dev->mlock);
    if (ret)
        pr_err("%s: BCM commit failed\n", __func__);

    return 0;
}
#endif

static int vvcam_isp_querycap(struct v4l2_subdev *sd, void *arg)
{
    struct v4l2_capability *cap = (struct v4l2_capability *)arg;

    strncpy(cap->driver, sd->name, sizeof(cap->driver));
    strncpy(cap->card, sd->name, sizeof(cap->card));
    snprintf(cap->bus_info, sizeof(cap->bus_info),
            "platform:%s", sd->name);

    return 0;
}

static int vvcam_isp_pad_requbufs(struct v4l2_subdev *sd, void *arg)
{
    struct vvcam_pad_reqbufs *pad_requbufs = (struct vvcam_pad_reqbufs *)arg;
    struct vvcam_isp_dev *isp_dev = v4l2_get_subdevdata(sd);

    return vvcam_isp_requebus_event(isp_dev, pad_requbufs->pad, pad_requbufs->num_buffers);
}

static int vvcam_isp_pad_buf_queue(struct v4l2_subdev *sd, void *arg)
{
    struct vvcam_pad_buf *pad_buf = (struct vvcam_pad_buf *)arg;
    struct vvcam_isp_dev *isp_dev = v4l2_get_subdevdata(sd);
    int ret;
    struct vvcam_isp_pad_data *cur_pad;
#ifdef DOLPHIN
    struct media_pad *pad;
    struct v4l2_subdev *subdev;

    cur_pad = &isp_dev->pad_data[pad_buf->pad];
    if (isp_dev->mcm_mode == ISP_MCM_MODE_ENABLED &&
            (cur_pad->sensor_out_state == SENSOR_OUT_ENABLE_MCM_MMU_ENABLE ||
             cur_pad->sensor_out_state == SENSOR_OUT_ENABLE_MCM_MMU_DISABLE)) {

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 0, 0)
         pad = media_pad_remote_pad_first(&isp_dev->pads[ISP_SINK_PAD(pad_buf->pad)]);
#else
        pad = media_entity_remote_pad(&isp_dev->pads[ISP_SINK_PAD(pad_buf->pad)]);
#endif
        if (!pad)
            return -EINVAL;

        if (is_media_entity_v4l2_subdev(pad->entity)) {
            pad_buf->pad = pad->index;

            subdev = media_entity_to_v4l2_subdev(pad->entity);
            ret = v4l2_subdev_call(subdev, core, ioctl, MCM_PAD_BUF_QUEUE, pad_buf);
            if (ret)
                return ret;
        }
    } else {
#endif
        cur_pad = &isp_dev->pad_data[pad_buf->pad];

        mutex_lock(&cur_pad->q_lock);
        list_add_tail(&pad_buf->buf->list, &cur_pad->queue);
        mutex_unlock(&cur_pad->q_lock);

        ret = vvcam_isp_qbuf_event(isp_dev, pad_buf->pad, pad_buf->buf);
#ifdef DOLPHIN
    }
#endif

    return ret;
}

#ifdef DOLPHIN
static int isp_allocate_3dnr_buffer(struct vvcam_isp_3dnr_buffer *pIsp3dnrBuf)
{
    SHM_HANDLE shm_handle;

    if (ispSS_SHM_Allocate(SHM_NONSECURE, RAW_4K_MAX_SIZE, 32,
            &shm_handle, SHM_NONSECURE_NON_CONTIG) != SUCCESS) {
        pr_err("Failed to allocate memory\n");
        return -ENOMEM;
    }
    pIsp3dnrBuf->shm_handle = shm_handle;
    ispSS_SHM_GetPageTableAddress(shm_handle, (void *)&pIsp3dnrBuf->phy_address);

    return 0;
}

static void isp_free_3dnr_buffer(struct vvcam_isp_3dnr_buffer *pIsp3dnrBuf)
{
    if (pIsp3dnrBuf->phy_address)
        ispSS_SHM_Release(pIsp3dnrBuf->shm_handle);
    pIsp3dnrBuf->phy_address = 0;
}

static int vvcam_isp_create_3dnr_buffer(struct vvcam_isp_dev *isp_dev)
{
    int ret = 0;

    ret = isp_allocate_3dnr_buffer(&isp_dev->dnr3_buf[0]);
    if (ret != 0) {
        pr_err("Failed to create denoise buffer-0\n");
        return -ENOMEM;
    }

    // If MCM Mode is enabled allocate one more buffer
    if (isp_dev->mcm_mode == ISP_MCM_MODE_ENABLED) {
        ret = isp_allocate_3dnr_buffer(&isp_dev->dnr3_buf[1]);
        if (ret != 0) {
            pr_err("Failed to create denoise buffer-1\n");
            isp_free_3dnr_buffer(&isp_dev->dnr3_buf[0]); // Free the 0th buffer
            return -ENOMEM;
        }
    }

    return 0;
}

static void vvcam_isp_destroy_3dnr_buffer(struct vvcam_isp_dev *isp_dev)
{
    isp_free_3dnr_buffer(&isp_dev->dnr3_buf[0]);

    if (isp_dev->mcm_mode == ISP_MCM_MODE_ENABLED)
        isp_free_3dnr_buffer(&isp_dev->dnr3_buf[1]);
}

static int vvcam_init_3dnr(struct vvcam_isp_dev *isp_dev)
{
    int ret = 0;

    if (isp_dev->mcm_mode == ISP_MCM_MODE_ENABLED) {
        // Currently 3DNR is disabled for MCM Mode
        isp_dev->dnr3_mmu_enable = 0;
    } else {
        isp_dev->dnr3_mmu_enable = isp_dev->dnr3_mmu_ctrl_tmp;
        if (isp_dev->dnr3_mmu_enable)
            ret = vvcam_isp_create_3dnr_buffer(isp_dev);
    }
    return ret;
}

static int vvcam_isp_pad_stream_on(struct vvcam_isp_dev *isp_dev, int pad,
                                            int32_t is_pad_mmu_enabled)
{
    uint32_t path = -1;
    int ret = 0;
    struct vvcam_isp_pad_data *cur_pad = &isp_dev->pad_data[pad];
    struct device *dev =  isp_dev->dev;
    uint8_t en_3dnr = 0;

    /* Skip the function when MMU is disabled and non MCM mode
     * For MCM mode BCM clock is needed irrespctive of MMU enable or not
     * So MCM mode this function needs to be executed
     * */
    if (!is_pad_mmu_enabled  &&
            isp_dev->mcm_mode == ISP_MCM_MODE_DISABLED)
        return 0;

    mutex_lock(&isp_dev->mlock);

    /* Set Pad MMU enabled flag
    */
    if (is_pad_mmu_enabled) {
        mutex_lock(&cur_pad->q_lock);
        cur_pad->mmu_enabled = 1;
        mutex_unlock(&cur_pad->q_lock);
    }

    if (isp_dev->mcm_mode == ISP_MCM_MODE_ENABLED) {
        /* IN MCM mode, BCM clock needs to be enabled for first stream
         * No update for BCM mux, as BCM direct write will be applied
         * in MCM mode
         */
        if (!isp_dev->mcm_ref_count) {
            isp_dev->mtr_state = ISP_MTR_RUNNING_STATE;
            en_3dnr = 1;
        }
        isp_dev->mcm_ref_count++;
        if (isp_dev->mcm_ref_count >= U8_MAX) {
            dev_err(dev, "mcm_ref_count variable overflow\n");
            ret = -EINVAL;
        }
    } else {
        path = vvcam_isp_get_mtr_path(pad);
        if (path == ISPSS_MTR_PATH_INVALID) {
            dev_err(dev, "Invalid mtr path! \n");
            ret = -EINVAL;
            goto unlock;
        }
        if (!isp_dev->bcm_conf_ref_count) {
            /* BCM clock needs to be enabled before doing
             * configure bcm interrupt mask settings
             */
            isp_bcm_enable_clock();
            isp_dev->mtr_state = ISP_MTR_RUNNING_STATE;
            isp_bcm_configure(path);
            en_3dnr = 1;
        } else {
            /* Subsequent cases, only mux needs to be modified
            */
            isp_bcm_update_interrupt_mux(path, 1);
        }
        if (!ret) {
            ++isp_dev->bcm_conf_ref_count;
            if (isp_dev->bcm_conf_ref_count >= U8_MAX) {
                dev_err(dev, "bcm_conf_ref_count variable overflow\n");
                ret = -EINVAL;
                goto unlock;
            }
        }
    }

    /* Enable 3DNR Buffer IOMMU based on control value received on first stream open.
     * The control value from second stream open is ignored.
     */
    if (en_3dnr) {
        ret = vvcam_init_3dnr(isp_dev);
        if (!ret)
            goto unlock;
    }

unlock:
    mutex_unlock(&isp_dev->mlock);

    return ret;
}

static int vvcam_isp_pad_stream_off(struct vvcam_isp_dev *isp_dev, int pad,
        int32_t is_pad_mmu_enabled)
{
    uint8_t is_stop_bcm_clock = 0;
    uint32_t path = -1;
    int ret = 0;
    uint8_t found = 0, pad_index = 0;
    struct vvcam_isp_pad_data *cur_pad = &isp_dev->pad_data[pad];
    struct device *dev =  isp_dev->dev;
    uint8_t port = pad / VVCAM_ISP_PORT_PAD_NR;

    /* Skip the function when MMU is disabled and non MCM mode
     * For MCM mode BCM clock is needed irrespctive of MMU enable or not
     * So MCM mode this function needs to be executed
     * */
    if (!is_pad_mmu_enabled &&
            isp_dev->mcm_mode == ISP_MCM_MODE_DISABLED)
        return 0;

    mutex_lock(&isp_dev->mlock);

    //Reset Pad MMU enabled flag
    mutex_lock(&cur_pad->q_lock);
    cur_pad->mmu_enabled = 0;
    mutex_unlock(&cur_pad->q_lock);

    if (isp_dev->mcm_mode == ISP_MCM_MODE_ENABLED) {
        /* MCM mode enable only BCM clock disable is needed while stopping
         * In MCM BCM direct write happens so no need to update BCM Mux
         */
        if (isp_dev->mcm_ref_count >= U8_MAX) {
            dev_err(dev, "mcm_ref_count variable overflow\n");
            ret = -EINVAL;
            goto unlock;
        }
        --isp_dev->mcm_ref_count;
        if (!isp_dev->mcm_ref_count) {
            dev_dbg(dev, "mcm_ref_count = %d. Call disable_mtr_clock\n",
                    isp_dev->mcm_ref_count);
            is_stop_bcm_clock = 1;
        }
    } else {
        /* In non MCM mode, whle stopping BCM clock to be disabled
         * Deconfigure the BCM interrupt mux or update if ref count is not zero
         */
        path = vvcam_isp_get_mtr_path(pad);
        if (path == ISPSS_MTR_PATH_INVALID) {
            dev_err(dev, "Invalid mtr path\n");
            ret = -EINVAL;
            goto unlock;
        }
        isp_dev->bcm_conf_ref_count--;
        if (!isp_dev->bcm_conf_ref_count) {
            is_stop_bcm_clock = 1;
            isp_bcm_deconfigure(path);
        } else if (isp_dev->bcm_conf_ref_count >= U8_MAX) {
            dev_err(dev, "bcm_conf_ref_count variable overflow\n");
            ret = -EINVAL;
            goto unlock;
        }
        isp_bcm_update_interrupt_mux(path, 0);
    }

    if (is_stop_bcm_clock) {
        isp_dev->mtr_state = ISP_MTR_IDLE_STATE;
        if (isp_dev->dnr3_mmu_enable) {
            ISPSS_MTR_Exit(ISPSS_MTR_PATH_TDNR0_RD + port);
            ISPSS_MTR_Exit(ISPSS_MTR_PATH_TDNR0_WR + port);
            vvcam_isp_destroy_3dnr_buffer(isp_dev);
        }
        if(isp_dev->mcm_mode == ISP_MCM_MODE_DISABLED)
            isp_bcm_disable_clock();
        // Reset 3DNR MMU Flag
        isp_dev->dnr3_mmu_enable = 0;
    }

    for (pad_index = 0; pad_index < VVCAM_ISP_PAD_NR; pad_index++) {
        if (isp_dev->pad_data[pad_index].stream) {
            found = 1;
            break;
        }
    }

    if (!found)
        isp_dev->process_done = 0;

unlock:
    mutex_unlock(&isp_dev->mlock);

    return ret;
}
#endif

static int vvcam_isp_pad_s_stream(struct v4l2_subdev *sd, void *arg)
{
    struct vvcam_pad_stream_status *pad_stream = (struct vvcam_pad_stream_status *)arg;
    struct vvcam_isp_dev *isp_dev = v4l2_get_subdevdata(sd);
    int ret = 0;
#ifdef DOLPHIN
    struct vvcam_isp_pad_data *cur_pad;
    struct vvcam_vb2_buffer *pos =  NULL, *next = NULL;
    struct media_pad *pad;
    struct v4l2_subdev *subdev;
    struct mcm_stream_status mcm_stream;

    cur_pad = &isp_dev->pad_data[pad_stream->pad];

    if (!pad_stream->param.status) {
        /* cur_pad->stream should be disabled even before stream OFF, so process_pad_buffers/
         * ISPSS_MTR_ConfigureMtr is not called during stream_off.
         */
        cur_pad->stream = pad_stream->param.status;
        ret = vvcam_isp_pad_stream_off(isp_dev, pad_stream->pad,
                pad_stream->param.ctrl_ctx.iommu_enabled);
        mutex_lock(&cur_pad->q_lock);
        INIT_LIST_HEAD(&cur_pad->queue);
        mutex_unlock(&cur_pad->q_lock);
    } else {
        ret = vvcam_isp_pad_stream_on(isp_dev, pad_stream->pad,
                pad_stream->param.ctrl_ctx.iommu_enabled);
        /* cur_pad->stream should be enabled after stream ON, so process_pad_buffers/
         * ISPSS_MTR_ConfigureMtr is not called until stream ON execution is complete
         */
        mutex_lock(&cur_pad->q_lock);
        cur_pad->stream = pad_stream->param.status;
        mutex_unlock(&cur_pad->q_lock);
        pad_stream->param.ctrl_ctx.dnr3_mmu_en = isp_dev->dnr3_mmu_enable;
    }

    /* If in MCM mode, inform the MCM subdev. Each MCM subdev pad can receive stream event
     * from three different paths. It just needs to keep the count of active streams (+/-
     * on Stream ON/OFF)
     */
    if (isp_dev->mcm_mode == ISP_MCM_MODE_ENABLED) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 0, 0)
        pad = media_pad_remote_pad_first(&isp_dev->pads[ISP_SINK_PAD(pad_stream->pad)]);
#else
        pad = media_entity_remote_pad(&isp_dev->pads[ISP_SINK_PAD(pad_stream->pad)]);
#endif
        if (!pad)
            return -EINVAL;

        if (is_media_entity_v4l2_subdev(pad->entity)) {
            mcm_stream.pad = pad->index;
            mcm_stream.status = pad_stream->param.status ? 1 : -1;

            subdev = media_entity_to_v4l2_subdev(pad->entity);
            ret = v4l2_subdev_call(subdev, core, ioctl, MCM_PAD_S_STREAM, &mcm_stream);
        }
    }

    if (!ret)
        ret = vvcam_isp_s_stream_event(isp_dev, pad_stream->pad, &pad_stream->param);
    if (!ret && pad_stream->param.status) {
        if (isp_dev->mcm_mode != ISP_MCM_MODE_ENABLED) {
            mutex_lock(&cur_pad->q_lock);
            cur_pad->skip_first_frame = true;
            mutex_unlock(&cur_pad->q_lock);
            pr_debug("%s: pad %u stream ON, first frame will be skipped\n",
                    __func__, pad_stream->pad);
        }
        } else if (ret && pad_stream->param.status) {
                pr_err("%s: pad %u stream ON failed, ret %d\n",
                                __func__, pad_stream->pad, ret);
        }
    /* driver can return an error if hardware fails, in that
     * case all buffers that have been already given by
     * the @buf_queue callback are to be returned by the driver
     * by calling vb2_buffer_done() with %VB2_BUF_STATE_QUEUED.
     */
    if (ret && pad_stream->param.status) {
        mutex_lock(&cur_pad->q_lock);
        if (!list_empty(&cur_pad->queue)) {
            list_for_each_entry_safe(pos, next, &cur_pad->queue, list) {
                if (pos) {
                    vb2_buffer_done(&pos->vb.vb2_buf, VB2_BUF_STATE_QUEUED);
                    list_del(&pos->list);
                }
            }
        }
        mutex_unlock(&cur_pad->q_lock);
    }
#else
    isp_dev->pad_data[pad_stream->pad].stream = pad_stream->status;
    if (pad_stream->status == 0)
        INIT_LIST_HEAD(&isp_dev->pad_data[pad_stream->pad].queue);
    ret = vvcam_isp_s_stream_event(isp_dev, pad_stream->pad, pad_stream->status);
#endif

    return ret;
}

#ifdef DOLPHIN
static int process_bcm(void *arg)
{
    struct vvcam_isp_dev *isp_dev = (struct vvcam_isp_dev *)arg;

    while (!kthread_should_stop()) {
        wait_event_interruptible_timeout(isp_dev->wq,
                isp_dev->process_done > 0,
                msecs_to_jiffies(1000));

        if (isp_dev->process_done > 0) {
            mutex_lock(&isp_dev->mlock);
            isp_dev->process_done --;
            mutex_unlock(&isp_dev->mlock);

            push_buf(isp_dev, -1);
        }
        usleep_range(2, 5);
    }

    return 0;
}
#endif

#ifdef DOLPHIN
static int vvcam_isp_recycle_buffer(struct vvcam_isp_dev *isp_dev,
        struct vvcam_isp_pad_data *cur_pad,
        struct vvcam_vb2_buffer *buf, unsigned int pad,
        unsigned int index)
{
    int qbuf_ret = 0;

    if (cur_pad->mmu_enabled)
        buf->is_pushed_queue = 0;
    mutex_lock(&cur_pad->q_lock);
    list_add_tail(&buf->list, &cur_pad->queue);
    mutex_unlock(&cur_pad->q_lock);
    qbuf_ret = vvcam_isp_qbuf_event(isp_dev, pad, buf);
    if (qbuf_ret) {
        mutex_lock(&cur_pad->q_lock);
        list_del_init(&buf->list);
        mutex_unlock(&cur_pad->q_lock);
        pr_err("%s: pad %u buffer %u recycle failed, ret %d\n",
                __func__, pad, index, qbuf_ret);
    }

    return qbuf_ret;
}
#endif

static int vvcam_isp_buf_done(struct v4l2_subdev *sd, void *arg)
{
    struct vvcam_isp_buf ubuf;
    struct vvcam_isp_pad_data *cur_pad;
    struct vvcam_isp_dev *isp_dev = v4l2_get_subdevdata(sd);
    struct vvcam_vb2_buffer *pos =  NULL, *next = NULL;
    struct vvcam_vb2_buffer *buf = NULL;
    struct media_pad *pad;
    struct v4l2_subdev *subdev;
    struct video_device *video;
    struct vvcam_pad_buf pad_buf;
    int ret = 0;
#ifdef DOLPHIN
    bool skip_first_frame = false;
#endif

    memcpy(&ubuf, arg, sizeof(struct vvcam_isp_buf));

    cur_pad = &isp_dev->pad_data[ubuf.pad];

    mutex_lock(&cur_pad->q_lock);

    if (list_empty(&cur_pad->queue) || (cur_pad->stream == 0)) {
        mutex_unlock(&cur_pad->q_lock);
        return -EINVAL;
    }
    list_for_each_entry_safe(pos, next, &cur_pad->queue, list) {
        if (pos && (pos->sequence == ubuf.index)) {
            buf = pos;
            list_del(&pos->list);
            break;
        }
    }

#ifdef DOLPHIN
    if (buf && isp_dev->mcm_mode != ISP_MCM_MODE_ENABLED &&
            cur_pad->skip_first_frame) {
        skip_first_frame = true;
        cur_pad->skip_first_frame = false;
    }
#endif

    mutex_unlock(&cur_pad->q_lock);

    if (buf) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 0, 0)
        pad = media_pad_remote_pad_first(&isp_dev->pads[ubuf.pad]);
#else
        pad = media_entity_remote_pad(&isp_dev->pads[ubuf.pad]);
#endif
        if (!pad) {
            ret =  -EINVAL;
            pr_err("%s: Error No remote pad available\n" , __func__);
            goto out;
        }
        if (is_media_entity_v4l2_subdev(pad->entity)) {

            subdev = media_entity_to_v4l2_subdev(pad->entity);
            memset(&pad_buf, 0, sizeof(pad_buf));
            pad_buf.pad = pad->index;
            pad_buf.buf = buf;
            ret = v4l2_subdev_call(subdev, core, ioctl, VVCAM_PAD_BUF_DONE, &pad_buf);
            if (ret) {
                pr_err("%s: Error Remote Subdev call fails\n" , __func__);
                goto out;
            }
        } else if (is_media_entity_v4l2_video_device(pad->entity)) {
            video = media_entity_to_video_device(pad->entity);
            if (buf->sequence < vb2_get_num_buffers(video->queue)) {
                if (buf->vb.vb2_buf.state == VB2_BUF_STATE_ACTIVE) {
                    buf->vb.vb2_buf.timestamp = ktime_get();
#ifndef DOLPHIN
                    vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
#else
                    if (skip_first_frame) {
                        ret = vvcam_isp_recycle_buffer(isp_dev, cur_pad, buf,
                                ubuf.pad, ubuf.index);
                        if (ret)
                            vb2_buffer_done(&buf->vb.vb2_buf,
                                    VB2_BUF_STATE_ERROR);
                    } else if (cur_pad->mmu_enabled && !buf->is_pushed_queue) {
                        pr_err("buffer not pushed to MTR for pad %d index %d\n",
                                ubuf.pad, ubuf.index);
                        vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_ERROR);
                    } else {
                        vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
                    }
#endif
                }
            }
        }
    }

#if DOLPHIN
    if (isp_dev->mcm_mode == ISP_MCM_MODE_DISABLED &&
                        cur_pad->mmu_enabled) {

        mutex_lock(&isp_dev->mlock);
        if (isp_dev->bcm_conf_ref_count == 1 ||
           ubuf.pad % VVCAM_ISP_PORT_PAD_NR == VVCAM_ISP_PORT_PAD_SOURCE_MP)
        isp_dev->process_done++;
        mutex_unlock(&isp_dev->mlock);

        wake_up(&isp_dev->wq);
    }
#endif

out:
    return ret;
}

#if DOLPHIN
static int vvcam_isp_mtr_update(struct v4l2_subdev *sd, void *arg)
{
    struct vvcam_isp_dev *isp_dev = v4l2_get_subdevdata(sd);
    struct vvcam_pad_mtr_update Port;

    memcpy(&Port, arg, sizeof(struct vvcam_pad_mtr_update));
    push_buf(isp_dev, Port.port);

    return 0;
}
#endif

static int vvcam_isp_queryctrl(struct v4l2_subdev *sd,void *arg)
{
    int ret;
    struct vvcam_isp_dev *isp_dev = v4l2_get_subdevdata(sd);
    struct vvcam_pad_queryctrl *pad_querctrl =
                                    (struct vvcam_pad_queryctrl *)arg;
    ret = v4l2_queryctrl(&isp_dev->ctrl_handler, pad_querctrl->query_ctrl);

    return ret;
}

static int vvcam_isp_query_ext_ctrl(struct v4l2_subdev *sd,void *arg)
{
    int ret;
    struct vvcam_isp_dev *isp_dev = v4l2_get_subdevdata(sd);
    struct vvcam_pad_query_ext_ctrl *pad_quer_ext_ctrl =
                                    (struct vvcam_pad_query_ext_ctrl *)arg;
    ret = v4l2_query_ext_ctrl(&isp_dev->ctrl_handler,
                        pad_quer_ext_ctrl->query_ext_ctrl);

    return ret;
}

static int vvcam_isp_querymenu(struct v4l2_subdev *sd,void *arg)
{
    int ret;
    struct vvcam_isp_dev *isp_dev = v4l2_get_subdevdata(sd);
    struct vvcam_pad_querymenu *pad_quermenu =
                                    (struct vvcam_pad_querymenu *)arg;
    ret = v4l2_querymenu(&isp_dev->ctrl_handler,
                        pad_quermenu->querymenu);

    return ret;
}

static int vvcam_isp_g_ctrl(struct v4l2_subdev *sd,void *arg)
{
    int ret;
    struct vvcam_isp_dev *isp_dev = v4l2_get_subdevdata(sd);
    struct vvcam_pad_control *pad_ctrl = (struct vvcam_pad_control *)arg;

    mutex_lock(&isp_dev->ctrl_lock);
    isp_dev->ctrl_pad = pad_ctrl->pad;
    ret = v4l2_g_ctrl(&isp_dev->ctrl_handler, pad_ctrl->control);
    mutex_unlock(&isp_dev->ctrl_lock);

    return ret;
}

static int vvcam_isp_s_ctrl(struct v4l2_subdev *sd,void *arg)
{
    int ret;
    struct vvcam_isp_dev *isp_dev = v4l2_get_subdevdata(sd);
    struct vvcam_pad_control *pad_ctrl = (struct vvcam_pad_control *)arg;

    mutex_lock(&isp_dev->ctrl_lock);
    isp_dev->ctrl_pad = pad_ctrl->pad;
    ret = v4l2_s_ctrl(NULL, &isp_dev->ctrl_handler, pad_ctrl->control);
    mutex_unlock(&isp_dev->ctrl_lock);

    return ret;
}

static int vvcam_isp_g_ext_ctrls(struct v4l2_subdev *sd,void *arg)
{
    int ret;
    struct vvcam_isp_dev *isp_dev = v4l2_get_subdevdata(sd);
    struct vvcam_pad_ext_controls *pad_ext_ctrls =
                            (struct vvcam_pad_ext_controls *)arg;

    mutex_lock(&isp_dev->ctrl_lock);
    isp_dev->ctrl_pad = pad_ext_ctrls->pad;
    ret = v4l2_g_ext_ctrls(&isp_dev->ctrl_handler, sd->devnode,
                            sd->v4l2_dev->mdev,
                            pad_ext_ctrls->ext_controls);
    mutex_unlock(&isp_dev->ctrl_lock);

    return ret;
}

static int vvcam_isp_s_ext_ctrls(struct v4l2_subdev *sd,void *arg)
{
    int ret;
    struct vvcam_isp_dev *isp_dev = v4l2_get_subdevdata(sd);
    struct vvcam_pad_ext_controls *pad_ext_ctrls =
                            (struct vvcam_pad_ext_controls *)arg;

    mutex_lock(&isp_dev->ctrl_lock);
    isp_dev->ctrl_pad = pad_ext_ctrls->pad;
    ret = v4l2_s_ext_ctrls(NULL, &isp_dev->ctrl_handler, sd->devnode,
                            sd->v4l2_dev->mdev,
                            pad_ext_ctrls->ext_controls);
    mutex_unlock(&isp_dev->ctrl_lock);

    return ret;
}

static int vvcam_isp_try_ext_ctrls(struct v4l2_subdev *sd,void *arg)
{
    int ret;
    struct vvcam_isp_dev *isp_dev = v4l2_get_subdevdata(sd);
    struct vvcam_pad_ext_controls *pad_ext_ctrls =
                            (struct vvcam_pad_ext_controls *)arg;
    ret = v4l2_try_ext_ctrls(&isp_dev->ctrl_handler, sd->devnode,
                            sd->v4l2_dev->mdev,
                            pad_ext_ctrls->ext_controls);

    return ret;
}

#ifdef DOLPHIN
static int vvcam_isp_set_format(struct v4l2_subdev *sd, void *arg)
{
    int ret = 0;
    struct vvcam_isp_dev *isp_dev = v4l2_get_subdevdata(sd);
    struct v4l2_format *f;
    struct vvcam_pad_set_format *v4l2_format_pad =
                             (struct vvcam_pad_set_format *)arg;
    struct vvcam_isp_pad_data *cur_pad = &isp_dev->pad_data[v4l2_format_pad->pad];

    mutex_lock(&isp_dev->ctrl_lock);
    cur_pad->v4l2_format = v4l2_format_pad->v4l2_format;
    mutex_unlock(&isp_dev->ctrl_lock);
    f = &cur_pad->v4l2_format;
    return ret;
}

static int create_process_bcm_thread(struct vvcam_isp_dev *isp_dev)
{
    init_waitqueue_head(&isp_dev->wq);

    isp_dev->task = kthread_run(process_bcm, isp_dev, "process_bcm");
    if (IS_ERR(isp_dev->task)) {
        pr_err("%s: Failed to create kernel thread\n", __func__);
        return PTR_ERR(isp_dev->task);
    }

    return 0;
}

static int vvcam_isp_set_mcm_mode(struct v4l2_subdev *sd, void *arg)
{
    int ret = 0;
    struct vvcam_isp_dev *isp_dev = v4l2_get_subdevdata(sd);
    struct vvcam_mcm_mode *mcm = (struct vvcam_mcm_mode *) arg;

    mutex_lock(&isp_dev->ctrl_lock);
    isp_dev->mcm_mode = mcm->mcm_mode;
    pr_info("%s: The MCM mode is %d\n", __func__, isp_dev->mcm_mode);
    if(mcm->mcm_mode == ISP_MCM_MODE_DISABLED) {
        ret = create_process_bcm_thread(isp_dev);
    }
    mutex_unlock(&isp_dev->ctrl_lock);

    return ret;
}

static int vvcam_isp_set_sensor_out_state(struct v4l2_subdev *sd, void *arg)
{
    int ret = 0;
    struct vvcam_isp_dev *isp_dev = v4l2_get_subdevdata(sd);
    struct vvcam_sensor_out_state *vv_sensor_out_state = (struct vvcam_sensor_out_state *) arg;
    struct v4l2_subdev *subdev;
    struct media_pad *pad;
    struct vvcam_isp_pad_data *cur_pad;
    struct mcm_buf_state m_sensor_out_state;

    mutex_lock(&isp_dev->ctrl_lock);
    cur_pad = &isp_dev->pad_data[vv_sensor_out_state->pad];
    cur_pad->sensor_out_state = vv_sensor_out_state->sensor_out_state;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 0, 0)
      pad = media_pad_remote_pad_first(&isp_dev->pads[ISP_SINK_PAD(vv_sensor_out_state->pad)]);
#else
    pad = media_entity_remote_pad(&isp_dev->pads[ISP_SINK_PAD(vv_sensor_out_state->pad)]);
#endif
    if (!pad) {
        ret = -EINVAL;
        goto unlock;
    }

    if (is_media_entity_v4l2_subdev(pad->entity)) {
        m_sensor_out_state.pad = pad->index;
        m_sensor_out_state.sensor_out_state = cur_pad->sensor_out_state;
        subdev = media_entity_to_v4l2_subdev(pad->entity);
        ret = v4l2_subdev_call(subdev, core, ioctl, MCM_PAD_MCM_BUF_STATE, &m_sensor_out_state);
        if (ret)
            pr_debug("sensor out set state failed\n");
    }

unlock:
    mutex_unlock(&isp_dev->ctrl_lock);

    return ret;
}
#endif

static long vvcam_isp_priv_ioctl(struct v4l2_subdev *sd,
                                unsigned int cmd, void *arg)
{
    int ret = -EINVAL;
    switch (cmd) {
        case VIDIOC_QUERYCAP:
            ret = vvcam_isp_querycap(sd, arg);
            break;
        case VVCAM_PAD_REQUBUFS:
            ret = vvcam_isp_pad_requbufs(sd, arg);
            break;
        case VVCAM_PAD_BUF_QUEUE:
            ret = vvcam_isp_pad_buf_queue(sd, arg);
            break;
        case VVCAM_PAD_S_STREAM:
            ret = vvcam_isp_pad_s_stream(sd, arg);
            break;
#ifdef DOLPHIN
        case VVCAM_PAD_SET_FORMAT:
            ret = vvcam_isp_set_format(sd, arg);
            break;
        case VVCAM_PAD_SET_MCM_MODE:
            ret = vvcam_isp_set_mcm_mode(sd, arg);
            break;
        case VVCAM_PAD_SET_SENSOR_OUT_STATE:
            ret = vvcam_isp_set_sensor_out_state(sd, arg);
            break;
        case VVCAM_PAD_MTR_UPDATE:
            ret = vvcam_isp_mtr_update(sd, arg);
            break;
#endif
        case VVCAM_ISP_IOC_BUFDONE:
            ret = vvcam_isp_buf_done(sd, arg);
            break;
        case VVCAM_PAD_QUERYCTRL:
            ret = vvcam_isp_queryctrl(sd, arg);
            break;
        case VVCAM_PAD_QUERY_EXT_CTRL:
            ret = vvcam_isp_query_ext_ctrl(sd, arg);
            break;
        case VVCAM_PAD_G_CTRL:
            ret = vvcam_isp_g_ctrl(sd, arg);
            break;
        case VVCAM_PAD_S_CTRL:
            ret = vvcam_isp_s_ctrl(sd, arg);
            break;
        case VVCAM_PAD_G_EXT_CTRLS:
            ret = vvcam_isp_g_ext_ctrls(sd, arg);
            break;
        case VVCAM_PAD_S_EXT_CTRLS:
            ret = vvcam_isp_s_ext_ctrls(sd, arg);
            break;
        case VVCAM_PAD_TRY_EXT_CTRLS:
            ret = vvcam_isp_try_ext_ctrls(sd, arg);
            break;
        case VVCAM_PAD_QUERYMENU:
            ret = vvcam_isp_querymenu(sd, arg);
            break;
        default:
            break;
    }

    return ret;
}

int vvcam_isp_subscribe_event(struct v4l2_subdev *sd,
            struct v4l2_fh *fh,
            struct v4l2_event_subscription *sub)
{
    switch (sub->type) {
        case V4L2_EVENT_CTRL:
            return v4l2_ctrl_subdev_subscribe_event(sd, fh, sub);
        case VVCAM_ISP_DEAMON_EVENT:
            return v4l2_event_subscribe(fh, sub, 2, NULL);
        default:
            return -EINVAL;
    }

}

static struct v4l2_subdev_core_ops vvcam_isp_core_ops = {
    .ioctl             = vvcam_isp_priv_ioctl,
    .subscribe_event   = vvcam_isp_subscribe_event,
    .unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static struct v4l2_subdev_video_ops vvcam_isp_video_ops = {
    /*.s_stream = vvcam_isp_s_stream,*/
};

static int vvcam_isp_set_fmt(struct v4l2_subdev *sd,
            struct v4l2_subdev_state *sd_state,
            struct v4l2_subdev_format *format)
{
    struct vvcam_isp_dev *isp_dev = v4l2_get_subdevdata(sd);
    uint32_t w, h;
    uint32_t sink_pad_index;
    struct vvcam_isp_pad_data *cur_pad = &isp_dev->pad_data[format->pad];
    struct vvcam_isp_pad_data *sink_pad;
    struct vvcam_isp_pad_data *source_pad;
    int i;
    int ret;

    sink_pad_index = format->pad - (format->pad % VVCAM_ISP_PORT_PAD_NR);
    sink_pad = &isp_dev->pad_data[sink_pad_index];

    if (sink_pad == cur_pad) {
        cur_pad->sink_detected = 1;
        cur_pad->format = format->format;
        for (i = 1; i < VVCAM_ISP_PORT_PAD_NR; i++) {
            source_pad = &isp_dev->pad_data[sink_pad_index + i];
            source_pad->sink_detected = 1;

            switch (i) {
                case VVCAM_ISP_PORT_PAD_SOURCE_MP:
                case VVCAM_ISP_PORT_PAD_SOURCE_SP1:
                case VVCAM_ISP_PORT_PAD_SOURCE_SP2:
                    source_pad->format = format->format;
                    source_pad->format.code = source_pad->mbus_fmt[0].code;
                    source_pad->format.field = V4L2_FIELD_NONE;
                    source_pad->format.quantization = V4L2_QUANTIZATION_DEFAULT;
                    source_pad->format.colorspace = V4L2_COLORSPACE_DEFAULT;
                    break;
                case VVCAM_ISP_PORT_PAD_SOURCE_RAW:
                    source_pad->format = format->format;
                    break;
                default:
                    break;
            }
        }
        return 0;
    }

    w = ALIGN(format->format.width, VVCAM_ISP_WIDTH_ALIGN);
    h = ALIGN(format->format.height, VVCAM_ISP_HEIGHT_ALIGN);
    w = clamp_t(uint32_t, w, VVCAM_ISP_WIDTH_MIN, sink_pad->format.width);
    h = clamp_t(uint32_t, h, VVCAM_ISP_HEIGHT_MIN, sink_pad->format.height);

    format->format.width = w;
    format->format.height = h;

    for (i = 0; i < cur_pad->num_formats; i++) {
        if (format->format.code == cur_pad->mbus_fmt[i].code)
            break;
    }

    if (i >= cur_pad->num_formats) {
        format->format.code = cur_pad->mbus_fmt[0].code;
    }

    ret = vvcam_isp_set_fmt_event(isp_dev, format->pad, &format->format);
    if (ret)
        return ret;

    cur_pad->format = format->format;

    return 0;
}

static int vvcam_isp_get_fmt(struct v4l2_subdev *sd,
            struct v4l2_subdev_state *sd_state,
            struct v4l2_subdev_format *format)
{
    struct vvcam_isp_dev *isp_dev = v4l2_get_subdevdata(sd);
    struct vvcam_isp_pad_data *pad_data = &isp_dev->pad_data[format->pad];

    if (pad_data->sink_detected) {
        format->format = pad_data->format;
    } else {
        return -EINVAL;
    }

    return 0;
}

static int vvcam_isp_enum_mbus_code(struct v4l2_subdev *sd,
            struct v4l2_subdev_state *sd_state,
            struct v4l2_subdev_mbus_code_enum *code)
{
    struct vvcam_isp_dev *isp_dev = v4l2_get_subdevdata(sd);
    struct vvcam_isp_pad_data *pad_data = &isp_dev->pad_data[code->pad];

    if (code->index >= pad_data->num_formats) {
        return -EINVAL;
    }

    code->code = pad_data->mbus_fmt[code->index].code;

    return 0;
}

static const struct v4l2_subdev_pad_ops vvcam_isp_pad_ops = {
    .set_fmt        = vvcam_isp_set_fmt,
    .get_fmt        = vvcam_isp_get_fmt,
    .enum_mbus_code = vvcam_isp_enum_mbus_code,
};

struct v4l2_subdev_ops vvcam_isp_subdev_ops = {
    .core  = &vvcam_isp_core_ops,
    .video = &vvcam_isp_video_ops,
    .pad   = &vvcam_isp_pad_ops,
};

static int vvcam_isp_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
    struct vvcam_isp_dev *isp_dev = v4l2_get_subdevdata(sd);

    mutex_lock(&isp_dev->mlock);

    isp_dev->refcnt++;
    pm_runtime_get_sync(sd->dev);

    mutex_unlock(&isp_dev->mlock);
    return 0;
}

static int vvcam_isp_close(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
    struct vvcam_isp_dev *isp_dev = v4l2_get_subdevdata(sd);

    mutex_lock(&isp_dev->mlock);

    isp_dev->refcnt--;
    pm_runtime_put_sync(sd->dev);

    mutex_unlock(&isp_dev->mlock);

    return 0;
}


static struct v4l2_subdev_internal_ops vvcam_isp_internal_ops = {
    .open  = vvcam_isp_open,
    .close = vvcam_isp_close,
};

static int vvcam_isp_link_setup(struct media_entity *entity,
        const struct media_pad *local,
        const struct media_pad *remote, u32 flags)
{
    return 0;
}

static const struct media_entity_operations vvcam_isp_entity_ops = {
    .link_setup     = vvcam_isp_link_setup,
    .link_validate  = v4l2_subdev_link_validate,
    .get_fwnode_pad = v4l2_subdev_get_fwnode_pad_1_to_1,

};

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
static int vvcam_isp_notifier_bound(struct v4l2_async_notifier *notifier,
                                    struct v4l2_subdev *sd,
                                    struct v4l2_async_connection *asc)
#else
static int vvcam_isp_notifier_bound(struct v4l2_async_notifier *notifier,
        struct v4l2_subdev *sd,
        struct v4l2_async_subdev *asd)
#endif
{
    int ret = 0;
    struct vvcam_isp_dev *isp_dev = container_of(notifier,
            struct vvcam_isp_dev, notifier);
    struct device *dev =  isp_dev->dev;

    struct fwnode_handle *ep = NULL;
    struct v4l2_fwnode_link link;
    struct media_entity *source, *sink;
    unsigned int source_pad, sink_pad;

    while(1) {
        ep = fwnode_graph_get_next_endpoint(sd->fwnode, ep);
        if (!ep)
            break;

        ret = v4l2_fwnode_parse_link(ep, &link);
        if (ret < 0) {
            dev_err(dev, "failed to parse link for %pOF: %d\n",
                    to_of_node(ep), ret);
            continue;
        }

        if (sd->entity.pads[link.local_port].flags == MEDIA_PAD_FL_SINK)
            continue;

        source     = &sd->entity;
        sink       = &isp_dev->sd.entity;
        source_pad = link.local_port;
        sink_pad   = link.remote_port;
        v4l2_fwnode_put_link(&link);
        dev_info(sd->dev, "%s: linking %s source_pad %d flags %ld and %s sink_pad %d flags %ld\n",
                __func__, source->name, source_pad, source->pads[source_pad].flags,
                sink->name, sink_pad, sink->pads[sink_pad].flags);
        ret = media_create_pad_link(source, source_pad,
        sink, sink_pad, MEDIA_LNK_FL_ENABLED);
        if (ret) {
            dev_err(dev, "failed to create %s:%u -> %s:%u link\n",
                source->name, source_pad,
                sink->name, sink_pad);
            break;
        }

    }

    fwnode_handle_put(ep);

    return ret;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
static void vvcam_isp_notifier_unbound(struct v4l2_async_notifier *notifier,
                                     struct v4l2_subdev *sd,
                                     struct v4l2_async_connection *asc)
{
    return;
}
#else
static void vvcam_isp_notifier_unbound(struct v4l2_async_notifier *notifier,
        struct v4l2_subdev *sd,
        struct v4l2_async_subdev *asd)
{
    return;
}
#endif

static const struct v4l2_async_notifier_operations vvcam_isp_notify_ops = {
    .bound    = vvcam_isp_notifier_bound,
    .unbind   = vvcam_isp_notifier_unbound,
};

static int vvcam_isp_async_notifier(struct vvcam_isp_dev *isp_dev)
{
    struct fwnode_handle *ep;
    struct fwnode_handle *remote_ep;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
    struct v4l2_async_connection *asc;
#else
    struct v4l2_async_subdev *asd;
#endif
    struct device *dev = isp_dev->dev;
    int ret = 0;
    int pad = 0;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
    v4l2_async_subdev_nf_init(&isp_dev->notifier, &isp_dev->sd);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 16, 0)
    v4l2_async_nf_init(&isp_dev->notifier);
#else
    v4l2_async_notifier_init(&isp_dev->notifier);
#endif

    isp_dev->notifier.ops = &vvcam_isp_notify_ops;

    if (dev_fwnode(isp_dev->dev) == NULL)
        return 0;

    for (pad = 0; pad < VVCAM_ISP_PAD_NR; pad++) {

        if (isp_dev->pads[pad].flags != MEDIA_PAD_FL_SINK)
            continue;

        ep = fwnode_graph_get_endpoint_by_id(dev_fwnode(dev),
                                        pad, 0, FWNODE_GRAPH_ENDPOINT_NEXT);
        if (!ep)
            continue;
        remote_ep = fwnode_graph_get_remote_endpoint(ep);
        if (!remote_ep) {
            fwnode_handle_put(ep);
            continue;
        }
        fwnode_handle_put(remote_ep);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
        asc = v4l2_async_nf_add_fwnode_remote(&isp_dev->notifier,
                                            ep, struct v4l2_async_connection);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 16, 0)
        asd = v4l2_async_nf_add_fwnode_remote(&isp_dev->notifier,
                                            ep, struct v4l2_async_subdev);
#else
        asd = v4l2_async_notifier_add_fwnode_remote_subdev(&isp_dev->notifier,
                                                ep, struct v4l2_async_subdev);
#endif

        fwnode_handle_put(ep);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
        if (IS_ERR(asc)) {
            ret = PTR_ERR(asc);
#else
        if (IS_ERR(asd)) {
            ret = PTR_ERR(asd);
#endif
            if (ret != -EEXIST) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 16, 0)
                v4l2_async_nf_cleanup(&isp_dev->notifier);
#else
                v4l2_async_notifier_cleanup(&isp_dev->notifier);
#endif
                return ret;
            }
        }
    }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
    ret = v4l2_async_nf_register(&isp_dev->notifier);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 16, 0)
    ret = v4l2_async_subdev_nf_register(&isp_dev->sd,
                        &isp_dev->notifier);
#else
    ret = v4l2_async_subdev_notifier_register(&isp_dev->sd,
                        &isp_dev->notifier);
#endif
    if (ret) {
        dev_err(isp_dev->dev, "Async notifier register error\n");
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 16, 0)
        v4l2_async_nf_cleanup(&isp_dev->notifier);
#else
        v4l2_async_notifier_cleanup(&isp_dev->notifier);
#endif
    }

    return ret;
}

static int vvcam_isp_pads_init(struct vvcam_isp_dev *isp_dev)
{
    int pad = 0;

    for (pad = 0; pad < VVCAM_ISP_PAD_NR; pad++) {
        if ((pad % VVCAM_ISP_PORT_PAD_NR) == VVCAM_ISP_PORT_PAD_SINK) {
            isp_dev->pads[pad].flags = MEDIA_PAD_FL_SINK;
        } else {
            isp_dev->pads[pad].flags = MEDIA_PAD_FL_SOURCE;
        }

        switch (pad % VVCAM_ISP_PORT_PAD_NR) {
            case VVCAM_ISP_PORT_PAD_SINK:
                break;
            case VVCAM_ISP_PORT_PAD_SOURCE_MP:
                isp_dev->pad_data[pad].num_formats = ARRAY_SIZE(vvcam_isp_mp_fmts);
                isp_dev->pad_data[pad].mbus_fmt = vvcam_isp_mp_fmts;
                break;
            case VVCAM_ISP_PORT_PAD_SOURCE_SP1:
                isp_dev->pad_data[pad].num_formats = ARRAY_SIZE(vvcam_isp_sp_fmts);
                isp_dev->pad_data[pad].mbus_fmt = vvcam_isp_sp_fmts;
                break;
            case VVCAM_ISP_PORT_PAD_SOURCE_SP2:
                isp_dev->pad_data[pad].num_formats = ARRAY_SIZE(vvcam_isp_sp_fmts);
                isp_dev->pad_data[pad].mbus_fmt = vvcam_isp_sp_fmts;
                break;
            case VVCAM_ISP_PORT_PAD_SOURCE_RAW:
                isp_dev->pad_data[pad].num_formats = ARRAY_SIZE(vvcam_isp_raw_fmts);;
                isp_dev->pad_data[pad].mbus_fmt = vvcam_isp_raw_fmts;
                break;
            default:
                break;
        }

        INIT_LIST_HEAD(&isp_dev->pad_data[pad].queue);
        mutex_init(&isp_dev->pad_data[pad].q_lock);
#ifdef DOLPHIN
        isp_dev->pad_data[pad].mmu_enabled = 0;
        isp_dev->pad_data[pad].sensor_out_state = SENSOR_OUT_DISABLE_MCM_MMU_ENABLE;
#endif
    }

    return 0;
}

static int vvcam_isp_parse_params(struct vvcam_isp_dev *isp_dev,
                        struct platform_device *pdev)
{

    int port = 0;
    isp_dev->id  = pdev->id;
    for (port = 0; port < VVCAM_ISP_PORT_NR; port++) {
        strncpy(isp_dev->sensor_info[port].sensor, VVCAM_ISP_DEFAULT_SENSOR,
            strlen(VVCAM_ISP_DEFAULT_SENSOR));
        strncpy(isp_dev->sensor_info[port].xml, VVCAM_ISP_DEFAULT_SENSOR_XML,
            strlen(VVCAM_ISP_DEFAULT_SENSOR_XML));
        isp_dev->sensor_info[port].mode = VVCAM_ISP_DEFAULT_SENSOR_MODE;
        strncpy(isp_dev->sensor_info[port].manu_json, VVCAM_ISP_DEFAULT_SENSOR_MANU_JSON,
            strlen(VVCAM_ISP_DEFAULT_SENSOR_MANU_JSON));
        strncpy(isp_dev->sensor_info[port].auto_json, VVCAM_ISP_DEFAULT_SENSOR_AUTO_JSON,
            strlen(VVCAM_ISP_DEFAULT_SENSOR_AUTO_JSON));
#ifdef DOLPHIN
        isp_dev->sensor_info[port].i2c_bus_id = VVCAM_ISP_DEFAULT_I2C_BUS_ID;
        isp_dev->sensor_info[port].mipi_id = VVCAM_ISP_DEFAULT_MIPI_ID;

        if (port == 0)
            isp_dev->sensor_info[port].csi_clk_rate = VVCAM_ISP_DEFAULT_CSI0_CLOCK_RATE;
        else
            isp_dev->sensor_info[port].csi_clk_rate = VVCAM_ISP_DEFAULT_CSI1_CLOCK_RATE;
#endif
    }
    fwnode_property_read_u32(of_fwnode_handle(pdev->dev.of_node),
            "id", &isp_dev->id);
    return 0;
}

#ifdef VVCAM_SUBDEV_PLATFORM_REGISTER
struct v4l2_subdev *g_vvcam_isp_subdev[VVCAM_ISP_DEV_MAX] = {NULL};
EXPORT_SYMBOL_GPL(g_vvcam_isp_subdev);
#endif

static int vvcam_isp_probe(struct platform_device *pdev)
{
    struct device *dev = &pdev->dev;
    struct vvcam_isp_dev *isp_dev;
    int ret;

    isp_dev = devm_kzalloc(&pdev->dev,
        sizeof(struct vvcam_isp_dev), GFP_KERNEL);
    if (!isp_dev)
        return -ENOMEM;

    mutex_init(&isp_dev->mlock);
    mutex_init(&isp_dev->ctrl_lock);
    isp_dev->dev = &pdev->dev;
    platform_set_drvdata(pdev, isp_dev);

    ret = vvcam_isp_parse_params(isp_dev, pdev);
    if (ret) {
        dev_err(&pdev->dev, "failed to parse params\n");
        return -EINVAL;
    }

    v4l2_subdev_init(&isp_dev->sd, &vvcam_isp_subdev_ops);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 0, 0)
    snprintf(isp_dev->sd.name, 64,
        "%s.%d",VVCAM_ISP_NAME, isp_dev->id);
#else
    snprintf(isp_dev->sd.name, V4L2_SUBDEV_NAME_SIZE,
        "%s.%d",VVCAM_ISP_NAME, isp_dev->id);
#endif

    isp_dev->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
    isp_dev->sd.flags |= V4L2_SUBDEV_FL_HAS_EVENTS;
    isp_dev->sd.dev =  &pdev->dev;
    isp_dev->sd.owner = THIS_MODULE;
    isp_dev->sd.internal_ops = &vvcam_isp_internal_ops;
    isp_dev->sd.entity.ops = &vvcam_isp_entity_ops;
    isp_dev->sd.entity.function = MEDIA_ENT_F_IO_V4L;
    isp_dev->sd.entity.obj_type = MEDIA_ENTITY_TYPE_V4L2_SUBDEV;
    isp_dev->sd.entity.name = isp_dev->sd.name;
    v4l2_set_subdevdata(&isp_dev->sd, isp_dev);

    vvcam_isp_pads_init(isp_dev);
    ret = media_entity_pads_init(&isp_dev->sd.entity,
                                VVCAM_ISP_PAD_NR, isp_dev->pads);
    if (ret)
       return ret;

#ifdef DOLPHIN
    isp_dev->dnr3_mmu_enable = 0;
#endif

    ret = vvcam_isp_async_notifier(isp_dev);
    if (ret)
        goto err_async_notifier;
#ifdef VVCAM_SUBDEV_PLATFORM_REGISTER
    isp_dev->sd.fwnode = &isp_dev->fwnode;
    g_vvcam_isp_subdev[isp_dev->id] = &isp_dev->sd;
#endif
    ret = v4l2_async_register_subdev(&isp_dev->sd);
    if (ret) {
        dev_err(dev, "register subdev error\n");
        goto error_regiter_subdev;
    }

    ret = vvcam_isp_procfs_register(isp_dev, &isp_dev->pde);
    if (ret) {
        dev_err(dev, "register procfs failed.\n");
        goto err_register_procfs;
    }

#ifdef DOLPHIN
    isp_dev->bcm_conf_ref_count = 0;
    isp_dev->mcm_mode = ISP_MCM_MODE_DISABLED;
    isp_dev->mcm_ref_count = 0;
    isp_dev->task = NULL;
    ret = isp_bcm_open();
    if (ret) {
        dev_err(dev, "%s bcm open failed %d\n", __func__, ret);
        goto error_bcm_open;
    }
    isp_dev->process_done = 0;
#endif

    isp_dev->event_shm.virt_addr = (void *)__get_free_pages(GFP_KERNEL, 3);
    isp_dev->event_shm.size = PAGE_SIZE * 8;
    memset(isp_dev->event_shm.virt_addr, 0, isp_dev->event_shm.size);
    isp_dev->event_shm.phy_addr = virt_to_phys(isp_dev->event_shm.virt_addr);
    mutex_init(&isp_dev->event_shm.event_lock);

    pm_runtime_enable(&pdev->dev);
    vvcam_isp_ctrl_init(isp_dev);

    dev_info(&pdev->dev, "vvcam isp driver probe success\n");

    return 0;

error_bcm_open:
    vvcam_isp_procfs_unregister(isp_dev->pde);
err_register_procfs:
    v4l2_async_unregister_subdev(&isp_dev->sd);

error_regiter_subdev:
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
    v4l2_async_nf_cleanup(&isp_dev->notifier);
    v4l2_async_nf_unregister(&isp_dev->notifier);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 16, 0)
    v4l2_async_nf_unregister(&isp_dev->notifier);
    v4l2_async_nf_cleanup(&isp_dev->notifier);
#else
    v4l2_async_notifier_unregister(&isp_dev->notifier);
    v4l2_async_notifier_cleanup(&isp_dev->notifier);
#endif
err_async_notifier:
    media_entity_cleanup(&isp_dev->sd.entity);

    return ret;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 0, 0)
static void vvcam_isp_remove(struct platform_device *pdev)
#else
static int vvcam_isp_remove(struct platform_device *pdev)
#endif
{
    struct vvcam_isp_dev *isp_dev;

    isp_dev = platform_get_drvdata(pdev);

#ifdef DOLPHIN
    if (isp_dev->task) {
        wake_up(&isp_dev->wq);
        kthread_stop(isp_dev->task);
    }
    isp_bcm_close();
#endif
    vvcam_isp_procfs_unregister(isp_dev->pde);
    v4l2_async_unregister_subdev(&isp_dev->sd);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
    v4l2_async_nf_unregister(&isp_dev->notifier);
    v4l2_async_nf_cleanup(&isp_dev->notifier);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 16, 0)
    v4l2_async_nf_unregister(&isp_dev->notifier);
    v4l2_async_nf_cleanup(&isp_dev->notifier);
#else
    v4l2_async_notifier_unregister(&isp_dev->notifier);
    v4l2_async_notifier_cleanup(&isp_dev->notifier);
#endif
    media_entity_cleanup(&isp_dev->sd.entity);
    pm_runtime_disable(&pdev->dev);
    free_pages((unsigned long)isp_dev->event_shm.virt_addr, 3);
    vvcam_isp_ctrl_destroy(isp_dev);
    dev_info(&pdev->dev, "vvcam isp driver remove\n");

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 0, 0)
    return 0;
#endif
}

static int vvcam_isp_system_suspend(struct device *dev)
{
    int ret = 0;
    ret = pm_runtime_force_suspend(dev);
    if (ret) {
        dev_err(dev, "force suspend %s failed\n", dev_name(dev));
        return ret;
    }
    return ret;
}

static int vvcam_isp_system_resume(struct device *dev)
{
    int ret = 0;
    ret = pm_runtime_force_resume(dev);
    if (ret) {
        dev_err(dev, "force resume %s failed\n", dev_name(dev));
        return ret;
    }
    return ret;
}

static int vvcam_isp_runtime_suspend(struct device *dev)
{
    return 0;
}

static int vvcam_isp_runtime_resume(struct device *dev)
{
    return 0;
}

static const struct dev_pm_ops vvcam_isp_pm_ops = {
    SET_SYSTEM_SLEEP_PM_OPS(vvcam_isp_system_suspend, vvcam_isp_system_resume)
    SET_RUNTIME_PM_OPS(vvcam_isp_runtime_suspend, vvcam_isp_runtime_resume, NULL)
};

static const struct of_device_id vvcam_isp_of_match[] = {
#ifdef VVCAM_SUBDEV_PLATFORM_REGISTER
    {.compatible = "verisilicon,nocompat",},
#else
    //TODO review the compatible string
    {.compatible = "verisilicon,isp",},
#endif
    { /* sentinel */ },
};

static struct platform_driver vvcam_isp_driver = {
    .probe  = vvcam_isp_probe,
    .remove = vvcam_isp_remove,
    .driver = {
        .name           = VVCAM_ISP_NAME,
        .owner          = THIS_MODULE,
        .of_match_table = vvcam_isp_of_match,
        .pm             = &vvcam_isp_pm_ops,
    }
};

static int __init vvcam_isp_init_module(void)
{
    int ret;
    ret = platform_driver_register(&vvcam_isp_driver);
    if (ret) {
        printk(KERN_ERR "Failed to register isp driver\n");
        return ret;
    }

#ifdef VVCAM_SUBDEV_PLATFORM_REGISTER
    ret = vvcam_isp_platform_device_register();
    if (ret) {
        platform_driver_unregister(&vvcam_isp_driver);
        printk(KERN_ERR "Failed to register vvcam isp platform devices\n");
        return ret;
    }
#endif

    return ret;
}

static void __exit vvcam_isp_exit_module(void)
{
    platform_driver_unregister(&vvcam_isp_driver);
#ifdef VVCAM_SUBDEV_PLATFORM_REGISTER
    vvcam_isp_platform_device_unregister();
#endif
}

module_init(vvcam_isp_init_module);
module_exit(vvcam_isp_exit_module);

MODULE_DESCRIPTION("Verisilicon isp v4l2 driver");
MODULE_AUTHOR("Verisilicon ISP SW Team");
MODULE_LICENSE("GPL");
