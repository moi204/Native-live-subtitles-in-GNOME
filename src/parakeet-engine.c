#include "parakeet-engine.h"

#include <parakeet_capi.h>
#include <glib.h>
#include <stdlib.h>
#include <string.h>

struct ParakeetEngine {
    parakeet_ctx *context;
    parakeet_stream *stream;
    char *model_path;
    char *language;
    GMutex lock;
    ParakeetTextCallback callback;
    void *user_data;
};

static bool start_stream(ParakeetEngine *engine) {
    if (engine->stream) parakeet_capi_stream_free(engine->stream);
    engine->stream = parakeet_capi_stream_begin_lang(engine->context, engine->language);
    if (!engine->stream) {
        g_warning("Unable to start Parakeet stream: %s", parakeet_capi_last_error(engine->context));
        return false;
    }
    return true;
}

ParakeetEngine *parakeet_engine_new(const char *model_path, const char *language,
                                    ParakeetTextCallback callback, void *user_data) {
    ParakeetEngine *engine = g_new0(ParakeetEngine, 1);
    engine->context = parakeet_capi_load(model_path);
    if (!engine->context) {
        g_warning("Unable to load Parakeet model %s: %s", model_path, parakeet_capi_load_error());
        g_free(engine);
        return NULL;
    }
    engine->model_path = g_strdup(model_path);
    engine->language = g_strdup(language && *language ? language : "auto");
    engine->callback = callback;
    engine->user_data = user_data;
    g_mutex_init(&engine->lock);
    if (!start_stream(engine)) {
        parakeet_engine_free(engine);
        return NULL;
    }
    return engine;
}

bool parakeet_engine_set_language(ParakeetEngine *engine, const char *language) {
    g_mutex_lock(&engine->lock);
    g_free(engine->language);
    engine->language = g_strdup(language && *language ? language : "auto");
    bool ok = start_stream(engine);
    g_mutex_unlock(&engine->lock);
    return ok;
}

void parakeet_engine_feed(ParakeetEngine *engine, const short *samples, size_t count) {
    g_mutex_lock(&engine->lock);
    if (!engine->stream) {
        g_mutex_unlock(&engine->lock);
        return;
    }
    float *pcm = g_new(float, count);
    for (size_t i = 0; i < count; i++) pcm[i] = samples[i] / 32768.0f;
    char *text = parakeet_capi_stream_feed(engine->stream, pcm, (int)count, NULL);
    g_free(pcm);
    if (text && *text) engine->callback(text, engine->user_data);
    parakeet_capi_free_string(text);
    g_mutex_unlock(&engine->lock);
}

void parakeet_engine_free(ParakeetEngine *engine) {
    if (!engine) return;
    if (engine->stream) parakeet_capi_stream_free(engine->stream);
    if (engine->context) parakeet_capi_free(engine->context);
    g_free(engine->model_path);
    g_free(engine->language);
    g_mutex_clear(&engine->lock);
    g_free(engine);
}
