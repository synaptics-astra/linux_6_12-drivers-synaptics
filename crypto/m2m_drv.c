// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2022 Synaptics Incorporated */

#define pr_fmt(fmt) "[berlin_m2m] " fmt

#include <linux/init.h>
#include <linux/module.h>
#include <linux/io.h>

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/irq.h>
#include <linux/platform_device.h>

#include <linux/workqueue.h>
#include <linux/sched/task.h>
#include <linux/dma-buf.h>
#include <linux/poll.h>
#include <linux/debugfs.h>

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/version.h>
#include <linux/interrupt.h>
#include <linux/uaccess.h>
#include <linux/clk.h>
/*************************************************************************
 * Local head files
 */
#include "uapi/m2m.h"
#include "m2m_wrapper.h"
#if !IS_ENABLED(CONFIG_OPTEE)
#include "tz_driver.h"
#include "tsp.h"
#else
#include <linux/tee_drv.h>
#endif
#include <uapi/bm.h>
#include "m2m_kernel_compatibility_wrap.h"

/***********************************************************************
 * Module Variable
 */
#define M2M_MODULE_NAME	    "m2m_module"
#define M2M_DEVICE_NAME	    "m2m"
#define M2M_MAX_DEVS	    1
#define M2M_MINOR	        0
#define TSP_INTR_MASK_M2M	1

#define RA_TspIntReg_software_int_status	0x002C
#define RA_TspIntReg_software_int_enable    0x0028

enum m2m_feed_mode {
	M2M_FEED_MODE_SYNC,
	M2M_FEED_MODE_ASYNC,
};

struct m2m_session {
	int sess_id;
	enum m2m_feed_mode fmode;
	struct list_head active_node;
	struct semaphore m2m_sem;
	struct mutex m2m_mutex;

	TEEC_Session teec_sess;
	bool teec_sess_opened;
	struct task_struct *task;

	/* for async mode */
	wait_queue_head_t  wq;
	int  total_cmdnr;
	int  finished_cmdnr;
	struct mutex cmd_mutex;

	/* for debug info */
	struct list_head node;
	pid_t pid;
	pid_t tid;

};

struct m2m_device_t {
	struct clk *tsp_clk_core;
	unsigned char *dev_name;
	struct cdev cdev;
	struct device *dev;
	struct class *dev_class;
	const struct file_operations *fops;
	dev_t dev_id;

	u32 irq_num;
	struct resource *res;
	void __iomem *intr_virt_addr;

	struct mutex sess_mutex;
	struct rw_semaphore rwsem;
	struct list_head sess_list;    /* list for all sessions */
	struct list_head active_sess_list;  /* list for active sessions */
	struct workqueue_struct *work_queue;
	struct work_struct work;

	atomic_t m2m_curr_sess_id;
	atomic_t m2m_sess_refcnt;

	struct dentry *debug_root;
	struct dentry *sess_debug_root;
};

union m2m_ioctl_arg {
	enum m2m_crypto_mode  crypto_mode;
	enum m2m_crypto_type  crypto_type;
	struct m2m_key config;
	struct m2m_pattern_mode pattern_mode;
	struct m2m_mem mem;
	enum m2m_residue_mode residue_mode;
	__u32  finished_cmdnr;
};

static struct m2m_device_t m2m_device = {
	.dev_name = M2M_DEVICE_NAME,
};

/**********************************************************************
 * Module API
 */

#define m2m_enter_func()	pr_debug("enter %s\n", __func__)

#define M2M_REG_WORD32_WRITE(addr, data) \
	writel_relaxed(((unsigned int)(data)), ((addr) + m2m_device.intr_virt_addr))
#define M2M_REG_WORD32_READ(offset, holder) \
	(*(holder) = readl_relaxed((offset) + m2m_device.intr_virt_addr))

