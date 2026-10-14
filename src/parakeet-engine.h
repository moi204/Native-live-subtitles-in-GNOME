#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef struct ParakeetEngine ParakeetEngine;
typedef void (*ParakeetTextCallback)(const char *text, void *user_data);

ParakeetEngine *parakeet_engine_new(const char *model_path, const char *language,
                                    ParakeetTextCallback callback, void *user_data);
void parakeet_engine_free(ParakeetEngine *engine);
bool parakeet_engine_set_language(ParakeetEngine *engine, const char *language);
void parakeet_engine_feed(ParakeetEngine *engine, const short *samples, size_t count);
