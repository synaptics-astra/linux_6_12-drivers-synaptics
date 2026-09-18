// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 Synaptics Incorporated
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/version.h>
#include <linux/platform_device.h>
#include <linux/of_reserved_mem.h>
#include <linux/of_graph.h>
#include <linux/vmalloc.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/pm_runtime.h>
#include <linux/miscdevice.h>
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
#include "mcm_subdev.h"
#include "mcm_event.h"
#include "ispSS_shm.h"
#include "ispSS_reg.h"
#include "ispSS_bcmbuf.h"
#include "isp_bcm.h"
#include "mtr_isp_wrap.h"
#include "isp_dma_heap.h"

#define MCM_NAME "mcm-subdev"

/* keep track of kind of buffers that will be used for MCM */
static int mcm_subdev_mcm_buf_state(struct v4l2_subdev *sd, void *arg)
{
	struct mcm_buf_state *m_state = (struct mcm_buf_state *)arg;
	struct mcm_subdev_dev *mcm_dev = v4l2_get_subdevdata(sd);
	struct mcm_pad_data *cur_pad;

	cur_pad = &mcm_dev->pad_data[m_state->pad];
	cur_pad->sensor_out_state = m_state->sensor_out_state;

	return 0;
}


/* Called from user space. If the raw_frame_dump is disabled, memory is
 * allocated with ispSS_SHM_Allocate, the allocated buffer is added to the
 * pad's queue. If the raw_frame_dump is enabled, return the buffer from
 * the queue. Return phy_addr to user space
 */
static int mcm_subdev_buf_alloc(struct v4l2_subdev *sd, void *arg)
{
	struct mcm_buf ubuf;
	struct mcm_pad_data *cur_pad;
	struct vvcam_vb2_buffer *pos;
	int ret = 0;
	bool found = false;
	struct mcm_subdev_dev *mcm_dev;
	int source_pad_index = 0;
	void *phy_addr = NULL;
	//void *virt_addr = NULL;
	struct vvcam_vb2_buffer *new_buf;
	uint32_t size;

	memcpy(&ubuf, arg, sizeof(struct mcm_buf));
	mcm_dev = v4l2_get_subdevdata(sd);
	source_pad_index = (ubuf.pad * MCM_CHN_MAX) + 1;

	cur_pad = &mcm_dev->pad_data[source_pad_index];
	cur_pad->v4l2_format.fmt.pix_mp.width = ubuf.plane.width;
	cur_pad->v4l2_format.fmt.pix_mp.height = ubuf.plane.height;
	cur_pad->v4l2_format.fmt.pix_mp.pixelformat = ubuf.plane.fourcc;

	if (cur_pad->sensor_out_state == SENSOR_OUT_ENABLE_MCM_MMU_ENABLE ||
			cur_pad->sensor_out_state == SENSOR_OUT_ENABLE_MCM_MMU_DISABLE) {
		mutex_lock(&cur_pad->q_lock);
		if (list_empty(&cur_pad->queue)) {
			mutex_unlock(&cur_pad->q_lock);
			dev_err(mcm_dev->dev, "%s queue empty, invalid state\n", __func__);
			return -EINVAL;
		}
		list_for_each_entry(pos, &cur_pad->queue, list) {
			if (pos->sequence == ubuf.index) {
				found = true;
				break;
			}
		}
		cur_pad->num_alloc_buf++;
		mutex_unlock(&cur_pad->q_lock);

		if (found) {
			ubuf.plane.dma_addr = pos->planes[Y_PLANE].dma_addr;
			ubuf.plane.size = pos->planes[Y_PLANE].size;
			dev_dbg(mcm_dev->dev, "index: %d dma_addr: 0x%x size: %d\n", ubuf.index,
					pos->planes[Y_PLANE].dma_addr, pos->planes[Y_PLANE].size);
		} else {
			dev_err(mcm_dev->dev, "%d buffer not found, invalid state", ubuf.index);
			ret = -EINVAL;
		}
	} else {
		size = ubuf.plane.size;

		new_buf = kzalloc(sizeof(struct vvcam_vb2_buffer), GFP_KERNEL);

		if (!new_buf)
			return -ENOMEM;

		if (cur_pad->sensor_out_state == SENSOR_OUT_DISABLE_MCM_MMU_DISABLE) {
			if (ispSS_SHM_Allocate(SHM_NONSECURE, size, 32, &new_buf->shm_handle,
					SHM_NONSECURE_CONTIG) != SUCCESS) {
				dev_err(mcm_dev->dev, "Failed to allocate memory\n");
				kfree(new_buf);
				return -ENOMEM;
			}
			ispSS_SHM_GetPhysicalAddress(new_buf->shm_handle, 0, &phy_addr);
		} else {
			if (ispSS_SHM_Allocate(SHM_NONSECURE, size, 32, &new_buf->shm_handle,
					SHM_NONSECURE_NON_CONTIG) != SUCCESS) {
				dev_err(mcm_dev->dev, "Failed to allocate memory\n");
				kfree(new_buf);
				return -ENOMEM;
			}
			ispSS_SHM_GetPageTableAddress(new_buf->shm_handle, &phy_addr);
		}

		ubuf.plane.dma_addr = (uint32_t)(uintptr_t)phy_addr;
		ubuf.plane.size = size;

		new_buf->sequence = ubuf.index;
		new_buf->planes[Y_PLANE].dma_addr = ubuf.plane.dma_addr;
		new_buf->planes[Y_PLANE].size = size;
		dev_dbg(mcm_dev->dev, "%s source_pad_index: %d index: %d addr: 0x%x", __func__,
				source_pad_index, ubuf.index, ubuf.plane.dma_addr);

		mutex_lock(&cur_pad->q_lock);
		list_add_tail(&new_buf->list, &cur_pad->queue);
		cur_pad->num_alloc_buf++;
		mutex_unlock(&cur_pad->q_lock);
	}

	memcpy(arg, &ubuf, sizeof(struct mcm_buf));
	return  ret;
}

