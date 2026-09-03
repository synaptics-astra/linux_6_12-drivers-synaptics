// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2025 Synaptics Incorporated */

#ifndef _CEC_DRIVER_H_
#define _CEC_DRIVER_H_

#include <linux/cec.h>
#include <linux/cdev.h>
#include <linux/hrtimer.h>
#include <linux/completion.h>
#include <linux/workqueue.h>

//#define CEC_PLATFORM_DEBUG

#define CEC_CC_MSG_TYPE 1

#define CEC_ISR_MSGQ_SIZE   32

/* ioctl commands */
#define CEC_IOCTL_INTR_MSG  0xbeef0001
#define CEC_IOCTL_GET_MSG   0xbeef0002
#define CEC_IOCTL_RX_MSG_BUF    0xbeef0003

/* CEC interrupt status */
#define SYNA_CEC_INTR_NONE                0x0000
#define SYNA_CEC_INTR_TX_UNICAST_NOACK    0x0001
#define SYNA_CEC_INTR_TX_BRDCAST_NOACK    0x0002
#define SYNA_CEC_INTR_TX_COLLISION        0x0004
#define SYNA_CEC_INTR_TX_SFT_FAIL         0x2008
#define SYNA_CEC_INTR_TX_FAIL             0x200F
#define SYNA_CEC_INTR_TX_COMPLETE         0x0010
#define SYNA_CEC_INTR_RX_COMPLETE         0x0020
#define SYNA_CEC_INTR_RX_LOW_DT_ERROR     0x0040
#define SYNA_CEC_INTR_RX_HIGH_DT_ERROR    0x0080
#define SYNA_CEC_INTR_RX_FAIL             0x00C0
#define SYNA_CEC_INTR_TX_FIFO_FULL        0x0100
#define SYNA_CEC_INTR_TX_FIFO_EMPTY       0x0200
#define SYNA_CEC_INTR_RX_FIFO_FULL        0x0400
#define SYNA_CEC_INTR_RX_FIFO_EMPTY       0x0800
#define SYNA_CEC_INTR_WAKEUP              0x1000
#define SYNA_CEC_INTR_ALL                 (SYNA_CEC_INTR_TX_FAIL        |\
                                            SYNA_CEC_INTR_TX_COMPLETE   |\
                                            SYNA_CEC_INTR_RX_COMPLETE   |\
                                            SYNA_CEC_INTR_RX_FAIL       |\
                                            SYNA_CEC_INTR_TX_FIFO_FULL  |\
                                            SYNA_CEC_INTR_TX_FIFO_EMPTY |\
                                            SYNA_CEC_INTR_RX_FIFO_FULL  |\
                                            SYNA_CEC_INTR_RX_FIFO_EMPTY |\
                                            SYNA_CEC_INTR_WAKEUP)
#define SYNA_CEC_INTR_RX_TX_STAT          (SYNA_CEC_INTR_TX_FAIL |\
                                            SYNA_CEC_INTR_TX_COMPLETE |\
                                            SYNA_CEC_INTR_RX_COMPLETE |\
                                            SYNA_CEC_INTR_RX_FAIL)

