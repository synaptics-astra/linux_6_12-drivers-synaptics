// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 Synaptics Incorporated
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <linux/delay.h>
#include "mcm_event.h"
#include "mcm_subdev.h"
#include "vvcam_v4l2_common.h"

static bool mcm_event_subscribed(struct v4l2_subdev *sd,
		uint32_t type, uint32_t id)
{
	struct v4l2_fh *fh;
	unsigned long flags;
	struct v4l2_subscribed_event *sev;
	bool subscribed = false;

	spin_lock_irqsave(&sd->devnode->fh_lock, flags);

	list_for_each_entry(fh, &sd->devnode->fh_list, list) {
		list_for_each_entry(sev, &fh->subscribed, list) {
			if (sev->type == type && sev->id == id) {
				subscribed = true;
				break;
			}
		}
		if (subscribed)
			break;
	}

	spin_unlock_irqrestore(&sd->devnode->fh_lock, flags);

	return subscribed;
}

static int mcm_post_event(struct v4l2_subdev *sd, struct mcm_event_pkg *event_pkg)
{
	struct v4l2_event event;
	int timeout_ms = 200000;
	int i = 0;

	memset(&event, 0, sizeof(event));

	event.type   = MCM_DEAMON_EVENT;
	event.id     = event_pkg->head.eid;
	memcpy(event.u.data, &event_pkg->head, sizeof(event_pkg->head));

	if (!mcm_event_subscribed(sd, event.type, event.id)) {
		dev_err(sd->dev, "post event %d not subscribed\n", event.id);
		return -EINVAL;
	}

	v4l2_event_queue(sd->devnode, &event);

	for (i = 0; i < timeout_ms; i++) {
		if (event_pkg->ack) {
			break;
		}
		usleep_range(5, 10);
	}

	if (event_pkg->ack == 0) {
		dev_err(sd->dev, "post event %d time out\n", event.id);
		return -EIO;
	}

	if (event_pkg->result) {
		dev_err(sd->dev, "post event %d return error %d\n", event.id, event_pkg->result);
		return -EINVAL;
	}

	return 0;
}

int mcm_qbuf_event(struct mcm_subdev_dev *mcm_dev, int pad,
		struct vvcam_vb2_buffer *buf)
{
	struct mcm_event_pkg *event_pkg = mcm_dev->event_shm.virt_addr;
	struct mcm_buf ubuf;
	int ret;

	ubuf.pad = pad;
	ubuf.index = buf->sequence;
	ubuf.plane.dma_addr = buf->planes[Y_PLANE].dma_addr;
	ubuf.plane.size = buf->planes[Y_PLANE].size;

	mutex_lock(&mcm_dev->event_shm.event_lock);
	event_pkg->head.pad = pad;
	event_pkg->head.dev = mcm_dev->id;
	event_pkg->head.eid = MCM_EVENT_QBUF;
	event_pkg->head.shm_addr = mcm_dev->event_shm.phy_addr;
	event_pkg->head.shm_size = mcm_dev->event_shm.size;
	event_pkg->head.data_size = sizeof(struct mcm_buf);
	event_pkg->ack = 0;
	event_pkg->result = 0;
	memcpy(event_pkg->data, &ubuf, sizeof(struct mcm_buf));

	ret = mcm_post_event(&mcm_dev->sd, event_pkg);

	mutex_unlock(&mcm_dev->event_shm.event_lock);

	return ret;
}