/* Called from the user space. Finds the buffer that matches the index passed,
 * and calls ispSS_SHM_Release on that buffer. For the Raw frame dump scenario,
 * GST will take care of releasing the buffers.
 */
static int mcm_subdev_buf_free(struct v4l2_subdev *sd, void *arg)
{
	struct mcm_subdev_dev *mcm_dev = v4l2_get_subdevdata(sd);
	struct mcm_pad_data *cur_pad;
	struct vvcam_vb2_buffer *pos = NULL, *next = NULL;
	struct mcm_buf ubuf;
	int source_pad_index;
	bool found = false;
	int ret = 0;

	memcpy(&ubuf, arg, sizeof(struct mcm_buf));
	source_pad_index = (ubuf.pad * MCM_CHN_MAX) + 1;
	cur_pad = &mcm_dev->pad_data[source_pad_index];

	mutex_lock(&cur_pad->q_lock);
	if (cur_pad->sensor_out_state == SENSOR_OUT_DISABLE_MCM_MMU_DISABLE ||
			cur_pad->sensor_out_state == SENSOR_OUT_DISABLE_MCM_MMU_ENABLE) {

		if (list_empty(&cur_pad->queue)) {
			dev_err(mcm_dev->dev, "%s queue empty, invalid state\n", __func__);
			ret = -EINVAL;
			goto exit;
		}
		list_for_each_entry_safe(pos, next, &cur_pad->queue, list) {
			if (pos && (pos->sequence == ubuf.index)) {
				ispSS_SHM_Release(pos->shm_handle);
				list_del(&pos->list);
				kfree(pos);
				pos = NULL;
				found = true;
				break;
			}
		}

		if (!found) {
			dev_err(mcm_dev->dev, "buffer not found, invalid state\n");
			ret = -EINVAL;
			goto exit;
		}
	}
	cur_pad->num_alloc_buf--;

	/* Mcm buf free is the last ioctl called during the pipeline close.
	 * Therefore, bcm clock disable logic has been put here, to avoid any issues
	 * arising out of abrupt clock disable. Ideally this should be part
	 * of stream off logic
	 */
	mutex_lock(&mcm_dev->mlock);
	if (cur_pad->num_alloc_buf == 0 && mcm_dev->refcnt_stream == 0)
		isp_bcm_disable_clock();

	mutex_unlock(&mcm_dev->mlock);

exit:
	mutex_unlock(&cur_pad->q_lock);
	return ret;
}

/* Invoked only in the case of raw frame dump, by the isp subdev.
 * Add the received buffer to the pad's queue. Trigger V4L2 event if the STREAM ON is done
 */
static int mcm_subdev_buf_queue(struct v4l2_subdev *sd, void *arg)
{
	struct vvcam_pad_buf *pad_buf = (struct vvcam_pad_buf *)arg;
	struct mcm_subdev_dev *mcm_dev = v4l2_get_subdevdata(sd);
	struct mcm_pad_data *cur_pad;

	cur_pad = &mcm_dev->pad_data[pad_buf->pad];

	mutex_lock(&cur_pad->q_lock);
	list_add_tail(&pad_buf->buf->list, &cur_pad->queue);
	mutex_unlock(&cur_pad->q_lock);

	if (cur_pad->stream)
		mcm_qbuf_event(mcm_dev, pad_buf->pad, pad_buf->buf);

	return 0;
}

static int mcm_subdev_recycle_buffer(struct mcm_subdev_dev *mcm_dev,
		struct mcm_pad_data *cur_pad, struct vvcam_vb2_buffer *buf,
		int source_pad_index, unsigned int index)
{
	int qbuf_ret = 0;

	mutex_lock(&cur_pad->q_lock);
	list_add_tail(&buf->list, &cur_pad->queue);
	mutex_unlock(&cur_pad->q_lock);

	qbuf_ret = mcm_qbuf_event(mcm_dev, source_pad_index, buf);
	if (qbuf_ret) {
		mutex_lock(&cur_pad->q_lock);
		list_del_init(&buf->list);
		mutex_unlock(&cur_pad->q_lock);
		pr_err("%s: pad %u buffer %u recycle failed, ret %d\n",
			__func__, source_pad_index, index, qbuf_ret);
	}

	return qbuf_ret;
}

/* Called from the user space. Finds the buffer that matches the index passed,
 * and calls vb2_buffer_done on that buffer if the raw_frame_dump is enabled
 */
