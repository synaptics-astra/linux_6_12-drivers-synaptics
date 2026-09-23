// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Synaptics Incorporated
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/miscdevice.h>
#include <linux/io.h>
#include <linux/spinlock.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pm.h>
#include <linux/platform_device.h>
#include <linux/thermal.h>
#include <linux/uaccess.h>
#include <linux/reboot.h>
#include <linux/reboot-mode.h>
#include <linux/semaphore.h>
#include <linux/sched/signal.h>
#include <linux/iio/iio.h>
#include <linux/iio/driver.h>
#include <soc/berlin/sm.h>
#include <linux/version.h>
#include <linux/rtc.h>

#if (LINUX_VERSION_CODE > KERNEL_VERSION(6, 11, 0))
#define RET void
#define RETURN
#define thermal_zone_device_register thermal_zone_device_register_with_trips
#else
#define RET int
#define RETURN return 0
#endif

#define SM_MSG_SIZE		32
#define SM_MSG_BODY_SIZE	(SM_MSG_SIZE - sizeof(short) * 2)
#define SM_MSGQ_TOTAL_SIZE	512
#define SM_MSGQ_HEADER_SIZE	SM_MSG_SIZE
#define SM_MSGQ_SIZE		(SM_MSGQ_TOTAL_SIZE - SM_MSGQ_HEADER_SIZE )
#define SM_MSGQ_MSG_COUNT	(SM_MSGQ_SIZE/SM_MSG_SIZE)

#define SOC_MSGQ_START			(psm->sm_base + psm->msgq_offset)
#define SM_CPU1_OUTPUT_QUEUE_ADDR	(SOC_MSGQ_START + SM_MSGQ_TOTAL_SIZE * 0)
#define SM_CPU0_INPUT_QUEUE_ADDR	(SOC_MSGQ_START + SM_MSGQ_TOTAL_SIZE * 1)
#define SM_CPU1_INPUT_QUEUE_ADDR	(SOC_MSGQ_START + SM_MSGQ_TOTAL_SIZE * 2)
#define SM_CPU0_OUTPUT_QUEUE_ADDR	(SOC_MSGQ_START + SM_MSGQ_TOTAL_SIZE * 3)

#define REBOOT_MODE_ADDR		(psm->sm_base + psm->msgq_offset - 0x100)
#define SM_BT_INFO_ADDR			(SOC_MSGQ_START + SM_MSGQ_TOTAL_SIZE * 4)

#define BERLIN_MSGQ_OFFSET		0x1f000
#define PLATYPUS_MSGQ_OFFSET		0x17000

#define MV_SM_ADC_READ  0x01

typedef struct
{
	short	m_iModuleID;
	short	m_iMsgLen;
	char	m_pucMsgBody[SM_MSG_BODY_SIZE];
} MV_SM_Message;

typedef struct
{
	int	m_iWrite;
	int	m_iRead;
	int	m_iWriteTotal;
	int	m_iReadTotal;
	char	m_Padding[SM_MSGQ_HEADER_SIZE - sizeof(int) * 4];
	char	m_Queue[SM_MSGQ_SIZE];
} MV_SM_MsgQ;

struct itcm_bt_info {
	unsigned int times;
	unsigned int level;
	bool is_btinfo;
};

struct bsm_wakeup_event {
	MV_SM_WAKEUP_SOURCE_TYPE type;
	bool set;
	bool bypass;
	char *name;
	struct semaphore resume_sem;
};

struct berlin_sm {
	void __iomem *sm_ctrl;
	void __iomem *sm_base;
	int sm_irq;
	u32 msgq_offset;
	struct device *dev;
	struct thermal_zone_device *bsm_thermal;
	struct reboot_mode_driver bsm_reboot;
	struct notifier_block bsm_reboot_nb;
	struct mutex thermal_lock;
	struct mutex adc_lock;

	/* events */
	wait_queue_head_t wait_q;
	int bt_event;
};

struct sm_adc_req {
	u32 cmd;
	u32 ch;
	u32 sample;
	u32 rsv;
};

struct sm_adc_resp {
	s32 ret;
	u32 ch;
	u32 raw;
	u32 rsv;
};

static struct berlin_sm *psm;

