#include <Arduino.h>
#include <esp_heap_caps.h>
#include <stdint.h>
#include <stddef.h>

extern "C" void *__real_heap_caps_malloc(size_t size, uint32_t caps);

#ifndef EPD_HEAP_LARGE_ALLOC_THRESHOLD
#define EPD_HEAP_LARGE_ALLOC_THRESHOLD 16384
#endif

static bool should_place_large_buffer_in_psram(size_t size, uint32_t caps)
{
    bool wants_internal = (caps & MALLOC_CAP_INTERNAL) != 0;
    bool wants_8bit = (caps & MALLOC_CAP_8BIT) != 0;
    bool wants_dma = (caps & MALLOC_CAP_DMA) != 0;
    bool wants_exec = (caps & MALLOC_CAP_EXEC) != 0;
    bool wants_psram = (caps & MALLOC_CAP_SPIRAM) != 0;

    return size >= EPD_HEAP_LARGE_ALLOC_THRESHOLD &&
           wants_internal &&
           wants_8bit &&
           !wants_dma &&
           !wants_exec &&
           !wants_psram;
}

extern "C" void *__wrap_heap_caps_malloc(size_t size, uint32_t caps)
{
    if (should_place_large_buffer_in_psram(size, caps)) {
        void *ptr = __real_heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (ptr) {
            Serial.printf("[HEAP FIX] large allocation redirected to PSRAM size=%u caps=0x%08lx internal_free=%u internal_largest=%u psram_free=%u\n",
                          (unsigned)size,
                          (unsigned long)caps,
                          (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                          (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                          (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
            return ptr;
        }
        Serial.printf("[HEAP FIX] PSRAM redirect failed size=%u caps=0x%08lx; using original allocator\n",
                      (unsigned)size,
                      (unsigned long)caps);
    }

    return __real_heap_caps_malloc(size, caps);
}
