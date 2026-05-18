/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2018-2020 Synaptics Incorporated */

#ifndef __BERLIN_PLAYBACK_H__
#define __BERLIN_PLAYBACK_H__

#include <sound/soc.h>

void berlin_playback_set_ch_mode(struct snd_pcm_substream *ss,
		u32 ch_num, u32 *ch, u32 mode, u32 dai_fmt,
		void (*dai_stop_fn)(void *), void *dai_stop_data);
void berlin_playback_set_volume_db(struct snd_pcm_substream *ss,
								   u32 mode, int volume_db);
int berlin_playback_hw_free(struct snd_pcm_substream *ss);
int berlin_playback_hw_params(struct snd_pcm_substream *ss,
								struct snd_pcm_hw_params *p);
int berlin_playback_prepare(struct snd_pcm_substream *ss);
int berlin_playback_trigger(struct snd_pcm_substream *ss, int cmd);
int berlin_playback_ack(struct snd_pcm_substream *ss);
snd_pcm_uframes_t berlin_playback_pointer(struct snd_pcm_substream *ss);
int berlin_playback_isr(struct snd_pcm_substream *ss,
						unsigned int chanId);
int berlin_playback_open(struct snd_pcm_substream *ss, int passthrough);
int berlin_playback_close(struct snd_pcm_substream *ss);
bool berlin_playback_passthrough_check(struct snd_pcm_substream *substream,
			       u32 *data_type);
void berlin_playback_hdmi_bitstream_start(struct snd_pcm_substream *substream);
u32 berlin_playback_get_pause_count(struct snd_pcm_substream *ss);
#endif
