// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2023 Synaptics Incorporated
 *
 */

#ifndef __VPP_MEM_H__
#define __VPP_MEM_H__

#include <linux/types.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/device.h>
#include <linux/dma-buf.h>
#include <linux/version.h>
#include "kernel_compatibility.h"

typedef enum __VPP_MEM_TYPE__ {
	VPP_MEM_TYPE_FIRST,
	/*CMA/Reserved*/
	VPP_MEM_TYPE_DMA = VPP_MEM_TYPE_FIRST,
	/*NonSecure-NonCached*/
	VPP_MEM_TYPE_NS_NC,
	VPP_MEM_TYPE_MAX
} VPP_MEM_TYPE;

typedef enum __VPP_MEM_ERROR_TYPE__ {
	VPP_MEM_ERROR_TYPE_FIRST,
	VPP_MEM_ERROR_TYPE_OK = VPP_MEM_ERROR_TYPE_FIRST,
	VPP_MEM_ERROR_TYPE_ENOMEM,
	VPP_MEM_ERROR_TYPE_EAGAIN,
	VPP_MEM_ERROR_TYPE_BADPARAM,
	VPP_MEM_ERROR_TYPE_NOENT,
	VPP_MEM_ERROR_TYPE_MAX
} VPP_MEM_ERROR_TYPE;

typedef struct __VPP_MEM_NODE__ {
	int shm_cnt;
	struct list_head shm_list;
	struct mutex shm_mutex;
	VPP_MEM_TYPE shm_type;
	struct device *dev; /*device file for which memory allocated*/
} VPP_MEM_NODE;

struct __VPP_MEM_OPS__;
typedef struct __VPP_MEM_LIST__ {
	struct __VPP_MEM_OPS__ *ops;
	VPP_MEM_NODE shm_node[VPP_MEM_TYPE_MAX];
	struct device *dev; /*device file for which memory allocated*/
} VPP_MEM_LIST;

typedef struct __VPP_MEM__ {
	struct list_head head;
	void *handle;       /*Handle to memory allocated*/
	void *k_addr;       /*kernel space address, by AllocateMemory*/
	void *u_addr;       /*user space address, by mmap()*/
	phys_addr_t p_addr;       /*physical address, by AllocateMemory*/
	size_t size;        /*size of the memory allocated*/
	struct iosys_map  map;

#ifdef CONFIG_DMABUF_HEAPS
	struct dma_buf_attachment *attach;
	struct sg_table *table;
#endif //CONFIG_DMABUF_HEAPS
	void *teeShm;                //handle to mem registered with tee
} VPP_MEM;

typedef struct __VPP_MEM_OPS__ {
	int (*VPP_MEM_IsReady)(void);
	int (*VPP_MEM_InitMemory)(VPP_MEM_LIST *shm_list);
	void (*VPP_MEM_DeInitMemory)(VPP_MEM_LIST *shm_list);
	int (*VPP_MEM_AllocateMemory)(VPP_MEM_NODE *shm_node,
				VPP_MEM *shm_handle, gfp_t flags);
	void (*VPP_MEM_FreeMemory)(VPP_MEM *shm_handle);
} VPP_MEM_OPS;


int VPP_MEM_IsReady(void);
void VPP_MEM_SetAmplessBoot(int ampless_boot);
int VPP_MEM_IsAmplessBoot(void);
void VPP_MEM_probe(VPP_MEM_LIST *shm_list);
int VPP_MEM_InitMemory(VPP_MEM_LIST *shm_list);
void VPP_MEM_DeInitMemory(VPP_MEM_LIST *shm_list);
int VPP_MEM_AllocateMemory(VPP_MEM_LIST *shm_list, VPP_MEM_TYPE shm_type,
				VPP_MEM *shm_handle, gfp_t flags);

int VPP_MEM_FreeMemory(VPP_MEM_LIST *shm_list, VPP_MEM_TYPE shm_type,
				VPP_MEM *shm_handle);
void VPP_MEM_FlushCache(VPP_MEM_LIST *shm_list, VPP_MEM *shm_handle,
				unsigned int size);
int VPP_MEM_MapMemory(VPP_MEM *shm_handle, struct vm_area_struct *vma);
void *VPP_MEM_PhysToVirt(VPP_MEM_LIST *shm_list, phys_addr_t pa);
int VPP_DMABUF_IsReady(void);
int VPP_DMABUF_AllocMem(VPP_MEM_NODE *shm_node, VPP_MEM *shm_handle, gfp_t flags);
int VPP_DMABUF_InitMemory(VPP_MEM_LIST *shm_list);
void VPP_DMABUF_DeinitMemory(VPP_MEM_LIST *shm_list);

#endif //__VPP_MEM_H__
