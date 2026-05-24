/* Simple firmware for a ESP32 displaying a static image on an EPaper Screen.
 *
 * Write an image into a header file using a 3...2...1...0 format per pixel,
 * for 4 bits color (16 colors - well, greys.) MSB first.  At 80 MHz, screen
 * clears execute in 1.075 seconds and images are drawn in 1.531 seconds.
 */

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_sleep.h>
#include <esp_timer.h>
#include <esp_types.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <epdiy.h>
#include "lvgl.h"
#include "main.h"
#include "ui.h"
#include "ui_port.h"
#include "peripheral.h"

// Arduino
#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <driver/i2c.h>
#include "scr_mrg.h"
#include "firasans_12.h"
#include "firasans_20.h"
#include "ui_port.h"
#include "nvs_param.h"
#include <PNGdec.h>
#include <math.h>

char global_buf[GLOBAL_BUF_LEN];

TaskHandle_t btn_handle;

// peripheral
bool peri_buf[E_PERI_MAX] = {0};


// bq25896
XPowersPPM PPM;

BQ27220 bq27220;

// Ink Screen
#define WAVEFORM EPD_BUILTIN_WAVEFORM
#define DEMO_BOARD epd_board_v7
EpdiyHighlevelState hl;

#ifndef EPD_SELFTEST_ON_BOOT
#define EPD_SELFTEST_ON_BOOT 0
#endif

// Touch
TouchDrvGT911 touch;

// RTC
SensorPCF8563 rtc;

// LVGL
#define DISP_BUF_SIZE (epd_rotated_display_width() * epd_rotated_display_height())
#define EPD_IMAGE_BUF_SIZE (((epd_rotated_display_width() + 1) / 2) * epd_rotated_display_height())
uint8_t *decodebuffer = NULL;
uint8_t *displaybuffer = NULL;
static constexpr uint8_t EPD_LOGICAL_WHITE_BYTE = 0xFF;
volatile bool disp_flush_enabled = true;
volatile bool indev_touch_enabled = true;
enum HomeInputState {
    HOME_INPUT_IDLE = 0,
    HOME_INPUT_SUPPRESS_UNTIL_RELEASE,
    HOME_INPUT_POST_RELEASE_DEADBAND,
    HOME_INPUT_WAIT_FRESH_PRESS
};

static volatile HomeInputState home_input_state = HOME_INPUT_IDLE;
static volatile uint32_t home_input_state_start_ms = 0;
static volatile uint32_t home_input_deadline_ms = 0;
static volatile bool home_nav_in_progress = false;
static volatile uint32_t home_button_last_ms = 0;
static volatile bool home_waiting_for_redraw_commit = false;
static volatile uint32_t home_redraw_deadline_ms = 0;
static constexpr uint32_t HOME_REDRAW_COMMIT_TIMEOUT_MS = 8000;
static lv_indev_t *touch_indev = NULL;
static QueueHandle_t ui_event_q = NULL;
static TaskHandle_t ui_task_handle = NULL;

enum class UiEvent : uint8_t {
    BOOT_SLEEP,
    TOGGLE_BACKLIGHT,
    HOME_SWITCH_TO_SPRINGBOARD,
};

static bool ui_post_event(UiEvent event)
{
    if (!ui_event_q) {
        return false;
    }
    return xQueueSend(ui_event_q, &event, 0) == pdTRUE;
}
bool disp_refr_is_busy = false;
static volatile bool disp_force_clear_next_flush = false;
enum DisplayUpdateKind {
    DISPLAY_UPDATE_NONE = 0,
    DISPLAY_UPDATE_NORMAL_FRAME,
    DISPLAY_UPDATE_SCREEN_REPLACE,
    DISPLAY_UPDATE_BOOT_REPLACE,
    DISPLAY_UPDATE_RECOVERY_CLEAN,
    DISPLAY_UPDATE_SHUTDOWN_IMAGE
};

struct DisplayCmd {
    uint32_t seq;
    DisplayUpdateKind kind;
    uint8_t *snapshot;
    bool has_dirty_union;
    lv_area_t dirty_union;
};

static constexpr uint8_t DISPLAY_SNAPSHOT_COUNT = 3;
static uint8_t *display_snapshot_pool[DISPLAY_SNAPSHOT_COUNT] = {NULL};
static volatile int8_t display_snapshot_owner[DISPLAY_SNAPSHOT_COUNT] = {-1, -1, -1}; // -1 free, 0 producer, 1 worker
static QueueHandle_t display_q = NULL;
static SemaphoreHandle_t display_snapshot_mutex = NULL;
static volatile uint32_t display_seq_counter = 0;
static volatile uint32_t disp_last_flush_ms = 0;
static constexpr uint32_t EPD_FRAME_SETTLE_MS = 150;
static volatile DisplayUpdateKind display_next_snapshot_kind = DISPLAY_UPDATE_NONE;
static volatile DisplayUpdateKind display_reliable_pending_kind = DISPLAY_UPDATE_NONE;
static uint32_t disp_lvgl_flush_count = 0;
static uint32_t disp_physical_commit_count = 0;
static uint32_t disp_replace_commit_count = 0;
static TaskHandle_t disp_flush_handle = NULL;
static constexpr uint32_t DISP_FLUSH_STACK_BYTES = 8 * 1024;
static StackType_t disp_flush_stack[DISP_FLUSH_STACK_BYTES / sizeof(StackType_t)];
static StaticTask_t disp_flush_task_tcb;
static volatile bool disp_flush_task_started = false;
static volatile bool disp_flush_task_create_failed = false;
static SemaphoreHandle_t framebuffer_mutex = NULL;
static SemaphoreHandle_t sd_mutex = NULL;
static SemaphoreHandle_t physical_display_mutex = NULL;
static volatile bool display_physical_commit_active = false;
static volatile bool shutdown_in_progress = false;
static bool display_have_vbus(void);
static bool display_safe_for_hard_clean(void);
static bool display_have_vbus_provisional(void);
static bool display_safe_for_hard_clean_boot(void);
void disp_request_normal_frame(void);
void disp_request_screen_replace(void);
void disp_request_boot_replace(void);
void disp_request_recovery_clean(void);
static bool display_commit_frame(DisplayUpdateKind kind, const uint8_t *framebuffer4bpp);
static bool display_internal_heap_ok_for_hard_clean();
static bool display_internal_heap_critical();

static bool publish_snapshot(DisplayUpdateKind kind, bool has_dirty_union, const lv_area_t *dirty_union);
static bool display_cmd_is_reliable(DisplayUpdateKind kind);
static void release_snapshot(uint32_t seq, uint8_t *snapshot);
static inline int display_update_kind_priority(DisplayUpdateKind kind);
static void display_set_next_snapshot_kind(DisplayUpdateKind kind);
static void ensure_display_flush_task_started(void);
bool disp_show_sleep_png_from_sd(const char *preferred_path);
bool display_begin_shutdown_sequence(uint32_t timeout_ms);
void display_cancel_pending_updates_for_shutdown(void);
bool display_show_shutdown_image_from_sd(const char *path);
static inline void epd_image_set_pixel_4bpp(uint8_t *buf, int32_t width, int32_t x, int32_t y, uint8_t gray4);

static inline uint8_t rgb565_to_gray4(uint16_t rgb565)
{
    uint8_t r = ((rgb565 >> 11) & 0x1F) * 255 / 31;
    uint8_t g = ((rgb565 >> 5) & 0x3F) * 255 / 63;
    uint8_t b = (rgb565 & 0x1F) * 255 / 31;
    uint8_t gray = (uint8_t)((299 * r + 587 * g + 114 * b) / 1000);
    return (uint8_t)(gray >> 4);
}

static PNG *sleep_png_decoder_ctx = NULL;
static uint16_t *sleep_png_line_buf_ctx = NULL;
static float sleep_png_scale_ctx = 1.0f;
static int sleep_png_offset_x_ctx = 0;
static int sleep_png_offset_y_ctx = 0;
static int sleep_png_screen_w_ctx = 0;
static int sleep_png_screen_h_ctx = 0;

static int sleep_png_draw_cb(PNGDRAW *pDraw)
{
    if (!pDraw || !sleep_png_decoder_ctx || !sleep_png_line_buf_ctx || !decodebuffer) return 0;
    sleep_png_decoder_ctx->getLineAsRGB565(pDraw, sleep_png_line_buf_ctx, PNG_RGB565_LITTLE_ENDIAN, 0xffffffff);
    const int src_y = pDraw->y;
    int dst_y0 = sleep_png_offset_y_ctx + (int)floorf(src_y * sleep_png_scale_ctx);
    int dst_y1 = sleep_png_offset_y_ctx + (int)floorf((src_y + 1) * sleep_png_scale_ctx);
    if (dst_y1 <= dst_y0) dst_y1 = dst_y0 + 1;
    for (int src_x = 0; src_x < pDraw->iWidth; ++src_x) {
        uint8_t gray4 = rgb565_to_gray4(sleep_png_line_buf_ctx[src_x]);
        int dst_x0 = sleep_png_offset_x_ctx + (int)floorf(src_x * sleep_png_scale_ctx);
        int dst_x1 = sleep_png_offset_x_ctx + (int)floorf((src_x + 1) * sleep_png_scale_ctx);
        if (dst_x1 <= dst_x0) dst_x1 = dst_x0 + 1;
        for (int y = dst_y0; y < dst_y1; ++y) {
            if (y < 0 || y >= sleep_png_screen_h_ctx) continue;
            for (int x = dst_x0; x < dst_x1; ++x) {
                if (x < 0 || x >= sleep_png_screen_w_ctx) continue;
                epd_image_set_pixel_4bpp(decodebuffer, sleep_png_screen_w_ctx, x, y, gray4);
            }
        }
    }
    return 1;
}

void sd_guard_init()
{
    if (!sd_mutex) sd_mutex = xSemaphoreCreateMutex();
}

