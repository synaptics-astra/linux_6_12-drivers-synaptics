// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2023 Synaptics Incorporated
 *
 */

#include "vpp_mem.h"
#include <linux/dma-buf.h>
#include <linux/dma-heap.h>
#include <linux/version.h>

static struct dma_heap *dmabuf_dma_heap;
static struct dma_heap *dmabuf_nc_dma_heap;

static char *dma_reserved_heaps[] = { "reserved", "linux,cma" };

void VPP_DMABUF_FreeMemory(VPP_MEM *shm_handle);
int is_ampless_boot(void);

static int Dmabuf_GetDmaBufByName(char *heap_name, struct dma_heap **heap)
{
	int res = VPP_MEM_ERROR_TYPE_OK;

	*heap = dma_heap_find(heap_name);
	if (*heap == NULL) {
		pr_err("%s: dma_heap_find failed for - %s\n", __func__, heap_name);
		return VPP_MEM_ERROR_TYPE_NOENT;
	}

	return res;
}

static int Dmabuf_AllocateMemory(struct dma_heap *heap, VPP_MEM *heap_shm, struct device *dev)
{
	if (IS_ERR(heap)) {
		pr_err("%s: invalid heap\n", __func__);
		return VPP_MEM_ERROR_TYPE_ENOMEM;
	}

	heap_shm->handle = dma_heap_buffer_alloc(heap, heap_shm->size, 0, 0);

	if (IS_ERR(heap_shm->handle)) {
		pr_err("%s: ion alloc failed, size=%ld\n", __func__, heap_shm->size);
		return VPP_MEM_ERROR_TYPE_ENOMEM;
	}

	heap_shm->attach = dma_buf_attach(heap_shm->handle, dev);
	if (heap_shm->attach == NULL) {
		dma_heap_buffer_free(heap_shm->handle);
		return VPP_MEM_ERROR_TYPE_ENOMEM;
	}

	heap_shm->table = dma_buf_map_attachment(heap_shm->attach, DMA_BIDIRECTIONAL);
	if (heap_shm->table == NULL) {
		dma_buf_detach(heap_shm->handle, heap_shm->attach);
		dma_heap_buffer_free(heap_shm->handle);
		return VPP_MEM_ERROR_TYPE_ENOMEM;
	}


	if (dma_buf_begin_cpu_access(heap_shm->handle, DMA_BIDIRECTIONAL) != 0) {
		pr_err("%s: dma_buf_begin_cpu_access failed\n", __func__);
		dma_buf_unmap_attachment(heap_shm->attach, heap_shm->table, DMA_BIDIRECTIONAL);
		dma_buf_detach(heap_shm->handle, heap_shm->attach);
		dma_heap_buffer_free(heap_shm->handle);
		return VPP_MEM_ERROR_TYPE_ENOMEM;
	}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
	{
		int ret;

		ret = dma_buf_vmap(heap_shm->handle, &heap_shm->map);
		heap_shm->k_addr = heap_shm->map.vaddr;
	}
#else
	heap_shm->k_addr = dma_buf_kmap(heap_shm->handle, 0);
#endif
	heap_shm->p_addr = (phys_addr_t)sg_dma_address(heap_shm->table->sgl);
	heap_shm->u_addr = (void *)NULL;

	if (heap_shm->k_addr == NULL) {
		VPP_DMABUF_FreeMemory(heap_shm);
		return VPP_MEM_ERROR_TYPE_ENOMEM;
	}

	return VPP_MEM_ERROR_TYPE_OK;
}

static int VPP_DMABUF_InitMem(int index)
{
	int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(dma_reserved_heaps); i++) {
		ret = Dmabuf_GetDmaBufByName(dma_reserved_heaps[i], &dmabuf_dma_heap);
		if (ret == VPP_MEM_ERROR_TYPE_OK)
			break;
	}
	return ret;
}

static int VPP_DMABUF_InitMem_NonCached(int index)
{
	return Dmabuf_GetDmaBufByName("NonSecure-NC", &dmabuf_nc_dma_heap);
}

/* DMABUF MEMORY Public APIs */
int VPP_DMABUF_IsReady(void)
{
	int ret;
	int i;
	struct dma_heap *heap;

	for (i = 0; i < ARRAY_SIZE(dma_reserved_heaps); i++) {
		heap = dma_heap_find(dma_reserved_heaps[i]);
		if (heap == NULL) {
			ret = VPP_MEM_ERROR_TYPE_ENOMEM;
		} else {
			ret = VPP_MEM_ERROR_TYPE_OK;
			break;
		}
	}
	return ret;
}

int VPP_DMABUF_AllocMem(VPP_MEM_NODE *shm_node, VPP_MEM *shm_handle, gfp_t flags)
{
	struct dma_heap *dmabuf_heap;

	if (shm_node->shm_type == VPP_MEM_TYPE_DMA) {
		dmabuf_heap = dmabuf_dma_heap;
	} else if (!is_ampless_boot() && shm_node->shm_type == VPP_MEM_TYPE_NS_NC) {
		dmabuf_heap = dmabuf_nc_dma_heap;
	} else {
		pr_err("%s: FAILED - Invalid shm type ..!!!\n", __func__);
		return VPP_MEM_ERROR_TYPE_ENOMEM;
	}

	return Dmabuf_AllocateMemory(dmabuf_heap, shm_handle, shm_node->dev);
}

int VPP_DMABUF_InitMemory(VPP_MEM_LIST *shm_list)
{
	int ret;

	ret = VPP_DMABUF_InitMem(0);
	if (ret != VPP_MEM_ERROR_TYPE_OK) {
		pr_err("%s: FAILED ..!!!\n", __func__);
		return ret;
	}

	//NS-NC not supported in ampless boot, So don't proceed.
	if (is_ampless_boot())
		goto ampless_init_done;

	ret = VPP_DMABUF_InitMem_NonCached(0);
	if (ret != VPP_MEM_ERROR_TYPE_OK) {
		pr_err("%s: VPP_DMABUF_InitMem_NonCached FAILED ..!!!\n", __func__);
		return ret;
	}

ampless_init_done:
	return VPP_MEM_ERROR_TYPE_OK;
}

void VPP_DMABUF_DeinitMemory(VPP_MEM_LIST *shm_list)
{
}

void VPP_DMABUF_FreeMemory(VPP_MEM *shm_handle)
{
	if (shm_handle->handle) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
		dma_buf_vunmap(shm_handle->handle, &shm_handle->map);
#endif
		dma_buf_end_cpu_access(shm_handle->handle, DMA_BIDIRECTIONAL);
		dma_buf_unmap_attachment(shm_handle->attach, shm_handle->table, DMA_BIDIRECTIONAL);
		dma_buf_detach(shm_handle->handle, shm_handle->attach);
		dma_heap_buffer_free(shm_handle->handle);
		shm_handle->handle = NULL;
	}
}

void VPP_MEM_probe(VPP_MEM_LIST *shm_list)
{
	shm_list->ops->VPP_MEM_IsReady = VPP_DMABUF_IsReady;
	shm_list->ops->VPP_MEM_InitMemory = VPP_DMABUF_InitMemory;
	shm_list->ops->VPP_MEM_DeInitMemory = VPP_DMABUF_DeinitMemory;
	shm_list->ops->VPP_MEM_AllocateMemory = VPP_DMABUF_AllocMem;
	shm_list->ops->VPP_MEM_FreeMemory = VPP_DMABUF_FreeMemory;
}
