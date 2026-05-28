from pathlib import Path
Import("env")

p = Path(env["PROJECT_DIR"]) / "examples" / "factory" / "main" / "main.cpp"
s = p.read_text(encoding="utf-8")

start = s.find("static void disp_flush(lv_disp_drv_t *disp")
end = s.find("\nvoid disp_request_full_clear(void)", start)
if start < 0 or end < 0:
    raise RuntimeError("Could not locate disp_flush block")

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

    DisplayUpdateKind requested_kind = display_next_snapshot_kind;
    DisplayUpdateKind kind = requested_kind != DISPLAY_UPDATE_NONE ? requested_kind : DISPLAY_UPDATE_NORMAL_FRAME;

    if (displaybuffer) {
        memcpy(displaybuffer, decodebuffer, EPD_IMAGE_BUF_SIZE);
    }
    if (framebuffer_mutex) xSemaphoreGive(framebuffer_mutex);

    display_commit_frame(kind, displaybuffer ? displaybuffer : decodebuffer);

    display_next_snapshot_kind = DISPLAY_UPDATE_NONE;
    if (display_reliable_pending_kind == kind) {
        display_reliable_pending_kind = DISPLAY_UPDATE_NONE;
    }
    disp_force_clear_next_flush = false;
    lv_disp_flush_ready(disp);
}
'''
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

s = s.replace('''    // disp_drv.render_start_cb = dips_render_start_cb;
''', '''    disp_drv.render_start_cb = [](lv_disp_drv_t *drv) {
        (void)drv;
        if (decodebuffer) memset(decodebuffer, EPD_LOGICAL_WHITE_BYTE, EPD_IMAGE_BUF_SIZE);
    };
''')

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

p.write_text(s, encoding="utf-8")
print("[PATCH] main.cpp display lifecycle patched: direct synchronous flush active")