bool sd_guard_lock(uint32_t timeout_ms)
{
    return sd_mutex && xSemaphoreTake(sd_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void sd_guard_unlock()
{
    if (sd_mutex) xSemaphoreGive(sd_mutex);
}

bool disp_show_sleep_png_from_sd(const char *preferred_path)
{
    if (!peri_buf[E_PERI_SD_CARD] || !decodebuffer || !framebuffer_mutex) {
        Serial.printf("[SLEEP IMG] unavailable sd=%d decode=%d fb_mutex=%d\n",
                      peri_buf[E_PERI_SD_CARD] ? 1 : 0,
                      decodebuffer ? 1 : 0,
                      framebuffer_mutex ? 1 : 0);
        return false;
    }

    const char *preferred = (preferred_path && preferred_path[0]) ? preferred_path : SYSTEM_SLEEP_IMAGE_PATH;
    const char *candidates[] = {
        preferred,
        SYSTEM_SLEEP_IMAGE_PATH,
        LEGACY_SLEEP_IMAGE_PATH,
        FALLBACK_SLEEP_ICON_PATH,
    };

    if (!sd_guard_lock(3000)) {
        Serial.println("[SLEEP IMG] sd lock timeout");
        return false;
    }

    File f;
    const char *path = NULL;
    bool seen[4] = {false, false, false, false};
    Serial.printf("[SLEEP IMG] resolve preferred=%s\n", preferred);
    for (size_t i = 0; i < 4; ++i) {
        const char *candidate = candidates[i];
        if (!candidate || !candidate[0]) continue;
        bool duplicate = false;
        for (size_t j = 0; j < i; ++j) {
            if (seen[j] && strcmp(candidates[j], candidate) == 0) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            Serial.printf("[SLEEP IMG] skip duplicate path=%s\n", candidate);
            continue;
        }
        seen[i] = true;
        Serial.printf("[SLEEP IMG] try path=%s\n", candidate);
        File try_f = SD.open(candidate, FILE_READ);
        if (try_f && !try_f.isDirectory()) {
            f = try_f;
            path = candidate;
            break;
        }
        if (try_f) try_f.close();
    }

    if (!f || f.isDirectory() || !path) {
        sd_guard_unlock();
        Serial.printf("[SLEEP IMG] open failed preferred=%s canonical=%s legacy=%s fallback=%s\n",
                      preferred, SYSTEM_SLEEP_IMAGE_PATH, LEGACY_SLEEP_IMAGE_PATH, FALLBACK_SLEEP_ICON_PATH);
        return false;
    }
    Serial.printf("[SLEEP IMG] selected path=%s\n", path);

    const size_t png_size = (size_t)f.size();
    if (png_size < 8 || png_size > (8 * 1024 * 1024)) {
        f.close();
        sd_guard_unlock();
        Serial.printf("[SLEEP IMG] invalid size=%u path=%s\n", (unsigned)png_size, path);
        return false;
    }

    uint8_t *png_raw = (uint8_t *)ps_malloc(png_size);
    if (!png_raw) {
        f.close();
        sd_guard_unlock();
        Serial.println("[SLEEP IMG] raw alloc failed");
        return false;
    }
    const size_t read_n = f.read(png_raw, png_size);
    f.close();
    sd_guard_unlock();
    if (read_n != png_size) {
        free(png_raw);
        Serial.println("[SLEEP IMG] short read");
        return false;
    }

    const uint8_t png_magic[8] = {0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};
    if (memcmp(png_raw, png_magic, sizeof(png_magic)) != 0) {
        free(png_raw);
        Serial.println("[SLEEP IMG] invalid png header");
        return false;
    }

    const int screen_w = epd_rotated_display_width();
    const int screen_h = epd_rotated_display_height();
    Serial.printf("[SLEEP IMG] heap before free_internal=%u largest_internal=%u free_psram=%u largest_psram=%u\n",
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
    uint16_t *line_buf = (uint16_t *)ps_malloc(4096 * sizeof(uint16_t));
    if (!line_buf) {
        free(png_raw);
        Serial.println("[SLEEP IMG] line buffer alloc failed");
        return false;
    }

    PNG png;
    auto open_cb = [](PNGDRAW *pDraw) -> int { (void)pDraw; return 1; };
    int rc = png.openRAM(png_raw, (int)png_size, open_cb);
    if (rc != PNG_SUCCESS) {
        free(line_buf);
        free(png_raw);
        Serial.printf("[SLEEP IMG] png open failed rc=%d\n", rc);
        return false;
    }
    int src_w = png.getWidth();
    int src_h = png.getHeight();
    png.close();
    if (src_w <= 0 || src_h <= 0 || src_w > 4096 || src_h > 4096) {
        free(line_buf);
        free(png_raw);
        Serial.printf("[SLEEP IMG] invalid dims %dx%d\n", src_w, src_h);
        return false;
    }

    float scale = (float)screen_w / (float)src_w;
    int scaled_w = screen_w;
    int scaled_h = (int)((float)src_h * scale);
    if (scaled_h > screen_h) {
        scale = (float)screen_h / (float)src_h;
        scaled_h = screen_h;
        scaled_w = (int)((float)src_w * scale);
    }
    if (scaled_w < 1) scaled_w = 1;
    if (scaled_h < 1) scaled_h = 1;
    const int offset_x = (screen_w - scaled_w) / 2;
    const int offset_y = (screen_h - scaled_h) / 2;
    Serial.printf("[SLEEP IMG] png src=%dx%d display=%dx%d draw=%dx%d offset=%d,%d\n",
                  src_w, src_h, screen_w, screen_h, scaled_w, scaled_h, offset_x, offset_y);

    if (xSemaphoreTake(framebuffer_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        free(line_buf);
        free(png_raw);
        Serial.println("[SLEEP IMG] framebuffer lock timeout");
        return false;
    }
    memset(decodebuffer, EPD_LOGICAL_WHITE_BYTE, EPD_IMAGE_BUF_SIZE);

    sleep_png_decoder_ctx = &png;
    sleep_png_line_buf_ctx = line_buf;
    sleep_png_scale_ctx = scale;
    sleep_png_offset_x_ctx = offset_x;
    sleep_png_offset_y_ctx = offset_y;
    sleep_png_screen_w_ctx = screen_w;
    sleep_png_screen_h_ctx = screen_h;

    rc = png.openRAM(png_raw, (int)png_size, sleep_png_draw_cb);
    if (rc == PNG_SUCCESS) {
        rc = png.decode(NULL, 0);
    }
    png.close();
    free(line_buf);
    free(png_raw);

    if (rc != PNG_SUCCESS) {
        sleep_png_decoder_ctx = NULL;
        sleep_png_line_buf_ctx = NULL;
        xSemaphoreGive(framebuffer_mutex);
        Serial.printf("[SLEEP IMG] decode failed rc=%d\n", rc);
        return false;
    }

    sleep_png_decoder_ctx = NULL;
    sleep_png_line_buf_ctx = NULL;
    static uint8_t *shutdown_commit_buf = NULL;
    if (!shutdown_commit_buf) {
        shutdown_commit_buf = (uint8_t *)ps_malloc(EPD_IMAGE_BUF_SIZE);
    }
    if (!shutdown_commit_buf) {
        xSemaphoreGive(framebuffer_mutex);
        Serial.println("[SLEEP IMG] commit buffer alloc failed");
        return false;
    }
    memcpy(shutdown_commit_buf, decodebuffer, EPD_IMAGE_BUF_SIZE);
    xSemaphoreGive(framebuffer_mutex);
    Serial.println("[SLEEP IMG] commit begin");
    bool commit_ok = display_commit_frame(DISPLAY_UPDATE_SHUTDOWN_IMAGE, shutdown_commit_buf);
    Serial.printf("[SLEEP IMG] commit end %s\n", commit_ok ? "ok" : "failed");
    if (!commit_ok) {
        Serial.printf("[SLEEP IMG] physical commit failed path=%s\n", path);
        return false;
    }
    Serial.printf("[SLEEP IMG] heap after free_internal=%u largest_internal=%u free_psram=%u largest_psram=%u\n",
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
    Serial.printf("[SLEEP IMG] rendered: %s\n", path);
    return true;
}

/*********************************************************************************
 *                                   TASK
 * *******************************************************************************/
void btn_task(void *param)
{
    bool boot_btn_pressed = false;
    bool gpio_has_btn = false;
    bool gpio_btn_ready = false;
    bool gpio_btn_pressed = false;
    bool pca_btn_pressed = false;
    bool gpio_last_raw = false;
    bool gpio_saw_edge = false;
    uint32_t gpio_startup_inactive_ms = 0;
    uint32_t task_start_ms = millis();
    bool use_pca_fallback = false;

    while(1)
    {
        if (digitalRead(BOARD_BOOT_BTN) == LOW)
        {
            if (!boot_btn_pressed) {
                boot_btn_pressed = true;
                ui_post_event(UiEvent::BOOT_SLEEP);
            }
        }
        else {
            boot_btn_pressed = false;
        }

        bool gpio_raw = false;
        bool gpio_pressed = false;
#if defined(BOARD_IO48_BTN) && (BOARD_IO48_BTN >= 0)
        gpio_has_btn = true;
        gpio_raw = (digitalRead(BOARD_IO48_BTN) == HIGH);
        gpio_pressed = BOARD_IO48_BTN_ACTIVE_LOW ? !gpio_raw : gpio_raw;
        Serial.printf("[BUTTON RAW] gpio48=%d pressed=%d ready=%d fallback=%d\n",
                      gpio_raw, gpio_pressed, gpio_btn_ready, use_pca_fallback);

        uint32_t now_ms = millis();
        if (gpio_raw != gpio_last_raw) {
            gpio_last_raw = gpio_raw;
            gpio_saw_edge = true;
        }

        if (!gpio_btn_ready) {
            if (!gpio_pressed) {
                if (gpio_startup_inactive_ms == 0) {
                    gpio_startup_inactive_ms = now_ms;
                } else if ((now_ms - gpio_startup_inactive_ms) >= 300) {
                    gpio_btn_ready = true;
                }
            } else {
                gpio_startup_inactive_ms = 0;
            }
        } else {
            if (gpio_pressed) {
                gpio_btn_pressed = true;
            } else if (gpio_btn_pressed) {
                ui_post_event(UiEvent::TOGGLE_BACKLIGHT);
                Serial.printf("[BUTTON] release source=GPIO48 queued_toggle gpio48=%d\n", gpio_raw);
                gpio_btn_pressed = false;
            }
        }

        if (!gpio_saw_edge && ((now_ms - task_start_ms) >= 7000)) {
            use_pca_fallback = true;
        }
#endif

        if (!gpio_has_btn || use_pca_fallback) {
            bool pca_pressed = button_read();
            if (pca_pressed) {
                pca_btn_pressed = true;
            } else if (pca_btn_pressed) {
                ui_post_event(UiEvent::TOGGLE_BACKLIGHT);
                pca_btn_pressed = false;
            }
        }
        delay(80);
    }
}

/*********************************************************************************
 *                              FUNCTION
 * *******************************************************************************/
static inline void checkError(enum EpdDrawError err) {
    if (err != EPD_DRAW_SUCCESS) {
        ESP_LOGE("demo", "draw error: %X", err);
    }
}

static void epd_low_level_self_test()
{
    Serial.println("[EPD SELFTEST] begin");

    EpdRect full_area = {
        .x = 0,
        .y = 0,
        .width = epd_rotated_display_width(),
        .height = epd_rotated_display_height(),
    };

    epd_poweron();
    epd_clear();
    epd_poweroff();

    epd_hl_set_all_white(&hl);

    EpdFontProperties font_props = epd_font_properties_default();
    font_props.flags = EPD_DRAW_ALIGN_CENTER;

    int cx = epd_rotated_display_width() / 2;
    int cy = epd_rotated_display_height() / 2;

    epd_write_string(&FiraSans_20,
                     "BATTERY EPD SELF TEST",
                     &cx,
                     &cy,
                     epd_hl_get_framebuffer(&hl),
                     &font_props);

    (void)full_area;

    epd_poweron();
    EpdDrawError gl16_err = epd_hl_update_screen(&hl, MODE_GL16, epd_ambient_temperature());
    checkError(gl16_err);
    epd_poweroff();

    Serial.println("[EPD SELFTEST] complete");
}

void indev_touch_en()
{
    indev_touch_enabled = true;
}

void indev_touch_dis()
{
    indev_touch_enabled = false;
}

void disp_full_refresh(void)
{
    epd_hl_set_all_white(&hl);
    epd_poweron();
    EpdDrawError gl16_err = epd_hl_update_screen(&hl, MODE_GL16, epd_ambient_temperature());
    checkError(gl16_err);
    epd_poweroff();
}

void disp_full_clean(void)
{
    int refresh_timer = 12;
    epd_poweron();
    // fill_line_black
    for (int i = 0; i < 10; i++) {
        epd_push_pixels(epd_full_screen(), refresh_timer, 0);
    }
    // fill_line_white
    for(int i = 0; i < 10; i++) {
        epd_push_pixels(epd_full_screen(), refresh_timer, 1);
    }
    // fill_line_noop
    for (int i = 0; i < 2; i++) {
        epd_push_pixels(epd_full_screen(), refresh_timer, 2);
    }
    // epd_clear();
    epd_poweroff();
}

void dips_clean(void)
{
    EpdRect rener_area = {
        .x = 0,
        .y = 0,
        .width = epd_rotated_display_width(),
        .height = epd_rotated_display_height(),
    };

    disp_full_clean();
    epd_hl_set_all_white(&hl);
    epd_poweron();
    EpdDrawError gc16_err = epd_hl_update_screen(&hl, MODE_GC16, epd_ambient_temperature());
    checkError(gc16_err);
    epd_poweroff();

    epd_draw_rotated_image(rener_area, decodebuffer, epd_hl_get_framebuffer(&hl));
    epd_poweron();
    checkError(epd_hl_update_screen(&hl, MODE_GL16, epd_ambient_temperature()));
    epd_poweroff();
}

void disp_refresh_screen(void)
{
    EpdRect rener_area = {
        .x = 0,
        .y = 0,
        .width = epd_rotated_display_width(),
        .height = epd_rotated_display_height(),
    };

    disp_full_clean();
    epd_hl_set_all_white(&hl);
    epd_poweron();
    EpdDrawError gc16_err = epd_hl_update_screen(&hl, MODE_GC16, epd_ambient_temperature());
    checkError(gc16_err);
    epd_poweroff();

    epd_draw_rotated_image(rener_area, decodebuffer, epd_hl_get_framebuffer(&hl));
    epd_poweron();
    checkError(epd_hl_update_screen(&hl, MODE_GL16, epd_ambient_temperature()));
    epd_poweroff();
}

/*********************************************************************************
 *                            STATIC  FUNCTION
 * *******************************************************************************/
static inline uint8_t lv_color_to_epd_gray4(lv_color_t color)
{
    lv_color32_t c32;
    c32.full = lv_color_to32(color);

    uint16_t gray = (uint16_t)c32.ch.red * 76U +
                    (uint16_t)c32.ch.green * 150U +
                    (uint16_t)c32.ch.blue * 30U;
    uint8_t gray4 = (uint8_t)(((gray >> 8) + 8U) >> 4);
    return gray4 > 0x0F ? 0x0F : gray4;
}

static inline void epd_image_set_pixel_4bpp(uint8_t *buf, int32_t width, int32_t x, int32_t y, uint8_t gray4)
{
    const int32_t pitch = (width + 1) / 2;
    uint8_t *dst = &buf[y * pitch + (x >> 1)];

    // This epdiy fork stores the even/left pixel in the low nibble.
    if (x & 1) {
        *dst = (uint8_t)((*dst & 0x0F) | (gray4 << 4));
    } else {
        *dst = (uint8_t)((*dst & 0xF0) | gray4);
    }
}

static bool display_cmd_is_reliable(DisplayUpdateKind kind)
{
    return kind == DISPLAY_UPDATE_SCREEN_REPLACE ||
           kind == DISPLAY_UPDATE_BOOT_REPLACE ||
           kind == DISPLAY_UPDATE_RECOVERY_CLEAN ||
           kind == DISPLAY_UPDATE_SHUTDOWN_IMAGE;
}

static inline int display_update_kind_priority(DisplayUpdateKind kind)
{
    switch (kind) {
        case DISPLAY_UPDATE_SHUTDOWN_IMAGE: return 5;
        case DISPLAY_UPDATE_RECOVERY_CLEAN: return 4;
        case DISPLAY_UPDATE_BOOT_REPLACE: return 3;
        case DISPLAY_UPDATE_SCREEN_REPLACE: return 2;
        case DISPLAY_UPDATE_NORMAL_FRAME: return 1;
        case DISPLAY_UPDATE_NONE:
        default: return 0;
    }
}

static void display_set_next_snapshot_kind(DisplayUpdateKind kind)
{
    DisplayUpdateKind old_kind = display_next_snapshot_kind;
    if (display_update_kind_priority(kind) > display_update_kind_priority(old_kind)) {
        display_next_snapshot_kind = kind;
        Serial.printf("[DISPLAY QUEUE] next kind set old=%d new=%d\n", (int)old_kind, (int)kind);
    }
}

static void display_mark_reliable_pending(DisplayUpdateKind kind)
{
    if (!display_cmd_is_reliable(kind)) {
        return;
    }
    DisplayUpdateKind old_kind = display_reliable_pending_kind;
    if (display_update_kind_priority(kind) > display_update_kind_priority(old_kind)) {
        display_reliable_pending_kind = kind;
        Serial.printf("[DISPLAY QUEUE] reliable request pending kind=%d old=%d\n", (int)kind, (int)old_kind);
    }
}

static void release_snapshot(uint32_t seq, uint8_t *snapshot)
{
    if (!display_snapshot_mutex) {
        return;
    }
    if (xSemaphoreTake(display_snapshot_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        Serial.printf("[DISPLAY QUEUE ERROR] release lock timeout seq=%lu\n", (unsigned long)seq);
        return;
    }
    for (uint8_t i = 0; i < DISPLAY_SNAPSHOT_COUNT; ++i) {
        if (display_snapshot_pool[i] == snapshot) {
            display_snapshot_owner[i] = -1;
            Serial.printf("[DISPLAY QUEUE] release seq=%lu\n", (unsigned long)seq);
            xSemaphoreGive(display_snapshot_mutex);
            return;
        }
    }
    xSemaphoreGive(display_snapshot_mutex);
}

static void queue_or_replace(DisplayCmd &slot, bool &has_slot, const DisplayCmd &incoming, const char *tag)
{
    if (has_slot) {
        Serial.printf("[DISPLAY QUEUE] %s dropped seq=%lu kind=%d\n", tag,
                      (unsigned long)slot.seq, (int)slot.kind);
        release_snapshot(slot.seq, slot.snapshot);
    }
    slot = incoming;
    has_slot = true;
}

static void absorb_cmd(DisplayCmd &pending_normal, bool &has_pending_normal,
                       DisplayCmd &pending_reliable, bool &has_pending_reliable,
                       const DisplayCmd &in)
{
    if (in.kind == DISPLAY_UPDATE_NONE) {
        release_snapshot(in.seq, in.snapshot);
        return;
    }
    if (display_cmd_is_reliable(in.kind)) {
        if (has_pending_normal) {
            Serial.printf("[DISPLAY QUEUE] reliable supersedes normal seq=%lu reliable_kind=%d\n",
                          (unsigned long)pending_normal.seq, (int)in.kind);
            release_snapshot(pending_normal.seq, pending_normal.snapshot);
            has_pending_normal = false;
        }
        queue_or_replace(pending_reliable, has_pending_reliable, in, "replace reliable");
        return;
    }
    if (has_pending_reliable) {
        Serial.printf("[DISPLAY QUEUE] drop normal seq=%lu while reliable kind=%d pending\n",
                      (unsigned long)in.seq, (int)pending_reliable.kind);
        release_snapshot(in.seq, in.snapshot);
        return;
    }
    DisplayUpdateKind reliable_pending = display_reliable_pending_kind;
    if (display_cmd_is_reliable(reliable_pending)) {
        Serial.println("[DISPLAY QUEUE] normal suppressed while reliable publish pending");
        release_snapshot(in.seq, in.snapshot);
        return;
    }
    queue_or_replace(pending_normal, has_pending_normal, in, "coalesce normal");
}

void display_cancel_pending_updates_for_shutdown(void)
{
    if (!display_q) return;
    DisplayCmd cmd = {};
    uint32_t dropped = 0;
    while (xQueueReceive(display_q, &cmd, 0) == pdTRUE) {
        release_snapshot(cmd.seq, cmd.snapshot);
        dropped++;
    }
    Serial.printf("[SHUTDOWN] display queue drained dropped=%lu\n", (unsigned long)dropped);
}

bool display_begin_shutdown_sequence(uint32_t timeout_ms)
{
    shutdown_in_progress = true;
    disp_flush_enabled = false;
    indev_touch_enabled = false;
    display_next_snapshot_kind = DISPLAY_UPDATE_NONE;
    display_reliable_pending_kind = DISPLAY_UPDATE_NONE;
    disp_force_clear_next_flush = false;

    display_cancel_pending_updates_for_shutdown();

    uint32_t start = millis();
    while (display_physical_commit_active && (millis() - start) < timeout_ms) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    bool idle = !display_physical_commit_active;
    Serial.printf("[SHUTDOWN] display quiesce %s active=%d\n", idle ? "ok" : "timeout", idle ? 0 : 1);
    return idle;
}

bool display_show_shutdown_image_from_sd(const char *path)
{
    if (!display_begin_shutdown_sequence(30000)) {
        Serial.println("[SHUTDOWN] display quiesce failed; refusing unsafe final EPD commit");
        return false;
    }
    return disp_show_sleep_png_from_sd(path);
}

static void disp_flush_task(void *param)
{
    (void)param;
    disp_flush_task_started = true;
    Serial.printf("[DISPLAY TASK] started stack_hwm=%lu core=%d\n",
                  (unsigned long)uxTaskGetStackHighWaterMark(NULL), xPortGetCoreID());
    DisplayCmd cmd = {};
    DisplayCmd pending_normal = {};
    DisplayCmd pending_reliable = {};
    bool has_pending_normal = false;
    bool has_pending_reliable = false;

    while (1) {
        if (xQueueReceive(display_q, &cmd, pdMS_TO_TICKS(50)) != pdTRUE) {
            continue;
        }
        Serial.printf("[DISPLAY QUEUE] received seq=%lu kind=%d\n", (unsigned long)cmd.seq, (int)cmd.kind);

        absorb_cmd(pending_normal, has_pending_normal, pending_reliable, has_pending_reliable, cmd);

        Serial.printf("[DISPLAY QUEUE] settle wait begin ms=%lu window=%lu\n",
                      (unsigned long)millis(), (unsigned long)EPD_FRAME_SETTLE_MS);
        while (true) {
            uint32_t now = millis();
            bool settled = (now - disp_last_flush_ms) >= EPD_FRAME_SETTLE_MS;
            if (settled && uxQueueMessagesWaiting(display_q) == 0) {
                Serial.printf("[DISPLAY QUEUE] settle complete ms=%lu last_flush=%lu\n",
                              (unsigned long)now, (unsigned long)disp_last_flush_ms);
                break;
            }
            if (xQueueReceive(display_q, &cmd, pdMS_TO_TICKS(20)) == pdTRUE) {
                absorb_cmd(pending_normal, has_pending_normal, pending_reliable, has_pending_reliable, cmd);
            }
        }

        if (shutdown_in_progress && has_pending_reliable &&
            pending_reliable.kind != DISPLAY_UPDATE_SHUTDOWN_IMAGE) {
            Serial.printf("[SHUTDOWN] drop worker reliable seq=%lu kind=%d\n",
                          (unsigned long)pending_reliable.seq,
                          (int)pending_reliable.kind);
            release_snapshot(pending_reliable.seq, pending_reliable.snapshot);
            has_pending_reliable = false;
        }
        if (shutdown_in_progress && has_pending_normal) {
            Serial.printf("[SHUTDOWN] drop worker normal seq=%lu kind=%d\n",
                          (unsigned long)pending_normal.seq,
                          (int)pending_normal.kind);
            release_snapshot(pending_normal.seq, pending_normal.snapshot);
            has_pending_normal = false;
        }

        if (has_pending_reliable) {
            Serial.printf("[DISPLAY QUEUE] commit reliable seq=%lu kind=%d\n",
                          (unsigned long)pending_reliable.seq, (int)pending_reliable.kind);
            UBaseType_t hwm_before = uxTaskGetStackHighWaterMark(NULL);
            if (hwm_before < 256) {
                Serial.printf("[DISPLAY TASK WARN] low stack high water mark=%lu\n", (unsigned long)hwm_before);
            }
            Serial.printf("[DISPLAY TASK] before_commit stack_hwm=%lu\n", (unsigned long)hwm_before);
            display_commit_frame(pending_reliable.kind, pending_reliable.snapshot);
            Serial.printf("[DISPLAY TASK] after_commit stack_hwm=%lu\n", (unsigned long)uxTaskGetStackHighWaterMark(NULL));
            if (display_reliable_pending_kind == pending_reliable.kind) {
                display_reliable_pending_kind = DISPLAY_UPDATE_NONE;
                Serial.println("[DISPLAY QUEUE] reliable physical commit complete; pending cleared");
            }
            release_snapshot(pending_reliable.seq, pending_reliable.snapshot);
            has_pending_reliable = false;
        } else if (has_pending_normal) {
            if (display_cmd_is_reliable(display_reliable_pending_kind)) {
                Serial.println("[DISPLAY QUEUE] normal suppressed while reliable publish pending");
                release_snapshot(pending_normal.seq, pending_normal.snapshot);
                has_pending_normal = false;
                continue;
            }
            Serial.printf("[DISPLAY QUEUE] commit normal seq=%lu kind=%d\n",
                          (unsigned long)pending_normal.seq, (int)pending_normal.kind);
            UBaseType_t hwm_before = uxTaskGetStackHighWaterMark(NULL);
            if (hwm_before < 256) {
                Serial.printf("[DISPLAY TASK WARN] low stack high water mark=%lu\n", (unsigned long)hwm_before);
            }
            Serial.printf("[DISPLAY TASK] before_commit stack_hwm=%lu\n", (unsigned long)hwm_before);
            display_commit_frame(pending_normal.kind, pending_normal.snapshot);
            Serial.printf("[DISPLAY TASK] after_commit stack_hwm=%lu\n", (unsigned long)uxTaskGetStackHighWaterMark(NULL));
            release_snapshot(pending_normal.seq, pending_normal.snapshot);
            has_pending_normal = false;
        }
    }
}

static void disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
    if(decodebuffer == NULL) {
        lv_disp_flush_ready(disp);
        return;
    }

    if(disp_flush_enabled) {
        if (framebuffer_mutex && xSemaphoreTake(framebuffer_mutex, pdMS_TO_TICKS(20)) != pdTRUE) {
            lv_disp_flush_ready(disp);
            return;
        }
        int32_t w = lv_area_get_width(area);
        int32_t h = lv_area_get_height(area);
        int32_t screen_w = epd_rotated_display_width();
        int32_t screen_h = epd_rotated_display_height();
        bool full_area = (area->x1 == 0 && area->y1 == 0 &&
                          area->x2 == (screen_w - 1) &&
                          area->y2 == (screen_h - 1));
        bool force_clear_this_flush = disp_force_clear_next_flush;

        if (force_clear_this_flush || full_area) {
            memset(decodebuffer, EPD_LOGICAL_WHITE_BYTE, EPD_IMAGE_BUF_SIZE);
            Serial.printf("[LVGL flush] logical framebuffer WHITE cleared full=%d force=%d\n",
                          full_area, force_clear_this_flush);
            Serial.printf("[LVGL flush] WHITE clear byte=0x%02X\n", EPD_LOGICAL_WHITE_BYTE);
        }

        for(int32_t y = 0; y < h; y++) {
            int32_t dst_y = area->y1 + y;
            if(dst_y < 0 || dst_y >= screen_h) continue;

            for(int32_t x = 0; x < w; x++) {
                int32_t dst_x = area->x1 + x;
                if(dst_x < 0 || dst_x >= screen_w) continue;

                uint8_t gray4 = lv_color_to_epd_gray4(color_p[y * w + x]);
                epd_image_set_pixel_4bpp(decodebuffer, screen_w, dst_x, dst_y, gray4);
            }
        }
        if (framebuffer_mutex) {
            xSemaphoreGive(framebuffer_mutex);
        }

        disp_lvgl_flush_count++;
        // printf("[disp_flush] x1:%d, y1:%d, w:%d, h:%d\n", area->x1, area->y1, w, h);
    }
    if (!lv_disp_flush_is_last(disp)) {
        lv_disp_flush_ready(disp);
        return;
    }

    disp_last_flush_ms = millis();
    DisplayUpdateKind requested_kind = display_next_snapshot_kind;
    DisplayUpdateKind kind = requested_kind != DISPLAY_UPDATE_NONE ? requested_kind : DISPLAY_UPDATE_NORMAL_FRAME;
    bool worker_ready = disp_flush_task_started || (disp_flush_handle != NULL);

    if (!worker_ready && disp_flush_task_create_failed) {
        if (framebuffer_mutex && xSemaphoreTake(framebuffer_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            display_commit_frame(kind, decodebuffer);
            xSemaphoreGive(framebuffer_mutex);
            if (requested_kind != DISPLAY_UPDATE_NONE) {
                display_next_snapshot_kind = DISPLAY_UPDATE_NONE;
                if (display_reliable_pending_kind == requested_kind) {
                    display_reliable_pending_kind = DISPLAY_UPDATE_NONE;
                }
            }
            if (disp_force_clear_next_flush && display_cmd_is_reliable(kind)) {
                disp_force_clear_next_flush = false;
            }
        } else {
            Serial.println("[DISPLAY TASK ERROR] synchronous fallback lock timeout");
        }
        lv_disp_flush_ready(disp);
        return;
    }

    bool published = publish_snapshot(kind, true, area);
    if (published && requested_kind != DISPLAY_UPDATE_NONE) {
        if (worker_ready) {
            display_next_snapshot_kind = DISPLAY_UPDATE_NONE;
        }
        if (worker_ready && display_cmd_is_reliable(requested_kind) && disp_force_clear_next_flush) {
            disp_force_clear_next_flush = false;
            Serial.printf("[DISPLAY QUEUE] force-clear consumed by published reliable kind=%d\n", (int)requested_kind);
        }
        if (!worker_ready && disp_flush_task_create_failed &&
            display_cmd_is_reliable(requested_kind) &&
            (requested_kind == DISPLAY_UPDATE_BOOT_REPLACE)) {
            Serial.println("[DISPLAY TASK ERROR] create failed; using synchronous boot replacement");
            if (framebuffer_mutex && xSemaphoreTake(framebuffer_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                if (displaybuffer && decodebuffer) {
                    memcpy(displaybuffer, decodebuffer, EPD_IMAGE_BUF_SIZE);
                    display_commit_frame(requested_kind, displaybuffer);
                }
                xSemaphoreGive(framebuffer_mutex);
                display_next_snapshot_kind = DISPLAY_UPDATE_NONE;
                if (disp_force_clear_next_flush) {
                    disp_force_clear_next_flush = false;
                }
            } else {
                Serial.println("[DISPLAY TASK ERROR] synchronous fallback lock timeout");
            }
        }
    } else if (!published && requested_kind != DISPLAY_UPDATE_NONE) {
        Serial.printf("[DISPLAY QUEUE] reliable publish failed; retained kind=%d\n", (int)requested_kind);
        Serial.println("[DISPLAY QUEUE] pending kind retained after publish failure");
        lv_obj_t *act = lv_scr_act();
        if (act) {
            lv_obj_invalidate(act);
        }
        Serial.printf("[DISPLAY QUEUE] retry scheduled for pending kind=%d\n", (int)requested_kind);
    }
    /* Inform the graphics library that you are ready with the flushing */
    lv_disp_flush_ready(disp);
}

void disp_request_full_clear(void)
{
    disp_request_recovery_clean();
}

void disp_request_normal_frame(void)
{
    publish_snapshot(DISPLAY_UPDATE_NORMAL_FRAME, false, NULL);
}

void disp_request_screen_replace(void)
{
    disp_force_clear_next_flush = true;
    display_mark_reliable_pending(DISPLAY_UPDATE_SCREEN_REPLACE);
    display_set_next_snapshot_kind(DISPLAY_UPDATE_SCREEN_REPLACE);
    lv_obj_t *act = lv_scr_act();
    if (act) lv_obj_invalidate(act);
    Serial.println("[DISPLAY LIFECYCLE] screen replace requested + invalidate");
}

void disp_request_boot_replace(void)
{
    disp_force_clear_next_flush = true;
    display_mark_reliable_pending(DISPLAY_UPDATE_BOOT_REPLACE);
    display_set_next_snapshot_kind(DISPLAY_UPDATE_BOOT_REPLACE);
    lv_obj_t *act = lv_scr_act();
    if (act) lv_obj_invalidate(act);
    Serial.println("[DISPLAY LIFECYCLE] boot replace requested + invalidate");
}

void disp_request_recovery_clean(void)
{
    disp_force_clear_next_flush = true;
    display_mark_reliable_pending(DISPLAY_UPDATE_RECOVERY_CLEAN);
    display_set_next_snapshot_kind(DISPLAY_UPDATE_RECOVERY_CLEAN);
    lv_obj_t *act = lv_scr_act();
    if (act) lv_obj_invalidate(act);
    Serial.println("[DISPLAY LIFECYCLE] recovery clean requested + invalidate");
}

static const char *home_input_state_name(HomeInputState s)
{
    switch (s) {
        case HOME_INPUT_IDLE: return "IDLE";
        case HOME_INPUT_SUPPRESS_UNTIL_RELEASE: return "SUPPRESS_UNTIL_RELEASE";
        case HOME_INPUT_POST_RELEASE_DEADBAND: return "POST_RELEASE_DEADBAND";
        case HOME_INPUT_WAIT_FRESH_PRESS: return "WAIT_FRESH_PRESS";
        default: return "UNKNOWN";
    }
}


void touch_begin_home_transition_guard(uint32_t min_block_ms)
{
    uint32_t now = millis();
    HomeInputState prev = home_input_state;
    home_input_state = HOME_INPUT_SUPPRESS_UNTIL_RELEASE;
    home_input_state_start_ms = now;
    uint32_t guard_ms = (min_block_ms > 1000) ? min_block_ms : 1000;
    home_input_deadline_ms = now + guard_ms;
    Serial.printf("[HOME INPUT] %s -> %s reason=compat_guard_begin ms=%lu\n",
                  home_input_state_name(prev), home_input_state_name(home_input_state), (unsigned long)min_block_ms);
}

bool touch_home_transition_guard_active(void)
{
    return home_input_state != HOME_INPUT_IDLE;
}


bool touch_reject_stale_home_event(void)
{
    return home_input_state == HOME_INPUT_SUPPRESS_UNTIL_RELEASE ||
           home_input_state == HOME_INPUT_POST_RELEASE_DEADBAND ||
           home_input_state == HOME_INPUT_WAIT_FRESH_PRESS;
}

static void my_input_read(lv_indev_drv_t * drv, lv_indev_data_t*data)
{
    static int16_t x=0, y=0;

    (void)drv;

    uint32_t now = millis();
    bool raw_pressed = indev_touch_enabled && touch.isPressed();

    if (home_waiting_for_redraw_commit) {
        data->state = LV_INDEV_STATE_RELEASED;
        data->point.x = x;
        data->point.y = y;

        if (now >= home_redraw_deadline_ms) {
            Serial.println("[HOME REDRAW WARN] redraw commit timeout; releasing touch guard");
            home_waiting_for_redraw_commit = false;

            // Do not freeze forever. Continue through existing HomeInputState logic.
            // Keep existing Home suppression state active so stale touches are still guarded.
        } else {
            return;
        }
    }

    if (home_input_state != HOME_INPUT_IDLE && (now - home_input_state_start_ms) > 5000) {
        Serial.println("[HOME INPUT ERROR] state stuck; forcing IDLE");
        home_input_state = HOME_INPUT_IDLE;
    }

    if (home_input_state == HOME_INPUT_SUPPRESS_UNTIL_RELEASE) {
        data->state = LV_INDEV_STATE_RELEASED;
        data->point.x = x;
        data->point.y = y;

        if (!raw_pressed) {
            Serial.printf("[HOME INPUT] %s -> %s reason=release\n",
                          home_input_state_name(HOME_INPUT_SUPPRESS_UNTIL_RELEASE),
                          home_input_state_name(HOME_INPUT_POST_RELEASE_DEADBAND));
            home_input_state = HOME_INPUT_POST_RELEASE_DEADBAND;
            home_input_state_start_ms = now;
            home_input_deadline_ms = now + 250;
            Serial.println("[HOME INPUT] release observed; entering deadband");
        } else if (now >= home_input_deadline_ms) {
            Serial.printf("[HOME INPUT] %s -> %s reason=timeout\n",
                          home_input_state_name(HOME_INPUT_SUPPRESS_UNTIL_RELEASE),
                          home_input_state_name(HOME_INPUT_POST_RELEASE_DEADBAND));
            home_input_state = HOME_INPUT_POST_RELEASE_DEADBAND;
            home_input_state_start_ms = now;
            home_input_deadline_ms = now + 250;
            Serial.println("[HOME INPUT WARN] release wait timed out; entering deadband");
        }

        return;
    }

    if (home_input_state == HOME_INPUT_POST_RELEASE_DEADBAND) {
        data->state = LV_INDEV_STATE_RELEASED;
        data->point.x = x;
        data->point.y = y;

        if (now >= home_input_deadline_ms) {
            Serial.printf("[HOME INPUT] %s -> %s\n",
                          home_input_state_name(HOME_INPUT_POST_RELEASE_DEADBAND),
                          home_input_state_name(HOME_INPUT_WAIT_FRESH_PRESS));
            home_input_state = HOME_INPUT_WAIT_FRESH_PRESS;
            home_input_state_start_ms = now;
            home_input_deadline_ms = now + 3000;
            Serial.println("[HOME INPUT] deadband ended; waiting for fresh press");
        }
        return;
    }

    if (home_input_state == HOME_INPUT_WAIT_FRESH_PRESS) {
        if (raw_pressed) {
            Serial.printf("[HOME INPUT] %s -> %s reason=fresh_press\n",
                          home_input_state_name(HOME_INPUT_WAIT_FRESH_PRESS),
                          home_input_state_name(HOME_INPUT_IDLE));
            home_input_state = HOME_INPUT_IDLE;
            Serial.println("[HOME INPUT] fresh press accepted");
        } else if (now >= home_input_deadline_ms) {
            Serial.printf("[HOME INPUT] %s -> %s reason=timeout\n",
                          home_input_state_name(HOME_INPUT_WAIT_FRESH_PRESS),
                          home_input_state_name(HOME_INPUT_IDLE));
            home_input_state = HOME_INPUT_IDLE;
            Serial.println("[HOME INPUT] fresh-press wait expired; returning idle");
        }
    }

    bool pressed = raw_pressed;
    if(pressed) {
        data->state = LV_INDEV_STATE_PRESSED;
        // Keep PRESSED state even if one coordinate sample is missed.
        // Update coordinates whenever a new point is available.
        touch.getPoint(&x, &y, 1);
    }
    else{
        data->state = LV_INDEV_STATE_RELEASED;
    }
    data->point.x = x;
    data->point.y = y;
}

static void lv_port_disp_init(void)
{
    lv_init();

    static lv_disp_draw_buf_t draw_buf;

    lv_color_t *lv_disp_buf_1 = (lv_color_t *)ps_calloc(sizeof(lv_color_t), DISP_BUF_SIZE);
    lv_color_t *lv_disp_buf_2 = (lv_color_t *)ps_calloc(sizeof(lv_color_t), DISP_BUF_SIZE);
    decodebuffer = (uint8_t *)ps_calloc(sizeof(uint8_t), EPD_IMAGE_BUF_SIZE);
    displaybuffer = (uint8_t *)ps_calloc(sizeof(uint8_t), EPD_IMAGE_BUF_SIZE);
    framebuffer_mutex = xSemaphoreCreateMutex();
    physical_display_mutex = xSemaphoreCreateMutex();
    display_q = xQueueCreate(8, sizeof(DisplayCmd));
    display_snapshot_mutex = xSemaphoreCreateMutex();
    bool snapshot_pool_ok = true;
    for (uint8_t i = 0; i < DISPLAY_SNAPSHOT_COUNT; ++i) {
        display_snapshot_pool[i] = (uint8_t *)heap_caps_malloc(EPD_IMAGE_BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!display_snapshot_pool[i]) {
            snapshot_pool_ok = false;
        }
    }
    if (!lv_disp_buf_1 || !lv_disp_buf_2 || !decodebuffer || !displaybuffer || !framebuffer_mutex || !physical_display_mutex) {
        Serial.println("[DISPLAY LIFECYCLE] FATAL: display buffers/mutex allocation failed; LVGL display not registered");
        return;
    }
    if (!display_q || !display_snapshot_mutex || !snapshot_pool_ok) {
        Serial.println("[DISPLAY LIFECYCLE] FATAL: display queue/snapshot allocation failed");
        return;
    }
    ensure_display_flush_task_started();
    if (decodebuffer) {
        memset(decodebuffer, EPD_LOGICAL_WHITE_BYTE, EPD_IMAGE_BUF_SIZE);
    }
    if (displaybuffer) {
        memset(displaybuffer, EPD_LOGICAL_WHITE_BYTE, EPD_IMAGE_BUF_SIZE);
    }
    lv_disp_draw_buf_init(&draw_buf, lv_disp_buf_1, lv_disp_buf_2, DISP_BUF_SIZE);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = epd_rotated_display_width();
    disp_drv.ver_res = epd_rotated_display_height();
    disp_drv.flush_cb = disp_flush;
    // disp_drv.render_start_cb = dips_render_start_cb;
    disp_drv.draw_buf = &draw_buf;
    disp_drv.full_refresh = 1;
    lv_disp_drv_register(&disp_drv);
    disp_request_boot_replace();

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);      /*Basic initialization*/
    indev_drv.type = LV_INDEV_TYPE_POINTER;                 /*See below.*/
    indev_drv.read_cb = my_input_read;              /*See below.*/
    /*Register the driver in LVGL and save the created input device object*/
    // static lv_indev_t * my_indev = lv_indev_drv_register(&indev_drv);
    touch_indev = lv_indev_drv_register(&indev_drv);
}

static void ensure_display_flush_task_started(void)
{
    if (disp_flush_handle != NULL || disp_flush_task_started || !display_q || !display_snapshot_mutex) {
        return;
    }

    size_t free_before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t largest_before = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    size_t free_psram_before = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    Serial.printf("[DISPLAY TASK] create precheck free_internal=%u largest_internal=%u free_psram=%u\n",
                  (unsigned)free_before, (unsigned)largest_before, (unsigned)free_psram_before);

    BaseType_t rc = pdFAIL;
    disp_flush_handle = xTaskCreateStaticPinnedToCore(disp_flush_task, "disp_flush_task",
                                                       DISP_FLUSH_STACK_BYTES / sizeof(StackType_t),
                                                       NULL, 2, disp_flush_stack, &disp_flush_task_tcb, 0);
    if (disp_flush_handle != NULL) {
        rc = pdPASS;
        Serial.printf("[DISPLAY TASK] static create ok stack_bytes=%u\n", (unsigned)DISP_FLUSH_STACK_BYTES);
    }

    size_t free_after = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t largest_after = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    Serial.printf("[DISPLAY TASK] create postcheck free_internal=%u largest_internal=%u\n",
                  (unsigned)free_after, (unsigned)largest_after);

    if (rc != pdPASS || disp_flush_handle == NULL) {
        disp_flush_task_create_failed = true;
        Serial.printf("[DISPLAY TASK ERROR] create failed rc=%ld handle=%p free_internal=%u largest_internal=%u\n",
                      (long)rc, (void *)disp_flush_handle, (unsigned)free_after, (unsigned)largest_after);
    }
}

static bool touch_gt911_init(void)
{
    // Touch --- 0x5D
    touch.setPins(BOARD_TOUCH_RST, BOARD_TOUCH_INT);
    if (!touch.begin(Wire, GT911_SLAVE_ADDRESS_L, BOARD_SDA, BOARD_SCL))
    {
        // while (1) {
            Serial.println("Failed to find GT911 - check your wiring!");
        //     delay(1000);
        // }
    }
    Serial.println("Init GT911 Sensor success!");

    // Set the center button to trigger the callback , Only for specific devices, e.g LilyGo-EPD47 S3 GT911
    touch.setHomeButtonCallback([](void *user_data) {
        uint32_t now = millis();

        if (now - home_button_last_ms < 700) {
            Serial.println("[HOME] ignored duplicate home callback");
            return;
        }

        home_button_last_ms = now;

        Serial.println("[HOME] callback: schedule springboard");
        bool posted = ui_post_event(UiEvent::HOME_SWITCH_TO_SPRINGBOARD);
        if (posted) {
            HomeInputState prev = home_input_state;
            home_input_state = HOME_INPUT_SUPPRESS_UNTIL_RELEASE;
            home_input_state_start_ms = now;
            home_input_deadline_ms = now + 1500;
            Serial.printf("[HOME INPUT] %s -> %s reason=callback\n",
                          home_input_state_name(prev), home_input_state_name(home_input_state));
        } else {
            Serial.println("[HOME] callback: queue full, event dropped");
        }
    }, NULL);

    touch.setInterruptMode(LOW_LEVEL_QUERY);
    return true;
}

static bool rtc_pcf8563_init(void)
{
    pinMode(BOARD_RTC_IRQ, INPUT_PULLUP);

    if (!rtc.begin(Wire, PCF8563_SLAVE_ADDRESS, BOARD_RTC_SDA, BOARD_RTC_SCL)) {
        Serial.println("Failed to find PCF8563 - check your wiring!");
        // while (1) {
        //     delay(1000);
        // }
        return false;
    }

    return true;
}

static void disp_init_status(const char *name, int *x, int *y, bool init_st)
{
    char buf[32] = {0};
    EpdFontProperties font_props = epd_font_properties_default();
    font_props.flags = EPD_DRAW_ALIGN_LEFT;

    const EpdFont* font;
    font = &FiraSans_12;

    snprintf(buf, 32, "[%s] %s", (init_st == true? "✔":"✖"), name);

    epd_write_string( font, buf, x, y, epd_hl_get_framebuffer(&hl), &font_props);
    epd_poweron();
    checkError(epd_hl_update_screen(&hl, MODE_GL16, epd_ambient_temperature()));
    epd_poweroff();
}


static bool screen_init(void)
{
    epd_init(&DEMO_BOARD, &ED047TC1, EPD_LUT_64K);
    // Set VCOM for boards that allow to set this in software (in mV).
    // This will print an error if unsupported. In this case,
    // set VCOM using the hardware potentiometer and delete this line.
    epd_set_vcom(ui_setting_get_vcom());
    // epd_set_vcom(ui_setting_get_vcom()); // TPS651851 VCOM output range 0-5.1v  step:10mV

    hl = epd_hl_init(WAVEFORM);

    // Default orientation is EPD_ROT_LANDSCAPE
    epd_set_rotation(EPD_ROT_INVERTED_PORTRAIT);

    printf(
        "Dimensions after rotation, width: %d height: %d\n\n", epd_rotated_display_width(),
        epd_rotated_display_height()
    );

    // The display bus settings for V7 may be conservative, you can manually
    // override the bus speed to tune for speed, i.e., if you set the PSRAM speed
    // to 120MHz.
    epd_set_lcd_pixel_clock_MHz(17);
    Serial.println("[EPD INIT] pixel clock set before boot clear");

    heap_caps_print_heap_info(MALLOC_CAP_INTERNAL);
    heap_caps_print_heap_info(MALLOC_CAP_SPIRAM);

    bool boot_can_hard_clean = display_safe_for_hard_clean_boot();
    if (peri_buf[E_PERI_BQ25896]) {
        Serial.printf("[EPD BOOT CLEAN] bq_init=1 vbus=%d vbat=%.3f vsys=%.3f safe=%d\n",
                      battery_25896_is_vbus_in(),
                      battery_25896_get_VBAT(),
                      battery_25896_get_VSYS(),
                      boot_can_hard_clean);
    } else {
        Serial.printf("[EPD BOOT CLEAN] bq_init=0 provisional_vbus=%d safe=%d\n",
                      display_have_vbus_provisional(),
                      boot_can_hard_clean);
    }

    if (boot_can_hard_clean) {
        epd_poweron();
        epd_clear();
        epd_poweroff();
        Serial.println("[EPD INIT] boot epd_clear complete");
    } else {
        Serial.println("[EPD POWER] boot epd_clear downgraded/skipped due to power-safety decision");
    }

    int cursor_x = 250;
    int cursor_y = epd_rotated_display_height() / 2 - 250;

    EpdFontProperties font_props = epd_font_properties_default();
    font_props.flags = EPD_DRAW_ALIGN_CENTER;

    const EpdFont* font;
    font = &FiraSans_20;

    epd_write_string(
        font, "Initialize    System", &cursor_x, &cursor_y, epd_hl_get_framebuffer(&hl), &font_props
    );
    epd_poweron();
    epd_hl_update_screen(&hl, MODE_GL16, epd_ambient_temperature());
    epd_poweroff();

    printf("current temperature: %.2f\n", epd_ambient_temperature());

    bool run_selftest = EPD_SELFTEST_ON_BOOT;
    if (digitalRead(BOARD_BOOT_BTN) == LOW) {
        run_selftest = true;
        Serial.println("[EPD SELFTEST] BOOT held low; running self-test");
    }
    if (run_selftest) {
        epd_low_level_self_test();
        delay(5000);
    }

    return true;
}

static bool bq25896_init(void)
{
    bool result =  PPM.init(Wire, BOARD_SDA, BOARD_SCL, BQ25896_SLAVE_ADDRESS);
    if (result == false) {
        // while (1) {
        //     Serial.println("PPM is not online...");
        //     delay(1000);
        // }
        return false;
    }
    
    // Set the minimum operating voltage. Below this voltage, the PPM will protect
    PPM.setSysPowerDownVoltage(3300);

    // To obtain voltage data, the ADC must be enabled first
    PPM.enableMeasure();

    PPM.disableOTG();

    bool vbus_present = battery_25896_is_vbus_in();
    Serial.printf("[PMIC INIT] bq25896 vbus_detect=%d vbus=%.3f vsys=%.3f vbat=%.3f\n",
                  vbus_present,
                  battery_25896_get_VBUS(),
                  battery_25896_get_VSYS(),
                  battery_25896_get_VBAT());

    if (vbus_present) {
        // Configure aggressive input/charge policy only when VBUS is actually present.
        PPM.setInputCurrentLimit(3250);
        Serial.printf("getInputCurrentLimit: %d mA\n", PPM.getInputCurrentLimit());
        PPM.disableCurrentLimitPin();
        PPM.setChargeTargetVoltage(4208);
        PPM.setPrechargeCurr(64);
        PPM.setChargerConstantCurr(1024);
        Serial.printf("getChargerConstantCurr: %d mA\n", PPM.getChargerConstantCurr());
        PPM.enableCharge();
        Serial.println("[PMIC] VBUS present: charging enabled");
    } else {
        // Conservative battery-only boot: keep charger path disabled and avoid
        // forcing high-input/high-charge startup policy.
        PPM.setInputCurrentLimit(500);
        PPM.disableCharge();
        Serial.println("[PMIC] battery-only: charging disabled; conservative startup policy");
    }

    // pinMode(OTG_ENABLE_PIN, OUTPUT);
    // digitalWrite(OTG_ENABLE_PIN, HIGH);

    return result;
}

static bool bq27220_init(void)
{
    return bq27220.init();
}

static bool display_have_vbus(void)
{
    return peri_buf[E_PERI_BQ25896] && battery_25896_is_vbus_in();
}

static bool display_have_vbus_provisional(void)
{
    return battery_25896_is_vbus_in();
}

#ifndef CONFIG_EPD_HARD_CLEAN_MIN_VBAT
#define CONFIG_EPD_HARD_CLEAN_MIN_VBAT 0.0f
#endif

static bool display_safe_for_hard_clean_boot(void)
{
    if (peri_buf[E_PERI_BQ25896]) {
        return display_safe_for_hard_clean();
    }
    return display_have_vbus_provisional();
}

static bool display_safe_for_hard_clean(void)
{
    if (!peri_buf[E_PERI_BQ25896]) return false;
    if (display_have_vbus()) return true;
    return battery_25896_get_VBAT() >= CONFIG_EPD_HARD_CLEAN_MIN_VBAT;
}

static bool sd_card_init(void)
{
    if(!SD.begin(BOARD_SD_CS)){
        Serial.println("Card Mount Failed");
        return false;
    }

    uint8_t cardType = SD.cardType();

    if(cardType == CARD_NONE){
        Serial.println("No SD card attached");
        return false;
    }

    Serial.print("SD Card Type: ");
    if(cardType == CARD_MMC){
        Serial.println("MMC");
    } else if(cardType == CARD_SD){
        Serial.println("SDSC");
    } else if(cardType == CARD_SDHC){
        Serial.println("SDHC");
    } else {
        Serial.println("UNKNOWN");
    }
    return true;
}

void idf_setup() 
{
    gpio_hold_dis((gpio_num_t)BOARD_TOUCH_RST);
    gpio_hold_dis((gpio_num_t)BOARD_LORA_RST);
    gpio_deep_sleep_hold_dis();

    // lora and sd use the same spi, in order to avoid mutual influence;
    // before powering on, all CS signals should be pulled high and in an unselected state;
    pinMode(BOARD_LORA_CS, OUTPUT);
    digitalWrite(BOARD_LORA_CS, HIGH);
    pinMode(BOARD_SD_CS, OUTPUT);
    digitalWrite(BOARD_SD_CS, HIGH);

    // Set the interrupt input to input pull-up
    if (BOARD_PCA9535_INT > 0) {
        pinMode(BOARD_PCA9535_INT, INPUT_PULLUP);
    }

    Serial.begin(115200);
    esp_reset_reason_t rr = esp_reset_reason();
    Serial.printf("[BOOT] reset_reason=%d wakeup_cause=%d\n",
                  rr,
                  esp_sleep_get_wakeup_cause());
    SerialGPS.begin(38400, SERIAL_8N1, BOARD_GPS_RXD, BOARD_GPS_TXD);
    // // while (!Serial);

    SPI.begin(BOARD_SPI_SCLK, BOARD_SPI_MISO, BOARD_SPI_MOSI);
    Wire.begin(BOARD_SDA, BOARD_SCL);

    pinMode(BOARD_BL_EN, OUTPUT);
    analogWrite(BOARD_BL_EN, 0); // Keep backlight off until user setting is applied
    pinMode(BOARD_BOOT_BTN, INPUT_PULLUP);
#if defined(BOARD_IO48_BTN) && (BOARD_IO48_BTN >= 0)
    pinMode(BOARD_IO48_BTN, INPUT_PULLUP);
#endif

    // Init system
    ui_nvs_set_defaulat_param();

    WiFi.persistent(false);
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_OFF);
    delay(100);

    peri_buf[E_PERI_BQ27220]    = bq27220_init();   // PMU --- 0x55
    peri_buf[E_PERI_BQ25896]    = bq25896_init();   // PMU --- 0x6B
    Serial.printf("[BOOT] bq25896 init before screen_init: %d\n", peri_buf[E_PERI_BQ25896]);

    Serial.println("[BOOT] before screen_init()");
    screen_init();
    io_extend_lora_gps_power_on(true);

    int cursor_x = 100;
    int cursor_y = epd_rotated_display_height() / 2 - 100 - 50;
    uint8_t io_val0 = pca9555_read_input(BOARD_I2C_PORT, 0);
    uint8_t io_val1 = pca9555_read_input(BOARD_I2C_PORT, 1);
    bool io_ret = false;
    lv_snprintf(global_buf, GLOBAL_BUF_LEN, "io_extend: 0x%02x, 0x%02x", io_val0, io_val1);
    if(((io_val0 & 0x01) && (io_val1 & 0x04))) io_ret = true;
    disp_init_status(global_buf, &cursor_x, &cursor_y, io_ret);

    cursor_x = 100;
    cursor_y = epd_rotated_display_height() / 2 - 100 - 0;
    disp_init_status("BQ27220 Init ...", &cursor_x, &cursor_y, peri_buf[E_PERI_BQ27220]);

    peri_buf[E_PERI_INK_POWER]  = false; 

    if (peri_buf[E_PERI_BQ25896]) {
        Serial.printf("[BOOT PWR] vbus_in=%d vbus=%.3f vsys=%.3f vbat=%.3f charging=%d\n",
                      battery_25896_is_vbus_in(),
                      battery_25896_get_VBUS(),
                      battery_25896_get_VSYS(),
                      battery_25896_get_VBAT(),
                      battery_25896_is_chr());
    }
    cursor_x = 100;
    cursor_y = epd_rotated_display_height() / 2 - 100 + 50;
    disp_init_status("BQ25896 Init ...", &cursor_x, &cursor_y, peri_buf[E_PERI_BQ25896]);

    peri_buf[E_PERI_RTC]        = rtc_pcf8563_init(); // RTC --- 0x51
    cursor_x = 100;
    cursor_y = epd_rotated_display_height() / 2 - 100 + 100;
    disp_init_status("RTC (PCF8563) Init ...", &cursor_x, &cursor_y, peri_buf[E_PERI_RTC]);

    ui_event_q = xQueueCreate(16, sizeof(UiEvent));
    peri_buf[E_PERI_TOUCH]      = touch_gt911_init();  // Touch --- 0x5D;
    cursor_x = 100;
    cursor_y = epd_rotated_display_height() / 2 - 100 + 150;
    disp_init_status("Touch (GT911) Init ...", &cursor_x, &cursor_y, peri_buf[E_PERI_TOUCH]);

    peri_buf[E_PERI_LORA]       = lora_sx1262_init();
    cursor_x = 100;
    cursor_y = epd_rotated_display_height() / 2 - 100 + 200;
    disp_init_status("LoRa (SX1262) Init ...", &cursor_x, &cursor_y, peri_buf[E_PERI_LORA]);

    peri_buf[E_PERI_SD_CARD]    = sd_card_init();
    sd_guard_init();
    cursor_x = 100;
    cursor_y = epd_rotated_display_height() / 2 - 100 + 250;
    disp_init_status("SD Card Init ...", &cursor_x, &cursor_y, peri_buf[E_PERI_SD_CARD]);

    printf("LVGL Init\n");
    lv_port_disp_init();
    Serial.println("[BOOT] after lv_port_disp_init()");

    printf("LVGL UI Entry\n");
    ui_task_handle = xTaskGetCurrentTaskHandle();
    ui_entry();
    Serial.printf("[EPD SAFE] screen root bg=0x%06X\n", EPD_COLOR_BG);
    Serial.println("[BOOT] after ui_entry()");

    peri_buf[E_PERI_GPS]        = gps_init();
    cursor_x = 100;
    cursor_y = epd_rotated_display_height() / 2 - 100 +300;
    disp_init_status("GPS Init ...", &cursor_x, &cursor_y, peri_buf[E_PERI_GPS]);

    // task
    xTaskCreate(btn_task, "lora_task", 1024 * 3, NULL, INFARED_PRIORITY, &btn_handle);
}

bool ui_is_ui_thread()
{
    return ui_task_handle != NULL && xTaskGetCurrentTaskHandle() == ui_task_handle;
}

void idf_loop() 
{
    ui_task_handle = xTaskGetCurrentTaskHandle();
    bool skip_lv_task_handler = false;
    UiEvent event;
    while (ui_event_q && xQueueReceive(ui_event_q, &event, 0) == pdTRUE) {
        switch (event) {
            case UiEvent::BOOT_SLEEP:
                scr_mgr_switch(SCREEN0_ID, false);
                ui_sleep();
                skip_lv_task_handler = true;
                break;
            case UiEvent::TOGGLE_BACKLIGHT: {
                int bl = 0;
                ui_setting_get_backlight(&bl);
                ui_setting_set_backlight(bl == 0 ? 1 : 0);
                break;
            }
            case UiEvent::HOME_SWITCH_TO_SPRINGBOARD: {
                if (home_nav_in_progress) {
                    break;
                }
                home_nav_in_progress = true;
                Serial.println("[HOME] idf_loop: switching to springboard");
                if (touch_indev) {
                    lv_indev_reset(touch_indev, NULL);
                }
                scr_mgr_switch(SCREEN0_ID, false);
                if (touch_indev) {
                    lv_indev_reset(touch_indev, NULL);
                }

                home_waiting_for_redraw_commit = true;
                home_redraw_deadline_ms = millis() + HOME_REDRAW_COMMIT_TIMEOUT_MS;

                lv_obj_t *act = lv_scr_act();
                if (act) {
                    lv_obj_invalidate(act);
                    Serial.println("[HOME REDRAW] springboard invalidated");
                }

#if defined(LV_VERSION_CHECK)
#if LV_VERSION_CHECK(8, 0, 0)
                lv_refr_now(NULL);
#else
                bool prev_touch_enabled = indev_touch_enabled;
                indev_touch_enabled = false;
                lv_task_handler();
                indev_touch_enabled = prev_touch_enabled;
#endif
#else
                bool prev_touch_enabled = indev_touch_enabled;
                indev_touch_enabled = false;
                lv_task_handler();
                indev_touch_enabled = prev_touch_enabled;
#endif
                Serial.println("[HOME REDRAW] forced LVGL refresh requested");

                uint32_t now = millis();
                HomeInputState prev = home_input_state;
                home_input_state = HOME_INPUT_SUPPRESS_UNTIL_RELEASE;
                home_input_state_start_ms = now;
                home_input_deadline_ms = now + 1500;
                Serial.printf("[HOME INPUT] %s -> %s reason=idf_loop_switch\n",
                              home_input_state_name(prev), home_input_state_name(home_input_state));
                home_nav_in_progress = false;
                Serial.println("[HOME] idf_loop: switch complete, skipped one LVGL handler");
                skip_lv_task_handler = true;
                break;
            }
        }
    }
    if (!skip_lv_task_handler) {
        lv_task_handler();
    } else {
        Serial.println("[UI EVENT] skipped lv_task_handler after queued UI transition");
    }

    gps_service_loop();
    ui_wifi_service_loop();
    delay(1);
}
static bool publish_snapshot(DisplayUpdateKind kind, bool has_dirty_union, const lv_area_t *dirty_union)
{
    if (shutdown_in_progress) {
        Serial.printf("[DISPLAY QUEUE] suppress publish during shutdown kind=%d\n", (int)kind);
        return false;
    }
    if (!(framebuffer_mutex && decodebuffer && display_q && display_snapshot_mutex)) {
        return false;
    }
    for (uint8_t i = 0; i < DISPLAY_SNAPSHOT_COUNT; ++i) {
        if (!display_snapshot_pool[i]) {
            return false;
        }
    }

    if (xSemaphoreTake(display_snapshot_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        Serial.println("[DISPLAY QUEUE ERROR] snapshot mutex timeout");
        return false;
    }
    int8_t slot = -1;
    for (uint8_t i = 0; i < DISPLAY_SNAPSHOT_COUNT; ++i) {
        if (display_snapshot_owner[i] == -1) {
            display_snapshot_owner[i] = 0;
            slot = (int8_t)i;
            break;
        }
    }
    if (slot < 0) {
        xSemaphoreGive(display_snapshot_mutex);
        Serial.println("[DISPLAY QUEUE ERROR] snapshot pool exhausted");
        return false;
    }
    xSemaphoreGive(display_snapshot_mutex);

    if (xSemaphoreTake(framebuffer_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        release_snapshot(0, display_snapshot_pool[(uint8_t)slot]);
        return false;
    }
    memcpy(display_snapshot_pool[(uint8_t)slot], decodebuffer, EPD_IMAGE_BUF_SIZE);
    xSemaphoreGive(framebuffer_mutex);

    DisplayCmd cmd = {
        .seq = ++display_seq_counter,
        .kind = kind,
        .snapshot = display_snapshot_pool[(uint8_t)slot],
        .has_dirty_union = has_dirty_union,
    };
    if (has_dirty_union && dirty_union) {
        cmd.dirty_union = *dirty_union;
    } else {
        cmd.dirty_union = {.x1 = 0, .y1 = 0, .x2 = 0, .y2 = 0};
    }

    if (xSemaphoreTake(display_snapshot_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        Serial.println("[DISPLAY QUEUE ERROR] snapshot mutex timeout");
        release_snapshot(cmd.seq, cmd.snapshot);
        return false;
    }
    display_snapshot_owner[(uint8_t)slot] = 1;
    xSemaphoreGive(display_snapshot_mutex);
    if (xQueueSend(display_q, &cmd, 0) != pdTRUE) {
        release_snapshot(cmd.seq, cmd.snapshot);
        Serial.println("[DISPLAY LIFECYCLE] display queue full; command dropped");
        return false;
    }
    Serial.printf("[DISPLAY QUEUE] publish seq=%lu kind=%d\n", (unsigned long)cmd.seq, (int)cmd.kind);
    return true;
}

static bool display_internal_heap_ok_for_hard_clean()
{
    size_t free_i = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t largest_i = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    return free_i >= 8192 && largest_i >= 1024;
}

static bool display_internal_heap_critical()
{
    size_t free_i = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t largest_i = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    return free_i < 4096 || largest_i < 256;
}

static void display_log_power(const char *phase, DisplayUpdateKind kind)
{
    if (display_internal_heap_critical()) {
        Serial.printf("[EPD POWER] %s kind=%d power_log_skipped low_internal_heap free=%u largest=%u\n",
                      phase,
                      (int)kind,
                      (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        return;
    }

    if (!peri_buf[E_PERI_BQ25896]) {
        Serial.printf("[EPD POWER] %s kind=%d bq25896_unavailable\n", phase, (int)kind);
        return;
    }

    Serial.printf("[EPD POWER] %s kind=%d usb=%d vbus=%.3f vsys=%.3f vbat=%.3f charging=%d\n",
                  phase,
                  (int)kind,
                  battery_25896_is_vbus_in(),
                  battery_25896_get_VBUS(),
                  battery_25896_get_VSYS(),
                  battery_25896_get_VBAT(),
                  battery_25896_is_chr());
}

static bool display_safe_for_recovery_clean()
{
    if (!peri_buf[E_PERI_BQ25896]) return true;

    if (!display_safe_for_hard_clean()) {
        Serial.printf("[EPD POWER] hard clean unsafe: vbat=%.3f threshold=%.3f\n",
                      battery_25896_get_VBAT(),
                      (float)CONFIG_EPD_HARD_CLEAN_MIN_VBAT);
        return false;
    }

    bool usb = battery_25896_is_vbus_in();
    float vsys = battery_25896_get_VSYS();
    float vbat = battery_25896_get_VBAT();

    if (usb) return true;

    if (vsys > 0.0f && vsys < 3.55f) {
        Serial.printf("[EPD POWER] recovery clean unsafe: vsys=%.3f vbat=%.3f\n", vsys, vbat);
        return false;
    }

    return true;
}

static bool display_commit_frame(DisplayUpdateKind kind, const uint8_t *framebuffer4bpp)
{
    struct PhysicalDisplayCommitGuard {
        bool locked = false;
        explicit PhysicalDisplayCommitGuard(SemaphoreHandle_t mtx)
        {
            if (mtx && xSemaphoreTake(mtx, pdMS_TO_TICKS(30000)) == pdTRUE) {
                locked = true;
                display_physical_commit_active = true;
            }
        }
        ~PhysicalDisplayCommitGuard()
        {
            if (locked) {
                display_physical_commit_active = false;
                xSemaphoreGive(physical_display_mutex);
            }
        }
    };

    if (kind == DISPLAY_UPDATE_NONE) {
        Serial.println("[DISPLAY LIFECYCLE] no pending update; skipping physical commit");
        return false;
    }
    if (!framebuffer4bpp) {
        Serial.println("[DISPLAY LIFECYCLE] null framebuffer; skipping physical commit");
        return false;
    }
    if (shutdown_in_progress && kind != DISPLAY_UPDATE_SHUTDOWN_IMAGE) {
        Serial.printf("[DISPLAY LIFECYCLE] suppress physical commit during shutdown kind=%d\n", (int)kind);
        return false;
    }
    if (!physical_display_mutex) {
        Serial.println("[DISPLAY LOCK] physical display mutex unavailable");
        return false;
    }

    PhysicalDisplayCommitGuard guard(physical_display_mutex);
    if (!guard.locked) {
        Serial.println("[DISPLAY LOCK] physical display lock timeout");
        return false;
    }
    Serial.printf("[DISPLAY LOCK] acquired kind=%d\n", (int)kind);
    EpdRect full_area = {.x = 0, .y = 0, .width = epd_rotated_display_width(), .height = epd_rotated_display_height()};
    disp_physical_commit_count++;
    Serial.printf("[DISPLAY LIFECYCLE] commit=%lu kind=%d lvgl_flushes=%lu\n",
                  (unsigned long)disp_physical_commit_count, (int)kind, (unsigned long)disp_lvgl_flush_count);
    if (ui_refresh_get_mode() == UI_REFRESH_MODE_FAST) {
        Serial.println("[DISPLAY LIFECYCLE] FAST/DU disabled; using GL16 safe mode");
    }

    bool is_replacement_commit = (kind == DISPLAY_UPDATE_RECOVERY_CLEAN || kind == DISPLAY_UPDATE_SCREEN_REPLACE || kind == DISPLAY_UPDATE_BOOT_REPLACE);
    bool do_hard_clean = is_replacement_commit;
    bool safe_for_hard_clean = true;
    if (do_hard_clean && !display_internal_heap_ok_for_hard_clean()) {
        Serial.printf("[EPD POWER] hard clean skipped low internal heap free=%u largest=%u\n",
                      (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        do_hard_clean = false;
    }
    if (do_hard_clean && is_replacement_commit) {
        safe_for_hard_clean = display_safe_for_recovery_clean();
    }
    if (kind == DISPLAY_UPDATE_RECOVERY_CLEAN && !safe_for_hard_clean) {
        Serial.println("[EPD POWER] recovery clean downgraded to single GL16 replacement");
        do_hard_clean = false;
    }
    if (kind == DISPLAY_UPDATE_SCREEN_REPLACE && !safe_for_hard_clean) {
        Serial.println("[EPD POWER] screen replace hard clean downgraded to single GL16 replacement");
        do_hard_clean = false;
    }
    if (kind == DISPLAY_UPDATE_BOOT_REPLACE) {
        do_hard_clean = do_hard_clean && safe_for_hard_clean;
        Serial.printf("[EPD POWER] boot replace hard clean decision safe=%d\n", safe_for_hard_clean ? 1 : 0);
        if (!safe_for_hard_clean) {
            Serial.println("[EPD POWER] boot replace hard clean downgraded to single GL16 replacement");
        }
    }
    if (kind == DISPLAY_UPDATE_SHUTDOWN_IMAGE) {
        do_hard_clean = false;
        Serial.println("[EPD POWER] shutdown image forcing single GL16 replacement (no hard clean)");
    }

    if (do_hard_clean) {
        disp_replace_commit_count++;
        Serial.println("[DISPLAY LIFECYCLE] physical white erase begin");
        epd_hl_set_all_white(&hl);
        epd_poweron();
        display_log_power("before_update", kind);
        uint32_t t0_gc16 = millis();
        EpdDrawError gc16_err = epd_hl_update_screen(&hl, MODE_GC16, epd_ambient_temperature());
        checkError(gc16_err);
        uint32_t dt_gc16 = millis() - t0_gc16;
        Serial.printf("[EPD POWER] update complete kind=%d mode=GC16 duration_ms=%lu\n",
                      (int)kind, (unsigned long)dt_gc16);
        display_log_power("after_update", kind);
        epd_poweroff();
        Serial.println("[DISPLAY LIFECYCLE] physical white erase complete");
        Serial.println("[DISPLAY LIFECYCLE] replacement GL16 frame begin (post-clean)");
        epd_hl_set_all_white(&hl);
        epd_draw_rotated_image(full_area, framebuffer4bpp, epd_hl_get_framebuffer(&hl));
        epd_poweron();
        vTaskDelay(pdMS_TO_TICKS(50));
        display_log_power("before_update", kind);
        uint32_t t0_gl16 = millis();
        EpdDrawError gl16_err = epd_hl_update_screen(&hl, MODE_GL16, epd_ambient_temperature());
        checkError(gl16_err);
        uint32_t dt_gl16 = millis() - t0_gl16;
        Serial.printf("[EPD POWER] update complete kind=%d mode=GL16 duration_ms=%lu\n",
                      (int)kind, (unsigned long)dt_gl16);
        display_log_power("after_update", kind);
        vTaskDelay(pdMS_TO_TICKS(20));
        epd_poweroff();
        Serial.println("[DISPLAY LIFECYCLE] replacement GL16 frame complete (post-clean)");
        if (home_waiting_for_redraw_commit &&
            (kind == DISPLAY_UPDATE_SCREEN_REPLACE || kind == DISPLAY_UPDATE_RECOVERY_CLEAN || kind == DISPLAY_UPDATE_BOOT_REPLACE || kind == DISPLAY_UPDATE_SHUTDOWN_IMAGE)) {
            home_waiting_for_redraw_commit = false;
            Serial.println("[HOME REDRAW] guard released after physical commit (post-clean)");
        }
        bool ok = (gc16_err == EPD_DRAW_SUCCESS && gl16_err == EPD_DRAW_SUCCESS);
        Serial.printf("[DISPLAY LOCK] releasing kind=%d ok=%d\n", (int)kind, ok ? 1 : 0);
        return ok;
    }
    if (kind == DISPLAY_UPDATE_BOOT_REPLACE || kind == DISPLAY_UPDATE_SCREEN_REPLACE || kind == DISPLAY_UPDATE_RECOVERY_CLEAN || kind == DISPLAY_UPDATE_SHUTDOWN_IMAGE) {
        disp_replace_commit_count++;
        Serial.println(kind == DISPLAY_UPDATE_SHUTDOWN_IMAGE ? "[DISPLAY LIFECYCLE] shutdown image GL16 frame begin" : "[DISPLAY LIFECYCLE] replacement GL16 frame begin");
        epd_hl_set_all_white(&hl);
        epd_draw_rotated_image(full_area, framebuffer4bpp, epd_hl_get_framebuffer(&hl));
        epd_poweron();
        vTaskDelay(pdMS_TO_TICKS(50));
        display_log_power("before_update", kind);
        uint32_t t0 = millis();
        EpdDrawError gl16_err = epd_hl_update_screen(&hl, MODE_GL16, epd_ambient_temperature());
        checkError(gl16_err);
        uint32_t dt = millis() - t0;
        Serial.printf("[EPD POWER] update complete kind=%d mode=GL16 duration_ms=%lu\n",
                      (int)kind, (unsigned long)dt);
        display_log_power("after_update", kind);
        vTaskDelay(pdMS_TO_TICKS(20));
        epd_poweroff();
        Serial.println(kind == DISPLAY_UPDATE_SHUTDOWN_IMAGE ? "[DISPLAY LIFECYCLE] shutdown image GL16 frame complete" : "[DISPLAY LIFECYCLE] replacement GL16 frame complete");
        if (home_waiting_for_redraw_commit &&
            (kind == DISPLAY_UPDATE_SCREEN_REPLACE || kind == DISPLAY_UPDATE_RECOVERY_CLEAN || kind == DISPLAY_UPDATE_BOOT_REPLACE || kind == DISPLAY_UPDATE_SHUTDOWN_IMAGE)) {
            home_waiting_for_redraw_commit = false;
            Serial.println("[HOME REDRAW] guard released after physical commit");
        }
        bool ok = (gl16_err == EPD_DRAW_SUCCESS);
        Serial.printf("[DISPLAY LOCK] releasing kind=%d ok=%d\n", (int)kind, ok ? 1 : 0);
        return ok;
    }
    epd_hl_set_all_white(&hl);
    epd_draw_rotated_image(full_area, framebuffer4bpp, epd_hl_get_framebuffer(&hl));
    epd_poweron();
    vTaskDelay(pdMS_TO_TICKS(50));
    display_log_power("before_update", kind);
    uint32_t t0 = millis();
    EpdDrawError gl16_err = epd_hl_update_screen(&hl, MODE_GL16, epd_ambient_temperature());
    checkError(gl16_err);
    uint32_t dt = millis() - t0;
    Serial.printf("[EPD POWER] update complete kind=%d mode=GL16 duration_ms=%lu\n",
                  (int)kind, (unsigned long)dt);
    display_log_power("after_update", kind);
    vTaskDelay(pdMS_TO_TICKS(20));
    epd_poweroff();
    Serial.println("[DISPLAY LIFECYCLE] normal full GL16 frame complete");
    bool ok = (gl16_err == EPD_DRAW_SUCCESS);
    Serial.printf("[DISPLAY LOCK] releasing kind=%d ok=%d\n", (int)kind, ok ? 1 : 0);
    return ok;
}
