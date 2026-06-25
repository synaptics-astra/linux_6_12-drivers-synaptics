// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2018-2020 Synaptics Incorporated */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/input.h>

#include <sound/core.h>
#include <sound/control.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-card.h>

#include "berlin_pcm.h"
#include "berlin_capture.h"
#include "berlin_spdif.h"

#include "aio_hal.h"
#include "avio_dhub_drv.h"
#include "berlin_util.h"

/* Enable the buffer status debugging. As some debug function may be called from isr, in case printk console log
 * level is high, isr would be waiting in console driver which would make the underrun situation more worst.
 * Enable it only when needed.
 */
//#define BUF_STATE_DEBUG

#define CIC_MAX_DECIMATION_FACTOR	128
#define CIC_MAX_ORDER			5
/* For every 32 bit PCM sample there are |decimation| PDM bits. Since
 * decimation factor is always a multiple of 32, the PDM buffer sizes are
 * integer multiples of the PCM buffer sizes.
 * When adjusting buffer size, check
 * snd_pcm_lib_preallocate_pages_for_all() in pcm.c.
 */
#define PCM_SAMPLE_BITS			32
#define PDM_MAX_DMA_BUFFER_SIZE		(MAX_CHID * 1024 * 256)
#define PDM_MAX_BUFFER_SIZE		(MAX_CHID * 1024 * 1024 * 2)

/* PCM_DMA_BUFFER_SIZE = PDM_MAX_DMA_BUFFER_SIZE / 4 */
#define PCM_DMA_BUFFER_SIZE		\
	(PDM_MAX_DMA_BUFFER_SIZE / \
	 (CIC_MAX_DECIMATION_FACTOR / PCM_SAMPLE_BITS))

#define PCM_DMA_MIN_SIZE		(1024)

/* PCM_MAX_BUFFER_SIZE = PDM_MAX_BUFFER_SIZE / 4 */
#define PCM_MAX_BUFFER_SIZE		\
	(PDM_MAX_BUFFER_SIZE / \
	 (CIC_MAX_DECIMATION_FACTOR / PCM_SAMPLE_BITS))

#define MUTE_THRESHOLD_MS   300

#define DHUB_FIFO_DEPTH 32

/* dmic multi-channel shift check count. At least 3 */
#define DMIC_CH_CHECK_CNT 3

enum berlin_mic_mute_state {
	BERLIN_MIC_MUTE_STATE_UNKNOWN = -1,
	BERLIN_MIC_MUTE_STATE_UNMUTE = 0,
	BERLIN_MIC_MUTE_STATE_MUTE = 1
};

struct cic_decimator {
	s32 fifo_lw5[5][2];
	s32 fifo_lw9[4];
	s32 fifo_lw13[6];
	s32 order;
	s32 decimation;
	s32 bmax;
	s32 (*process)(struct cic_decimator *cic, u32 *data);
};

struct berlin_capture {
	/*
	 * Tracks the base address of the last submitted DMA block.
	 * Moved to next period in ISR.
	 * read in berlin_playback_pointer.
	 */
	u32 current_dma_offset;
	/*
	 * Offset of the next DMA buffer that needs to be decimated in bytes.
	 * Since it is only read/written on a work queue thread,
	 * it does not need locking.
	 */
	u32 read_offset;
	/*
	 * Offset in bytes at which decoded PCM data is being written.
	 * Writing should only be done on a work queue thread; must be locked.
	 * Reading on a work queue thread does not require locking.
	 * Reading outside of a work queue thread requires locking.
	 */
	u32 runtime_offset;
	/*
	 * Number of bytes read but not decoded yet.
	 * Read and write under lock.
	 */
	u32 cnt;
	/*
	 * Is there a submitted DMA request?
	 * Set when a DMA request is submitted to DHUB.
	 * Cleared on 'stop' or ISR.
	 */
	bool dma_pending;

	/*
	 * Indicates if page memory is allocated
	 */
	bool pages_allocated;

	/*
	 * Instance lock between ISR and higher contexts.
	 */
	spinlock_t lock;

	/* DMA buffer */
	u8 *dma_area[MAX_CHID];
	dma_addr_t dma_addr[MAX_CHID];
	u32 dma_bytes_total;
	u32 dma_bytes_ch;
	u32 dma_period_total;
	u32 dma_period_ch;
	u32 pauseSampleCounter;

	/* hw parameter */
	u32 fs;
	u32 sample_format;
	u32 pcm_ratio;
	u32 pcm_ratio_div;
	u32 channel_num;
	u32 channel_map;
	u32 chid[MAX_CHID];
	u32 chid_num;
	u32 max_ch_inuse; //define how many ch actually used
	u32 mode; // I2SI_MODE OR PDMI_MODE
	bool interleaved;
	bool dummy_data;
	bool multi_lanes;

	/* capture status */
	bool capturing;
	bool enable_mic_mute;
	bool ch_shift_check; /* for dmic multi-channel shift check */

	u8 spdif_channel_status[24];			/* 192 bits = 24 bytes for full block */
	u8 spdif_channel_status_pending[24];	/* working copy, ISR-context only */
	u8 spdif_channel_status_completed[24];  /* snapshot of last completed block */
	u32 (*spdif_get_sample_rate)(void *data);
	void *spdif_get_sample_rate_data;
	u32 spdif_bit_index;					/* Bit position in channel_status array */
	bool spdif_block_synced;				/* Track if we're synchronized to a valid B preamble */
	bool spdif_csb_ready;					/* completed CSB block with changes */

	struct snd_kcontrol *spdif_rate_ctl;
	struct snd_kcontrol *spdif_csb_ctl;

	struct snd_pcm_substream *ss;
	struct cic_decimator cic[MAX_CHANNELS];
	struct workqueue_struct *wq;
	struct delayed_work delayed_work;
	enum berlin_mic_mute_state mic_mute_state;
	struct work_struct spdif_snd_notify_work;
	u32 zero_chunk_count;
	struct input_dev *mic_mute;
	struct berlin_chip *chip;

	void (*copy)(struct snd_pcm_substream *ss);

	void *aio_handle;
};

static u32 bc_rates[] = {
	8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000, 64000,
	88200, 96000, 176400, 192000
};

static struct snd_pcm_hw_constraint_list berlin_constraints_rates = {
	.count	= ARRAY_SIZE(bc_rates),
	.list	= bc_rates,
	.mask	= 0,
};

static void berlin_capture_set_kcontrol(
				struct snd_pcm_substream *ss,
				struct snd_kcontrol **kctl,
				const char *name)
{
	struct snd_ctl_elem_id id;
	struct snd_pcm_runtime *runtime = ss->runtime;
	struct berlin_capture *bc = runtime->private_data;
	struct snd_soc_pcm_runtime *rtd = ss->private_data;

	if (!kctl || !name || !rtd || !rtd->card || !rtd->card->snd_card)
		return;

	memset(&id, 0, sizeof(id));
	id.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	strscpy(id.name, name, sizeof(id.name));

	*kctl = snd_ctl_find_id(rtd->card->snd_card, &id);
	/* Cache the private data */
	if (*kctl) {
		(*kctl)->private_data = bc;
	}
}

int berlin_capture_spdif_sample_rate_info(struct snd_kcontrol *kcontrol,
					   struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = bc_rates[ARRAY_SIZE(bc_rates) - 1];
	return 0;
}

int berlin_capture_spdif_control_status_buffer_info(struct snd_kcontrol *kcontrol,
					   struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;
	return 0;
}

int berlin_capture_spdif_sample_rate_get(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	struct berlin_capture *bc = (struct berlin_capture *)kcontrol->private_data;
	ucontrol->value.integer.value[0] = 0;

	if (!bc) {
		return 0;
	}

	if (bc->mode == SPDIFI_MODE && bc->spdif_get_sample_rate) {
		ucontrol->value.integer.value[0] = bc->spdif_get_sample_rate(bc->spdif_get_sample_rate_data);
	}

	return 0;
}

void berlin_capture_set_spdif_sample_rate_provider(
				struct snd_pcm_substream *substream,
				u32 (*get_fs_fn)(void *data), void *data)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct berlin_capture *bc = runtime->private_data;

	if (!bc)
		return;

	bc->spdif_get_sample_rate = get_fs_fn;
	bc->spdif_get_sample_rate_data = data;
}

int berlin_capture_spdif_control_status_buffer_get(struct snd_kcontrol *kcontrol,
					   struct snd_ctl_elem_value *ucontrol)
{
	struct berlin_capture *bc = (struct berlin_capture *)kcontrol->private_data;
	unsigned long flags;