static int mcm_subdev_buf_done(struct v4l2_subdev *sd, void *arg)
{
	struct mcm_subdev_dev *mcm_dev = v4l2_get_subdevdata(sd);
	struct mcm_pad_data *cur_pad;
	struct vvcam_vb2_buffer *pos = NULL, *next = NULL;
	struct vvcam_vb2_buffer *buf = NULL;
	struct mcm_buf ubuf;
	int source_pad_index;
	int ret = 0;
	bool skip_first_mcm_frame = false;

	memcpy(&ubuf, arg, sizeof(struct mcm_buf));
	source_pad_index = (ubuf.pad * MCM_CHN_MAX) + 1;
	cur_pad = &mcm_dev->pad_data[source_pad_index];

	mutex_lock(&cur_pad->q_lock);

	if (list_empty(&cur_pad->queue)) {
		mutex_unlock(&cur_pad->q_lock);
		dev_err(mcm_dev->dev, "%s queue empty, invalid state\n", __func__);
		return -EINVAL;
	}

	list_for_each_entry_safe(pos, next, &cur_pad->queue, list) {
		if (pos && (pos->sequence == ubuf.index)) {
			buf = pos;
			list_del(&pos->list);
			if (cur_pad->skip_first_mcm_frame) {
				skip_first_mcm_frame = true;
				cur_pad->skip_first_mcm_frame = false;
			}
			break;
		}
	}
	mutex_unlock(&cur_pad->q_lock);

	if (buf) {
		if (cur_pad->sensor_out_state == SENSOR_OUT_ENABLE_MCM_MMU_ENABLE ||
				cur_pad->sensor_out_state == SENSOR_OUT_ENABLE_MCM_MMU_DISABLE) {
			if (buf->vb.vb2_buf.state == VB2_BUF_STATE_ACTIVE) {
				buf->vb.vb2_buf.timestamp = ktime_get();
				if (skip_first_mcm_frame) {
					ret = mcm_subdev_recycle_buffer(mcm_dev, cur_pad, buf,
							source_pad_index, ubuf.index);
					if (ret)
						vb2_buffer_done(&buf->vb.vb2_buf,
								VB2_BUF_STATE_ERROR);
				} else {
					vb2_buffer_done(&buf->vb.vb2_buf, VB2_BUF_STATE_DONE);
				}
			}
		}

	} else {
		dev_err(mcm_dev->dev, "buffer not found, invalid state\n");
		return -EINVAL;
	}

	return 0;
}

/* Called from isp subdev. Store if the STREAM ON is done */
static int mcm_s_stream(struct v4l2_subdev *sd, void *arg)
{
	struct mcm_stream_status *mcm_stream = (struct mcm_stream_status *)arg;
	struct mcm_subdev_dev *mcm_dev = v4l2_get_subdevdata(sd);
	struct mcm_pad_data *cur_pad;
	int32_t wrpath = -1, rdpath = -1;

	mutex_lock(&mcm_dev->mlock);
	cur_pad = &mcm_dev->pad_data[mcm_stream->pad];
	cur_pad->stream += mcm_stream->status;
	mcm_dev->refcnt_stream += mcm_stream->status;

	/*Stream on ioctl will be called only for pad1 and pad3, so the below logic
	 * should suffice
	 */
	wrpath = (mcm_stream->pad / MCM_CHN_MAX) ? ISPSS_MTR_PATH_MCM1_WR : ISPSS_MTR_PATH_MCM0_WR;
	rdpath = (mcm_stream->pad / MCM_CHN_MAX) ? ISPSS_MTR_PATH_MCM1_RD : ISPSS_MTR_PATH_MCM0_RD;

	dev_dbg(mcm_dev->dev, "%s stream: %d\n", __func__, cur_pad->stream);
	/* call the exit/deconfigure functions only for the last stream*/
	if (cur_pad->stream == 0) {
		isp_bcm_deconfigure(wrpath);
		ISPSS_MTR_Exit(wrpath);
		ISPSS_MTR_Exit(rdpath);
		cur_pad->first_frame = true;
		cur_pad->skip_first_mcm_frame = false;
		/* Gst/test application itself will free the raw dump buffers. We just need to
		 * clear the queue here
		 */
		if (cur_pad->sensor_out_state == SENSOR_OUT_ENABLE_MCM_MMU_ENABLE ||
				cur_pad->sensor_out_state == SENSOR_OUT_ENABLE_MCM_MMU_DISABLE)
			INIT_LIST_HEAD(&cur_pad->queue);
	} else if (cur_pad->stream == 1 && mcm_stream->status == 1) {
		cur_pad->skip_first_mcm_frame = true;
		/* Only for first stream on, both the above will be 1 */
		if (cur_pad->sensor_out_state == SENSOR_OUT_DISABLE_MCM_MMU_DISABLE ||
				cur_pad->sensor_out_state == SENSOR_OUT_DISABLE_MCM_MMU_ENABLE)
			INIT_LIST_HEAD(&cur_pad->queue);
		if (mcm_dev->refcnt_stream == 1)
			isp_bcm_enable_clock();
		isp_bcm_configure(wrpath);
	}
	mutex_unlock(&mcm_dev->mlock);

	return 0;
}

/* Called from user space, Does the MTR configuration for the buffer passed.
 * Update the is_pushed_queue to reflect that the buffer has been handled
 */
