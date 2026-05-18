// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2018-2020 Synaptics Incorporated */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/of_device.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include "berlin_pcm.h"
#include "berlin_capture.h"
#include "berlin_playback.h"

#include "aio_hal.h"
#include "avio_dhub_drv.h"
#include "avio_common.h"

/* Enable the buffer status debugging. As some debug function may be called from isr, in case printk console log
 * level is high, isr would be waiting in console driver which would make the underrun situation more worst.
 * Enable it only when needed.
 */
//#define BUF_STATE_DEBUG

static struct berlin_chip *dev_to_berlin_chip(struct device *dev)
{
	struct snd_card *card = NULL;

	card = dev_get_drvdata(dev);
	if (card == NULL)
		return NULL;
	return (struct berlin_chip *)card->private_data;
}

void berlin_report_xrun(struct berlin_chip *chip,
			enum berlin_xrun_t xrun_type)
{
	atomic_long_inc(chip->xruns + xrun_type);
}

static enum berlin_xrun_t berlin_xrun_string_to_type(const char *string)
{
	if (strcmp("pcm_overrun", string) == 0)
		return PCM_OVERRUN;
	if (strcmp("fifo_overrun", string) == 0)
		return FIFO_OVERRUN;
	if (strcmp("pcm_underrun", string) == 0)
		return PCM_UNDERRUN;
	if (strcmp("fifo_underrun", string) == 0)
		return FIFO_UNDERRUN;
	if (strcmp("irq_disable_us", string) == 0)
		return IRQ_DISABLE;
#ifdef BUF_STATE_DEBUG
	snd_printd("%s: unrecognized xrun type: %s\n", __func__, string);
#endif
	return XRUN_T_MAX;
}

static ssize_t
xrun_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	unsigned long overruns;

	struct berlin_chip *chip = NULL;
	enum berlin_xrun_t xrun_type =
		berlin_xrun_string_to_type(attr->attr.name);

	if (xrun_type == XRUN_T_MAX)
		return -EINVAL;

	chip = dev_to_berlin_chip(dev);
	if (chip == NULL)
		return -ENODEV;
	overruns = atomic_long_read(chip->xruns + xrun_type);
	return snprintf(buf, PAGE_SIZE, "%lu\n", overruns);
}

static ssize_t
xrun_store(struct device *dev,
	   struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long overruns;
	int err;

	struct berlin_chip *chip = NULL;
	enum berlin_xrun_t xrun_type =
		berlin_xrun_string_to_type(attr->attr.name);

	if (xrun_type == XRUN_T_MAX)
		return -EINVAL;

	chip = dev_to_berlin_chip(dev);
	if (chip == NULL)
		return -ENODEV;

	err = kstrtoul(buf, 10, &overruns);
	if (err < 0)
		return err;

	// TODO(yichunko): Remove the following after underrun issue is
	// resolved
	// in IRQ_DISABLE mode, overruns = # of us to block
	if (xrun_type == IRQ_DISABLE) {
		unsigned long flags;

		local_irq_save(flags);
#ifdef BUF_STATE_DEBUG
		snd_printd("block for %lu us.\n", overruns);
#endif
		udelay(overruns);
		local_irq_restore(flags);
	}
	// TODO(yichunko)
	atomic_long_set(chip->xruns + xrun_type, overruns);
	return count;
}

static DEVICE_ATTR(pcm_overrun, 0644, xrun_show, xrun_store);
static DEVICE_ATTR(fifo_overrun, 0644, xrun_show, xrun_store);
static DEVICE_ATTR(pcm_underrun, 0644, xrun_show, xrun_store);
static DEVICE_ATTR(fifo_underrun, 0644, xrun_show, xrun_store);
static DEVICE_ATTR(irq_disable_us, 0644, xrun_show, xrun_store);

static struct attribute *berlin_xrun_sysfs_entries[] = {
	&dev_attr_pcm_overrun.attr,
	&dev_attr_fifo_overrun.attr,
	&dev_attr_pcm_underrun.attr,
	&dev_attr_fifo_underrun.attr,
	&dev_attr_irq_disable_us.attr,
	NULL,
};

static const struct attribute_group berlin_sysfs_group = {
	.name	= "xrun",
	.attrs	= berlin_xrun_sysfs_entries,
};

static irqreturn_t berlin_alsa_io_isr(int irq, void *dev_id)
{
	struct snd_pcm_substream *substream;
	struct irq_data *irqd;
	irq_hw_number_t hw_irq;
	int chan_id;

	substream = dev_id;
	irqd = irq_get_irq_data(irq);
	if (!irqd)
		return IRQ_NONE;

	hw_irq = irqd_to_hwirq(irqd);
	chan_id = (int)hw_irq;

	if (substream->runtime) {
		if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
			berlin_capture_isr(substream);
		else if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			berlin_playback_isr(substream, chan_id);
	}

	return IRQ_HANDLED;
}