	if (!bc) {
		memset(ucontrol->value.iec958.status, 0, 24);
		return 0;
	}

	spin_lock_irqsave(&bc->lock, flags);
	memcpy(ucontrol->value.iec958.status, bc->spdif_channel_status, 24);
	spin_unlock_irqrestore(&bc->lock, flags);

	return 0;
}

/* Reset all SPDIF capture tracking state */
static void spdif_capture_state_reset(struct berlin_capture *bc)
{
	bc->spdif_bit_index = 0;
	memset(bc->spdif_channel_status, 0, sizeof(bc->spdif_channel_status));
	memset(bc->spdif_channel_status_pending, 0,
		sizeof(bc->spdif_channel_status_pending));
	memset(bc->spdif_channel_status_completed, 0,
		sizeof(bc->spdif_channel_status_completed));
	bc->spdif_block_synced = false;
	bc->spdif_csb_ready = false;
}

static void berlin_spdif_snd_notify_work_fn(struct work_struct *work)
{
	struct berlin_capture *bc =
		container_of(work, struct berlin_capture, spdif_snd_notify_work);

	if (bc->spdif_rate_ctl && bc->chip && bc->chip->card)
		snd_ctl_notify(bc->chip->card,
			       SNDRV_CTL_EVENT_MASK_VALUE,
			       &bc->spdif_rate_ctl->id);

	if (bc->spdif_csb_ctl && bc->chip && bc->chip->card)
		snd_ctl_notify(bc->chip->card,
			       SNDRV_CTL_EVENT_MASK_VALUE,
			       &bc->spdif_csb_ctl->id);
}

/* must always be called under lock. */
static void start_dma_if_needed(struct snd_pcm_substream *ss)
{
	struct snd_pcm_runtime *runtime = ss->runtime;
	struct berlin_capture *bc = runtime->private_data;
	dma_addr_t dma_src;
	u32 ch, dma_size, i;
	bool intr = true;

	assert_spin_locked(&bc->lock);

	if (!bc->capturing) {
#ifdef BUF_STATE_DEBUG
		snd_printd("%s: capturing: %u\n", __func__, bc->capturing);
#endif
		return;
	}

	if (bc->dma_pending) {
#ifdef BUF_STATE_DEBUG
		snd_printd("%s: DMA pending. Skipping DMA start.\n", __func__);
#endif
		return;
	}

	if (bc->cnt > (bc->dma_bytes_ch - bc->dma_period_ch)) {
#ifdef BUF_STATE_DEBUG
		snd_printd("%s: overrun: PCM conversion too slow.\n", __func__);
#endif
		return;
	}

	for (i = 0; i < bc->chid_num; i++) {
		ch = bc->chid[i];
		dma_src = bc->dma_addr[i] + bc->current_dma_offset;
		dma_size = bc->dma_period_ch;
		bc->dma_pending = true;
		dhub_channel_write_cmd(bc->chip->dhub, ch,
					dma_src, dma_size, 0, 0, 0, intr,
					0, 0);
		intr = false;
		bc->pauseSampleCounter += (bc->dma_period_ch / (bc->channel_num *
			    DHUB_FIFO_DEPTH / 8));
	}
}

static void berlin_capture_trigger_start(struct snd_pcm_substream *ss)
{
	struct snd_pcm_runtime *runtime = ss->runtime;
	struct berlin_capture *bc = runtime->private_data;
	unsigned long flags;
	dma_addr_t dma_src;
	u32 dma_size, i, c, ch;
	bool intr = true;
	u32 skip_dma_size = 1;
	u32 queue_dma_cmd_size = 2;

	spin_lock_irqsave(&bc->lock, flags);
	if (bc->capturing) {
		spin_unlock_irqrestore(&bc->lock, flags);
		return;
	}

	bc->capturing = true;

	dma_size = bc->dma_period_ch;
	// Queue two DMA commands when trigger start to avoid FIFO overrun
	/* In each interrupt, write one DMA command, so if interrupts are
	 * handled in time, there will always be 2 DMA commands in queue.
	 * The first interrupt will be raised and stay asserted until cleared.
	 * If the first interrupt isn’t cleared before the second interrupt is
	 * raised, the interrupt line stays asserted.
	 * The overrun happens when IRQ is disabled for longer than 8 ms, such
	 * that IRQ is not served and the next DMA is not set up in time before
	 * FIFO being filled up.
	 * The calculation is based on FIFO size = 2048 bytes, 16khz sampling
	 * rate, stereo and 64 bits per sample for PDM data.
	 * PDM_MAX_DMA_BUFFER_SIZE / (DECIMATION_FACTOR / 32) /
	 * (2 channels * 4 bytes / sample) / (16000 frames / second)
	 * 4096 / (128 / 32) / 8 / 16000 = 0.008 sec
	 * Queueing 2 DMA commands will relax the limit to ~16ms
	 */
	// We intentionally let the dma_src in dhub_channel_write_cmd
	// be the same in the for-loop, because the data in the first few DMA
	// blocks may be corrupted and we just ignore them.
	for (i = 0; i < skip_dma_size; i++) {
		// skip dma cmd will not trigger interrupt
		intr = false;
		for (c = 0; c < bc->chid_num; c++) {
			ch = bc->chid[c];
			dma_src = bc->dma_addr[c] + bc->current_dma_offset;
			dhub_channel_write_cmd(bc->chip->dhub, ch,
					       dma_src, dma_size, 0, 0, 0, intr,
					       0, 0);
		}
		bc->pauseSampleCounter += (bc->dma_period_ch / (bc->channel_num *
			    DHUB_FIFO_DEPTH / 8));
	}

	for (i = 0; i < queue_dma_cmd_size; i++) {
		intr = true;
		for (c = 0; c < bc->chid_num; c++) {
			ch = bc->chid[c];
			dma_src = bc->dma_addr[c] + bc->current_dma_offset;
			dhub_channel_write_cmd(bc->chip->dhub, ch,
					dma_src, dma_size, 0, 0, 0, intr,
					0, 0);
			intr = false;
		}
		if (i < queue_dma_cmd_size - 1)
			bc->current_dma_offset += dma_size;

		bc->pauseSampleCounter += (bc->dma_period_ch / (bc->channel_num *
			    DHUB_FIFO_DEPTH / 8));
	}
	bc->dma_pending = true;
	spin_unlock_irqrestore(&bc->lock, flags);
	snd_printd("%s: dma capture chid%d start done. PSC 0x%X Period 0x%X \n",
				__func__,
				bc->chid[0],
				bc->pauseSampleCounter,
				bc->dma_period_ch);
}

u32 berlin_capture_get_pause_count(struct snd_pcm_substream *ss)
{
	struct snd_pcm_runtime *runtime = ss->runtime;
	struct berlin_capture *bc = runtime->private_data;

	return bc->pauseSampleCounter;
}

static void berlin_capture_trigger_stop(struct snd_pcm_substream *ss)
{
	struct snd_pcm_runtime *runtime = ss->runtime;
	struct berlin_capture *bc = runtime->private_data;
	unsigned long flags;

	spin_lock_irqsave(&bc->lock, flags);
	bc->capturing = false;
	bc->dma_pending = false;
	spin_unlock_irqrestore(&bc->lock, flags);
}

static const struct snd_pcm_hardware berlin_capture_hw = {
	.info			= (SNDRV_PCM_INFO_MMAP
				   | SNDRV_PCM_INFO_INTERLEAVED
				   | SNDRV_PCM_INFO_MMAP_VALID
				   | SNDRV_PCM_INFO_PAUSE
				   | SNDRV_PCM_INFO_RESUME
				   | SNDRV_PCM_INFO_SYNC_START),
	.formats		= (SNDRV_PCM_FMTBIT_S32_LE
				   | SNDRV_PCM_FMTBIT_S24_LE
				   | SNDRV_PCM_FMTBIT_S24_3LE
				   | SNDRV_PCM_FMTBIT_S16_LE
				   | SNDRV_PCM_FMTBIT_IEC958_SUBFRAME_LE),
	.rates			= (SNDRV_PCM_RATE_8000_192000
				   | SNDRV_PCM_RATE_KNOT),
	.channels_min		= MIN_CHANNELS,
	.channels_max		= MAX_CHANNELS,
	.buffer_bytes_max	= PCM_MAX_BUFFER_SIZE,
	.period_bytes_min	= PCM_DMA_MIN_SIZE,
	.period_bytes_max	= PCM_DMA_BUFFER_SIZE,
	.periods_min		= 2,
	.periods_max		= PCM_MAX_BUFFER_SIZE / PCM_DMA_MIN_SIZE,
	.fifo_size		= 0
};

