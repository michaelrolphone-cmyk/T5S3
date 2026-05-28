#pragma once

#define LV_COLOR_DEPTH 16

/*
 * Route LVGL object/style/event/timer allocations through PSRAM.
 * The display draw buffers are still allocated explicitly by lv_port_disp_init().
 */
#define LV_MEM_CUSTOM 1
#define LV_MEM_CUSTOM_INCLUDE "lvgl_psram_alloc.h"
#define LV_MEM_CUSTOM_ALLOC lvgl_psram_malloc
#define LV_MEM_CUSTOM_FREE lvgl_psram_free
#define LV_MEM_CUSTOM_REALLOC lvgl_psram_realloc

#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
