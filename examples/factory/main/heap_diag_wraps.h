#pragma once

/*
 * Diagnostic-only wrappers.
 *
 * These macros intentionally wrap startup-critical calls so serial logs show the
 * heap delta around each operation. They do not change return values or behavior;
 * they only add before/after heap prints.
 */
#include "heap_diag.h"

#ifndef HEAP_DIAG_DISABLE_WRAP_MACROS

#define epd_init(...) HEAP_DIAG_VOID("epd_init", epd_init(__VA_ARGS__))
#define epd_set_vcom(...) HEAP_DIAG_VOID("epd_set_vcom", epd_set_vcom(__VA_ARGS__))
#define epd_hl_init(...) HEAP_DIAG_EXPR("epd_hl_init", epd_hl_init(__VA_ARGS__))
#define epd_set_rotation(...) HEAP_DIAG_VOID("epd_set_rotation", epd_set_rotation(__VA_ARGS__))
#define epd_set_lcd_pixel_clock_MHz(...) HEAP_DIAG_VOID("epd_set_lcd_pixel_clock_MHz", epd_set_lcd_pixel_clock_MHz(__VA_ARGS__))
#define epd_poweron(...) HEAP_DIAG_VOID("epd_poweron", epd_poweron(__VA_ARGS__))
#define epd_poweroff(...) HEAP_DIAG_VOID("epd_poweroff", epd_poweroff(__VA_ARGS__))
#define epd_clear(...) HEAP_DIAG_VOID("epd_clear", epd_clear(__VA_ARGS__))
#define epd_write_string(...) HEAP_DIAG_VOID("epd_write_string", epd_write_string(__VA_ARGS__))
#define epd_hl_update_screen(...) HEAP_DIAG_EXPR("epd_hl_update_screen", epd_hl_update_screen(__VA_ARGS__))
#define epd_hl_set_all_white(...) HEAP_DIAG_VOID("epd_hl_set_all_white", epd_hl_set_all_white(__VA_ARGS__))
#define epd_draw_rotated_image(...) HEAP_DIAG_VOID("epd_draw_rotated_image", epd_draw_rotated_image(__VA_ARGS__))

#define lv_init(...) HEAP_DIAG_VOID("lv_init", lv_init(__VA_ARGS__))
#define lv_disp_draw_buf_init(...) HEAP_DIAG_VOID("lv_disp_draw_buf_init", lv_disp_draw_buf_init(__VA_ARGS__))
#define lv_disp_drv_register(...) HEAP_DIAG_EXPR("lv_disp_drv_register", lv_disp_drv_register(__VA_ARGS__))
#define lv_indev_drv_register(...) HEAP_DIAG_EXPR("lv_indev_drv_register", lv_indev_drv_register(__VA_ARGS__))

#define ps_malloc(...) HEAP_DIAG_EXPR("ps_malloc", ps_malloc(__VA_ARGS__))
#define ps_calloc(...) HEAP_DIAG_EXPR("ps_calloc", ps_calloc(__VA_ARGS__))
#define heap_caps_malloc(...) HEAP_DIAG_EXPR("heap_caps_malloc", heap_caps_malloc(__VA_ARGS__))
#define heap_caps_calloc(...) HEAP_DIAG_EXPR("heap_caps_calloc", heap_caps_calloc(__VA_ARGS__))

#define xQueueCreate(...) HEAP_DIAG_EXPR("xQueueCreate", xQueueCreate(__VA_ARGS__))
#define xSemaphoreCreateMutex(...) HEAP_DIAG_EXPR("xSemaphoreCreateMutex", xSemaphoreCreateMutex(__VA_ARGS__))
#define xTaskCreate(...) HEAP_DIAG_EXPR("xTaskCreate", xTaskCreate(__VA_ARGS__))
#define xTaskCreatePinnedToCore(...) HEAP_DIAG_EXPR("xTaskCreatePinnedToCore", xTaskCreatePinnedToCore(__VA_ARGS__))
#define xTaskCreateStaticPinnedToCore(...) HEAP_DIAG_EXPR("xTaskCreateStaticPinnedToCore", xTaskCreateStaticPinnedToCore(__VA_ARGS__))

#endif