/******************** decimator algorithm start ********************/
#define DECIMATE_Q	28
#define DECIMATE_MAX	((1L<<DECIMATE_Q)-1)
#define DECIMATE_MIN	(-DECIMATE_MAX - 1)
#define DECIMATE_AS_INT(val) ((s32)((double)(val) * DECIMATE_MAX))

#define DECIMATE_AS_Q31(val) \
	((val) >= DECIMATE_MAX ? 0x7fffffff : \
	((val) <= DECIMATE_MIN ? 0x80000000 : ((val) << (31-DECIMATE_Q))))

inline s32 DECIMATE_MUL(s32 s1, s32 s2)
{
	return (s32)(((int64_t)s1 * s2) >> DECIMATE_Q);
}

static inline void reflector_pair(const s32 *G, s32 *X, s32 *D)
{
	s32 t[2];

	t[0] = X[0] + D[0];
	t[1] = X[1] + D[1];

	X[0] = DECIMATE_MUL(t[0], G[0]) + D[0];
	X[1] = DECIMATE_MUL(t[1], G[1]) + D[1];

	D[0] = t[0] - X[0];
	D[1] = t[1] - X[1];
}

static inline void reflector_pair_lw5(s32 *X, s32 *D)
{
	s32 t[2];

	t[0] = X[0] + D[0];
	t[1] = X[1] + D[1];

	X[0] = (t[0] >> 1) + (t[0] >> 4) + D[0];
	X[1] = (t[1] >> 3) + D[1];

	D[0] = t[0] - X[0];
	D[1] = t[1] - X[1];
}

static const s32 gamma_lw9[] = {
	DECIMATE_AS_INT(0.171417236f),
	DECIMATE_AS_INT(0.042724609f),
	DECIMATE_AS_INT(0.746093750f),
	DECIMATE_AS_INT(0.394050598f)
};

static const s32 gamma_lw13[] = {
	DECIMATE_AS_INT(0.191894531f),
	DECIMATE_AS_INT(0.052734375f),
	DECIMATE_AS_INT(0.563964844f),
	DECIMATE_AS_INT(0.375000000f),
	DECIMATE_AS_INT(0.911132813f),
	DECIMATE_AS_INT(0.741210938f)
};

static inline s32 lw5(s32 *fifo, s32 *signal)
{
	reflector_pair_lw5(signal, fifo);
	return signal[0] + signal[1];
}

static inline s32 lw9(s32 *fifo, s32 *signal)
{
	reflector_pair(gamma_lw9+0, signal, fifo+0);
	reflector_pair(gamma_lw9+2, signal, fifo+2);
	return signal[0] + signal[1];
}

static inline s32 lw13(s32 *fifo, s32 *signal)
{
	reflector_pair(gamma_lw13+0, signal, fifo+0);
	reflector_pair(gamma_lw13+2, signal, fifo+2);
	reflector_pair(gamma_lw13+4, signal, fifo+4);

	return signal[0] + signal[1];
}

static int decimator_lw5(s32 *fifo, s32 *signal, int count)
{
	int i;

	count >>= 1;

	for (i = 0; i < count; i++)
		signal[i] = lw5(fifo, signal+(i<<1));

	return count;
}

/* lw9 for 4 point only */
static inline void decimator_lw9(s32 *fifo, s32 *signal)
{
	signal[0] = lw9(fifo, signal);
	signal[1] = lw9(fifo, signal+2);
}

static s32 cic_decimator_process(struct cic_decimator *cic, u32 *data)
{
	return cic->process(cic, data);
}

static
s32 cic_decimator_process_o4_128x(struct cic_decimator *cic, u32 *data)
{
	s32 block, i;
	s32 sig[32];
	s32 finnal[4];

	for (block = 0; block < 4; ++block) {
		uint32_t bits = data[2 * block];

		for (i = 0; i < PCM_SAMPLE_BITS; i++) {
			sig[i] = (bits & 1) ? DECIMATE_AS_INT(0.0078125) :
						DECIMATE_AS_INT(-0.0078125f);
			bits >>= 1;
		}
		decimator_lw5(cic->fifo_lw5[0], sig, 32);
		decimator_lw5(cic->fifo_lw5[1], sig, 16);
		decimator_lw5(cic->fifo_lw5[2], sig, 8);
		decimator_lw5(cic->fifo_lw5[3], sig, 4);
		decimator_lw5(cic->fifo_lw5[4], sig, 2);
		finnal[block] = sig[0];
	}
	decimator_lw9(cic->fifo_lw9, finnal);
	finnal[0] = lw13(cic->fifo_lw13, finnal);

	return DECIMATE_AS_Q31(finnal[0]);
}

static
s32 cic_decimator_process_o5_64x(struct cic_decimator *cic, u32 *data)
{
	s32 block, i;
	s32 sig[32];
	s32 finnal[2];

	for (block = 0; block < 2; ++block) {
		u32 bits = data[2 * block];

		for (i = 0; i < PCM_SAMPLE_BITS; i++) {
			sig[i] = (bits & 1) ? DECIMATE_AS_INT(0.015625f) :
						DECIMATE_AS_INT(-0.015625f);
			bits >>= 1;
		}
		decimator_lw5(cic->fifo_lw5[0], sig, 32);
		decimator_lw5(cic->fifo_lw5[1], sig, 16);
		decimator_lw5(cic->fifo_lw5[2], sig, 8);
		decimator_lw5(cic->fifo_lw5[3], sig, 4);
		finnal[block] = lw9(cic->fifo_lw9, sig);
	}
	finnal[0] = lw13(cic->fifo_lw13, finnal);

	return DECIMATE_AS_Q31(finnal[0]);
}

static s32 cic_decimator_process_general(struct cic_decimator *cic,
					 uint32_t *data)
{
	/* decimte 32 which output sample rate is 96KHz */
	s32 i;
	s32 sig[32];

	u32 bits = data[0];

	for (i = 0; i < PCM_SAMPLE_BITS; i++) {
		sig[i] = (bits & 1) ? DECIMATE_AS_INT(0.03125f) :
					DECIMATE_AS_INT(-0.03125f);
		bits >>= 1;
	}
	decimator_lw5(cic->fifo_lw5[0], sig, 32);
	decimator_lw5(cic->fifo_lw5[1], sig, 16);
	decimator_lw5(cic->fifo_lw5[2], sig, 8);
	decimator_lw9(cic->fifo_lw9, sig);
	sig[0] = lw13(cic->fifo_lw13, sig);

	return DECIMATE_AS_Q31(sig[0]);
}
/*************** decimator algorithm end **************/


static void cic_decimator_init(struct cic_decimator *cic, s32 decimation,
			       s32 order)
{
	memset(cic, 0, sizeof(*cic));
	cic->decimation = decimation;
	cic->order = order;
	/*
	 * CIC_BMAX = ceil(CIC_ORDER * log2(CIC_DECIMATION_FACTOR * M)
	 *   + BITWIDTH_IN)
	 * with M = 1 and BITWIDTH_IN = 1.
	 */
	cic->bmax = order * ilog2(decimation) + 1;
	if (cic->order == 4 && cic->decimation == 128)
		cic->process = &cic_decimator_process_o4_128x;
	else if (cic->order == 5 && cic->decimation == 64)
		cic->process = &cic_decimator_process_o5_64x;
	else
		cic->process = &cic_decimator_process_general;
}

static int cic_order_from_rate(int sample_rate)
{
	switch (sample_rate) {
	case 8000:
	case 11025:
	case 12000:
	case 16000:
	case 22050:
	case 24000:
		return 4;
	case 32000:
	case 44100:
	case 48000:
	case 64000:
	case 88200:
	case 96000:
		return 5;
	default:
		break;
	}
	return 0;
}

static int cic_decimation_factor_from_rate(int sample_rate)
{
	switch (sample_rate) {
	case 8000:
	case 11025:
	case 12000:
	case 16000:
	case 22050:
	case 24000:
		return 128;
	case 32000:
	case 44100:
	case 48000:
		return 64;
	case 64000:
	case 88200:
	case 96000:
		return 32;
	default:
		break;
	}
	return 0;
}

/*
 * This is used for checking if stream data from dmic is valid data
 */
static bool is_data_valid(u32 data)
{
	/* By data dump, invalid dmic channel pattern could be one of following:
	 * 0x0, 0x80000000, 0x7ffffe00
	 */
	return !((data == 0) || (data == 0x80000000) || (data == 0x7ffffe00));
}