static int m2m_debug_sess_show(struct seq_file *s, void *unused)
{
	struct m2m_device_t *pdev = s->private;
	struct m2m_session *sess;
	struct list_head *curr, *next;
	int i = 0;

	seq_puts(s, "|No  |sessId |pid   |tid   |sync_mode |total_cmds |finished_cmds |\n");
	seq_puts(s, "------------------------------------------------------------------\n");

	down_read(&pdev->rwsem);
	list_for_each_safe(curr, next, &pdev->sess_list) {
		sess = list_entry(curr, struct m2m_session, node);
		seq_printf(s, "|%4d|%7d|%6d|%6d|%10d|%11d|%14d|\n",
			   i, sess->sess_id, sess->pid, sess->tid, sess->fmode,
			   sess->total_cmdnr, sess->finished_cmdnr);
		i++;
	}
	up_read(&pdev->rwsem);
	seq_puts(s, "------------------------------------------------------------------\n");

	return 0;
}

static int m2m_debug_sess_open(struct inode *inode, struct file *file)
{
	return single_open(file, m2m_debug_sess_show, inode->i_private);
}

static const struct file_operations debug_sess_fops = {
	.open = m2m_debug_sess_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static void sess_push_cmd(struct m2m_session *sess)
{
	down_write(&m2m_device.rwsem);

	mutex_lock(&sess->cmd_mutex);
	if (sess->total_cmdnr == sess->finished_cmdnr)
		list_add_tail(&sess->active_node, &m2m_device.active_sess_list);
	sess->total_cmdnr += 1;
	mutex_unlock(&sess->cmd_mutex);

	up_write(&m2m_device.rwsem);
}

/* pop unfinished cmds, and returns the real pop cmds number
 * @pop_nr:  pop cmds number, 0 means pop all the unfinished cmds
 *
 * at the end, if real_pop_nr > 0, and  total_cmdnr == finished_cmdnr, then
 * it needs to remove the sess from the active session list
 */
static int sess_pop_cmd(struct m2m_session *sess, int pop_nr)
{
	int real_pop_nr = pop_nr;

	down_write(&m2m_device.rwsem);

	mutex_lock(&sess->cmd_mutex);
	if (pop_nr)
		sess->total_cmdnr -= pop_nr;
	else {
		real_pop_nr = sess->total_cmdnr - sess->finished_cmdnr;
		if (real_pop_nr) {
			pr_debug("sess[%d] pop unfinished cmds %d, current total cmds %d\n",
				sess->sess_id, real_pop_nr, sess->total_cmdnr);
			sess->total_cmdnr = sess->finished_cmdnr;
		}
	}

	if (sess->total_cmdnr == sess->finished_cmdnr && real_pop_nr)
		list_del(&sess->active_node);
	mutex_unlock(&sess->cmd_mutex);

	up_write(&m2m_device.rwsem);
	return real_pop_nr;
}

static struct m2m_session *m2m_session_create(void)
{
	struct m2m_session *sess;

	m2m_enter_func();
	sess = kzalloc(sizeof(*sess), GFP_KERNEL);
	if (!sess)
		return ERR_PTR(-ENOMEM);

	get_task_struct(current->group_leader);
	task_lock(current->group_leader);
	sess->task = current->group_leader;
	sess->pid = task_tgid_vnr(current);
	sess->tid = task_pid_vnr(current);
	task_unlock(current->group_leader);

	mutex_init(&sess->m2m_mutex);
	sema_init(&sess->m2m_sem, 0);
	sess->sess_id = atomic_read(&m2m_device.m2m_curr_sess_id);
	atomic_inc(&m2m_device.m2m_curr_sess_id);
	sess->teec_sess_opened = false;

	//initialization for async mode
	mutex_init(&sess->cmd_mutex);
	init_waitqueue_head(&sess->wq);
	sess->total_cmdnr = 0;
	sess->finished_cmdnr = 0;

	down_write(&m2m_device.rwsem);
	list_add_tail(&sess->node, &m2m_device.sess_list);
	up_write(&m2m_device.rwsem);

	return sess;
}

static void m2m_session_destroy(struct m2m_session *sess)
{
	int pop_nr;

	m2m_enter_func();
	pop_nr = sess_pop_cmd(sess, 0);
	if (pop_nr) {
		pr_err("err: still remain unfinished cmds[%d:%d] when relase m2m sess\n",
			pop_nr, sess->finished_cmdnr);
		if (sess->fmode == M2M_FEED_MODE_SYNC)
			up(&sess->m2m_sem);
	}

	mutex_lock(&sess->m2m_mutex);
	if (sess->teec_sess_opened) {
		m2m_wrapper_close_session(&sess->teec_sess);
		sess->teec_sess_opened = false;
	}

	down_write(&m2m_device.rwsem);
	list_del(&sess->node);
	up_write(&m2m_device.rwsem);

	if (sess->task)
		put_task_struct(sess->task);

	mutex_unlock(&sess->m2m_mutex);
	kfree(sess);
}

static int m2m_set_mode(struct m2m_session *sess, enum m2m_crypto_mode mode)
{
	int ret = 0;

	m2m_enter_func();
	mutex_lock(&sess->m2m_mutex);

	if (!sess->teec_sess_opened) {
		ret = m2m_wrapper_open_session(&sess->teec_sess, mode);
		if (ret) {
			pr_err("m2m_wrapper_open_session(mode: %d) failed %d\n",
				mode, ret);
			goto end;
		}
		sess->teec_sess_opened = true;
	} else {
		pr_err("m2m[%d] already set crypto mode\n", sess->sess_id);
		ret = -EEXIST;
	}

end:
	mutex_unlock(&sess->m2m_mutex);
	return ret;
}

static int m2m_set_scheme(struct m2m_session *sess, enum m2m_crypto_type type)
{
	int ret = 0;

	m2m_enter_func();
	mutex_lock(&sess->m2m_mutex);

	if (!sess->teec_sess_opened) {
		pr_err("error! m2m teec session was not created\n");
		ret = -ESRCH;
		goto end;
	}

	ret = m2m_wrapper_set_scheme(&sess->teec_sess, type);
	if (ret)
		pr_err("m2m_wrapper_set_scheme(type: %d) failed %d\n", type, ret);

end:
	mutex_unlock(&sess->m2m_mutex);
	return ret;
}

static int m2m_set_key(struct m2m_session *sess, struct m2m_key *config)
{
	int ret = 0;

	m2m_enter_func();
	mutex_lock(&sess->m2m_mutex);

	if (!sess->teec_sess_opened) {
		pr_err("error! m2m teec session was not created\n");
		ret = -ESRCH;
		goto end;
	}

	ret = m2m_wrapper_config(&sess->teec_sess, config);
	if (ret)
		pr_err("m2m_wrapper_config failed %d\n", ret);

end:
	mutex_unlock(&sess->m2m_mutex);
	return ret;
}

static int m2m_set_pattern_mode(struct m2m_session *sess,
					struct m2m_pattern_mode *pattern_mode)
{
	int ret = 0;

	m2m_enter_func();
	mutex_lock(&sess->m2m_mutex);

	if (!sess->teec_sess_opened) {
		pr_err("error! m2m teec session was not created\n");
		ret = -ESRCH;
		goto end;
	}

	ret = m2m_wrapper_set_patternMode(&sess->teec_sess, pattern_mode);
	if (ret)
		pr_err("m2m_wrapper_set_patternMode[%d:%d] failed %d\n",
			pattern_mode->pattern_enc, pattern_mode->pattern_clr, ret);

end:
	mutex_unlock(&sess->m2m_mutex);
	return ret;
}

static int m2m_set_residue_mode(struct m2m_session *sess,
					enum m2m_residue_mode residue_mode)
{
	int ret = 0;

	m2m_enter_func();
	mutex_lock(&sess->m2m_mutex);

	if (!sess->teec_sess_opened) {
		pr_err("error! m2m teec session was not created\n");
		ret = -ESRCH;
		goto end;
	}

	ret = m2m_wrapper_set_residueMode(&sess->teec_sess, residue_mode);
	if (ret)
		pr_err("m2m_wrapper_set_residueMode(%d) failed %d\n", residue_mode, ret);

end:
	mutex_unlock(&sess->m2m_mutex);
	return ret;
}

static int internal_get_m2m_buffer_memid(struct m2m_buf *mbuf, unsigned int *memid)
{
	int ret = 0;
	struct bm_pt_param pt_param = {0};
	struct dma_buf *dmabuf;

	dmabuf = dma_buf_get(mbuf->fd);
	if (IS_ERR(dmabuf)) {
		pr_err("failed to get dmabuf from fd: %d\n", mbuf->fd);
		return PTR_ERR(dmabuf);
	}

	ret = bm_fetch_pt(dmabuf, &pt_param);
	if (ret) {
		pr_err("failed to get pt from dmabuf fd: %d ret = %d\n", mbuf->fd, ret);
		ret = -EINVAL;
		goto end;
	}

	*memid = pt_param.mem_id;
end:
	dma_buf_put(dmabuf);
	return ret;
}



static int m2m_update(struct m2m_session *sess, struct m2m_mem *mem)
{
	int ret = 0;
	// phys_addr_t input_phy, output_phy;
	unsigned int input_memid, output_memid;

	m2m_enter_func();
	mutex_lock(&sess->m2m_mutex);

	if (!sess->teec_sess_opened) {
		pr_err("error! m2m teec session was not created\n");
		ret = -ESRCH;
		goto end;
	}

	ret = internal_get_m2m_buffer_memid(&mem->inbuf, &input_memid);
	if (ret) {
		pr_err("error! invalid input m2m buffer\n");
		goto end;
	}

	ret = internal_get_m2m_buffer_memid(&mem->outbuf, &output_memid);
	if (ret) {
		pr_err("error! invalid output m2m buffer\n");
		goto end;
	}

	pr_debug("update: input[%u, 0x%x]  output[%u, 0x%x]\n",
		input_memid, mem->inbuf.size, output_memid, mem->outbuf.size);

	//it is better to push the sess to list(for isr callback) firstly, in case tz call
	// costs much time but the interrupt has already come
	sess_push_cmd(sess);

	ret = m2m_wrapper_update(&sess->teec_sess,
			input_memid, mem->inbuf.size, mem->inbuf.offset,
			output_memid, mem->outbuf.size, mem->outbuf.offset);

	if (ret) {
		pr_err("m2m_wrapper_update failed %d\n", ret);
		sess_pop_cmd(sess, 1);
	} else {
		if (sess->fmode == M2M_FEED_MODE_SYNC) {
			ret = down_interruptible(&sess->m2m_sem);
			if (unlikely(ret < 0)) {
				pr_err("down_interruptible failed 0x%x\n", ret);
				sess_pop_cmd(sess, 1);
			} else
				ret = 0;
		}
	}

end:
	mutex_unlock(&sess->m2m_mutex);
	return ret;
}

static int m2m_query(struct m2m_session *sess, __u32 *finished_cmdnr)
{
	int ret = 0;

	m2m_enter_func();
	mutex_lock(&sess->m2m_mutex);

	if (!sess->teec_sess_opened) {
		pr_err("error! m2m teec session was not created\n");
		ret = -ESRCH;
		goto end;
	}

	mutex_lock(&sess->cmd_mutex);
	*finished_cmdnr = sess->finished_cmdnr;
	sess->total_cmdnr -= sess->finished_cmdnr;
	pr_debug("m2m[%d] cmd: finished %d, remain: %d\n", sess->sess_id,
			sess->finished_cmdnr, sess->total_cmdnr);
	sess->finished_cmdnr = 0;
	mutex_unlock(&sess->cmd_mutex);

end:
	mutex_unlock(&sess->m2m_mutex);
	return ret;
}

/* m2m_routine_work updates the cmds per active sess, and it removes
 * the m2m sess from the active sess list when all cmds finished, so its behavior
 * should be aligned with sess_push_cmd and sess_pop_cmd to avoid dead lock,
 * the calling sequence of them follows below
 * down sess sem -> lock cmd mutext->handle cmds and active sess list ->
 * unlock cmd mutext -> up sess sem
 */
static void m2m_routine_work(struct work_struct *data)
{
	struct m2m_device_t *pdev = container_of(data, struct m2m_device_t, work);
	struct list_head *curr, *next;
	struct m2m_session *tmp;
	unsigned int cmdFinishNum;
	int ret;

	down_write(&pdev->rwsem);
	list_for_each_safe(curr, next, &pdev->active_sess_list) {
		tmp = list_entry(curr, struct m2m_session, active_node);
		ret = m2m_wrapper_routine(&tmp->teec_sess, &cmdFinishNum);
		if (unlikely(ret)) {
			pr_err("m2m routine failed, %d\n", ret);
		} else if (cmdFinishNum) {
			if (tmp->fmode == M2M_FEED_MODE_SYNC) {
				mutex_lock(&tmp->cmd_mutex);
				tmp->total_cmdnr = 0;
				list_del(&tmp->active_node);
				mutex_unlock(&tmp->cmd_mutex);
				up(&tmp->m2m_sem);
			} else {
				mutex_lock(&tmp->cmd_mutex);
				tmp->finished_cmdnr += cmdFinishNum;

				if (unlikely(tmp->finished_cmdnr > tmp->total_cmdnr)) {
					pr_err("m2m[%d] in error cmd status! finished[%d], total[%d]\n",
						tmp->sess_id, tmp->finished_cmdnr, tmp->total_cmdnr);
					//should not happen, anyway set error handling for it
					tmp->finished_cmdnr = tmp->total_cmdnr;
				}

				if (tmp->total_cmdnr == tmp->finished_cmdnr)
					list_del(&tmp->active_node);
				pr_debug("m2m[%d] cmd status[%d: %d]\n", tmp->sess_id,
					tmp->total_cmdnr, tmp->finished_cmdnr);
				mutex_unlock(&tmp->cmd_mutex);

				/* wake up any blocked sess */
				wake_up_interruptible(&tmp->wq);
			}
		}
	}
	up_write(&pdev->rwsem);

}

static irqreturn_t m2m_drv_isr(int irq, void *dev_id)
{
	struct m2m_device_t *pdev = (struct m2m_device_t *)dev_id;
	u32 addr, val;

	addr = RA_TspIntReg_software_int_status;
	M2M_REG_WORD32_READ(addr, &val);

	pr_debug("%s read(%p) status 0x%x\n", __func__,
			m2m_device.intr_virt_addr + addr, val);

	/*currently there is only one event (TSP_INTR_MASK_M2M) from Figo, and tsp.ko
	 * still need to handle it, since m2m.ko is an optional module, so this shared event is
	 * wroten in tsp.ko,  then here the val is always 0,
	 * the better way for it is to avoid using shared event in future
	 */
	if (val != 0) {
		pr_debug("none zero event 0x%x\n", val);
		M2M_REG_WORD32_WRITE(addr, val);
	}

	/* list_empty may not the real value, since race condition may occur here,
	 * then the bad impact is do invliad calling of m2m_routine_work, which had no
	 * side effect even when the sess list is empty
	 * Add list_empty judgement here is to avoid too much call of m2m_routine_work,
	 * since currently m2m and tsp shared this interrupt, in future, after classifing the
	 * interrupts, then can remove the list_empty here
	 */
	if (!list_empty(&pdev->active_sess_list))
		queue_work(pdev->work_queue, &pdev->work);

	return IRQ_HANDLED;
}

static int m2m_drv_open(struct inode *inode, struct file *file)
{
	struct m2m_session *sess;
	int ret = 0;

	sess = m2m_session_create();
	if (IS_ERR(sess))
		return PTR_ERR(sess);
	pr_debug("m2m sess %d opened\n", sess->sess_id);
	file->private_data = sess;
	if (file->f_flags & O_NONBLOCK)
		sess->fmode = M2M_FEED_MODE_ASYNC;
	else
		sess->fmode = M2M_FEED_MODE_SYNC;

	mutex_lock(&m2m_device.sess_mutex);

	if (atomic_inc_return(&m2m_device.m2m_sess_refcnt) == 1) {
		pr_debug("register m2m irq %d!\n", m2m_device.irq_num);
		ret = request_irq(m2m_device.irq_num, m2m_drv_isr, IRQF_SHARED,
			M2M_MODULE_NAME, &m2m_device);
		if (unlikely(ret < 0)) {
			pr_err("tsp irq:%5d, err:%8x\n", m2m_device.irq_num, ret);
			m2m_session_destroy(sess);
			atomic_dec_return(&m2m_device.m2m_sess_refcnt);
		} else {
			pr_debug("m2m request_irq success\n");
			M2M_REG_WORD32_WRITE(RA_TspIntReg_software_int_enable, 0xffff);
		}
	}

	mutex_unlock(&m2m_device.sess_mutex);

	return ret;
}

static int m2m_drv_release(struct inode *inode, struct file *file)
{
	struct m2m_session *sess = file->private_data;

	pr_debug("m2m sess %d release\n", sess->sess_id);
	m2m_session_destroy(sess);
	if (atomic_dec_return(&m2m_device.m2m_sess_refcnt) == 0) {
		M2M_REG_WORD32_WRITE(RA_TspIntReg_software_int_enable, 0x0);
		free_irq(m2m_device.irq_num, (void *)&m2m_device);
		pr_debug("m2m free_irq success\n");
	}
	return 0;
}

static long m2m_drv_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	unsigned int dir;
	struct m2m_session *sess = filp->private_data;
	union m2m_ioctl_arg data;

	dir = _IOC_DIR(cmd);
	if (_IOC_SIZE(cmd) > sizeof(data))
		return -EINVAL;

	pr_debug("m2m[%d] ioctl cmd: 0x%x\n", sess->sess_id, cmd);
	switch (cmd) {
	case M2M_IOC_SET_MODE:
		data.crypto_mode = (enum m2m_crypto_mode)arg;
		ret = m2m_set_mode(sess, data.crypto_mode);
		break;
	case M2M_IOC_SET_SCHEME:
		data.crypto_type = (enum m2m_crypto_type)arg;
		ret = m2m_set_scheme(sess, data.crypto_type);
		break;
	case M2M_IOC_SET_KEY:
		if (copy_from_user(&data, (void __user *)arg, _IOC_SIZE(cmd)))
			return -EFAULT;
		ret = m2m_set_key(sess, &data.config);
		break;
	case M2M_IOC_SET_PATTERN_MODE:
		if (copy_from_user(&data, (void __user *)arg, _IOC_SIZE(cmd)))
			return -EFAULT;
		ret = m2m_set_pattern_mode(sess, &data.pattern_mode);
		break;
	case M2M_IOC_SET_RESIDUE_MODE:
		data.residue_mode = (enum m2m_residue_mode)arg;
		ret = m2m_set_residue_mode(sess, data.residue_mode);
		break;
	case M2M_IOC_UPDATE:
		if (copy_from_user(&data, (void __user *)arg, _IOC_SIZE(cmd)))
			return -EFAULT;
		ret = m2m_update(sess, &data.mem);
		break;
	case M2M_IOC_QUERY:
		memset(&data, 0, sizeof(data));
		ret = m2m_query(sess, &data.finished_cmdnr);
		break;
	default:
		return -EINVAL;
	}

	if (dir & _IOC_READ) {
		if (copy_to_user((void __user *)arg, &data, _IOC_SIZE(cmd)))
			return -EFAULT;
	}

	return ret;
}

