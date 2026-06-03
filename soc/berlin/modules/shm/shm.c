// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2019 Synaptics Incorporated
 *
 * Author: Dejin Zheng <Dejin.Zheng@synaptics.com>
 *         Jie Xi <Jie.Xi@synaptics.com>
 *
 */

#define pr_fmt(fmt) "[shm kernel driver]" fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/miscdevice.h>
#include <linux/version.h>
#include <linux/semaphore.h>
#include <linux/uaccess.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/dma-buf.h>
#include <linux/scatterlist.h>
#include <linux/sched/task.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <uapi/linux/dma-heap.h>
#include <linux/dma-heap.h>
#include "shm.h"
#include <linux/mod_devicetable.h>
#include "kernel_compatibility.h"


struct gid_node_data {
	int gid;
	bool alloc;
	struct berlin_buffer_node *bn;
};

enum shm_dmabuf_src {
	SHM_REG,
	SHM_ALLOC
};

enum shm_dmabuf_pa_flags {
	SHM_PA_INVALID,
	SHM_PA_SCATTER,
	SHM_PA_CONTIGUOUS,
};

static struct shm_device shm_dev;

static void berlin_buffer_node_destroy(struct kref *kref)
{
	struct berlin_buffer_node *bn = container_of(kref,
					 struct berlin_buffer_node, ref);

	pr_debug("free buffer node[%08x]\n", bn->gid);
	down_write(&bn->dev->idr_lock);
	idr_remove(&bn->dev->idr, bn->gid);
	up_write(&bn->dev->idr_lock);

	pr_debug("dma buffer refcount before put: %ld\n",
		atomic_long_read(&bn->dmabuf->file->f_count));
	if (bn->sg_table)
		dma_buf_unmap_attachment(bn->attach, bn->sg_table, DMA_BIDIRECTIONAL);

	dma_buf_detach(bn->dmabuf, bn->attach);
	dma_buf_put(bn->dmabuf);

	kfree(bn);
}

static inline int berlin_buffer_node_put(struct berlin_buffer_node *bn)
{
	pr_debug("ref count before putting buffer node[%08x]: %d\n",
		bn->gid, refcount_read(&bn->ref.refcount));
	return kref_put(&bn->ref, berlin_buffer_node_destroy);
}

static inline int berlin_buffer_node_get(struct berlin_buffer_node *bn)
{
	int ret;

	pr_debug("ref count before getting buffer node[%08x]: %d\n",
		bn->gid, refcount_read(&bn->ref.refcount));
	ret = kref_get_unless_zero(&bn->ref);
	if (!ret)
		pr_err("%s: fail to get buffer node\n", __func__);
	return ret ? 0 : -EINVAL;
}

static inline int buffer_dec_ref(struct berlin_buffer_node *bn)
{
	dma_buf_put(bn->dmabuf);
	return berlin_buffer_node_put(bn);
}

static inline int buffer_inc_ref(struct berlin_buffer_node *bn)
{
	int ret;

	ret = berlin_buffer_node_get(bn);
	if (!ret)
		get_dma_buf(bn->dmabuf);
	return ret;
}

static int shm_debug_gid_show(struct seq_file *s, void *unused)
{
	struct shm_device *dev = s->private;
	struct berlin_buffer_node *bn;
	phys_addr_t phys;
	size_t size;
	int i = 0;
	int id = 0;

	seq_puts(s, "|No  |Gid  |Ref |  pid   |  process_name  |   tid  |   thread_name  |physaddr|  size  |alloclen|\n");
	seq_puts(s, "--------------------------------------------------------------------------------------------------------\n");

	down_read(&dev->idr_lock);
	idr_for_each_entry(&dev->idr, bn, id) {
		if (!bn->sg_table) {
			bn->sg_table = dma_buf_map_attachment(bn->attach, DMA_BIDIRECTIONAL);
			if (IS_ERR(bn->sg_table)) {
				pr_err("map dmabuf %p failed with err=%ld\n", bn->dmabuf,
						PTR_ERR(bn->sg_table));
				return PTR_ERR(bn->sg_table);
			}
		}
		phys = PFN_PHYS(page_to_pfn(sg_page(bn->sg_table->sgl)));
		size = bn->dmabuf->size;

		seq_printf(s, "|%4d|%5u|%4ld|%8d|%16s|%8d|%16s|%8llx|%8zx|%8zx|\n",
			   i, id,
			   atomic_long_read(&bn->dmabuf->file->f_count),
			   bn->owner->pid, bn->owner->task_comm,
			   bn->owner->tid, bn->owner->thread_name,
			   phys, size, bn->alloc_len);
		i++;
	}
	up_read(&dev->idr_lock);
	seq_puts(s, "--------------------------------------------------------------------------------------------------------\n");

	return 0;
}

