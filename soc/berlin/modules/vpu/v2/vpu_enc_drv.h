// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 - 2023 Synaptics Incorporated
 * Author: Hsia-Jun(Randy) Li <randy.li@synaptics.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __SYNA_VPU_ENC_DRV_H__
#define __SYNA_VPU_ENC_DRV_H__

#include <linux/videodev2.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-core.h>

#include "hantro.h"

#define SYNA_VENC_MAX_AUX_BUFS_NUM	(4U)
#define SYNA_VENC_MAX_AUX_BUFS_SHIFT	(2U)

#define SYNA_VENC_BITRATE_MIN		(10000)
#define SYNA_VENC_BITRATE_MAX		(40000000)

int syna_vepu_h1_init(struct syna_vpu_dev *vpu);
int syna_vepu_h1_finalize(struct syna_vpu_dev *vpu);
#endif