static u32 update_channel_map(const u64 *src, u32 pre_map, const u32 ch_num)
{
	const u32 *dat;
	u32 new_map;
	u32 ch_map[DMIC_CH_CHECK_CNT];
	int i, j;
	u32 new_ch_num;
	const u32 *dat_dump;

	dat = (u32 *) src;
	dat_dump = (u32 *) src;

	for (i = 0; i < DMIC_CH_CHECK_CNT; i++) {
		new_map = 0;
		new_ch_num = 0;
		for (j = 0; j < MAX_CHANNELS; j++) {
			if (is_data_valid(*dat++)) {
				new_map |= (1 << j);
				new_ch_num++;
			}
		}
		ch_map[i] = new_map;
	}

	if ((ch_map[0] == ch_map[1]) && (ch_map[1] == ch_map[2]))
		new_map = ch_map[0];
	else
		new_map = pre_map;

#ifdef BUF_STATE_DEBUG
	if (pre_map != new_map) {
		snd_printd("capture new_map: 0x%08x, pre_map: 0x%08x\n", new_map, pre_map);
		snd_printd("(%08x)(%08x)(%08x)(%08x)(%08x)(%08x)(%08x)(%08x)",
				*dat_dump, *(dat_dump+1),  *(dat_dump+2), *(dat_dump+3),
				*(dat_dump+4), *(dat_dump+5), *(dat_dump+6), *(dat_dump+7));
	}
#endif
	return new_map;
}

/*
 * This is used for checking if stream data is zeros
 */
static bool is_all_zero(const void *data, size_t length)
{
	const unsigned char *p = data;
	size_t loop = 0;

	for (loop = 0; loop < 16; loop++) {
		if (!length)
			return true;
		if (*p)
			return false;
		p++;
		length--;
	}

	/* compare the remaining data with first all 0 16 bytes */
	return (memcmp(data, p, length) == 0);
}

void set_mic_mute_state(struct snd_pcm_substream *ss, int mute)
{
	struct snd_pcm_runtime *runtime = ss->runtime;
	struct berlin_capture *bc = runtime->private_data;
	struct input_dev *mic_mute = bc->mic_mute;

	if (!bc->enable_mic_mute)
		return;

	if (mute) {
		if (bc->mic_mute_state != BERLIN_MIC_MUTE_STATE_MUTE) {
			if (bc->mic_mute_state == BERLIN_MIC_MUTE_STATE_UNKNOWN) {
				/* set_bit avoid key up missing */
				set_bit(KEY_MICMUTE, mic_mute->key);
			} else {
				input_event(mic_mute, EV_KEY, KEY_MICMUTE, 1);
				input_sync(mic_mute);
			}

			bc->mic_mute_state = BERLIN_MIC_MUTE_STATE_MUTE;
			return;
		}
	} else {
		if (bc->mic_mute_state != BERLIN_MIC_MUTE_STATE_UNMUTE) {
			if (bc->mic_mute_state != BERLIN_MIC_MUTE_STATE_UNKNOWN) {
				input_event(mic_mute, EV_KEY, KEY_MICMUTE, 0);
				input_sync(mic_mute);
			}
			bc->mic_mute_state = BERLIN_MIC_MUTE_STATE_UNMUTE;
		}
	}
}

static void check_mic_mute_state(struct berlin_capture *bc,
				 uint32_t *base,
				 size_t byte_per_chunk)
{
	struct input_dev *mic_mute = bc->mic_mute;
	/* # of chunk = # of millisecond * (1second / 1000 millisecond) * \
	 * (sample/second) * (byte/sample) * (chunks/byte)
	 * = # of millisecond / 1000 * sample_rate(Hz) * (2 channels * \
	 *   4 bytes/channel-sample) / (byte/chunk)
	 * = (# of millisecond * sample_rate(Hz) * 8) / \
	 *   (1000 * byte_per_chunk)
	 */
	const size_t mute_threshold_in_chunk =
		DIV_ROUND_UP(MUTE_THRESHOLD_MS * bc->fs * 8,
			     1000 * byte_per_chunk);
	bool is_all_zeros;

	if (!bc->enable_mic_mute)
		return;

	is_all_zeros = is_all_zero(base, byte_per_chunk);
	if (is_all_zeros && (bc->mic_mute_state != BERLIN_MIC_MUTE_STATE_MUTE)) {
		if (++bc->zero_chunk_count > mute_threshold_in_chunk) {
			if (bc->mic_mute_state == BERLIN_MIC_MUTE_STATE_UNKNOWN) {
				/* set_bit avoid key up missing */
				set_bit(KEY_MICMUTE, mic_mute->key);
			} else {
				input_event(mic_mute, EV_KEY, KEY_MICMUTE, 1);
				input_sync(mic_mute);
			}

			bc->mic_mute_state = BERLIN_MIC_MUTE_STATE_MUTE;
		}
		return;
	}
	if (!is_all_zeros) {
		bc->zero_chunk_count = 0;
		if (bc->mic_mute_state != BERLIN_MIC_MUTE_STATE_UNMUTE) {
			if (bc->mic_mute_state != BERLIN_MIC_MUTE_STATE_UNKNOWN) {
				input_event(mic_mute, EV_KEY, KEY_MICMUTE, 0);
				input_sync(mic_mute);
			}
			bc->mic_mute_state = BERLIN_MIC_MUTE_STATE_UNMUTE;
		}
	}
}

static void decode_pdm_to_pcm(struct work_struct *work)
{
	struct berlin_capture *bc =
		container_of(work, struct berlin_capture, delayed_work.work);
	struct snd_pcm_substream *ss = bc->ss;
	struct snd_pcm_runtime *runtime = ss->runtime;
	u32 *src = (u32 *)(bc->dma_area[0] + bc->read_offset);
	s32 *dst = (s32 *)(runtime->dma_area + bc->runtime_offset);
	unsigned long flags;
	const size_t period_size_bytes =
		frames_to_bytes(runtime, runtime->period_size);
	const size_t pcm_buffer_size_bytes =
		frames_to_bytes(runtime, runtime->buffer_size);
	const size_t pdm_pcm_ratio =
		bc->cic[0].decimation / PCM_SAMPLE_BITS;
	const size_t pdms_per_dma_ch =
		bytes_to_samples(runtime, bc->dma_period_ch);
	u32 i = 0, j = 0, ch = 0, chid = 0;

	while (bc->capturing) {
		spin_lock_irqsave(&bc->lock, flags);
		if (bc->cnt < bc->dma_period_total) {
			//restart the DMA if it is broken.
			if (!bc->dma_pending)
				start_dma_if_needed(ss);
			spin_unlock_irqrestore(&bc->lock, flags);
			return;
		}
		spin_unlock_irqrestore(&bc->lock, flags);
		/* check pdm_data to infer mic mute */
		check_mic_mute_state(bc, src, bc->dma_period_ch);

		if (bc->mic_mute_state == BERLIN_MIC_MUTE_STATE_UNMUTE) {
			for (i = 0; i < pdms_per_dma_ch; i += 2 * pdm_pcm_ratio) {
				for (chid = 0; chid < bc->chid_num; chid++) {
					src = (u32 *)(bc->dma_area[chid] + bc->read_offset);
					for (j = 0; j < 2; j++) {
						u32 *pdm_data = src + i + j;
						s32 pcm_data;

						ch = 2 * chid + j;
						// Skip unused ch process as requested
						if (ch < bc->max_ch_inuse)
							pcm_data = cic_decimator_process(&bc->cic[ch], pdm_data);
						else
							pcm_data = 0;
						*dst++ = pcm_data;
					}
				}
			}
		} else
			/* output zero pcm on mute */
			memset(dst, 0, period_size_bytes);

		bc->read_offset += bc->dma_period_ch;
		bc->read_offset %= bc->dma_bytes_ch;
		spin_lock_irqsave(&bc->lock, flags);
		/* compress two $DECIMATION_FACTOR 1-bit PDM samples to two
		 * 32-bit PCM samples.
		 */
		bc->runtime_offset += period_size_bytes;
		bc->runtime_offset %= pcm_buffer_size_bytes;
		bc->cnt -= bc->dma_period_total;
		spin_unlock_irqrestore(&bc->lock, flags);

		snd_pcm_period_elapsed(ss);
		src = (u32 *)(bc->dma_area[0] + bc->read_offset);
		dst = (s32 *)(runtime->dma_area + bc->runtime_offset);
	}
}

static void pdm_copy(struct snd_pcm_substream *ss)
{
	struct snd_pcm_runtime *runtime = ss->runtime;
	struct berlin_capture *bc = runtime->private_data;

	queue_delayed_work(bc->wq, &bc->delayed_work, 0);
}

