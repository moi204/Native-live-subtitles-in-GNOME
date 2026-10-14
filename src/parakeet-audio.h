#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef void (*ParakeetAudioCallback)(const short *samples, size_t count, void *user_data);

typedef struct ParakeetAudio ParakeetAudio;

ParakeetAudio *parakeet_audio_start(bool microphone, ParakeetAudioCallback callback, void *user_data);
void parakeet_audio_stop(ParakeetAudio *audio);
