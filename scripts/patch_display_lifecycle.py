from pathlib import Path
Import("env")

p = Path(env["PROJECT_DIR"]) / "examples" / "factory" / "main" / "main.cpp"
s = p.read_text(encoding="utf-8")

new_flush = '''static void disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
    if (decodebuffer == NULL) {
        lv_disp_flush_ready(disp);
        return;
    }

    if (!disp_flush_enabled) {
        lv_disp_flush_ready(disp);
        return;
    }

    if (framebuffer_mutex && xSemaphoreTake(framebuffer_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        Serial.println("[DISPLAY DIRECT] flush lock timeout");
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
            uint8_t gray4 = lv_color_to_epd_gray4(color_p[y * w + x]);
            epd_image_set_pixel_4bpp(decodebuffer, screen_w, dst_x, dst_y, gray4);
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

    epd_draw_rotated_image(render_area, decodebuffer, epd_hl_get_framebuffer(&hl));
    if (framebuffer_mutex) xSemaphoreGive(framebuffer_mutex);

    epd_poweron();
    if (ui_refresh_get_mode() == UI_REFRESH_MODE_FAST) {
        checkError(epd_hl_update_area(&hl, MODE_DU, epd_ambient_temperature(), render_area));
    } else if (ui_refresh_get_mode() == UI_REFRESH_MODE_NEAT) {
        checkError(epd_hl_update_screen(&hl, MODE_GC16, epd_ambient_temperature()));
    } else {
        checkError(epd_hl_update_screen(&hl, MODE_GL16, epd_ambient_temperature()));
    }
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
s = s.replace(queue_check, '    Serial.println("[DISPLAY DIRECT] async display queue disabled; using synchronous LVGL flush path");\n')

if "disp_drv.render_start_cb = [](lv_disp_drv_t *drv)" not in s:
    s = s.replace('''    // disp_drv.render_start_cb = dips_render_start_cb;
''', '''    disp_drv.render_start_cb = [](lv_disp_drv_t *drv) {
        (void)drv;
        if (decodebuffer) memset(decodebuffer, EPD_LOGICAL_WHITE_BYTE, EPD_IMAGE_BUF_SIZE);
    };
''')
else:
    print("[PATCH] render_start clear already installed")

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

old_lut = 'epd_init(&DEMO_BOARD, &ED047TC1, EPD_LUT_64K);'
new_lut = 'epd_init(&DEMO_BOARD, &ED047TC1, EPD_LUT_1K); // 1K LUT keeps renderer lookup table internal and avoids the 64K heap cliff'
if old_lut in s:
    s = s.replace(old_lut, new_lut, 1)
elif new_lut in s or 'epd_init(&DEMO_BOARD, &ED047TC1, EPD_LUT_1K)' in s:
    print("[PATCH] EPD init already uses EPD_LUT_1K")
else:
    raise RuntimeError("Could not locate EPD LUT init call")

p.write_text(s, encoding="utf-8")
print("[PATCH] main.cpp display lifecycle patch complete: upstream-style flush path active")