static void hdmi_copy(struct snd_pcm_substream *ss)
{
	struct snd_pcm_runtime *runtime = ss->runtime;
	struct berlin_capture *bc = runtime->private_data;
	size_t period_total = bc->dma_period_total;
	const size_t pcm_buffer_size_bytes =
		frames_to_bytes(runtime, runtime->buffer_size);
	unsigned long flags;
	int frames = bc->dma_period_ch /
			(bc->channel_num * DHUB_FIFO_DEPTH / 8);

	u32 *src = (u32 *)(bc->dma_area[0] + bc->read_offset);
	u32 *dst = (u32 *)(runtime->dma_area + bc->runtime_offset);

	period_total = frames_to_bytes(runtime, frames);

	check_mic_mute_state(bc, (u32 *)src, bc->dma_period_ch/bc->pcm_ratio);

	if (bc->mic_mute_state == BERLIN_MIC_MUTE_STATE_UNMUTE)
		memcpy((void *)dst, (void *)src, bc->dma_period_ch/bc->pcm_ratio);
	else
		memset((void *)dst, 0, bc->dma_period_ch/bc->pcm_ratio);

	spin_lock_irqsave(&bc->lock, flags);
	bc->read_offset += bc->dma_period_ch/bc->pcm_ratio;
	bc->read_offset %= bc->dma_bytes_ch;
	bc->runtime_offset += period_total;
	bc->runtime_offset %= pcm_buffer_size_bytes;
	bc->cnt -= period_total;
	spin_unlock_irqrestore(&bc->lock, flags);

	snd_pcm_period_elapsed(ss);
}


static void copy_pcm(struct snd_pcm_substream *ss)
{
	struct snd_pcm_runtime *runtime = ss->runtime;
	struct berlin_capture *bc = runtime->private_data;
	size_t period_total = bc->dma_period_total;
	const size_t pcm_buffer_size_bytes =
		frames_to_bytes(runtime, runtime->buffer_size);
	size_t samples_pcm_dma_ch =
		bytes_to_samples(runtime, bc->dma_period_ch) >> 1;
	u64 *src = (u64 *)(bc->dma_area[0] + bc->read_offset);
	s64 *dst = (s64 *)(runtime->dma_area + bc->runtime_offset);
	unsigned long flags;
	u32 i, j, chid;
	const int32_t *pcm_src = (int32_t *)src;
	int frames = bc->dma_period_ch /
			   (bc->channel_num * DHUB_FIFO_DEPTH / 8);
	static const uint8_t channel_map[8] = {0, 1, 4, 5, 2, 3, 6, 7};

	/* copy dhub to DMA, re-calculate data */
	period_total = frames_to_bytes(runtime, frames);

	/* check pcm data to infer mic mute */
	check_mic_mute_state(bc, (u32 *)src, bc->dma_period_ch);
	if (bc->chid_num == 1) {
		/* copy data */
		if (bc->interleaved && bc->dummy_data) {
			samples_pcm_dma_ch = samples_pcm_dma_ch / MAX_CHID;
			for (i = 0; i < samples_pcm_dma_ch; i++) {
				src = (u64 *)(bc->dma_area[0] + bc->read_offset +
						i * PDM_DHUB_OCPF_CHUNK_SZ);
				memcpy((void *)dst, (void *)src,
					PDM_DHUB_OCPF_CHUNK_SZ *
					((bc->channel_num + 1) / 2) / MAX_CHID);
				dst = (s64 *)((u8 *)dst +
						PDM_DHUB_OCPF_CHUNK_SZ *
						((bc->channel_num + 1) / 2) / MAX_CHID);
			}
		} else {
			if (bc->sample_format == SNDRV_PCM_FORMAT_S16_LE) {
				int16_t *pcm_dst = (int16_t *)dst;

				if (!bc->multi_lanes) {
					for (i = 0; i < frames; i++) {
						for (j = 0; j < bc->channel_num; j++)
							*pcm_dst++ =
								(pcm_src[i * bc->channel_num + j] >> 16) &
								0xffff;
					}
				} else {
					for (i = 0; i < frames; i++) {
						for (j = 0; j < bc->channel_num; j++)
							*pcm_dst++ =
								(pcm_src[i * bc->channel_num + channel_map[j]] >> 16) &
								0xffff;
					}
				}
			} else if (bc->sample_format == SNDRV_PCM_FORMAT_S24_3LE) {
				char *pcm_dst = (char *)dst;

				if (!bc->multi_lanes) {
					for (i = 0; i < frames ; i++) {
						for (j = 0; j < bc->channel_num; j++) {
							*pcm_dst++ =
								(pcm_src[i * bc->channel_num + j] >> 8) &
								0xff;
							*pcm_dst++ =
								(pcm_src[i * bc->channel_num + j] >> 16) &
								0xff;
							*pcm_dst++ =
								(pcm_src[i * bc->channel_num + j] >> 24) &
								0xff;
						}
					}
				} else {
					for (i = 0; i < frames ; i++) {
						for (j = 0; j < bc->channel_num; j++) {
							*pcm_dst++ =
								(pcm_src[i * bc->channel_num + channel_map[j]] >> 8) &
								0xff;
							*pcm_dst++ =
								(pcm_src[i * bc->channel_num + channel_map[j]] >> 16) &
								0xff;
							*pcm_dst++ =
								(pcm_src[i * bc->channel_num + channel_map[j]] >> 24) &
								0xff;
						}
					}
				}
			} else {
				if (!bc->multi_lanes) {
					memcpy((void *)dst, (void *)src, bc->dma_period_ch);
				} else {
					int32_t *pcm_dst = (int32_t *)dst;

					for (i = 0; i < frames ; i++) {
						for (j = 0; j < bc->channel_num; j++)
							*pcm_dst++ = pcm_src[i * bc->channel_num + channel_map[j]];
					}
				}
			}
		}
	} else {
		for (i = 0; i < samples_pcm_dma_ch; i++) {
			for (chid = 0; chid < bc->chid_num; chid++) {
				src = (u64 *)(bc->dma_area[chid]
					      + bc->read_offset) + i;
				*dst++ = *src;
			}
		}
	}

	spin_lock_irqsave(&bc->lock, flags);
	bc->read_offset += bc->dma_period_ch;
	bc->read_offset %= bc->dma_bytes_ch;
	if (bc->interleaved && bc->dummy_data)
		bc->runtime_offset +=
			period_total * ((bc->channel_num + 1) / 2) / MAX_CHID;
	else
		bc->runtime_offset += period_total;
	bc->runtime_offset %= pcm_buffer_size_bytes;
	bc->cnt -= period_total;
	spin_unlock_irqrestore(&bc->lock, flags);

	snd_pcm_period_elapsed(ss);
}

static void dmic_copy(struct snd_pcm_substream *ss)
{
	struct snd_pcm_runtime *runtime = ss->runtime;
	struct berlin_capture *bc = runtime->private_data;
	size_t period_total = bc->dma_period_total;
	const size_t pcm_buffer_size_bytes =
		frames_to_bytes(runtime, runtime->buffer_size);
	/*size of the dmic dhub sample is 32 bits for all formats*/
	size_t samples_pcm_dma_ch = ((bc->dma_period_ch * 8) / DHUB_FIFO_DEPTH) >> 1;
	u64 *src = (u64 *)(bc->dma_area[0] + bc->read_offset);
	s64 *dst = (s64 *)(runtime->dma_area + bc->runtime_offset);
	unsigned long flags;
	u32 i, j, offset, ch_num;

	/* check pcm data to infer mic mute */
	check_mic_mute_state(bc, (u32 *)src, bc->dma_period_ch);
	/* copy data */
	samples_pcm_dma_ch = samples_pcm_dma_ch / MAX_CHID;
	/* If ch_shift_check is enabled, do channel map update at run-time */
	if (bc->ch_shift_check)
		bc->channel_map = update_channel_map(src, bc->channel_map, bc->channel_num);

	for (i = 0; i < samples_pcm_dma_ch; i++) {
		ch_num = 0;
		for (j = 0; j < MAX_CHANNELS; j++) {
			offset = i * PDM_DHUB_OCPF_CHUNK_SZ + j * 4;
			src = (u64 *)
				(bc->dma_area[0] + bc->read_offset + offset);
			if (bc->channel_map & (1 << j)) {
				if (bc->sample_format == SNDRV_PCM_FORMAT_S24_3LE) {
					char *pcm_dst = (char *)dst;
					s32 tempdst = 0;

					memcpy((void *)&tempdst, (void *)src, 4);
					*pcm_dst++ = (char)(tempdst >> 8);
					*pcm_dst++ = (char)(tempdst >> 16);
					*pcm_dst++ = (char)(tempdst >> 24);
					dst = (s64 *)((u8 *)dst + 3);
				} else {
					memcpy((void *)dst, (void *)src, 4);
					dst = (s64 *)((u8 *)dst + 4);
				}
				ch_num++;
			}
			if (ch_num == bc->channel_num)
				break;
		}
	}
	spin_lock_irqsave(&bc->lock, flags);
	bc->read_offset += bc->dma_period_ch;
	bc->read_offset %= bc->dma_bytes_ch;
	if (bc->interleaved && bc->dummy_data)
		bc->runtime_offset +=
			period_total * ((bc->channel_num + 1) / 2)
			/ MAX_CHID;
	else
		bc->runtime_offset += period_total;
	bc->runtime_offset %= pcm_buffer_size_bytes;
	bc->cnt -= period_total;
	spin_unlock_irqrestore(&bc->lock, flags);

	snd_pcm_period_elapsed(ss);
}