static int mcm_subdev_mtr_mcm_wr_update(struct v4l2_subdev *sd, void *arg)
{
	int ret = 0;
	struct mtr_mcm_update mcm_update;
	struct mcm_subdev_dev *mcm_dev = v4l2_get_subdevdata(sd);
	struct mcm_pad_data *cur_pad;
	struct vvcam_vb2_buffer *pos;
	void *bcm_buf = NULL;
	int source_pad_index;

	memcpy(&mcm_update, arg, sizeof(struct mtr_mcm_update));

	source_pad_index = (mcm_update.port * MCM_CHN_MAX) + 1;
	cur_pad = &mcm_dev->pad_data[source_pad_index];

	if (cur_pad->sensor_out_state == SENSOR_OUT_ENABLE_MCM_MMU_ENABLE ||
			cur_pad->sensor_out_state == SENSOR_OUT_DISABLE_MCM_MMU_ENABLE) {
		if (!cur_pad->stream) {
			dev_dbg(mcm_dev->dev, "stream has stopped!! not handling this!!\n");
			return 0;
		}

		mutex_lock(&cur_pad->q_lock);

		list_for_each_entry(pos, &cur_pad->queue, list) {
			if (pos->sequence == mcm_update.index) {
				bcm_buf = isp_bcm_get_next_bcmbuf();

				ret = ISPSS_MTR_ConfigureMtr(&cur_pad->v4l2_format,
						pos->planes[Y_PLANE].dma_addr, 0,
						ISPSS_MTR_PATH_MCM0_WR + mcm_update.port,
						bcm_buf);
				if (ret) {
					pr_err("%s: Configure MTR failed\n", __func__);
					break;
				}

				/* MTR MCM WR first frame has to direct write */
				ret = isp_bcm_commit(bcm_buf, cur_pad->first_frame,
						ISPSS_MTR_PATH_MCM0_WR + mcm_update.port,
						NOBLOCK);
				if (ret)
					pr_err("%s: BCM commit failed\n", __func__);
				else
					pos->is_pushed_queue = 1;

				if (cur_pad->first_frame)
					cur_pad->first_frame =  false;

				break;
			}
		}

		mutex_unlock(&cur_pad->q_lock);
	}

	return ret;
}

static int mcm_write_bcm_array(struct v4l2_subdev *sd, void *arg)
{
	struct mcm_bcm_array bcm_array;
	uint64_t tmp_addr;
	int ret = 0;
	struct mcm_subdev_dev *mcm_dev = v4l2_get_subdevdata(sd);

	memcpy(&bcm_array, arg, sizeof(struct mcm_bcm_array));
	mutex_lock(&mcm_dev->mlock);
	if (mcm_dev->refcnt_stream) {
		tmp_addr = bcm_array.addr;

		ret = ISPSS_BCMDHUB_Raw_To_Commit((uint32_t *)tmp_addr, bcm_array.size);
		if (ret)
			dev_err(sd->dev, "BCM commit failed %d\n", ret);
	} else {
		dev_err(sd->dev, "%s not done %d\n", __func__, ret);
		ret = -EINVAL;
	}
	mutex_unlock(&mcm_dev->mlock);

	return ret;
}

static int mcm_subdev_mtr_isp_update(struct v4l2_subdev *sd, void *arg)
{
	struct mtr_mcm_update mtr_update;
	struct media_pad *pad;
	struct v4l2_subdev *subdev;
	struct mcm_pad_data *cur_pad;
	struct vvcam_pad_mtr_update vvcam_mtr_update;
	struct vvcam_vb2_buffer *pos;
	int source_pad_index;
	int ret = 0;
	struct mcm_subdev_dev *mcm_dev = v4l2_get_subdevdata(sd);

	memcpy(&mtr_update, arg, sizeof(struct mtr_mcm_update));
	source_pad_index = (mtr_update.port * MCM_CHN_MAX) + 1;
	cur_pad = &mcm_dev->pad_data[source_pad_index];

	mutex_lock(&mcm_dev->mlock);
	if (!mcm_dev->refcnt_stream) {
		dev_err(mcm_dev->dev, "%s stream has stopped!! not handling this!!\n", __func__);
		mutex_unlock(&mcm_dev->mlock);
		return -EINVAL;
	}
	mutex_unlock(&mcm_dev->mlock);

	if (cur_pad->sensor_out_state == SENSOR_OUT_DISABLE_MCM_MMU_ENABLE) {
		mutex_lock(&cur_pad->q_lock);
		list_for_each_entry(pos, &cur_pad->queue, list) {
			if (pos->sequence == mtr_update.index) {

				/* TODO Future improvement: use BCM immediate write instead
				 * of direct reg write
				 */
				ret = ISPSS_MTR_ConfigureMtr(&cur_pad->v4l2_format,
						pos->planes[Y_PLANE].dma_addr, 0,
						ISPSS_MTR_PATH_MCM0_RD + mtr_update.port,
						NULL);
				if (ret)
					pr_err("%s: Configure MTR failed\n", __func__);

				break;
			}
		}
		mutex_unlock(&cur_pad->q_lock);
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 0, 0)
	pad = media_pad_remote_pad_first(&mcm_dev->pads[source_pad_index]);
#else
	pad = media_entity_remote_pad(&mcm_dev->pads[source_pad_index]);
#endif

	if (!pad)
		return -EINVAL;

	if (is_media_entity_v4l2_subdev(pad->entity)) {
		vvcam_mtr_update.port = mtr_update.port;

		subdev = media_entity_to_v4l2_subdev(pad->entity);
		ret = v4l2_subdev_call(subdev, core, ioctl,
				VVCAM_PAD_MTR_UPDATE, &vvcam_mtr_update);
		if (ret)
			return ret;
	}

	return 0;
}