int berlin_pcm_request_dma_irq(struct snd_pcm_substream *substream,
			       struct berlin_ss_params *params)
{
	u32 ch[MAX_CHID], i, irq_num, chid_num;
	int err;
	void *dev_id = substream;

	if (!params) {
		snd_printk("%s: null params input\n", __func__);
		return -EINVAL;
	}

	irq_num = params->irq_num;
	chid_num = params->chid_num;
	if (irq_num == 0 || chid_num == 0 || irq_num > chid_num) {
		snd_printk("%s: invalid num irq %d chid %d\n",
			   __func__, irq_num, chid_num);
		return 0;
	}

	for (i = 0; i < irq_num; i++) {
		err = request_irq(params->irq[i], berlin_alsa_io_isr, IRQF_SHARED,
				  params->dev_name, dev_id);
		if (unlikely(err < 0)) {
			snd_printk("irq %d request error: %d\n",
				   params->irq[i], err);
			return err;
		}
	}

	for (i = 0; i < chid_num; i++)
	{
		struct irq_data *irqd = irq_get_irq_data(params->irq[i]);

		if (!irqd)
			return -EINVAL;
		ch[i] = irqd_to_hwirq(irqd);
	}

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		berlin_capture_set_ch_mode(substream, chid_num, ch,
					   params->mode,
					   params->enable_mic_mute,
					   params->interleaved,
					   params->dummy_data,
					   params->multi_lanes,
					   params->channel_map,
					   params->ch_shift_check,
					   params->aio_handle);
	else if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		berlin_playback_set_ch_mode(substream, chid_num, ch,
						params->mode, params->dai_fmt,
						params->dai_stop_fn,
						params->dai_stop_data);

	return err;
}
EXPORT_SYMBOL(berlin_pcm_request_dma_irq);

void berlin_pcm_free_dma_irq(struct snd_pcm_substream *substream,
			     u32 irq_num,
			     unsigned int *irq)
{
	u32 i = 0;

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		berlin_capture_set_ch_mode(substream, 0, 0, 0,
					false, false, false, false, 0, 0, NULL);
	else if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		berlin_playback_set_ch_mode(substream, 0, 0, 0, 0, NULL, NULL);

	for (i = 0; i < irq_num; i++)
		free_irq(irq[i], (void *)substream);
}
EXPORT_SYMBOL(berlin_pcm_free_dma_irq);

u32 berlin_get_pause_count(struct snd_pcm_substream *substream)
{
	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		return berlin_capture_get_pause_count(substream);
	else if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		return berlin_playback_get_pause_count(substream);

	return 0;
}
EXPORT_SYMBOL(berlin_get_pause_count);

void berlin_pcm_max_ch_inuse(struct snd_pcm_substream *substream,
			     u32 ch_num)
{
	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		berlin_capture_set_ch_inuse(substream, ch_num);
}
EXPORT_SYMBOL(berlin_pcm_max_ch_inuse);

bool berline_pcm_passthrough_check(struct snd_pcm_substream *substream,
			       u32 *data_type)
{
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		return berlin_playback_passthrough_check(substream, data_type);

	return false;
}
EXPORT_SYMBOL(berline_pcm_passthrough_check);

void berlin_pcm_hdmi_bitstream_start(struct snd_pcm_substream *substream)
{
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		berlin_playback_hdmi_bitstream_start(substream);
}
EXPORT_SYMBOL(berlin_pcm_hdmi_bitstream_start);

int berlin_pcm_indai_aip_alloc(struct snd_pcm_substream *ss, void **pFrame, int *size)
{
	return berlin_indai_aip_alloc(ss, pFrame, size);
}
EXPORT_SYMBOL(berlin_pcm_indai_aip_alloc);

void berlin_pcm_set_ch_mode(struct snd_pcm_substream *ss,
	u32 chid_num, u32 *chid, u32 mode,
	bool enable_mic_mute, bool interleaved_mode,
	bool dummy_data, bool multi_lanes, u32 channel_map,
	bool ch_shift_check, void * aio_handle)
{
	return berlin_capture_set_ch_mode(ss, chid_num, chid, mode,
		enable_mic_mute, interleaved_mode, dummy_data, multi_lanes, channel_map,
		ch_shift_check, aio_handle);
}
EXPORT_SYMBOL(berlin_pcm_set_ch_mode);