static struct bsm_wakeup_event wakeup_events[] = {
	{
		.type = MV_SM_WAKEUP_SOURCE_IR,
		.name = "IR",
		.bypass = true,
	},
	{
		.type = MV_SM_WAKEUP_SOURCE_WIFI,
		.name = "WIFI",
	},
	{
		.type = MV_SM_WAKEUP_SOURCE_WOL,
		.name = "WOL",
	},
	{
		.type = MV_SM_WAKEUP_SOURCE_VGA,
		.name = "VGA",
	},
	{
		.type = MV_SM_WAKEUP_SOURCE_CEC,
		.name = "CEC",
	},
	{
		.type = MV_SM_WAKEUP_SOURCE_TIMER,
		.name = "TIMER",
	},
	{
		.type = MV_SM_WAKEUP_SOURCE_BUTTON,
		.name = "BUTTON",
		.bypass = true,
	},
	{
		.type = MV_SM_WAKEUP_SOURCE_BT,
		.name = "BT",
	},
};

#define SM_Q_PUSH( pSM_Q ) {				\
	pSM_Q->m_iWrite += SM_MSG_SIZE;			\
	if( pSM_Q->m_iWrite >= SM_MSGQ_SIZE )		\
		pSM_Q->m_iWrite -= SM_MSGQ_SIZE;	\
	pSM_Q->m_iWriteTotal += SM_MSG_SIZE; }

#define SM_Q_POP( pSM_Q ) {				\
	pSM_Q->m_iRead += SM_MSG_SIZE;			\
	if( pSM_Q->m_iRead >= SM_MSGQ_SIZE )		\
		pSM_Q->m_iRead -= SM_MSGQ_SIZE;		\
	pSM_Q->m_iReadTotal += SM_MSG_SIZE; }

static int bsm_link_msg_nolock(MV_SM_MsgQ *q, MV_SM_Message *m)
{
	MV_SM_Message *p;

	if (q->m_iWrite < 0 || q->m_iWrite > SM_MSGQ_SIZE - SM_MSG_SIZE)
		/* buggy ? */
		return -EIO;

	/* message queue full, ignore the newest message */
	if (q->m_iRead == q->m_iWrite && q->m_iReadTotal != q->m_iWriteTotal)
		return -EBUSY;

	p = (MV_SM_Message*)(&(q->m_Queue[q->m_iWrite]));
	memcpy(p, m, sizeof(*p));
	mb();
	SM_Q_PUSH(q);

	return 0;
}

static int bsm_link_msg(MV_SM_MsgQ *q, MV_SM_Message *m, spinlock_t *lock)
{
	int ret;
	unsigned long flags;

	spin_lock_irqsave(lock, flags);
	ret = bsm_link_msg_nolock(q, m);
	spin_unlock_irqrestore(lock, flags);

	return ret;
}

static int bsm_unlink_msg_nolock(MV_SM_MsgQ *q, MV_SM_Message *m)
{
	int ret = -EAGAIN; /* means no data */

	if (q->m_iRead < 0 || q->m_iRead > SM_MSGQ_SIZE - SM_MSG_SIZE ||
			q->m_iReadTotal > q->m_iWriteTotal)
		/* buggy ? */
		return -EIO;

	/* if buffer was overflow written, only the last messages are
	 * saved in queue. move read pointer into the same position of
	 * write pointer and keep buffer full.
	 */
	if (q->m_iWriteTotal - q->m_iReadTotal > SM_MSGQ_SIZE) {
		int iTotalDataSize = q->m_iWriteTotal - q->m_iReadTotal;

		q->m_iReadTotal += iTotalDataSize - SM_MSGQ_SIZE;
		q->m_iRead += iTotalDataSize % SM_MSGQ_SIZE;
		if (q->m_iRead >= SM_MSGQ_SIZE)
			q->m_iRead -= SM_MSGQ_SIZE;
	}

	if (q->m_iReadTotal < q->m_iWriteTotal) {
		/* alright get one message */
		int idx = q->m_iRead;

		if (idx < 0 || idx > SM_MSGQ_SIZE - SM_MSG_SIZE)
			return -EIO;
		memcpy(m, &q->m_Queue[idx], SM_MSG_SIZE);
		mb();
		SM_Q_POP(q);
		ret = 0;
	}

	return ret;
}

