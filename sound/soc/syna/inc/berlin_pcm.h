/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2018-2020 Synaptics Incorporated */

#ifndef __BERLIN_PCM_H__
#define __BERLIN_PCM_H__

#include <linux/platform_device.h>
#include <linux/atomic.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>
#include "aio_priv.h"
#include "avio_dhub_drv.h"
#include "hal_dhub.h"
#include "hal_dhub_wrap.h"
#include "kernel_compatibility.h"

enum berlin_xrun_t {
	PCM_OVERRUN,
	FIFO_OVERRUN,
	PCM_UNDERRUN,
	FIFO_UNDERRUN,
	IRQ_DISABLE,
	XRUN_T_MAX
};

/* Each capture substream support either I2S OR PDM, not BOTH! */
#define I2SI_MODE		(1)
#define PDMI_MODE		(I2SI_MODE << 1)
#define SPDIFI_MODE		(I2SI_MODE << 2)
#define DMICI_MODE		(I2SI_MODE << 3)
#define HDMII_MODE		(I2SI_MODE << 7)
#define FLAG_EARC_MODE	(I2SI_MODE << 0xF)
/*
 * Playback substream may masked to I2S AND SPDIF at the sametime
 * The reason is we need to consider the customer use case, currently
 * they only feed one output stream but want playback to i2s and spdif.
 * Can split into I2S and SPDIF dai in further if customer use case allow
 */
#define I2SO_MODE	(I2SI_MODE << 4)
#define SPDIFO_MODE	(I2SI_MODE << 5)
#define HDMIO_MODE	(I2SI_MODE << 6)

/* used to identify dai's format
 * different dai may have different format at the same time
 * ex. spdif: pcm
 *     hdmi : iec61937
 */
#define DAI_FMT_NOT_SET 	0
#define DAI_FMT_PCM 		1
#define DAI_FMT_IEC61937 	2

// Support up to MAX_CHANNELS audio channels, MAX_CHANNELS/2 dhub channels
#define MAX_CHID		(MAX_CHANNELS >> 1)

#define HDMI_STEREO_CHANNEL_NUM (2)
#define HDMI_PCM_MAX_CHANNEL_NUM (8)

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 12, 0))
#if defined(CONFIG_SND_DEBUG) || defined(CONFIG_SND_VERBOSE_PRINTK)
#define	snd_printd	printk
#define	snd_printk	printk
#else
#define	snd_printd(...)	do { } while (0)
#define	snd_printk(...)	do { } while (0)
#endif
#else
#define	snd_soc_rtd_to_cpu	asoc_rtd_to_cpu
#define	snd_soc_rtd_to_codec	asoc_rtd_to_codec
#endif

struct berlin_chip {
	struct snd_card *card;
	struct platform_device *pdev;
	struct snd_hwdep *hwdep;
	void __iomem *pll_base;
	atomic_long_t xruns[XRUN_T_MAX];
	HDL_dhub *dhub;
	struct mutex dhub_lock;
	int passthrough_enable;	/* for playback */
};

struct berlin_ss_params {
	u32 irq_num;
	u32 chid_num;
	u32 mode;
	u32 channel_map;
	bool interleaved;
	bool dummy_data;
	bool multi_lanes;
	bool enable_mic_mute;
	u32 dai_fmt;
	bool ch_shift_check;	/* for dmic multi-channel shift check */
	unsigned int *irq;
	const char *dev_name;
	void *aio_handle;
	void (*dai_stop_fn)(void *);
	void *dai_stop_data;
};

struct iec61937_burst_header {
	u16 syncword1;
	u16 syncword2;
	u16 burstinfo;
	u16 len;
};

enum eAudioFreq {
	eAUD_Freq_8K = 8000,
	eAUD_Freq_11K = 11025,
	eAUD_Freq_12K = 12000,
	eAUD_Freq_16K = 16000,
	eAUD_Freq_22K = 22050,
	eAUD_Freq_24K = 24000,
	eAUD_Freq_32K = 32000,
	eAUD_Freq_44K = 44100,
	eAUD_Freq_48K = 48000,
	eAUD_Freq_64K = 64000,
	eAUD_Freq_88K = 88200,
	eAUD_Freq_96K = 96000,
	eAUD_Freq_176K = 176400,
	eAUD_Freq_192K = 192000,
	eAUD_Freq_384K = 384000,
	eAUD_Freq_768K = 768000,
};
void berlin_report_xrun(struct berlin_chip *chip, enum berlin_xrun_t xrun_type);
int berlin_pcm_request_dma_irq(struct snd_pcm_substream *substream,
					struct berlin_ss_params *params);
void berlin_pcm_free_dma_irq(struct snd_pcm_substream *substream,
				u32 irq_num, unsigned int *irq);
void berlin_pcm_max_ch_inuse(struct snd_pcm_substream *ss,
				u32 ch_num);
bool berline_pcm_passthrough_check(struct snd_pcm_substream *substream,
					u32 *data_type);
void berlin_pcm_hdmi_bitstream_start(struct snd_pcm_substream *substream);
u32 berlin_get_pause_count(struct snd_pcm_substream *substream);
int berlin_pcm_indai_aip_alloc(struct snd_pcm_substream *ss, void **pFrame, int *size);
void berlin_pcm_set_ch_mode(struct snd_pcm_substream *ss,
	u32 chid_num, u32 *chid, u32 mode,
	bool enable_mic_mute, bool interleaved_mode,
	bool dummy_data, bool multi_lanes, u32 channel_map,
	bool ch_shift_check, void * aio_handle);
void berlin_pcm_set_spdif_sample_rate_provider(struct snd_pcm_substream *ss,
				u32 (*get_fs_fn)(void *data), void *data);
void berlin_pcm_set_volume_db(struct snd_pcm_substream *ss, u32 mode, int volume_db);
void berlin_pcm_set_mic_mute_state(struct snd_pcm_substream *ss, int mute);
int berlin_pcm_aip_event_callback_newframe(struct snd_pcm_substream *ss);
#endif