// --  CEC
#define CEC_TOGGLE_FOR_WRITE_REG_ADDR       0x0000
#define CEC_TOGGLE_FOR_READ_REG_ADDR        0x0004
#define CEC_RDY_ADDR                        0x0008
#define CEC_RX_RDY_ADDR                     0x000c
#define CEC_TX_FIFO_RESET_ADDR              0x0010
#define CEC_RX_FIFO_RESET_ADDR              0x0014
#define CEC_PMODE_ADDR                      0x0018
#define CEC_TX_TYPE_ADDR                    0x0020
#define CEC_SIGNAL_FREE_TIME_0_ADDR         0x0028
#define CEC_SIGNAL_FREE_TIME_1_ADDR         0x0029
#define CEC_SIGNAL_FREE_TIME_2_ADDR         0x002a
#define CEC_SIGNAL_FREE_TIME_3_ADDR         0x002b
#define CEC_START_BIT_LO_THRESH_0_ADDR      0x002c
#define CEC_START_BIT_LO_THRESH_1_ADDR      0x002d
#define CEC_START_BIT_LO_THRESH_2_ADDR      0x002e
#define CEC_START_BIT_LO_THRESH_3_ADDR      0x002f
#define CEC_START_BIT_HI_THRESH_0_ADDR      0x0030
#define CEC_START_BIT_HI_THRESH_1_ADDR      0x0031
#define CEC_START_BIT_HI_THRESH_2_ADDR      0x0032
#define CEC_START_BIT_HI_THRESH_3_ADDR      0x0033
#define CEC_DATA_BIT_0_LO_THRESH_0_ADDR     0x0034
#define CEC_DATA_BIT_0_LO_THRESH_1_ADDR     0x0035
#define CEC_DATA_BIT_0_LO_THRESH_2_ADDR     0x0036
#define CEC_DATA_BIT_0_LO_THRESH_3_ADDR     0x0037
#define CEC_DATA_BIT_1_LO_THRESH_0_ADDR     0x0038
#define CEC_DATA_BIT_1_LO_THRESH_1_ADDR     0x0039
#define CEC_DATA_BIT_1_LO_THRESH_2_ADDR     0x003a
#define CEC_DATA_BIT_1_LO_THRESH_3_ADDR     0x003b
#define CEC_DATA_BIT_0_HI_THRESH_0_ADDR     0x003c
#define CEC_DATA_BIT_0_HI_THRESH_1_ADDR     0x003d
#define CEC_DATA_BIT_0_HI_THRESH_2_ADDR     0x003e
#define CEC_DATA_BIT_0_HI_THRESH_3_ADDR     0x003f
#define CEC_DATA_BIT_1_HI_THRESH_0_ADDR     0x0040
#define CEC_DATA_BIT_1_HI_THRESH_1_ADDR     0x0041
#define CEC_DATA_BIT_1_HI_THRESH_2_ADDR     0x0042
#define CEC_DATA_BIT_1_HI_THRESH_3_ADDR     0x0043
#define CEC_SSP_ACK_TIME_0_ADDR             0x0044
#define CEC_SSP_ACK_TIME_1_ADDR             0x0045
#define CEC_SSP_ACK_TIME_2_ADDR             0x0046
#define CEC_SSP_ACK_TIME_3_ADDR             0x0047
#define CEC_INTR_ENABLE0_REG_ADDR           0x0048
#define CEC_INTR_ENABLE1_REG_ADDR           0x0049
#define CEC_DATA_REG_ADDR                   0x004c
#define CEC_EOM_REG_ADDR                    0x0050
#define CEC_INTR_STATUS0_REG_ADDR           0x0058
#define CEC_INTR_STATUS1_REG_ADDR           0x0059
#define CEC_NOMINAL_SAMPLE_TIME_0_ADDR      0x005c
#define CEC_NOMINAL_SAMPLE_TIME_1_ADDR      0x005d
#define CEC_NOMINAL_SAMPLE_TIME_2_ADDR      0x005e
#define CEC_NOMINAL_SAMPLE_TIME_3_ADDR      0x005f
#define CEC_HYST_TIME_0_ADDR                0x0060
#define CEC_HYST_TIME_1_ADDR                0x0061
#define CEC_HYST_TIME_2_ADDR                0x0062
#define CEC_HYST_TIME_3_ADDR                0x0063
#define CEC_FOLLOWER_ACK_TIME_0_ADDR        0x0064
#define CEC_FOLLOWER_ACK_TIME_1_ADDR        0x0065
#define CEC_FOLLOWER_ACK_TIME_2_ADDR        0x0066
#define CEC_FOLLOWER_ACK_TIME_3_ADDR        0x0067
#define CEC_RX_BUF_READ_REG_ADDR            0x0068
#define CEC_RX_EOM_READ_REG_ADDR            0x0069
#define CEC_LOGICAL_ADDR0_REG_ADDR          0x006a
#define CEC_LOGICAL_ADDR1_REG_ADDR          0x006b
#define CEC_LOGICAL_ADDR2_REG_ADDR          0x006c
#define CEC_LOGICAL_ADDR3_REG_ADDR          0x006d
#define CEC_LOGICAL_ADDR4_REG_ADDR          0x006e
#define CEC_JITTER_CNT_0_ADDR               0x0070
#define CEC_JITTER_CNT_1_ADDR               0x0071
#define CEC_JITTER_CNT_2_ADDR               0x0072
#define CEC_JITTER_CNT_3_ADDR               0x0073
#define CEC_LINE_STATUS_REG_ADDR            0x0074
#define CEC_TX_PRESENT_STATE_REG_ADDR       0x0078
#define CEC_RX_PRESENT_STATE_REG_ADDR       0x0079
#define CEC_COLL_CTRL_REG_ADDR              0x007a
#define CEC_COLL_WINDOW_TIME_REG_0_ADDR     0x007c
#define CEC_COLL_WINDOW_TIME_REG_1_ADDR     0x007d
#define CEC_COLL_WINDOW_TIME_REG_2_ADDR     0x007e
#define CEC_COLL_WINDOW_TIME_REG_3_ADDR     0x007f
#define CEC_TX_FIFO_FULL_THRESH             0x0080
#define CEC_TX_FIFO_WPTR                    0x0081
#define CEC_TX_FIFO_RPTR                    0x0082
#define CEC_TX_FIFO_DPTR                    0x0083
#define CEC_RX_FIFO_FULL_THRESH             0x0084
#define CEC_RX_FIFO_WPTR                    0x0085
#define CEC_RX_FIFO_RPTR                    0x0086
#define CEC_RX_FIFO_DPTR                    0x0087
#define CEC_JITTER_CNT_SB_0                 0x0088
#define CEC_JITTER_CNT_SB_1                 0x0089
#define CEC_JITTER_CNT_SB_2                 0x008a
#define CEC_JITTER_CNT_SB_3                 0x008b
#define CEC_ERR_NOTIF_TIME_0                0x008c
#define CEC_ERR_NOTIF_TIME_1                0x008d
#define CEC_ERR_NOTIF_TIME_2                0x008e
#define CEC_ERR_NOTIF_TIME_3                0x008f
#define CEC_GLITCH_FILT_W_L                 0x0090
#define CEC_GLITCH_FILT_W_H                 0x0091