static int mcm_subdev_begin_cpu_access(struct v4l2_subdev *sd, void *arg)
{
	struct mtr_mcm_update mcm_update;
	struct mcm_pad_data *cur_pad;
	struct vvcam_vb2_buffer *pos = NULL;
	int source_pad_index;
	int ret = 0;
	struct mcm_subdev_dev *mcm_dev = v4l2_get_subdevdata(sd);

	memcpy(&mcm_update, arg, sizeof(struct mtr_mcm_update));
	source_pad_index = (mcm_update.port * MCM_CHN_MAX) + 1;
	cur_pad = &mcm_dev->pad_data[source_pad_index];

	list_for_each_entry(pos, &cur_pad->queue, list) {
		if (pos->sequence == mcm_update.index) {
			if (cur_pad->sensor_out_state == SENSOR_OUT_ENABLE_MCM_MMU_ENABLE ||
				cur_pad->sensor_out_state == SENSOR_OUT_ENABLE_MCM_MMU_DISABLE)
				vb2_isp_dma_heap_finish(vb2_plane_cookie(&pos->vb.vb2_buf, 0));
			else
				ispSS_SHM_InvalidCache(pos->shm_handle);
			break;
		}
	}
	if (!pos)
		return -EINVAL;

	return ret;
}

static int mcm_subdev_end_cpu_access(struct v4l2_subdev *sd, void *arg)
{
	struct mtr_mcm_update mcm_update;
	struct mcm_pad_data *cur_pad;
	struct vvcam_vb2_buffer *pos = NULL;
	int source_pad_index;
	int ret = 0;
	struct mcm_subdev_dev *mcm_dev = v4l2_get_subdevdata(sd);

	memcpy(&mcm_update, arg, sizeof(struct mtr_mcm_update));
	source_pad_index = (mcm_update.port * MCM_CHN_MAX) + 1;
	cur_pad = &mcm_dev->pad_data[source_pad_index];

	list_for_each_entry(pos, &cur_pad->queue, list) {
		if (pos->sequence == mcm_update.index) {
			if (cur_pad->sensor_out_state == SENSOR_OUT_ENABLE_MCM_MMU_ENABLE ||
				cur_pad->sensor_out_state == SENSOR_OUT_ENABLE_MCM_MMU_DISABLE)
				vb2_isp_dma_heap_prepare(vb2_plane_cookie(&pos->vb.vb2_buf, 0));
			else
				ispSS_SHM_CleanCache(pos->shm_handle, 0, 0);
			break;
		}
	}
	if (!pos)
		return -EINVAL;

	return ret;
}

static long mcm_subdev_priv_ioctl(struct v4l2_subdev *sd,
		unsigned int cmd, void *arg)
{
	int ret = -EINVAL;

	switch (cmd) {
	case MCM_IOC_BUF_ALLOCATE:
		ret = mcm_subdev_buf_alloc(sd, arg);
		break;
	case MCM_IOC_BUF_FREE:
		ret = mcm_subdev_buf_free(sd, arg);
		break;
	case MCM_IOC_BUF_DONE:
		ret = mcm_subdev_buf_done(sd, arg);
		break;
	case MCM_IOC_MTR_MCM_WR_UPDATE:
		ret = mcm_subdev_mtr_mcm_wr_update(sd, arg);
		break;
	case MCM_PAD_MCM_BUF_STATE:
		ret = mcm_subdev_mcm_buf_state(sd, arg);
		break;
	case MCM_PAD_BUF_QUEUE:
		ret = mcm_subdev_buf_queue(sd, arg);
		break;
	case MCM_PAD_S_STREAM:
		ret = mcm_s_stream(sd, arg);
		break;
	case MCM_IOC_WRITE_BCM_ARRAY:
		ret = mcm_write_bcm_array(sd, arg);
		break;
	case MCM_IOC_MTR_ISP_UPDATE:
		ret = mcm_subdev_mtr_isp_update(sd, arg);
		break;
		//TODO Move the begin, end CPU access functionality to misc device ioctl
	case MCM_IOC_BEGIN_CPU_ACCESS:
		ret = mcm_subdev_begin_cpu_access(sd, arg);
		break;
	case MCM_IOC_END_CPU_ACCESS:
		ret = mcm_subdev_end_cpu_access(sd, arg);
		break;

	default:
		break;
	}

	return ret;
}

static int mcm_subdev_subscribe_event(struct v4l2_subdev *sd,
		struct v4l2_fh *fh,
		struct v4l2_event_subscription *sub)
{
	switch (sub->type) {
	case MCM_DEAMON_EVENT:
		return v4l2_event_subscribe(fh, sub, 2, NULL);
	default:
		return -EINVAL;
	}
}

static const struct v4l2_subdev_core_ops mcm_subdev_core_ops = {
	.ioctl             = mcm_subdev_priv_ioctl,
	.subscribe_event   = mcm_subdev_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static const struct v4l2_subdev_video_ops mcm_subdev_video_ops = {
};

const struct v4l2_subdev_ops mcm_subdev_subdev_ops = {
	.core  = &mcm_subdev_core_ops,
	.video = &mcm_subdev_video_ops,
	//.pad   = &mcm_subdev_pad_ops,
};


static int mcm_subdev_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct mcm_subdev_dev *mcm_dev = v4l2_get_subdevdata(sd);

	mutex_lock(&mcm_dev->mlock);

	mcm_dev->refcnt++;

	mutex_unlock(&mcm_dev->mlock);
	return 0;
}

