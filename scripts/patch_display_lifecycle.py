from pathlib import Path
Import("env")

p = Path(env["PROJECT_DIR"]) / "examples" / "factory" / "main" / "main.cpp"
s = p.read_text(encoding="utf-8")

# Add a real 1-bit framebuffer for the 1K black/white display path.
if "uint8_t *bwbuffer = NULL;" not in s:
    s = s.replace(
        "uint8_t *decodebuffer = NULL;\nuint8_t *displaybuffer = NULL;\n",
        "uint8_t *decodebuffer = NULL;\nuint8_t *displaybuffer = NULL;\nuint8_t *bwbuffer = NULL;\n"
    )

if "#define EPD_BW_BUF_SIZE" not in s:
    s = s.replace(
        "#define EPD_IMAGE_BUF_SIZE (((epd_rotated_display_width() + 1) / 2) * epd_rotated_display_height())\n",
        "#define EPD_IMAGE_BUF_SIZE (((epd_rotated_display_width() + 1) / 2) * epd_rotated_display_height())\n#define EPD_BW_BUF_SIZE (((epd_rotated_display_width() + 7) / 8) * epd_rotated_display_height())\n"
    )

if "static constexpr uint8_t EPD_BW_THRESHOLD" not in s:
    s = s.replace(
        "static constexpr uint8_t EPD_LOGICAL_WHITE_BYTE = 0xFF;\n",
        "static constexpr uint8_t EPD_LOGICAL_WHITE_BYTE = 0xFF;\nstatic constexpr uint8_t EPD_BW_THRESHOLD = 160;\n"
    )

if "static inline void epd_bw_set_pixel" not in s:
    marker = "static bool display_cmd_is_reliable(DisplayUpdateKind kind)"
    helper = '''static inline bool lv_color_to_epd_bw_white(lv_color_t color)
{
    lv_color32_t c32;
    c32.full = lv_color_to32(color);
    uint16_t gray = (uint16_t)c32.ch.red * 76U +
                    (uint16_t)c32.ch.green * 150U +
                    (uint16_t)c32.ch.blue * 30U;
    uint8_t gray8 = gray >> 8;
    return gray8 >= EPD_BW_THRESHOLD;
}

static inline void epd_bw_set_pixel(uint8_t *buf, int32_t width, int32_t x, int32_t y, bool white)
{
    const int32_t pitch = (width + 7) / 8;
    uint8_t *dst = &buf[y * pitch + (x >> 3)];
    uint8_t mask = (uint8_t)(0x80 >> (x & 7));
    if (white) {
        *dst |= mask;
    } else {
        *dst &= (uint8_t)~mask;
    }
}

'''
    pos = s.find(marker)
    if pos < 0:
        raise RuntimeError("Could not locate display_cmd_is_reliable marker")
    s = s[:pos] + helper + s[pos:]

new_flush = '''static void disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
    if (bwbuffer == NULL) {
        lv_disp_flush_ready(disp);
        return;
    }

    if (!disp_flush_enabled) {
        lv_disp_flush_ready(disp);
        return;
    }

    if (framebuffer_mutex && xSemaphoreTake(framebuffer_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        Serial.println("[DISPLAY BW] flush lock timeout");
        lv_disp_flush_ready(disp);
        return;
    }

    int32_t w = lv_area_get_width(area);
    int32_t h = lv_area_get_height(area);
    int32_t screen_w = epd_rotated_display_width();
    int32_t screen_h = epd_rotated_display_height();

    for (int32_t y = 0; y < h; y++) {
        int32_t dst_y = area->y1 + y;
        if (dst_y < 0 || dst_y >= screen_h) continue;
        for (int32_t x = 0; x < w; x++) {
            int32_t dst_x = area->x1 + x;
            if (dst_x < 0 || dst_x >= screen_w) continue;
            bool white = lv_color_to_epd_bw_white(color_p[y * w + x]);
            epd_bw_set_pixel(bwbuffer, screen_w, dst_x, dst_y, white);
        }
    }

    disp_lvgl_flush_count++;

    if (!lv_disp_flush_is_last(disp)) {
        if (framebuffer_mutex) xSemaphoreGive(framebuffer_mutex);
        lv_disp_flush_ready(disp);
        return;
    }

    EpdRect render_area = {
        .x = 0,
        .y = 0,
        .width = epd_rotated_display_width(),
        .height = epd_rotated_display_height(),
    };

    if (framebuffer_mutex) xSemaphoreGive(framebuffer_mutex);

    epd_poweron();
    checkError(epd_draw_base(
        render_area,
        bwbuffer,
        render_area,
        (EpdDrawMode)(MODE_EPDIY_MONOCHROME | MODE_PACKING_8PPB | PREVIOUSLY_WHITE),
        epd_ambient_temperature(),
        NULL,
        NULL,
        WAVEFORM));
    epd_poweroff();

    display_next_snapshot_kind = DISPLAY_UPDATE_NONE;
    display_reliable_pending_kind = DISPLAY_UPDATE_NONE;
    disp_force_clear_next_flush = false;
    lv_disp_flush_ready(disp);
}
'''

