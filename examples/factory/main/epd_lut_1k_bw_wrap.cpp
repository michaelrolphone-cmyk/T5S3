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

extern "C" void __real_epd_init(const void *board, const void *display, int lut);
extern "C" enum EpdDrawError __real_epd_hl_update_screen(EpdiyHighlevelState *state, enum EpdDrawMode mode, int temperature);
extern "C" void __real_epd_draw_rotated_image(EpdRect image_area, const uint8_t *image_buffer, uint8_t *framebuffer);

static uint8_t *bw_threshold_buffer = nullptr;
static size_t bw_threshold_buffer_size = 0;
static bool bw_mode_logged = false;

static inline uint8_t bw_threshold_nibble(uint8_t gray4)
{
    return (gray4 < EPD_BW_GRAY4_THRESHOLD) ? 0x00 : 0x0F;
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
    enum EpdDrawMode selected_mode = mode;
    if (mode == MODE_GL16 || mode == MODE_GC16) {
        selected_mode = MODE_DU;
    }
    if (!bw_mode_logged || selected_mode != mode) {
        Serial.printf("[EPD MODE] update requested=%d selected=%d lut=1K bilevel=1\n", (int)mode, (int)selected_mode);
        bw_mode_logged = true;
    }
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