static int shm_debug_gid_open(struct inode *inode, struct file *file)
{
	return single_open(file, shm_debug_gid_show, inode->i_private);
}

static const struct file_operations debug_gid_fops = {
	.open = shm_debug_gid_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int gid_add_client(struct shm_client *client,
			  struct gid_node_data *node_data)
{
	struct rb_node **p;
	struct rb_node *parent = NULL;
	int found = 0;

	mutex_lock(&client->lock);
	p = &client->gids.rb_node;
	while (*p) {
		struct berlin_gid_node *tmp = rb_entry(*p,
						struct berlin_gid_node, node);
		parent = *p;
		if (tmp->gid < node_data->gid)
			p = &(*p)->rb_left;
		else if (tmp->gid > node_data->gid)
			p = &(*p)->rb_right;
		else {
			pr_debug("duplicated gid[%d] on client[%s]\n",
					tmp->gid, client->name);
			found = 1;
			if (!node_data->alloc)
				tmp->ref_cnt++;
			else
				pr_err("same gid[%d] allocated\n",
					  tmp->gid);
			break;
		}
	}
	if (!found) {
		struct berlin_gid_node *data;

		pr_debug("add gid node[%d] on client[%s]\n",
			node_data->gid, client->name);
		data = kzalloc(sizeof(*data), GFP_KERNEL);
		if (!data) {
			mutex_unlock(&client->lock);
			return -ENOMEM;
		}
		data->gid = node_data->gid;
		data->data = node_data->bn;
		data->ref_cnt = 1;

		rb_link_node(&data->node, parent, p);
		rb_insert_color(&data->node, &client->gids);
	}
	mutex_unlock(&client->lock);

	return 0;
}

static void gid_remove_all(struct shm_client *client)
{
	struct rb_node *p = NULL;
	struct berlin_gid_node *data = NULL;
	struct berlin_buffer_node *bn = NULL;

	mutex_lock(&client->lock);
	pr_debug("remove all gid nodes from client[%s]\n",
			client->name);
	while ((p = rb_first(&client->gids))) {
		data = rb_entry(p, struct berlin_gid_node, node);
		rb_erase(&data->node, &client->gids);
		bn = (struct berlin_buffer_node *)data->data;
		while (data->ref_cnt) {
			berlin_buffer_node_put(bn);
			data->ref_cnt--;
		}
		kfree(data);
	}
	mutex_unlock(&client->lock);
}

static int gid_remove_client(struct shm_client *client, int gid)
{
	int ret = 0;
	struct rb_node *p = NULL;
	struct berlin_gid_node *data = NULL;
	struct berlin_buffer_node *bn = NULL;

	mutex_lock(&client->lock);
	p = client->gids.rb_node;
	while (p) {
		data = rb_entry(p, struct berlin_gid_node, node);
		if (data->gid < gid)
			p = p->rb_left;
		else if (data->gid > gid)
			p = p->rb_right;
		else
			break;
	}
	if (data && p) {
		bn = (struct berlin_buffer_node *)data->data;
		berlin_buffer_node_put(bn);
		data->ref_cnt--;
		pr_debug("%s: gid %d from client[%s], ref cnt %d\n", __func__,
			 gid, client->name, data->ref_cnt);
		if (!data->ref_cnt) {
			pr_debug("remove gid node[%d] from client[%s]\n",
				  data->gid, client->name);
			rb_erase(&data->node, &client->gids);
			kfree(data);
		}
	} else {
		pr_err("%s: no gid %d found from client[%s]\n", __func__, gid,
			client->name);
		ret = -EINVAL;
	}
	mutex_unlock(&client->lock);
	return ret;
}

static int buffer_node_create(struct shm_client *client,
			      struct dma_buf *dmabuf, int *gid, int flags,
			      size_t alloc_len)
{
	struct berlin_buffer_node *bn;
	struct gid_node_data node_data;
	int ret;

	bn = kzalloc(sizeof(*bn), GFP_KERNEL);
	if (!bn)
		return -ENOMEM;

	bn->dmabuf = dmabuf;
	bn->buf_flags = flags;
	bn->alloc_len = alloc_len;
	bn->dev = &shm_dev;
	bn->owner = client;
	kref_init(&bn->ref);

	bn->attach = dma_buf_attach(bn->dmabuf, shm_dev.dev);
	if (IS_ERR(bn->attach)) {
		pr_err("attach dmabuf %p failed with err=%ld\n", bn->dmabuf, PTR_ERR(bn->attach));
		ret = PTR_ERR(bn->attach);
		goto free1;
	}

	/* when dma_buf_map_attachment, we ignore cache operations
	 * since we will do cache clean by ioctl cmd do_sync
	 * let user to control cache operations
	 */
	bn->attach->dma_map_attrs |= DMA_ATTR_SKIP_CPU_SYNC;

	/* for SHM_REG, we don't need to map the buffer here. Since it's uauslly called dynamically
	 * And dma_buf_map_attachment will hurt performance, so we skip it here.
	 * When get_pa or cached_sync, then we map the buffer.
	 */
	if (flags >> 16 == SHM_ALLOC) {
		bn->sg_table = dma_buf_map_attachment(bn->attach, DMA_BIDIRECTIONAL);
		if (IS_ERR(bn->sg_table)) {
			pr_err("map dmabuf %p failed with err=%ld\n", bn->dmabuf, PTR_ERR(bn->sg_table));
			ret = PTR_ERR(bn->sg_table);
			goto free2;
		}
		bn->phy_addr = (unsigned long)PFN_PHYS(page_to_pfn(sg_page(bn->sg_table->sgl)));
		if (bn->sg_table->nents > 1)
			bn->phy_addr_flags = SHM_PA_SCATTER;
		else
			bn->phy_addr_flags = SHM_PA_CONTIGUOUS;
	}
	mutex_init(&bn->lock);

	down_write(&shm_dev.idr_lock);
	ret = idr_alloc(&shm_dev.idr, bn, 1, 0, GFP_KERNEL);
	up_write(&shm_dev.idr_lock);
	if (ret < 0) {
		pr_err("error in allocating gid: %x\n", ret);
		goto free3;
	}
	*gid = ret;
	bn->gid = *gid;

	node_data.gid = *gid;
	node_data.bn = bn;
	node_data.alloc = true;
	ret = gid_add_client(client, &node_data);
	if (ret)
		goto free4;

	bn->alloc_len = dmabuf->size;

	return ret;
free4:
	down_write(&shm_dev.idr_lock);
	idr_remove(&shm_dev.idr, bn->gid);
	up_write(&shm_dev.idr_lock);
free3:
	if (bn->sg_table)
		dma_buf_unmap_attachment(bn->attach, bn->sg_table, DMA_BIDIRECTIONAL);
free2:
	dma_buf_detach(bn->dmabuf, bn->attach);
free1:
	mutex_destroy(&bn->lock);
	kfree(bn);
	return ret;
}

struct shm_client *shm_client_create(const char *name)
{
	struct shm_client *client;
	struct task_struct *task;
	struct rb_node **p;
	struct rb_node *parent = NULL;
	struct shm_client *entry;

	if (!name) {
		pr_err("%s: Name cannot be null\n", __func__);
		return ERR_PTR(-EINVAL);
	}

	get_task_struct(current->group_leader);
	task_lock(current->group_leader);
	/*
	 * don't bother to store task struct for kernel threads,
	 * they can't be killed anyway
	 */
	if (current->group_leader->flags & PF_KTHREAD) {
		put_task_struct(current->group_leader);
		task = NULL;
	} else {
		task = current->group_leader;
	}
	task_unlock(current->group_leader);

	client = kzalloc(sizeof(*client), GFP_KERNEL);
	if (!client)
		goto err_put_task_struct;

	client->dev = &shm_dev;
	mutex_init(&client->lock);
	client->task = task;
	client->name = kstrdup(name, GFP_KERNEL);
	if (!client->name)
		goto err_free_client;

	client->gids = RB_ROOT;

	get_task_comm(client->task_comm, current->group_leader);
	client->pid = task_tgid_vnr(current);
	strncpy(client->thread_name, current->comm, TASK_COMM_LEN);
	client->tid = task_pid_vnr(current);

	down_write(&shm_dev.client_lock);
	p = &shm_dev.clients.rb_node;
	while (*p) {
		parent = *p;
		entry = rb_entry(parent, struct shm_client, node);

		if (client < entry)
			p = &(*p)->rb_left;
		else if (client > entry)
			p = &(*p)->rb_right;
	}
	rb_link_node(&client->node, parent, p);
	rb_insert_color(&client->node, &shm_dev.clients);
	up_write(&shm_dev.client_lock);

	return client;

err_free_client:
	kfree(client);
err_put_task_struct:
	if (task)
		put_task_struct(current->group_leader);
	return ERR_PTR(-ENOMEM);
}
EXPORT_SYMBOL(shm_client_create);

void shm_client_destroy(struct shm_client *client)
{
	struct shm_device *dev = client->dev;

	gid_remove_all(client);

	down_write(&dev->client_lock);
	if (client->task)
		put_task_struct(client->task);
	rb_erase(&client->node, &dev->clients);
	up_write(&dev->client_lock);

	kfree(client->name);
	kfree(client);
}
EXPORT_SYMBOL(shm_client_destroy);

static int shm_release(struct inode *inode, struct file *file)
{
	struct shm_client *client = file->private_data;

	pr_debug("%s called\n", __func__);
	shm_client_destroy(client);
	return 0;
}

static int shm_open(struct inode *inode, struct file *file)
{
	struct shm_client *client;
	char debug_name[64];

	pr_debug("%s called\n", __func__);
	snprintf(debug_name, 64, "%u", task_pid_nr(current->group_leader));
	client = shm_client_create(debug_name);
	if (IS_ERR(client))
		return PTR_ERR(client);
	file->private_data = client;

	return 0;
}

static int shm_dmabuf_alloc(struct shm_client *client,
			    struct dmabuf_allocation_data *alloc,
			    struct dma_buf **pdmabuf)
{
	int fd;
	int ret;
	struct dma_buf *dmabuf;
	struct dma_heap *heap;
	unsigned int buf_flags;
	size_t len = alloc->len;
	unsigned int fd_flags = alloc->fd_flags;

	pr_debug("len: %lx heap_flags: %llx fd_flags: %x, heap name %s\n",
		  len, alloc->heap_flags, fd_flags, alloc->heap_name);

	heap = dma_heap_find(alloc->heap_name);
	if (!heap) {
		pr_err("failed to find dmabuf heap %s\n", alloc->heap_name);
		return PTR_ERR(heap);
	}
	dmabuf = dma_heap_buffer_alloc(heap, len, fd_flags, alloc->heap_flags);
	if (IS_ERR(dmabuf)) {
		pr_err("%s heap fail to allocate dma buf:%ld\n",
				alloc->heap_name, PTR_ERR(dmabuf));
		return PTR_ERR(dmabuf);
	}
	/* one additional refcnt for the gid */
	get_dma_buf(dmabuf);

	fd = dma_buf_fd(dmabuf, fd_flags);
	if (fd < 0) {
		pr_err("error in ion_alloc: %x\n", fd);
		ret = fd;
		goto free_buf;
	}
	buf_flags = (SHM_ALLOC << 16);
	ret = buffer_node_create(client, dmabuf, &alloc->gid, buf_flags, len);
	if (ret)
		goto free_buf;
	alloc->fd = fd;
	*pdmabuf = dmabuf;

	return ret;

free_buf:
	/*
	 * The refcnt of the dmabuf are increased by 2,
	 * one for fd and one for gid
	 * So if there is any error happened, the refcnt
	 * should be decreased by 2 accordingly
	 */
	dma_buf_put(dmabuf);
	dma_buf_put(dmabuf);
	return ret;
}

static int shm_detach(struct shm_client *client, int gid)
{
	if (gid <= 0) {
		pr_err("%s: invalid gid[%08x]", __func__, gid);
		return -EINVAL;
	}

	return gid_remove_client(client, gid);
}

static int shm_get_fd(struct shm_client *client, int gid, int *fd)
{
	struct berlin_buffer_node *bn;
	struct gid_node_data node_data;
	int ret;

	down_read(&shm_dev.idr_lock);
	bn = idr_find(&shm_dev.idr, gid);
	if (!bn) {
		pr_err("%s: fail to find the buf from gid[%x].\n",
				__func__, gid);
		up_read(&shm_dev.idr_lock);
		return -EINVAL;
	}
	ret = buffer_inc_ref(bn);
	if (ret) {
		up_read(&shm_dev.idr_lock);
		return ret;
	}
	up_read(&shm_dev.idr_lock);

	node_data.gid = gid;
	node_data.bn = bn;
	node_data.alloc = false;
	ret = gid_add_client(client, &node_data);
	if (ret) {
		pr_err("%s: fail to add gid[%08x].\n",
				__func__, gid);
		buffer_dec_ref(bn);
		return ret;
	}

	*fd = dma_buf_fd(bn->dmabuf, O_CLOEXEC);
	if (*fd < 0) {
		pr_err("%s: fail to get fd from gid[%08x]: %d.\n",
				__func__, gid, *fd);
		gid_remove_client(client, gid);
		dma_buf_put(bn->dmabuf);
		ret = *fd;
	}

	return ret;
}

int shm_put(struct shm_client *client, int gid)
{
	struct berlin_buffer_node *bn;
	int ret;

	down_read(&shm_dev.idr_lock);
	bn = idr_find(&shm_dev.idr, gid);
	if (!bn) {
		pr_err("%s: fail to find the buf from gid[%08x].\n",
				__func__, gid);
		up_read(&shm_dev.idr_lock);
		return PTR_ERR(bn);
	}
	ret = buffer_inc_ref(bn);
	if (ret) {
		up_read(&shm_dev.idr_lock);
		return ret;
	}
	up_read(&shm_dev.idr_lock);

	gid_remove_client(client, gid);
	buffer_dec_ref(bn);
	return 0;
}
EXPORT_SYMBOL(shm_put);

struct dma_buf *shm_get_dma_buf(struct shm_client *client, int gid)
{
	struct berlin_buffer_node *bn;
	struct gid_node_data node_data;
	int ret;

	down_read(&shm_dev.idr_lock);
	bn = idr_find(&shm_dev.idr, gid);
	if (!bn) {
		pr_err("%s: fail to find the buf from gid[%x].\n",
				__func__, gid);
		up_read(&shm_dev.idr_lock);
		return ERR_PTR(-EINVAL);
	}
	ret = buffer_inc_ref(bn);
	if (ret) {
		up_read(&shm_dev.idr_lock);
		return ERR_PTR(ret);
	}
	up_read(&shm_dev.idr_lock);

	node_data.gid = gid;
	node_data.bn = bn;
	node_data.alloc = false;
	ret = gid_add_client(client, &node_data);
	if (ret) {
		pr_err("%s: fail to add gid[%08x].\n",
				__func__, gid);
		buffer_dec_ref(bn);
		return ERR_PTR(ret);
	}

	return bn->dmabuf;
}
EXPORT_SYMBOL(shm_get_dma_buf);

static int shm_get_pa(struct shm_misc_data *data)
{
	struct berlin_buffer_node *bn;
	int ret;

	down_read(&shm_dev.idr_lock);
	bn = idr_find(&shm_dev.idr, data->gid);
	if (!bn) {
		pr_err("%s: fail to find the buf from gid[%08x].\n",
				__func__, data->gid);
		up_read(&shm_dev.idr_lock);
		return -EINVAL;
	}
	ret = buffer_inc_ref(bn);
	if (ret) {
		up_read(&shm_dev.idr_lock);
		return ret;
	}
	up_read(&shm_dev.idr_lock);

	if (bn->phy_addr_flags == SHM_PA_INVALID) {
		if (!bn->sg_table) {
			bn->sg_table = dma_buf_map_attachment(bn->attach, DMA_BIDIRECTIONAL);
			if (IS_ERR(bn->sg_table)) {
				pr_err("map dmabuf %p failed with err=%ld\n", bn->dmabuf, PTR_ERR(bn->sg_table));
				buffer_dec_ref(bn);
				return PTR_ERR(bn->sg_table);
			}
		}
		if (bn->sg_table->nents > 1) {
			bn->phy_addr_flags = SHM_PA_SCATTER;
		}
		else {
			bn->phy_addr = (unsigned long)PFN_PHYS(page_to_pfn(sg_page(bn->sg_table->sgl)));
			bn->phy_addr_flags = SHM_PA_CONTIGUOUS;
		}
	}

	if (bn->phy_addr_flags == SHM_PA_SCATTER) {
		data->phy_addr = 0;
		data->len = bn->dmabuf->size;
	} else if (bn->phy_addr_flags == SHM_PA_CONTIGUOUS) {
		data->phy_addr = bn->phy_addr;
		data->len = bn->dmabuf->size;
	} else {
		pr_err("%s: invalid phy_addr_flags[%d] for gid[%08x].\n",
				__func__, bn->phy_addr_flags, data->gid);
		return -EINVAL;
	}

	pr_debug("%s: gid[%08x] addr[%llx] len[%llx].\n",
		  __func__, data->gid, data->phy_addr, data->len);
	buffer_dec_ref(bn);

	return 0;
}

static int shm_attach(struct shm_client *client, int fd, int *gid)
{
	struct dma_buf *dmabuf;
	int ret;
	int buf_flags = SHM_REG << 16;

	dmabuf = dma_buf_get(fd);
	if (IS_ERR(dmabuf)) {
		pr_err("fail to get dmabuf from fd:%ld\n", PTR_ERR(dmabuf));
		return PTR_ERR(dmabuf);
	}

	ret = buffer_node_create(client, dmabuf, gid, buf_flags, 0);
	if (ret) {
		pr_err("fail to create buffer node: %d\n", ret);
		dma_buf_put(dmabuf);
	}

	return ret;
}

static int do_sync(struct device *dev, struct berlin_buffer_node *bn,
		   struct shm_sync_data *data)
{
	struct sg_table *table = NULL;
	unsigned long offset, region_len, op_len;
	struct scatterlist *sg;
	phys_addr_t phys_addr;
	int i;

	if (!dev) {
		pr_err("no device found\n");
		return -EINVAL;
	}

	offset = data->offset;
	region_len = data->len;
	if (!bn->sg_table) {
		bn->sg_table = dma_buf_map_attachment(bn->attach, DMA_BIDIRECTIONAL);
		if (IS_ERR(bn->sg_table)) {
			pr_err("map dmabuf %p failed with err=%ld\n", bn->dmabuf, PTR_ERR(bn->sg_table));
			return PTR_ERR(bn->sg_table);
		}

		/* get pa when do_sync to save shm_get_pa time*/
		if (bn->sg_table->nents > 1) {
			bn->phy_addr_flags = SHM_PA_SCATTER;
		}
		else {
			bn->phy_addr = (unsigned long)PFN_PHYS(page_to_pfn(sg_page(bn->sg_table->sgl)));
			bn->phy_addr_flags = SHM_PA_CONTIGUOUS;
		}
	}

	table = bn->sg_table;
	switch (data->op) {
	case SHM_INVALIDATE_CACHE:
		mutex_lock(&bn->lock);
		for_each_sg(table->sgl, sg, table->nents, i) {
			if (sg->length > offset) {
				phys_addr = PFN_PHYS(page_to_pfn(sg_page(sg)));
				op_len = sg->length - offset;
				op_len = op_len > region_len ?
					 region_len : op_len;
				dma_sync_single_for_cpu(dev,
					phys_addr + offset,
					op_len, DMA_FROM_DEVICE);
				region_len -= op_len;
				if (!region_len)
					break;
				offset = 0;
			} else {
				offset -= sg->length;
			}
		}
		mutex_unlock(&bn->lock);
		break;
	case SHM_CLEAN_CACHE:
		mutex_lock(&bn->lock);
		for_each_sg(table->sgl, sg, table->nents, i) {
			if (sg->length > offset) {
				phys_addr = PFN_PHYS(page_to_pfn(sg_page(sg)));
				op_len = sg->length - offset;
				op_len = op_len > region_len ?
					 region_len : op_len;
				dma_sync_single_for_device(dev,
					phys_addr + offset,
					op_len, DMA_TO_DEVICE);
				region_len -= op_len;
				if (!region_len)
					break;
				offset = 0;
			} else {
				offset -= sg->length;
			}
		}
		mutex_unlock(&bn->lock);
		break;
	case SHM_FLUSH_CACHE:
		mutex_lock(&bn->lock);
		for_each_sg(table->sgl, sg, table->nents, i) {
			if (sg->length > offset) {
				phys_addr = PFN_PHYS(page_to_pfn(sg_page(sg)));
				op_len = sg->length - offset;
				op_len = op_len > region_len ?
					 region_len : op_len;
				dma_sync_single_for_device(dev,
					phys_addr + offset,
					op_len, DMA_TO_DEVICE);
				dma_sync_single_for_cpu(dev,
					phys_addr + offset,
					op_len, DMA_FROM_DEVICE);
				region_len -= op_len;
				if (!region_len)
					break;
				offset = 0;
			} else {
				offset -= sg->length;
			}
		}
		mutex_unlock(&bn->lock);
		break;
	default:
		pr_err("%s: Unknown cache command %d\n",
			__func__, data->op);
		return -EINVAL;
	}

	return 0;
}

static int shm_sync(struct shm_sync_data *data)
{
	int ret = 0;
	struct berlin_buffer_node *bn;

	if (data->op > SHM_FLUSH_CACHE)
		return -EINVAL;

	down_read(&shm_dev.idr_lock);
	bn = idr_find(&shm_dev.idr, data->gid);
	if (!bn) {
		pr_err("%s: fail to find the buf from gid[%08x].\n",
				__func__, data->gid);
		up_read(&shm_dev.idr_lock);
		return PTR_ERR(bn);
	}
	ret = buffer_inc_ref(bn);
	if (ret) {
		up_read(&shm_dev.idr_lock);
		return ret;
	}
	up_read(&shm_dev.idr_lock);

	pr_debug("cache op: %d offset: %llx len: %llx\n",
		data->op, data->offset, data->len);

	do_sync(shm_dev.dev, bn, data);

	buffer_dec_ref(bn);
	return ret;
}

static long shm_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	unsigned int dir;
	union shm_ioctl_arg data;
	struct dma_buf *pdmabuf;
	struct shm_client *client = filp->private_data;

	dir = _IOC_DIR(cmd);
	if (_IOC_SIZE(cmd) > sizeof(data))
		return -EINVAL;

	if (cmd == SHM_IOC_DETACH)
		data.id = (int)arg;
	else if (!(dir & _IOC_WRITE))
		memset(&data, 0, sizeof(data));
	else if (copy_from_user(&data, (void __user *)arg, _IOC_SIZE(cmd)))
		return -EFAULT;

	pr_debug("shm cmd[%d] from client[%s]\n", _IOC_NR(cmd), client->name);
	switch (cmd) {
	case SHM_IOC_DMABUF_ALLOC:
		ret = shm_dmabuf_alloc(client, &(data.dmabuf_data), &pdmabuf);
		break;
	case SHM_IOC_GET_PA:
		ret = shm_get_pa(&data.misc);
		break;
	case SHM_IOC_GET_FD:
		ret = shm_get_fd(client, data.fd.gid, &data.fd.fd);
		break;
	case SHM_IOC_PUT:
		ret = shm_put(client, data.fd.gid);
		break;
	case SHM_IOC_ATTACH:
		ret = shm_attach(client, data.fd.fd, &data.fd.gid);
		break;
	case SHM_IOC_DETACH:
		ret = shm_detach(client, data.id);
		break;
	case SHM_IOC_SYNC:
		ret = shm_sync(&data.sync);
		break;
	default:
		return -EINVAL;
	}

	if (dir & _IOC_READ) {
		if (copy_to_user((void __user *)arg, &data, _IOC_SIZE(cmd))) {
			if (!ret) {
				if (cmd == SHM_IOC_DMABUF_ALLOC) {
					shm_detach(client, data.dmabuf_data.gid);
					dma_buf_put(pdmabuf);
				}
			}
			return -EFAULT;
		}
	}
	return ret;
}

