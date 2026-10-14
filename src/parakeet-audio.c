#include "parakeet-audio.h"

#include <pulse/simple.h>
#include <pulse/error.h>
#include <glib.h>
#include <stdlib.h>

struct ParakeetAudio {
    pa_simple *stream;
    GThread *thread;
    volatile bool stopping;
    ParakeetAudioCallback callback;
    void *user_data;
};

static gpointer capture_thread(gpointer user_data) {
    ParakeetAudio *audio = user_data;
    short samples[1600];
    int error = 0;

    while (!audio->stopping) {
        if (pa_simple_read(audio->stream, samples, sizeof(samples), &error) < 0)
            break;
        if (!audio->stopping)
            audio->callback(samples, G_N_ELEMENTS(samples), audio->user_data);
    }
    return NULL;
}

ParakeetAudio *parakeet_audio_start(bool microphone, ParakeetAudioCallback callback, void *user_data) {
    const pa_sample_spec spec = {
        .format = PA_SAMPLE_S16LE,
        .rate = 16000,
        .channels = 1,
    };
    const char *device = microphone ? NULL : "@DEFAULT_SINK@.monitor";
    int error = 0;
    pa_simple *stream = pa_simple_new(NULL, "Live Captions", PA_STREAM_RECORD,
                                      device, "Live captions", &spec, NULL, NULL, &error);
    if (!stream) {
        g_warning("Unable to open audio source: %s", pa_strerror(error));
        return NULL;
    }

    ParakeetAudio *audio = g_new0(ParakeetAudio, 1);
    audio->stream = stream;
    audio->callback = callback;
    audio->user_data = user_data;
    audio->thread = g_thread_new("parakeet-audio", capture_thread, audio);
    return audio;
}

void parakeet_audio_stop(ParakeetAudio *audio) {
    if (!audio) return;
    audio->stopping = true;
    if (audio->thread) g_thread_join(audio->thread);
    pa_simple_free(audio->stream);
    g_free(audio);
}
