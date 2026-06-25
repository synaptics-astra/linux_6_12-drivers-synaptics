/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2018-2020 Synaptics Incorporated */

#ifndef __BERLIN_CAPTURE_H__
#define __BERLIN_CAPTURE_H__

#include <sound/soc.h>

#define SNDRV_CTL_NAME_IEC958_CAPTURE_SAMPLE_RATE \
	"SPDIFIN Sample Rate"
#define SNDRV_CTL_NAME_IEC958_CAPTURE_DEFAULT "SPDIFIN IEC958 Capture Default"

void berlin_capture_set_ch_mode(struct snd_pcm_substream *substream,
				u32 ch_num, u32 *chid, u32 mode,
				bool enable_mic_mute, bool interleaved_mode,
				bool dummy_data, bool multi_lanes, u32 channel_map,
				bool ch_shift_check, void * aio_handle);
void berlin_capture_set_spdif_sample_rate_provider(
				struct snd_pcm_substream *substream,
				u32 (*get_fs_fn)(void *data), void *data);
int berlin_capture_hw_free(struct snd_pcm_substream *substream);
int berlin_capture_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params);
int berlin_capture_prepare(struct snd_pcm_substream *substream);
int berlin_capture_trigger(struct snd_pcm_substream *substream, int cmd);
snd_pcm_uframes_t
berlin_capture_pointer(struct snd_pcm_substream *substream);
int berlin_capture_isr(struct snd_pcm_substream *substream);
int berlin_capture_open(struct snd_pcm_substream *substream);
int berlin_capture_close(struct snd_pcm_substream *substream);
void berlin_capture_set_ch_inuse(struct snd_pcm_substream *substream,
				 u32 ch_num);
u32 berlin_capture_get_pause_count(struct snd_pcm_substream *substream);
int berlin_capture_spdif_sample_rate_get(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol);
int berlin_capture_spdif_sample_rate_info(struct snd_kcontrol *kcontrol,
					   struct snd_ctl_elem_info *uinfo);
int berlin_capture_spdif_control_status_buffer_get(struct snd_kcontrol *kcontrol,
					   struct snd_ctl_elem_value *ucontrol);
int berlin_capture_spdif_control_status_buffer_info(struct snd_kcontrol *kcontrol,
					   struct snd_ctl_elem_info *uinfo);
int berlin_indai_aip_alloc(struct snd_pcm_substream *ss, void **pFrame, int *size);
int berlin_aip_event_callback_newframe(struct snd_pcm_substream *ss);
void set_mic_mute_state(struct snd_pcm_substream *ss, int mute);

#endif
