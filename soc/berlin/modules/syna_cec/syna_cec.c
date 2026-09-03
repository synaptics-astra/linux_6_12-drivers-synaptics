// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2025 Synaptics Incorporated */

#define pr_fmt(fmt) "[cec kernel driver]" fmt

#include <linux/init.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/mod_devicetable.h>
#include <linux/of.h>
#include <linux/semaphore.h>
#include <linux/uaccess.h>
#include <linux/mm.h>
#include <media/cec.h>
#include <media/cec-notifier.h>
#include "syna_cec.h"
#include "syna_cec_prv.h"
#include "kernel_compatibility.h"

#define CEC_DEVICE_NAME     "syna_cec"

/* Bus sample interval for software signal-free-time enforcement. */
#define CEC_BUS_CHECK_INTERVAL_US	1000

/*
 * Max retry budget for finding a clean SFT window on a busy bus.
 * CEC bit time is 2.4ms, max SFT is 7 bit periods (~16.8ms).
 * A single CEC frame can occupy ~45ms. 20 retries at worst-case
 * SFT=7 gives ~360ms total budget, enough to go through at
 * least 7 full CEC frames of contention before giving up.
 */
#define CEC_SFT_RETRY_CNT		20

/* CEC device logical address mapping - based on Spec HDMI 1.3a */
#define CEC_LOG_ADDR_TYPE_RESERVED 7
const char CEC_devLogAddrMap[] =
{
	CEC_LOG_ADDR_TYPE_TV,           // 0
	CEC_LOG_ADDR_TYPE_RECORD,       // 1
	CEC_LOG_ADDR_TYPE_RECORD,       // 2
	CEC_LOG_ADDR_TYPE_TUNER,        // 3
	CEC_LOG_ADDR_TYPE_PLAYBACK,     // 4
	CEC_LOG_ADDR_TYPE_AUDIOSYSTEM,  // 5
	CEC_LOG_ADDR_TYPE_TUNER,        // 6
	CEC_LOG_ADDR_TYPE_TUNER,        // 7
	CEC_LOG_ADDR_TYPE_PLAYBACK,     // 8
	CEC_LOG_ADDR_TYPE_RECORD,       // 9
	CEC_LOG_ADDR_TYPE_TUNER,        // 10 // 1.3a
	CEC_LOG_ADDR_TYPE_PLAYBACK,     // 11 // 1.3a
	CEC_LOG_ADDR_TYPE_RESERVED,     // 12
	CEC_LOG_ADDR_TYPE_RESERVED,     // 13
	CEC_LOG_ADDR_TYPE_SPECIFIC,     // 14 (since free use addr is also for TV)
};

/*******************************************************************************
  Module internal function
  */

/******************************************************************************
 * FUNCTION : Finds device type for the given logical address
 * PARAMS   : logAddr   - Logical address
 * RETURN   : Device type corresponding to the logical address
 *****************************************************************************/
static int cec_get_device_type_from_addr (int logAddr)
{
	if (logAddr > CEC_MAX_LOG_ADDR)
		return -EINVAL;

	return (CEC_devLogAddrMap[logAddr]);
}

static irqreturn_t syna_cec_irq_thread_handler(int irq, void *data)
{
	struct cec_device_t *cec = (struct cec_device_t *)data;

	if (cec->tx_done) {
		cec_transmit_attempt_done(cec->adap, cec->tx_status);
		cec->tx_done = false;
	}
	if (cec->rx_done) {
		struct cec_msg msg = {};

		msg.len = cec->rx_buf_cnt;
		memcpy(msg.msg, cec->rx_buf, msg.len);
#ifdef CEC_PLATFORM_DEBUG
		pr_info("CEC RX data length (%d) data : %*phC\n",
			cec->rx_buf_cnt, cec->rx_buf_cnt, cec->rx_buf);
#endif
		cec_received_msg(cec->adap, &msg);
		cec->rx_done = false;
		cec->rx_buf_cnt = 0;
	}
	return IRQ_HANDLED;
}