struct cec_device_t {
	int isr_en_state;
	int cec_irq;
	atomic_t irq_stat;
	struct cec_adapter  *adap;
	void __iomem *cec_virt_addr;
	struct device *dev;
	struct clk *clk;
	struct cec_notifier *notifier;
	bool rx_done;
	bool tx_done;
	int tx_status;
	u8 rx_buf[CEC_MAX_MSG_SIZE];
	u8 rx_buf_cnt;
	u8 tx_buf[CEC_MAX_MSG_SIZE];
	u8 tx_buf_cur;
	u8 tx_buf_cnt;
	/* Software SFT enforcement */
	struct hrtimer sft_timer;
	struct completion sft_completion;
	atomic_t sft_poll_cnt;
	atomic_t sft_required_polls;
	/*
	 * Deferred transmit. adap_transmit() is called by the CEC core's
	 * kthread with adap->lock held, so it must return quickly: it stashes
	 * the message in tx_msg/tx_signal_free_time and queues tx_work. The
	 * whole SFT wait + FIFO write then runs in tx_work (process context,
	 * adap->lock released), so the wait never blocks RX delivery or TX
	 * completion, and the failure completion can call cec_transmit_*_done()
	 * without re-entering adap->lock.
	 */
	struct workqueue_struct *tx_wq;
	struct work_struct tx_work;
	struct cec_msg tx_msg;
	u32 tx_signal_free_time;
#ifdef CEC_HWSFT_DEBUG
	ktime_t tx_rdy_time;
	u32 tx_sft_us;
#endif
};


#endif  //_CEC_DRIVER_H_
