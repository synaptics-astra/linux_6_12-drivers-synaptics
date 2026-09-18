/* SPDX-License-Identifier: MIT OR GPL-2.0-or-later */
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


#ifndef __VVCAM_ISP_DRIVER_H__
#define __VVCAM_ISP_DRIVER_H__
#include <linux/list.h>
#include <linux/videodev2.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mc.h>
#include <media/v4l2-ctrls.h>
#include <media/videobuf2-dma-contig.h>
#include "vvcam_v4l2_common.h"

#ifdef DOLPHIN
#include "ispSS_shm.h"
#endif

#define VVCAM_ISP_NAME "vvcam-isp-subdev"

#define MAX_MEMORY_DEVICE       2
#define VVCAM_ISP_WIDTH_ALIGN 16
#define VVCAM_ISP_HEIGHT_ALIGN 8
#define VVCAM_ISP_WIDTH_MIN 32
#define VVCAM_ISP_HEIGHT_MIN 16

#ifdef DOLPHIN
#define VVCAM_ISP_DEFAULT_CSI0_CLOCK_RATE 600000000
#define VVCAM_ISP_DEFAULT_CSI1_CLOCK_RATE 300000000

#define VVCAM_ISP_PORT_NR  2
#else
#define VVCAM_ISP_PORT_NR  4
#endif
enum vvcam_isp_port_pad_e {
    VVCAM_ISP_PORT_PAD_SINK = 0,
    VVCAM_ISP_PORT_PAD_SOURCE_MP,
    VVCAM_ISP_PORT_PAD_SOURCE_SP1,
    VVCAM_ISP_PORT_PAD_SOURCE_SP2,
    VVCAM_ISP_PORT_PAD_SOURCE_RAW,
    VVCAM_ISP_PORT_PAD_NR,
};

#ifdef DOLPHIN
enum vvcam_isp_mtr_state_e {
    ISP_MTR_IDLE_STATE = 0,
    ISP_MTR_RUNNING_STATE = 1
};
#endif

#define VVCAM_ISP_PAD_NR (VVCAM_ISP_PORT_NR * VVCAM_ISP_PORT_PAD_NR)

struct vvcam_isp_mbus_fmt {
    uint32_t code;
};

struct vvcam_isp_pad_data {
    uint32_t sink_detected;
    struct v4l2_mbus_framefmt format;
#ifdef DOLPHIN
    struct v4l2_format v4l2_format;
    uint32_t mmu_enabled;
    uint32_t sensor_out_state;
#endif
    struct v4l2_fract frmival_min;
    struct v4l2_fract frmival_max;
    uint32_t num_formats;
    struct vvcam_isp_mbus_fmt *mbus_fmt;
    struct list_head queue;
    //spinlock_t qlock;
    struct mutex q_lock;
    uint32_t stream;
#ifdef DOLPHIN
    bool skip_first_frame;
#endif
};

struct vvcam_isp_event_shm {
    struct mutex event_lock;
    uint64_t phy_addr;
    void *virt_addr;
    uint32_t size;
};

struct vvcam_isp_sensor_info {
    char sensor[32];
    uint8_t mode;
    char xml[64];
    char manu_json[128];
    char auto_json[128];
#ifdef DOLPHIN
   uint8_t i2c_bus_id;
   uint8_t mipi_id;
   uint32_t csi_clk_rate;
#endif
};

#ifdef DOLPHIN
struct vvcam_isp_3dnr_buffer {
    SHM_HANDLE shm_handle;
    uint32_t phy_address;
};
#endif

struct vvcam_isp_dev {
    phys_addr_t paddr;
    uint32_t regs_size;
    void __iomem *base;
    void __iomem *reset;
    int  id;
    int fe_irq;
    int isp_irq;
    int mi_irq;
    struct device *dev;
    struct mutex mlock;
    uint32_t refcnt;
    struct v4l2_subdev sd;
    struct media_pad pads[VVCAM_ISP_PAD_NR];
    struct v4l2_async_notifier notifier;
#ifdef VVCAM_SUBDEV_PLATFORM_REGISTER
    struct fwnode_handle fwnode;
#endif
    struct vvcam_isp_pad_data pad_data[VVCAM_ISP_PAD_NR];

    struct vvcam_isp_event_shm event_shm;
    struct v4l2_ctrl_handler ctrl_handler;
    struct mutex ctrl_lock;
    uint32_t ctrl_pad;

    unsigned long pde;
    struct vvcam_isp_sensor_info sensor_info[VVCAM_ISP_PORT_NR];

#ifdef DOLPHIN
    uint32_t mcm_mode;
    uint8_t bcm_conf_ref_count;
    uint8_t mcm_ref_count;
    enum vvcam_isp_mtr_state_e mtr_state;
    struct device *alloc_dev[MAX_MEMORY_DEVICE];
    struct work_struct  isp_work;
    struct task_struct *task;
    wait_queue_head_t wq;
    uint8_t process_done;
    struct vvcam_isp_3dnr_buffer dnr3_buf[VVCAM_ISP_PORT_NR];
    /* 3DNR Control variables. 'dnr3_mmu_ctrl_tmp' receives the control from v4l2.
     * 'dnr3_mmu_enable' is the actual control variable, which gets updated after checking
     * the necessary conditions in stream_on.
     */
    uint8_t dnr3_mmu_ctrl_tmp; // Temp variable to receive control.
    uint8_t dnr3_mmu_enable; // Actual control variable.
#endif
};

#ifdef DOLPHIN
uint32_t vvcam_isp_get_mtr_path(uint32_t pad_index);
int vvcam_isp_subscribe_event(struct v4l2_subdev *sd,
            struct v4l2_fh *fh,
            struct v4l2_event_subscription *sub);
#endif
#endif