static irqreturn_t cec_devices_isr(int irq, void *data)
{
	struct cec_device_t *cec = (struct cec_device_t *)data;
	unsigned short reg;
	int sts_info;
	int intr;
	int i;
	u8 dptr_len;
	u8 value;
	irqreturn_t ret = IRQ_HANDLED;

	// Read CEC status register
	syna_cec_reg_read(cec, CEC_INTR_STATUS0_REG_ADDR, &value, 1);
	reg = (unsigned short) value;
	syna_cec_reg_read(cec, CEC_INTR_STATUS1_REG_ADDR, &value, 1);
	reg |= ((unsigned short) value << 8);

#ifdef CEC_PLATFORM_DEBUG
	pr_info("%s: intr reg (0x%x)\n", __func__, reg);
#endif
	if (reg & SYNA_CEC_INTR_TX_FAIL) {
		intr = SYNA_CEC_INTR_TX_FAIL;
		syna_cec_get_fail_status(cec, SYNA_CEC_MODE_TX, &sts_info, reg);
		cec->tx_status = sts_info;
		value = 0;
		syna_cec_reg_write(cec, CEC_RDY_ADDR, &value, 1);
		syna_cec_reg_read(cec, CEC_INTR_ENABLE0_REG_ADDR, &value, 1);
		value &= ~(intr & 0x00ff);
		syna_cec_reg_write(cec, CEC_INTR_ENABLE0_REG_ADDR, &value, 1);
		cec->tx_done = true;
		ret = IRQ_WAKE_THREAD;
	}
	if (reg & SYNA_CEC_INTR_TX_COMPLETE) {
		intr = SYNA_CEC_INTR_TX_COMPLETE;
		value = 0;
		syna_cec_reg_write(cec, CEC_RDY_ADDR, &value, 1);
		syna_cec_reg_read(cec, CEC_INTR_ENABLE0_REG_ADDR, &value, 1);
		value &= ~(intr & 0x00ff);
		syna_cec_reg_write(cec, CEC_INTR_ENABLE0_REG_ADDR, &value, 1);
		cec->tx_done = true;
		cec->tx_status = CEC_TX_STATUS_OK;
		ret = IRQ_WAKE_THREAD;
	}

	if (reg & SYNA_CEC_INTR_RX_FAIL) {
		cec->rx_done = false;
		cec->rx_buf_cnt = 0;
		/*
		 * Full RX path reset: disable then re-enable RX mode.
		 * This resets the FIFO, re-enables RX ready, and
		 * re-enables RX interrupts (including RX_FAIL).
		 */
		syna_cec_set_mode(cec, SYNA_CEC_MODE_RX, false);
		syna_cec_set_mode(cec, SYNA_CEC_MODE_RX, true);
	}
	if (reg & SYNA_CEC_INTR_RX_COMPLETE) {
		intr = SYNA_CEC_INTR_RX_COMPLETE;
		syna_cec_reg_read(cec, CEC_INTR_ENABLE0_REG_ADDR, &value, 1);
		value &= ~(intr & 0x00ff);
		syna_cec_reg_write(cec, CEC_INTR_ENABLE0_REG_ADDR, &value, 1);
		// read cec mesg from rx buffer
		syna_cec_reg_read(cec, CEC_RX_FIFO_DPTR, &dptr_len, 1);
		/*
		 * Validate FIFO data length before reading. The hardware
		 * can report a corrupted length after bus errors or
		 * glitches, guard against buffer overflow and reset
		 * the RX path.
		 */
		if (dptr_len == 0 || dptr_len > CEC_MAX_MSG_SIZE) {
			pr_warn("%s: RX FIFO invalid length (%d), resetting\n",
				__func__, dptr_len);
			syna_cec_set_mode(cec, SYNA_CEC_MODE_RX, false);
			syna_cec_set_mode(cec, SYNA_CEC_MODE_RX, true);
			goto rx_exit;
		}

		cec->rx_buf_cnt = dptr_len;
		value = 0x01;
		for (i = 0; i < dptr_len; i++) {
			syna_cec_reg_read(cec, CEC_RX_BUF_READ_REG_ADDR, &cec->rx_buf[i], 1);
			syna_cec_reg_write(cec, CEC_TOGGLE_FOR_READ_REG_ADDR, &value, 1);
		}
		value = 0;
		syna_cec_reg_write(cec, CEC_RX_RDY_ADDR, &value, 1);
		value = 1;
		syna_cec_reg_write(cec, CEC_RX_RDY_ADDR, &value, 1);
		syna_cec_reg_read(cec, CEC_INTR_ENABLE0_REG_ADDR, &value, 1);
		value |= (intr & 0x00ff);
		syna_cec_reg_write(cec, CEC_INTR_ENABLE0_REG_ADDR, &value, 1);
		cec->rx_done = true;
		ret = IRQ_WAKE_THREAD;
	}

rx_exit:
	return ret;
}