static int bsm_unlink_msg(MV_SM_MsgQ *q, MV_SM_Message *m, spinlock_t *lock)
{
	int ret;
	unsigned long flags;

	spin_lock_irqsave(lock, flags);
	ret = bsm_unlink_msg_nolock(q, m);
	spin_unlock_irqrestore(lock, flags);

	return ret;
}

static DEFINE_SPINLOCK(sm_lock);

static inline int bsm_link_msg_to_sm(MV_SM_Message *m)
{
	MV_SM_MsgQ *q = (MV_SM_MsgQ *)SM_CPU0_INPUT_QUEUE_ADDR;

	return bsm_link_msg(q, m, &sm_lock);
}

static inline int bsm_unlink_msg_from_sm(MV_SM_Message *m)
{
	MV_SM_MsgQ *q = (MV_SM_MsgQ *)SM_CPU0_OUTPUT_QUEUE_ADDR;

	return bsm_unlink_msg(q, m, &sm_lock);
}

#define DEFINE_SM_MODULES(id)				\
	{						\
		.m_iModuleID  = id,			\
	}

typedef struct {
	int m_iModuleID;
	wait_queue_head_t m_wq;
	spinlock_t m_Lock;
	MV_SM_MsgQ m_MsgQ;
	struct mutex m_Mutex;
} MV_SM_Module;

static MV_SM_Module SMModules[MAX_MSG_TYPE] = {
	DEFINE_SM_MODULES(MV_SM_ID_SYS),
	DEFINE_SM_MODULES(MV_SM_ID_COMM),
	DEFINE_SM_MODULES(MV_SM_ID_IR),
	DEFINE_SM_MODULES(MV_SM_ID_KEY),
	DEFINE_SM_MODULES(MV_SM_ID_POWER),
	DEFINE_SM_MODULES(MV_SM_ID_WD),
	DEFINE_SM_MODULES(MV_SM_ID_TEMP),
	DEFINE_SM_MODULES(MV_SM_ID_VFD),
	DEFINE_SM_MODULES(MV_SM_ID_SPI),
	DEFINE_SM_MODULES(MV_SM_ID_I2C),
	DEFINE_SM_MODULES(MV_SM_ID_UART),
	DEFINE_SM_MODULES(MV_SM_ID_CEC),
	DEFINE_SM_MODULES(MV_SM_ID_WOL),
	DEFINE_SM_MODULES(MV_SM_ID_LED),
	DEFINE_SM_MODULES(MV_SM_ID_ETH),
	DEFINE_SM_MODULES(MV_SM_ID_DDR),
	DEFINE_SM_MODULES(MV_SM_ID_WIFIBT),
	DEFINE_SM_MODULES(MV_SM_ID_DEBUG),
	DEFINE_SM_MODULES(MV_SM_ID_CONSOLE),
	DEFINE_SM_MODULES(MV_SM_ID_PMIC),
	DEFINE_SM_MODULES(MV_SM_ID_AUDIO),
	DEFINE_SM_MODULES(MV_SM_ID_GPIO),
	DEFINE_SM_MODULES(MV_SM_ID_GPIOBTN),
	DEFINE_SM_MODULES(MV_SM_ID_ADC),
};

static inline MV_SM_Module *bsm_search_module(int id)
{
	if (id > 0 && id <= ARRAY_SIZE(SMModules))
		return &(SMModules[id-1]);
	else
		return NULL;
}

static int bsm_link_msg_to_module(MV_SM_Message *m)
{
	MV_SM_Module *module;
	int ret;

	module = bsm_search_module(m->m_iModuleID);
	if (!module)
		return -EINVAL;

	ret = bsm_link_msg(&(module->m_MsgQ), m, &(module->m_Lock));
	if (ret == 0) {
		/* wake up any process pending on wait-queue */
		wake_up_interruptible(&(module->m_wq));
	}

	return ret;
}

