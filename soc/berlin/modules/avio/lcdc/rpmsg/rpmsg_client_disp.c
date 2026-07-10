// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2026 Synaptics Incorporated
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/rpmsg.h>
#include <linux/err.h>
#include "rpmsg_client_disp.h"
#include <avio_core.h>

/* RPmsg service name for display configuration */
#define RPMSG_DISP_SERVICE_NAME    "rpmsg-disp"

/* RPmsg device instance (global, set during probe) */
static struct rpmsg_device *g_rpmsg_disp_dev;

/* Callback handler for responses from MCU */
static int rpmsg_disp_rpmsg_cb(struct rpmsg_device *rpdev, void *data,
			      int len, void *priv, u32 src)
{
	rpmsg_disp_cfg_msg_t *msg = (rpmsg_disp_cfg_msg_t *)data;

	if (len != sizeof(rpmsg_disp_cfg_msg_t)) {
		dev_err(&rpdev->dev, "Invalid message size: %d\n", len);
		return -EINVAL;
	}

	switch (msg->msg_type) {
	case RPMSG_DISP_MSG_ACK:
		dev_info(&rpdev->dev, "MCU: display config applied successfully (%ux%u)\n",
			 msg->xres, msg->yres);
		/* Notify AVIO that MCU confirmed low-power display capability */
		avio_lcdc_set_lp_enabled(true);
		break;
	case RPMSG_DISP_MSG_ERROR:
		dev_warn(&rpdev->dev, "MCU: failed to apply display config (error: 0x%x)\n",
			msg->status);
		/*
		 * Do NOT clear the confirmed flag. A message-level error does not
		 * mean MCU lost seamless-switch capability. If it was never
		 * confirmed it stays 0; callers fall back to normal power paths.
		 */
		break;
	default:
		dev_warn(&rpdev->dev, "Unknown message type: 0x%x\n", msg->msg_type);
		break;
	}

	return 0;
}

/* Probe function - called when RPmsg device is connected */
static int rpmsg_disp_rpmsg_probe(struct rpmsg_device *rpdev)
{
	dev_info(&rpdev->dev, "RPMsg display client probed (0x%x -> 0x%x)\n",
		 rpdev->src, rpdev->dst);

	/* Store device handle for later use */
	g_rpmsg_disp_dev = rpdev;

	return 0;
}

/* Remove function */
static void rpmsg_disp_rpmsg_remove(struct rpmsg_device *rpdev)
{
	dev_info(&rpdev->dev, "RPMsg display client removed\n");
	g_rpmsg_disp_dev = NULL;
	/* Reset so a re-probe must re-confirm capability before callers trust it */
	avio_lcdc_set_lp_enabled(false);
}

/* Device ID table - must match service name announced by MCU */
static const struct rpmsg_device_id rpmsg_disp_id_table[] = {
	{ .name = RPMSG_DISP_SERVICE_NAME },
	{ },
};
MODULE_DEVICE_TABLE(rpmsg, rpmsg_disp_id_table);

/* RPmsg driver structure */
static struct rpmsg_driver rpmsg_client_disp_drv = {
	.drv.name   = "rpmsg_client_disp",
	.id_table   = rpmsg_disp_id_table,
	.probe      = rpmsg_disp_rpmsg_probe,
	.callback   = rpmsg_disp_rpmsg_cb,
	.remove     = rpmsg_disp_rpmsg_remove,
};

int rpmsg_disp_client_register(void)
{
	return register_rpmsg_driver(&rpmsg_client_disp_drv);
}

void rpmsg_disp_client_unregister(void)
{
	unregister_rpmsg_driver(&rpmsg_client_disp_drv);
}

/* PUBLIC: Send display config to MCU via RPmsg */
int rpmsg_disp_send_msg(const rpmsg_disp_cfg_msg_t *disp_msg)
{
	int ret;

	if (!g_rpmsg_disp_dev) {
		pr_warn("Display RPmsg device not available\n");
		return -ENODEV;
	}

	if (!disp_msg) {
		pr_err("Invalid display config pointer\n");
		return -EINVAL;
	}

	ret = rpmsg_send(g_rpmsg_disp_dev->ept, (void *)disp_msg, sizeof(*disp_msg));

	if (ret)
		dev_err(&g_rpmsg_disp_dev->dev, "Failed to send display config: %d\n", ret);
	else
		dev_dbg(&g_rpmsg_disp_dev->dev, "Display config sent: %ux%u\n", disp_msg->xres, disp_msg->yres);

	return ret;
}

MODULE_DESCRIPTION("Synaptics RPMsg display config client");
MODULE_LICENSE("GPL");