static unsigned int m2m_drv_poll(struct file *file, poll_table *wait)
{
	unsigned int mask = 0;
	struct m2m_session *sess = file->private_data;

	//don't support sync mode
	if (sess->fmode == M2M_FEED_MODE_SYNC)
		return mask;

	poll_wait(file, &sess->wq, wait);
	mutex_lock(&sess->cmd_mutex);
	if (sess->finished_cmdnr)
		mask |= POLLIN | POLLRDNORM;
	pr_debug("%s cmd status[%d: %d]\n", __func__, sess->total_cmdnr, sess->finished_cmdnr);
	mutex_unlock(&sess->cmd_mutex);

	return mask;
}
/*********************************************************************
 * Module Register API
 */

static const struct file_operations m2m_ops = {
	.open = m2m_drv_open,
	.release = m2m_drv_release,
	.unlocked_ioctl = m2m_drv_ioctl,
	.compat_ioctl = m2m_drv_ioctl,
	.poll = m2m_drv_poll,
	.owner = THIS_MODULE,
};

static const struct of_device_id m2m_match[] = {
	{
		.compatible = "syna,berlin-m2m",
	},
	{},
};

static int m2m_drv_init(struct m2m_device_t *pdev)
{
	struct cdev *dev = &pdev->cdev;
	int res;

	/* Now setup cdevs. */
	cdev_init(dev, pdev->fops);
	dev->owner = THIS_MODULE;
	res = cdev_add(dev, pdev->dev_id, 1);
	if (res) {
		pr_err("m2m driver cdev_add failed.\n");
		res = -ENODEV;
		goto err_add_device;
	}
	pr_info("setup cdevs device minor [%d]\n", MINOR(pdev->dev_id));

	/* add M2m class to sysfs */
	pdev->dev_class = m2m_create_dev_class(pdev->dev_name);
	if (IS_ERR(pdev->dev_class)) {
		pr_err("class_create failed.\n");
		res = -ENODEV;
		goto err_add_device;
	}

	device_create(pdev->dev_class, NULL, pdev->dev_id, NULL, pdev->dev_name);
	pr_info("create device [%s]\n", pdev->dev_name);

	mutex_init(&pdev->sess_mutex);
	init_rwsem(&pdev->rwsem);
	INIT_LIST_HEAD(&pdev->sess_list);
	INIT_LIST_HEAD(&pdev->active_sess_list);

	pdev->work_queue = alloc_ordered_workqueue("berlin_m2m", WQ_MEM_RECLAIM);

	if (pdev->work_queue == NULL) {
		pr_err("Create m2m WorkQueue failed.\n");
		res = -EFAULT;
		goto err_add_device;
	}

	INIT_WORK(&pdev->work, m2m_routine_work);

	res = m2m_wrapper_init(pdev->dev);
	if (res) {
		pr_err("error in m2m teec init: %d\n", res);
		destroy_workqueue(pdev->work_queue);
		goto err_add_device;
	}

	pdev->debug_root = debugfs_create_dir("m2m", NULL);
	pdev->sess_debug_root = debugfs_create_file("status", 0664,
					pdev->debug_root, (void *)pdev,
					&debug_sess_fops);

	return 0;

err_add_device:
	if (pdev->dev_class) {
		device_destroy(pdev->dev_class, pdev->dev_id);
		class_destroy(pdev->dev_class);
	}

	cdev_del(&pdev->cdev);

	return res;
}