#ifdef CONFIG_PM_SLEEP
static int cec_suspend(struct device *dev)
{
	struct cec_device_t *cec_dev = dev_get_drvdata(dev);

	pr_info("cec_suspend\n");

	cancel_work_sync(&cec_dev->tx_work);
	hrtimer_cancel(&cec_dev->sft_timer);
	disable_irq_nosync(cec_dev->cec_irq);

	return 0;
}

static int cec_resume(struct device *dev)
{
	struct cec_device_t *cec_dev = dev_get_drvdata(dev);

	pr_info("cec_resume\n");

	enable_irq(cec_dev->cec_irq);

	return 0;
}
#endif

static int syna_cec_adap_log_addr(struct cec_adapter *adap, u8 logical_addr)
{
	struct cec_device_t *cec_dev = adap->priv;
	int i;

#ifdef CEC_PLATFORM_DEBUG
	pr_info("%s log addr (%d)\n", __func__, logical_addr);
#endif
	if (logical_addr == CEC_LOG_ADDR_INVALID) {
		for (i = 0; i < 5; i++)
			syna_cec_enable_log_addr(cec_dev, false, i, 0);
		return 0;
	}

	syna_cec_enable_log_addr(cec_dev, true,
				 cec_get_device_type_from_addr(logical_addr),
				 logical_addr);
	return 0;
}

static int syna_cec_adap_enable(struct cec_adapter *adap, bool enable)
{
	struct cec_device_t *cec_dev = adap->priv;
	if (enable) {
		syna_cec_load_default_val(cec_dev);
	} else {
		syna_cec_set_mode (cec_dev, SYNA_CEC_MODE_TX, false);
		syna_cec_set_mode (cec_dev, SYNA_CEC_MODE_RX, false);
	}
	return 0;
}

/*
 * hrtimer callback (hardirq context). Samples the CEC line once
 * per tick and counts consecutive idle samples. A busy sample
 * resets the count (sliding window). Signals completion only
 * after the required consecutive idle samples,  i.e. the line
 * stayed idle for the full signal-free time window.
 */
static enum hrtimer_restart syna_cec_sft_timer_callback(struct hrtimer *timer)
{
	struct cec_device_t *cec_dev =
		container_of(timer, struct cec_device_t, sft_timer);

	/*
	 * readb_relaxed() from hardirq is safe here: CEC_LINE_STATUS_REG is a
	 * read-only status register and no spinlock protects it that could be
	 * held by a preempted context.
	 */
	if (syna_cec_rx_line_status(cec_dev) & 0x01) {
		/* Line idle: count consecutive idle samples. */
		if (atomic_inc_return(&cec_dev->sft_poll_cnt) >=
		    atomic_read(&cec_dev->sft_required_polls)) {
			complete(&cec_dev->sft_completion);
			return HRTIMER_NORESTART;
		}
	} else {
		/* Line busy: restart the consecutive-idle count. */
		atomic_set(&cec_dev->sft_poll_cnt, 0);
	}

	hrtimer_forward_now(timer,
			    ns_to_ktime(CEC_BUS_CHECK_INTERVAL_US * 1000UL));
	return HRTIMER_RESTART;
}

/*
 * syna_cec_wait_free_time - Wait for CEC bus idle for the required SFT.
 *
 * Uses hrtimer + completion to sample the CEC line without blocking
 * the CEC framework from processing RX messages. The hrtimer callback
 * runs in hardirq context independently of the kthread.
 *
 * Returns 0 on success, -EBUSY if the bus never stayed idle
 * long enough within the retry budget.
 */
static int syna_cec_wait_free_time(struct cec_device_t *cec_dev,
				   u32 signal_free_time)
{
	int required_polls;
	unsigned int timeout_us;

	/* Consecutive idle samples needed (one per CEC_BUS_CHECK_INTERVAL_US). */
	required_polls = DIV_ROUND_UP(CEC_FREE_TIME_TO_USEC(signal_free_time),
				      CEC_BUS_CHECK_INTERVAL_US);

	/*
	 * Total budget to find a clean signal-free window.
	 * See CEC_SFT_RETRY_CNT for the derivation.
	 */
	timeout_us = (CEC_FREE_TIME_TO_USEC(signal_free_time) +
		      CEC_BUS_CHECK_INTERVAL_US) * CEC_SFT_RETRY_CNT;

	atomic_set(&cec_dev->sft_poll_cnt, 0);
	atomic_set(&cec_dev->sft_required_polls, required_polls);
	reinit_completion(&cec_dev->sft_completion);

	hrtimer_start(&cec_dev->sft_timer,
		      ns_to_ktime(CEC_BUS_CHECK_INTERVAL_US * 1000UL),
		      HRTIMER_MODE_REL);

	/*
	 * The callback samples continuously, counting consecutive idle ticks
	 * and resetting on any busy sample. Completion = required_polls
	 * consecutive idle (full SFT window clean). Timeout = the bus never
	 * stayed idle long enough within the budget.
	 */
	if (wait_for_completion_timeout(&cec_dev->sft_completion,
					usecs_to_jiffies(timeout_us))) {
		hrtimer_cancel(&cec_dev->sft_timer);
		return 0;
	}

	hrtimer_cancel(&cec_dev->sft_timer);

	return -EBUSY;
}