static int mcm_subdev_close(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct mcm_subdev_dev *mcm_dev = v4l2_get_subdevdata(sd);
	int pad = 0;

	mutex_lock(&mcm_dev->mlock);

	mcm_dev->refcnt--;
	for (pad = 0; pad < MCM_SUBDEV_PAD_NR; pad++) {
		struct mcm_pad_data *cur_pad = &mcm_dev->pad_data[pad];
		struct vvcam_vb2_buffer *pos, *n;

		if (cur_pad->stream) {
			dev_warn(mcm_dev->dev,
					"pad %d is streaming, no need of releasing it\n", pad);
			continue;
		}

		mutex_lock(&cur_pad->q_lock);
		if (list_empty(&cur_pad->queue)) {
			mutex_unlock(&cur_pad->q_lock);
			continue;
		}

		list_for_each_entry_safe(pos, n, &cur_pad->queue, list) {
			if (pos && (pos->shm_handle)) {
				ispSS_SHM_Release(pos->shm_handle);
				list_del(&pos->list);
				kfree(pos);
				pos = NULL;
			}
		}
		mutex_unlock(&cur_pad->q_lock);
	}
	mutex_unlock(&mcm_dev->mlock);

	return 0;
}


static const struct v4l2_subdev_internal_ops mcm_subdev_internal_ops = {
	.open  = mcm_subdev_open,
	.close = mcm_subdev_close,
};

static int mcm_subdev_link_setup(struct media_entity *entity,
		const struct media_pad *local,
		const struct media_pad *remote, u32 flags)
{
	return 0;
}

static const struct media_entity_operations mcm_subdev_entity_ops = {
	.link_setup     = mcm_subdev_link_setup,
	.link_validate  = v4l2_subdev_link_validate,
	.get_fwnode_pad = v4l2_subdev_get_fwnode_pad_1_to_1,

};

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
static int mcm_subdev_notifier_bound(struct v4l2_async_notifier *notifier,
		struct v4l2_subdev *sd,
		struct v4l2_async_connection *asc)
#else
static int mcm_subdev_notifier_bound(struct v4l2_async_notifier *notifier,
		struct v4l2_subdev *sd,
		struct v4l2_async_subdev *asd)
#endif
{
	return 0;
}

#if KERNEL_VERSION(6, 6, 0) <= LINUX_VERSION_CODE
static void mcm_subdev_notifier_unbound(struct v4l2_async_notifier *notifier,
		struct v4l2_subdev *sd,
		struct v4l2_async_connection *asc)
{
}
#else
static void mcm_subdev_notifier_unbound(struct v4l2_async_notifier *notifier,
		struct v4l2_subdev *sd,
		struct v4l2_async_subdev *asd)
{
}
#endif

static const struct v4l2_async_notifier_operations mcm_subdev_notify_ops = {
	.bound    = mcm_subdev_notifier_bound,
	.unbind   = mcm_subdev_notifier_unbound,
};

static int mcm_subdev_async_notifier(struct mcm_subdev_dev *mcm_dev)
{
	struct fwnode_handle *ep;
	struct fwnode_handle *remote_ep;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
	struct v4l2_async_connection *asc;
#else
	struct v4l2_async_subdev *asd;
#endif
	struct device *dev = mcm_dev->dev;
	int ret = 0;
	int pad = 0;
	int sink_pad = 0;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
	v4l2_async_subdev_nf_init(&mcm_dev->notifier, &mcm_dev->sd);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 16, 0)
	v4l2_async_nf_init(&mcm_dev->notifier);
#else
	v4l2_async_notifier_init(&mcm_dev->notifier);
#endif

	mcm_dev->notifier.ops = &mcm_subdev_notify_ops;

	if (dev_fwnode(mcm_dev->dev) == NULL)
		return 0;

	for (pad = 0; pad < MCM_SUBDEV_PAD_NR; pad++) {

		if (mcm_dev->pads[pad].flags != MEDIA_PAD_FL_SINK)
			continue;

		ep = fwnode_graph_get_endpoint_by_id(dev_fwnode(dev),
				pad, 0, FWNODE_GRAPH_ENDPOINT_NEXT);
		if (!ep)
			continue;

		mcm_dev->mcm_ep[sink_pad++] = ep;
		dev_info(mcm_dev->dev, "%s: saving ep\n", __func__);

		remote_ep = fwnode_graph_get_remote_endpoint(ep);
		if (!remote_ep) {
			fwnode_handle_put(ep);
			continue;
		}
		fwnode_handle_put(remote_ep);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
		asc = v4l2_async_nf_add_fwnode_remote(&mcm_dev->notifier,
				ep, struct v4l2_async_connection);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 16, 0)
		asd = v4l2_async_nf_add_fwnode_remote(&mcm_dev->notifier,
				ep, struct v4l2_async_subdev);
#else
		asd = v4l2_async_notifier_add_fwnode_remote_subdev(&mcm_dev->notifier,
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
				v4l2_async_nf_cleanup(&mcm_dev->notifier);
#else
				v4l2_async_notifier_cleanup(&mcm_dev->notifier);
#endif
				return ret;
			}
		}
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
	ret = v4l2_async_nf_register(&mcm_dev->notifier);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 16, 0)
	ret = v4l2_async_subdev_nf_register(&mcm_dev->sd,
				&mcm_dev->notifier);
#else
	ret = v4l2_async_subdev_notifier_register(&mcm_dev->sd,
				&mcm_dev->notifier);
#endif

	if (ret) {
		dev_err(mcm_dev->dev, "Async notifier register error\n");
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 16, 0)
		v4l2_async_nf_cleanup(&mcm_dev->notifier);
#else
		v4l2_async_notifier_cleanup(&mcm_dev->notifier);