static int bsm_unlink_msg_from_module(MV_SM_Message *m)
{
	MV_SM_Module *module;
	DEFINE_WAIT(__wait);
	unsigned long flags;
	int ret;

	module = bsm_search_module(m->m_iModuleID);
	if (!module)
		return -EINVAL;

	for (;;) {
		prepare_to_wait(&(module->m_wq), &__wait, TASK_INTERRUPTIBLE);
		spin_lock_irqsave(&(module->m_Lock), flags);
		ret = bsm_unlink_msg_nolock(&(module->m_MsgQ), m);
		spin_unlock_irqrestore(&(module->m_Lock), flags);
		if (ret != -EAGAIN)
			break;

		schedule();

		if (signal_pending(current)) {
			ret = -ERESTARTSYS;
			break;
		}
	}
	finish_wait(&(module->m_wq), &__wait);

	return ret;
}

static LIST_HEAD(sm_ir_handler);
static DEFINE_SPINLOCK(sm_ir_lock);

void register_sm_ir_handler(struct sm_ir_handler *handler)
{
	unsigned long flags;

	spin_lock_irqsave(&sm_ir_lock, flags);
	list_add_rcu(&handler->node, &sm_ir_handler);
	spin_unlock_irqrestore(&sm_ir_lock, flags);
}
EXPORT_SYMBOL(register_sm_ir_handler);

void unregister_sm_ir_handler(struct sm_ir_handler *handler)
{
	unsigned long flags;

	spin_lock_irqsave(&sm_ir_lock, flags);
	list_del_rcu(&handler->node);
	spin_unlock_irqrestore(&sm_ir_lock, flags);
	synchronize_rcu();
}
EXPORT_SYMBOL(unregister_sm_ir_handler);

static void call_sm_ir_handler(int ir_key)
{
	struct sm_ir_handler *handler;

	rcu_read_lock();
	list_for_each_entry_rcu(handler, &sm_ir_handler, node) {
		if (!handler->key || handler->key == ir_key)
			handler->fn(ir_key);
	}
	rcu_read_unlock();
}

int bsm_msg_send(int id, void *msg, int len)
{
	MV_SM_Message m = {};
	int ret;
	int cnt_timeout = 3 * 100;

	if (!msg)
		return -EINVAL;

	if (unlikely(len < 4) || unlikely(len > SM_MSG_BODY_SIZE))
		return -EINVAL;

	m.m_iModuleID = id;
	m.m_iMsgLen   = len;
	memcpy(&m.m_pucMsgBody[0], msg, len);
	for (;;) {
		ret = bsm_link_msg_to_sm(&m);
		if (ret != -EBUSY)
			break;
		mdelay(10);

		cnt_timeout--;
		if (cnt_timeout < 0)
			break;
	}

	return ret;
}
EXPORT_SYMBOL(bsm_msg_send);

int bsm_msg_recv(int id, void *msg, int *len)
{
	MV_SM_Message m = {};
	int ret;

	m.m_iModuleID = id;
	ret = bsm_unlink_msg_from_module(&m);
	if (ret)
		return ret;

	if (msg)
		memcpy(msg, m.m_pucMsgBody, m.m_iMsgLen);

	if (len)
		*len = m.m_iMsgLen;

	return 0;
}
EXPORT_SYMBOL(bsm_msg_recv);

static void bsm_handle_wakeup_event(struct bsm_wakeup_event *wk)
{
	if (wk->set && !wk->bypass) {
		wk->set = false;
		up(&wk->resume_sem);
	}
}

static void bsm_msg_dispatch(void)
{
	MV_SM_Message m;
	int ret;

	/* read all messages from SM buffers and dispatch them */
	for (;;) {
		ret = bsm_unlink_msg_from_sm(&m);
		if (ret)
			break;

		if (m.m_iModuleID == MV_SM_ID_IR && m.m_iMsgLen == 4) {
			/* special case for IR events */
			int ir_key = *(int *)m.m_pucMsgBody;
			call_sm_ir_handler(ir_key);
		} else if (m.m_iModuleID == MV_SM_ID_POWER &&
				m.m_iMsgLen == 4 &&
				*(int *)m.m_pucMsgBody == 0) {
			struct bsm_wakeup_event *wk;
			wk = &wakeup_events[MV_SM_WAKEUP_SOURCE_TIMER];
			bsm_handle_wakeup_event(wk);
		} else {
			/* try best to dispatch received message */
			ret = bsm_link_msg_to_module(&m);
			if (ret != 0) {
				printk(KERN_ERR "Drop SM message\n");
				continue;
			}
		}
	}
}

