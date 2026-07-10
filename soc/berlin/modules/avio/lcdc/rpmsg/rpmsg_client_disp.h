// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Synaptics Incorporated
 */

#ifndef __RPMSG_CLIENT_DISP_H__
#define __RPMSG_CLIENT_DISP_H__

#include <linux/types.h>
#include <linux/errno.h>

/* Message types */
typedef enum {
	RPMSG_DISP_MSG_CONFIG = 0x01,    /* Linux -> MCU: panel display config */
	RPMSG_DISP_MSG_ACK = 0x02,       /* MCU -> Linux: config acknowledgement */
	RPMSG_DISP_MSG_ERROR = 0x03      /* MCU -> Linux: config apply error */
} rpmsg_disp_msg_type_t;

/* Display configuration message exchanged over RPMsg */
typedef struct {
	uint32_t msg_type;
	uint32_t xres;
	uint32_t yres;
	uint32_t orientation;
	uint32_t status;
} rpmsg_disp_cfg_msg_t;

/* Full declarations for RPMsg display client */
int rpmsg_disp_send_msg(const rpmsg_disp_cfg_msg_t *disp_msg);
int rpmsg_disp_client_register(void);
void rpmsg_disp_client_unregister(void);

#endif /* __RPMSG_CLIENT_DISP_H__ */