/*
 * Deferred TX: runs SFT wait + FIFO write in process context.
 *
 * adap_transmit() is called by the CEC core kthread with
 * adap->lock held. The SFT wait (up to ~360ms) must not run
 * there, it would block the threaded IRQ handler from
 * delivering RX and reporting TX completion (dropping messages
 * and breaking ARC). adap_transmit() returns immediately after
 * queuing this work, releasing adap->lock.
 */
static void syna_cec_tx_work(struct work_struct *work)
{
	struct cec_device_t *cec_dev =
		container_of(work, struct cec_device_t, tx_work);

	if (syna_cec_wait_free_time(cec_dev, cec_dev->tx_signal_free_time)) {
		pr_warn("%s: CEC bus busy, signal-free time not satisfied\n",
			__func__);
		cec_transmit_attempt_done(cec_dev->adap, CEC_TX_STATUS_ARB_LOST);
		return;
	}

	/* TX completion (OK/FAIL) is reported later by the threaded IRQ. */
	syna_cec_transmit_data(cec_dev, &cec_dev->tx_msg,
			       cec_dev->tx_signal_free_time);
}


static int syna_cec_adap_transmit(struct cec_adapter *adap, u8 attempts,
				   u32 signal_free_time, struct cec_msg *msg)
{
	struct cec_device_t *cec_dev = adap->priv;

#ifdef CEC_PLATFORM_DEBUG
	pr_info("%s: length (%d), SF time (%d), data: %*phC\n",
		__func__, msg->len, signal_free_time, msg->len, msg->msg);
#endif
	/*
	 * CEC CTS 9.6: after arbitration loss, the device is a new
	 * initiator (SFT >= 5), not a retransmitter (SFT >= 3).
	 */
	if (msg->tx_arb_lost_cnt && signal_free_time < 5)
		signal_free_time = 5;

	/*
	 * return immediately so the core releases adap->lock.
	 * The SFT wait + FIFO write happen in tx_work without the lock held.
	 */
	cec_dev->tx_msg = *msg;
	cec_dev->tx_signal_free_time = signal_free_time;
	queue_work(cec_dev->tx_wq, &cec_dev->tx_work);
	return 0;
}

static const struct cec_adap_ops syna_cec_ops = {
	.adap_enable = syna_cec_adap_enable,
	.adap_log_addr = syna_cec_adap_log_addr,
	.adap_transmit = syna_cec_adap_transmit,
};