static irqreturn_t bsm_intr(int irq, void *p)
{
	u32 val;
	struct berlin_sm *priv = p;
	struct itcm_bt_info *p_itcm;

	val = readl_relaxed(priv->sm_ctrl);
	val &= ~(1 << 1);
	writel_relaxed(val, priv->sm_ctrl);

	bsm_msg_dispatch();

	p_itcm = (struct itcm_bt_info *)SM_BT_INFO_ADDR;
	if (p_itcm->is_btinfo == true) {
		priv->bt_event = 1;
		wake_up_interruptible(&priv->wait_q);
		p_itcm->is_btinfo = false;
	}
	return IRQ_HANDLED;
}

static int bsm_open(struct inode *inode, struct file *file)
{
	file->private_data = psm;
	return 0;
}

static ssize_t bsm_read(struct file *file, char __user *buf,
				size_t count, loff_t *ppos)
{
	MV_SM_Message m;
	int id = (int)(*ppos);
	int ret;

	if (count < SM_MSG_SIZE)
		return -EINVAL;

	m.m_iModuleID = id;
	ret = bsm_unlink_msg_from_module(&m);
	if (!ret) {
		if (copy_to_user(buf, (void *)&m, SM_MSG_SIZE))
			return -EFAULT;
		return SM_MSG_SIZE;
	}

	return 0;
}

static ssize_t bsm_write(struct file *file, const char __user *buf,
				size_t count, loff_t *ppos)
{
	MV_SM_Message SM_Msg;
	int ret;
	int id = (int)(*ppos);

	if (count < 4 || count > SM_MSG_BODY_SIZE)
		return -EINVAL;

	if (copy_from_user(SM_Msg.m_pucMsgBody, buf, count))
		return -EFAULT;

	ret = bsm_msg_send(id, SM_Msg.m_pucMsgBody, count);
	if (ret < 0)
		return -EFAULT;
	else
		return count;
}

static long bsm_unlocked_ioctl(struct file *file,
			unsigned int cmd, unsigned long arg)
{
	MV_SM_Module *module;
	MV_SM_Message m;
	int ret = 0, id;
	struct berlin_sm *priv = file->private_data;
	struct itcm_bt_info *p_itcm, val;

	/* for legacy, will be removed */
	if (cmd == SM_Enable_WaitQueue || cmd == SM_Disable_WaitQueue)
		return 0;

	if (cmd == SM_BT_INFO) {

		/* with timeout, ioctl won't block here. */
		ret = wait_event_interruptible_timeout(priv->wait_q, priv->bt_event, msecs_to_jiffies(1000));
		if (ret < 0) {
			pr_warn("bt wakeup wait fails\n");
			return ret;
		}

		/* update and return the new value after wait_event*/
		p_itcm = (struct itcm_bt_info *)SM_BT_INFO_ADDR;
		val.times = p_itcm->times;
		val.level = p_itcm->level;


		if (copy_to_user((void __user *)arg, (void *)&val, sizeof(val)))
			return -EFAULT;
		priv->bt_event = 0;
		return 0;
	}

	if (cmd == SM_WAIT_WAKEUP) {
		id = arg;
		if (id < 0 || id >= ARRAY_SIZE(wakeup_events) ||
			wakeup_events[id].bypass)
			return -EINVAL;

		wakeup_events[id].set = true;
		return down_interruptible(&wakeup_events[id].resume_sem);
	}

	if (copy_from_user(&m, (void __user *)arg, SM_MSG_SIZE))
		return -EFAULT;

	if (m.m_iMsgLen < 1 || m.m_iMsgLen > SM_MSG_BODY_SIZE)
		return -EINVAL;

	id = m.m_iModuleID;
	module = bsm_search_module(id);
	if (!module)
		return -EINVAL;

	mutex_lock(&(module->m_Mutex));

	switch (cmd) {
	case SM_READ:
		ret = bsm_unlink_msg_from_module(&m);
		if (!ret) {
			if (copy_to_user((void __user *)arg, &m, SM_MSG_SIZE))
				ret = -EFAULT;
		}
		break;
	case SM_WRITE:
		ret = bsm_msg_send(m.m_iModuleID, m.m_pucMsgBody, m.m_iMsgLen);
		break;
	case SM_RDWR:
		ret = bsm_msg_send(m.m_iModuleID, m.m_pucMsgBody, m.m_iMsgLen);
		if (ret)
			break;
		ret = bsm_unlink_msg_from_module(&m);
		if (!ret) {
			if (copy_to_user((void __user *)arg, &m, SM_MSG_SIZE))
				ret = -EFAULT;
		}
		break;
	default:
		ret = -EINVAL;
		break;
	}

	mutex_unlock(&(module->m_Mutex));

	return ret;
}

