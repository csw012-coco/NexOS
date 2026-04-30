#pragma once

#include "user/public/sysapi.h"

#define NEX_AUDIO_CAP_PLAYBACK SYS_AUDIO_CAP_PLAYBACK
#define NEX_AUDIO_CAP_TONE SYS_AUDIO_CAP_TONE

#define NEX_AUDIO_DRIVER_NONE SYS_AUDIO_DRIVER_NONE
#define NEX_AUDIO_DRIVER_AC97 SYS_AUDIO_DRIVER_AC97

int ac97_query(struct syscall_ac97_info *info);
int audio_query(uint32_t index, struct syscall_audio_info *info);
int audio_tone(uint32_t index, uint32_t hz, uint32_t duration_ms);
int audio_play(uint32_t index, const struct syscall_audio_play_info *info);