static const struct file_operations shm_ops = {
	.open = shm_open,
	.release = shm_release,
	.unlocked_ioctl = shm_ioctl,
	.compat_ioctl = shm_ioctl,
	.owner = THIS_MODULE,
};

static int shm_init(struct shm_device *dev)
{
	dev->debug_root = debugfs_create_dir("shm", NULL);
	dev->gid_debug_root = debugfs_create_file("gid", 0664,
					dev->debug_root, (void *)dev,
					&debug_gid_fops);

	init_rwsem(&dev->idr_lock);
	init_rwsem(&dev->client_lock);
	idr_init(&dev->idr);
	dev->clients = RB_ROOT;

	return 0;
}

static int shm_exit(struct shm_device *dev)
{
	pr_debug("%s\n", __func__);
	idr_destroy(&dev->idr);
	debugfs_remove_recursive(dev->gid_debug_root);
	debugfs_remove_recursive(dev->debug_root);

	return 0;
}

static int shm_probe(struct platform_device *pdev)
{
	int ret;

	ret = shm_init(&shm_dev);
	if (ret < 0) {
		pr_err("shm_init fail!\n");
		return ret;
	}
	pr_debug("%s OK\n", __func__);

	shm_dev.dev = &pdev->dev;
	shm_dev.mdev.minor = MISC_DYNAMIC_MINOR;
	shm_dev.mdev.name = SHM_DEVICE_NAME;
	shm_dev.mdev.fops = &shm_ops;
	shm_dev.mdev.parent = NULL;
	ret = misc_register(&shm_dev.mdev);
	if (ret) {
		pr_err("error in register shm devices: %d\n", ret);
		shm_exit(&shm_dev);
	}

	return ret;
}

static RET_TYPE shm_remove(struct platform_device *pdev)
{
	misc_deregister(&shm_dev.mdev);
	shm_exit(&shm_dev);
	pr_debug("%s OK\n", __func__);
	RETURN_VALUE;
}

static const struct of_device_id shm_match[] = {
	{.compatible = "syna,berlin-shm",},
	{},
};
MODULE_DEVICE_TABLE(of, shm_match);

static struct platform_driver shm_driver = {
	.probe = shm_probe,
	.remove = shm_remove,
	.driver = {
		.name = "amp-shm",
		.of_match_table = shm_match,
	},
};

module_platform_driver(shm_driver);

MODULE_AUTHOR("synaptics");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("shm module");
MODULE_IMPORT_NS(DMA_BUF);
