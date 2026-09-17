// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2020 Synaptics Incorporated */

#define _VPP_ISR_C_
#include <linux/kthread.h>
#include "drv_msg.h"
#include "vpp_isr.h"
#include "vpp_api.h"
#include "avio_io.h"
#include "hal_vpp_wrap.h"
#include "avio_dhub_cfg.h"
#include "avio_dhub_drv.h"

#include "vpp_intrs.h"

#define VPP_ISR_MSGQ_SIZE 64
#define VPP_INTR_NO_MAX 32

extern vpp_config_params vpp_config_param;

#define HPD_CHECK_TIMEOUT  10*HZ
static AMPMsgQ_t *hVPPIntrQ;
static AMPMsgQ_t hVPPInputFrameQ;
static struct semaphore vpp_sem;
static struct task_struct *vpp_isr_task;

static struct semaphore vpp_vsync_sem;
static struct semaphore vpp_vsync1_sem;
static struct semaphore vpp_hdmitx_hpd_sem;

static atomic_t vppintr_cnt[VPP_INTR_NO_MAX];

static HRESULT wait_vpp_primary_vsync(int Id) {
	struct semaphore *pSem;
	HRESULT hres;

	pSem = (!Id) ? &vpp_vsync_sem : &vpp_vsync1_sem;

	hres = down_interruptible(pSem);

	return hres;
}

static HRESULT wait_hdmi_hpd_change(void) {
	return down_interruptible(&vpp_hdmitx_hpd_sem);
}

static int VPP_IRQ_Handler(unsigned int irq, void *dev_id)
{
	int rc;
	int intr_num;
	CC_MSG_t msg;

	if (!hVPPIntrQ)
		goto EXIT_ISR;

	intr_num = ffs(irq) - 1;

	if (atomic_read(&vppintr_cnt[intr_num]))
		goto EXIT_ISR;
	else
		atomic_inc(&vppintr_cnt[intr_num]);

	msg.m_MsgID = VPP_CC_MSG_TYPE_VPP;
	msg.m_Param2 = 0;
	msg.m_Param1 = intr_num;
	rc = AMPMsgQ_Add(hVPPIntrQ, &msg);
	if (likely(rc == S_OK))
		up(&vpp_sem);

EXIT_ISR:
	return IRQ_HANDLED;
}

static int VPP_HandleInputQueueMsg(CC_MSG_t *hDisplayFrameMsg)
{
	int ret;

	switch (hDisplayFrameMsg->m_MsgID)
	{
		case VPP_FRAMEQ_MSGT_DISPLAY_FRAME:
		{
			ret = wrap_MV_VPPOBJ_DisplayFrame(hDisplayFrameMsg->m_Param1,
				(void *)(uintptr_t) hDisplayFrameMsg->m_Param2);
		}
		break;

		case VPP_FRAMEQ_MSGT_STILL_PICTURE:
		{
			ret = wrap_MV_VPPOBJ_SetStillPicture(hDisplayFrameMsg->m_Param1,
				(void *)(uintptr_t) hDisplayFrameMsg->m_Param2);
		}
		break;

		default:
		{
			pr_err("Message type not supported %d\n",
				hDisplayFrameMsg->m_MsgID);
			ret = MV_VPP_EUNSUPPORT;
		}
		break;
	}
	return ret;
}

static void VPP_DisplayPreISRService(void)
{
	HRESULT rc;
	CC_MSG_t hDisplayFrameMsg;
	VBUF_INFO *pFrameInfo;
	VPP_VBUF *pVppVbufDesc;

	/*
	 * Apply or Update plane-info : refwin/dispwin/etc.
	 * Avoid/Skip until any previous update is pending to be processed
	 */
	if (MV_VPP_UpdatePlaneInfoFromISR() == VPP_TA_E_SWSTATEWRONG)
		return;

	do {
		rc = AMPMsgQ_ReadTry(&hVPPInputFrameQ, &hDisplayFrameMsg);
		if (likely(rc == S_OK)) {
			pFrameInfo = (VBUF_INFO *) hDisplayFrameMsg.m_Param2;
			pVppVbufDesc = pFrameInfo->pVppVbufInfo_virt;

			AMPMsgQ_ReadFinish(&hVPPInputFrameQ);
			VPP_HandleInputQueueMsg(&hDisplayFrameMsg);
		}
	} while (rc == S_OK);
}

