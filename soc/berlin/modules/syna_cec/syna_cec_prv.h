// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2025 Synaptics Incorporated */

#ifndef __SYNA_CEC_PRIV_H__
#define __SYNA_CEC_PRIV_H__

#include <linux/types.h>
#include "syna_cec.h"

/*-----------------------------------------------------------------------------
 * Macros and Constants
 *-----------------------------------------------------------------------------
 */
#define CEC_MAX_MSG_LEN 16
#define SOC_SM_CEC_BASE 0xF7FE1000
// Time in microseconds
#define CEC_RESPONSE_TIME           200000
#define CEC_SIGNAL_FREE_TIME        12000
#define CEC_START_BIT_0_LOW_TIME    3700
#define CEC_START_BIT_0_HI_TIME     800
#define CEC_DATA_BIT_0_LOW_TIME     1500
#define CEC_DATA_BIT_0_HI_TIME      900
#define CEC_DATA_BIT_1_LOW_TIME     600
#define CEC_DATA_BIT_1_HI_TIME      1800
#define CEC_SSP_ACK_TIME            1050
#define CEC_NOMINAL_SAMPLE_TIME     1050
#define CEC_FOLL_ACK_ASSRT_TIME     1500

#define CEC_FOLL_HYST_TIME          800
#define CEC_STARTBIT_JITTER_TIME    205
#define CEC_DATABIT_JITTER_TIME     357
#define CEC_ERR_GEN_TIME            3600 // 1.5 times data bit time

#define CEC_COLL_WINDOW_TIME        250
#define CEC_START_BIT_TIME          (CEC_START_BIT_0_LOW_TIME + CEC_START_BIT_0_HI_TIME)
#define CEC_DATA_BIT_TIME           (CEC_DATA_BIT_0_LOW_TIME  + CEC_DATA_BIT_0_HI_TIME)

// CEC FIFO Status
#define CEC_TX_FIFO_FULL            0
#define CEC_TX_FIFO_EMPTY           1
#define CEC_RX_FIFO_FULL            2
#define CEC_RX_FIFO_EMPTY           3

// Command queue size
#define CEC_MAX_FIFO_SIZE  32

// Broadcast logical address
#define CEC_BROADCAST_ADDR  0x0F

// Last logical address
#define CEC_MAX_LOG_ADDR    0x0F

/*-----------------------------------------------------------------------------
 * Enums
 *-----------------------------------------------------------------------------
 */
/* CEC queue commands */
typedef enum CEC_CMD_ID_T
{
	CEC_CMD_SET_PHYADDR = 0x00,
	CEC_CMD_RESET_PHYADDR,
	CEC_CMD_ALLOC_LOGADDR,
	CEC_CMD_UNALLOC_LOGADDR,
	CEC_CMD_TX_FRAME,
	CEC_CMD_RX_FRAME,
	CEC_CMD_TX_STS,
	CEC_CMD_RX_STS,
	CEC_CMD_MAX
}CEC_CMD_ID;

/*-----------------------------------------------------------------------------
 * Enums
 *-----------------------------------------------------------------------------
 */
/* CEC Transmit type */
typedef enum tagsyna_cec_tx_type
{
	SYNA_CEC_TX_TYPE_FIRST   = 0,
	SYNA_CEC_TX_TYPE_UNICAST = 0,
	SYNA_CEC_TX_TYPE_BROADCAST,
	SYNA_CEC_TX_TYPE_MAX
} syna_cec_tx_type, *Psyna_cec_tx_type;

/* CEC Mode */
typedef enum tagsyna_cec_mode
{
	SYNA_CEC_MODE_FIRST  = 0,
	SYNA_CEC_MODE_TX  = 0,
	SYNA_CEC_MODE_RX,
	SYNA_CEC_MODE_MAX
} syna_cec_mode, *Psyna_cec_mode;