void berlin_pcm_set_spdif_sample_rate_provider(struct snd_pcm_substream *ss,
				u32 (*get_fs_fn)(void *data), void *data)
{
	if (ss->stream == SNDRV_PCM_STREAM_CAPTURE)
		berlin_capture_set_spdif_sample_rate_provider(ss, get_fs_fn, data);
}
EXPORT_SYMBOL(berlin_pcm_set_spdif_sample_rate_provider);

void berlin_pcm_set_volume_db(struct snd_pcm_substream *ss, u32 mode, int volume_db)
{
	if (ss->stream == SNDRV_PCM_STREAM_PLAYBACK)
		berlin_playback_set_volume_db(ss, mode, volume_db);
}
EXPORT_SYMBOL(berlin_pcm_set_volume_db);

void berlin_pcm_set_mic_mute_state(struct snd_pcm_substream *ss, int mute)
{
	return set_mic_mute_state(ss, mute);
}
EXPORT_SYMBOL(berlin_pcm_set_mic_mute_state);

int berlin_pcm_aip_event_callback_newframe(struct snd_pcm_substream *ss)
{
	return berlin_aip_event_callback_newframe(ss);
}
EXPORT_SYMBOL(berlin_pcm_aip_event_callback_newframe);

static int berlin_pcm_open(struct snd_soc_component *component,
							struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_component *c = snd_soc_rtdcom_lookup(rtd, "syna-berlin-pcm");
	struct berlin_chip *chip;
	struct device *dev;

	if (!c)
		return -EINVAL;

	dev = c->dev;
	chip = dev_get_drvdata(dev);

	snd_printd("%s stream: %s (%p)\n", __func__,
		substream->stream == SNDRV_PCM_STREAM_CAPTURE ? "capture" : "playback", substream);
	/* get dhub handle */
	if (mutex_lock_interruptible(&chip->dhub_lock) != 0)
		return  -EINTR;
	if (chip->dhub == NULL) {
		chip->dhub = Dhub_GetDhubHandle_ByDhubId(DHUB_ID_AG_DHUB);
		if (unlikely(chip->dhub == NULL)) {
			snd_printk("chip->dhub: get failed\n");
			mutex_unlock(&chip->dhub_lock);
			return -EBUSY;
		}
	}
	mutex_unlock(&chip->dhub_lock);

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		return berlin_capture_open(substream);
	else if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		return berlin_playback_open(substream, chip->passthrough_enable);

	return 0;
}

static int berlin_pcm_close(struct snd_soc_component *component,
							struct snd_pcm_substream *substream)
{
	snd_printd("%s stream: %s (%p)\n", __func__,
		substream->stream == SNDRV_PCM_STREAM_CAPTURE ? "capture" : "playback", substream);

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		return berlin_capture_close(substream);
	else if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		return berlin_playback_close(substream);

	return 0;
}

static int berlin_pcm_hw_params(struct snd_soc_component *component,
							struct snd_pcm_substream *substream,
			 struct snd_pcm_hw_params *params)
{
	snd_printd("%s stream: %s (%p)\n", __func__,
		substream->stream == SNDRV_PCM_STREAM_CAPTURE ? "capture" : "playback", substream);

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		return berlin_capture_hw_params(substream, params);
	else if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		return berlin_playback_hw_params(substream, params);

	return 0;
}

static int berlin_pcm_hw_free(struct snd_soc_component *component,
							struct snd_pcm_substream *substream)
{
	snd_printd("%s stream: %s (%p)\n", __func__,
		substream->stream == SNDRV_PCM_STREAM_CAPTURE ? "capture" : "playback", substream);

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		return berlin_capture_hw_free(substream);
	else if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		return berlin_playback_hw_free(substream);

	return 0;
}

static int berlin_pcm_prepare(struct snd_soc_component *component,
							struct snd_pcm_substream *substream)
{
	snd_printd("%s stream: %s (%p)\n", __func__,
		substream->stream == SNDRV_PCM_STREAM_CAPTURE ? "capture" : "playback", substream);

	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		return berlin_capture_prepare(substream);
	else if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		return berlin_playback_prepare(substream);

	return 0;
}

static int berlin_pcm_trigger(struct snd_soc_component *component,
							struct snd_pcm_substream *substream, int cmd)
{
	struct snd_pcm_substream *s;

	snd_printd("%s stream: %s %s (%p)\n", __func__,
		substream->stream == SNDRV_PCM_STREAM_CAPTURE ? "capture" : "playback",
		cmd == SNDRV_PCM_TRIGGER_START ? "start" : "stop",
		substream);

