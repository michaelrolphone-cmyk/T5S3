#include "heap_diag.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

void heap_diag_mark(const char *tag)
{
    multi_heap_info_t internal = {};
    multi_heap_info_t psram = {};
    heap_caps_get_info(&internal, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    heap_caps_get_info(&psram, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    Serial.printf(
        "[HEAP] %s core=%d internal_free=%u internal_largest=%u internal_min=%u internal_alloc=%u internal_free_blocks=%u internal_alloc_blocks=%u psram_free=%u psram_largest=%u psram_min=%u psram_alloc=%u\n",
        tag ? tag : "null",
        xPortGetCoreID(),
        (unsigned)internal.total_free_bytes,
        (unsigned)internal.largest_free_block,
        (unsigned)internal.minimum_free_bytes,
        (unsigned)internal.total_allocated_bytes,
        (unsigned)internal.free_blocks,
        (unsigned)internal.allocated_blocks,
        (unsigned)psram.total_free_bytes,
        (unsigned)psram.largest_free_block,
        (unsigned)psram.minimum_free_bytes,
        (unsigned)psram.total_allocated_bytes);
}