/* CEC status information */
typedef enum tagsyna_cec_sts_info
{
	SYNA_CEC_STS_INFO_NONE = 0x00,

	SYNA_CEC_STS_TX_FAIL_INFO_NONE,
	// Follower not giving a ACK bit
	SYNA_CEC_STS_TX_FAIL_NOACK,
	// Follower acked the message
	SYNA_CEC_STS_TX_ACKED,
	// Transmission of data did not start within time speciifed by response timer
	SYNA_CEC_STS_TX_FAIL_RESP_TIMEOUT,
	// Collision detected on CEC line
	SYNA_CEC_STS_TX_FAIL_COLL_DET = 0x08,
	// Signal free time check failed, lost the arbitration
	SYNA_CEC_STS_TX_FAIL_SIG_FREE_TIME,

	SYNA_CEC_STS_RX_FAIL_INFO_NONE,
	// High-to-low transition occurred on CEC line after safe sampling period of
	// data bit duration
	SYNA_CEC_STS_RX_FAIL_TRANS_AFTER_SSP,
	// No reception after waiting for a high-to-low transition on CEC line for data bit
	SYNA_CEC_STS_RX_FAIL_NO_TRANS_FOR_DATA_BIT,
	// No reception after waiting for a high-to-low transition on CEC line
	// when ACK bit was due to be placed
	SYNA_CEC_STS_RX_FAIL_NO_TRANS_FOR_ACK_BIT,
	// Data bit total time interval is less than the time specified by CEC spec
	SYNA_CEC_STS_RX_FAIL_LOW_DB_TIME,
	// Data bit total time interval is more than the time specified by CEC spec
	SYNA_CEC_STS_RX_FAIL_HIGH_DB_TIME
} syna_cec_sts_info, *Psyna_cec_sts_info;

/*-----------------------------------------------------------------------------
 * Structures
 *-----------------------------------------------------------------------------
 */
typedef struct tagSYNA_CEC_TX_DATA
{
	syna_cec_tx_type  txType;
	u8  retxCondn;
	u8  retxCount;
	u8  txDataLen;
	u8  txDataBuf[16];
	u32 txTime;
} SYNA_CEC_TX_DATA, *PSYNA_CEC_TX_DATA;
/*-----------------------------------------------------------------------------
 * Function Prototypes
 *-----------------------------------------------------------------------------
 */

/******************************************************************************
 * FUNCTION : Enables/Disables the given interrupt(s)
 * PARAMS   : *cec_dev - Pointer to CEC object
 *		  : fail_mode - Failure mode (Tx or Rx mode)
 *		  : *p_sts_info- Pointer to return status information
 *****************************************************************************/
void syna_cec_get_fail_status(struct cec_device_t *cec_dev,
				syna_cec_mode fail_mode, int *p_sts_info,
				u16 intr_status);

/******************************************************************************
 * FUNCTION : Writes block of data starting at specified register address
 * PARAMS   : *cec_dev - Pointer to CEC object
 *		  : p_data_buf - Pointer to data buffer
 *		  : length   - Length of data to be written
 *		  : buf_write - Flag to indicate if data has to be written to
 *		  :		  - BCM buffer or to register immediately
 *****************************************************************************/
void syna_cec_reg_write(struct cec_device_t *cec_dev, u32 start_addr,
			  u8 *p_data_buf, u32 length);

/******************************************************************************
 * FUNCTION : Reads block of data starting at specified register address
 * PARAMS   : *cec_dev - Pointer to CEC object
 *		  : p_data_buf - Pointer to data buffer (Output)
 *		  : length   - Length of data to be read
 *****************************************************************************/
void syna_cec_reg_read(struct cec_device_t *cec_dev, u32 start_addr,
			 u8 *p_data_buf, u32 length);

/*-----------------------------------------------------------------------------
 * Function Prototypes
 *-----------------------------------------------------------------------------
 */
/******************************************************************************
 * FUNCTION : Loads default values of CEC block to registers
 * PARAMS   : *cec_dev - Pointer to CEC object
 *****************************************************************************/
void syna_cec_load_default_val(struct cec_device_t *cec_dev);

