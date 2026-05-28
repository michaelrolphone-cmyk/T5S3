#include <Arduino.h>
#include <string.h>
#include <lvgl.h>
#include <epdiy.h>

extern uint8_t *decodebuffer;

extern "C" lv_disp_t *__real_lv_disp_drv_register(lv_disp_drv_t *driver);

static void (*previous_render_start_cb)(lv_disp_drv_t *driver) = nullptr;
static uint32_t render_start_clear_count = 0;

static size_t epd_logical_frame_size()
{
    int width = epd_rotated_display_width();
    int height = epd_rotated_display_height();
    if (width <= 0 || height <= 0) return 0;
    return (((size_t)width + 1U) / 2U) * (size_t)height;
}

static void render_start_clear_decodebuffer(lv_disp_drv_t *driver)
{
    if (previous_render_start_cb) {
        previous_render_start_cb(driver);
    }

    size_t frame_size = epd_logical_frame_size();
    if (!decodebuffer || frame_size == 0) {
        return;
    }

    memset(decodebuffer, 0xFF, frame_size);
    render_start_clear_count++;

    if (render_start_clear_count <= 5 || (render_start_clear_count % 25) == 0) {
        Serial.printf("[LVGL RENDER] logical EPD framebuffer cleared at render_start count=%lu bytes=%u\n",
                      (unsigned long)render_start_clear_count,
                      (unsigned)frame_size);
    }
}

extern "C" lv_disp_t *__wrap_lv_disp_drv_register(lv_disp_drv_t *driver)
{
    if (driver) {
        previous_render_start_cb = driver->render_start_cb;
        driver->render_start_cb = render_start_clear_decodebuffer;
        driver->full_refresh = 1;
        Serial.println("[LVGL RENDER] render_start logical framebuffer clear installed; full_refresh forced");
    }

    return __real_lv_disp_drv_register(driver);
}
