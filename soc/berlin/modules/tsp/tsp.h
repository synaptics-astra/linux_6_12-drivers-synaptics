// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2026 Synaptics Incorporated
 *
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/kdev_t.h>
#include <linux/device.h>
#include <linux/version.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <linux/mm.h>
#include <linux/clk.h>
#include <linux/semaphore.h>
#include <linux/cdev.h>
#include "kernel_compatibility.h"
#include <linux/firmware.h>
#include <linux/completion.h>
#include "drv_msg.h"

#define     TSP_ISR_MSGQ_SIZE                              32
#define     TSP_ISR_START                                  0x1
#define     TSP_ISR_STOP                                   0x2
#define     TSP_ISR_WAKEUP                                 0x3
/*
 * RA_TspReg_IntReg is 0xF344 on BG5CT
 * not sure if we need to support BG5CT on kernel 4.14
 */
#define     RA_TspReg_Figo1Dtcm                            0x20000
#define     RA_FigoSysBasic_FIGO0                          0xA800
#define     RA_FigoReg_figoRstn                            0x0028
#define     RA_TspReg_IntReg                               0xDB44
#define     RA_TspIntReg_software_int_enable               0x0028
#define     RA_TspIntReg_software_int_status               0x002C
#define     RA_TspIntReg_software_int_set                  0x0030

#define     Figo_STA_RESET                                 0x00
#define     Figo_STA_RELEASE                               0x01

#define     Figo_CMD_RUN                                   0x01
#define     Figo_CMD_STALL                                 0x02

#define     RA_Figo0_cmd                                   0x0018
#define     RA_Figo0_resp                                  0x001C

#define     RA_TspDtcmGlobal_LA                            0x00008
#define     RA_LocalArea_Ext_D0th_                         0x00000
#define     RA_LocalArea_Ext_D2th_                         0x00008

#define TSP_DEVICE_NAME			"tsp"
#define TSP_DEVICE_PATH			("/dev/" TSP_DEVICE_NAME)
#define TSP_MAX_DEVS            2
#define TSP_MINOR               0

#define TSP_IOCTL_CMD_MSG       _IOW('t', 1, int[2])
#define TSP_IOCTL_GET_MSG       _IOR('t', 2, CC_MSG_t)
#define TSP_IOCTL_DISABLE_INT   _IO('t', 3)
#define TSP_IOCTL_ENABLE_INT    _IO('t', 4)
#define TSP_IOCTL_SET_CLK_RATE  _IOW('t', 5, enum clk_setting)
#define TSP_IOCTL_LOAD_TSP_FW  _IOW('t', 6, int[2])

#define TSP_FIGO_NUM    2

struct tsp_context {
	struct clk *core;
	unsigned long default_rate;
	AMPMsgQ_t hTSPMsgQ;
	struct semaphore tsp_sem;
};

struct tsp_device_t {
	struct tsp_context TspCtx;
	struct device *dev;
	unsigned char *dev_name;
	struct cdev cdev;
	struct class *dev_class;
	int major;
	int minor;
};

struct tsp_firmware_info {
	void *data;
	size_t size;
	bool loaded;
	struct completion fw_completion;
};

enum clk_setting {
	TSP_CLK_LOW,
	TSP_CLK_NORMAL
};

enum {
	TSP_FW_LOAD = 9,
	TSP_SAVE_HW_CONTEXT = 0x10000,
	TSP_RESTORE_HW_CONTEXT,
	TSP_SET_FIGO_STATE,
	TSP_GET_FIGO_STATE
};

int tz_tsp_initialize(struct device *dev);
void tz_tsp_finalize(void);
int tz_tsp_save_hw_context(uint32_t figo_id);
int tz_tsp_restore_hw_context(uint32_t figo_id);
int tz_tsp_set_figo_state(uint32_t figo_id, uint32_t state);
int tz_tsp_get_figo_state(uint32_t figo_id, uint32_t *state);


#if !IS_ENABLED(CONFIG_OPTEE)
bool tz_get_tsp_ta_status(void);
#endif
int tz_tsp_load_firmware(struct device *dev, const struct tsp_firmware_info fw_info,
				int figo_id, int fw_idx, bool force_load);