static int m2m_drv_exit(struct m2m_device_t *pdev)
{
	if (pdev->dev_class) {
		/* del sysfs entries */
		device_destroy(pdev->dev_class, pdev->dev_id);
		class_destroy(pdev->dev_class);
	}
	/* del cdev */
	cdev_del(&pdev->cdev);

	mutex_destroy(&pdev->sess_mutex);

	flush_workqueue(pdev->work_queue);
	destroy_workqueue(pdev->work_queue);
	m2m_wrapper_exit();

	debugfs_remove_recursive(pdev->sess_debug_root);
	debugfs_remove_recursive(pdev->debug_root);

	clk_disable_unprepare(pdev->tsp_clk_core);

	return 0;
}

static int m2m_drv_probe(struct platform_device *pdev)
{
	int ret;
	struct device *dev = &pdev->dev;

	m2m_enter_func();

#if !IS_ENABLED(CONFIG_OPTEE)
	/* Defer probe since there is dependency of tzd */
	if (!tzd_get_kernel_dev_file())
		return -EPROBE_DEFER;

	if (!tz_get_tsp_ta_status())
		return -EPROBE_DEFER;
#endif

	m2m_device.fops = &m2m_ops;
	m2m_device.dev = dev;
	dev_set_drvdata(dev, (void *)&m2m_device);
	device_rename(dev, M2M_DEVICE_NAME);
	pr_info("m2m device name is %s\n", dev_name(dev));

	m2m_device.irq_num = platform_get_irq(pdev, 0);
	if (m2m_device.irq_num <= 0) {
		pr_err("failed to get irq for M2M\n");
		ret = -ENODEV;
		goto  err_fail;
	}

	m2m_device.res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	//since its region overlaps the region of tsp driver, devm_ioremap_resource will fail
	m2m_device.intr_virt_addr = ioremap(m2m_device.res->start, resource_size(m2m_device.res));
	if (!m2m_device.intr_virt_addr) {
		pr_err("failed to map TSP interrupt registers!\n");
		ret = -ENOMEM;
		goto err_fail;
	}

	m2m_device.tsp_clk_core = devm_clk_get_optional(&pdev->dev, "core");
	if (IS_ERR(m2m_device.tsp_clk_core)) {
		ret = PTR_ERR(m2m_device.tsp_clk_core);
		pr_err("error in getting core clk handle: %d\n", ret);
		goto err_clk_get;
	}

	clk_prepare_enable(m2m_device.tsp_clk_core);

	ret = alloc_chrdev_region(&m2m_device.dev_id, 0, M2M_MAX_DEVS, M2M_DEVICE_NAME);
	if (ret < 0) {
		pr_err("alloc_chrdev_region() failed for ovp\n");
		goto err_alloc_chrdev_region;
	}

	ret = m2m_drv_init(&m2m_device);
	if (ret)
		goto err_drv_init;

	return 0;

err_drv_init:
	unregister_chrdev_region(m2m_device.dev_id, M2M_MAX_DEVS);
err_alloc_chrdev_region:
	clk_disable_unprepare(m2m_device.tsp_clk_core);
err_clk_get:
	iounmap(m2m_device.intr_virt_addr);
err_fail:
	pr_err("%s failed !!! (%d)\n", __func__, ret);

	return ret;
}

static RET_TYPE m2m_drv_remove(struct platform_device *pdev)
{
	m2m_drv_exit(&m2m_device);
	unregister_chrdev_region(m2m_device.dev_id, M2M_MAX_DEVS);
	iounmap(m2m_device.intr_virt_addr);

	RETURN_VALUE;
}

static struct platform_driver m2m_driver = {
	.probe = m2m_drv_probe,
	.remove = m2m_drv_remove,
	.driver = {
		.name = M2M_DEVICE_NAME,
		.of_match_table = m2m_match,
	},
};
module_platform_driver(m2m_driver);

MODULE_AUTHOR("synaptics");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("M2M module driver");
MODULE_IMPORT_NS(SYNA_BM);
MODULE_IMPORT_NS(DMA_BUF);