static __always_inline void process_spdif_channel_status(struct berlin_capture *bc, u32 sample)
{
	u8 preamble;
	u8 c_bit;
	int byte_idx, bit_idx;

	/* Extract preamble type and C-bit from the sample */
	preamble = (sample >> 28) & 0xF;
	c_bit = (sample >> 26) & 0x1;

	/* Handle preamble B (start of block) */
	if (unlikely(preamble == TYPE_RX_B)) {
		/* Reset block on preamble B */
		bc->spdif_bit_index = 0;
		memset(bc->spdif_channel_status_pending, 0,
			sizeof(bc->spdif_channel_status_pending));
		bc->spdif_block_synced = true;

		/* Store first C-bit */
		bc->spdif_channel_status_pending[0] |= (c_bit << 0);
		bc->spdif_bit_index++;

	} else if (preamble == TYPE_RX_M) {
		/* Only process M preambles if we're synchronized (have seen B) */
		if (!bc->spdif_block_synced) {
			/* Ignore M preambles until we see a B preamble */
			return;
		}

		/* Verify we haven't overrun the block */
		if (bc->spdif_bit_index >= SPDIF_BLOCK_SIZE) {
			bc->spdif_block_synced = false;
			return;
		}

		/* Store C-bit in channel_status array */
		byte_idx = bc->spdif_bit_index >> 3;
		bit_idx = bc->spdif_bit_index & 7;
		bc->spdif_channel_status_pending[byte_idx] |= (c_bit << bit_idx);
		bc->spdif_bit_index++;

		/* Check if we've completed a full 192-bit block */
		if (bc->spdif_bit_index == SPDIF_BLOCK_SIZE) {
			/* Snapshot completed block immediately so it isn't
			* overwritten if another B preamble arrives later in
			* the same period before spdif_copy() publishes. */
			memcpy(bc->spdif_channel_status_completed,
			bc->spdif_channel_status_pending,
			sizeof(bc->spdif_channel_status_completed));
			bc->spdif_csb_ready = true;

			/* Reset for next block - will wait for next B preamble */
			bc->spdif_block_synced = false;
		}
	}
	/* Ignore TYPE_RX_W (subframe 2) and any other preambles for channel status extraction */
}

static void spdif_copy(struct snd_pcm_substream *ss)
{
	struct snd_pcm_runtime *runtime = ss->runtime;
	struct berlin_capture *bc = runtime->private_data;
	size_t period_total = bc->dma_period_total;
	const size_t pcm_buffer_size_bytes =
		frames_to_bytes(runtime, runtime->buffer_size);
	u32 *src = (u32 *)(bc->dma_area[0] + bc->read_offset);
	u32 *dst = (u32 *)(runtime->dma_area + bc->runtime_offset);
	const int32_t *spdif_src = (int32_t *)src;
	int16_t *spdif_dst_16 = (int16_t *)dst;
	int32_t *spdif_dst = (int32_t *)dst;
	unsigned long flags;
	int i, j;
	int frames = bc->dma_period_ch /
				(bc->channel_num * DHUB_FIFO_DEPTH / 8);

	period_total = frames_to_bytes(runtime, frames);

	switch (bc->sample_format) {
	case SNDRV_PCM_FORMAT_S16_LE:
		for (i = 0; i < frames; i++) {
			for (j = 0; j < bc->channel_num; j++) {
				u32 sample = spdif_src[i * bc->channel_num + j];

				/* Process S/PDIF frame and extract channel status */
				if (j == 0)
					process_spdif_channel_status(bc, sample);

				/* Chopped SPDIF header and copy only 16 bit audio payload */
				*spdif_dst_16++ = (sample >> 8) & 0xffff;
			}
		}
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
	case SNDRV_PCM_FORMAT_S32_LE:
		for (i = 0; i < frames; i++) {
			for (j = 0; j < bc->channel_num; j++) {
				u32 sample = spdif_src[i * bc->channel_num + j];

				/* Process S/PDIF frame and extract channel status */
				if (j == 0)
					process_spdif_channel_status(bc, sample);

				/* Chopped SPDIF header and copy only 24 bit audio payload */
				*spdif_dst++ = (sample << 8) & 0xffffff00;
			}
		}
		break;
	default:
		/* IEC958 raw data pass-through, copy all 32 bits which includes
		 * audio payload and header.
		 *
		 * TBD : to add Channel Status Buffer (CSB) processing logic in pass through mode.
		 */
		memcpy((void *)dst, (void *)src, bc->dma_period_ch);
		break;
	}

	spin_lock_irqsave(&bc->lock, flags);

	/* schedule defered work to publish csb and notify only when csb changed*/
	if (bc->spdif_csb_ready) {
		bool changed = memcmp(bc->spdif_channel_status_completed,
				      bc->spdif_channel_status,
				      sizeof(bc->spdif_channel_status)) != 0;
		memcpy(bc->spdif_channel_status, bc->spdif_channel_status_completed,
		       sizeof(bc->spdif_channel_status));
		bc->spdif_csb_ready = false;
		if (changed)
			schedule_work(&bc->spdif_snd_notify_work);
	}

	bc->read_offset += bc->dma_period_ch;
	bc->read_offset %= bc->dma_bytes_ch;
	bc->runtime_offset += period_total;
	bc->runtime_offset %= pcm_buffer_size_bytes;
	bc->cnt -= period_total;
	spin_unlock_irqrestore(&bc->lock, flags);
	snd_pcm_period_elapsed(ss);
}

/* Must call before hw_param! */
void berlin_capture_set_ch_mode(struct snd_pcm_substream *ss,
				u32 chid_num, u32 *chid, u32 mode,
				bool enable_mic_mute, bool interleaved_mode,
				bool dummy_data, bool multi_lanes, u32 channel_map,
				bool ch_shift_check, void * aio_handle)
{
	struct snd_pcm_runtime *runtime = ss->runtime;
	struct berlin_capture *bc = runtime->private_data;

	if (bc) {
		u32 i;

		for (i = 0; i < chid_num; i++)
			bc->chid[i] = chid ? chid[i] : 0;
		bc->chid_num = chid_num;
		bc->mode = mode;
		bc->interleaved = interleaved_mode;
		bc->dummy_data = dummy_data;
		bc->multi_lanes = multi_lanes;
		bc->enable_mic_mute = enable_mic_mute;
		bc->channel_map = channel_map;
		bc->ch_shift_check = ch_shift_check;
		bc->aio_handle = aio_handle;
		snd_printd(
			"capture chnum %d mode %d interleaved %d dummy_data %d\n",
			chid_num, mode, interleaved_mode, dummy_data);
	}
}

void berlin_capture_set_ch_inuse(struct snd_pcm_substream *ss,
				 u32 ch_num)
{
	struct snd_pcm_runtime *runtime = ss->runtime;
	struct berlin_capture *bc = runtime->private_data;

	bc->max_ch_inuse = ch_num;
}

int berlin_capture_hw_free(struct snd_pcm_substream *ss)
{
	struct snd_pcm_runtime *runtime = ss->runtime;
	struct berlin_capture *bc = runtime->private_data;
	struct berlin_chip *chip = bc->chip;
	u32 i;

	if (bc->mode != HDMII_MODE) {
		for (i = 0; i < bc->chid_num; i++) {
		/* clear the DMA queue Ddub*/
			DhubChannelClear(bc->chip->dhub, bc->chid[i], 0);
		}
	}

	if (chip && bc->dma_area[0]) {
		struct platform_device *platform_dev = chip->pdev;

		if (bc->mode == PDMI_MODE)
			flush_delayed_work(&bc->delayed_work);
		dma_free_coherent(&platform_dev->dev, bc->dma_bytes_total * bc->pcm_ratio / bc->pcm_ratio_div,
				  bc->dma_area[0], bc->dma_addr[0]);
		for (i = 0; i < MAX_CHID; i++) {
			bc->dma_area[i] = NULL;
			bc->dma_addr[i] = 0;
		}
	}

	if (bc->pages_allocated == true) {
		snd_pcm_lib_free_pages(ss);
		bc->pages_allocated = false;
	}
	return 0;
}