static int VPP_ISR_Task(void *param)
{
	CC_MSG_t msg;
	HRESULT rc = MV_VPP_OK;
	int intr_num;

	while (!kthread_should_stop()) {
		rc = down_interruptible(&vpp_sem);
		if (unlikely(rc < 0))
			return rc;
		rc = AMPMsgQ_DequeueRead(hVPPIntrQ, &msg);
		if (unlikely(!rc))
			continue;

		VPP_DisplayPreISRService();

		intr_num = msg.m_Param1;
		msg.m_Param1 = bSETMASK(msg.m_Param1);
		wrap_MV_VPPOBJ_IsrHandler(msg.m_MsgID, msg.m_Param1);
#if defined VPP_DHUB_HDMITX_HPD_INTR  //all chips doesn't have hdmi interface
		if (bSETMASK(VPP_DHUB_HDMITX_HPD_INTR) == msg.m_Param1)
			up(&vpp_hdmitx_hpd_sem);
		else
#endif
			VPP_SIGNAL_VSYNC(vpp_config_param.display_mode,
				intr_num,
				&vpp_vsync_sem,
				&vpp_vsync1_sem);

		atomic_dec(&vppintr_cnt[intr_num]);
	}

	return 0;
}

void VPP_CreateISRTask(void)
{
	unsigned int err;
	AMPMsgQ_t *hVPPtmpIntrQ;

	sema_init(&vpp_sem, 0);
	sema_init(&vpp_vsync_sem, 0);
	sema_init(&vpp_vsync1_sem, 0);
	sema_init(&vpp_hdmitx_hpd_sem, 0);

	err = AMPMsgQ_Init(&hVPPInputFrameQ, VPP_ISR_MSGQ_SIZE);
	if (unlikely(err != S_OK))
		pr_err("%s:%d: VPP Display Frame MsgQ init FAILED, err:%8x\n",
			__func__, __LINE__, err);

	hVPPtmpIntrQ = kmalloc(sizeof(AMPMsgQ_t), GFP_KERNEL);
	if (unlikely(!hVPPtmpIntrQ)) {
		pr_err("%s:%d: VPP MsgQ init mem alloc FAILED\n", __func__, __LINE__);
	} else {
		err = AMPMsgQ_Init(hVPPtmpIntrQ, VPP_ISR_MSGQ_SIZE);
		if (unlikely(err != S_OK))
			pr_err("%s:%d: VPP MsgQ init FAILED, err:%8x\n", __func__, __LINE__, err);
		else
			hVPPIntrQ = hVPPtmpIntrQ;
	}
	//Register Callback to wait for VPP VSYNC
	wrap_MV_VPP_RegisterWaitForVppVsyncCb(wait_vpp_primary_vsync);

	//Register callback to wait for HDMI connection change
	wrap_MV_VPP_RegisterWaitForHdmiHpd(wait_hdmi_hpd_change);

	vpp_isr_task = kthread_run(VPP_ISR_Task, NULL, "VPP ISR Thread");
	if (IS_ERR(vpp_isr_task))
		return;
	sched_set_fifo(vpp_isr_task);
}

void VPP_StopISRTask(void)
{
	unsigned int err;
	CC_MSG_t msg;

	kthread_stop(vpp_isr_task);
	do {
		err = AMPMsgQ_DequeueRead(hVPPIntrQ, &msg);
	} while (likely(err == 1));
	sema_init(&vpp_sem, 0);
	err = AMPMsgQ_Destroy(hVPPIntrQ);
	if (unlikely(err != S_OK))
		pr_err("%s:%d: VPP MsgQ Destroy FAILED, err:%8x\n", __func__, __LINE__, err);

	if (hVPPIntrQ)
		kfree(hVPPIntrQ);
}

void VPP_EnableDhubInterrupt(bool enable)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(vpp_intrs); i++) {
		Dhub_IntrRegisterHandler(DHUB_ID_VPP_DHUB, vpp_intrs[i], NULL, (enable ? VPP_IRQ_Handler : NULL));
	}
}

int VPP_PushFrameToInputQueue(int planeId, int msgID, VBUF_INFO *pFrameInfo)
{
	CC_MSG_t msg;
	HRESULT result;

	msg.m_MsgID = msgID;
	msg.m_Param2 = (u64) pFrameInfo;
	msg.m_Param1 = planeId;

	result = AMPMsgQ_Add(&hVPPInputFrameQ, &msg);
	if (unlikely(result != S_OK))
		pr_err("Push Frame Failed E[%d]\n", result);

	return result;
}
