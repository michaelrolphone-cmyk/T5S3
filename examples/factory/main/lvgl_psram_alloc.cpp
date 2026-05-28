#include "lvgl_psram_alloc.h"

#include <Arduino.h>
#include <string.h>
#include "esp_heap_caps.h"

extern "C" void *lvgl_psram_malloc(size_t size)
{
    if (size == 0) {
        return nullptr;
    }

    void *ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ptr) {
        Serial.printf("[LVGL PSRAM] malloc failed in PSRAM size=%u; falling back to internal heap\n", (unsigned)size);
        ptr = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return ptr;
}

extern "C" void *lvgl_psram_realloc(void *ptr, size_t size)
{
    if (size == 0) {
        heap_caps_free(ptr);
        return nullptr;
    }

    void *new_ptr = heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!new_ptr) {
        Serial.printf("[LVGL PSRAM] realloc failed in PSRAM size=%u; falling back to internal heap\n", (unsigned)size);
        new_ptr = heap_caps_realloc(ptr, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    return new_ptr;
}

extern "C" void lvgl_psram_free(void *ptr)
{
    heap_caps_free(ptr);
}