/******************************************************************************
 * FUNCTION : Sets/Resets transmit/receive mode
 * PARAMS   : *cec_dev - Pointer to CEC object
 *		  : cec_mode  - CEC mode (Tx/Rx)
 *		  : enable   - 1, to enable and 0 to disable
 *		  : buf_write - Flag to indicate if data has to be written to
 *		  :		  - BCM buffer or to register immediately
 * RETURN   : 0, on success
 *		  : Error code, otherwise
 *****************************************************************************/
int syna_cec_set_mode (struct cec_device_t *cec_dev, syna_cec_mode cec_mode,
					bool enable);

/******************************************************************************
 * FUNCTION : Give Rx Line Status (added for polling in case of Sig free Time Error)
 * PARAMS   : *cec_dev - Pointer to CEC object
 * RETURN   : RX line status
 *****************************************************************************/
u8 syna_cec_rx_line_status(struct cec_device_t *cec_dev);
/******************************************************************************
 * FUNCTION : Sends data to the transmit FIFO and initiates transfer on the
 *		  : CEC line, It also monitors for tx errors and handles
 *		  : retransmission if specified
 * PARAMS   : *cec_dev - Pointer to CEC object
 *		  : msg  - Pointer to transmit data structure
 *		  : signal_free_time - Signal free time
 *		  : attempt
 * RETURN   : 0, on success
 *		  : Error code, otherwise
 *****************************************************************************/
int syna_cec_transmit_data(struct cec_device_t *cec_dev, struct cec_msg *msg, int signal_free_time);

/******************************************************************************
 * FUNCTION : Reads data from the receive FIFO (Data is read until the FIFO
 *		  : becomes empty)
 * PARAMS   : *cec_dev - Pointer to CEC object
 *		  : p_data_len - Pointer to return data length read (Output)
 *		  : p_data_buf - Pointer to data buffer (Output)
 * RETURN   : 0, on success
 *		  : Error code, otherwise
 *****************************************************************************/
int syna_cec_receive_data(struct cec_device_t *cec_dev, u8 *p_data_len, u8 *p_data_buf);

/******************************************************************************
 * FUNCTION : Handles CEC interrupt
 * PARAMS   : *cec_dev	 - Pointer to CEC object
 *		  : p_intr_field   - Pointer to return interrupt field (Output)
 *		  : p_intr_data_len - Pointer to return interrupt data length (Output)
 *		  : p_intr_data	- Pointer to interrupt data (Output)
 *		  : p_tx_data	  - Pointer to transmit data in case it is required
 *		  : for a retransmission attempt
 * RETURN   : 0, on success
 *		  : Error code, otherwise
 *****************************************************************************/
int syna_cec_handle_interrupt(struct cec_device_t *cec_dev, u16 *p_intr_field,
						   u8 *p_intr_data_len, u8 *p_intr_data,
						   PSYNA_CEC_TX_DATA p_tx_data,u16 reg);
/******************************************************************************
 * FUNCTION : Controls logical address at given index
 * PARAMS   : *cec_dev	 - Pointer to CEC object
 *		  : enable	   - TRUE/FALSE to enable/disable logical address
 *		  : log_addr	  - Logical address (if disabled, it is forced to 0)
 * RETURN   : 0, on success
 *		  : Error code, otherwise
 *****************************************************************************/
int syna_cec_enable_log_addr (struct cec_device_t *cec_dev,
						  bool enable, u8 addr_index, u8 log_addr);

/******************************************************************************
 * FUNCTION : Gets device type from address
 * PARAMS   : *cec_dev - Pointer to CEC object
 *		  : enable - TRUE/FALSE to enable/disable logical address
 *		  : addr_index - Address index
 *		  : log_addr - Logical address (if disabled, it is forced to 0)
 *		  : logAddr - Logical address (if disabled, it is forced to 0)
 * RETURN   : 0, on success
 *		  : Error code, otherwise
 *****************************************************************************/
int syna_cec_get_device_type_from_addr (struct cec_device_t *cec_dev,
						  bool enable, u8 addrIndex, u8 logAddr);

#endif // __SYNA_CEC_PRIV_H__