static int get_sm_time(struct device *dev, u64 *ms)
{
	int msg, rcv[4], len, ret;

	msg = MV_SM_GET_SUSPEND_RESUME_TIME;
	ret = bsm_msg_send(MV_SM_ID_POWER, &msg, sizeof(msg));
	if (ret < 0)
		return ret;

	ret = bsm_msg_recv(MV_SM_ID_POWER, rcv, &len);
	if (ret < 0)
		return ret;

	if (len != 8)
		return -EIO;

	*ms = rcv[0] + ((u64)rcv[1] << 32);
	return 0;
}

static int smrtc_read_time(struct device *dev, struct rtc_time *tm)
{
	int ret;
	u64 ms;
	time64_t secs;

	ret = get_sm_time(dev, &ms);
	if (ret < 0) {
		dev_err(dev, "Failed to get time from SM\n");
		return -EIO;
	}
	secs = ms / 1000;
	rtc_time64_to_tm(secs, tm);
	return rtc_valid_tm(tm);
}

static const struct rtc_class_ops smrtc_ops = {
	.read_time = smrtc_read_time,
};

static const struct file_operations bsm_fops = {
	.owner		= THIS_MODULE,
	.open 		= bsm_open,
	.write		= bsm_write,
	.read		= bsm_read,
	.unlocked_ioctl	= bsm_unlocked_ioctl,
	.compat_ioctl	= bsm_unlocked_ioctl,
	.llseek		= default_llseek,
};

static struct miscdevice sm_dev = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= "bsm",
	.fops	= &bsm_fops,
};

static int bsm_get_temp(struct thermal_zone_device *thermal, int *temp)
{
	int ret, msg, len, rcv[4];
	static int prev_temp = 0;
#if (LINUX_VERSION_CODE > KERNEL_VERSION(6, 11, 0))
	struct berlin_sm *priv = thermal_zone_device_priv(thermal);
#else
	struct berlin_sm *priv = thermal->devdata;
#endif

	mutex_lock(&priv->thermal_lock);

	msg = MV_SM_TEMP_SAMPLE;
	ret = bsm_msg_send(MV_SM_ID_TEMP, &msg, sizeof(msg));
	if (ret < 0) {
		*temp = prev_temp;
		goto get_temp_error;
	}

	ret = bsm_msg_recv(MV_SM_ID_TEMP, rcv, &len);
	if (ret < 0) {
		*temp = prev_temp;
		goto get_temp_error;
	}

	if (len != 16) {
		ret = -EIO;
		*temp = prev_temp;
		goto get_temp_error;
	}

	*temp = rcv[3] * 1000;

get_temp_error:
	mutex_unlock(&priv->thermal_lock);

	return ret;
}

static int bsm_get_adc(struct berlin_sm *priv, u32 ch, u32 sample, u32 *raw)
{
	struct sm_adc_req req = {
		.cmd = MV_SM_ADC_READ,
		.ch = ch,
		.sample = sample,
		.rsv = 0,
	};

	struct sm_adc_resp resp;
	int len = 0;
	int ret;

	if (!raw)
		return -EINVAL;

	mutex_lock(&priv->adc_lock);
	ret = bsm_msg_send(MV_SM_ID_ADC, &req, sizeof(req));
	if (ret < 0)
		goto out;

	ret = bsm_msg_recv(MV_SM_ID_ADC, &resp, &len);
	if (ret < 0)
		goto out;

	if (len != sizeof(resp)) {
		ret = -EIO;
		goto out;
	}

	if (resp.ret < 0) {
		ret = -EIO;
		goto out;
	}

	*raw = resp.raw;
	ret = 0;

out:
	mutex_unlock(&priv->adc_lock);
	return ret;
}

