#include <Arduino.h>
#include <epdiy.h>
#include <esp_heap_caps.h>
#include <string.h>

#ifndef EPD_USE_1K_BW_LUT
#define EPD_USE_1K_BW_LUT 1
#endif

#ifndef EPD_BW_GRAY4_THRESHOLD
#define EPD_BW_GRAY4_THRESHOLD 8
#endif

#ifndef EPD_BW_FORCE_DU_FAST_MODE
#define EPD_BW_FORCE_DU_FAST_MODE 0
#endif

#ifndef EPD_BW_PREWHITE_EACH_CONTENT_UPDATE
#define EPD_BW_PREWHITE_EACH_CONTENT_UPDATE 1
#endif

extern "C" void __real_epd_init(const void *board, const void *display, int lut);
extern "C" enum EpdDrawError __real_epd_hl_update_screen(EpdiyHighlevelState *state, enum EpdDrawMode mode, int temperature);
extern "C" void __real_epd_draw_rotated_image(EpdRect image_area, const uint8_t *image_buffer, uint8_t *framebuffer);

static uint8_t *bw_threshold_buffer = nullptr;
static size_t bw_threshold_buffer_size = 0;
static uint8_t *bw_frame_backup = nullptr;
static size_t bw_frame_backup_size = 0;
static bool bw_mode_logged = false;
static bool bw_prewhite_logged = false;

static inline uint8_t bw_threshold_nibble(uint8_t gray4)
{
    return (gray4 < EPD_BW_GRAY4_THRESHOLD) ? 0x00 : 0x0F;
}

static size_t bw_framebuffer_size_bytes()
{
    const int width = epd_rotated_display_width();
    const int height = epd_rotated_display_height();
    if (width <= 0 || height <= 0) return 0;
    return (((size_t)width + 1U) / 2U) * (size_t)height;
}

static bool bw_frame_is_all_white(const uint8_t *framebuffer, size_t size)
{
    if (!framebuffer || size == 0) return true;
    for (size_t i = 0; i < size; ++i) {
        if (framebuffer[i] != 0xFF) return false;
    }
    return true;
}

static bool bw_ensure_frame_backup(size_t size)
{
    if (size == 0) return false;
    if (bw_frame_backup_size >= size && bw_frame_backup) return true;

    uint8_t *new_buffer = (uint8_t *)heap_caps_realloc(
        bw_frame_backup,
        size,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!new_buffer) {
        Serial.printf("[EPD BW] prewhite backup alloc failed size=%u; doing single update only\n", (unsigned)size);
        return false;
    }

    bw_frame_backup = new_buffer;
    bw_frame_backup_size = size;
    Serial.printf("[EPD BW] prewhite backup ready size=%u\n", (unsigned)size);
    return true;
}

static const uint8_t *bw_threshold_4bpp_image(EpdRect image_area, const uint8_t *image_buffer)
{
    if (!image_buffer || image_area.width <= 0 || image_area.height <= 0) {
        return image_buffer;
    }

    const size_t row_bytes = ((size_t)image_area.width + 1U) / 2U;
    const size_t required = row_bytes * (size_t)image_area.height;
    if (required == 0) {
        return image_buffer;
    }

    if (bw_threshold_buffer_size < required) {
        uint8_t *new_buffer = (uint8_t *)heap_caps_realloc(
            bw_threshold_buffer,
            required,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!new_buffer) {
            Serial.printf("[EPD BW] threshold buffer alloc failed size=%u; using source image unmodified\n", (unsigned)required);
            return image_buffer;
        }
        bw_threshold_buffer = new_buffer;
        bw_threshold_buffer_size = required;
        Serial.printf("[EPD BW] threshold buffer ready size=%u\n", (unsigned)required);
    }

    for (size_t i = 0; i < required; ++i) {
        const uint8_t src = image_buffer[i];
        const uint8_t low = bw_threshold_nibble(src & 0x0F);
        const uint8_t high = bw_threshold_nibble((src >> 4) & 0x0F);
        bw_threshold_buffer[i] = (uint8_t)((high << 4) | low);
    }

    return bw_threshold_buffer;
}

static enum EpdDrawMode bw_select_update_mode(enum EpdDrawMode requested)
{
#if EPD_BW_FORCE_DU_FAST_MODE
    if (requested == MODE_GL16 || requested == MODE_GC16) {
        return MODE_DU;
    }
#endif
    /*
     * Keep the caller's selected mode by default.
     *
     * Forcing all updates to MODE_DU caused weak/partial full-screen draws.
     * Forcing all updates to MODE_GC16 caused dark gray shading and black icon
     * blocks with the 1K LUT on this panel. The stable approach is to keep the
     * memory-saving 1K LUT and threshold image content to bilevel, but let the
     * existing display lifecycle choose GL16/GC16/DU as it already does.
     */
    return requested;
}

extern "C" void __wrap_epd_init(const void *board, const void *display, int lut)
{
#if EPD_USE_1K_BW_LUT
    (void)lut;
    Serial.println("[EPD MODE] forcing EPD_LUT_1K black/white mode");
    __real_epd_init(board, display, (int)EPD_LUT_1K);
#else
    __real_epd_init(board, display, lut);
#endif
}

extern "C" enum EpdDrawError __wrap_epd_hl_update_screen(EpdiyHighlevelState *state, enum EpdDrawMode mode, int temperature)
{
#if EPD_USE_1K_BW_LUT
    enum EpdDrawMode selected_mode = bw_select_update_mode(mode);
    if (!bw_mode_logged || selected_mode != mode) {
        Serial.printf("[EPD MODE] update requested=%d selected=%d lut=1K bilevel=1 force_du=%d prewhite=%d\n",
                      (int)mode,
                      (int)selected_mode,
                      (int)EPD_BW_FORCE_DU_FAST_MODE,
                      (int)EPD_BW_PREWHITE_EACH_CONTENT_UPDATE);
        bw_mode_logged = true;
    }

#if EPD_BW_PREWHITE_EACH_CONTENT_UPDATE
    uint8_t *framebuffer = epd_hl_get_framebuffer(state);
    const size_t frame_size = bw_framebuffer_size_bytes();
    if (framebuffer && frame_size > 0 && !bw_frame_is_all_white(framebuffer, frame_size) && bw_ensure_frame_backup(frame_size)) {
        if (!bw_prewhite_logged) {
            Serial.println("[EPD BW] prewhite erase enabled for non-white full-frame updates");
            bw_prewhite_logged = true;
        }
        memcpy(bw_frame_backup, framebuffer, frame_size);
        epd_hl_set_all_white(state);
        enum EpdDrawError white_rc = __real_epd_hl_update_screen(state, selected_mode, temperature);
        memcpy(framebuffer, bw_frame_backup, frame_size);
        if (white_rc != EPD_DRAW_SUCCESS) {
            Serial.printf("[EPD BW] prewhite update returned %d; continuing content update\n", (int)white_rc);
        }
    }
#endif

    return __real_epd_hl_update_screen(state, selected_mode, temperature);
#else
    return __real_epd_hl_update_screen(state, mode, temperature);
#endif
}

extern "C" void __wrap_epd_draw_rotated_image(EpdRect image_area, const uint8_t *image_buffer, uint8_t *framebuffer)
{
#if EPD_USE_1K_BW_LUT
    const uint8_t *bw_image = bw_threshold_4bpp_image(image_area, image_buffer);
    __real_epd_draw_rotated_image(image_area, bw_image, framebuffer);
#else
    __real_epd_draw_rotated_image(image_area, image_buffer, framebuffer);
#endif
}