int berlin_capture_hw_params(struct snd_pcm_substream *ss,
			     struct snd_pcm_hw_params *params)
{
	struct snd_pcm_runtime *runtime = ss->runtime;
	struct berlin_capture *bc = runtime->private_data;
	struct snd_soc_pcm_runtime *rtd = ss->private_data;
	struct snd_soc_component *component = snd_soc_rtdcom_lookup(rtd, "syna-berlin-pcm");
	const size_t pcm_period_size_bytes = params_period_bytes(params);
	const size_t pcm_buffer_size_bytes = pcm_period_size_bytes * params_periods(params);
	struct device *dev;
	struct berlin_chip *chip;
	struct platform_device *platform_dev;
	int err;
	u32 ch_pair_num, ch, i;
	unsigned long long mic_sleep_us;

	if (!component)
		return -EINVAL;

	dev = component->dev;
	chip = dev_get_drvdata(dev);
	platform_dev = chip->pdev;

	bc->sample_format = params_format(params);
	bc->pcm_ratio = 1;
	bc->pcm_ratio_div = 1;
	if (bc->sample_format == SNDRV_PCM_FORMAT_S16_LE)
		bc->pcm_ratio *= 2;
	else if (bc->sample_format == SNDRV_PCM_FORMAT_S24_3LE) {
		bc->pcm_ratio *= 4;
		bc->pcm_ratio_div = 3;
	}

	if (bc->mode & FLAG_EARC_MODE) {
		ss->wait_time = 500;
		bc->mode = bc->mode & ~FLAG_EARC_MODE;
	}

	if (bc->mode == PDMI_MODE) {
		INIT_DELAYED_WORK(&bc->delayed_work, decode_pdm_to_pcm);
		bc->copy = &pdm_copy;
	} else if (bc->mode == I2SI_MODE) {
		bc->copy = &copy_pcm;
	} else if (bc->mode == DMICI_MODE) {
		bc->copy = &dmic_copy;
	} else if (bc->mode == SPDIFI_MODE) {
		bc->copy = &spdif_copy;
	} else if (bc->mode == HDMII_MODE) {
		bc->copy = &hdmi_copy;
	} else {
		snd_printk("non valid mode %d!", bc->mode);
		return -EINVAL;
	}

	bc->fs = params_rate(params);
	bc->channel_num = params_channels(params);
	snd_printd(
		"fs:%d ch:%d, width:%d fmt:%s, buf size:%lu period size:%lu pcm ratio %d div %d \n",
		bc->fs, bc->channel_num,
		params_width(params),
		snd_pcm_format_name(bc->sample_format),
		pcm_buffer_size_bytes, pcm_period_size_bytes,
		bc->pcm_ratio, bc->pcm_ratio_div);

	if ((bc->mode == I2SI_MODE) || (bc->mode == HDMII_MODE)) {
		ch_pair_num = bc->chid_num;
	} else {
		//in case of odd channel number
		ch_pair_num = (bc->channel_num + 1) / 2;
		if (!bc->interleaved) {
			if (ch_pair_num !=  bc->chid_num) {
				snd_printk("chid mis-match with dai [%d %d]\n",
					   ch_pair_num, bc->chid_num);
				return -EINVAL;
			}
		}
	}

	bc->chip = chip;
	if (rtd->card && rtd->card->snd_card)
		bc->chip->card = rtd->card->snd_card;

	berlin_capture_hw_free(ss);
	err = snd_pcm_lib_malloc_pages(ss, pcm_buffer_size_bytes);
	if (err < 0) {
		snd_printk("fail to alloc pages for buffers %d\n", err);
		return err;
	}
	bc->pages_allocated = true;

	if (bc->mode == PDMI_MODE) {
		for (ch = 0; ch < 2 * ch_pair_num; ch++) {
			s32 cic_order = cic_order_from_rate(bc->fs);
			s32 decimation =
				cic_decimation_factor_from_rate(bc->fs);
			if (cic_order == 0 || decimation == 0) {
				snd_printk("%s: Can't determine CIC from fs %d\n",
					   __func__, bc->fs);
				snd_pcm_lib_free_pages(ss);
				return -EINVAL;
			}
			cic_decimator_init(&bc->cic[ch], decimation, cic_order);
		}
	}

	if (bc->mode == PDMI_MODE) {
		bc->dma_bytes_total = pcm_buffer_size_bytes *
				(bc->cic[0].decimation / PCM_SAMPLE_BITS);
		bc->dma_period_total = pcm_period_size_bytes *
				(bc->cic[0].decimation / PCM_SAMPLE_BITS);
	} else if (bc->mode == I2SI_MODE ||
		bc->mode == SPDIFI_MODE ||
		bc->mode == HDMII_MODE) {
		bc->dma_bytes_total = pcm_buffer_size_bytes;
		bc->dma_period_total = pcm_period_size_bytes;
		if (bc->mode == SPDIFI_MODE) {
			switch (bc->sample_format) {
			case SNDRV_PCM_FORMAT_S24_LE:
			case SNDRV_PCM_FORMAT_S32_LE:
				bc->pcm_ratio = 1;
				bc->pcm_ratio_div = 1;
				break;
			case SNDRV_PCM_FORMAT_S16_LE:
				bc->pcm_ratio = 1;
				bc->pcm_ratio_div = 2;
				break;
			default:
				bc->pcm_ratio = 1;
				bc->pcm_ratio_div = 1;
				break;
			}
		}
	} else if (bc->mode == DMICI_MODE) {
		bc->dma_bytes_total = pcm_buffer_size_bytes;
		bc->dma_period_total = pcm_period_size_bytes;
		/* Because of the format structure, for SNDRV_PCM_FORMAT_S24_LE pcm ratios should be 1*/
		if (bc->sample_format == SNDRV_PCM_FORMAT_S24_LE) {
			bc->pcm_ratio = 1;
			bc->pcm_ratio_div = 1;
		}
	}

	if (bc->interleaved && bc->dummy_data) {
		bc->dma_period_total = bc->dma_period_total * MAX_CHID;
		bc->dma_bytes_total = bc->dma_bytes_total * MAX_CHID;
	}

	bc->dma_area[0] = dma_alloc_coherent(&platform_dev->dev,
					      bc->dma_bytes_total * bc->pcm_ratio / bc->pcm_ratio_div,
					      &bc->dma_addr[0],
					      GFP_KERNEL | __GFP_ZERO);
	if (!bc->dma_area[0]) {
		snd_printk("%s: failed to allocate DMA area\n", __func__);
		goto err_dma;
	}

	bc->dma_period_ch = (bc->dma_period_total * bc->pcm_ratio / bc->pcm_ratio_div) / bc->chid_num;
	bc->dma_bytes_ch = (bc->dma_bytes_total * bc->pcm_ratio / bc->pcm_ratio_div) / bc->chid_num;
	for (i = 0; i < bc->chid_num; i++) {
		bc->dma_area[i] = bc->dma_area[0] + i * bc->dma_bytes_ch;
		bc->dma_addr[i] = bc->dma_addr[0] + i * bc->dma_bytes_ch;
	}

	/* Microphone takes 32768 cycles to wake up. Sleep for
	 * ceil(32768 cycles * (1/samplerate seconds per cycle) * 10^6) us
	 * This is not in trigger_start() because sleep should not happen
	 * in the atomic trigger callback.
	 * http://www.alsa-project.org/~tiwai/writing-an-alsa-driver/\
	 * ch05s06.html#pcm-interface-operators-trigger-callback
	 */
	mic_sleep_us = DIV_ROUND_UP_ULL(32768ull * 1000 * 3000,
					bc->fs * bc->cic[0].decimation);
	usleep_range(mic_sleep_us, mic_sleep_us + 100);
	return 0;

err_dma:
	snd_pcm_lib_free_pages(ss);
	return -ENOMEM;
}

int berlin_capture_prepare(struct snd_pcm_substream *ss)
{
	struct snd_pcm_runtime *runtime = ss->runtime;
	struct berlin_capture *bc = runtime->private_data;
	unsigned long flags;

	spin_lock_irqsave(&bc->lock, flags);
	bc->current_dma_offset = 0;
	bc->read_offset = 0;
	bc->cnt = 0;
	bc->runtime_offset = 0;

	spdif_capture_state_reset(bc);

	spin_unlock_irqrestore(&bc->lock, flags);
	return 0;
}