static const struct iio_chan_spec bsm_adc_channels[] = {
	{
		.type = IIO_VOLTAGE,
		.indexed = 1,
		.channel = 0,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
	},
	{
		.type = IIO_VOLTAGE,
		.indexed = 1,
		.channel = 1,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
	},
};

static int bsm_adc_iio_read_raw(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       int *val, int *val2, long mask)
{
	struct berlin_sm *priv = dev_get_drvdata(&indio_dev->dev);
	u32 raw;
	int ret;

	if (!priv)
		return -ENODEV;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		ret = bsm_get_adc(priv, chan->channel, 4, &raw);
		if (ret)
			return ret;

		*val = (int)raw;
		return IIO_VAL_INT;
	default:
		return -EINVAL;
	}
}

static const struct iio_info bsm_adc_iio_info = {
	.read_raw = bsm_adc_iio_read_raw,
};

static struct thermal_zone_device_ops ops = {
	.get_temp = bsm_get_temp,
};

static int bsm_reboot_mode_write(struct reboot_mode_driver *reboot,
				 unsigned int magic)
{
	writel_relaxed(magic, REBOOT_MODE_ADDR);

	return 0;
}

/*
 * Most reboot modes should be handled by reboot-mode driver, but we
 * do need an extra reboot nb to handle those modes which can't be
 * supported by reboot-mode driver, for example, there's a space in
 * the mode name: "foo bar".
 */
static int bsm_reboot_notify(struct notifier_block *this,
			     unsigned long mode, void *cmd)
{
	if (!cmd)
		return NOTIFY_DONE;

	if (!strcmp(cmd, "dm-verity device corrupted"))
		writel_relaxed(0x12513991, REBOOT_MODE_ADDR);

	return NOTIFY_DONE;
}

static int bsm_probe(struct platform_device *pdev)
{
	int i, ret;
	struct resource *r;
	struct resource *r_sm_ctrl;
	struct berlin_sm *priv;
	struct device *dev = &pdev->dev;
	struct device_node *np = pdev->dev.of_node;
	resource_size_t size;
	const char *name;
	struct rtc_device *rtc;
	struct iio_dev *indio_dev;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	psm = priv;
	priv->msgq_offset = (uintptr_t)of_device_get_match_data(dev);
	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	if (!r || resource_type(r) != IORESOURCE_MEM) {
		dev_err(dev, "invalid resource\n");
		return -EINVAL;
	}

	size = resource_size(r);
	name = r->name ?: dev_name(dev);

	if (!devm_request_mem_region(dev, r->start, size, name)) {
		dev_err(dev, "can't request region for resource %pR\n", r);
		return -EBUSY;
	}

	if (of_property_read_bool(np, "no-memory-wc"))
		priv->sm_base = devm_ioremap(dev, r->start, size);
	else
		priv->sm_base = devm_ioremap_wc(dev, r->start, size);
	if (IS_ERR(priv->sm_base))
		return PTR_ERR(priv->sm_base);

	r_sm_ctrl = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	priv->sm_ctrl = devm_ioremap_resource(dev, r_sm_ctrl);
	if (IS_ERR(priv->sm_ctrl))
		return PTR_ERR(priv->sm_ctrl);

	priv->sm_irq = platform_get_irq(pdev, 0);
	if (priv->sm_irq < 0)
		return -ENXIO;

	init_waitqueue_head(&priv->wait_q);
	priv->bt_event = 0;

	for (i = 0; i < ARRAY_SIZE(SMModules); i++) {
		init_waitqueue_head(&(SMModules[i].m_wq));
		spin_lock_init(&(SMModules[i].m_Lock));
		mutex_init(&(SMModules[i].m_Mutex));
		memset(&(SMModules[i].m_MsgQ), 0, sizeof(MV_SM_MsgQ));
	}
	mutex_init(&priv->thermal_lock);
	mutex_init(&priv->adc_lock);

	ret = devm_request_irq(dev, priv->sm_irq, bsm_intr, 0, "bsm", priv);
	if (ret < 0)
		return ret;

	for (i = 0; i < ARRAY_SIZE(wakeup_events); i++)
		sema_init(&wakeup_events[i].resume_sem, 0);

	if (of_property_read_bool(np, "smrtc-enable")) {
		dev_dbg(dev, "smrtc-enable is present in the device tree\n");
		rtc = devm_rtc_device_register(&pdev->dev, "smrtc",
			&smrtc_ops, THIS_MODULE);
		if (IS_ERR(rtc))
			return PTR_ERR(rtc);
	} else {
		dev_dbg(dev, "smrtc-enable is not present\n");
	}

	ret = misc_register(&sm_dev);
	if (ret < 0)
		return ret;

	priv->bsm_thermal = thermal_zone_device_register("bsm_thermal", 0, 0,
                                                   priv, &ops, NULL, 0, 0);
	if (IS_ERR(priv->bsm_thermal)) {
		dev_warn(dev,
			 "Failed to register thermal zone device\n");
		priv->bsm_thermal = NULL;
	}

	indio_dev = devm_iio_device_alloc(dev, 0);
	if (!indio_dev) {
		dev_err(dev, "Failed to allocate IIO device\n");
		return -ENOMEM;
	}

	dev_set_drvdata(&indio_dev->dev, priv);

	indio_dev->name = "bsm-adc";
	indio_dev->dev.parent = dev;
	indio_dev->info = &bsm_adc_iio_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = bsm_adc_channels;
	indio_dev->num_channels = ARRAY_SIZE(bsm_adc_channels);

	ret = devm_iio_device_register(dev, indio_dev);
	if (ret) {
		dev_err(dev, "failed to register IIO ADC: %d\n", ret);
		return ret;
	}

	priv->bsm_reboot_nb.notifier_call = bsm_reboot_notify;
	register_reboot_notifier(&priv->bsm_reboot_nb);

	priv->bsm_reboot.dev = dev;
	priv->bsm_reboot.write = bsm_reboot_mode_write;
	/* clear reboot reason */
	writel_relaxed(0, REBOOT_MODE_ADDR);
	platform_set_drvdata(pdev, priv);
	return devm_reboot_mode_register(dev, &priv->bsm_reboot);
}

