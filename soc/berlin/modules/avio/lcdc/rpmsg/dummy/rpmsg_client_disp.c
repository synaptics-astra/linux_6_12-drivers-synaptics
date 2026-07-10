// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Synaptics Incorporated
 */

#include "rpmsg_client_disp.h"

/*
 * Stub implementation when the RPMsg display client is not reachable.
 * Returns -ENODEV to indicate the service is not available.
 */
int rpmsg_disp_send_msg(const rpmsg_disp_cfg_msg_t *disp_msg)
{
	(void)disp_msg;
	return -ENODEV;
}

int rpmsg_disp_client_register(void)
{
	return 0;
}

void rpmsg_disp_client_unregister(void)
{
}