int berlin_capture_trigger(struct snd_pcm_substream *ss, int cmd)
{
	int ret = 0;
	struct snd_pcm_runtime *runtime = ss->runtime;
	struct berlin_capture *bc = runtime->private_data;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if (bc->mode != HDMII_MODE)
			berlin_capture_trigger_start(ss);
		else
			bc->capturing = true;
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		berlin_capture_trigger_stop(ss);
		break;
	default:
		ret = -EINVAL;
	}
	return ret;
}

snd_pcm_uframes_t
berlin_capture_pointer(struct snd_pcm_substream *ss)
{
	struct snd_pcm_runtime *runtime = ss->runtime;
	struct berlin_capture *bc = runtime->private_data;
	u32 buf_pos;
	unsigned long flags;

	spin_lock_irqsave(&bc->lock, flags);
	buf_pos = bc->runtime_offset;
	spin_unlock_irqrestore(&bc->lock, flags);

	return bytes_to_frames(runtime, buf_pos);
}

int berlin_indai_aip_alloc(struct snd_pcm_substream *ss, void **pFrame, int *size)
{
	int ret = 0;
	struct snd_pcm_runtime *runtime = ss->runtime;
	struct berlin_capture *bc = runtime->private_data;
	u32 dma_size;
	u32 *dma_src;


	if (!bc->capturing) {
		snd_printd("%s: capturing: %u\n", __func__, bc->capturing);
		*pFrame = NULL;
		*size = 0;
		return ret;
	}

	if (bc->dma_pending) {
		snd_printd("%s: DMA pending. Skipping DMA start.\n", __func__);
		*pFrame = NULL;
		*size = 0;
		ret = -1;
		return ret;
	}

	if (bc->cnt > (bc->dma_bytes_ch - bc->dma_period_ch)) {
		snd_printd("%s: overrun: PCM conversion too slow.\n", __func__);
		*pFrame = NULL;
		*size = 0;
		ret = -1;
		return ret;
	}

	dma_size = bc->dma_period_ch/bc->pcm_ratio;

	dma_src = (u32 *)(bc->dma_area[0] + bc->read_offset);
	bc->dma_pending = true;

	*pFrame = (u32 *)dma_src;
	*size = dma_size;

	return ret;
}

int berlin_aip_event_callback_newframe(struct snd_pcm_substream *ss)
{
	berlin_capture_isr(ss);

	return 0;
}

int berlin_capture_isr(struct snd_pcm_substream *ss)
{
	struct snd_pcm_runtime *runtime = ss->runtime;
	struct berlin_capture *bc = runtime->private_data;
	int frames = bc->dma_period_ch /
			(bc->channel_num * DHUB_FIFO_DEPTH / 8);
	size_t period_total = frames_to_bytes(runtime, frames);

	spin_lock(&bc->lock);
	/* If we are not running, do not chain, and clear pending */
	if (!bc->capturing) {
		bc->dma_pending = false;
		spin_unlock(&bc->lock);
		return 0;
	}

	/* If we were not pending, avoid pointer manipulation */
	if (!bc->dma_pending) {
		spin_unlock(&bc->lock);
		return 0;
	}

	/* Roll the DMA pointer, and chain if needed */
	bc->current_dma_offset += bc->dma_period_ch;
	bc->current_dma_offset %= bc->dma_bytes_ch;
	bc->dma_pending = false;
	bc->cnt += period_total;
	if (bc->mode != HDMII_MODE)
		start_dma_if_needed(ss);
	spin_unlock(&bc->lock);
	bc->copy(ss);

	return 0;
}

int berlin_capture_open(struct snd_pcm_substream *ss)
{
	struct snd_pcm_runtime *runtime = ss->runtime;
	struct berlin_capture *bc;

	int err = snd_pcm_hw_constraint_list(runtime, 0,
					     SNDRV_PCM_HW_PARAM_RATE,
					     &berlin_constraints_rates);
	int i = 0;

	if (err < 0) {
		snd_printk("%s: capture hw_constraint_list fail %d\n",
			   __func__, err);
		return err;
	}

	bc = kzalloc(sizeof(struct berlin_capture), GFP_KERNEL);
	if (bc == NULL)
		return -ENOMEM;

	runtime->private_data = bc;
	runtime->hw = berlin_capture_hw;

	/* buffer and period size are multiple of minimum depth size */
	err = snd_pcm_hw_constraint_step(ss->runtime, 0,
					 SNDRV_PCM_HW_PARAM_BUFFER_BYTES,
					 1024);
	if (err)
		return -EINVAL;

	err = snd_pcm_hw_constraint_step(ss->runtime, 0,
					 SNDRV_PCM_HW_PARAM_PERIOD_BYTES,
					 1024);
	if (err)
		return -EINVAL;

	err = snd_pcm_hw_constraint_integer(ss->runtime,
					    SNDRV_PCM_HW_PARAM_PERIODS);
	if (err < 0)
		return -EINVAL;

	bc->aio_handle = NULL;
	bc->spdif_get_sample_rate = NULL;
	bc->spdif_get_sample_rate_data = NULL;
	bc->dma_pending = false;
	bc->capturing = false;
	bc->pages_allocated = false;
	ss->wait_time = 500;
	bc->ss = ss;
	for (i = 0; i < MAX_CHID; i++) {
		bc->dma_area[i] = NULL;
		bc->dma_addr[i] = 0;
	}

	spdif_capture_state_reset(bc);
	INIT_WORK(&bc->spdif_snd_notify_work, berlin_spdif_snd_notify_work_fn);

	berlin_capture_set_kcontrol(ss, &bc->spdif_rate_ctl,
					SNDRV_CTL_NAME_IEC958_CAPTURE_SAMPLE_RATE);

	berlin_capture_set_kcontrol(ss, &bc->spdif_csb_ctl,
			SNDRV_CTL_NAME_IEC958_CAPTURE_DEFAULT);

	bc->wq = alloc_workqueue("berlin_capture", WQ_HIGHPRI | WQ_UNBOUND, 1);
	if (bc->wq == NULL) {
		err = -ENOMEM;
		goto __free_kmem;
	}

	bc->mic_mute = input_allocate_device();
	if (bc->mic_mute == NULL) {
		snd_printk("%s: alloc input device fail\n", __func__);
		err = -ENOMEM;
		goto __destroy_wq;
	}
	bc->mic_mute->name = "mic_state";
	bc->mic_mute->evbit[BIT_WORD(EV_KEY)] = BIT_MASK(EV_KEY);
	bc->mic_mute->keybit[BIT_WORD(KEY_MICMUTE)] =
						BIT_MASK(KEY_MICMUTE);
	bc->mic_mute_state = BERLIN_MIC_MUTE_STATE_UNKNOWN;
	bc->max_ch_inuse = MAX_CHANNELS;

	err = input_register_device(bc->mic_mute);
	if (err) {
		snd_printk("%s: fail to register mic_mute_state %d\n",
			   __func__, err);
		goto __free_inputdev;
	}

	spin_lock_init(&bc->lock);

	return 0;

__free_inputdev:
	if (bc->mic_mute) {
		input_free_device(bc->mic_mute);
		bc->mic_mute = NULL;
	}
__destroy_wq:
	if (bc->wq) {
		destroy_workqueue(bc->wq);
		bc->wq = NULL;
	}
__free_kmem:
	kfree(bc);
	runtime->private_data = NULL;

	return err;
}

int berlin_capture_close(struct snd_pcm_substream *ss)
{
	/* disable audio interrupt */
	struct snd_pcm_runtime *runtime = ss->runtime;
	struct berlin_capture *bc = runtime->private_data;

	if (bc) {
		//need flush the delayed work after disable interrupt
		berlin_capture_hw_free(ss);

		if (bc->mic_mute) {
			input_unregister_device(bc->mic_mute);
			bc->mic_mute = NULL;
		}
		if (bc->wq) {
			destroy_workqueue(bc->wq);
			bc->wq = NULL;
		}

		cancel_work_sync(&bc->spdif_snd_notify_work);

		spin_lock_irq(&bc->lock);
		spdif_capture_state_reset(bc);
		if (bc->spdif_rate_ctl)
			bc->spdif_rate_ctl->private_data = NULL;
		if (bc->spdif_csb_ctl)
			bc->spdif_csb_ctl->private_data = NULL;
		bc->spdif_rate_ctl = NULL;
		bc->spdif_csb_ctl = NULL;
		spin_unlock_irq(&bc->lock);

		kfree(bc);
		runtime->private_data = NULL;
	}

	snd_printd("%s: substream 0x%p done\n", __func__, ss);
	return 0;
}
