/*
 * TrustZone Driver
 *
 * Copyright (C) 2019 Synaptics Ltd.
 * Copyright (C) 2016 Marvell Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/version.h>
#include <linux/moduleparam.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/version.h>
#ifdef CONFIG_DMABUF_HEAPS
#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#endif
#include "config.h"
#include "tz_log.h"
#include "tz_driver_private.h"
#include "kernel_compatibility.h"

struct shm_ops {
	int (*init)(void);
	int (*malloc)(struct tzd_shm *tzshm, gfp_t flags);
	int (*free)(struct tzd_shm *tzshm);
	int (*destroy)(void);
};


#ifdef CONFIG_DMABUF_HEAPS
static struct dma_heap *nc_dma_heap;

static int tzd_dmabuf_heap_shm_init(void)
{
	nc_dma_heap = dma_heap_find("NonSecure");
	if (nc_dma_heap != NULL) {
		tz_info("Using Nonsecure heap buffer!");
		return 0;
	}

	nc_dma_heap = dma_heap_find("reserved");
	if (nc_dma_heap != NULL) {
		tz_info("Using reserved heap buffer!");
		return 0;
	}

	return -ENOENT;
}
static int tzd_dmabuf_heap_shm_destroy(void)
{
	return 0;
}

static int tzd_dmabuf_heap_shm_alloc(struct tzd_shm *tzshm, gfp_t flags)
{
	void *pa = NULL;
	struct device *dev;
	struct dma_buf *nc_dma_buf;
	struct dma_buf_attachment * attach;
	struct sg_table *table;
	struct iosys_map map = {};

	nc_dma_buf = dma_heap_buffer_alloc(nc_dma_heap, tzshm->len, 0, 0);
	if (IS_ERR_OR_NULL(nc_dma_buf)) {
		tz_error("dma heap alloc failed, size=%d, error %ld", tzshm->len, PTR_ERR(nc_dma_buf));
		return -ENOMEM;
	}

	dev = tzshm->dev->dev;
	attach = dma_buf_attach(nc_dma_buf, dev);
	if (IS_ERR_OR_NULL(attach)) {
		dma_heap_buffer_free(nc_dma_buf);
		return PTR_ERR(attach);
	}

	table = dma_buf_map_attachment(attach, DMA_BIDIRECTIONAL);
	if (IS_ERR_OR_NULL(table)) {
		dma_buf_detach(nc_dma_buf, attach);
		dma_heap_buffer_free(nc_dma_buf);
		return PTR_ERR(table);
	}

	if (dma_buf_begin_cpu_access(nc_dma_buf, DMA_BIDIRECTIONAL) != 0) {
		dma_buf_unmap_attachment(attach, table, DMA_BIDIRECTIONAL);
		dma_buf_detach(nc_dma_buf, attach);
		dma_heap_buffer_free(nc_dma_buf);
		return -ENOMEM;
	}

	pa = (void *)sg_dma_address(table->sgl);

	dma_buf_vmap(nc_dma_buf, &map);
	if (iosys_map_is_null(&map)) {
		tz_error("dma heap buffer map failed.");
		dma_buf_end_cpu_access(nc_dma_buf, DMA_BIDIRECTIONAL);
		dma_buf_unmap_attachment(attach, table, DMA_BIDIRECTIONAL);
		dma_buf_detach(nc_dma_buf, attach);
		dma_heap_buffer_free(nc_dma_buf);
		return -ENOMEM;
	}

	tzshm->k_addr = map.vaddr;
	tzshm->u_addr = NULL;
	tzshm->userdata = (void *)nc_dma_buf;
	tzshm->p_addr = pa;
	tzshm->table = table;
	tzshm->attach = attach;

	return 0;
}

static int tzd_dmabuf_heap_shm_free(struct tzd_shm *tzshm)
{
	if (tzshm->userdata) {
		struct dma_buf *dmabuf_heap = tzshm->userdata;
		struct iosys_map map;
		iosys_map_set_vaddr(&map, tzshm->k_addr);
		dma_buf_vunmap(dmabuf_heap, &map);

		dma_buf_end_cpu_access(dmabuf_heap, DMA_BIDIRECTIONAL);
		dma_buf_unmap_attachment(tzshm->attach, tzshm->table, DMA_BIDIRECTIONAL);
		dma_buf_detach(dmabuf_heap, tzshm->attach);
		dma_heap_buffer_free(dmabuf_heap);
	}
	return 0;
}

static const struct shm_ops dmabuf_shm_ops = {
	.init = tzd_dmabuf_heap_shm_init,
	.malloc = tzd_dmabuf_heap_shm_alloc,
	.free = tzd_dmabuf_heap_shm_free,
	.destroy = tzd_dmabuf_heap_shm_destroy,
};

#endif

static int tzd_kmem_shm_init(void)
{
	return 0;
}

static int tzd_kmem_shm_destroy(void)
{
	return 0;
}

static int tzd_kmem_shm_alloc(struct tzd_shm *tzshm, gfp_t flags)
{
	void *alloc_addr;

	alloc_addr = (void *)__get_free_pages(flags,
			get_order(roundup(tzshm->len, PAGE_SIZE)));
	if (unlikely(!alloc_addr)) {
		tz_error("get free pages failed");
		return -ENOMEM;
	}

	tzshm->k_addr = alloc_addr;
	tzshm->u_addr = NULL;
	tzshm->p_addr = (void *)virt_to_phys(alloc_addr);

	return 0;
}

static int tzd_kmem_shm_free(struct tzd_shm *tzshm)
{
	if (likely(tzshm && tzshm->k_addr))
		free_pages((unsigned long)tzshm->k_addr,
			get_order(roundup(tzshm->len, PAGE_SIZE)));
	return 0;
}

static const struct shm_ops kmem_shm_ops = {
	.init = tzd_kmem_shm_init,
	.malloc = tzd_kmem_shm_alloc,
	.free = tzd_kmem_shm_free,
	.destroy = tzd_kmem_shm_destroy,
};

#ifdef CONFIG_DMABUF_HEAPS
# define DEFAULT_TZ_SHM_OPS	(&dmabuf_shm_ops)
#else
# define DEFAULT_TZ_SHM_OPS	(&kmem_shm_ops)
#endif

static const struct shm_ops *tz_shm_ops = DEFAULT_TZ_SHM_OPS;

struct tzd_shm *tzd_shm_new(struct tzd_dev_file *dev, size_t size, gfp_t flags)
{
	struct tzd_shm *shm;

	if (unlikely(!dev || !size))
		return NULL;

	shm = kzalloc(sizeof(*shm), flags);
	if (unlikely(!shm))
		return NULL;
	shm->len = size;
	shm->dev = dev;
	if (unlikely(tz_shm_ops->malloc(shm, flags) < 0)) {
		tz_error("shm_malloc failure on size (%zu)", size);
		kfree(shm);
		return NULL;
	}

	mutex_lock(&dev->tz_mutex);
	list_add_tail(&shm->head, &dev->dev_shm_head.shm_list);
	dev->dev_shm_head.shm_cnt++;
	mutex_unlock(&dev->tz_mutex);

	return shm;
}

void *tzd_shm_alloc(struct tzd_dev_file *dev, size_t size, gfp_t flags)
{
	struct tzd_shm *shm = tzd_shm_new(dev, size, flags);
	if (!shm)
		return NULL;
	return shm->p_addr;
}

int tzd_shm_free(struct tzd_dev_file *dev, const void *x)
{
	struct tzd_shm *shm, *next;

	if (unlikely(!dev))
		return -ENODEV;

	if (unlikely(!x))
		return -EFAULT;

	mutex_lock(&dev->tz_mutex);
	list_for_each_entry_safe(shm, next,
			&dev->dev_shm_head.shm_list, head) {
		if (shm->p_addr == x) {
			list_del(&shm->head);
			mutex_unlock(&dev->tz_mutex);
			if (likely(shm->k_addr))
				tz_shm_ops->free(shm);
			kfree(shm);
			return 0;
		}
	}
	mutex_unlock(&dev->tz_mutex);

	return 0;
}

int tzd_shm_delete(struct tzd_dev_file *dev)
{
	struct tzd_shm *shm, *next;

	if (unlikely(!dev))
		return -ENODEV;

	mutex_lock(&dev->tz_mutex);
	list_for_each_entry_safe(shm, next,
			&dev->dev_shm_head.shm_list, head) {
		list_del(&shm->head);
		if (likely(shm->k_addr))
			tz_shm_ops->free(shm);
		kfree(shm);
	}
	mutex_unlock(&dev->tz_mutex);

	return 0;
}

int tzd_shm_mmap(struct tzd_dev_file *dev, struct vm_area_struct *vma)
{
	struct tzd_shm *shm, *next;
	unsigned long pgoff;
	unsigned long map_len = vma->vm_end - vma->vm_start;
	int ret = -EINVAL;

	if (unlikely(!dev))
		return -ENODEV;

	mutex_lock(&dev->tz_mutex);
	list_for_each_entry_safe(shm, next,
			&dev->dev_shm_head.shm_list, head) {
		pgoff = (unsigned long)shm->p_addr >> PAGE_SHIFT;
		if ((pgoff == vma->vm_pgoff) && (map_len == PAGE_ALIGN(shm->len))){
			shm->u_addr = (void *)((unsigned long)vma->vm_start);
			ret = 0;
			break;
		}
	}
	mutex_unlock(&dev->tz_mutex);

	return ret;
}

void *tzd_shm_phys_to_virt_nolock(struct tzd_dev_file *dev, void *pa)
{
	struct tzd_shm *shm, *next;

	if (unlikely(!dev))
		return NULL;

	list_for_each_entry_safe(shm, next,
			&dev->dev_shm_head.shm_list, head) {
		if ((pa < shm->p_addr + shm->len) && (pa >= shm->p_addr))
			return shm->k_addr + (pa - shm->p_addr);
	}

	return NULL;
}

void *tzd_shm_phys_to_virt(struct tzd_dev_file *dev, void *pa)
{
	void *va;

	if (unlikely(!dev))
		return NULL;
	mutex_lock(&dev->tz_mutex);
	va = tzd_shm_phys_to_virt_nolock(dev, pa);
	mutex_unlock(&dev->tz_mutex);

	return va;
}
#ifdef CONFIG_DMABUF_HEAPS
#define DEFAULT_SHM_TYPE	"dmabuf-heaps"
#else
#define DEFAULT_SHM_TYPE	"kmem"
#endif

#define MAX_TYPE_LENGTH 20

static char shm_type[MAX_TYPE_LENGTH] = DEFAULT_SHM_TYPE;
#ifdef CONFIG_DMABUF_HEAPS
module_param_string(shm_type, shm_type, sizeof (shm_type), 0);
MODULE_PARM_DESC(shm_type,
	"dmabuf-heaps: use dmabuff heap shared mem; kmem: use kmem shared mem");
#endif

int tzd_shm_init(void)
{
#ifdef CONFIG_DMABUF_HEAPS
	if (!strncmp(shm_type, "dmabuf-heaps", 12))
		tz_shm_ops = &dmabuf_shm_ops;
	else
#endif
	if (!strncmp(shm_type, "kmem", 4))
		tz_shm_ops = &kmem_shm_ops;

	if (tz_shm_ops->init)
		return tz_shm_ops->init();

	return 0;
}

int tzd_shm_exit(void)
{
	return tz_shm_ops->destroy();
}