#endif
	}

	return ret;
}

static int mcm_subdev_pads_init(struct mcm_subdev_dev *mcm_dev)
{
	int pad = 0;

	for (pad = 0; pad < MCM_SUBDEV_PAD_NR; pad++) {
		if (pad % MCM_CHN_MAX == 0)
			mcm_dev->pads[pad].flags = MEDIA_PAD_FL_SINK;
		else
			mcm_dev->pads[pad].flags = MEDIA_PAD_FL_SOURCE;

		INIT_LIST_HEAD(&mcm_dev->pad_data[pad].queue);
		mutex_init(&mcm_dev->pad_data[pad].q_lock);
		mcm_dev->pad_data[pad].stream = 0;
		mcm_dev->pad_data[pad].sensor_out_state = SENSOR_OUT_DISABLE_MCM_MMU_ENABLE;
		mcm_dev->pad_data[pad].first_frame = true;
	}
	return 0;
}

static int mcm_subdev_parse_params(struct mcm_subdev_dev *mcm_dev,
		struct platform_device *pdev)
{
	fwnode_property_read_u32(of_fwnode_handle(pdev->dev.of_node),
			"id", &mcm_dev->id);
	return 0;
}

static int mcm_miscdev_open(struct inode *inode, struct file *file)
{
	struct miscdevice *pmisc_dev = file->private_data;
	struct mcm_subdev_dev *mcm_dev;

	mcm_dev = container_of(pmisc_dev, struct mcm_subdev_dev, miscdev);
	if (!mcm_dev)
		return -ENOMEM;

	mcm_dev->refcnt_misc++;

	return 0;
}

static int mcm_miscdev_release(struct inode *inode, struct file *file)
{

	struct miscdevice *pmisc_dev = file->private_data;
	struct mcm_subdev_dev *mcm_dev;

	mcm_dev = container_of(pmisc_dev, struct mcm_subdev_dev, miscdev);
	if (!mcm_dev)
		return -ENOMEM;

	mcm_dev->refcnt_misc--;

	return 0;
}

static int mcm_miscdev_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct miscdevice *pmisc_dev = file->private_data;
	struct mcm_subdev_dev *mcm_dev;
	struct vvcam_vb2_buffer *pos;
	struct mcm_pad_data *cur_pad;
	int pad = 0;

	pr_info("vm_pgoff: 0x%lx\n", vma->vm_pgoff);

	mcm_dev = container_of(pmisc_dev, struct mcm_subdev_dev, miscdev);
	if (!mcm_dev) {
		pr_err("didn't get mcm_dev\n");
		return -ENOMEM;
	}

	/*
	 * Only the source pads 1, 3 will have buffers in the queue, no need
	 * of the checking the sinkpads
	 */
	for (pad = 1; pad < MCM_SUBDEV_PAD_NR; pad += 2) {
		cur_pad = &mcm_dev->pad_data[pad];

		mutex_lock(&cur_pad->q_lock);
		if (list_empty(&cur_pad->queue)) {
			pr_warn("%s queue empty pad %d\n", __func__, pad);
			mutex_unlock(&cur_pad->q_lock);
			continue;
		}

		list_for_each_entry(pos, &cur_pad->queue, list) {
			if (pos && (pos->planes[Y_PLANE].dma_addr >> PAGE_SHIFT)
					== vma->vm_pgoff){
				mutex_unlock(&cur_pad->q_lock);
				goto found_handle;
			}
		}
		mutex_unlock(&cur_pad->q_lock);
	}

	if (!pos)
		return -EAGAIN;

found_handle:
	vma->vm_pgoff = 0;

	/* If the MCM raw frame dump is enabled, we are working with isp_dma_heap
	 * buffers, hence call the respective mmap API
	 */
	if (cur_pad->sensor_out_state == SENSOR_OUT_ENABLE_MCM_MMU_ENABLE ||
			cur_pad->sensor_out_state == SENSOR_OUT_ENABLE_MCM_MMU_DISABLE)
		vb2_isp_dma_heap_mmap(vb2_plane_cookie(&pos->vb.vb2_buf, 0), vma);
	else
		ispSS_SHM_mmap(pos->shm_handle, vma);

	pr_info("%s success\n", __func__);
	return 0;
}

static const struct file_operations mcm_miscdev_fops = {
	.owner          = THIS_MODULE,
	.open           = mcm_miscdev_open,
	.release        = mcm_miscdev_release,
	.mmap           = mcm_miscdev_mmap,
};

static int mcm_subdev_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mcm_subdev_dev *mcm_dev;
	int ret;

	mcm_dev = devm_kzalloc(&pdev->dev,
			sizeof(struct mcm_subdev_dev), GFP_KERNEL);
	if (!mcm_dev)
		return -ENOMEM;

	mutex_init(&mcm_dev->mlock);
	mcm_dev->dev = &pdev->dev;
	platform_set_drvdata(pdev, mcm_dev);

	ret = mcm_subdev_parse_params(mcm_dev, pdev);
	if (ret) {
		dev_err(&pdev->dev, "failed to parse params\n");
		return -EINVAL;
	}

	v4l2_subdev_init(&mcm_dev->sd, &mcm_subdev_subdev_ops);
#if KERNEL_VERSION(6, 0, 0) <= LINUX_VERSION_CODE
	snprintf(mcm_dev->sd.name, 64,
			"%s.%d", MCM_NAME, mcm_dev->id);