	snd_pcm_group_for_each_entry(s, substream) {
		if (s->stream == SNDRV_PCM_STREAM_CAPTURE)
			berlin_capture_trigger(s, cmd);
		else if (s->stream == SNDRV_PCM_STREAM_PLAYBACK)
			berlin_playback_trigger(s, cmd);
	}

	return 0;
}

static snd_pcm_uframes_t
berlin_pcm_pointer(struct snd_soc_component *component,
					struct snd_pcm_substream *substream)
{
	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		return berlin_capture_pointer(substream);
	else if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		return berlin_playback_pointer(substream);

	return 0;
}

static int berlin_pcm_ack(struct snd_soc_component *component,
							struct snd_pcm_substream *substream)
{
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		return berlin_playback_ack(substream);

	return 0;
}

#define PREALLOC_BUFFER (2 * 1024 * 1024)
#define PREALLOC_BUFFER_MAX (2 * 1024 * 1024)
/*
 * ALSA API channel-map control callbacks
 */
static int berlin_pcm_chmap_ctl_info(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 8;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = SNDRV_CHMAP_LAST;
	return 0;
}

static int berlin_pcm_chmap_ctl_get(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_pcm_chmap *info = snd_kcontrol_chip(kcontrol);
	struct snd_soc_dai *dai = info->private_data;
	u32 tx_ch[8], tx_num;
	int i;

	if (dai->driver->ops->get_channel_map) {
		dai->driver->ops->get_channel_map(dai, &tx_num, tx_ch, NULL, NULL);
		for (i = 0; i < tx_num; i++)
			ucontrol->value.integer.value[i] = tx_ch[i];
	}

	return 0;
}

static int berlin_pcm_chmap_ctl_put(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_pcm_chmap *info = snd_kcontrol_chip(kcontrol);
	struct snd_soc_dai *dai = info->private_data;
	u32 tx_ch[8], tx_num;
	int i;

	if (dai->driver->ops->set_channel_map) {
		tx_num = 8;
		for (i = 0; i < tx_num; i++)
			tx_ch[i] = ucontrol->value.integer.value[i];
		dai->driver->ops->set_channel_map(dai, tx_num, tx_ch, 0, NULL);
	}

	return 0;
}

static int berlin_pcm_chmap_ctl_tlv(struct snd_kcontrol *kcontrol, int op_flag,
			      unsigned int size, unsigned int __user *tlv)
{
	return 0;
}

static int berlin_pcm_new(struct snd_soc_component *component,
						struct snd_soc_pcm_runtime *rtd)
{
	struct snd_pcm_chmap *chmap = NULL;
	struct snd_kcontrol *kctl;
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	int err, i;

	if (rtd->pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream_count) {
		err = snd_pcm_add_chmap_ctls(rtd->pcm,
					     SNDRV_PCM_STREAM_PLAYBACK,
					     NULL, 0, 0, &chmap);
		if (err < 0)
			return err;
	} else if (rtd->pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream_count) {
		err = snd_pcm_add_chmap_ctls(rtd->pcm,
					     SNDRV_PCM_STREAM_CAPTURE,
					     NULL, 0, 0, &chmap);
		if (err < 0)
			return err;
	}

	if (chmap) {
		/* override handlers */
		kctl = chmap->kctl;
		chmap->private_data = cpu_dai;
		for (i = 0; i < kctl->count; i++)
			kctl->vd[i].access |= SNDRV_CTL_ELEM_ACCESS_WRITE;
		kctl->info = berlin_pcm_chmap_ctl_info;
		kctl->get = berlin_pcm_chmap_ctl_get;
		kctl->put = berlin_pcm_chmap_ctl_put;
		kctl->tlv.c = berlin_pcm_chmap_ctl_tlv;
	}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(6, 12, 0))
	snd_pcm_lib_preallocate_pages_for_all(rtd->pcm,
			SNDRV_DMA_TYPE_CONTINUOUS,
			snd_dma_continuous_data
			(GFP_KERNEL),
			PREALLOC_BUFFER,
			PREALLOC_BUFFER_MAX);
#else
	snd_pcm_lib_preallocate_pages_for_all(rtd->pcm,
			SNDRV_DMA_TYPE_CONTINUOUS,
			component->dev,
			PREALLOC_BUFFER,
			PREALLOC_BUFFER_MAX);
#endif

	return 0;
}

static void berlin_pcm_free(struct snd_soc_component *component, struct snd_pcm *pcm)
{
	snd_pcm_lib_preallocate_free_for_all(pcm);
}

