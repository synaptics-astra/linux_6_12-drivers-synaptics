// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2023 Synaptics Incorporated
 *
 */

#include <linux/module.h>
#include "vpp_mem.h"

static int vpp_mem_ampless_boot;

/* Set by vpp driver; kept here (rather than queried back from vpp) to avoid
 * a circular module dependency: vpp already depends on vpp_mem, so vpp_mem
 * must not call back into vpp.
 */
void VPP_MEM_SetAmplessBoot(int ampless_boot)
{
	vpp_mem_ampless_boot = ampless_boot;
}
EXPORT_SYMBOL(VPP_MEM_SetAmplessBoot);

int VPP_MEM_IsAmplessBoot(void)
{
	return vpp_mem_ampless_boot;
}
EXPORT_SYMBOL(VPP_MEM_IsAmplessBoot);

int VPP_MEM_IsReady(void)
{
	int res;

	VPP_MEM_LIST shm_list;

	shm_list.ops = kmalloc(sizeof(VPP_MEM_OPS), GFP_KERNEL);
	if (!shm_list.ops) {
		pr_err("%s alloc mem failed\n", __func__);
		return 0;
	}

	VPP_MEM_probe(&shm_list);

	res = shm_list.ops->VPP_MEM_IsReady();

	pr_info("%s - res : %d\n", __func__, res);

	kfree(shm_list.ops);

	return res == VPP_MEM_ERROR_TYPE_OK ? 1 : 0;
}
EXPORT_SYMBOL(VPP_MEM_IsReady);

int VPP_MEM_InitMemory(VPP_MEM_LIST *shm_list)
{
	int i;
	VPP_MEM_NODE *shm_node;

	shm_list->ops = kmalloc(sizeof(VPP_MEM_OPS), GFP_KERNEL);
	if (!shm_list->ops) {
		pr_err("%s alloc mem failed\n", __func__);
		return VPP_MEM_ERROR_TYPE_ENOMEM;
	}

	for (i = 0; i < VPP_MEM_TYPE_MAX; i++) {
		shm_node = &shm_list->shm_node[i];
		INIT_LIST_HEAD(&shm_node->shm_list);
		mutex_init(&shm_node->shm_mutex);
		shm_node->shm_cnt = 0;
		shm_node->shm_type = i;
		shm_node->dev = shm_list->dev;
	}

	VPP_MEM_probe(shm_list);

	return shm_list->ops->VPP_MEM_InitMemory(shm_list);
}
EXPORT_SYMBOL(VPP_MEM_InitMemory);

void VPP_MEM_DeInitMemory(VPP_MEM_LIST *shm_list)
{
	shm_list->ops->VPP_MEM_DeInitMemory(shm_list);
}
EXPORT_SYMBOL(VPP_MEM_DeInitMemory);

int VPP_MEM_AllocateMemory(VPP_MEM_LIST *shm_list, VPP_MEM_TYPE shm_type,
				VPP_MEM *shm_handle, gfp_t flags)
{
	int res;
	VPP_MEM_NODE *shm_node;

	if (shm_type >= VPP_MEM_TYPE_MAX || !shm_handle)
		return VPP_MEM_ERROR_TYPE_ENOMEM;

	shm_node = &shm_list->shm_node[shm_type];

	//Allocated memory
	res = shm_list->ops->VPP_MEM_AllocateMemory(shm_node, shm_handle, flags);

	//Add allocated memory to list
	if (res == VPP_MEM_ERROR_TYPE_OK) {
		mutex_lock(&shm_node->shm_mutex);
		list_add_tail(&shm_handle->head, &shm_node->shm_list);
		shm_node->shm_cnt++;
		mutex_unlock(&shm_node->shm_mutex);
	}

	return res;
}
EXPORT_SYMBOL(VPP_MEM_AllocateMemory);

int VPP_MEM_FreeMemory(VPP_MEM_LIST *shm_list, VPP_MEM_TYPE shm_type,
							VPP_MEM *shm_handle)
{
	int res = VPP_MEM_ERROR_TYPE_OK;
	VPP_MEM *curr, *next;

	if (shm_type >= VPP_MEM_TYPE_MAX || !shm_handle)
		res = VPP_MEM_ERROR_TYPE_BADPARAM;

	//remove allocated memory from list and free memory
	if (res == VPP_MEM_ERROR_TYPE_OK) {
		VPP_MEM_NODE *shm_node = &shm_list->shm_node[shm_type];
		mutex_lock(&shm_node->shm_mutex);
		list_for_each_entry_safe(curr, next,
			&shm_node->shm_list, head) {
			if (curr->p_addr == shm_handle->p_addr) {
				list_del(&shm_handle->head);
				shm_node->shm_cnt--;
				mutex_unlock(&shm_node->shm_mutex);
				if (likely(shm_handle->k_addr))
					shm_list->ops->VPP_MEM_FreeMemory(shm_handle);
				res = VPP_MEM_ERROR_TYPE_OK;
				break;
			}
		}
		mutex_unlock(&shm_node->shm_mutex);
	}

	return res;
}
EXPORT_SYMBOL(VPP_MEM_FreeMemory);

void VPP_MEM_FlushCache(VPP_MEM_LIST *shm_list, VPP_MEM *shm_handle, unsigned int size)
{
	dma_addr_t phys_start = (uintptr_t) shm_handle->p_addr;

	//If size not provided, then flush entire memory allocated
	if (!size)
		size = shm_handle->size;

	dma_sync_single_for_device(shm_list->dev, phys_start,
				size, DMA_TO_DEVICE);
	dma_sync_single_for_cpu(shm_list->dev, phys_start,
				size, DMA_FROM_DEVICE);
}
EXPORT_SYMBOL(VPP_MEM_FlushCache);

int VPP_MEM_MapMemory(VPP_MEM *shm_handle, struct vm_area_struct *vma)
{
	size_t size;

	shm_handle->u_addr = (void *)vma->vm_start;
	size = vma->vm_end - vma->vm_start;
	if (remap_pfn_range(vma,
				vma->vm_start,
				(((phys_addr_t)shm_handle->p_addr) >> PAGE_SHIFT) +
				vma->vm_pgoff, size,
				vma->vm_page_prot)) {
		pr_err("%s: FAILED ..!!!\n", __func__);
		return VPP_MEM_ERROR_TYPE_EAGAIN;
	}
	return 0;
}
EXPORT_SYMBOL(VPP_MEM_MapMemory);

void * VPP_MEM_PhysToVirt(VPP_MEM_LIST *shm_list, phys_addr_t pa)
{
	void *va = NULL;
	VPP_MEM_TYPE shm_type;
	VPP_MEM *curr, *next;

	if (unlikely(!shm_list->dev))
		return NULL;

	for (shm_type = VPP_MEM_TYPE_FIRST;
			shm_type < VPP_MEM_TYPE_MAX; shm_type++) {
		VPP_MEM_NODE *shm_node = &shm_list->shm_node[shm_type];
		mutex_lock(&shm_node->shm_mutex);
		list_for_each_entry_safe(curr, next,
			&shm_node->shm_list, head) {
			if ((pa < curr->p_addr + curr->size) && (pa >= curr->p_addr)) {
				va = curr->k_addr + (pa - curr->p_addr);
				mutex_unlock(&shm_node->shm_mutex);
				break;
			}
		}
		mutex_unlock(&shm_node->shm_mutex);
	}

	return va;
}
EXPORT_SYMBOL(VPP_MEM_PhysToVirt);

MODULE_IMPORT_NS(DMA_BUF);
MODULE_LICENSE("GPL");
