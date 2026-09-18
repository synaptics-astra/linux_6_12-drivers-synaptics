/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2021 - 2023 Synaptics Incorporated
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __MCM_SUBDEV_H__
#define __MCM_SUBDEV_H__

#include <media/videobuf2-dma-contig.h>
#include <linux/miscdevice.h>

#define MCM_SUBDEV_PAD_NR 4

struct mcm_mbus_fmt {
	uint32_t code;
};

struct mcm_pad_data {
	uint32_t sink_detected;
	struct v4l2_format v4l2_format;
	struct v4l2_mbus_framefmt format;
	uint32_t num_formats;
	struct mcm_mbus_fmt *mbus_fmt;
	struct list_head queue;
	//spinlock_t qlock;
	struct mutex q_lock;
	uint32_t stream;
	uint8_t sensor_out_state;
	bool first_frame;
	bool skip_first_mcm_frame;
	uint8_t num_alloc_buf;
};

struct mcm_event_shm {
	struct mutex event_lock;
	uint64_t phy_addr;
	void *virt_addr;
	uint32_t size;
};

struct mcm_subdev_dev {
	int  id;
	struct device *dev;
	struct mutex mlock;
	uint32_t refcnt;
	uint32_t refcnt_misc;
	struct v4l2_subdev sd;
	struct miscdevice miscdev;
	struct media_pad pads[MCM_SUBDEV_PAD_NR];
	struct v4l2_async_notifier notifier;
	struct fwnode_handle *mcm_ep[MCM_SUBDEV_PAD_NR];
	struct mcm_pad_data pad_data[MCM_SUBDEV_PAD_NR];

	struct mcm_event_shm event_shm;
	int state;
	uint32_t refcnt_stream;

	struct v4l2_format format;
};

#endif