start = s.find("static void disp_flush(lv_disp_drv_t *disp")
end = s.find("\nvoid disp_request_full_clear(void)", start)
if start < 0 or end < 0:
    raise RuntimeError("Could not locate disp_flush block")
s = s[:start] + new_flush + s[end:]

queue_setup = '''    display_q = xQueueCreate(8, sizeof(DisplayCmd));
    display_snapshot_mutex = xSemaphoreCreateMutex();
    bool snapshot_pool_ok = true;
    for (uint8_t i = 0; i < DISPLAY_SNAPSHOT_COUNT; ++i) {
        display_snapshot_pool[i] = (uint8_t *)heap_caps_malloc(EPD_IMAGE_BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!display_snapshot_pool[i]) {
            snapshot_pool_ok = false;
        }
    }
'''
s = s.replace(queue_setup, "")

queue_check = '''    if (!display_q || !display_snapshot_mutex || !snapshot_pool_ok) {
        Serial.println("[DISPLAY LIFECYCLE] FATAL: display queue/snapshot allocation failed");
        return;
    }
    ensure_display_flush_task_started();
'''
s = s.replace(queue_check, '    Serial.println("[DISPLAY BW] async display queue disabled; using synchronous 1-bit LVGL flush path");\n')

alloc_old = "    decodebuffer = (uint8_t *)ps_calloc(sizeof(uint8_t), EPD_IMAGE_BUF_SIZE);\n    displaybuffer = (uint8_t *)ps_calloc(sizeof(uint8_t), EPD_IMAGE_BUF_SIZE);\n"
alloc_new = "    decodebuffer = (uint8_t *)ps_calloc(sizeof(uint8_t), EPD_IMAGE_BUF_SIZE);\n    displaybuffer = (uint8_t *)ps_calloc(sizeof(uint8_t), EPD_IMAGE_BUF_SIZE);\n    bwbuffer = (uint8_t *)ps_calloc(sizeof(uint8_t), EPD_BW_BUF_SIZE);\n"
if alloc_old in s and "bwbuffer = (uint8_t *)ps_calloc" not in s:
    s = s.replace(alloc_old, alloc_new, 1)

check_old = "if (!lv_disp_buf_1 || !lv_disp_buf_2 || !decodebuffer || !displaybuffer || !framebuffer_mutex)"
s = s.replace(check_old, "if (!lv_disp_buf_1 || !lv_disp_buf_2 || !decodebuffer || !displaybuffer || !bwbuffer || !framebuffer_mutex)")

if "if (bwbuffer) {" not in s:
    s = s.replace(
        "    if (displaybuffer) {\n        memset(displaybuffer, EPD_LOGICAL_WHITE_BYTE, EPD_IMAGE_BUF_SIZE);\n    }\n",
        "    if (displaybuffer) {\n        memset(displaybuffer, EPD_LOGICAL_WHITE_BYTE, EPD_IMAGE_BUF_SIZE);\n    }\n    if (bwbuffer) {\n        memset(bwbuffer, 0xFF, EPD_BW_BUF_SIZE);\n    }\n"
    )

# Clear the real 1bpp framebuffer, not just the old grayscale staging buffer.
if "if (bwbuffer) memset(bwbuffer, 0xFF, EPD_BW_BUF_SIZE);" not in s:
    s = s.replace('''    // disp_drv.render_start_cb = dips_render_start_cb;
''', '''    disp_drv.render_start_cb = [](lv_disp_drv_t *drv) {
        (void)drv;
        if (decodebuffer) memset(decodebuffer, EPD_LOGICAL_WHITE_BYTE, EPD_IMAGE_BUF_SIZE);
        if (bwbuffer) memset(bwbuffer, 0xFF, EPD_BW_BUF_SIZE);
    };
''')
else:
    print("[PATCH] render_start BW clear already installed")

s = s.replace('''void disp_request_normal_frame(void)
{
    publish_snapshot(DISPLAY_UPDATE_NORMAL_FRAME, false, NULL);
}
''', '''void disp_request_normal_frame(void)
{
    lv_obj_t *act = lv_scr_act();
    if (act) lv_obj_invalidate(act);
}
''')

lut_1k_comment = 'epd_init(&DEMO_BOARD, &ED047TC1, EPD_LUT_1K); // 1K LUT keeps renderer lookup table internal and avoids the 64K heap cliff'
lut_1k_plain = 'epd_init(&DEMO_BOARD, &ED047TC1, EPD_LUT_1K);'
lut_64k = 'epd_init(&DEMO_BOARD, &ED047TC1, EPD_LUT_64K);'
if lut_64k in s:
    s = s.replace(lut_64k, lut_1k_comment, 1)
elif lut_1k_plain in s or lut_1k_comment in s:
    print("[PATCH] EPD init already uses EPD_LUT_1K")
else:
    raise RuntimeError("Could not locate EPD LUT init call")

p.write_text(s, encoding="utf-8")
print("[PATCH] main.cpp display lifecycle patch complete: true 1-bit black/white path active")
