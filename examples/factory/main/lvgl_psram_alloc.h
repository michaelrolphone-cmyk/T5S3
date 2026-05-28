#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void *lvgl_psram_malloc(size_t size);
void *lvgl_psram_realloc(void *ptr, size_t size);
void lvgl_psram_free(void *ptr);

#ifdef __cplusplus
}
#endif