static RET bsm_remove(struct platform_device *pdev)
{
	struct berlin_sm *priv = platform_get_drvdata(pdev);
	misc_deregister(&sm_dev);

	if (priv->bsm_thermal)
		thermal_zone_device_unregister(priv->bsm_thermal);

	RETURN;
}

#ifdef CONFIG_PM
static int bsm_resume(struct device *dev)
{
	int ret, msg, len, rcv[3];

	msg = MV_SM_POWER_WAKEUP_SOURCE_REQUEST;
	ret = bsm_msg_send(MV_SM_ID_POWER, &msg, sizeof(msg));
	if (ret < 0)
		return ret;

	ret = bsm_msg_recv(MV_SM_ID_POWER, rcv, &len);
	if (ret < 0)
		return ret;
	if (len != 12)
		return -EIO;

	ret = rcv[0];
	if (ret >= 0 && ret < ARRAY_SIZE(wakeup_events)) {
		struct bsm_wakeup_event *wk = &wakeup_events[ret];
		printk("wakeup from: %s\n", wk->name);
		bsm_handle_wakeup_event(wk);
	}

	return 0;
}

static struct dev_pm_ops bsm_pm_ops = {
	.resume		= bsm_resume,
};
#endif

static const struct of_device_id bsm_match[] = {
	{
		.compatible = "marvell,berlin-sm",
		.data = (void *)BERLIN_MSGQ_OFFSET
	},
	{
		.compatible = "syna,platypus-sm",
		.data = (void *)PLATYPUS_MSGQ_OFFSET
	},
	{},
};
MODULE_DEVICE_TABLE(of, bsm_match);

static struct platform_driver bsm_driver = {
	.probe		= bsm_probe,
	.remove		= bsm_remove,
	.driver = {
		.name	= "marvell,berlin-sm",
		.owner	= THIS_MODULE,
		.of_match_table = bsm_match,
#ifdef CONFIG_PM
		.pm	= &bsm_pm_ops,
#endif
	},
};
module_platform_driver(bsm_driver);

MODULE_AUTHOR("Marvell-Galois");
MODULE_DESCRIPTION("System Manager Driver");
MODULE_LICENSE("GPL");