#else
	snprintf(mcm_dev->sd.name, V4L2_SUBDEV_NAME_SIZE,
			"%s.%d", MCM_NAME, mcm_dev->id);
#endif

	mcm_dev->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	mcm_dev->sd.flags |= V4L2_SUBDEV_FL_HAS_EVENTS;
	mcm_dev->sd.dev =  &pdev->dev;
	mcm_dev->sd.owner = THIS_MODULE;
	mcm_dev->sd.internal_ops = &mcm_subdev_internal_ops;
	mcm_dev->sd.entity.ops = &mcm_subdev_entity_ops;
	mcm_dev->sd.entity.function = MEDIA_ENT_F_IO_V4L;
	mcm_dev->sd.entity.obj_type = MEDIA_ENTITY_TYPE_V4L2_SUBDEV;
	mcm_dev->sd.entity.name = mcm_dev->sd.name;
	v4l2_set_subdevdata(&mcm_dev->sd, mcm_dev);

	mcm_subdev_pads_init(mcm_dev);
	ret = media_entity_pads_init(&mcm_dev->sd.entity,
			MCM_SUBDEV_PAD_NR, mcm_dev->pads);
	if (ret)
		return ret;

	ret = mcm_subdev_async_notifier(mcm_dev);
	if (ret)
		goto err_async_notifier;

	ret = v4l2_async_register_subdev(&mcm_dev->sd);
	if (ret) {
		dev_err(dev, "register subdev error\n");
		goto error_register_subdev;
	}

	ret = isp_bcm_open();
	if (ret) {
		dev_err(dev, "bcm open failed %d\n", ret);
		goto error_bcm_open;
	}

	/* Allocate shm to be used for sharing the V4L2 event data*/
	mcm_dev->event_shm.virt_addr = (void *)__get_free_pages(GFP_KERNEL, 1);
	mcm_dev->event_shm.size = PAGE_SIZE * 2;
	memset(mcm_dev->event_shm.virt_addr, 0, mcm_dev->event_shm.size);
	mcm_dev->event_shm.phy_addr = virt_to_phys(mcm_dev->event_shm.virt_addr);
	mutex_init(&mcm_dev->event_shm.event_lock);

	mcm_dev->miscdev.minor = MISC_DYNAMIC_MINOR;
	mcm_dev->miscdev.name  = "mcm-miscdev";
	mcm_dev->miscdev.fops  = &mcm_miscdev_fops;

	ret = misc_register(&mcm_dev->miscdev);
	if (ret) {
		dev_err(&pdev->dev, "failed to register misc device\n");
		return -EINVAL;
	}

	dev_info(&pdev->dev, "mcm driver probe success\n");

	return 0;

error_bcm_open:
	v4l2_async_unregister_subdev(&mcm_dev->sd);
error_register_subdev:
#if KERNEL_VERSION(5, 16, 0) <= LINUX_VERSION_CODE
	v4l2_async_nf_unregister(&mcm_dev->notifier);
	v4l2_async_nf_cleanup(&mcm_dev->notifier);
#else
	v4l2_async_notifier_unregister(&mcm_dev->notifier);
	v4l2_async_notifier_cleanup(&mcm_dev->notifier);
#endif
err_async_notifier:
	media_entity_cleanup(&mcm_dev->sd.entity);

	return ret;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 0)
static void mcm_subdev_remove(struct platform_device *pdev)
#else
static int mcm_subdev_remove(struct platform_device *pdev)
#endif
{
	struct mcm_subdev_dev *mcm_dev;

	mcm_dev = platform_get_drvdata(pdev);

	dev_info(&pdev->dev, "mcm driver remove started\n");
	misc_deregister(&mcm_dev->miscdev);
	isp_bcm_close();

	v4l2_async_unregister_subdev(&mcm_dev->sd);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 16, 0)
	v4l2_async_nf_unregister(&mcm_dev->notifier);
	v4l2_async_nf_cleanup(&mcm_dev->notifier);
#else
	v4l2_async_notifier_unregister(&mcm_dev->notifier);
	v4l2_async_notifier_cleanup(&mcm_dev->notifier);
#endif
	media_entity_cleanup(&mcm_dev->sd.entity);
	free_pages((unsigned long)mcm_dev->event_shm.virt_addr, 1);
	dev_info(&pdev->dev, "mcm driver remove\n");

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 0, 0)
	return 0;
#endif
}

static const struct of_device_id mcm_subdev_of_match[] = {
	{.compatible = "syna,dolphin-mcm",},
	{ /* sentinel */ },
};

MODULE_DEVICE_TABLE(of, mcm_subdev_of_match);

static struct platform_driver mcm_subdev_driver = {
	.probe  = mcm_subdev_probe,
	.remove = mcm_subdev_remove,
	.driver = {
		.name           = MCM_NAME,
		.owner          = THIS_MODULE,
		.of_match_table = mcm_subdev_of_match,
	}
};

static int __init mcm_subdev_init_module(void)
{
	int ret;

	ret = platform_driver_register(&mcm_subdev_driver);
	if (ret) {
		pr_err("Failed to register mcm subdev driver\n");
		return ret;
	}

	return ret;
}

static void __exit mcm_subdev_exit_module(void)
{
	platform_driver_unregister(&mcm_subdev_driver);
}

module_init(mcm_subdev_init_module);
module_exit(mcm_subdev_exit_module);

MODULE_DESCRIPTION("MCM V4L2 Subdev driver");
MODULE_LICENSE("GPL");