static int berlin_pcm_hdmi_spdif_out_switch_get(
					struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *value)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct device *dev = component->dev;
	struct berlin_chip *chip = dev_get_drvdata(dev);

	value->value.integer.value[0] = chip->passthrough_enable;

	return 0;
}

static int berlin_pcm_hdmi_spdif_out_switch_put(
				     struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *value)
{
	struct snd_soc_component *component = snd_kcontrol_chip(kcontrol);
	struct device *dev = component->dev;
	struct berlin_chip *chip = dev_get_drvdata(dev);
	int changed;

	changed = value->value.integer.value[0] != chip->passthrough_enable;
	if (changed) {
		chip->passthrough_enable = value->value.integer.value[0];
		snd_printd("berlin audio passthrough: %s\n",
			value->value.integer.value[0] ? "enable" : "disable");
	}

	return changed;
}

static struct snd_kcontrol_new berlin_pcm_controls[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = SNDRV_CTL_NAME_IEC958("", PLAYBACK, SWITCH),
		.info = snd_ctl_boolean_mono_info,
		.get = berlin_pcm_hdmi_spdif_out_switch_get,
		.put = berlin_pcm_hdmi_spdif_out_switch_put,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = SNDRV_CTL_NAME_IEC958_CAPTURE_SAMPLE_RATE,
		.access = SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_VOLATILE,
		.info = berlin_capture_spdif_sample_rate_info,
		.get = berlin_capture_spdif_sample_rate_get,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = SNDRV_CTL_NAME_IEC958_CAPTURE_DEFAULT,
		.access = SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_VOLATILE,
		.info = berlin_capture_spdif_control_status_buffer_info,
		.get = berlin_capture_spdif_control_status_buffer_get,
	}
};

static struct snd_soc_component_driver berlin_pcm_component = {
	.name		= "syna-berlin-pcm",
	.pcm_construct = berlin_pcm_new,
	.pcm_destruct = berlin_pcm_free,
	.open		= berlin_pcm_open,
	.close		= berlin_pcm_close,
	.hw_params	= berlin_pcm_hw_params,
	.hw_free	= berlin_pcm_hw_free,
	.prepare	= berlin_pcm_prepare,
	.trigger	= berlin_pcm_trigger,
	.pointer	= berlin_pcm_pointer,
	.ack		= berlin_pcm_ack,
	.controls	= berlin_pcm_controls,
	.num_controls	= ARRAY_SIZE(berlin_pcm_controls),
};

static int berlin_pcm_probe(struct platform_device *pdev)
{
	struct berlin_chip *chip;
	struct device *dev = &pdev->dev;
	int ret;

	snd_printd("berlin-pcm probe %p\n", pdev);

	//Defer probe until dependent soc module/s are probed/initialized
	if (!is_avio_driver_initialized())
		return -EPROBE_DEFER;

	ret = avio_sub_module_dhub_init();
	if (ret) {
		snd_printk("%s: dhub open failed: %x\n", __func__, ret);
		return ret;
	}

	chip = devm_kzalloc(dev, sizeof(struct berlin_chip), GFP_KERNEL);
	if (chip == NULL)
		return -ENOMEM;

	chip->pdev = pdev;

	mutex_init(&chip->dhub_lock);
	of_dma_configure(dev, dev->of_node, true);

	dev_set_drvdata(&pdev->dev, chip);

	ret = devm_snd_soc_register_component(&pdev->dev,
					&berlin_pcm_component,
					NULL, 0);
	if (ret < 0)
		snd_printk("can not do snd soc register\n");

	return ret;
}

static RET_TYPE berlin_pcm_dev_remove(struct platform_device *pdev)
{
	RETURN_VALUE;
}

static const struct of_device_id berlin_pcm_of_match[] = {
	{.compatible = "syna,berlin-pcm"},
	{.compatible = "syna,as370-pcm"},
	{.compatible = "syna,dolphin-pcm"},
	{.compatible = "syna,myna-pcm"},
	{.compatible = "syna,myna2-pcm"},
	{.compatible = "syna,platypus-pcm"},
	{.compatible = "syna,klamath-pcm"},
	{}
};
MODULE_DEVICE_TABLE(of, berlin_pcm_of_match);

static struct platform_driver berlin_pcm_driver = {
	.driver			= {
		.name		= "syna-berlin-pcm",
		.of_match_table = berlin_pcm_of_match,
	},

	.probe			= berlin_pcm_probe,
	.remove			= berlin_pcm_dev_remove,
};
module_platform_driver(berlin_pcm_driver);

MODULE_AUTHOR("Synaptics");
MODULE_DESCRIPTION("Berlin PCM Driver");
MODULE_LICENSE("GPL v2");