static int cec_probe(struct platform_device *pdev)
{
	int res;
	struct resource *cec_resource;
	struct cec_device_t *cec_dev;
	struct device *hdmi_dev;
	struct device_node *sub_node, *hdmi_node;

	hdmi_dev = cec_notifier_parse_hdmi_phandle(&pdev->dev);

	if (IS_ERR(hdmi_dev)) {
		pr_err("%s:%d ERROR : %ld\n", __func__, __LINE__, PTR_ERR(hdmi_dev));
		return PTR_ERR(hdmi_dev);
	}
	hdmi_node = hdmi_dev->of_node;

	if (!hdmi_node) {
		pr_err("HDMI device node not found\n");
		return -ENODEV;
	}

	sub_node = of_get_child_by_name(hdmi_node, "hdmi_tx");
	if (!sub_node) {
		pr_err("Subnode 'hdmi_tx' not found\n");
		return -ENODEV;
	}

	// Check if the subnode is enabled (status="okay" or property absent)
	if (!of_device_is_available(sub_node)) {
		pr_err("Subnode 'hdmi_tx' is disabled in DTS. Skipping initialization.\n");
		of_node_put(sub_node); // Release the node reference
		return -ENODEV; // Or handle as appropriate for your driver
	}
	of_node_put(sub_node);

	cec_dev = devm_kzalloc(&pdev->dev, sizeof(struct cec_device_t), GFP_KERNEL);
	if (!cec_dev)
		return -ENOMEM;

	platform_set_drvdata(pdev, cec_dev);

	cec_dev->cec_irq = platform_get_irq(pdev, 0);
	if (cec_dev->cec_irq < 0)
		return cec_dev->cec_irq;

	cec_resource = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	cec_dev->cec_virt_addr = devm_ioremap_resource(&pdev->dev, cec_resource);
	if (IS_ERR(cec_dev->cec_virt_addr)) {
		res = PTR_ERR(cec_dev->cec_virt_addr);
		goto err_ioremap;
	}

	cec_dev->dev = &pdev->dev;

	/*
	 * IRQF_ONESHOT keeps the CEC IRQ masked until the threaded handler
	 * finishes. Without it the hardirq can re-fire while the threaded
	 * handler is still running and overwrite the shared rx_buf/rx_done/
	 * tx_done state, dropping RX messages under load (breaks ARC). The
	 * mask window stays short because the SFT wait runs in tx_work, not
	 * in the threaded handler.
	 */
	res = devm_request_threaded_irq(&pdev->dev, cec_dev->cec_irq,
		cec_devices_isr, syna_cec_irq_thread_handler,
		IRQF_ONESHOT, "cec_irq", (void *)cec_dev);

	if (res) {
		dev_err(&pdev->dev,
			"Unable to request interrupt for device\n");
		goto err_irq;
	}

	cec_dev->adap = cec_allocate_adapter(&syna_cec_ops, cec_dev, CEC_DEVICE_NAME,
			CEC_CAP_DEFAULTS | CEC_CAP_MONITOR_ALL |
			CEC_CAP_CONNECTOR_INFO | CEC_CAP_PHYS_ADDR,
			CEC_MAX_LOG_ADDRS);
	if (IS_ERR(cec_dev->adap)) {
		res = PTR_ERR(cec_dev->adap);
		dev_err(&pdev->dev, "Couldn't create cec adapter\n");
		goto err_adapter_alloc;
	}

	cec_dev->notifier = cec_notifier_cec_adap_register(hdmi_dev, NULL,
							   cec_dev->adap);
	if (!cec_dev->notifier) {
		res = -ENOMEM;
		goto err_notifier;
	}

	hrtimer_init(&cec_dev->sft_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	cec_dev->sft_timer.function = syna_cec_sft_timer_callback;
	init_completion(&cec_dev->sft_completion);

	cec_dev->tx_wq = alloc_ordered_workqueue("syna_cec_tx", WQ_HIGHPRI);
	if (!cec_dev->tx_wq) {
		res = -ENOMEM;
		goto err_wq;
	}
	INIT_WORK(&cec_dev->tx_work, syna_cec_tx_work);

	res = cec_register_adapter(cec_dev->adap, &pdev->dev);
	if (res) {
		dev_err(&pdev->dev, "Couldn't register device\n");
		goto err_register_adapter;
	}

	return 0;

err_register_adapter:
	destroy_workqueue(cec_dev->tx_wq);
err_wq:
	cec_notifier_cec_adap_unregister(cec_dev->notifier, cec_dev->adap);
err_notifier:
	cec_delete_adapter(cec_dev->adap);
err_adapter_alloc:
err_irq:
err_ioremap:
	pr_err("cec_probe failed !!! (%d)\n", res);
	return res;
}

static RET_TYPE cec_remove(struct platform_device *pdev)
{
	struct cec_device_t *cec_dev = platform_get_drvdata(pdev);

	/*
	 * Unregister adapter first, stops the CEC kthread so no new
	 * tx_work is queued. Then flush pending work and cancel the timer.
	 */
	cec_notifier_cec_adap_unregister(cec_dev->notifier, cec_dev->adap);
	cec_unregister_adapter(cec_dev->adap);

	cancel_work_sync(&cec_dev->tx_work);
	hrtimer_cancel(&cec_dev->sft_timer);
	destroy_workqueue(cec_dev->tx_wq);
	RETURN_VALUE;
}

static const struct of_device_id cec_match[] = {
	{.compatible = "syna,berlin-cec",},
	{},
};
MODULE_DEVICE_TABLE(of, cec_match);

static SIMPLE_DEV_PM_OPS(cec_pmops, cec_suspend,
			 cec_resume);

static struct platform_driver syna_cec_driver = {
	.probe = cec_probe,
	.remove = cec_remove,
	.driver = {
		   .name = CEC_DEVICE_NAME,
		   .of_match_table = cec_match,
		   .pm = &cec_pmops,
	},
};
module_platform_driver(syna_cec_driver);

MODULE_AUTHOR("synaptics");
MODULE_DESCRIPTION("cec module driver");
MODULE_LICENSE("GPL v2");
