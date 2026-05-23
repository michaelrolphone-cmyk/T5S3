

#include "lvgl.h"
#include "scr_mrg.h"
#include "ui.h"
#include "ui_port.h"
#include "src/assets.h"
#include "nvs_param.h"
#include "SD.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <HTTPClient.h>
#include <PNGdec.h>

/* clang-format off */

#define ARRAY_LEN(a) (sizeof(a)/sizeof(a[0]))

static const uint32_t EPD_MAIN_STATES[] = {
    LV_PART_MAIN | LV_STATE_DEFAULT,
    LV_PART_MAIN | LV_STATE_PRESSED,
    LV_PART_MAIN | LV_STATE_FOCUSED,
    LV_PART_MAIN | LV_STATE_CHECKED,
    LV_PART_MAIN | LV_STATE_EDITED,
    LV_PART_MAIN | LV_STATE_DISABLED,
};

static void epd_style_label(lv_obj_t *obj)
{
    if (!obj) return;
    lv_obj_set_style_text_color(obj, lv_color_hex(EPD_COLOR_FG), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(obj, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void epd_style_main_states(lv_obj_t *obj, lv_opa_t bg_opa, uint16_t border_width)
{
    if (!obj) return;
    for (size_t i = 0; i < sizeof(EPD_MAIN_STATES) / sizeof(EPD_MAIN_STATES[0]); ++i) {
        uint32_t sel = EPD_MAIN_STATES[i];
        lv_obj_set_style_bg_color(obj, lv_color_hex(EPD_COLOR_BG), sel);
        lv_obj_set_style_bg_opa(obj, bg_opa, sel);
        lv_obj_set_style_text_color(obj, lv_color_hex(EPD_COLOR_FG), sel);
        lv_obj_set_style_text_opa(obj, LV_OPA_COVER, sel);
        lv_obj_set_style_border_color(obj, lv_color_hex(EPD_COLOR_FG), sel);
        lv_obj_set_style_border_width(obj, border_width, sel);
        lv_obj_set_style_shadow_width(obj, 0, sel);
        lv_obj_set_style_outline_width(obj, 0, sel);
    }
}

static void epd_style_plain(lv_obj_t *obj) { epd_style_main_states(obj, LV_OPA_COVER, 1); }
static void epd_style_transparent(lv_obj_t *obj) { epd_style_main_states(obj, LV_OPA_TRANSP, 0); }

static void epd_style_button(lv_obj_t *obj)
{
    epd_style_main_states(obj, LV_OPA_COVER, 2);
    lv_obj_set_style_bg_color(obj, lv_color_hex(EPD_COLOR_BG), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_width(obj, 3, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(obj, lv_color_hex(EPD_COLOR_BG), LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(obj, 3, LV_PART_MAIN | LV_STATE_FOCUSED);
}

static void epd_style_textarea(lv_obj_t *obj)
{
    epd_style_plain(obj);
    lv_obj_set_style_bg_color(obj, lv_color_hex(EPD_COLOR_BG), LV_PART_SELECTED);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_SELECTED);
    lv_obj_set_style_text_color(obj, lv_color_hex(EPD_COLOR_FG), LV_PART_SELECTED);
    lv_obj_set_style_bg_color(obj, lv_color_hex(EPD_COLOR_FG), LV_PART_CURSOR);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_CURSOR);
    lv_obj_set_style_width(obj, 2, LV_PART_CURSOR);
}

static void epd_style_keyboard(lv_obj_t *obj)
{
    epd_style_plain(obj);
    const uint32_t item_selectors[] = {
        LV_PART_ITEMS | LV_STATE_DEFAULT, LV_PART_ITEMS | LV_STATE_PRESSED, LV_PART_ITEMS | LV_STATE_FOCUSED,
        LV_PART_ITEMS | LV_STATE_CHECKED, LV_PART_ITEMS | LV_STATE_DISABLED,
    };
    for (size_t i = 0; i < sizeof(item_selectors) / sizeof(item_selectors[0]); ++i) {
        uint32_t sel = item_selectors[i];
        lv_obj_set_style_bg_color(obj, lv_color_hex(EPD_COLOR_BG), sel);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, sel);
        lv_obj_set_style_text_color(obj, lv_color_hex(EPD_COLOR_FG), sel);
        lv_obj_set_style_text_opa(obj, LV_OPA_COVER, sel);
        lv_obj_set_style_border_color(obj, lv_color_hex(EPD_COLOR_FG), sel);
        lv_obj_set_style_border_width(obj, 1, sel);
        lv_obj_set_style_shadow_width(obj, 0, sel);
        lv_obj_set_style_outline_width(obj, 0, sel);
    }
    lv_obj_set_style_border_width(obj, 3, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(obj, lv_color_hex(EPD_COLOR_BG), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_ITEMS | LV_STATE_PRESSED);
}

static void keyboard_disable_long_press_repeat(lv_obj_t *keyboard)
{
    if (!keyboard) return;
    lv_btnmatrix_set_btn_ctrl_all(keyboard, LV_BTNMATRIX_CTRL_NO_REPEAT);
}

static void epd_style_scrollbar(lv_obj_t *obj)
{
    if (!obj) return;
    lv_obj_set_style_bg_color(obj, lv_color_hex(EPD_COLOR_FG), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(obj, LV_OPA_50, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(obj, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
}

static void epd_style_selectable_field(lv_obj_t *obj)
{
    epd_style_main_states(obj, LV_OPA_TRANSP, 1);
    lv_obj_set_style_bg_color(obj, lv_color_hex(EPD_COLOR_BG), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_width(obj, 3, LV_PART_MAIN | LV_STATE_PRESSED);
}

static int scr_refresh_mode;
static lv_timer_t *taskbar_update_timer = NULL;
static void format_time_12h(uint8_t h24, uint8_t m, char *buf, size_t len, const char **ampm);
uint16_t taskbar_statue[TASKBAR_ID_MAX] = {0};
struct tm timeinfo = {0};
//************************************[ Other fun ]******************************************
#if 1
void scr_back_btn_create(lv_obj_t *parent, const char *text, lv_event_cb_t cb)
{
    lv_obj_t * btn = lv_btn_create(parent);
    epd_style_transparent(btn);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_set_height(btn, 50);
    lv_obj_align(btn, LV_ALIGN_TOP_LEFT, 15, 15);
    lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_outline_width(btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_outline_width(btn, 0, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_outline_width(btn, 0, LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label2 = lv_label_create(btn);
    lv_obj_align(label2, LV_ALIGN_LEFT_MID, 0, 0);
    epd_style_label(label2);
    lv_label_set_text(label2, " " LV_SYMBOL_LEFT);

    lv_obj_t *label = lv_label_create(parent);
    lv_obj_align_to(label, btn, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
    lv_obj_set_style_text_font(label, &Font_Mono_Bold_30, LV_PART_MAIN);
    epd_style_label(label);
    lv_label_set_text(label, text);
    lv_obj_add_flag(label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(label, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_ext_click_area(label, 30);
}

void scr_middle_line(lv_obj_t *parent)
{
    static lv_point_t line_points[2] = {0};
    line_points[0].x = LCD_HOR_SIZE / 2;
    line_points[0].y = 0;
    line_points[1].x = LCD_HOR_SIZE / 2;
    line_points[1].y = LCD_VER_SIZE - 150;

    /*Create style*/
    static lv_style_t style_line;
    lv_style_init(&style_line);
    lv_style_set_line_width(&style_line, 2);
    lv_style_set_line_color(&style_line, lv_color_black());
    lv_style_set_line_rounded(&style_line, true);
    /*Create a line and apply the new style*/
    lv_obj_t * line1;
    line1 = lv_line_create(parent);
    lv_line_set_points(line1, line_points, 2);     /*Set the points*/
    lv_obj_add_style(line1, &style_line, 0);
    lv_obj_set_align(line1, LV_ALIGN_LEFT_MID);
}

static const char *line_full_format(int max_c, const char *str1, const char *str2)
{
    int len1 = 0, len2 = 0;
    int j;

    len1 = strlen(str1);

    strncpy(global_buf, str1, len1);

    len2 = strlen(str2);
    for(j = len1; j < max_c -1 - len2; j++){
        global_buf[j] = ' ';
    }
    strncpy(global_buf + j, str2, len2);
    j = j + len2;
    
    global_buf[j] = '\0'; 

    printf("[%d] buf: %s\n", __LINE__, global_buf);

    return (const char *)global_buf;
}


/* clang-format on */
#define SETTING_PAGE_MAX_ITEM 7

#define UI_LIST_CREATE(func, handle, list, num, page_num, curr_page)                       \
    static void func##_scr_event(lv_event_t *e)                                            \
    {                                                                                      \
        lv_obj_t *tgt = (lv_obj_t *)e->target;                                             \
        ui_setting_handle *h = (ui_setting_handle *)e->user_data;                          \
        int n;                                                                             \
        if (e->code == LV_EVENT_CLICKED)                                                   \
        {                                                                                  \
            switch (h->type)                                                               \
            {                                                                              \
            case UI_SETTING_TYPE_SW:                                                       \
                if (h->get_cb != NULL && h->set_cb != NULL)                                \
                {                                                                          \
                    h->get_cb(&n);                                                         \
                    h->set_cb(n);                                                          \
                    lv_label_set_text_fmt(h->st, "%s", h->get_cb(NULL));                   \
                }                                                                          \
                break;                                                                     \
            case UI_SETTING_TYPE_SUB:                                                      \
                scr_mgr_push(h->sub_id, false);                                            \
                break;                                                                     \
            default:                                                                       \
                break;                                                                     \
            }                                                                              \
        }                                                                                  \
    }                                                                                      \
    static void func##_item_create(void)                                                   \
    {                                                                                      \
        num = sizeof(handle) / sizeof(handle[0]);                                          \
        page_num = num / SETTING_PAGE_MAX_ITEM;                                            \
        int start = (curr_page * SETTING_PAGE_MAX_ITEM);                                   \
        int end = start + SETTING_PAGE_MAX_ITEM;                                           \
        if (end > num)                                                                     \
            end = num;                                                                     \
        for (int i = start; i < end; i++)                                                  \
        {                                                                                  \
            ui_setting_handle *h = &handle[i];                                             \
                                                                                           \
            h->obj = lv_obj_class_create_obj(&lv_list_btn_class, list);                    \
            lv_obj_class_init_obj(h->obj);                                                 \
            lv_obj_set_size(h->obj, LV_PCT(100), LV_SIZE_CONTENT);                         \
            epd_style_button(h->obj);                                                      \
                                                                                           \
            lv_obj_t *label = lv_label_create(h->obj);                                     \
            lv_label_set_text(label, h->name);                                             \
            lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);                  \
            lv_obj_align(label, LV_ALIGN_LEFT_MID, 10, 0);                                 \
            epd_style_label(label);                                                        \
                                                                                           \
            lv_obj_set_height(h->obj, 85);                                                 \
            lv_obj_set_style_text_font(h->obj, &Font_Mono_Bold_30, LV_PART_MAIN);          \
            lv_obj_set_style_bg_color(h->obj, lv_color_hex(EPD_COLOR_BG), LV_PART_MAIN);   \
            lv_obj_set_style_text_color(h->obj, lv_color_hex(EPD_COLOR_FG), LV_PART_MAIN); \
            lv_obj_set_style_border_width(h->obj, 3, LV_PART_MAIN | LV_STATE_DEFAULT);     \
            lv_obj_set_style_border_width(h->obj, 3, LV_PART_MAIN | LV_STATE_PRESSED);     \
            lv_obj_set_style_outline_width(h->obj, 0, LV_PART_MAIN | LV_STATE_PRESSED);    \
            lv_obj_set_style_radius(h->obj, 30, LV_PART_MAIN | LV_STATE_DEFAULT);          \
            lv_obj_add_event_cb(h->obj, func##_scr_event, LV_EVENT_CLICKED, (void *)h);    \
                                                                                           \
            if (h->get_cb)                                                                 \
            {                                                                              \
                h->st = lv_label_create(h->obj);                                           \
                lv_obj_set_style_text_font(h->st, &Font_Mono_Bold_30, LV_PART_MAIN);       \
                lv_obj_align(h->st, LV_ALIGN_RIGHT_MID, -3, 0);                            \
                lv_label_set_text_fmt(h->st, "%s", h->get_cb(NULL));                       \
                epd_style_label(h->st);                                                    \
            }                                                                              \
        }                                                                                  \
    }

#define UI_LIST_BTN_CREATE(func, list, page, num, page_num, curr_page) \
    static void func##_page_switch_cb(lv_event_t *e)                   \
    {                                                                  \
        char opt = (int)e->user_data;                                  \
                                                                       \
        if (num < SETTING_PAGE_MAX_ITEM)                               \
            return;                                                    \
                                                                       \
        int child_cnt = lv_obj_get_child_cnt(list);                    \
                                                                       \
        for (int i = 0; i < child_cnt; i++)                            \
        {                                                              \
            lv_obj_t *child = lv_obj_get_child(list, 0);               \
            if (child)                                                 \
                lv_obj_del(child);                                     \
        }                                                              \
                                                                       \
        if (opt == 'p')                                                \
        {                                                              \
            curr_page = (curr_page < page_num) ? curr_page + 1 : 0;    \
        }                                                              \
        else if (opt == 'n')                                           \
        {                                                              \
            curr_page = (curr_page > 0) ? curr_page - 1 : page_num;    \
        }                                                              \
                                                                       \
        func##_item_create();                                          \
        lv_label_set_text_fmt(page, "%d / %d", curr_page, page_num);   \
    }

/* clang-format off */
void ui_list_btn_create(lv_obj_t *parent, lv_event_cb_t event_cb)
{
    lv_obj_t * ui_Button2 = lv_btn_create(parent);
    epd_style_button(ui_Button2);
    lv_obj_set_width(ui_Button2, 140);
    lv_obj_set_height(ui_Button2, 85);
    lv_obj_align(ui_Button2, LV_ALIGN_BOTTOM_MID, -140, -30);
    // lv_obj_set_align(ui_Button2, LV_ALIGN_CENTER);
    lv_obj_add_flag(ui_Button2, LV_OBJ_FLAG_SCROLL_ON_FOCUS);     /// Flags
    lv_obj_clear_flag(ui_Button2, LV_OBJ_FLAG_SCROLLABLE);      /// Flags
    lv_obj_set_style_radius(ui_Button2, 10, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t * ui_Label1 = lv_label_create(ui_Button2);
    lv_obj_set_width(ui_Label1, LV_SIZE_CONTENT);   /// 1
    lv_obj_set_height(ui_Label1, LV_SIZE_CONTENT);    /// 1
    lv_obj_set_align(ui_Label1, LV_ALIGN_CENTER);
    lv_label_set_text(ui_Label1, "Back");
    epd_style_label(ui_Label1);

    lv_obj_t * ui_Button14 = lv_btn_create(parent);
    epd_style_button(ui_Button14);
    lv_obj_set_width(ui_Button14, 140);
    lv_obj_set_height(ui_Button14, 85);
    lv_obj_align(ui_Button14, LV_ALIGN_BOTTOM_MID, 140, -30);
    // lv_obj_set_align(ui_Button14, LV_ALIGN_CENTER);
    lv_obj_add_flag(ui_Button14, LV_OBJ_FLAG_SCROLL_ON_FOCUS);     /// Flags
    lv_obj_clear_flag(ui_Button14, LV_OBJ_FLAG_SCROLLABLE);      /// Flags
    lv_obj_set_style_radius(ui_Button14, 10, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t * ui_Label15 = lv_label_create(ui_Button14);
    lv_obj_set_width(ui_Label15, LV_SIZE_CONTENT);   /// 1
    lv_obj_set_height(ui_Label15, LV_SIZE_CONTENT);    /// 1
    lv_obj_set_align(ui_Label15, LV_ALIGN_CENTER);
    lv_label_set_text(ui_Label15, "Next");
    epd_style_label(ui_Label15);

    lv_obj_add_event_cb(ui_Button2, event_cb, LV_EVENT_CLICKED, (void*)'n');
    lv_obj_add_event_cb(ui_Button14, event_cb, LV_EVENT_CLICKED, (void*)'p');
}

#endif
//************************************[ screen 0 ]****************************************** menu
#if 1
static const int SPRINGBOARD_ICON_W = 96;
static const int SPRINGBOARD_ICON_H = 96;
static const size_t SPRINGBOARD_ICON_MAX_PNG_SIZE = 512 * 1024;
static int springboard_icon_bw_threshold = 180;

struct springboard_runtime_icon {
    lv_color_t *buf;
    lv_obj_t *canvas;
    bool loaded_png;
    char source_path[96];
};

/* Expected SD icon files:
 * /icons/apps/clock.png
 * /icons/apps/lora.png
 * /icons/apps/sd_card.png
 * /icons/apps/gps.png
 * /icons/apps/reader.png
 * /icons/apps/wifi.png
 * /icons/apps/battery.png
 * /icons/apps/settings.png
 * /icons/apps/power.png
 * /icons/apps/sleep.png
 * /icons/apps/test.png
 * /icons/apps/browser.png
 * /icons/apps/maps.png
 */
const struct menu_icon icon_buf[] = {
    {&img_clock,    "clock"   , 45,   45,  "/icons/apps/clock.png",   NULL},
    {&img_lora,     "lora"    , 210,  45,  "/icons/apps/lora.png",    NULL},
    {&img_sd_card,  "sd card" , 375,  45,  "/icons/apps/sd_card.png", "/icons/apps/sd.png"},
    {&img_gps,      "gps"     , 45,   250, "/icons/apps/gps.png",     NULL},
    {&img_test,     "Reader"  , 210,  250, "/icons/apps/reader.png",  "/icons/apps/markdown_reader.png"},
    {&img_wifi,     "wifi"    , 375,  250, "/icons/apps/wifi.png",    NULL},
    {&img_battery,  "battery" , 45,   455, "/icons/apps/battery.png", NULL},
};

const struct menu_icon icon_buf2[] = {
    {&img_setting,  "setting" , 45,   45,  "/icons/apps/settings.png", "/icons/apps/setting.png"},
    {&img_shutdown, "shutdown", 210,  45,  "/icons/apps/power.png",    "/icons/apps/shutdown.png"},
    {&img_sleep,    "sleep"   , 375,  45,  "/icons/apps/sleep.png",    NULL},
    {&img_test,     "test"    , 45,   250, "/icons/apps/test.png",     NULL},
    {&img_wifi,     "browser" , 210,  250, "/icons/apps/browser.png",  NULL},
    {&img_gps,      "maps"    , 375,  250, "/icons/apps/maps.png",     NULL},
};
static springboard_runtime_icon springboard_icons_page1[ARRAY_LEN(icon_buf)];
static springboard_runtime_icon springboard_icons_page2[ARRAY_LEN(icon_buf2)];
static PNG springboard_png_decoder;
static lv_color_t *springboard_decode_buf = NULL;
static uint16_t *springboard_decode_line = NULL;
static int springboard_decode_src_w = 0;
static int springboard_decode_src_h = 0;
static int springboard_decode_draw_w = 0;
static int springboard_decode_draw_h = 0;
static int springboard_decode_offset_x = 0;
static int springboard_decode_offset_y = 0;

static int springboard_png_draw_cb(PNGDRAW *pDraw)
{
    if (!pDraw || !springboard_decode_buf || !springboard_decode_line || springboard_decode_src_w <= 0 || springboard_decode_src_h <= 0) return 0;
    springboard_png_decoder.getLineAsRGB565(pDraw, springboard_decode_line, PNG_RGB565_LITTLE_ENDIAN, 0xffffffff);
    if (pDraw->y < 0 || pDraw->y >= springboard_decode_src_h) return 1;
    int dst_y = springboard_decode_offset_y + ((pDraw->y * springboard_decode_draw_h) / springboard_decode_src_h);
    if (dst_y < 0 || dst_y >= SPRINGBOARD_ICON_H) return 1;
    for (int dst_x = 0; dst_x < springboard_decode_draw_w; dst_x++) {
        int src_x = (dst_x * springboard_decode_src_w) / springboard_decode_draw_w;
        if (src_x < 0 || src_x >= springboard_decode_src_w) continue;
        uint16_t c = springboard_decode_line[src_x];
        uint8_t r = ((c >> 11) & 0x1f) << 3;
        uint8_t g = ((c >> 5) & 0x3f) << 2;
        uint8_t b = (c & 0x1f) << 3;
        int gray = (r * 30 + g * 59 + b * 11) / 100;
        springboard_decode_buf[dst_y * SPRINGBOARD_ICON_W + springboard_decode_offset_x + dst_x] =
            (gray < springboard_icon_bw_threshold) ? lv_color_black() : lv_color_white();
    }
    return 1;
}

static bool springboard_icon_png_exists(const char *path)
{
    if (!path || !path[0]) return false;
    int sd_ok = 0;
    ui_test_get_sd(&sd_ok);
    if (sd_ok != 1) return false;
    bool ok = false;
    sd_guard_lock();
    if (SD.exists(path)) {
        File f = SD.open(path, FILE_READ);
        if (f) {
            size_t sz = f.size();
            if (sz >= 8 && sz <= SPRINGBOARD_ICON_MAX_PNG_SIZE) {
                uint8_t sig[8] = {0};
                if (f.read(sig, sizeof(sig)) == sizeof(sig)) {
                    const uint8_t png_magic[8] = {0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};
                    ok = (memcmp(sig, png_magic, sizeof(sig)) == 0);
                }
            }
            f.close();
        }
    }
    sd_guard_unlock();
    return ok;
}

static bool springboard_decode_png_to_buf(const char *path, lv_color_t *dst, char *reason, size_t reason_len)
{
    if (!path || !dst) { lv_snprintf(reason, reason_len, "invalid_argument"); return false; }
    memset(dst, 0xFF, SPRINGBOARD_ICON_W * SPRINGBOARD_ICON_H * sizeof(lv_color_t));
    sd_guard_lock();
    File f = SD.open(path, FILE_READ);
    if (!f) { sd_guard_unlock(); lv_snprintf(reason, reason_len, "open_failed"); return false; }
    size_t sz = f.size();
    if (sz < 8 || sz > SPRINGBOARD_ICON_MAX_PNG_SIZE) { f.close(); sd_guard_unlock(); lv_snprintf(reason, reason_len, "invalid_size"); return false; }
    uint8_t *raw = (uint8_t *)ps_malloc(sz);
    if (!raw) { f.close(); sd_guard_unlock(); lv_snprintf(reason, reason_len, "png_raw_alloc_failed"); return false; }
    size_t n = f.read(raw, sz);
    f.close();
    sd_guard_unlock();
    if (n != sz) { free(raw); lv_snprintf(reason, reason_len, "short_read"); return false; }

    uint16_t *line = (uint16_t *)malloc(SPRINGBOARD_ICON_W * sizeof(uint16_t));
    if (!line) { free(raw); lv_snprintf(reason, reason_len, "line_alloc_failed"); return false; }
    int rc = springboard_png_decoder.openRAM(raw, (int)sz, springboard_png_draw_cb);
    if (rc != PNG_SUCCESS) { free(raw); free(line); lv_snprintf(reason, reason_len, "png_open_failed:%d", rc); return false; }
    int src_w = springboard_png_decoder.getWidth(), src_h = springboard_png_decoder.getHeight();
    if (src_w <= 0 || src_h <= 0) { springboard_png_decoder.close(); free(raw); free(line); lv_snprintf(reason, reason_len, "invalid_dim"); return false; }
    float scale = (float)SPRINGBOARD_ICON_W / (float)src_w;
    float scale_h = (float)SPRINGBOARD_ICON_H / (float)src_h;
    if (scale_h < scale) scale = scale_h;
    springboard_decode_draw_w = (int)(src_w * scale);
    springboard_decode_draw_h = (int)(src_h * scale);
    if (springboard_decode_draw_w < 1) springboard_decode_draw_w = 1;
    if (springboard_decode_draw_h < 1) springboard_decode_draw_h = 1;
    springboard_decode_offset_x = (SPRINGBOARD_ICON_W - springboard_decode_draw_w) / 2;
    springboard_decode_offset_y = (SPRINGBOARD_ICON_H - springboard_decode_draw_h) / 2;
    springboard_decode_src_w = src_w;
    springboard_decode_src_h = src_h;
    springboard_decode_buf = dst;
    springboard_decode_line = line;
    rc = springboard_png_decoder.decode(NULL, 0);
    springboard_png_decoder.close();
    springboard_decode_buf = NULL;
    springboard_decode_line = NULL;
    free(raw);
    free(line);
    if (rc != PNG_SUCCESS) { lv_snprintf(reason, reason_len, "png_decode_failed:%d", rc); return false; }
    lv_snprintf(reason, reason_len, "ok");
    return true;
}

static lv_obj_t *ui_Panel4;
static lv_obj_t *menu_screen1;
static lv_obj_t *menu_screen2;
// taskbar
static lv_obj_t *menu_taskbar = NULL;
static lv_obj_t *menu_taskbar_time = NULL;
static lv_obj_t *menu_taskbar_charge = NULL;
static lv_obj_t *menu_taskbar_battery = NULL;
static lv_obj_t *menu_taskbar_battery_percent = NULL;
static lv_obj_t *menu_taskbar_wifi = NULL;
static lv_obj_t *menu_taskbar_sd = NULL;

static int page_num = 1;
static int page_curr = 0;

static void menu_get_gesture_dir(int dir)
{
    if(dir == LV_DIR_LEFT) {
        if(page_curr < page_num){
            page_curr++;
            // ui_disp_full_refr();
            // // ui_full_refresh();
        }
        else{
            return ;
        }
    } else if(dir == LV_DIR_RIGHT) {
        if(page_curr > 0){
            page_curr--;
            // // ui_full_refresh();
        }
        else{
            return ;
        }
    }   

    Serial.printf("[gesture] curr=%d, sum=%d, dir=%d\n", page_curr, page_num, dir);

    if(page_curr == 1) {
        lv_obj_clear_flag(menu_screen2, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(menu_screen1, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(lv_obj_get_child(ui_Panel4, 0), lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(lv_obj_get_child(ui_Panel4, 1), lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);

    } else if(page_curr == 0) {
        lv_obj_clear_flag(menu_screen1, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(menu_screen2, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(lv_obj_get_child(ui_Panel4, 0), lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(lv_obj_get_child(ui_Panel4, 1), lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

static void menu_gesture_event(lv_event_t *e)
{
    if (touch_reject_stale_home_event()) {
        Serial.println("[MENU] ignored stale gesture after Home");
        return;
    }

    lv_indev_t * touch_indev = lv_indev_get_next(NULL);
    lv_dir_t dir = lv_indev_get_gesture_dir(touch_indev);

    // printf("code=%d gesture = %d\n", e->code, dir);

    if(dir == LV_DIR_RIGHT) { // right
        menu_get_gesture_dir(LV_DIR_RIGHT);
    } 
    else if(dir == LV_DIR_LEFT) { // left
        menu_get_gesture_dir(LV_DIR_LEFT);
    }
}

static void menu_btn_event(lv_event_t *e)
{
    if (touch_reject_stale_home_event()) {
        Serial.println("[MENU] ignored stale click after Home");
        return;
    }
    int data = (int)e->user_data;
    printf("code=%d\n", lv_event_get_code(e));
    if(e->code == LV_EVENT_CLICKED) {

        // ui_full_refresh();
        // ui_full_clean();
        if(data < ARRAY_LEN(icon_buf))
        {
            printf("[%d] %s is clicked.\n", data, icon_buf[data].icon_str);
        }
        else{
            printf("[%d] %s is clicked.\n", data, icon_buf2[data - ARRAY_LEN(icon_buf)].icon_str);
        }
        /************* page1 ************
         * 0 --- SCREEN1_ID  --- clock
         * 1 --- SCREEN2_ID  --- lora
         * 2 --- SCREEN3_ID  --- sd card
         * 3 --- SCREEN10_ID --- gps
         * 4 --- SCREEN11_ID --- markdown
         * 5 --- SCREEN6_ID  --- wifi
         * 6 --- SCREEN7_ID  --- battery
         ************ page2 ************
         * 7 --- SCREEN4_ID  --- setting
         * 8 --- SCREEN8_ID  --- shutdown
         * 9 --- SCREEN9_ID  --- sleep
         * 10 -- SCREEN5_ID  --- test
         * 11 -- SCREEN12_ID --- browser
         * 12 -- SCREEN13_ID --- maps
        */
        switch (data) {
            case 0: scr_mgr_push(SCREEN1_ID, false); break;
            case 1: scr_mgr_push(SCREEN2_ID, false); break;
            case 2: scr_mgr_push(SCREEN3_ID, false); break;
            case 3: scr_mgr_push(SCREEN10_ID, false); break;
            case 4: scr_mgr_push(SCREEN11_ID, false); break;
            case 5: scr_mgr_push(SCREEN6_ID, false); break;
            case 6: scr_mgr_push(SCREEN7_ID, false); break;
            case 7: scr_mgr_push(SCREEN4_ID, false); break;
            case 8: scr_mgr_push(SCREEN8_ID, false); break;
            case 9: scr_mgr_push(SCREEN9_ID, false); break;
            case 10: scr_mgr_push(SCREEN5_ID, false); break;
            case 11: scr_mgr_push(SCREEN12_ID, false); break;
            case 12: scr_mgr_push(SCREEN13_ID, false); break;
            default: break;
        }
    }
}

static void create0(lv_obj_t *parent) 
{
    page_curr = 0;

    int status_bar_height = 60;

    menu_taskbar = lv_obj_create(parent);
    lv_obj_set_size(menu_taskbar, LV_HOR_RES, status_bar_height);
    lv_obj_set_style_pad_all(menu_taskbar, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(menu_taskbar, 1, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(menu_taskbar, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(menu_taskbar, LV_OBJ_FLAG_SCROLLABLE);
    
    menu_taskbar_time = lv_label_create(menu_taskbar);
    lv_obj_set_style_border_width(menu_taskbar_time, 0, 0);
    lv_obj_set_style_text_font(menu_taskbar_time, &Font_Mono_Bold_25, LV_PART_MAIN);
    lv_obj_align(menu_taskbar_time, LV_ALIGN_LEFT_MID, 20, 0);

    lv_obj_t *status_parent = lv_obj_create(menu_taskbar);
    lv_obj_set_size(status_parent, lv_pct(80)-4, status_bar_height-10);
    lv_obj_set_style_pad_all(status_parent, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(status_parent, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(status_parent, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(status_parent, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_left(status_parent, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(status_parent, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(status_parent, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(status_parent, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(status_parent, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(status_parent, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_scrollbar_mode(status_parent, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(status_parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(status_parent, LV_ALIGN_RIGHT_MID, 0, 0);

    menu_taskbar_wifi = lv_label_create(status_parent);
    lv_label_set_text_fmt(menu_taskbar_wifi, "%s", LV_SYMBOL_WIFI);
    if(taskbar_statue[TASKBAR_ID_WIFI]) {
        lv_obj_clear_flag(menu_taskbar_wifi, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(menu_taskbar_wifi, LV_OBJ_FLAG_HIDDEN);
    }

    menu_taskbar_sd = lv_label_create(status_parent);
    lv_label_set_text_fmt(menu_taskbar_sd, "%s", LV_SYMBOL_SD_CARD);
    lv_obj_add_flag(menu_taskbar_sd, LV_OBJ_FLAG_HIDDEN);

    int sd_st = 0;
    ui_test_get_sd(&sd_st);
    if(sd_st == 1)
    {
        lv_obj_clear_flag(menu_taskbar_sd, LV_OBJ_FLAG_HIDDEN);
    }

    menu_taskbar_charge = lv_label_create(status_parent);
    lv_label_set_text_fmt(menu_taskbar_charge, "%s", LV_SYMBOL_CHARGE);
    if(taskbar_statue[TASKBAR_ID_CHARGE]) {
        lv_obj_clear_flag(menu_taskbar_charge, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(menu_taskbar_charge, LV_OBJ_FLAG_HIDDEN);
    }

    menu_taskbar_battery = lv_label_create(status_parent);

    menu_taskbar_battery_percent = lv_label_create(status_parent);
    lv_obj_set_style_text_font(menu_taskbar_battery_percent, &Font_Mono_Bold_25, LV_PART_MAIN);

    // menu create
    menu_screen1 = lv_obj_create(parent);
    lv_obj_set_size(menu_screen1, lv_pct(100), LV_VER_RES - status_bar_height);
    lv_obj_set_style_bg_color(menu_screen1, lv_color_hex(EPD_COLOR_BG), LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(menu_screen1, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_border_width(menu_screen1, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(menu_screen1, lv_color_hex(EPD_COLOR_FG), LV_PART_MAIN);
    lv_obj_set_style_border_side(menu_screen1, LV_BORDER_SIDE_TOP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(menu_screen1, 0, LV_PART_MAIN);
    lv_obj_align(menu_screen1, LV_ALIGN_BOTTOM_MID, 0, 0);
    // lv_obj_add_flag(menu_screen1, LV_OBJ_FLAG_HIDDEN);

    menu_screen2 = lv_obj_create(parent);
    lv_obj_set_size(menu_screen2, lv_pct(100), LV_VER_RES - status_bar_height);
    lv_obj_set_style_bg_color(menu_screen2, lv_color_hex(EPD_COLOR_BG), LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(menu_screen2, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_border_width(menu_screen2, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(menu_screen2, lv_color_hex(EPD_COLOR_FG), LV_PART_MAIN);
    lv_obj_set_style_border_side(menu_screen2, LV_BORDER_SIDE_TOP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(menu_screen2, 0, LV_PART_MAIN);
    lv_obj_align(menu_screen2, LV_ALIGN_BOTTOM_MID, 0, 0);
    // lv_obj_add_flag(menu_screen2, LV_OBJ_FLAG_HIDDEN);

    int icon_buf_len = ARRAY_LEN(icon_buf);
    int icon_buf2_len = ARRAY_LEN(icon_buf2);
    memset(springboard_icons_page1, 0, sizeof(springboard_icons_page1));
    memset(springboard_icons_page2, 0, sizeof(springboard_icons_page2));

    for(int i = 0; i < icon_buf_len; i++) {
        const char *path_used = NULL;
        bool alias_used = false;
        if (springboard_icon_png_exists(icon_buf[i].png_path)) path_used = icon_buf[i].png_path;
        else if (springboard_icon_png_exists(icon_buf[i].png_alias_path)) { path_used = icon_buf[i].png_alias_path; alias_used = true; }
        bool png_loaded = false;
        if (path_used) {
            springboard_icons_page1[i].buf = (lv_color_t *)ps_malloc(SPRINGBOARD_ICON_W * SPRINGBOARD_ICON_H * sizeof(lv_color_t));
            if (springboard_icons_page1[i].buf) {
                char reason[64] = {0};
                png_loaded = springboard_decode_png_to_buf(path_used, springboard_icons_page1[i].buf, reason, sizeof(reason));
                if (png_loaded) {
                    lv_obj_t *canvas = lv_canvas_create(menu_screen1);
                    lv_canvas_set_buffer(canvas, springboard_icons_page1[i].buf, SPRINGBOARD_ICON_W, SPRINGBOARD_ICON_H, LV_IMG_CF_TRUE_COLOR);
                    lv_obj_add_flag(canvas, LV_OBJ_FLAG_CLICKABLE);
                    lv_obj_set_style_bg_opa(canvas, LV_OPA_TRANSP, LV_PART_MAIN);
                    lv_obj_set_style_border_width(canvas, 0, LV_PART_MAIN);
                    lv_obj_set_pos(canvas, icon_buf[i].offs_x, icon_buf[i].offs_y);
                    lv_obj_add_event_cb(canvas, menu_btn_event, LV_EVENT_CLICKED, (void *)i);
                    springboard_icons_page1[i].canvas = canvas;
                    springboard_icons_page1[i].loaded_png = true;
                    lv_snprintf(springboard_icons_page1[i].source_path, sizeof(springboard_icons_page1[i].source_path), "%s", path_used);
                    Serial.printf(alias_used ? "[ICON] alias loaded %s\n" : "[ICON] loaded %s\n", path_used);
                } else {
                    Serial.printf("[ICON] decode failed %s: %s; using fallback img_test\n", path_used, reason);
                    free(springboard_icons_page1[i].buf);
                    springboard_icons_page1[i].buf = NULL;
                }
            } else Serial.printf("[ICON] decode failed %s: icon_buf_alloc_failed; using fallback img_test\n", path_used);
        }
        if (!png_loaded) {
            lv_obj_t *img = lv_img_create(menu_screen1);
            lv_obj_add_flag(img, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_style_bg_opa(img, LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_set_style_border_width(img, 0, LV_PART_MAIN);
            lv_obj_set_pos(img, icon_buf[i].offs_x, icon_buf[i].offs_y);
            lv_img_set_src(img, &img_test);
            lv_obj_add_event_cb(img, menu_btn_event, LV_EVENT_CLICKED, (void *)i);
            if (!path_used) Serial.printf("[ICON] missing %s; using fallback img_test\n", icon_buf[i].png_path);
        }

        // lv_obj_t *btn = lv_btn_create(menu_screen1);
        // lv_obj_set_size(btn, 120, 120);
        // lv_obj_set_x(btn, icon_buf[i].offs_x);
        // lv_obj_set_y(btn, icon_buf[i].offs_y);
        // lv_obj_add_event_cb(btn, menu_btn_event, LV_EVENT_CLICKED, (void *)i);
    }

    for(int i = 0; i < icon_buf2_len; i++) {
        const char *path_used = NULL;
        bool alias_used = false;
        if (springboard_icon_png_exists(icon_buf2[i].png_path)) path_used = icon_buf2[i].png_path;
        else if (springboard_icon_png_exists(icon_buf2[i].png_alias_path)) { path_used = icon_buf2[i].png_alias_path; alias_used = true; }
        bool png_loaded = false;
        if (path_used) {
            springboard_icons_page2[i].buf = (lv_color_t *)ps_malloc(SPRINGBOARD_ICON_W * SPRINGBOARD_ICON_H * sizeof(lv_color_t));
            if (springboard_icons_page2[i].buf) {
                char reason[64] = {0};
                png_loaded = springboard_decode_png_to_buf(path_used, springboard_icons_page2[i].buf, reason, sizeof(reason));
                if (png_loaded) {
                    lv_obj_t *canvas = lv_canvas_create(menu_screen2);
                    lv_canvas_set_buffer(canvas, springboard_icons_page2[i].buf, SPRINGBOARD_ICON_W, SPRINGBOARD_ICON_H, LV_IMG_CF_TRUE_COLOR);
                    lv_obj_add_flag(canvas, LV_OBJ_FLAG_CLICKABLE);
                    lv_obj_set_style_bg_opa(canvas, LV_OPA_TRANSP, LV_PART_MAIN);
                    lv_obj_set_style_border_width(canvas, 0, LV_PART_MAIN);
                    lv_obj_set_pos(canvas, icon_buf2[i].offs_x, icon_buf2[i].offs_y);
                    lv_obj_add_event_cb(canvas, menu_btn_event, LV_EVENT_CLICKED, (void *)(icon_buf_len + i));
                    springboard_icons_page2[i].canvas = canvas;
                    springboard_icons_page2[i].loaded_png = true;
                    lv_snprintf(springboard_icons_page2[i].source_path, sizeof(springboard_icons_page2[i].source_path), "%s", path_used);
                    Serial.printf(alias_used ? "[ICON] alias loaded %s\n" : "[ICON] loaded %s\n", path_used);
                } else {
                    Serial.printf("[ICON] decode failed %s: %s; using fallback img_test\n", path_used, reason);
                    free(springboard_icons_page2[i].buf);
                    springboard_icons_page2[i].buf = NULL;
                }
            } else Serial.printf("[ICON] decode failed %s: icon_buf_alloc_failed; using fallback img_test\n", path_used);
        }
        if (!png_loaded) {
            lv_obj_t *img = lv_img_create(menu_screen2);
            lv_obj_add_flag(img, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_style_bg_opa(img, LV_OPA_TRANSP, LV_PART_MAIN);
            lv_obj_set_style_border_width(img, 0, LV_PART_MAIN);
            lv_obj_set_pos(img, icon_buf2[i].offs_x, icon_buf2[i].offs_y);
            lv_img_set_src(img, &img_test);
            lv_obj_add_event_cb(img, menu_btn_event, LV_EVENT_CLICKED, (void *)(icon_buf_len + i));
            if (!path_used) Serial.printf("[ICON] missing %s; using fallback img_test\n", icon_buf2[i].png_path);
        }

        // lv_obj_t *btn = lv_btn_create(menu_screen2);
        // lv_obj_set_size(btn, 120, 120);
        // lv_obj_set_x(btn, icon_buf[i].offs_x);
        // lv_obj_set_y(btn, icon_buf[i].offs_y);
        // lv_obj_add_event_cb(btn, menu_btn_event, LV_EVENT_CLICKED, (void *)(icon_buf_len + i));
    }

    ui_Panel4 = lv_obj_create(parent);
    lv_obj_set_width(ui_Panel4, 240);
    lv_obj_set_height(ui_Panel4, 35);
    lv_obj_set_align(ui_Panel4, LV_ALIGN_BOTTOM_MID);
    lv_obj_set_flex_flow(ui_Panel4, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ui_Panel4, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(ui_Panel4, LV_OBJ_FLAG_SCROLLABLE);      /// Flags
    lv_obj_set_style_radius(ui_Panel4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_Panel4, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_Panel4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_Panel4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(ui_Panel4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(ui_Panel4, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_Panel4, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(ui_Panel4, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_spread(ui_Panel4, 0, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);

    lv_obj_t *ui_Button11 = lv_btn_create(ui_Panel4);
    lv_obj_set_width(ui_Button11, 15);
    lv_obj_set_height(ui_Button11, 15);
    lv_obj_add_flag(ui_Button11, LV_OBJ_FLAG_SCROLL_ON_FOCUS);     /// Flags
    lv_obj_clear_flag(ui_Button11, LV_OBJ_FLAG_CHECKABLE);      /// Flags
    lv_obj_set_style_radius(ui_Button11, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_Button11, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_Button11, lv_color_hex(EPD_COLOR_FG), LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *ui_Button12 = lv_btn_create(ui_Panel4);
    lv_obj_set_width(ui_Button12, 15);
    lv_obj_set_height(ui_Button12, 15);
    lv_obj_add_flag(ui_Button12, LV_OBJ_FLAG_SCROLL_ON_FOCUS);     /// Flags
    lv_obj_clear_flag(ui_Button12, LV_OBJ_FLAG_CHECKABLE);      /// Flags
    lv_obj_set_style_radius(ui_Button12, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ui_Button12, 2, LV_PART_MAIN | LV_STATE_DEFAULT);

    if(page_curr == 1) {
        lv_obj_clear_flag(menu_screen2, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(menu_screen1, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(lv_obj_get_child(ui_Panel4, 0), lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(lv_obj_get_child(ui_Panel4, 1), lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    } else if(page_curr == 0) {
        lv_obj_clear_flag(menu_screen1, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(menu_screen2, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(lv_obj_get_child(ui_Panel4, 0), lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(lv_obj_get_child(ui_Panel4, 1), lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}
static void entry0(void) {
    lv_timer_resume(taskbar_update_timer);

    lv_obj_add_event_cb(scr_mgr_get_top_obj(), menu_gesture_event, LV_EVENT_GESTURE, NULL);

    uint8_t h, m, s;
    char time_buf[16] = {0};
    ui_clock_get_time(&h, &m, &s);
    format_time_12h(h, m, time_buf, sizeof(time_buf), NULL);
    lv_label_set_text_fmt(menu_taskbar_time, "%s", time_buf);

    lv_label_set_text_fmt(menu_taskbar_battery, "%s", ui_battert_27220_get_percent_level());

    lv_label_set_text_fmt(menu_taskbar_battery_percent, "%d", ui_battery_27220_get_percent());
}
static void exit0(void) {
    lv_timer_pause(taskbar_update_timer);
}
static void destroy0(void) 
{
    for (size_t i = 0; i < ARRAY_LEN(springboard_icons_page1); ++i) {
        if (springboard_icons_page1[i].buf) { free(springboard_icons_page1[i].buf); springboard_icons_page1[i].buf = NULL; }
        springboard_icons_page1[i].canvas = NULL;
        springboard_icons_page1[i].loaded_png = false;
        springboard_icons_page1[i].source_path[0] = '\0';
    }
    for (size_t i = 0; i < ARRAY_LEN(springboard_icons_page2); ++i) {
        if (springboard_icons_page2[i].buf) { free(springboard_icons_page2[i].buf); springboard_icons_page2[i].buf = NULL; }
        springboard_icons_page2[i].canvas = NULL;
        springboard_icons_page2[i].loaded_png = false;
        springboard_icons_page2[i].source_path[0] = '\0';
    }
}

static scr_lifecycle_t screen0 = {
    .create = create0,
    .entry =   entry0,
    .exit  =   exit0,
    .destroy = destroy0,
};
#endif
//************************************[ screen 1 ]****************************************** clock
#if 1
static lv_obj_t  * calendar;
static lv_timer_t *get_timer = NULL;
static lv_meter_indicator_t * indic_min;
static lv_meter_indicator_t * indic_hour;
static lv_obj_t *clock_time;
static lv_obj_t *clock_data;
static lv_obj_t *clock_ap;
static lv_obj_t *clock_month;
static const char *week_list_en[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char * month_names_def[12] = LV_CALENDAR_DEFAULT_MONTH_NAMES;
static void format_time_12h(uint8_t h24, uint8_t m, char *buf, size_t len, const char **ampm)
{
    uint8_t h12 = h24 % 12;
    if(h12 == 0) h12 = 12;
    if(ampm) *ampm = (h24 >= 12) ? "P.M." : "A.M.";
    lv_snprintf(buf, len, "%02d:%02d", h12, m);
}

static bool get_refresh_data(void)
{
    uint8_t h, m, s;
    uint8_t year, mont, day, week;

    ui_clock_get_time(&h, &m, &s);
    ui_clock_get_data(&year, &mont, &day, &week);

    char time_buf[16] = {0};
    const char *ampm = NULL;
    format_time_12h(h, m, time_buf, sizeof(time_buf), &ampm);
    lv_label_set_text_fmt(clock_ap, "%s", ampm);

    lv_calendar_set_today_date(calendar, 2000+year, mont, day);
    lv_calendar_set_showed_date(calendar, 2000+year, mont);
    lv_label_set_text_fmt(clock_month, "%s", month_names_def[mont-1]);

    lv_label_set_text_fmt(clock_time, "%s", time_buf);
    lv_label_set_text_fmt(clock_data, "20%02d-%02d-%02d  %s", year, mont, day, week_list_en[week]);

    printf("%2d:%2d:%02d-%d/%d/%d\n", h, m, s, year, mont, day);

    return year;
}

static void get_timer_event(lv_timer_t *t) 
{
    // refresh time per 65s
    bool is_ref = get_refresh_data();
    if(is_ref) {
        lv_timer_set_period(get_timer, 65*1000);
    }
}

static void scr1_btn_event_cb(lv_event_t * e)
{
    if(e->code == LV_EVENT_CLICKED){
        // ui_full_refresh();
        scr_mgr_pop(false);
    }
}

static void create1(lv_obj_t *parent) {
    clock_time = lv_label_create(parent);
    clock_data = lv_label_create(parent);
    clock_ap = lv_label_create(parent);
    clock_month = lv_label_create(parent);

    lv_obj_set_style_border_width(clock_data, 2, 0);
    lv_obj_set_style_pad_top(clock_data, 30, 0);
    lv_obj_set_style_border_side(clock_data, LV_BORDER_SIDE_TOP, LV_PART_MAIN);

    lv_obj_set_style_text_font(clock_time, &Font_Mono_Bold_90, LV_PART_MAIN);
    lv_obj_set_style_text_font(clock_data, &Font_Mono_Bold_30, LV_PART_MAIN);
    lv_obj_set_style_text_font(clock_ap, &Font_Mono_Bold_30, LV_PART_MAIN);
    lv_obj_set_style_text_font(clock_month, &Font_Mono_Bold_30, LV_PART_MAIN);

    //---------------------
    calendar = lv_calendar_create(parent);
    lv_obj_set_size(calendar, 430, 380);
    lv_obj_set_style_text_font(calendar, &Font_Geist_Bold_20, LV_PART_MAIN);

    lv_obj_set_style_border_width(calendar, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(lv_calendar_get_btnmatrix(calendar), 0, LV_PART_ITEMS);
    lv_obj_set_style_border_side(lv_calendar_get_btnmatrix(calendar), LV_BORDER_SIDE_TOP, LV_PART_MAIN);

    //---------------------
    // scr_middle_line(parent);

    // back
    scr_back_btn_create(parent, "Clock", scr1_btn_event_cb); 
}
static void entry1(void) {
    // refresh time
    bool is_ref = get_refresh_data();
    if(is_ref) {
        get_timer = lv_timer_create(get_timer_event, 60*1000, NULL);
    } else {
        get_timer = lv_timer_create(get_timer_event, 6000, NULL);
    }
    
    // layout
    lv_obj_set_style_text_align(clock_time, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_align(clock_data, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_align(clock_ap, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    lv_obj_align(calendar, LV_ALIGN_TOP_MID, 0, 130);
    lv_obj_align_to(clock_month, calendar, LV_ALIGN_OUT_TOP_RIGHT, 0, -5);
    lv_obj_align_to(clock_time, calendar, LV_ALIGN_OUT_BOTTOM_MID, 0, 100);
    lv_obj_align_to(clock_data, clock_time, LV_ALIGN_OUT_BOTTOM_MID, 0, 30);
    lv_obj_align_to(clock_ap, clock_time, LV_ALIGN_OUT_RIGHT_MID, 0, 20);
}
static void exit1(void) {
    if(get_timer) {
        lv_timer_del(get_timer);
        get_timer = NULL;
    }
}
static void destroy1(void) { }

static scr_lifecycle_t screen1 = {
    .create = create1,
    .entry = entry1,
    .exit  = exit1,
    .destroy = destroy1,
};
#endif
//************************************[ screen 2 ]****************************************** lora
// --------------------- screen --------------------- lora
#if 1
lv_obj_t * scr2_list;
static lv_obj_t *scr2_lab_buf[20];

static void scr2_list_event(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * obj = lv_event_get_target(e);
    for(int i = 0; i < lv_obj_get_child_cnt(obj); i++) 
    {
        lv_obj_t * child = lv_obj_get_child(obj, i);
        if(lv_obj_check_type(child, &lv_label_class)) {
            char *str = lv_label_get_text(child);

            if(strcmp("-Auto Test", str) == 0)
            {
                scr_mgr_push(SCREEN2_1_ID, false);
            }
            if(strcmp("-Manual Test", str) == 0)
            {
                scr_mgr_push(SCREEN2_2_ID, false);
            }
            if(strcmp("-Lora Setting", str) == 0)
            {
                scr_mgr_push(SCREEN2_3_ID, false);
            }
            printf("%s\n", str);
        }
    }
}

static lv_obj_t * scr2_create_label(lv_obj_t *parent)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_width(label, LCD_HOR_SIZE/2-50);
    lv_obj_set_style_text_font(label, &Font_Mono_Bold_25, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(EPD_COLOR_FG), LV_PART_MAIN | LV_STATE_DEFAULT);   
    // lv_obj_set_style_border_width(label, 1, LV_PART_MAIN);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    return label;
}

static void scr2_btn_event_cb(lv_event_t * e)
{
    if(e->code == LV_EVENT_CLICKED){
        // ui_full_refresh();
        scr_mgr_pop(false);
    }
}

static void scr2_item_create(const char *name, lv_event_cb_t cb)
{
    lv_obj_t * obj = lv_obj_class_create_obj(&lv_list_btn_class, scr2_list);
    lv_obj_class_init_obj(obj);
    lv_obj_set_size(obj, LV_PCT(100), LV_SIZE_CONTENT);

    lv_obj_t *label = lv_label_create(obj);
    lv_label_set_text(label, name);
    lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 10, 0);

    lv_obj_set_height(obj, 130);
    lv_obj_set_style_text_font(obj, &Font_Mono_Bold_30, LV_PART_MAIN);
    lv_obj_set_style_bg_color(obj, lv_color_hex(EPD_COLOR_BG), LV_PART_MAIN);
    lv_obj_set_style_text_color(obj, lv_color_hex(EPD_COLOR_FG), LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(obj, 3, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_outline_width(obj, 0, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_radius(obj, 30, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_add_event_cb(obj, cb, LV_EVENT_CLICKED, NULL); 
}

static void create2(lv_obj_t *parent) 
{
    scr2_list = lv_list_create(parent);
    lv_obj_set_size(scr2_list, lv_pct(93), lv_pct(91));
    lv_obj_align(scr2_list, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(scr2_list, lv_color_hex(EPD_COLOR_BG), LV_PART_MAIN);
    lv_obj_set_style_pad_top(scr2_list, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_row(scr2_list, 15, LV_PART_MAIN);
    lv_obj_set_style_radius(scr2_list, 0, LV_PART_MAIN);
    // lv_obj_set_style_outline_pad(scr2_list, 2, LV_PART_MAIN);
    lv_obj_set_style_border_width(scr2_list, 0, LV_PART_MAIN);
    lv_obj_set_style_border_color(scr2_list, lv_color_hex(EPD_COLOR_FG), LV_PART_MAIN);
    lv_obj_set_style_shadow_width(scr2_list, 0, LV_PART_MAIN);

    scr2_item_create("-Auto Test", scr2_list_event);
    scr2_item_create("-Manual Test", scr2_list_event);
    scr2_item_create("-Lora Setting", scr2_list_event);

    // back
    scr_back_btn_create(parent, "Lora", scr2_btn_event_cb);
}

static void entry2(void) 
{
}

static void exit2(void) { }
static void destroy2(void) { }

static scr_lifecycle_t screen2 = { 
    .create = create2,
    .entry = entry2,
    .exit  = exit2,
    .destroy = destroy2,
};
#endif
// --------------------- screen 2.1 --------------------- Auto Send
#if 1
static lv_obj_t *scr2_1_cont;
static lv_timer_t *scr2_1_timer = NULL;
static lv_obj_t *scr2_1_sw_btn;
static lv_obj_t *scr2_1_sw_btn_info;
static int scr2_1_cnt = 0;

static void scr2_1_btn_event_cb(lv_event_t * e)
{
    if(e->code == LV_EVENT_CLICKED){
        scr_mgr_pop(false);
    }
}

static void lora_auto_send_event(lv_event_t * e)
{
    if(e->code == LV_EVENT_CLICKED){
        if(ui_lora_get_mode() == LORA_MODE_SEND) {
            ui_lora_set_mode(LORA_MODE_RECV);
            lv_label_set_text(scr2_1_sw_btn_info, "Recv");
            ui_lora_recv_resume();
        } else if(ui_lora_get_mode() == LORA_MODE_RECV) {
            ui_lora_set_mode(LORA_MODE_SEND);
            lv_label_set_text(scr2_1_sw_btn_info, "Send");
            ui_lora_recv_suspend();
        }
    }
    for(int i = 0; i < ARRAY_LEN(scr2_lab_buf); i++){
        lv_label_set_text_fmt(scr2_lab_buf[i], " ", i);
    }
    scr2_1_cnt = 0;
}

static void lora_timer_event(lv_timer_t *t)
{
    static int data = 0;
    char buf[32];
    const char *recv_info = NULL;
    int recv_rssi = 0;

    if(ui_lora_get_mode() == LORA_MODE_SEND) 
    {
        scr2_1_cnt++;
        if(scr2_1_cnt >= ARRAY_LEN(scr2_lab_buf)) {
            for(int i = 0; i < ARRAY_LEN(scr2_lab_buf); i++){
                lv_label_set_text_fmt(scr2_lab_buf[i], " ", i);
            }
            scr2_1_cnt = 0;
        }

        lv_snprintf(buf, 32, "# %d T5-EPaper-S3", data++);
        lv_label_set_text_fmt(scr2_lab_buf[scr2_1_cnt], "send-> %s", buf);
        ui_lora_send(buf);
    }
    else if(ui_lora_get_mode() == LORA_MODE_RECV)
    {
        if(ui_lora_recv(&recv_info, &recv_rssi))
        {
            scr2_1_cnt++;
            if(scr2_1_cnt >= ARRAY_LEN(scr2_lab_buf)) {
                for(int i = 0; i < ARRAY_LEN(scr2_lab_buf); i++){
                    lv_label_set_text_fmt(scr2_lab_buf[i], " ", i);
                }
                scr2_1_cnt = 0;
            }
            ui_lora_clean_recv_flag();
            lv_label_set_text_fmt(scr2_lab_buf[scr2_1_cnt], "recv-> %s [%d]", recv_info, recv_rssi);
        }
    }
}

static lv_obj_t * scr2_1_create_label(lv_obj_t *parent)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_width(label, LCD_HOR_SIZE/2-50);
    lv_obj_set_style_text_font(label, &Font_Mono_Bold_25, LV_PART_MAIN);
    // lv_obj_set_style_border_width(label, 1, LV_PART_MAIN);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    return label;
}

static void create2_1(lv_obj_t *parent) 
{
    scr2_1_cont = lv_obj_create(parent);
    lv_obj_set_size(scr2_1_cont, lv_pct(98), lv_pct(80));
    lv_obj_set_style_bg_color(scr2_1_cont, lv_color_hex(EPD_COLOR_BG), LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(scr2_1_cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(scr2_1_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(scr2_1_cont, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_all(scr2_1_cont, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(scr2_1_cont, 20, LV_PART_MAIN);
    lv_obj_set_flex_flow(scr2_1_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_top(scr2_1_cont, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_row(scr2_1_cont, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_column(scr2_1_cont, 5, LV_PART_MAIN);
    lv_obj_align(scr2_1_cont, LV_ALIGN_BOTTOM_MID, 0, -20);

    lv_obj_t *scr2_1_info = lv_label_create(parent);
    lv_obj_set_style_text_font(scr2_1_info, &Font_Mono_Bold_25, LV_PART_MAIN);
    lv_obj_set_style_text_align(scr2_1_info, LV_TEXT_ALIGN_LEFT, 0);
                                    // "Frequery:***MHz    Bandwidth:***KHz\n"
    lv_label_set_text_fmt(scr2_1_info, "Freq: %.0fMHz    BD: %dKHz\n"
                                       "Power: %d       Spread: %d",
                                        ui_lora_get_freq(),
                                        ui_lora_get_bandwidth(),
                                        ui_lora_get_power(),
                                        ui_lora_get_spread_factor());
    lv_obj_set_style_border_width(scr2_1_info, 0, LV_PART_MAIN);
    lv_obj_align(scr2_1_info, LV_ALIGN_TOP_MID, 0, 85);

    for(int i = 0; i < ARRAY_LEN(scr2_lab_buf); i++) {
        scr2_lab_buf[i] = scr2_1_create_label(scr2_1_cont);
        lv_obj_set_width(scr2_lab_buf[i], LV_SIZE_CONTENT);   /// 1
        lv_obj_set_height(scr2_lab_buf[i], LV_SIZE_CONTENT);    /// 1
        lv_obj_set_style_border_width(scr2_lab_buf[i], 0, LV_PART_MAIN);
        lv_label_set_long_mode(scr2_lab_buf[i], LV_LABEL_LONG_DOT);
        lv_label_set_text_fmt(scr2_lab_buf[i], " ", i);
    }

    scr2_1_sw_btn = lv_btn_create(parent);
    lv_obj_set_size(scr2_1_sw_btn, 100, 50);
    lv_obj_set_style_radius(scr2_1_sw_btn, 5, LV_PART_MAIN);
    lv_obj_set_style_border_width(scr2_1_sw_btn, 2, LV_PART_MAIN);
    scr2_1_sw_btn_info = lv_label_create(scr2_1_sw_btn);
    lv_obj_set_style_text_font(scr2_1_sw_btn_info, &Font_Mono_Bold_25, LV_PART_MAIN);
    lv_obj_set_style_text_align(scr2_1_sw_btn_info, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text_fmt(scr2_1_sw_btn_info, "%s", (ui_lora_get_mode() == LORA_MODE_SEND ? "Send" : "Recv"));
    lv_obj_center(scr2_1_sw_btn_info);
    lv_obj_align(scr2_1_sw_btn, LV_ALIGN_TOP_RIGHT, -10, 20);
    lv_obj_add_event_cb(scr2_1_sw_btn, lora_auto_send_event, LV_EVENT_CLICKED, NULL);

    scr_back_btn_create(parent, ("Auto Send"), scr2_1_btn_event_cb);
}
static void entry2_1(void) 
{
    scr2_1_cnt = 0;
    scr2_1_timer = lv_timer_create(lora_timer_event, 3000, NULL);
}
static void exit2_1(void) 
{
    if(scr2_1_timer) {
        lv_timer_del(scr2_1_timer);
        scr2_1_timer = NULL;
    }
}
static void destroy2_1(void) { }

static scr_lifecycle_t screen2_1 = {
    .create = create2_1,
    .entry = entry2_1,
    .exit  = exit2_1,
    .destroy = destroy2_1,
};
#endif
// --------------------- screen 2.2 --------------------- Manual Send
#if 1
#define LORA_RECV_INFO_MAX_LINE 12
#define MANUAL_SEND_LINE_MAX 12
#define MANUAL_SEND_LINE_MAX_CH 34

static int lora_mode_st = LORA_MODE_SEND;
static lv_obj_t *lora_mode_lab;
// static lv_obj_t *lora_mode_sw;
static lv_obj_t *keyborad;
static lv_obj_t *textarea;
static lv_obj_t *cnt_label;
static lv_timer_t *lora_send_timer = NULL;

static int send_cnt = 0;
static int recv_cnt = 0;
static int lora_lab_cnt = 0;
int lab_idx = 0;

static lv_obj_t *scr2_2_cont_info;

static void lora_send_timer_event(lv_timer_t *t)
{
    if(lora_mode_st == LORA_MODE_SEND) return;
    
    String str = "";

    // if(lora_recv_success) {
    //     lora_recv_success = false;

    //     recv_cnt += strlen(lora_recv_data.c_str());

    //     lv_label_set_text_fmt(cnt_label, "R:%d", recv_cnt);

    //     str += lora_recv_data;

    //     lv_label_set_text_fmt(scr2_lab_buf[0], "RECV: %ddBm", lora_recv_rssi);

    //     if(scr2_lab_buf[lora_lab_cnt] == NULL) {
    //         scr2_lab_buf[lora_lab_cnt] = scr2_create_label(scr2_cont_info);
    //         lv_label_set_text(scr2_lab_buf[lora_lab_cnt], str.c_str());
    //     } else {
    //         lv_label_set_text(scr2_lab_buf[lora_lab_cnt], str.c_str());
    //     }

    //     lora_lab_cnt++;
    //     if(lora_lab_cnt >= LORA_RECV_INFO_MAX_LINE) {
    //         lora_lab_cnt = 1;
    //     }
    // }
}

static void lora_mode_sw_event(lv_event_t * e)
{
    if(lora_mode_st == LORA_MODE_SEND)
    {
        lora_mode_st = LORA_MODE_RECV;
        lv_obj_add_flag(keyborad, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(textarea, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(scr2_2_cont_info, LV_OBJ_FLAG_HIDDEN);
        lv_timer_resume(lora_send_timer);
        lora_lab_cnt = 1;

        ui_lora_set_mode(LORA_MODE_RECV);
    } 
    else if(lora_mode_st == LORA_MODE_RECV) 
    {
        lora_mode_st = LORA_MODE_SEND;
        lv_obj_clear_flag(keyborad, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(textarea, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(scr2_2_cont_info, LV_OBJ_FLAG_HIDDEN);
        lv_timer_pause(lora_send_timer);

        ui_lora_set_mode(LORA_MODE_SEND);
    }

    lv_label_set_text_fmt(lora_mode_lab, "MODE : %s", (lora_mode_st == LORA_MODE_SEND)? "SEND" : "RECV");
}

static void ta_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * ta = lv_event_get_target(e);
    lv_obj_t * kb = (lv_obj_t *)lv_event_get_user_data(e);

    if(code == LV_EVENT_VALUE_CHANGED)
    {
        printf("LV_EVENT_VALUE_CHANGED\n");
        return;
    }

    // if(code == LV_EVENT_READY)
    // {
    //     printf("LV_EVENT_READY\n");
    //     int ret = 0;
    //     ui_test_get_lora(&ret);
    //     if(ret == true) 
    //     {
    //         const char *str = lv_textarea_get_text(ta);
    //         int str_len = strlen(str);

    //         send_cnt += str_len;
    //         lv_label_set_text_fmt(cnt_label, "S:%d", send_cnt);
    //         // ui_lora_transmit(str);
    //     }
    //     else 
    //     {
    //         printf("Not found LORA\n");
    //     }
    //     lv_textarea_set_text(ta,"");
    // }


    if(code == LV_EVENT_READY)
    {
        char *str = (char *)lv_textarea_get_text(ta);
        int str_len = strlen(str);

        send_cnt += str_len;
        lv_label_set_text_fmt(cnt_label, "S:%d", send_cnt);

        printf("lab_idx=%d, mode=%d, len=%d, %s\n", lab_idx, lora_mode_st, str_len, str);

        ui_lora_send(str);

        static bool negation = true;

        // if(negation)
        // {
            lv_label_set_text_fmt(scr2_lab_buf[lab_idx], "S:%s", str);
        // } else {
        //     char buf[MANUAL_SEND_LINE_MAX_CH];
        //     lv_snprintf(buf, MANUAL_SEND_LINE_MAX_CH, "%s:R", str);
        //     int len = strlen(buf);
        //     int i;
        //     for(i = 0; i < MANUAL_SEND_LINE_MAX_CH-len; i++)
        //     {
        //         buf[i] = ' ';
        //     }

        //     printf("len=%d, i=%d\n", len, i);
        //     strcpy(&buf[i], str);
        //     lv_label_set_text_fmt(scr2_lab_buf[lab_idx], "%s:R", buf);
        // }
        // negation = !negation;

        lab_idx++;
        if(lab_idx >= MANUAL_SEND_LINE_MAX) {
            lab_idx = 0;
        }
        lv_textarea_set_text(ta,"");
    }

    ui_refresh_set_mode(UI_REFRESH_MODE_FAST);
}

static void scr2_2_btn_event_cb(lv_event_t * e)
{
    if(e->code == LV_EVENT_CLICKED){
        scr_mgr_pop(false);
    }
}

static lv_obj_t * scr2_2_create_label(lv_obj_t *parent)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_width(label, lv_pct(99));
    lv_obj_set_style_text_font(label, &Font_Mono_Bold_25, LV_PART_MAIN);   
    lv_obj_set_style_border_width(label, 0, LV_PART_MAIN);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_border_side(label, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(label, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    return label;
}


static void create2_2(lv_obj_t *parent) 
{
    /*Create a keyboard to use it with an of the text areas*/
    keyborad = lv_keyboard_create(parent);
    lv_obj_set_height(keyborad, lv_pct(40));
    lv_obj_align(keyborad, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_border_width(keyborad, 0, LV_PART_MAIN);
    keyboard_disable_long_press_repeat(keyborad);

    /*Create a text area. The keyboard will write here*/
    textarea = lv_textarea_create(parent);
    lv_obj_set_style_text_font(textarea, &Font_Mono_Bold_25, LV_PART_MAIN);  
    lv_obj_add_event_cb(textarea, ta_event_cb, LV_EVENT_VALUE_CHANGED, keyborad);
    lv_obj_add_event_cb(textarea, ta_event_cb, LV_EVENT_READY, keyborad);
    lv_obj_set_size(textarea, lv_pct(98), lv_pct(6));
    // lv_obj_add_state(textarea, LV_STATE_FOCUSED);       /// States
    lv_obj_clear_flag(textarea, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_text_letter_space(textarea, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_line_space(textarea, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_keyboard_set_textarea(keyborad, textarea);
    // lv_obj_set_style_border_width(textarea, 0, LV_PART_MAIN);
    // lv_obj_set_style_shadow_width(textarea, 0, LV_PART_MAIN);
    // lv_obj_set_style_outline_width(textarea, 0, LV_PART_MAIN);
    lv_obj_align_to(textarea, keyborad, LV_ALIGN_OUT_TOP_MID, 0, -5);

    scr2_2_cont_info = lv_obj_create(parent);
    
    lv_obj_set_size(scr2_2_cont_info, lv_pct(100), lv_pct(45));
    lv_obj_set_style_bg_color(scr2_2_cont_info, lv_color_hex(EPD_COLOR_BG), LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(scr2_2_cont_info, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(scr2_2_cont_info, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(scr2_2_cont_info, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_all(scr2_2_cont_info, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(scr2_2_cont_info, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_top(scr2_2_cont_info, 10, LV_PART_MAIN);
    lv_obj_set_flex_flow(scr2_2_cont_info, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(scr2_2_cont_info, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_column(scr2_2_cont_info, 0, LV_PART_MAIN);
    lv_obj_align_to(scr2_2_cont_info, textarea, LV_ALIGN_OUT_TOP_MID, 0, 0);

    for(int i = 0; i < MANUAL_SEND_LINE_MAX; i++) {
        scr2_lab_buf[i] = scr2_2_create_label(scr2_2_cont_info);
        lv_label_set_text(scr2_lab_buf[i], ":");
    }
    // 
    // lora_mode_sw = lv_btn_create(parent);
    // lv_obj_set_style_radius(lora_mode_sw, 5, LV_PART_MAIN);
    // lv_obj_set_style_border_width(lora_mode_sw, 2, LV_PART_MAIN);
    // lora_mode_lab = lv_label_create(lora_mode_sw);
    // lv_obj_set_style_text_font(lora_mode_lab, &Font_Mono_Bold_25, LV_PART_MAIN);
    // lv_obj_align(lora_mode_sw, LV_ALIGN_TOP_MID, 0, 22);
    // lv_obj_add_event_cb(lora_mode_sw, lora_mode_sw_event, LV_EVENT_CLICKED, NULL);

    cnt_label = lv_label_create(parent);
    lv_obj_set_style_text_font(cnt_label, &Font_Mono_Bold_25, LV_PART_MAIN);
    lv_obj_align(cnt_label, LV_ALIGN_TOP_RIGHT, -30, 22);
    lv_label_set_text_fmt(cnt_label, "S:%d", send_cnt);

    scr_back_btn_create(parent, ("Manual Send"), scr2_2_btn_event_cb);
}
static void entry2_2(void) 
{
    lora_lab_cnt = 0;

    ui_setting_get_refresh_speed(&scr_refresh_mode);

    if(ui_lora_get_mode() == LORA_MODE_RECV) {
        ui_lora_set_mode(LORA_MODE_SEND);
    }
}
static void exit2_2(void) 
{
    ui_refresh_set_mode(scr_refresh_mode);
}
static void destroy2_2(void) { }

static scr_lifecycle_t screen2_2 = {
    .create = create2_2,
    .entry = entry2_2,
    .exit  = exit2_2,
    .destroy = destroy2_2,
};
#endif
// --------------------- screen 2.3 --------------------- Lora Setting
#if 1

#define RADIO_FREQUENCY_LIST "433MHz\n 850MHz\n 868MHz\n 915MHz\n 920MHz"
#define RADIO_BANDWIDTH "125KHz\n 250KHz\n 500KHz"
#define RADIO_TX_POWER "10dBm\n 22dBm"

static float lora_freq_list[] = {433.0, 850.0, 868.0, 915.0, 920.0};
static int lora_band_list[] = {125, 250, 500};
static int lora_power_list[] = {10, 22};

static lv_obj_t *scr2_3_cont;
static lv_obj_t *dropdown_freq;
static lv_obj_t *dropdown_band;
static lv_obj_t *dropdown_power;

static void scr2_3_btn_event_cb(lv_event_t * e)
{
    if(e->code == LV_EVENT_CLICKED){
        scr_mgr_pop(false);
    }
}

static void lora_setting_event_handler(lv_event_t * e)
{
    char buf[32]={0};
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * obj = lv_event_get_target(e);
    const char *flag = ( const char *)lv_event_get_user_data(e);
    int select = lv_dropdown_get_selected(obj);

    lv_dropdown_get_selected_str(obj, buf, sizeof(buf));
    switch (*flag)
    {
    case 'f': 
        for(int i = 0; i < ARRAY_LEN(lora_freq_list); i++) {
            if(lora_freq_list[select] == lora_freq_list[i]) {
                printf("set freq %.1fMHz\n", lora_freq_list[i]);
                ui_lora_set_freq(lora_freq_list[i]);
            }
        }
        break;
    case 'b': 
        for(int i = 0; i < ARRAY_LEN(lora_band_list); i++) {
            if(lora_band_list[select] == lora_band_list[i]) {
                printf("set bandwidth %dKhz\n", lora_band_list[i]);
                ui_lora_set_bandwidth(lora_band_list[i]);
            }
        }
        break;
    case 'p': 
        for(int i = 0; i < ARRAY_LEN(lora_power_list); i++) {
            if(lora_power_list[select] == lora_power_list[i]) {
                printf("set power %ddBm\n", lora_power_list[i]);
                ui_lora_set_power(lora_power_list[i]);
            }
        }
        break;
    
    default:
        break;
    }
}

static lv_obj_t * scr2_3_lora_setting_create(lv_obj_t *parent, const char *text)
{
    lv_obj_t *ui_Container1 = lv_obj_create(parent);
    lv_obj_remove_style_all(ui_Container1);
    lv_obj_set_height(ui_Container1, lv_pct(7));
    lv_obj_set_width(ui_Container1, lv_pct(100));
    lv_obj_set_x(ui_Container1, 35);
    lv_obj_set_y(ui_Container1, -16);
    lv_obj_set_align(ui_Container1, LV_ALIGN_CENTER);
    lv_obj_set_flex_flow(ui_Container1, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ui_Container1, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(ui_Container1, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);      /// Flags
    lv_obj_set_style_pad_row(ui_Container1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(ui_Container1, 20, LV_PART_MAIN | LV_STATE_DEFAULT);
    // lv_obj_set_style_border_width(ui_Container1, 3, LV_PART_MAIN);

    lv_obj_t *ui_Label14 = lv_label_create(ui_Container1);
    lv_obj_set_width(ui_Label14, LV_SIZE_CONTENT);   /// 1
    lv_obj_set_height(ui_Label14, LV_SIZE_CONTENT);    /// 1
    lv_obj_set_x(ui_Label14, -60);
    lv_obj_set_y(ui_Label14, -42);
    lv_obj_set_align(ui_Label14, LV_ALIGN_CENTER);
    lv_label_set_text(ui_Label14, text);
    lv_obj_set_style_text_font(ui_Label14, &Font_Mono_Bold_25, LV_PART_MAIN);   

    lv_obj_t *ui_Dropdown1 = lv_dropdown_create(ui_Container1);
    lv_obj_set_width(ui_Dropdown1, lv_pct(60));
    lv_obj_set_height(ui_Dropdown1, LV_SIZE_CONTENT);    /// 1
    lv_obj_set_x(ui_Dropdown1, 19);
    lv_obj_set_y(ui_Dropdown1, -1);
    lv_obj_add_flag(ui_Dropdown1, LV_OBJ_FLAG_SCROLL_ON_FOCUS);     /// Flags
    // lv_obj_set_style_text_font(ui_Label14, &Font_Mono_Bold_25, LV_PART_ITEMS);  

    // lv_obj_set_style_bg_opa(ui_Dropdown1, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_width(ui_Dropdown1, 1, LV_PART_MAIN | LV_STATE_PRESSED);
    // lv_obj_set_style_shadow_width(ui_Dropdown1, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_PRESSED);

    return ui_Dropdown1;
}

static void create2_3(lv_obj_t *parent) 
{
    scr2_3_cont = lv_obj_create(parent);
    lv_obj_remove_style_all(scr2_3_cont);
    lv_obj_set_width(scr2_3_cont, lv_pct(100));
    lv_obj_set_height(scr2_3_cont, lv_pct(85));
    lv_obj_set_align(scr2_3_cont, LV_ALIGN_CENTER);
    lv_obj_set_flex_flow(scr2_3_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scr2_3_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(scr2_3_cont, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);      /// Flags
    lv_obj_set_style_pad_row(scr2_3_cont, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(scr2_3_cont, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    // lv_obj_set_style_border_width(scr2_3_cont, 3, LV_PART_MAIN);
    lv_obj_set_align(scr2_3_cont, LV_ALIGN_BOTTOM_MID);

    dropdown_freq = scr2_3_lora_setting_create(scr2_3_cont, "Freq: ");
    lv_dropdown_set_options(dropdown_freq, RADIO_FREQUENCY_LIST);
    for(int i = 0; i < ARRAY_LEN(lora_freq_list); i++) {
        if(ui_lora_get_freq() == lora_freq_list[i]) {
            lv_dropdown_set_selected(dropdown_freq, i);
        }
    }

    dropdown_band = scr2_3_lora_setting_create(scr2_3_cont, "Band: ");
    lv_dropdown_set_options(dropdown_band, RADIO_BANDWIDTH);
    for(int i = 0; i < ARRAY_LEN(lora_band_list); i++) {
        if(ui_lora_get_bandwidth() == lora_band_list[i]) {
            lv_dropdown_set_selected(dropdown_band, i);
        }
    }

    dropdown_power = scr2_3_lora_setting_create(scr2_3_cont, "Power:");
    lv_dropdown_set_options(dropdown_power, RADIO_TX_POWER);
    for(int i = 0; i < ARRAY_LEN(lora_power_list); i++) {
        if(ui_lora_get_power() == lora_power_list[i]) {
            lv_dropdown_set_selected(dropdown_power, i);
        }
    }

    static const char freq_flag = 'f';
    static const char band_flag = 'b';
    static const char power_flag = 'p';
    lv_obj_add_event_cb(dropdown_freq, lora_setting_event_handler, LV_EVENT_VALUE_CHANGED, (void *)&freq_flag);
    lv_obj_add_event_cb(dropdown_band, lora_setting_event_handler, LV_EVENT_VALUE_CHANGED, (void *)&band_flag);
    lv_obj_add_event_cb(dropdown_power,   lora_setting_event_handler, LV_EVENT_VALUE_CHANGED, (void *)&power_flag);
    // back
    scr_back_btn_create(parent, ("Lora Setting"), scr2_3_btn_event_cb);
}
static void entry2_3(void) 
{

}
static void exit2_3(void) {
    ui_lora_param_set();
}
static void destroy2_3(void) { }

static scr_lifecycle_t screen2_3 = {
    .create = create2_3,
    .entry = entry2_3,
    .exit  = exit2_3,
    .destroy = destroy2_3,
};
#endif
//************************************[ screen 3 ]****************************************** sd_card
#if 1
static lv_obj_t *scr3_cont_file;
static lv_obj_t *scr3_cont_img;
static lv_obj_t *sd_info;
static lv_obj_t *ui_photos_img;
static char sd_curr_path[128] = "/";
static char md_open_path[128] = {0};
static char image_open_path[128] = {0};
static lv_point_t scr3_press_point = {0, 0};
static bool scr3_drag_detected = false;
static void sd_file_list_populate(void);

static bool path_has_ext_ci(const char *path, const char *ext)
{
    if (!path || !ext) return false;
    size_t path_len = strlen(path);
    size_t ext_len = strlen(ext);
    if (path_len < ext_len) return false;
    const char *tail = path + path_len - ext_len;
    for (size_t i = 0; i < ext_len; ++i) {
        char a = tail[i];
        char b = ext[i];
        if (a >= 'A' && a <= 'Z') a = a - 'A' + 'a';
        if (b >= 'A' && b <= 'Z') b = b - 'A' + 'a';
        if (a != b) return false;
    }
    return true;
}

static void read_img_btn_event(lv_event_t * e)
{
    if(e->code == LV_EVENT_PRESSED) {
        lv_indev_t *indev = lv_indev_get_act();
        if(indev) {
            lv_indev_get_point(indev, &scr3_press_point);
        }
        scr3_drag_detected = false;
        return;
    }

    if(e->code == LV_EVENT_PRESSING) {
        lv_indev_t *indev = lv_indev_get_act();
        if(indev) {
            lv_point_t curr = {0, 0};
            lv_indev_get_point(indev, &curr);
            if(LV_ABS(curr.x - scr3_press_point.x) > 12 || LV_ABS(curr.y - scr3_press_point.y) > 12) {
                scr3_drag_detected = true;
            }
        }
        return;
    }

    if(e->code != LV_EVENT_CLICKED) return;
    if(scr3_drag_detected) {
        scr3_drag_detected = false;
        return;
    }

    lv_obj_t *path_lab = (lv_obj_t *)e->user_data;
    const char *full_path = lv_label_get_text(path_lab);
    if(full_path[0] == 'D') {
        lv_snprintf(sd_curr_path, sizeof(sd_curr_path), "%s", &full_path[1]);
        sd_file_list_populate();
        return;
    }

    if(strncmp(full_path, "FS:", 3) == 0 &&
       (strstr(&full_path[3], ".md") || strstr(&full_path[3], ".markdown") || strstr(&full_path[3], ".txt") || strstr(&full_path[3], ".html") || strstr(&full_path[3], ".htm") || strstr(&full_path[3], ".csv"))) {
        lv_snprintf(md_open_path, sizeof(md_open_path), "%s", &full_path[3]);
        scr_mgr_push(SCREEN11_ID, false);
        return;
    }

    if (strncmp(full_path, "FS:", 3) == 0 && path_has_ext_ci(&full_path[3], ".png")) {
        lv_snprintf(image_open_path, sizeof(image_open_path), "%s", &full_path[3]);
        scr_mgr_push(SCREEN14_ID, false);
        return;
    }

    if(strncmp(full_path, "FS:", 3) == 0) {
        lv_img_set_src(ui_photos_img, &full_path[3]);
        printf("event [%s]\n", &full_path[3]);
    }
}

static void scr3_btn_event_cb(lv_event_t * e)
{
    if(e->code == LV_EVENT_CLICKED){
        // ui_full_refresh();
        scr_mgr_pop(false);
    }
}

static void scr3_add_img_btn(const char *text, int text_len, int type)
{
    (void)text_len;
    (void)type;
    lv_obj_t *obj = lv_list_add_btn(scr3_cont_file, NULL, text);
    lv_obj_set_width(obj, lv_pct(100));
    lv_obj_set_height(obj, 60);
    lv_obj_set_style_text_font(obj, &Font_Mono_Bold_30, LV_PART_MAIN);
    lv_obj_set_style_bg_color(obj, lv_color_hex(EPD_COLOR_BG), LV_PART_MAIN);
    lv_obj_set_style_text_color(obj, lv_color_hex(EPD_COLOR_FG), LV_PART_MAIN);

    lv_obj_t *lab1 = lv_label_create(obj);
    lv_label_set_text(lab1, text);
    lv_obj_add_flag(lab1, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(obj, read_img_btn_event, LV_EVENT_PRESSED, lab1);
    lv_obj_add_event_cb(obj, read_img_btn_event, LV_EVENT_PRESSING, lab1);
    lv_obj_add_event_cb(obj, read_img_btn_event, LV_EVENT_CLICKED, lab1);

    uint32_t child_cnt = lv_obj_get_child_cnt(obj);
    for(uint32_t i = 0; i < child_cnt; i++) {
        lv_obj_t *child = lv_obj_get_child(obj, i);
        lv_obj_add_event_cb(child, read_img_btn_event, LV_EVENT_PRESSED, lab1);
        lv_obj_add_event_cb(child, read_img_btn_event, LV_EVENT_PRESSING, lab1);
        lv_obj_add_event_cb(child, read_img_btn_event, LV_EVENT_CLICKED, lab1);
    }
}

static void sd_go_parent(void)
{
    if(strcmp(sd_curr_path, "/") == 0) return;
    char *last = strrchr(sd_curr_path, '/');
    if(last == sd_curr_path) {
        sd_curr_path[1] = '\0';
    } else if(last) {
        *last = '\0';
    } else {
        lv_snprintf(sd_curr_path, sizeof(sd_curr_path), "/");
    }
}


static void sd_file_list_populate(void)
{
    uint32_t child_cnt = lv_obj_get_child_cnt(scr3_cont_file);
    for(uint32_t i = 0; i < child_cnt; i++) {
        lv_obj_del(lv_obj_get_child(scr3_cont_file, 0));
    }

    if(strcmp(sd_curr_path, "/") != 0) {
        scr3_add_img_btn("..", 2, 1);
        lv_obj_t *obj = lv_obj_get_child(scr3_cont_file, lv_obj_get_child_cnt(scr3_cont_file) - 1);
        lv_obj_t *lab1 = lv_obj_get_child(obj, lv_obj_get_child_cnt(obj) - 1);
        static char parent_flag[128];
        lv_snprintf(parent_flag, sizeof(parent_flag), "D%s", sd_curr_path);
        lv_label_set_text(lab1, parent_flag);
        lv_obj_remove_event_cb(obj, read_img_btn_event);
        lv_obj_add_event_cb(obj, [](lv_event_t *e){
            if(e->code == LV_EVENT_CLICKED){
                sd_go_parent();
                sd_file_list_populate();
            }
        }, LV_EVENT_CLICKED, NULL);
    }

    File root = SD.open(sd_curr_path);
    if (!root || !root.isDirectory()) {
        return;
    }

    File file = root.openNextFile();
    uint16_t file_index = 0;
    while (file)
    {
        if (file_index >= 64) {
            file.close();
            break;
        }

        char display_name[64] = {0};
        char full_path[128] = {0};
        if (file.isDirectory()) {
            snprintf(display_name, sizeof(display_name), "[%s]", file.name());
            lv_snprintf(full_path, sizeof(full_path), "D%s", file.path());
        } else {
            snprintf(display_name, sizeof(display_name), "%s", file.name());
            lv_snprintf(full_path, sizeof(full_path), "FS:%s", file.path());
        }

        scr3_add_img_btn(display_name, strlen(display_name), 0);
        lv_obj_t *obj = lv_obj_get_child(scr3_cont_file, lv_obj_get_child_cnt(scr3_cont_file) - 1);
        lv_obj_t *lab1 = lv_obj_get_child(obj, lv_obj_get_child_cnt(obj) - 1);
        lv_label_set_text(lab1, full_path);

        file.close();
        file = root.openNextFile();
        file_index++;
    }
    root.close();
}

static void create3(lv_obj_t *parent) {
    lv_snprintf(sd_curr_path, sizeof(sd_curr_path), "/");
    scr3_cont_file = lv_list_create(parent);
    lv_obj_set_size(scr3_cont_file, lv_pct(100), lv_pct(85));
    lv_obj_set_style_bg_color(scr3_cont_file, lv_color_hex(EPD_COLOR_BG), LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(scr3_cont_file, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_scroll_dir(scr3_cont_file, LV_DIR_VER);
    lv_obj_set_style_border_width(scr3_cont_file, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(scr3_cont_file, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(scr3_cont_file, 5, LV_PART_MAIN);
    lv_obj_set_style_pad_row(scr3_cont_file, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_column(scr3_cont_file, 0, LV_PART_MAIN);
    lv_obj_set_align(scr3_cont_file, LV_ALIGN_BOTTOM_MID);

    scr3_cont_img = lv_obj_create(parent);
    lv_obj_set_size(scr3_cont_img, 1, 1);
    lv_obj_set_style_bg_color(scr3_cont_img, lv_color_hex(EPD_COLOR_BG), LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(scr3_cont_img, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_border_width(scr3_cont_img, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(scr3_cont_img, 0, LV_PART_MAIN);
    lv_obj_add_flag(scr3_cont_img, LV_OBJ_FLAG_HIDDEN);

    //---------------------
    ui_photos_img = lv_img_create(scr3_cont_img);
    lv_obj_align(ui_photos_img, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *lab1;
    int ret = 0;
    ui_test_get_sd(&ret);
    if(ret) {
        ui_sd_read();
        sd_file_list_populate();

        // //---------------------
        // scr_middle_line(parent);

        sd_info = lv_label_create(parent);
        lv_obj_set_style_text_font(sd_info, &Font_Mono_Bold_30, LV_PART_MAIN);
        lv_label_set_text(sd_info, "SD GALLERY"); 
    } else {
        sd_info = lv_label_create(parent);
        lv_obj_set_style_text_font(sd_info, &Font_Mono_Bold_30, LV_PART_MAIN);
        lv_label_set_text(sd_info, "NO FIND SD CARD!"); 
    }

    // back
    scr_back_btn_create(parent, "SD", scr3_btn_event_cb);
}
static void entry3(void) 
{
    // lv_obj_align(scr3_cont, LV_ALIGN_BOTTOM_MID, 0, 0);
    int ret = 0;
    ui_test_get_sd(&ret);
    if(ret) {
        lv_obj_align(sd_info, LV_ALIGN_TOP_MID, 0, 22);
    } else {
        lv_obj_center(sd_info);
    }
}
static void exit3(void) { }
static void destroy3(void) {
}

static scr_lifecycle_t screen3 = {
    .create = create3,
    .entry = entry3,
    .exit  = exit3,
    .destroy = destroy3,
};
#endif
//************************************[ screen 4 ]****************************************** setting
// --------------------- screen 2.1 --------------------- About System
#if 1
static void scr4_1_btn_event_cb(lv_event_t * e)
{
    if(e->code == LV_EVENT_CLICKED){
        scr_mgr_pop(false);
    }
}

static void create4_1(lv_obj_t *parent) 
{
    lv_obj_t *info = lv_label_create(parent);
    lv_obj_set_width(info, LV_HOR_RES * 0.9);
    lv_obj_set_style_text_color(info, lv_color_hex(EPD_COLOR_FG), LV_PART_MAIN);
    lv_obj_set_style_text_font(info, &Font_Mono_Bold_25, LV_PART_MAIN);
    lv_obj_set_style_text_align(info, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(info, LV_LABEL_LONG_WRAP);

    String str = "";

    str += "\n                           \n";
    str += line_full_format(32, "SF Version:", ui_setting_get_sf_ver());
    str += "\n                           \n";

    str += line_full_format(32, "HD Version:", ui_setting_get_hd_ver());
    str += "\n                           \n";

    char buf[32];
    uint64_t total=0, used=0;
    ui_sd_get_capacity(&total, &used);
    lv_snprintf(buf, 32, "%llu/%llu MB", used, total);
    str += line_full_format(32, "TF Card Cap:", (const char *)buf);
    str += "\n                           \n";

    lv_label_set_text_fmt(info, str.c_str());
    
    lv_obj_align(info, LV_ALIGN_TOP_MID, 0, 50);
    
    scr_back_btn_create(parent, ("About System"), scr4_1_btn_event_cb);
}
static void entry4_1(void) 
{
}
static void exit4_1(void) {
}
static void destroy4_1(void) { }

static scr_lifecycle_t screen4_1 = {
    .create = create4_1,
    .entry = entry4_1,
    .exit  = exit4_1,
    .destroy = destroy4_1,
};
#endif
// --------------------- screen 4.2 --------------------- Set EPD Vcom
#if 1
static lv_obj_t *set_1000mv_item;
static lv_obj_t *set_100mv_item;
static lv_obj_t *set_10mv_item;

static lv_obj_t * set_epd_vcom_lab;
static lv_obj_t *set_1000mv_lab;
static lv_obj_t *set_100mv_lab;
static lv_obj_t *set_10mv_lab;

static float set_epd_vcom_num;
static int   set_1000mv_num;
static int   set_100mv_num;
static int   set_10mv_num;

static void scr4_2_btn_event_cb(lv_event_t * e)
{
    if(e->code == LV_EVENT_CLICKED){
        scr_mgr_pop(false);
    }
}

/* clang-format on */
#define SET_RANGE(opt, num, min, max, num_lab) \
    if (opt == '+')                            \
    {                                          \
        num++;                                 \
    }                                          \
    else                                       \
    {                                          \
        num--;                                 \
    }                                          \
    num = num > max ? max : num;               \
    num = num < min ? min : num;               \
    lv_label_set_text_fmt(num_lab, "%d", num);
/* clang-format off */
static void scr4_2_sub_item_event(lv_event_t *e)
{
    lv_obj_t *parent = lv_obj_get_parent(e->target);
    char opt = (int)(lv_event_get_user_data(e));

    if(parent == set_1000mv_item) 
    {
        SET_RANGE(opt, set_1000mv_num, 0, 4, set_1000mv_lab);
    } 
    else if(parent == set_100mv_item) 
    {
        SET_RANGE(opt, set_100mv_num, 0, 9, set_100mv_lab);
    } 
    else if(parent == set_10mv_item) 
    {
        SET_RANGE(opt, set_10mv_num, 0, 9, set_10mv_lab);
    }

    set_epd_vcom_num = (set_1000mv_num * 1000 + set_100mv_num * 100 + set_10mv_num * 10) / 1000.0;
    ui_setting_set_vcom((int)(set_epd_vcom_num * 1000));
    lv_label_set_text_fmt(set_epd_vcom_lab, "%.2fV", set_epd_vcom_num);
}

static lv_obj_t *scr4_2_sub_item_create(lv_obj_t *parent, lv_obj_t **lab, const char *range, const char *unit)
{
    lv_obj_t *ui_Container1 = lv_obj_create(parent);
    lv_obj_remove_style_all(ui_Container1);
    lv_obj_set_width(ui_Container1, 490);
    lv_obj_set_height(ui_Container1, 145);
    lv_obj_set_x(ui_Container1, 5);
    lv_obj_set_y(ui_Container1, -177);
    lv_obj_set_align(ui_Container1, LV_ALIGN_CENTER);
    lv_obj_clear_flag(ui_Container1, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);      /// Flags
    lv_obj_set_style_border_width(ui_Container1, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ui_Container1, 20, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *ui_Label2 = lv_label_create(ui_Container1);
    lv_obj_set_width(ui_Label2, LV_SIZE_CONTENT);   /// 1
    lv_obj_set_height(ui_Label2, LV_SIZE_CONTENT);    /// 1
    lv_obj_set_x(ui_Label2, 0);
    lv_obj_set_y(ui_Label2, 29);
    lv_obj_set_align(ui_Label2, LV_ALIGN_CENTER);
    // lv_obj_set_style_border_width(ui_Label2, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text_fmt(ui_Label2, "%s", range);
    lv_obj_set_style_text_font(ui_Label2, &Font_Mono_Bold_25, LV_PART_MAIN | LV_STATE_DEFAULT);

    *lab = lv_label_create(ui_Container1);
    lv_obj_set_width(*lab, LV_SIZE_CONTENT);   /// 1
    lv_obj_set_height(*lab, LV_SIZE_CONTENT);    /// 1
    lv_obj_set_x(*lab, -23);
    lv_obj_set_y(*lab, -13);
    lv_obj_set_align(*lab, LV_ALIGN_CENTER);
    lv_label_set_text(*lab, " 0 ");
    lv_obj_set_style_border_width(*lab, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(*lab, &Font_Mono_Bold_30, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *ui_Label9 = lv_label_create(ui_Container1);
    lv_obj_set_width(ui_Label9, LV_SIZE_CONTENT);   /// 1
    lv_obj_set_height(ui_Label9, LV_SIZE_CONTENT);    /// 1
    lv_obj_set_x(ui_Label9, 20);
    lv_obj_set_y(ui_Label9, -12);
    lv_obj_set_align(ui_Label9, LV_ALIGN_CENTER);
    lv_label_set_text_fmt(ui_Label9, "%s", unit);
    // lv_obj_set_style_border_width(ui_Label9, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_Label9, &Font_Mono_Bold_25, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *sub_btn = lv_obj_create(ui_Container1);
    lv_obj_remove_style_all(sub_btn);
    lv_obj_set_width(sub_btn, 76);
    lv_obj_set_height(sub_btn, 72);
    lv_obj_set_x(sub_btn, -171);
    lv_obj_set_y(sub_btn, 6);
    lv_obj_set_align(sub_btn, LV_ALIGN_CENTER);
    lv_obj_clear_flag(sub_btn, LV_OBJ_FLAG_SCROLLABLE);      /// Flags
    lv_obj_set_style_radius(sub_btn, 20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(sub_btn, 3, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *sub_txt = lv_label_create(sub_btn);
    lv_obj_set_align(sub_txt, LV_ALIGN_CENTER);
    lv_label_set_text(sub_txt, "SUB");
    lv_obj_set_style_text_font(sub_txt, &Font_Mono_Bold_25, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *add_btn = lv_obj_create(ui_Container1);
    lv_obj_remove_style_all(add_btn);
    lv_obj_set_width(add_btn, 76);
    lv_obj_set_height(add_btn, 72);
    lv_obj_set_x(add_btn, 166);
    lv_obj_set_y(add_btn, 3);
    lv_obj_set_align(add_btn, LV_ALIGN_CENTER);
    lv_obj_clear_flag(add_btn, LV_OBJ_FLAG_SCROLLABLE);      /// Flags
    lv_obj_set_style_radius(add_btn, 20, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(add_btn, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
    
    lv_obj_t *add_txt = lv_label_create(add_btn);
    lv_obj_set_align(add_txt, LV_ALIGN_CENTER);
    lv_label_set_text(add_txt, "ADD");
    lv_obj_set_style_text_font(add_txt, &Font_Mono_Bold_25, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_add_event_cb(sub_btn, scr4_2_sub_item_event, LV_EVENT_CLICKED, (void *)'-');
    lv_obj_add_event_cb(add_btn, scr4_2_sub_item_event, LV_EVENT_CLICKED, (void *)'+');

    return ui_Container1;
}

static void create4_2(lv_obj_t *parent) 
{
    lv_obj_t * ui_Container2 = lv_obj_create(parent);
    lv_obj_remove_style_all(ui_Container2);
    lv_obj_set_width(ui_Container2, lv_pct(100));
    lv_obj_set_height(ui_Container2, lv_pct(60));
    lv_obj_set_x(ui_Container2, 0);
    lv_obj_set_y(ui_Container2, 23);
    lv_obj_set_align(ui_Container2, LV_ALIGN_CENTER);
    lv_obj_set_flex_flow(ui_Container2, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ui_Container2, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(ui_Container2, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);      /// Flags
    lv_obj_set_style_border_width(ui_Container2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_row(ui_Container2, 30, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_column(ui_Container2, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    set_epd_vcom_lab = lv_label_create(parent);
    lv_obj_set_width(set_epd_vcom_lab, LV_SIZE_CONTENT);   /// 1
    lv_obj_set_height(set_epd_vcom_lab, LV_SIZE_CONTENT);    /// 1
    lv_obj_set_x(set_epd_vcom_lab, -1);
    lv_obj_set_y(set_epd_vcom_lab, -310);
    lv_obj_set_align(set_epd_vcom_lab, LV_ALIGN_CENTER);
    lv_label_set_text(set_epd_vcom_lab, "1.54V");
    lv_obj_set_style_text_font(set_epd_vcom_lab, &Font_Mono_Bold_90, LV_PART_MAIN | LV_STATE_DEFAULT);

    set_1000mv_item = scr4_2_sub_item_create(ui_Container2, &set_1000mv_lab, "range: 0 - 4", "* V");
    set_100mv_item  = scr4_2_sub_item_create(ui_Container2, &set_100mv_lab , "range: 0 - 9", "* 0.1V");
    set_10mv_item   = scr4_2_sub_item_create(ui_Container2, &set_10mv_lab  , "range: 0 - 9", "* 0.01V");

    int default_vcom = ui_setting_get_vcom();
    set_1000mv_num = default_vcom / 1000;
    set_100mv_num  = (default_vcom / 100) % 10;
    set_10mv_num   = (default_vcom / 10) %10 ;

    lv_label_set_text_fmt(set_1000mv_lab, "%d", set_1000mv_num);
    lv_label_set_text_fmt(set_100mv_lab, "%d", set_100mv_num);
    lv_label_set_text_fmt(set_10mv_lab, "%d", set_10mv_num);

    lv_obj_align(set_1000mv_lab, LV_ALIGN_CENTER, -23, -12);
    lv_obj_align(set_100mv_lab, LV_ALIGN_CENTER,  -38, -12);
    lv_obj_align(set_10mv_lab, LV_ALIGN_CENTER,   -48, -12);

    set_epd_vcom_num = (set_1000mv_num * 1000 + set_100mv_num * 100 + set_10mv_num * 10) / 1000.0;
    lv_label_set_text_fmt(set_epd_vcom_lab, "%.2fV", set_epd_vcom_num);

    scr_back_btn_create(parent, ("Set Vcom"), scr4_2_btn_event_cb);
}
static void entry4_2(void) 
{
}
static void exit4_2(void) {
}
static void destroy4_2(void) { }

static scr_lifecycle_t screen4_2 = {
    .create = create4_2,
    .entry = entry4_2,
    .exit  = exit4_2,
    .destroy = destroy4_2,
};
#endif
// --------------------- screen --------------------- Setting
#if 1
static lv_obj_t *setting_list;
static lv_obj_t *setting_page;
static int setting_num = 0;
static int setting_page_num = 0;
static int setting_curr_page = 0;

void set_cb(int n){}

const char *get_cb(int *ret_n) 
{ 
    return "OFF";
}

const char *get_vcom_cb(int *ret_n) 
{
    float v = (ui_setting_get_vcom() / 1000.0);
    lv_snprintf(global_buf, GLOBAL_BUF_LEN, "%0.2fV", v);
    return (const char *)global_buf;
}

static ui_setting_handle setting_handle_list[] = {
    {.name="Backlight",       .type=UI_SETTING_TYPE_SW,  .set_cb=ui_setting_set_backlight_level,     .get_cb=ui_setting_get_backlight},
    {.name="Refresh Speed",   .type=UI_SETTING_TYPE_SW,  .set_cb=ui_setting_set_refresh_speed, .get_cb=ui_setting_get_refresh_speed},
    {.name="WiFi Settings",   .type=UI_SETTING_TYPE_SUB, .set_cb=NULL, .get_cb=NULL,        .sub_id=SCREEN6_ID},
    {.name = "-Set Date & Time", .type=UI_SETTING_TYPE_SUB, .set_cb=NULL, .get_cb=NULL, .sub_id=SCREEN4_3_ID},
    {.name = "-Set EPD Vcom", .type=UI_SETTING_TYPE_SUB, .set_cb=NULL, .get_cb=get_vcom_cb, .sub_id=SCREEN4_2_ID},
    {.name="-About System",   .type=UI_SETTING_TYPE_SUB, .set_cb=NULL, .get_cb=NULL,        .sub_id=SCREEN4_1_ID},
};

/**
 * func:      setting
 * handle:    setting_handle_list
 * list:      setting_list
 * num:       setting_num
 * page_num:  setting_page_num
 * curr_page: setting_curr_page
 */
// #define UI_LIST_CREATE(func, handle, list, num, page_num, curr_page) 
UI_LIST_CREATE(setting, setting_handle_list, setting_list, setting_num, setting_page_num, setting_curr_page)

/**
 * func:      setting
 * list:      setting_list
 * page:      setting_page
 * num:       setting_num
 * page_num:  setting_page_num
 * curr_page: setting_curr_page
 */
// #define UI_LIST_BTN_CREATE(func, list, page, num, page_num, curr_page) 
UI_LIST_BTN_CREATE(setting, setting_list, setting_page, setting_num, setting_page_num, setting_curr_page) 

static void scr4_btn_event_cb(lv_event_t * e)
{
    if(e->code == LV_EVENT_CLICKED) {
        // ui_full_refresh();
        scr_mgr_pop(false);
    }
}

static void create4(lv_obj_t *parent) 
{
    setting_list = lv_list_create(parent);
    lv_obj_set_size(setting_list, lv_pct(93), lv_pct(91));
    lv_obj_align(setting_list, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(setting_list, lv_color_hex(EPD_COLOR_BG), LV_PART_MAIN);
    lv_obj_set_style_pad_top(setting_list, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_row(setting_list, 10, LV_PART_MAIN);
    lv_obj_set_style_radius(setting_list, 0, LV_PART_MAIN);
    // lv_obj_set_style_outline_pad(setting_list, 2, LV_PART_MAIN);
    lv_obj_set_style_border_width(setting_list, 0, LV_PART_MAIN);
    lv_obj_set_style_border_color(setting_list, lv_color_hex(EPD_COLOR_FG), LV_PART_MAIN);
    lv_obj_set_style_shadow_width(setting_list, 0, LV_PART_MAIN);

    setting_item_create();

    if(setting_page_num > 0)
        ui_list_btn_create(parent, setting_page_switch_cb);

    setting_page = lv_label_create(parent);
    lv_obj_set_width(setting_page, LV_SIZE_CONTENT);   /// 1
    lv_obj_set_height(setting_page, LV_SIZE_CONTENT);    /// 1
    lv_obj_align(setting_page, LV_ALIGN_BOTTOM_MID, 0, -30);
    lv_label_set_text_fmt(setting_page, "%d / %d", setting_curr_page, setting_page_num);
    lv_obj_set_style_text_color(setting_page, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(setting_page, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    // back
    scr_back_btn_create(parent, "Setting", scr4_btn_event_cb);
}
static void entry4(void) { }
static void exit4(void) { }
static void destroy4(void) { }

static scr_lifecycle_t screen4 = {
    .create = create4,
    .entry = entry4,
    .exit  = exit4,
    .destroy = destroy4,
};
#endif

// --------------------- screen 4.3 --------------------- Set Date & Time
#if 1
static lv_obj_t *set_dt_lab;
static lv_obj_t *set_dt_field_btns[5];
static lv_obj_t *set_dt_field_labs[5];
static int set_dt_field = 0;
static int set_dt_year = 2024, set_dt_month = 1, set_dt_day = 1, set_dt_hour = 0, set_dt_min = 0;

static void set_dt_update_field_focus(void)
{
    for (int i = 0; i < 5; i++) {
        bool selected = (i == set_dt_field);
        lv_obj_set_style_bg_opa(set_dt_field_btns[i], LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(set_dt_field_btns[i], selected ? 3 : 1, LV_PART_MAIN);
    }
}

static void set_dt_update_label(void)
{
    lv_label_set_text_fmt(set_dt_lab, "%04d-%02d-%02d  %02d:%02d", set_dt_year, set_dt_month, set_dt_day, set_dt_hour, set_dt_min);
    lv_label_set_text_fmt(set_dt_field_labs[0], "%04d", set_dt_year);
    lv_label_set_text_fmt(set_dt_field_labs[1], "%02d", set_dt_month);
    lv_label_set_text_fmt(set_dt_field_labs[2], "%02d", set_dt_day);
    lv_label_set_text_fmt(set_dt_field_labs[3], "%02d", set_dt_hour);
    lv_label_set_text_fmt(set_dt_field_labs[4], "%02d", set_dt_min);
    set_dt_update_field_focus();
}

static void set_dt_adjust(int delta)
{
    int *val = NULL, min = 0, max = 0;
    switch(set_dt_field) {
        case 0: val = &set_dt_year;  min = 2000; max = 2099; break;
        case 1: val = &set_dt_month; min = 1;    max = 12;   break;
        case 2: val = &set_dt_day;   min = 1;    max = 31;   break;
        case 3: val = &set_dt_hour;  min = 0;    max = 23;   break;
        default:val = &set_dt_min;   min = 0;    max = 59;   break;
    }
    *val += delta;
    if(*val > max) *val = min;
    if(*val < min) *val = max;
    set_dt_update_label();
}

static void set_dt_event_cb(lv_event_t * e)
{
    if(e->code != LV_EVENT_CLICKED) return;
    int cmd = (int)e->user_data;
    if(cmd == 0) set_dt_adjust(1);
    else if(cmd == 1) set_dt_adjust(-1);
    else if(cmd >= 10 && cmd <= 14) {
        set_dt_field = cmd - 10;
        set_dt_update_field_focus();
    }
    else if(cmd == 4) { ui_clock_set_data_time(set_dt_year, set_dt_month, set_dt_day, set_dt_hour, set_dt_min, 0); }
}

static void scr4_3_btn_event_cb(lv_event_t * e)
{
    if(e->code == LV_EVENT_CLICKED) scr_mgr_pop(false);
}

static void create4_3(lv_obj_t *parent)
{
    set_dt_lab = lv_label_create(parent);
    lv_obj_set_style_text_font(set_dt_lab, &Font_Mono_Bold_30, LV_PART_MAIN);
    epd_style_label(set_dt_lab);
    lv_obj_align(set_dt_lab, LV_ALIGN_TOP_MID, 0, 70);
    lv_obj_add_flag(set_dt_lab, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *field_row = lv_obj_create(parent);
    lv_obj_set_size(field_row, 470, 80);
    lv_obj_align(field_row, LV_ALIGN_TOP_MID, 0, 130);
    lv_obj_set_style_border_width(field_row, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(field_row, LV_OPA_TRANSP, LV_PART_MAIN);
    epd_style_transparent(field_row);
    lv_obj_set_style_pad_all(field_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(field_row, 6, LV_PART_MAIN);
    lv_obj_set_flex_flow(field_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(field_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    const char *sep_text[] = {"-", "-", "  ", ":"};
    int field_width[] = {108, 68, 68, 68, 68};
    for (int i = 0; i < 5; i++) {
        lv_obj_t *btn = lv_btn_create(field_row);
        set_dt_field_btns[i] = btn;
        lv_obj_set_size(btn, field_width[i], 64);
        lv_obj_set_style_radius(btn, 12, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
        lv_obj_set_style_border_color(btn, lv_color_hex(EPD_COLOR_FG), LV_PART_MAIN);
        epd_style_selectable_field(btn);
        lv_obj_add_event_cb(btn, set_dt_event_cb, LV_EVENT_CLICKED, (void *)(10 + i));

        set_dt_field_labs[i] = lv_label_create(btn);
        lv_obj_set_style_text_font(set_dt_field_labs[i], &Font_Mono_Bold_30, LV_PART_MAIN);
        epd_style_label(set_dt_field_labs[i]);
        lv_obj_center(set_dt_field_labs[i]);

        if (i < 4) {
            lv_obj_t *sep = lv_label_create(field_row);
            lv_obj_set_style_text_font(sep, &Font_Mono_Bold_30, LV_PART_MAIN);
            lv_label_set_text(sep, sep_text[i]);
            epd_style_label(sep);
        }
    }

    set_dt_update_label();

    lv_obj_t *btn_up = lv_btn_create(parent);
    epd_style_button(btn_up);
    lv_obj_set_size(btn_up, 130, 70);
    lv_obj_align(btn_up, LV_ALIGN_CENTER, -150, 50);
    lv_obj_add_event_cb(btn_up, set_dt_event_cb, LV_EVENT_CLICKED, (void *)0);
    lv_label_set_text(lv_label_create(btn_up), "UP");
    lv_obj_t *btn_up_label = lv_obj_get_child(btn_up, 0);
    lv_obj_set_style_text_color(btn_up_label, lv_color_hex(EPD_COLOR_FG), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(btn_up_label);

    lv_obj_t *btn_down = lv_btn_create(parent);
    epd_style_button(btn_down);
    lv_obj_set_size(btn_down, 130, 70);
    lv_obj_align(btn_down, LV_ALIGN_CENTER, 0, 50);
    lv_obj_add_event_cb(btn_down, set_dt_event_cb, LV_EVENT_CLICKED, (void *)1);
    lv_label_set_text(lv_label_create(btn_down), "DOWN");
    lv_obj_t *btn_down_label = lv_obj_get_child(btn_down, 0);
    lv_obj_set_style_text_color(btn_down_label, lv_color_hex(EPD_COLOR_FG), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(btn_down_label);

    lv_obj_t *btn_save = lv_btn_create(parent);
    epd_style_button(btn_save);
    lv_obj_set_size(btn_save, 130, 70);
    lv_obj_align(btn_save, LV_ALIGN_CENTER, 150, 50);
    lv_obj_add_event_cb(btn_save, set_dt_event_cb, LV_EVENT_CLICKED, (void *)4);
    lv_label_set_text(lv_label_create(btn_save), "SAVE");
    lv_obj_t *btn_save_label = lv_obj_get_child(btn_save, 0);
    lv_obj_set_style_text_color(btn_save_label, lv_color_hex(EPD_COLOR_FG), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(btn_save_label);

    scr_back_btn_create(parent, "Set Date & Time", scr4_3_btn_event_cb);
}

static void entry4_3(void)
{
    uint8_t y, m, d, w, hh, mm, ss;
    ui_clock_get_data(&y, &m, &d, &w);
    ui_clock_get_time(&hh, &mm, &ss);
    set_dt_year = 2000 + y;
    set_dt_month = m;
    set_dt_day = d;
    set_dt_hour = hh;
    set_dt_min = mm;
    set_dt_field = 0;
    set_dt_update_label();
}
static void exit4_3(void) { }
static void destroy4_3(void) { }

static scr_lifecycle_t screen4_3 = {
    .create = create4_3,
    .entry = entry4_3,
    .exit  = exit4_3,
    .destroy = destroy4_3,
};
#endif
//************************************[ screen 5 ]****************************************** test
#if 1
static lv_obj_t *test_list;
static lv_obj_t *test_page;
static int test_num = 0;
static int test_page_num = 0;
static int test_curr_page = 0;

void test_set_cb(int n) {}

const char *test_get_cb(int *ret_n)
{
    return "PASS";
}

static ui_setting_handle test_handle_list[] = {
    {.name = "GPS",              .type=UI_SETTING_TYPE_SW, .set_cb = test_set_cb, .get_cb = ui_test_get_gps},
    {.name = "LoRa",             .type=UI_SETTING_TYPE_SW, .set_cb = test_set_cb, .get_cb = ui_test_get_lora},
    {.name = "SD Card",          .type=UI_SETTING_TYPE_SW, .set_cb = test_set_cb, .get_cb = ui_test_get_sd},
    {.name = "[0x51] RTC",       .type=UI_SETTING_TYPE_SW, .set_cb = test_set_cb, .get_cb = ui_test_get_rtc},
    {.name = "[0x5D] Touch",     .type=UI_SETTING_TYPE_SW, .set_cb = test_set_cb, .get_cb = ui_test_get_touch},
    {.name = "[0x6B] BQ25896",   .type=UI_SETTING_TYPE_SW, .set_cb = test_set_cb, .get_cb = ui_test_get_BQ25896},
    {.name = "[0x55] BQ27220",   .type=UI_SETTING_TYPE_SW, .set_cb = test_set_cb, .get_cb = ui_test_get_BQ27220},
    {.name = "[0x20] PCA9535",   .type=UI_SETTING_TYPE_SW, .set_cb = test_set_cb, .get_cb = test_get_cb},
    {.name = "[0x68] TPS651851", .type=UI_SETTING_TYPE_SW, .set_cb = test_set_cb, .get_cb = test_get_cb},
};

///////////////////// FUNCTIONS ////////////////////
/**
 * func:      test
 * handle:    test_handle_list
 * list:      test_list
 * num:       test_num
 * page_num:  test_page_num
 * curr_page: test_curr_page
 */
// #define UI_LIST_CREATE(func, handle, list, num, page_num, curr_page) 
UI_LIST_CREATE(test, test_handle_list, test_list, test_num, test_page_num, test_curr_page)

/**
 * func:      test
 * list:      test_list
 * page:      test_page
 * num:       test_num
 * page_num:  test_page_num
 * curr_page: test_curr_page
 */
// #define UI_LIST_BTN_CREATE(func, list, page, num, page_num, curr_page) 
UI_LIST_BTN_CREATE(test, test_list, test_page, test_num, test_page_num, test_curr_page)


static void scr5_btn_event_cb(lv_event_t * e)
{
    if(e->code == LV_EVENT_CLICKED) {
        // ui_full_refresh();
        scr_mgr_pop(false);
    }
}

static void create5(lv_obj_t *parent) 
{
    test_list = lv_list_create(parent);
    lv_obj_set_size(test_list, lv_pct(93), lv_pct(91));
    lv_obj_align(test_list, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(test_list, lv_color_hex(EPD_COLOR_BG), LV_PART_MAIN);
    lv_obj_set_style_pad_top(test_list, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_row(test_list, 10, LV_PART_MAIN);
    lv_obj_set_style_radius(test_list, 0, LV_PART_MAIN);
    // lv_obj_set_style_outline_pad(test_list, 2, LV_PART_MAIN);
    lv_obj_set_style_border_width(test_list, 0, LV_PART_MAIN);
    lv_obj_set_style_border_color(test_list, lv_color_hex(EPD_COLOR_FG), LV_PART_MAIN);
    lv_obj_set_style_shadow_width(test_list, 0, LV_PART_MAIN);

    test_item_create();

    if(test_page_num > 0)
        ui_list_btn_create(parent, test_page_switch_cb);

    test_page = lv_label_create(parent);
    lv_obj_set_width(test_page, LV_SIZE_CONTENT);   /// 1
    lv_obj_set_height(test_page, LV_SIZE_CONTENT);    /// 1
    lv_obj_align(test_page, LV_ALIGN_BOTTOM_MID, 0, -30);
    lv_label_set_text_fmt(test_page, "%d / %d", test_curr_page, test_page_num);
    lv_obj_set_style_text_color(test_page, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(test_page, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    // back
    scr_back_btn_create(parent, "Test", scr5_btn_event_cb);
}
static void entry5(void) { }
static void exit5(void) { }
static void destroy5(void) { }

static scr_lifecycle_t screen5 = {
    .create = create5,
    .entry = entry5,
    .exit  = exit5,
    .destroy = destroy5,
};
#endif
//************************************[ screen 6 ]****************************************** wifi
#if 1
lv_obj_t *scr6_root;
lv_obj_t *wifi_st_lab = NULL;
lv_obj_t *ip_lab = NULL;
lv_obj_t *ssid_lab = NULL;
lv_obj_t *pwd_lab = NULL;
static lv_timer_t   *wifi_rssi_timer            = NULL;

static volatile bool smartConfigStart      = false;
static lv_timer_t   *wifi_timer            = NULL;
static uint32_t      wifi_timer_counter    = 0;
static uint32_t      wifi_connnect_timeout = 60;
static lv_obj_t     *wifi_sta_ssid_ta      = NULL;
static lv_obj_t     *wifi_sta_pwd_ta       = NULL;
static lv_obj_t     *wifi_ap_ssid_ta       = NULL;
static lv_obj_t     *wifi_ap_pwd_ta        = NULL;
static lv_obj_t     *wifi_keyboard         = NULL;

static String wifi_sta_ssid = "";
static String wifi_sta_pwd  = "";
static String wifi_ap_ssid = "T5S3-AP";
static String wifi_ap_pwd  = "12345678";
static WebServer wifi_web_server(80);
static DNSServer wifi_dns_server;
static bool wifi_web_started = false;

static void wifi_load_saved_settings(void)
{
    wifi_sta_ssid = nvs_param_get_str(NVS_ID_WIFI_STA_SSID);
    wifi_sta_pwd = nvs_param_get_str(NVS_ID_WIFI_STA_PWD);
    wifi_ap_ssid = nvs_param_get_str(NVS_ID_WIFI_AP_SSID);
    wifi_ap_pwd = nvs_param_get_str(NVS_ID_WIFI_AP_PWD);

    if (wifi_ap_ssid.length() == 0) wifi_ap_ssid = "T5S3-AP";
    if (wifi_ap_pwd.length() < 8) wifi_ap_pwd = "12345678";
}

static void wifi_save_settings(void)
{
    nvs_param_set_str(NVS_ID_WIFI_STA_SSID, wifi_sta_ssid.c_str());
    nvs_param_set_str(NVS_ID_WIFI_STA_PWD, wifi_sta_pwd.c_str());
    nvs_param_set_str(NVS_ID_WIFI_AP_SSID, wifi_ap_ssid.c_str());
    nvs_param_set_str(NVS_ID_WIFI_AP_PWD, wifi_ap_pwd.c_str());
}

static bool wifi_connect_saved_sta(const char *reason)
{
    wifi_load_saved_settings();

    if (wifi_sta_ssid.length() == 0) {
        Serial.printf("[wifi] no saved STA SSID; cannot connect for %s\n", reason ? reason : "");
        return false;
    }

    WiFi.persistent(false);
    WiFi.setAutoReconnect(true);

    if (WiFi.getMode() == WIFI_OFF) {
        WiFi.mode(WIFI_STA);
    } else if (WiFi.getMode() == WIFI_AP) {
        WiFi.mode(WIFI_AP_STA);
    }

    Serial.printf("[wifi] connecting saved STA for %s ssid='%s'\n",
                  reason ? reason : "",
                  wifi_sta_ssid.c_str());

    WiFi.begin(wifi_sta_ssid.c_str(), wifi_sta_pwd.c_str());
    return true;
}

static void wifi_send_cors_headers(void)
{
    wifi_web_server.sendHeader("Access-Control-Allow-Origin", "http://paper.go");
    wifi_web_server.sendHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
    wifi_web_server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}


static const char *WIFI_API_ROOT_HTML = R"HTML(<!doctype html>
<html>
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width,initial-scale=1" />
  <title>paper.api Wi-Fi Settings</title>
  <style>
    body { font-family: sans-serif; max-width: 560px; margin: 24px auto; padding: 0 16px; }
    h1 { font-size: 1.2rem; }
    label { display:block; margin: 10px 0 4px; font-weight: 600; }
    input { width: 100%; box-sizing: border-box; padding: 8px; }
    button { margin-top: 14px; padding: 10px 14px; }
    .row { margin-top: 6px; }
    #status { margin-top: 10px; color: #1f2937; }
  </style>
</head>
<body>
  <h1>paper.api Wi-Fi Settings</h1>
  <p class="row">Saved via <code>POST /settings</code> to device NVS, same fields as the on-device settings app.</p>

  <label for="wifi_ssid">Wi-Fi SSID</label>
  <input id="wifi_ssid" type="text" />

  <label for="wifi_password">Wi-Fi Password</label>
  <input id="wifi_password" type="password" />

  <label for="ap_ssid">AP SSID</label>
  <input id="ap_ssid" type="text" />

  <label for="ap_password">AP Password (min 8 chars or empty)</label>
  <input id="ap_password" type="password" />

  <button id="saveBtn">Save Settings</button>
  <div id="status"></div>

  <script>
    const statusEl = document.getElementById('status');
    function setStatus(msg) { statusEl.textContent = msg; }

    async function loadSettings() {
      setStatus('Loading...');
      const r = await fetch('/settings');
      if (!r.ok) throw new Error('GET /settings failed: ' + r.status);
      const d = await r.json();
      document.getElementById('wifi_ssid').value = d.wifi_ssid || '';
      document.getElementById('wifi_password').value = d.wifi_password || '';
      document.getElementById('ap_ssid').value = d.ap_ssid || '';
      document.getElementById('ap_password').value = d.ap_password || '';
      setStatus('Loaded current settings.');
    }

    async function saveSettings() {
      setStatus('Saving...');
      const body = new URLSearchParams();
      body.set('wifi_ssid', document.getElementById('wifi_ssid').value);
      body.set('wifi_password', document.getElementById('wifi_password').value);
      body.set('ap_ssid', document.getElementById('ap_ssid').value);
      body.set('ap_password', document.getElementById('ap_password').value);

      const r = await fetch('/settings', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body
      });
      if (!r.ok) throw new Error('POST /settings failed: ' + r.status);
      const d = await r.json();
      setStatus('Saved. Wi-Fi connected: ' + (d.wifi_connected ? 'yes' : 'no'));
    }

    document.getElementById('saveBtn').addEventListener('click', () => {
      saveSettings().catch(e => setStatus('Error: ' + e.message));
    });

    loadSettings().catch(e => setStatus('Error: ' + e.message));
  </script>
</body>
</html>)HTML";

static void wifi_handle_api_root_get(void)
{
    wifi_web_server.send(200, "text/html", WIFI_API_ROOT_HTML);
}

static void wifi_handle_settings_options(void)
{
    wifi_send_cors_headers();
    wifi_web_server.send(204, "text/plain", "");
}

static void wifi_handle_settings_get(void)
{
    int backlight = 0;
    int refresh_speed = 0;
    ui_setting_get_backlight(&backlight);
    ui_setting_get_refresh_speed(&refresh_speed);
    int vcom = ui_setting_get_vcom();
    bool wifi_connected = ui_wifi_get_status();

    String json = "{";
    json += "\"backlight\":" + String(backlight) + ",";
    json += "\"refresh_speed\":" + String(refresh_speed) + ",";
    json += "\"vcom\":" + String(vcom) + ",";
    json += "\"wifi_connected\":" + String(wifi_connected ? "true" : "false") + ",";
    json += "\"wifi_ssid\":\"" + wifi_sta_ssid + "\",";
    json += "\"wifi_password\":\"" + wifi_sta_pwd + "\",";
    json += "\"ap_ssid\":\"" + wifi_ap_ssid + "\",";
    json += "\"ap_password\":\"" + wifi_ap_pwd + "\"";
    json += "}";

    wifi_send_cors_headers();
    wifi_web_server.send(200, "application/json", json);
}

static void wifi_handle_settings_post(void)
{
    if (wifi_web_server.hasArg("backlight")) {
        ui_setting_set_backlight_level(wifi_web_server.arg("backlight").toInt());
    }
    if (wifi_web_server.hasArg("refresh_speed")) {
        int target = wifi_web_server.arg("refresh_speed").toInt();
        int current = 0;
        ui_setting_get_refresh_speed(&current);
        for (int i = 0; i < 3 && current != target; ++i) {
            ui_setting_set_refresh_speed(0);
            ui_setting_get_refresh_speed(&current);
        }
    }
    if (wifi_web_server.hasArg("vcom")) {
        ui_setting_set_vcom(wifi_web_server.arg("vcom").toInt());
    }

    bool ap_config_changed = false;
    if (wifi_web_server.hasArg("wifi_ssid")) {
        wifi_sta_ssid = wifi_web_server.arg("wifi_ssid");
    }
    if (wifi_web_server.hasArg("wifi_password")) {
        wifi_sta_pwd = wifi_web_server.arg("wifi_password");
    }
    if (wifi_web_server.hasArg("ap_ssid")) {
        wifi_ap_ssid = wifi_web_server.arg("ap_ssid");
        ap_config_changed = true;
    }
    if (wifi_web_server.hasArg("ap_password")) {
        String ap_pwd = wifi_web_server.arg("ap_password");
        if (ap_pwd.length() >= 8 || ap_pwd.length() == 0) {
            wifi_ap_pwd = ap_pwd;
            ap_config_changed = true;
        }
    }

    wifi_save_settings();
    Serial.printf("[wifi settings] saved STA ssid='%s' pwd_len=%u AP ssid='%s'\n",
                  wifi_sta_ssid.c_str(),
                  (unsigned)wifi_sta_pwd.length(),
                  wifi_ap_ssid.c_str());
    if (ap_config_changed) {
        WiFi.softAPdisconnect(true);
        if (!WiFi.softAP(wifi_ap_ssid.c_str(), wifi_ap_pwd.c_str())) {
            Serial.println("[wifi] softAP reconfigure failed");
        }
    }

    wifi_handle_settings_get();
}

static String wifi_guess_content_type(const String &path)
{
    if (path.endsWith(".html") || path.endsWith(".htm")) return "text/html";
    if (path.endsWith(".css")) return "text/css";
    if (path.endsWith(".js")) return "application/javascript";
    if (path.endsWith(".json")) return "application/json";
    if (path.endsWith(".png")) return "image/png";
    if (path.endsWith(".jpg") || path.endsWith(".jpeg")) return "image/jpeg";
    if (path.endsWith(".svg")) return "image/svg+xml";
    if (path.endsWith(".ico")) return "image/x-icon";
    if (path.endsWith(".txt")) return "text/plain";
    return "application/octet-stream";
}

static bool wifi_serve_sd_path(String req_path)
{
    if (!peri_buf[E_PERI_SD_CARD]) return false;
    if (!sd_guard_lock(2000)) return false;
    if (req_path.length() == 0 || req_path == "/") req_path = "/index.html";
    if (req_path.endsWith("/")) req_path += "index.html";
    if (req_path.indexOf("..") >= 0) { sd_guard_unlock(); return false; }

    String fs_path = String("/webroot") + req_path;
    File f = SD.open(fs_path.c_str(), FILE_READ);
    if (!f || f.isDirectory()) {
        if (f) f.close();
        sd_guard_unlock();
        return false;
    }

    wifi_web_server.streamFile(f, wifi_guess_content_type(fs_path));
    f.close();
    sd_guard_unlock();
    return true;
}

static void wifi_start_web_services(void)
{
    if (wifi_web_started) return;

    wifi_dns_server.start(53, "*", WiFi.softAPIP());
    wifi_web_server.on("/", HTTP_GET, wifi_handle_api_root_get);
    wifi_web_server.on("/settings", HTTP_OPTIONS, wifi_handle_settings_options);
    wifi_web_server.on("/settings", HTTP_GET, wifi_handle_settings_get);
    wifi_web_server.on("/settings", HTTP_POST, wifi_handle_settings_post);
    wifi_web_server.onNotFound([]() {
        String host = wifi_web_server.hostHeader();
        if (host.equalsIgnoreCase("paper.api") || host.equalsIgnoreCase("paper.api:80")) {
            if (wifi_web_server.uri() == "/") {
                wifi_handle_api_root_get();
                return;
            }
            wifi_send_cors_headers();
            wifi_web_server.send(404, "application/json", "{\"error\":\"not_found\"}");
            return;
        }
        if (host.equalsIgnoreCase("paper.go") || host.equalsIgnoreCase("paper.go:80") || host.length() == 0) {
            if (wifi_serve_sd_path(wifi_web_server.uri())) return;
            wifi_web_server.send(404, "text/plain", "Not Found");
            return;
        }
        wifi_web_server.sendHeader("Location", "http://paper.go/");
        wifi_web_server.send(302, "text/plain", "Redirecting to http://paper.go/");
    });
    wifi_web_server.begin();
    wifi_web_started = true;
}

static void wifi_enable_apsta(void)
{
    WiFi.mode(WIFI_AP_STA);
    if (WiFi.softAPIP().toString() == "0.0.0.0") {
        if (!WiFi.softAP(wifi_ap_ssid.c_str(), wifi_ap_pwd.c_str())) {
            Serial.println("[wifi] softAP start failed");
        } else {
            Serial.printf("[wifi] AP started: %s IP=%s\n", wifi_ap_ssid.c_str(), WiFi.softAPIP().toString().c_str());
        }
    }
    wifi_start_web_services();
}

static void wifi_info_label_create(lv_obj_t *parent)
{
    ip_lab = lv_label_create(parent);
    epd_style_label(ip_lab);
    // lv_obj_set_style_text_color(ip_lab, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(ip_lab, &Font_Mono_Bold_25, LV_PART_MAIN);
    lv_label_set_text_fmt(ip_lab, "ip: %s", ui_wifi_get_ip());
    lv_obj_align_to(ip_lab, wifi_st_lab, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 10);

    ssid_lab = lv_label_create(parent);
    epd_style_label(ssid_lab);
    // lv_obj_set_style_text_color(ssid_lab, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(ssid_lab, &Font_Mono_Bold_25, LV_PART_MAIN);
    lv_label_set_text_fmt(ssid_lab, "ssid: %s", WiFi.SSID().c_str());
    lv_obj_align_to(ssid_lab, ip_lab, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 10);

    pwd_lab = lv_label_create(parent);
    epd_style_label(pwd_lab);
    // lv_obj_set_style_text_color(pwd_lab, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(pwd_lab, &Font_Mono_Bold_25, LV_PART_MAIN);
    lv_label_set_text_fmt(pwd_lab, "rssi: %ddB", WiFi.RSSI());
    lv_obj_align_to(pwd_lab, ssid_lab, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 10);
}

static void wifi_rssi_update_timer(lv_timer_t *t)
{
    if(ui_wifi_get_status())
    {
        lv_label_set_text_fmt(pwd_lab, "rssi: %ddB", WiFi.RSSI());
    }
}

static void wifi_config_event_handler(lv_event_t *e)
{
    static int step = 0;
    lv_event_code_t code  = lv_event_get_code(e);

    if(code != LV_EVENT_CLICKED) {
        return;
    }

    ui_refresh_set_mode(UI_REFRESH_MODE_FAST);

    if(ui_wifi_get_status()){
        Serial.println(" WiFi is connected do not need to configure WiFi.");
        return;
    }

    if (smartConfigStart) {
        Serial.println("[wifi config] Config Stop");
        if (wifi_timer) {
            lv_timer_del(wifi_timer);
            wifi_timer = NULL;
        }
        WiFi.stopSmartConfig();
        Serial.println("return smart Config has Start;");
        smartConfigStart = false;
        return;
    }
    wifi_enable_apsta();
    WiFi.disconnect();
    smartConfigStart = true;
    WiFi.beginSmartConfig();
    Serial.println("[wifi config] Config Start");
    lv_label_set_text(wifi_st_lab, "Wifi Config ...");
    
    wifi_timer = lv_timer_create([](lv_timer_t *t) {
        bool      destory = false;
        wifi_timer_counter++;
        if (wifi_timer_counter > wifi_connnect_timeout && !WiFi.isConnected()) {
            Serial.println("Connect timeout!");
            destory = true;
            Serial.println("[wifi config] Time Out");
        } else {
            switch (step)
            {
                case 0: lv_label_set_text(wifi_st_lab, "Connecting -"); break;
                case 1: lv_label_set_text(wifi_st_lab, "Connecting /"); break;
                case 2: lv_label_set_text(wifi_st_lab, "Connecting -"); break;
                case 3: lv_label_set_text(wifi_st_lab, "Connecting \\"); break;
                default:
                    break;
            }
            step++;
            step &= 0x3;
        }
        if (WiFi.isConnected()) {
            Serial.println("WiFi has connected!");
            Serial.printf("SSID:%s\r\n", WiFi.SSID().c_str());
            Serial.printf("PSW:%s\r\n", WiFi.psk().c_str());

            // if(strcmp(wifi_ssid, WiFi.SSID().c_str()) == 0) {
            //     Serial.printf("SSID == CURR SSID\r\n");
            // }
            // if(strcmp(wifi_password, WiFi.psk().c_str()) == 0) {
            //     Serial.printf("PSW == CURR PSW\r\n");
            // }
            
            // String ssid = WiFi.SSID();
            // String pwsd = WiFi.psk();
            // if(strcmp(wifi_ssid, ssid.c_str()) != 0 ||
            //    strcmp(wifi_password, pwsd.c_str()) != 0) {
            //     memcpy(wifi_ssid, ssid.c_str(), WIFI_SSID_MAX_LEN);
            //     memcpy(wifi_password, pwsd.c_str(), WIFI_PSWD_MAX_LEN);
            //     eeprom_wr_wifi(ssid.c_str(), ssid.length(), pwsd.c_str(), pwsd.length());
            // }

            destory   = true;
            String IP = WiFi.localIP().toString();
            ui_wifi_set_status(true);
            Serial.println("[wifi config] WiFi has connected!");

            lv_label_set_text(wifi_st_lab, (ui_wifi_get_status() == true ? "Wifi Connect" : "Wifi Disconnect"));
            
            wifi_info_label_create(scr6_root);
        }
        if (destory) {
            WiFi.stopSmartConfig();
            smartConfigStart = false;
            lv_timer_del(wifi_timer);
            wifi_timer         = NULL;
            wifi_timer_counter = 0;
        }
        // Every seconds check conected
    },
    1000, NULL);
}

static void wifi_ta_focus_event_handler(lv_event_t *e)
{
    lv_obj_t *ta = lv_event_get_target(e);
    if (wifi_keyboard) {
        lv_keyboard_set_textarea(wifi_keyboard, ta);
    }
}

static void wifi_apply_settings_event_handler(lv_event_t *e)
{
    if (e->code != LV_EVENT_CLICKED) return;

    wifi_sta_ssid = lv_textarea_get_text(wifi_sta_ssid_ta);
    wifi_sta_pwd  = lv_textarea_get_text(wifi_sta_pwd_ta);
    wifi_ap_ssid  = lv_textarea_get_text(wifi_ap_ssid_ta);
    String ap_pwd = lv_textarea_get_text(wifi_ap_pwd_ta);

    if (ap_pwd.length() >= 8) {
        wifi_ap_pwd = ap_pwd;
    }

    wifi_save_settings();
    Serial.printf("[wifi settings] saved STA ssid='%s' pwd_len=%u AP ssid='%s'\n",
                  wifi_sta_ssid.c_str(),
                  (unsigned)wifi_sta_pwd.length(),
                  wifi_ap_ssid.c_str());

    if (wifi_ap_ssid.length() > 0 && wifi_ap_pwd.length() >= 8) {
        WiFi.softAPdisconnect(true);
        if (!WiFi.softAP(wifi_ap_ssid.c_str(), wifi_ap_pwd.c_str())) {
            Serial.println("[wifi] softAP reconfigure failed");
        }
    }

    lv_label_set_text(wifi_st_lab, "Wifi Settings Applied");
}

static void scr6_btn_event_cb(lv_event_t * e)
{
    if(e->code == LV_EVENT_CLICKED){
        // ui_full_refresh();
        scr_mgr_pop(false);
    }
}

static void create6(lv_obj_t *parent) 
{
    ui_set_rotation(LV_DISP_ROT_NONE);

    scr6_root = parent;
    wifi_st_lab = lv_label_create(parent);
    lv_obj_set_width(wifi_st_lab, 360);
    // lv_obj_set_style_text_color(wifi_st_lab, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_font(wifi_st_lab, &Font_Mono_Bold_25, LV_PART_MAIN);
    epd_style_label(wifi_st_lab);
    lv_label_set_text(wifi_st_lab, (ui_wifi_get_status() ? "Wifi Connect" : "Wifi Disconnect"));
    lv_obj_set_style_text_align(wifi_st_lab, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    lv_obj_align(wifi_st_lab, LV_ALIGN_BOTTOM_RIGHT, -0, -190);

    wifi_load_saved_settings();
    wifi_enable_apsta();

    if(ui_wifi_get_status()) {
        wifi_info_label_create(parent);
    }

    lv_obj_t *label;
    lv_obj_t *form = lv_obj_create(parent);
    lv_obj_set_size(form, lv_pct(95), 220);
    lv_obj_align(form, LV_ALIGN_TOP_MID, 0, 50);
    lv_obj_set_style_pad_all(form, 10, LV_PART_MAIN);
    lv_obj_set_flex_flow(form, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(form, 8, LV_PART_MAIN);
    epd_style_transparent(form);

    auto create_field = [&](const char *title, lv_obj_t **out_ta, const char *value, bool pwd) {
        lv_obj_t *row = lv_obj_create(form);
        lv_obj_set_size(row, lv_pct(100), 45);
        lv_obj_set_style_pad_all(row, 4, LV_PART_MAIN);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        epd_style_transparent(row);
        lv_obj_t *lab = lv_label_create(row);
        lv_label_set_text(lab, title);
        epd_style_label(lab);
        lv_obj_set_width(lab, 170);
        lv_obj_set_style_text_font(lab, &Font_Mono_Bold_25, LV_PART_MAIN);
        lv_obj_t *ta = lv_textarea_create(row);
        lv_obj_set_size(ta, 280, 36);
        epd_style_textarea(ta);
        lv_textarea_set_one_line(ta, true);
        lv_textarea_set_text(ta, value);
        lv_obj_add_event_cb(ta, wifi_ta_focus_event_handler, LV_EVENT_FOCUSED, NULL);
        if (pwd) lv_textarea_set_password_mode(ta, true);
        *out_ta = ta;
    };

    create_field("WiFi SSID", &wifi_sta_ssid_ta, wifi_sta_ssid.c_str(), false);
    create_field("WiFi Password", &wifi_sta_pwd_ta, wifi_sta_pwd.c_str(), true);
    create_field("AP SSID", &wifi_ap_ssid_ta, wifi_ap_ssid.c_str(), false);
    create_field("AP Password", &wifi_ap_pwd_ta, wifi_ap_pwd.c_str(), true);

    wifi_keyboard = lv_keyboard_create(parent);
    lv_obj_set_height(wifi_keyboard, lv_pct(40));
    lv_obj_align(wifi_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(wifi_keyboard, wifi_sta_ssid_ta);
    keyboard_disable_long_press_repeat(wifi_keyboard);
    epd_style_keyboard(wifi_keyboard);

    // apply btn
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 200, 50);
    lv_obj_align_to(btn, wifi_keyboard, LV_ALIGN_OUT_TOP_RIGHT, -40, -10);
    lv_obj_set_style_radius(btn, 10, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 2, LV_PART_MAIN);
    epd_style_button(btn);
    label = lv_label_create(btn);
    lv_label_set_text(label, "Apply");
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(label, &Font_Mono_Bold_25, LV_PART_MAIN);
    epd_style_label(label);
    lv_obj_center(label);
    lv_obj_add_event_cb(btn, wifi_apply_settings_event_handler, LV_EVENT_CLICKED, NULL);
    lv_obj_move_foreground(btn);
    
    //---------------------
    // scr_middle_line(parent);
    // back
    scr_back_btn_create(parent, "Wifi", scr6_btn_event_cb);
}
static void entry6(void) 
{
    ui_setting_get_refresh_speed(&scr_refresh_mode);

    wifi_rssi_timer = lv_timer_create(wifi_rssi_update_timer, 3000, NULL);
}
static void exit6(void) 
{
    ui_refresh_set_mode(scr_refresh_mode);

    if (wifi_timer) {
        lv_timer_del(wifi_timer);
        wifi_timer = NULL;

        WiFi.stopSmartConfig();
        smartConfigStart = false;
        wifi_timer         = NULL;
        wifi_timer_counter = 0;
    }

    if (wifi_rssi_timer) {
        lv_timer_del(wifi_rssi_timer);
        wifi_rssi_timer = NULL;
    }
}
static void destroy6(void) 
{
    ui_set_rotation(LV_DISP_ROT_NONE);
}

void ui_wifi_service_loop(void)
{
    if (!wifi_web_started) return;
    wifi_dns_server.processNextRequest();
    wifi_web_server.handleClient();
}

static scr_lifecycle_t screen6 = {
    .create = create6,
    .entry = entry6,
    .exit  = exit6,
    .destroy = destroy6,
};
// end
#endif
//************************************[ screen 7 ]****************************************** battery
#if 1
static lv_obj_t *scr7_cont_letf;
static lv_obj_t *scr7_cont_right;
static lv_obj_t *batt_right[10] = {0};
static lv_obj_t *batt_left[10] = {0};
static lv_timer_t *batt_refr_timer = NULL;
#define line_max 28

static void battery_set_line(lv_obj_t *label, const char *str1, const char *str2)
{
    int w2 = strlen(str2);
    int w1 = line_max - w2;
    lv_label_set_text_fmt(label, "%-*s%-*s", w1, str1, w2, str2);
}

static void scr7_btn_event_cb(lv_event_t * e)
{
    if(e->code == LV_EVENT_CLICKED){
        scr_mgr_pop(false);
    }
}

static void battery_data_refr(void)
{
    char buf[line_max];
    // BQ25896
    if(battery_25896_is_vaild()) {
        battery_25896_refr();

        battery_set_line(batt_left[0], "Charge:", (battery_25896_is_chr() == true ? "Charging" : "Not charged"));

        lv_snprintf(buf, line_max, "%.2fV", battery_25896_get_VBUS());
        battery_set_line(batt_left[1], "VBUS:", buf);

        lv_snprintf(buf, line_max, "%.2fV", battery_25896_get_VSYS());
        battery_set_line(batt_left[2], "VSYS:", buf);

        lv_snprintf(buf, line_max, "%.2fV", battery_25896_get_VBAT());
        battery_set_line(batt_left[3], "VBAT:", buf);

        lv_snprintf(buf, line_max, "%.2fv", battery_25896_get_targ_VOLT());
        battery_set_line(batt_left[4], "VOLT Target:", buf);

        lv_snprintf(buf, line_max, "%.2fmA", battery_25896_get_CHG_CURR());
        battery_set_line(batt_left[5], "Charge Curr:", buf);

        lv_snprintf(buf, line_max, "%.2fmA", battery_25896_get_PREC_CURR());
        battery_set_line(batt_left[6], "Precharge Curr:", buf);

        lv_snprintf(buf, line_max, "%s", battery_25896_get_CHG_ST());
        battery_set_line(batt_left[7], "CHG Status:", buf);

        lv_snprintf(buf, line_max, "%s", battery_25896_get_VBUS_ST());
        battery_set_line(batt_left[8], "VBUS Status:", buf);

        lv_snprintf(buf, line_max, "%s", battery_25896_get_NTC_ST());
        battery_set_line(batt_left[9], "NCT:", buf);

    }

    // BQ27220
    if(ui_battery_27220_is_vaild()) {
        battery_set_line(batt_right[0], "VBUS Input:", (ui_battery_27220_get_input() == true? "Connected" : "Disonnected"));

        if(ui_battery_27220_get_input() == true ){
            lv_snprintf(buf, line_max, "%s", (ui_battery_27220_get_charge_finish()? "Finsish":"Charging"));
        } else {
            lv_snprintf(buf, line_max, "%s", "Discharge");
        }
        battery_set_line(batt_right[1], "Charing Status:", buf);

        lv_snprintf(buf, line_max, "0x%x", ui_battery_27220_get_status());
        battery_set_line(batt_right[2], "Battery Status:", buf);

        lv_snprintf(buf, line_max, "%dmV", ui_battery_27220_get_voltage());
        battery_set_line(batt_right[3], "Voltage:", buf);

        lv_snprintf(buf, line_max, "%dmA", ui_battery_27220_get_current());
        battery_set_line(batt_right[4], "Current:", buf);

        lv_snprintf(buf, line_max, "%.2fC", (float)(ui_battery_27220_get_temperature() / 10.0 - 273.0));
        battery_set_line(batt_right[5], "Temperature:", buf);

        lv_snprintf(buf, line_max, "%dmAh", ui_battery_27220_get_remain_capacity());
        battery_set_line(batt_right[6], "Capacity Remain:", buf);

        lv_snprintf(buf, line_max, "%dmAh", ui_battery_27220_get_full_capacity());
        battery_set_line(batt_right[7], "Capacity Full:", buf);

        lv_snprintf(buf, line_max, "%d%%", ui_battery_27220_get_percent());
        battery_set_line(batt_right[8], "Capacity Percent:", buf);

        lv_snprintf(buf, line_max, "%d%%", ui_battery_27220_get_health());
        battery_set_line(batt_right[9], "Capacity Health:", buf);
    }
}

static void batt_refr_timer_event(lv_timer_t *t)
{
    battery_data_refr();
    // ui_epd_refr(EPD_REFRESH_TIME, 2, 2);
}

static lv_obj_t * scr7_create_label(lv_obj_t *parent)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_width(label, LCD_HOR_SIZE/2-50);
    lv_obj_set_style_text_font(label, &Font_Mono_Bold_25, LV_PART_MAIN);
    lv_obj_set_style_border_width(label, 1, LV_PART_MAIN);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_border_side(label, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    return label;
}

static void create7(lv_obj_t *parent)
{
    ui_set_rotation(LV_DISP_ROT_270);

    lv_obj_t *label;

    // left cont
    scr7_cont_letf = lv_obj_create(parent);
    lv_obj_set_size(scr7_cont_letf, lv_pct(49), lv_pct(85));
    lv_obj_set_style_bg_color(scr7_cont_letf, lv_color_hex(EPD_COLOR_BG), LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(scr7_cont_letf, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(scr7_cont_letf, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(scr7_cont_letf, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(scr7_cont_letf, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(scr7_cont_letf, 20, LV_PART_MAIN);
    lv_obj_set_flex_flow(scr7_cont_letf, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(scr7_cont_letf, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_column(scr7_cont_letf, 5, LV_PART_MAIN);
    lv_obj_set_align(scr7_cont_letf, LV_ALIGN_BOTTOM_LEFT);

    // left
    if(!battery_25896_is_vaild()) {
        label = scr7_create_label(scr7_cont_letf);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_label_set_text_fmt(label, "%s", "[0x6B] BQ25896 NOT FOUND");
        goto NO_BATTERY_BQ25896;
    }

    label = scr7_create_label(scr7_cont_letf);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_text_fmt(label, "%s", "[0x6B] BQ25896");

    for(int i = 0; i < sizeof(batt_left) / sizeof(batt_left[0]); i++) {
        batt_left[i] = scr7_create_label(scr7_cont_letf);
    }

    battery_set_line(batt_left[0], "Charge:", "---");
    battery_set_line(batt_left[1], "VBUS:", "---");
    battery_set_line(batt_left[2], "VBUS Status:", "---");
    battery_set_line(batt_left[3], "VSYS:", "---");
    battery_set_line(batt_left[4], "VSYS Status:", "---");
    battery_set_line(batt_left[5], "VBAT:", "---");
    battery_set_line(batt_left[6], "ICHG:", "---");
    battery_set_line(batt_left[7], "TEMP:", "---");
    battery_set_line(batt_left[8], "TSPCT:", "---");
    battery_set_line(batt_left[9], "Charger Err:", "---");

    // right cont
NO_BATTERY_BQ25896:

    scr7_cont_right = lv_obj_create(parent);
    lv_obj_set_size(scr7_cont_right, lv_pct(49), lv_pct(85));
    lv_obj_set_style_bg_color(scr7_cont_right, lv_color_hex(EPD_COLOR_BG), LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(scr7_cont_right, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(scr7_cont_right, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(scr7_cont_right, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(scr7_cont_right, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(scr7_cont_right, 10, LV_PART_MAIN);
    lv_obj_set_flex_flow(scr7_cont_right, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(scr7_cont_right, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_column(scr7_cont_right, 5, LV_PART_MAIN);
    lv_obj_set_align(scr7_cont_right, LV_ALIGN_BOTTOM_RIGHT);

    // right
    if(!ui_battery_27220_is_vaild()) {
        label = scr7_create_label(scr7_cont_right);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_label_set_text_fmt(label, "%s", "[0x55] BQ27220 NOT FOUND");
        goto NO_BATTERY;
    }
    label = scr7_create_label(scr7_cont_right);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_text_fmt(label, "%s", "[0x55] BQ27220");

    for(int i = 0; i < sizeof(batt_right) / sizeof(batt_right[0]); i++) {
        batt_right[i] = scr7_create_label(scr7_cont_right);
    }

    battery_set_line(batt_right[0], "Charge:", "---");
    battery_set_line(batt_right[1], "VOLT:", "---");
    battery_set_line(batt_right[2], "VOLT Charge:", "---");
    battery_set_line(batt_right[3], "CURR Average:", "---");
    battery_set_line(batt_right[4], "CURR Instant:", "---");
    battery_set_line(batt_right[5], "Curr Standby:", "---");
    battery_set_line(batt_right[6], "Curr Charging:", "---");
    battery_set_line(batt_right[7], "TEMP:", "---");
    battery_set_line(batt_right[8], "CAP BATT:", "---");
    battery_set_line(batt_right[9], "CAP BATT Full:", "---");

NO_BATTERY:
    //---------------------
    scr_middle_line(parent);
    // back
    scr_back_btn_create(parent, "battery", scr7_btn_event_cb);
    // timer
    batt_refr_timer = lv_timer_create(batt_refr_timer_event, 5000, NULL);
    lv_timer_pause(batt_refr_timer);
}

static void entry7(void) {
    battery_data_refr();
    lv_timer_resume(batt_refr_timer);
}
static void exit7(void) {
    lv_timer_pause(batt_refr_timer);
}
static void destroy7(void) { 
    lv_timer_del(batt_refr_timer);
    if(batt_refr_timer){
        batt_refr_timer = NULL;
    }
    ui_set_rotation(LV_DISP_ROT_NONE);
}

static scr_lifecycle_t screen7 = {
    .create = create7,
    .entry = entry7,
    .exit  = exit7,
    .destroy = destroy7,
};
#undef line_max
#endif
//************************************[ screen 8 ]****************************************** gps
#if 1
#define line_max 32
static lv_obj_t *scr3_cont;
static lv_obj_t *scr3_cnt_lab;
static lv_obj_t *scr8_lab_buf[8];
static lv_timer_t *GPS_loop_timer = NULL;

static void scr_label_line_algin(lv_obj_t *label, int line_len, const char *str1, const char *str2)
{
    int w2 = strlen(str2);
    int w1 = line_len - w2;
    lv_label_set_text_fmt(label, "%-*s%-*s", w1, str1, w2, str2);
}

static lv_obj_t * scr3_create_label(lv_obj_t *parent)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_width(label, lv_pct(90));
    lv_obj_set_style_text_font(label, &Font_Mono_Bold_25, LV_PART_MAIN);   
    lv_obj_set_style_border_width(label, 1, LV_PART_MAIN);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_border_side(label, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
    return label;
}

static void scr3_GPS_updata(void)
{
    double lat      = 0; // Latitude
    double lon      = 0; // Longitude
    double speed    = 0; // Speed over ground
    float alt      = 0; // Altitude
    float accuracy = 0; // Accuracy
    uint32_t   vsat     = 0; // Visible Satellites
    int   usat     = 0; // Used Satellites
    uint16_t   year     = 0; // 
    uint8_t   month    = 0; // 
    uint8_t   day      = 0; // 
    uint8_t   hour     = 0; // 
    uint8_t   min      = 0; // 
    uint8_t   sec      = 0; // 

    static int cnt = 0;

    lv_label_set_text_fmt(scr3_cnt_lab, " %05d ", ui_gps_get_charsProcessed());

    ui_gps_get_coord(&lat, &lon);
    ui_gps_get_data(&year, &month, &day);
    ui_gps_get_time(&hour, &min, &sec);
    ui_gps_get_satellites(&vsat);
    ui_gps_get_speed(&speed);

    lv_snprintf(global_buf, GLOBAL_BUF_LEN, "%0.3f", lat);
    scr_label_line_algin(scr8_lab_buf[0], line_max, "latitude:", global_buf);

    lv_snprintf(global_buf, GLOBAL_BUF_LEN, "%0.3f", lon);
    scr_label_line_algin(scr8_lab_buf[1], line_max, "longitude:", global_buf);

    lv_snprintf(global_buf, GLOBAL_BUF_LEN, "%d", year);
    scr_label_line_algin(scr8_lab_buf[2], line_max, "year:", global_buf);

    lv_snprintf(global_buf, GLOBAL_BUF_LEN, "%d", month);
    scr_label_line_algin(scr8_lab_buf[3], line_max, "month:", global_buf);

    lv_snprintf(global_buf, GLOBAL_BUF_LEN, "%d", day);
    scr_label_line_algin(scr8_lab_buf[4], line_max, "day:", global_buf);

    lv_snprintf(global_buf, GLOBAL_BUF_LEN, "%02d:%02d:%02d", hour, min, sec);
    scr_label_line_algin(scr8_lab_buf[5], line_max, "time:", global_buf);

    lv_snprintf(global_buf, GLOBAL_BUF_LEN, "%0.2f kmph", speed);
    scr_label_line_algin(scr8_lab_buf[6], line_max, "Speed:", global_buf);

    lv_snprintf(global_buf, GLOBAL_BUF_LEN, "%d", vsat);
    scr_label_line_algin(scr8_lab_buf[7], line_max, "satellites:", global_buf);

    // lv_snprintf(global_buf, GLOBAL_BUF_LEN, "%0.1f", alt);
    // scr_label_line_algin(scr8_lab_buf[8], line_max, "alt:", global_buf);

    // lv_snprintf(global_buf, GLOBAL_BUF_LEN, "%d", usat);
    // scr_label_line_algin(scr8_lab_buf[9], line_max, "usat:", global_buf);

}

static void GPS_loop_timer_event(lv_timer_t * t)
{
    // ui_GPS_print_info();
    scr3_GPS_updata();
}

static void scr10_btn_event_cb(lv_event_t * e)
{
    if(e->code == LV_EVENT_CLICKED){
        scr_mgr_pop(false);
    }
}

static void create10(lv_obj_t *parent)
{
    scr3_cont = lv_obj_create(parent);
    lv_obj_set_size(scr3_cont, lv_pct(100), lv_pct(88));
    // lv_obj_set_style_bg_color(scr3_cont, DECKPRO_COLOR_BG, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(scr3_cont, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(scr3_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(scr3_cont, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(scr3_cont, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(scr3_cont, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(scr3_cont, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_column(scr3_cont, 0, LV_PART_MAIN);
    lv_obj_set_align(scr3_cont, LV_ALIGN_BOTTOM_LEFT);
    lv_obj_set_flex_flow(scr3_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(scr3_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);

    for(int i = 0; i < sizeof(scr8_lab_buf) / sizeof(scr8_lab_buf[0]); i++) {
        scr8_lab_buf[i] = scr3_create_label(scr3_cont);
        lv_label_set_text(scr8_lab_buf[i], " ");
    }

    scr3_cnt_lab = lv_label_create(parent);
    lv_obj_set_style_text_font(scr3_cnt_lab, &Font_Mono_Bold_25, LV_PART_MAIN);
    lv_obj_set_style_radius(scr3_cnt_lab, 5, LV_PART_MAIN);
    lv_obj_set_style_border_width(scr3_cnt_lab, 2, LV_PART_MAIN);
    lv_obj_set_style_text_align(scr3_cnt_lab, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text_fmt(scr3_cnt_lab, " %05d ", 0);
    lv_obj_center(scr3_cnt_lab);
    lv_obj_align(scr3_cnt_lab, LV_ALIGN_TOP_RIGHT, -20, 20);

    // back 
    scr_back_btn_create(parent, "GPS", scr10_btn_event_cb);
}

static void entry10(void) {
    scr3_GPS_updata();
    ui_gps_task_resume();
    GPS_loop_timer = lv_timer_create(GPS_loop_timer_event, 3000, NULL);
 
}
static void exit10(void) 
{
    // Do not suspend GPS here. GPS logging is a background service and must continue after leaving the GPS screen.
    // Keep GPS running in the background so CSV logging continues whenever the device is on.
    // Only stop the UI refresh timer when leaving the GPS screen.
    if(GPS_loop_timer) {
        lv_timer_del(GPS_loop_timer);
        GPS_loop_timer = NULL;
    }
}
static void destroy10(void) { 

}

static scr_lifecycle_t screen10 = {
    .create = create10,
    .entry = entry10,
    .exit  = exit10,
    .destroy = destroy10,
};
#endif

//************************************[ screen 11 ]****************************************** markdown
#if 1
static lv_obj_t *md_cont = NULL;
static lv_obj_t *md_label = NULL;
static lv_obj_t *md_span = NULL;
static char md_text_buf[4096] = {0};
typedef enum { DOC_TYPE_MD = 0, DOC_TYPE_TEXT, DOC_TYPE_HTML, DOC_TYPE_CSV } doc_type_t;
static doc_type_t md_doc_type = DOC_TYPE_MD;

static const lv_font_t *md_header_font_from_level(int level)
{
    switch(level) {
        case 1: return &Font_Mono_Bold_90;
        case 2: return &Font_Mono_Bold_30;
        case 3: return &Font_Mono_Bold_25;
        case 4: return &Font_Mono_Bold_20;
        default: return &Font_Geist_Bold_20; // ##### same size as content, bold
    }
}

static bool md_is_unordered_list(const char *line, size_t line_len, size_t *content_start)
{
    if(line_len < 2) return false;
    if((line[0] == '-' || line[0] == '*' || line[0] == '+') && line[1] == ' ') {
        if(content_start) *content_start = 2;
        return true;
    }
    return false;
}

static bool md_is_ordered_list(const char *line, size_t line_len, size_t *content_start)
{
    size_t i = 0;
    while(i < line_len && line[i] >= '0' && line[i] <= '9') i++;
    if(i > 0 && i + 1 < line_len && (line[i] == '.' || line[i] == ')') && line[i + 1] == ' ') {
        if(content_start) *content_start = i + 2;
        return true;
    }
    return false;
}

static bool md_path_ends_with(const char *path, const char *suffix)
{
    if(!path || !suffix) return false;
    size_t path_len = strlen(path), suffix_len = strlen(suffix);
    if(path_len < suffix_len) return false;
    return strcasecmp(path + path_len - suffix_len, suffix) == 0;
}

static void md_add_text_span(const char *txt, const lv_font_t *font)
{
    lv_span_t *sp = lv_spangroup_new_span(md_span);
    lv_style_set_text_font(&sp->style, font);
    lv_style_set_text_color(&sp->style, lv_color_hex(EPD_COLOR_FG));
    lv_span_set_text(sp, txt);
}

static void md_add_line_markdown_inline(const char *line, size_t len, const lv_font_t *normal_font, const lv_font_t *bold_font)
{
    size_t i = 0;
    while(i < len) {
        if(i + 1 < len && line[i] == '*' && line[i + 1] == '*') {
            size_t end = i + 2;
            while(end + 1 < len && !(line[end] == '*' && line[end + 1] == '*')) end++;
            if(end + 1 < len) {
                char bold_part[512];
                lv_snprintf(bold_part, sizeof(bold_part), "%.*s", (int)(end - (i + 2)), &line[i + 2]);
                md_add_text_span(bold_part, bold_font);
                i = end + 2;
                continue;
            }
        }

        if(line[i] == '`') {
            size_t end = i + 1;
            while(end < len && line[end] != '`') end++;
            if(end < len) {
                char code_part[512];
                lv_snprintf(code_part, sizeof(code_part), "%.*s", (int)(end - (i + 1)), &line[i + 1]);
                md_add_text_span(code_part, &Font_Mono_Bold_20);
                i = end + 1;
                continue;
            }
        }

        if(line[i] == '*') {
            size_t end = i + 1;
            while(end < len && line[end] != '*') end++;
            if(end < len) {
                char italic_part[512];
                lv_snprintf(italic_part, sizeof(italic_part), "%.*s", (int)(end - (i + 1)), &line[i + 1]);
                md_add_text_span(italic_part, normal_font);
                i = end + 1;
                continue;
            }
        }

        if(line[i] == '[') {
            size_t close_bracket = i + 1;
            while(close_bracket < len && line[close_bracket] != ']') close_bracket++;
            if(close_bracket + 1 < len && line[close_bracket + 1] == '(') {
                size_t close_paren = close_bracket + 2;
                while(close_paren < len && line[close_paren] != ')') close_paren++;
                if(close_paren < len) {
                    char link_text[512];
                    lv_snprintf(link_text, sizeof(link_text), "%.*s", (int)(close_bracket - (i + 1)), &line[i + 1]);
                    md_add_text_span(link_text, bold_font);
                    i = close_paren + 1;
                    continue;
                }
            }
        }

        char ch[2] = {line[i], '\0'};
        md_add_text_span(ch, normal_font);
        i++;
    }
}

static void md_render_to_spangroup(const char *text)
{
    lv_spangroup_set_mode(md_span, LV_SPAN_MODE_BREAK);
    lv_spangroup_set_overflow(md_span, LV_SPAN_OVERFLOW_CLIP);
    lv_spangroup_set_indent(md_span, 0);
    lv_spangroup_set_align(md_span, LV_TEXT_ALIGN_LEFT);
    lv_spangroup_del_span(md_span, NULL);

    if(text == NULL) return;

    if(md_doc_type == DOC_TYPE_CSV) {
        const char *line = text;
        while(*line) {
            const char *line_end = strchr(line, '\n');
            size_t line_len = line_end ? (size_t)(line_end - line) : strlen(line);
            char row[512]; size_t w = 0;
            for(size_t i = 0; i < line_len && w + 3 < sizeof(row) - 1; ++i) {
                if(line[i] == ',') { row[w++] = ' '; row[w++] = '|'; row[w++] = ' '; }
                else row[w++] = line[i];
            }
            row[w] = '\0';
            md_add_text_span(row, &Font_Mono_Bold_20);
            md_add_text_span("\n", &Font_Mono_Bold_20);
            if(!line_end) break;
            line = line_end + 1;
        }
        return;
    }

    if(md_doc_type == DOC_TYPE_HTML) {
        const char *p = text;
        bool bold_on = false;
        bool in_ordered = false;
        int ol_idx = 1;
        while(*p) {
            if(*p == '<') {
                const char *tag_end = strchr(p, '>');
                if(!tag_end) break;
                if(strncasecmp(p, "<h1", 3) == 0) md_add_text_span("\n", &Font_Geist_Light_20);
                else if(strncasecmp(p, "<h2", 3) == 0) md_add_text_span("\n", &Font_Geist_Light_20);
                else if(strncasecmp(p, "<h3", 3) == 0) md_add_text_span("\n", &Font_Geist_Light_20);
                else if(strncasecmp(p, "<h4", 3) == 0) md_add_text_span("\n", &Font_Geist_Light_20);
                else if(strncasecmp(p, "<h5", 3) == 0) md_add_text_span("\n", &Font_Geist_Light_20);
                else if(strncasecmp(p, "</h", 3) == 0 || strncasecmp(p, "<p", 2) == 0 || strncasecmp(p, "</p", 3) == 0 || strncasecmp(p, "<br", 3) == 0 || strncasecmp(p, "</tr", 4) == 0 || strncasecmp(p, "<table", 6) == 0 || strncasecmp(p, "</table", 7) == 0) md_add_text_span("\n", &Font_Geist_Light_20);
                else if(strncasecmp(p, "<ul", 3) == 0 || strncasecmp(p, "</ul", 4) == 0) md_add_text_span("\n", &Font_Geist_Light_20);
                else if(strncasecmp(p, "<ol", 3) == 0) { in_ordered = true; ol_idx = 1; md_add_text_span("\n", &Font_Geist_Light_20); }
                else if(strncasecmp(p, "</ol", 4) == 0) { in_ordered = false; md_add_text_span("\n", &Font_Geist_Light_20); }
                else if(strncasecmp(p, "<li", 3) == 0) {
                    if(in_ordered) { char n[16]; lv_snprintf(n, sizeof(n), "%d. ", ol_idx++); md_add_text_span(n, &Font_Geist_Light_20); }
                    else md_add_text_span("• ", &Font_Geist_Light_20);
                }
                else if(strncasecmp(p, "</li", 4) == 0) md_add_text_span("\n", &Font_Geist_Light_20);
                else if(strncasecmp(p, "<td", 3) == 0 || strncasecmp(p, "<th", 3) == 0) {}
                else if(strncasecmp(p, "</td", 4) == 0 || strncasecmp(p, "</th", 4) == 0) md_add_text_span(" | ", &Font_Mono_Bold_20);
                else if(strncasecmp(p, "<b>", 3) == 0 || strncasecmp(p, "<strong", 7) == 0) bold_on = true;
                else if(strncasecmp(p, "</b>", 4) == 0 || strncasecmp(p, "</strong", 9) == 0) bold_on = false;
                p = tag_end + 1;
                continue;
            }
            const char *txt_end = strchr(p, '<');
            size_t len = txt_end ? (size_t)(txt_end - p) : strlen(p);
            if(len > 0) {
                char seg[512];
                lv_snprintf(seg, sizeof(seg), "%.*s", (int)len, p);
                md_add_text_span(seg, bold_on ? &Font_Geist_Bold_20 : &Font_Geist_Light_20);
            }
            if(!txt_end) break;
            p = txt_end;
        }
        return;
    }

    const char *line = text;
    bool in_code_block = false;
    while(*line) {
        const char *line_end = strchr(line, '\n');
        size_t line_len = line_end ? (size_t)(line_end - line) : strlen(line);

        bool header = false;
        bool table = false;
        size_t content_offset = 0;
        size_t content_len = line_len;
        int hashes = 0;

        if(line_len >= 3 && strncmp(line, "```", 3) == 0) {
            in_code_block = !in_code_block;
            md_add_text_span("\n", &Font_Geist_Light_20);
            goto next_line;
        }

        if(!in_code_block) {
            while(hashes < (int)line_len && hashes < 5 && line[hashes] == '#') hashes++;
            if(hashes > 0 && hashes < (int)line_len && line[hashes] == ' ') {
                header = true;
                content_offset = (size_t)hashes + 1;
                content_len = line_len - content_offset;
            } else if(md_is_unordered_list(line, line_len, &content_offset)) {
                char bullet[8] = "• ";
                md_add_text_span(bullet, &Font_Geist_Light_20);
                content_len = line_len - content_offset;
            } else if(md_is_ordered_list(line, line_len, &content_offset)) {
                char prefix[24];
                lv_snprintf(prefix, sizeof(prefix), "%.*s ", (int)(content_offset - 1), line);
                md_add_text_span(prefix, &Font_Geist_Light_20);
                content_len = line_len - content_offset;
            } else if(line_len > 1 && line[0] == '>' && line[1] == ' ') {
                md_add_text_span("│ ", &Font_Geist_Light_20);
                content_offset = 2;
                content_len = line_len - content_offset;
            } else if(line_len >= 3 && ((line[0] == '-' && line[1] == '-' && line[2] == '-') || (line[0] == '*' && line[1] == '*' && line[2] == '*'))) {
                md_add_text_span("--------------------------------\n", &Font_Mono_Bold_20);
                goto next_line;
            } else if(line_len > 0 && line[0] == '|') {
                table = true;
                content_offset = 0;
            }
        }

        if(header) {
            // Ensure headers are visually separated from surrounding body text.
            md_add_text_span("\n\n\n\n", &Font_Geist_Light_20);
            md_add_line_markdown_inline(&line[content_offset], content_len, md_header_font_from_level(hashes), md_header_font_from_level(hashes));
            md_add_text_span("\n\n\n", &Font_Geist_Light_20);
        } else if(in_code_block) {
            char code_line[512];
            lv_snprintf(code_line, sizeof(code_line), "%.*s\n", (int)line_len, line);
            md_add_text_span(code_line, &Font_Mono_Bold_20);
        } else if(table) {
            char table_line[512];
            lv_snprintf(table_line, sizeof(table_line), "%.*s\n", (int)line_len, line);
            md_add_text_span(table_line, &Font_Mono_Bold_20);
        } else {
            md_add_line_markdown_inline(&line[content_offset], content_len, &Font_Geist_Light_20, &Font_Geist_Bold_20);
            md_add_text_span("\n", &Font_Geist_Light_20);
        }

next_line:
        if(!line_end) break;
        line = line_end + 1;
    }
}

static void scr11_btn_event_cb(lv_event_t * e)
{
    if(e->code == LV_EVENT_CLICKED){
        scr_mgr_pop(false);
    }
}

static void md_load_file(const char *path)
{
    md_text_buf[0] = '\0';
    md_doc_type = DOC_TYPE_MD;
    if(path == NULL || path[0] == '\0') {
        lv_snprintf(md_text_buf, sizeof(md_text_buf), "No markdown file selected from SD browser.");
        return;
    }
    if (!peri_buf[E_PERI_SD_CARD]) {
        lv_snprintf(md_text_buf, sizeof(md_text_buf), "SD unavailable.");
        return;
    }
    if (!sd_guard_lock(2000)) {
        lv_snprintf(md_text_buf, sizeof(md_text_buf), "SD busy; try again.");
        return;
    }
    File f = SD.open(path, FILE_READ);
    if(!f || f.isDirectory()) {
        lv_snprintf(md_text_buf, sizeof(md_text_buf), "Failed to open:\n%s", path);
        sd_guard_unlock();
        return;
    }
    size_t idx = 0;
    while(f.available() && idx < sizeof(md_text_buf) - 1) {
        char c = (char)f.read();
        if(c != '\r') md_text_buf[idx++] = c;
    }
    md_text_buf[idx] = '\0';
    f.close();
    sd_guard_unlock();
    if(md_path_ends_with(path, ".txt")) md_doc_type = DOC_TYPE_TEXT;
    else if(md_path_ends_with(path, ".html") || md_path_ends_with(path, ".htm")) md_doc_type = DOC_TYPE_HTML;
    else if(md_path_ends_with(path, ".csv")) md_doc_type = DOC_TYPE_CSV;
}

static void create11(lv_obj_t *parent)
{
    md_cont = lv_obj_create(parent);
    lv_obj_set_size(md_cont, lv_pct(96), lv_pct(84));
    lv_obj_align(md_cont, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(md_cont, lv_color_hex(EPD_COLOR_BG), LV_PART_MAIN);
    lv_obj_set_style_pad_all(md_cont, 8, LV_PART_MAIN);
    lv_obj_set_style_border_width(md_cont, 1, LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(md_cont, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_scroll_dir(md_cont, LV_DIR_VER);

    md_span = lv_spangroup_create(md_cont);
    lv_obj_set_width(md_span, lv_pct(100));
    lv_obj_set_style_text_color(md_span, lv_color_hex(EPD_COLOR_FG), LV_PART_MAIN);
    lv_obj_set_style_pad_all(md_span, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(md_span, 4, LV_PART_MAIN);
    md_label = NULL;
    md_render_to_spangroup("Open a markdown file from SD.");

    scr_back_btn_create(parent, "Markdown Reader", scr11_btn_event_cb);
}

static void entry11(void)
{
    md_load_file(md_open_path);
    md_render_to_spangroup(md_text_buf);
    lv_obj_scroll_to_y(md_cont, 0, LV_ANIM_OFF);
}
static void exit11(void) { }
static void destroy11(void) { }

static scr_lifecycle_t screen11 = {
    .create = create11,
    .entry = entry11,
    .exit  = exit11,
    .destroy = destroy11,
};
#endif

//************************************[ screen 12 ]****************************************** web browser
#if 1
static lv_obj_t *web_url_ta = NULL;
static lv_obj_t *web_keyboard = NULL;
static lv_obj_t *web_cont = NULL;
static lv_obj_t *web_span = NULL;
static char web_url_buf[256] = "https://example.com";
static lv_timer_t *web_wifi_connect_timer = NULL;
static TaskHandle_t web_fetch_task_handle = NULL;
static volatile bool web_fetch_in_progress = false;
static volatile bool web_fetch_done = false;
static String web_fetch_result;
static uint32_t web_wifi_connect_start_ms = 0;
static bool web_pending_fetch_after_wifi = false;
static const uint32_t WEB_WIFI_CONNECT_TIMEOUT_MS = 20000;

static void web_show_text(const char *text)
{
    md_span = web_span;
    md_doc_type = DOC_TYPE_HTML;
    md_render_to_spangroup(text);
    lv_obj_scroll_to_y(web_cont, 0, LV_ANIM_OFF);
}

static void web_fetch_worker(void *param)
{
    char *in_url = (char *)param;
    char url[256] = {0};
    HTTPClient http;
    bool http_started = false;
    int code = 0;
    if(strstr(in_url, "http://") == in_url || strstr(in_url, "https://") == in_url) lv_snprintf(url, sizeof(url), "%s", in_url);
    else lv_snprintf(url, sizeof(url), "http://%s", in_url);

    if(WiFi.status() != WL_CONNECTED) { web_fetch_result = "Wi-Fi is not connected. Open Wi-Fi app and connect first."; goto done; }

    if(!http.begin(url)) {
        web_fetch_result = "Failed to initialize HTTP client.";
        goto done;
    }
    http_started = true;
    http.setTimeout(10000);
    code = http.GET();
    if(code <= 0) {
        web_fetch_result = String("HTTP GET failed: ") + String(code);
        goto done;
    }
    web_fetch_result = http.getString();
done:
    if (http_started) http.end();
    if (in_url) free(in_url);
    web_fetch_done = true;
    web_fetch_in_progress = false;
    vTaskDelete(NULL);
}

static void web_fetch_and_render(const char *in_url)
{
    if (web_fetch_in_progress) { web_show_text("Browser request in progress..."); return; }
    if(in_url == NULL || in_url[0] == '\0') { web_show_text("Empty URL."); return; }
    char *url_copy = (char *)malloc(strlen(in_url) + 1);
    if (!url_copy) { web_show_text("Out of memory."); return; }
    strcpy(url_copy, in_url);
    web_fetch_done = false;
    web_fetch_in_progress = true;
    if (xTaskCreate(web_fetch_worker, "web_fetch_worker", 8192, url_copy, 1, &web_fetch_task_handle) != pdPASS) {
        free(url_copy);
        web_fetch_in_progress = false;
        web_show_text("Failed to start browser worker.");
    } else {
        web_show_text("Loading page...");
    }
}

static void web_wifi_connect_timer_cb(lv_timer_t *t)
{
    if (web_fetch_done) {
        web_fetch_done = false;
        lv_snprintf(md_text_buf, sizeof(md_text_buf), "%s", web_fetch_result.c_str());
        web_show_text(md_text_buf);
        return;
    }
    (void)t;
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[web] WiFi connected ip=%s\n", WiFi.localIP().toString().c_str());

        if (web_wifi_connect_timer) {
            lv_timer_del(web_wifi_connect_timer);
            web_wifi_connect_timer = NULL;
        }

        ui_wifi_set_status(true);
        web_pending_fetch_after_wifi = false;

        web_show_text("WiFi connected. Loading page...");
        web_fetch_and_render(web_url_buf);
        return;
    }

    uint32_t elapsed = millis() - web_wifi_connect_start_ms;
    if (elapsed >= WEB_WIFI_CONNECT_TIMEOUT_MS) {
        Serial.println("[web] WiFi connect timeout");

        if (web_wifi_connect_timer) {
            lv_timer_del(web_wifi_connect_timer);
            web_wifi_connect_timer = NULL;
        }

        web_pending_fetch_after_wifi = false;
        ui_wifi_set_status(false);

        lv_snprintf(md_text_buf, sizeof(md_text_buf),
                    "WiFi connection timed out.\n\nSaved SSID: %s\n\nOpen WiFi Settings and verify the password.",
                    wifi_sta_ssid.c_str());
        web_show_text(md_text_buf);
        return;
    }

    lv_snprintf(md_text_buf, sizeof(md_text_buf),
                "Connecting to WiFi...\n\nSSID: %s\nElapsed: %lu sec",
                wifi_sta_ssid.c_str(),
                (unsigned long)(elapsed / 1000));
    web_show_text(md_text_buf);
}

static void web_connect_and_fetch_saved_url(void)
{
    if (WiFi.status() == WL_CONNECTED) {
        ui_wifi_set_status(true);
        web_fetch_and_render(web_url_buf);
        return;
    }

    wifi_load_saved_settings();

    if (wifi_sta_ssid.length() == 0) {
        web_show_text("No saved WiFi SSID.\n\nOpen WiFi Settings, enter credentials, tap Apply, then return to Browser.");
        return;
    }

    web_show_text("Connecting to WiFi...");
    web_pending_fetch_after_wifi = true;
    web_wifi_connect_start_ms = millis();

    if (!wifi_connect_saved_sta("web browser")) {
        web_pending_fetch_after_wifi = false;
        web_show_text("Unable to start WiFi connection. Check saved WiFi settings.");
        return;
    }

    if (web_wifi_connect_timer) {
        lv_timer_del(web_wifi_connect_timer);
        web_wifi_connect_timer = NULL;
    }

    web_wifi_connect_timer = lv_timer_create(web_wifi_connect_timer_cb, 1000, NULL);
    lv_timer_ready(web_wifi_connect_timer);
}

static void web_back_btn_event(lv_event_t *e) { if(e->code == LV_EVENT_CLICKED) scr_mgr_pop(false); }
static void web_go_btn_event(lv_event_t *e)
{
    if(e->code != LV_EVENT_CLICKED) return;
    const char *url = lv_textarea_get_text(web_url_ta);
    lv_snprintf(web_url_buf, sizeof(web_url_buf), "%s", url ? url : "");
    if (WiFi.status() == WL_CONNECTED) {
        web_fetch_and_render(web_url_buf);
    } else {
        web_connect_and_fetch_saved_url();
    }
}

static void web_ta_event_cb(lv_event_t *e)
{
    if(e->code == LV_EVENT_FOCUSED) lv_keyboard_set_textarea(web_keyboard, (lv_obj_t *)e->target);
}

static void create12(lv_obj_t *parent)
{
    scr_back_btn_create(parent, "Web Browser", web_back_btn_event);
    web_url_ta = lv_textarea_create(parent);
    lv_obj_set_size(web_url_ta, lv_pct(76), 50);
    lv_obj_align(web_url_ta, LV_ALIGN_TOP_LEFT, 15, 80);
    lv_textarea_set_one_line(web_url_ta, true);
    lv_textarea_set_text(web_url_ta, web_url_buf);
    lv_obj_add_event_cb(web_url_ta, web_ta_event_cb, LV_EVENT_FOCUSED, NULL);
    epd_style_textarea(web_url_ta);

    lv_obj_t *go_btn = lv_btn_create(parent);
    lv_obj_set_size(go_btn, 90, 50);
    lv_obj_align_to(go_btn, web_url_ta, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
    lv_obj_add_event_cb(go_btn, web_go_btn_event, LV_EVENT_CLICKED, NULL);
    epd_style_button(go_btn);
    lv_obj_t *go_label = lv_label_create(go_btn);
    lv_label_set_text(go_label, "Go");
    epd_style_label(go_label);
    lv_obj_center(go_label);

    web_cont = lv_obj_create(parent);
    lv_obj_set_size(web_cont, lv_pct(96), lv_pct(45));
    lv_obj_align(web_cont, LV_ALIGN_TOP_MID, 0, 150);
    lv_obj_set_scroll_dir(web_cont, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(web_cont, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_pad_all(web_cont, 8, LV_PART_MAIN);
    epd_style_transparent(web_cont);
    epd_style_scrollbar(web_cont);

    web_span = lv_spangroup_create(web_cont);
    lv_obj_set_width(web_span, lv_pct(100));
    epd_style_plain(web_span);
    web_show_text("Type a URL with the on-screen keyboard and tap Go.");

    web_keyboard = lv_keyboard_create(parent);
    lv_obj_set_height(web_keyboard, lv_pct(32));
    lv_obj_align(web_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(web_keyboard, web_url_ta);
    keyboard_disable_long_press_repeat(web_keyboard);
    epd_style_keyboard(web_keyboard);
}

static void entry12(void)
{
    web_connect_and_fetch_saved_url();
}
static void exit12(void)
{
    if (web_wifi_connect_timer) {
        lv_timer_del(web_wifi_connect_timer);
        web_wifi_connect_timer = NULL;
    }
    web_pending_fetch_after_wifi = false;
}
static void destroy12(void) { }

static scr_lifecycle_t screen12 = {
    .create = create12,
    .entry = entry12,
    .exit = exit12,
    .destroy = destroy12,
};
#endif

//************************************[ screen 13 ]****************************************** maps
#if 1
static lv_obj_t *maps_status = NULL;
static lv_obj_t *maps_info = NULL;
static lv_obj_t *maps_canvas = NULL;
static lv_obj_t *maps_marker = NULL;
static lv_timer_t *maps_timer = NULL;
static uint32_t maps_wifi_start_ms = 0;
static bool maps_waiting_wifi = false;
static const uint32_t MAPS_WIFI_TIMEOUT_MS = 20000;
static lv_color_t *maps_canvas_buf = NULL;
static uint16_t *maps_png_line_buf = NULL;
static uint8_t *maps_png_raw = NULL;
static size_t maps_png_raw_size = 0;
static int maps_nonwhite_pixels = 0;
static int maps_render_zoom = 18;
static int maps_render_x = 0;
static int maps_render_y = 0;
static int maps_render_px = 0;
static int maps_render_py = 0;
static size_t maps_render_file_size = 0;
static bool maps_render_png_magic_ok = false;
static bool maps_render_from_cache = false;
static char maps_render_path[128] = {0};
static PNG maps_png_decoder;
enum MapsTileProvider {
    MAPS_PROVIDER_STADIA_STAMEN_TONER = 0,
    MAPS_PROVIDER_CARTO_LIGHT = 1,
    MAPS_PROVIDER_OSM_STANDARD = 2
};
static const int MAPS_TILE_SIZE = 256;
static const int MAPS_GRID_COLS = 2;
static const int MAPS_GRID_ROWS = 3;
static const int MAPS_CANVAS_W = MAPS_TILE_SIZE * MAPS_GRID_COLS;
static const int MAPS_CANVAS_H = MAPS_TILE_SIZE * MAPS_GRID_ROWS;
static int maps_decode_dest_x = 0;
static int maps_decode_dest_y = 0;
static int maps_bw_threshold = 180;
static MapsTileProvider maps_tile_provider = MAPS_PROVIDER_STADIA_STAMEN_TONER;
static String maps_stadia_api_key;

static const char *maps_provider_name(MapsTileProvider p)
{
    switch (p) {
        case MAPS_PROVIDER_STADIA_STAMEN_TONER: return "stamen_toner";
        case MAPS_PROVIDER_CARTO_LIGHT: return "carto_light";
        case MAPS_PROVIDER_OSM_STANDARD: return "osm";
        default: return "unknown";
    }
}

static bool maps_build_tile_url(char *url, size_t url_len, MapsTileProvider provider, int z, int x, int y)
{
    switch (provider) {
        case MAPS_PROVIDER_STADIA_STAMEN_TONER:
            if (maps_stadia_api_key.length() > 0) {
                lv_snprintf(url, url_len, "https://tiles.stadiamaps.com/tiles/stamen_toner/%d/%d/%d.png?api_key=%s", z, x, y, maps_stadia_api_key.c_str());
            } else {
                lv_snprintf(url, url_len, "https://tiles.stadiamaps.com/tiles/stamen_toner/%d/%d/%d.png", z, x, y);
            }
            return true;
        case MAPS_PROVIDER_CARTO_LIGHT:
            lv_snprintf(url, url_len, "https://a.basemaps.cartocdn.com/light_all/%d/%d/%d.png", z, x, y);
            return true;
        case MAPS_PROVIDER_OSM_STANDARD:
            lv_snprintf(url, url_len, "https://tile.openstreetmap.org/%d/%d/%d.png", z, x, y);
            return true;
        default:
            return false;
    }
}

static void maps_build_tile_url_redacted(char *url, size_t url_len, MapsTileProvider provider, int z, int x, int y)
{
    switch (provider) {
        case MAPS_PROVIDER_STADIA_STAMEN_TONER:
            lv_snprintf(url, url_len, "https://tiles.stadiamaps.com/tiles/stamen_toner/%d/%d/%d.png", z, x, y);
            break;
        case MAPS_PROVIDER_CARTO_LIGHT:
            lv_snprintf(url, url_len, "https://a.basemaps.cartocdn.com/light_all/%d/%d/%d.png", z, x, y);
            break;
        case MAPS_PROVIDER_OSM_STANDARD:
            lv_snprintf(url, url_len, "https://tile.openstreetmap.org/%d/%d/%d.png", z, x, y);
            break;
        default:
            lv_snprintf(url, url_len, "unknown");
            break;
    }
}

static void maps_load_stadia_key()
{
    maps_stadia_api_key = "";
    if (!peri_buf[E_PERI_SD_CARD]) return;
    if (!sd_guard_lock(1000)) return;
    File f = SD.open("/config/maps_stadia_key.txt", FILE_READ);
    if (f) {
        maps_stadia_api_key = f.readString();
        maps_stadia_api_key.trim();
        f.close();
    }
    sd_guard_unlock();
    if (maps_stadia_api_key.length() > 0) Serial.println("[MAP] Stadia API key loaded");
    else Serial.println("[MAP] No Stadia API key configured");
}

static bool maps_coord_valid(double lat, double lon)
{
    if (lat < -85.05112878 || lat > 85.05112878) return false;
    if (lon < -180.0 || lon > 180.0) return false;
    if (lat == 0.0 && lon == 0.0) return false;
    return true;
}

static void maps_set_status(const char *txt) { if (maps_status) lv_label_set_text(maps_status, txt); }

static void maps_tile_for(double lat,double lon,int z,int *tx,int *ty,int *px,int *py)
{
    double n=pow(2.0, z);
    double x=((lon+180.0)/360.0)*n;
    double r=lat*M_PI/180.0;
    double y=(1.0-log(tan(r)+1.0/cos(r))/M_PI)/2.0*n;
    int ix=(int)floor(x), iy=(int)floor(y);
    if(ix<0)ix=0; if(iy<0)iy=0; if(ix>= (int)n) ix=(int)n-1; if(iy>=(int)n) iy=(int)n-1;
    int ipx=(int)floor((x-ix)*MAPS_TILE_SIZE), ipy=(int)floor((y-iy)*MAPS_TILE_SIZE);
    if(ipx<0)ipx=0; if(ipx>=MAPS_TILE_SIZE)ipx=MAPS_TILE_SIZE-1; if(ipy<0)ipy=0; if(ipy>=MAPS_TILE_SIZE)ipy=MAPS_TILE_SIZE-1;
    *tx=ix;*ty=iy;*px=ipx;*py=ipy;
}

static bool maps_cache_dirs(const char *provider, int z,int x)
{
    if(!peri_buf[E_PERI_SD_CARD]) return false;
    if(!sd_guard_lock(2000)) return false;
    if(!SD.exists("/cache")) SD.mkdir("/cache");
    if(!SD.exists("/cache/maps")) SD.mkdir("/cache/maps");
    char p0[96]; lv_snprintf(p0,sizeof(p0),"/cache/maps/%s",provider); if(!SD.exists(p0)) SD.mkdir(p0);
    char p1[96]; lv_snprintf(p1,sizeof(p1),"/cache/maps/%s/%d",provider,z); if(!SD.exists(p1)) SD.mkdir(p1);
    char p2[128]; lv_snprintf(p2,sizeof(p2),"/cache/maps/%s/%d/%d",provider,z,x); if(!SD.exists(p2)) SD.mkdir(p2);
    sd_guard_unlock();
    return true;
}

static bool maps_download_tile(const char *path, MapsTileProvider provider, int z,int x,int y, int *http_code, String &url_redacted)
{
    char url[256];
    char redacted[256];
    *http_code = -1;
    if (!maps_build_tile_url(url, sizeof(url), provider, z, x, y)) return false;
    maps_build_tile_url_redacted(redacted, sizeof(redacted), provider, z, x, y);
    url_redacted = String(redacted);
    Serial.printf("[MAP] provider=%s z=%d x=%d y=%d\n", maps_provider_name(provider), z, x, y);
    Serial.printf("[MAP] HTTP GET %s\n", redacted);
    HTTPClient http;
    http.setUserAgent("T5S3-PaperPro-Maps/0.1 (contact: michael.rol.phone@gmail.com)");
    http.setTimeout(10000);
    if(!http.begin(url)) return false;
    int code=http.GET();
    *http_code = code;
    if(code!=200){ http.end(); Serial.printf("[MAP] provider=%s http=%d\n", maps_provider_name(provider), code); return false; }
    WiFiClient *stream=http.getStreamPtr();
    if (!sd_guard_lock(3000)) { http.end(); return false; }
    if (SD.exists(path)) SD.remove(path);
    File f=SD.open(path, FILE_WRITE);
    if(!f){ sd_guard_unlock(); http.end(); return false;}
    uint8_t buf[512]; int total=0;
    while(http.connected() && (http.getSize()>0 || stream->available())){
        int n=stream->readBytes(buf,sizeof(buf)); if(n<=0) break; f.write(buf,n); total+=n;
    }
    f.close(); sd_guard_unlock(); http.end();
    Serial.printf("[MAP] saved tile bytes=%d\n", total);
    return total>0;
}

static bool maps_check_png_file(const char *path, size_t *file_size, bool *magic_ok)
{
    *file_size = 0;
    *magic_ok = false;
    if (!sd_guard_lock(2000)) return false;
    if (!SD.exists(path)) { sd_guard_unlock(); return false; }
    File f = SD.open(path, FILE_READ);
    if (!f) { sd_guard_unlock(); return false; }
    *file_size = (size_t)f.size();
    uint8_t magic[8] = {0};
    size_t read_n = f.read(magic, sizeof(magic));
    f.close();
    sd_guard_unlock();
    const uint8_t expect[8] = {0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A};
    *magic_ok = (read_n == sizeof(magic) && memcmp(magic, expect, sizeof(expect)) == 0);
    Serial.printf("[MAP] cache_path=%s\n", path);
    Serial.printf("[MAP] cached_file_size=%u\n", (unsigned)*file_size);
    Serial.printf("[MAP] png_magic_ok=%d\n", *magic_ok ? 1 : 0);
    return true;
}

static bool maps_decode_png_to_canvas(const char *path, String &decode_result)
{
    if (!maps_canvas || !maps_canvas_buf || !maps_png_line_buf) { decode_result = "canvas_buffer_missing"; return false; }
    if (!sd_guard_lock(3000)) { decode_result = "sd_lock_failed"; return false; }
    File f = SD.open(path, FILE_READ);
    if (!f) { decode_result = "open_failed"; sd_guard_unlock(); return false; }
    size_t sz = (size_t)f.size();
    if (sz < 16 || sz > 1024 * 1024) { f.close(); sd_guard_unlock(); decode_result = "size_invalid"; return false; }
    if (maps_png_raw) { free(maps_png_raw); maps_png_raw = NULL; maps_png_raw_size = 0; }
    maps_png_raw = (uint8_t*)ps_malloc(sz);
    if (!maps_png_raw) { f.close(); sd_guard_unlock(); decode_result = "psram_alloc_failed"; return false; }
    size_t n = f.read(maps_png_raw, sz);
    f.close();
    sd_guard_unlock();
    if (n != sz) { decode_result = "read_failed"; return false; }
    maps_png_raw_size = sz;
    auto maps_png_draw_cb = [](PNGDRAW *pDraw) -> int {
        if (!pDraw || !maps_canvas_buf || !maps_png_line_buf) return 0;
        maps_png_decoder.getLineAsRGB565(pDraw, maps_png_line_buf, PNG_RGB565_LITTLE_ENDIAN, 0xffffffff);
        for (int x = 0; x < pDraw->iWidth && x < MAPS_TILE_SIZE; ++x) {
            uint16_t c = maps_png_line_buf[x];
            int r = ((c >> 11) & 0x1F) * 255 / 31;
            int g = ((c >> 5) & 0x3F) * 255 / 63;
            int b = (c & 0x1F) * 255 / 31;
            int gray = (299 * r + 587 * g + 114 * b) / 1000;
            bool black = gray < maps_bw_threshold;
            if (black) maps_nonwhite_pixels++;
            int dst_x = maps_decode_dest_x + x;
            int dst_y = maps_decode_dest_y + pDraw->y;
            if (dst_x >= 0 && dst_x < MAPS_CANVAS_W && dst_y >= 0 && dst_y < MAPS_CANVAS_H) {
                maps_canvas_buf[dst_y * MAPS_CANVAS_W + dst_x] = black ? lv_color_black() : lv_color_white();
            }
        }
        return 1;
    };
    int rc = maps_png_decoder.openRAM(maps_png_raw, (int)maps_png_raw_size, maps_png_draw_cb);
    if (rc != PNG_SUCCESS) { decode_result = "open_png_failed"; return false; }
    if (maps_png_decoder.getWidth() != MAPS_TILE_SIZE || maps_png_decoder.getHeight() != MAPS_TILE_SIZE) { maps_png_decoder.close(); decode_result = "tile_not_256"; return false; }
    maps_nonwhite_pixels = 0;
    rc = maps_png_decoder.decode(NULL, 0);
    maps_png_decoder.close();
    if (rc != PNG_SUCCESS) { decode_result = "decode_failed"; return false; }
    decode_result = "ok";
    Serial.printf("[MAP] nonwhite_pixels=%d\n", maps_nonwhite_pixels);
    return true;
}

static void maps_write_debug(const String &dbg_text)
{
    File dbg = SD.open("/cache/maps/latest_debug.txt", FILE_WRITE);
    if (dbg) {
        dbg.print(dbg_text);
        dbg.close();
    }
}

static bool maps_try_render(double lat,double lon)
{
    const int z = 18;
    int base_x, base_y, px, py;
    maps_tile_for(lat, lon, z, &base_x, &base_y, &px, &py);
    int left_x = (px < MAPS_TILE_SIZE / 2) ? base_x - 1 : base_x;
    int top_y = base_y - 1;
    int n = 1 << z;
    bool has_sd = peri_buf[E_PERI_SD_CARD];
    bool any_decoded = false;
    bool any_failed = false;
    maps_set_status("Loading map...");
    lv_canvas_fill_bg(maps_canvas, lv_color_white(), LV_OPA_COVER);
    String dbg;
    dbg.reserve(2048);
    dbg += "lat=" + String(lat, 6) + "\nlon=" + String(lon, 6) + "\n";
    dbg += "zoom=" + String(z) + "\n";
    dbg += "base_tile_x=" + String(base_x) + "\nbase_tile_y=" + String(base_y) + "\n";
    dbg += "base_pixel_x=" + String(px) + "\nbase_pixel_y=" + String(py) + "\n";
    dbg += "left_x=" + String(left_x) + "\ntop_y=" + String(top_y) + "\n";
    for (int row = 0; row < MAPS_GRID_ROWS; ++row) {
        for (int col = 0; col < MAPS_GRID_COLS; ++col) {
            int tile_x_raw = left_x + col;
            int tile_y = top_y + row;
            int tile_x = (tile_x_raw % n + n) % n;
            maps_decode_dest_x = col * MAPS_TILE_SIZE;
            maps_decode_dest_y = row * MAPS_TILE_SIZE;
            if (tile_y < 0 || tile_y >= n) {
                Serial.printf("[MAP] grid tile row=%d col=%d z=%d x=%d y=%d source=clamped decode=skip\n", row, col, z, tile_x, tile_y);
                dbg += "tile[" + String(row) + "," + String(col) + "] path=(clamped y) source=none decode=skip\n";
                continue;
            }
            const MapsTileProvider providers[] = {MAPS_PROVIDER_STADIA_STAMEN_TONER, MAPS_PROVIDER_CARTO_LIGHT, MAPS_PROVIDER_OSM_STANDARD};
            bool decoded = false;
            bool tile_attempted = false;
            for (size_t pi = 0; pi < ARRAY_LEN(providers); ++pi) {
                MapsTileProvider provider = providers[pi];
                const char *provider_name = maps_provider_name(provider);
                char tile[160];
                lv_snprintf(tile, sizeof(tile), "/cache/maps/%s/%d/%d/%d.png", provider_name, z, tile_x, tile_y);
                bool cache_hit = has_sd && SD.exists(tile);
                const char *source = cache_hit ? "cache" : "download";
                int http_code = -1;
                String url_redacted = "";
                String decode_result = "not_attempted";
                String fallback_to = "";
                tile_attempted = true;
                if (!cache_hit) {
                    if (WiFi.status() != WL_CONNECTED) {
                        wifi_load_saved_settings();
                        if (wifi_sta_ssid.length() == 0) { maps_set_status("Map unavailable"); return false; }
                        if (!maps_waiting_wifi) { maps_wifi_start_ms = millis(); maps_waiting_wifi = true; maps_set_status("Connecting WiFi..."); wifi_connect_saved_sta("maps tile download"); }
                        return false;
                    }
                    if (!has_sd) { maps_set_status("SD unavailable"); return false; }
                    maps_cache_dirs(provider_name, z, tile_x);
                    bool dl_ok = maps_download_tile(tile, provider, z, tile_x, tile_y, &http_code, url_redacted);
                    if (!dl_ok) {
                        any_failed = true;
                        if (pi + 1 < ARRAY_LEN(providers) && (http_code == 401 || http_code == 403 || http_code == 404 || http_code < 0)) {
                            fallback_to = maps_provider_name(providers[pi + 1]);
                            Serial.printf("[MAP] provider=%s http=%d; trying fallback\n", provider_name, http_code);
                        }
                        dbg += "tile[" + String(row) + "," + String(col) + "] provider=" + String(provider_name) + " url=" + url_redacted + " http=" + String(http_code) + " fallback=" + fallback_to + " cache_path=" + String(tile) + " decode=download_failed\n";
                        continue;
                    }
                } else {
                    maps_build_tile_url_redacted((char*)md_text_buf, sizeof(md_text_buf), provider, z, tile_x, tile_y);
                    url_redacted = String(md_text_buf);
                }
                size_t file_size = 0;
                bool png_magic_ok = false;
                if (!maps_check_png_file(tile, &file_size, &png_magic_ok) || file_size <= 100 || !png_magic_ok) {
                    any_failed = true;
                    dbg += "tile[" + String(row) + "," + String(col) + "] provider=" + String(provider_name) + " url=" + url_redacted + " http=" + String(http_code) + " fallback=" + fallback_to + " cache_path=" + String(tile) + " decode=invalid_png\n";
                    continue;
                }
                decoded = maps_decode_png_to_canvas(tile, decode_result);
                Serial.printf("[MAP] grid tile row=%d col=%d z=%d x=%d y=%d source=%s provider=%s decode=%s\n", row, col, z, tile_x, tile_y, source, provider_name, decode_result.c_str());
                dbg += "tile[" + String(row) + "," + String(col) + "] provider=" + String(provider_name) + " url=" + url_redacted + " http=" + String(http_code) + " fallback=" + fallback_to + " cache_path=" + String(tile) + " decode=" + decode_result + "\n";
                if (decoded) {
                    maps_tile_provider = provider;
                    break;
                }
                any_failed = true;
            }
            if (decoded) {
                any_decoded = true;
            } else {
                any_failed = true;
                if (tile_attempted) {
                    Serial.printf("[MAP] grid tile row=%d col=%d z=%d x=%d y=%d decode=all_providers_failed\n", row, col, z, tile_x, tile_y);
                }
            }
        }
    }
    int marker_canvas_x = (base_x - left_x) * MAPS_TILE_SIZE + px;
    int marker_canvas_y = (base_y - top_y) * MAPS_TILE_SIZE + py;
    int sx = lv_obj_get_x(maps_canvas) + marker_canvas_x;
    int sy = lv_obj_get_y(maps_canvas) + marker_canvas_y;
    lv_obj_set_pos(maps_marker, sx - 16, sy - 16);
    lv_obj_move_foreground(maps_marker);
    lv_obj_invalidate(maps_canvas);
    dbg += "marker_canvas_x=" + String(marker_canvas_x) + "\nmarker_canvas_y=" + String(marker_canvas_y) + "\n";
    maps_write_debug(dbg);
    if (!any_decoded) {
        maps_set_status(has_sd ? "Map unavailable" : "SD unavailable");
        return false;
    }
    if (any_failed) maps_set_status("Tile decode failed");
    else { lv_label_set_text(maps_status, ""); lv_obj_add_flag(maps_status, LV_OBJ_FLAG_HIDDEN); }
    return true;
}

static void maps_timer_cb(lv_timer_t *t)
{
    (void)t;
    if(maps_waiting_wifi && WiFi.status()!=WL_CONNECTED){
        uint32_t e=millis()-maps_wifi_start_ms;
        maps_set_status("Connecting WiFi...");
        if(e>MAPS_WIFI_TIMEOUT_MS){ maps_waiting_wifi=false; maps_set_status("WiFi timeout"); }
        return;
    }
    maps_waiting_wifi=false;
    double lat=0,lon=0; ui_gps_get_coord(&lat,&lon);
    Serial.printf("[MAP] gps lat=%.6f lon=%.6f\n",lat,lon);
    if(!maps_coord_valid(lat,lon)){ maps_set_status("Waiting for GPS fix..."); return; }
    if(maps_try_render(lat,lon)){ lv_timer_del(maps_timer); maps_timer=NULL; return; }
}
static void maps_back(lv_event_t *e){ if(e->code==LV_EVENT_CLICKED) scr_mgr_pop(false);}
static void create13(lv_obj_t *p){
    scr_back_btn_create(p, "Maps", maps_back);
    maps_status=lv_label_create(p); lv_obj_align(maps_status, LV_ALIGN_TOP_LEFT, 20, 80); lv_obj_set_width(maps_status, lv_pct(95));
    maps_canvas=lv_canvas_create(p);
    lv_obj_set_size(maps_canvas, MAPS_CANVAS_W, MAPS_CANVAS_H);
    lv_obj_set_style_bg_color(maps_canvas, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(maps_canvas, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(maps_canvas, 0, 0);
    lv_obj_set_style_border_color(maps_canvas, lv_color_black(), 0);
    lv_obj_align(maps_canvas, LV_ALIGN_TOP_MID, 0, 90);
    if (!maps_canvas_buf) maps_canvas_buf = (lv_color_t *)ps_malloc(MAPS_CANVAS_W * MAPS_CANVAS_H * sizeof(lv_color_t));
    if (!maps_png_line_buf) maps_png_line_buf = (uint16_t *)ps_malloc(MAPS_TILE_SIZE * sizeof(uint16_t));
    if (!maps_canvas_buf || !maps_png_line_buf) {
        if (!maps_canvas_buf) Serial.println("[MAP] maps_canvas_buf allocation failed");
        if (!maps_png_line_buf) Serial.println("[MAP] maps_png_line_buf allocation failed");
        maps_set_status("Map buffer allocation failed");
    }
    if (maps_canvas_buf) {
        lv_canvas_set_buffer(maps_canvas, maps_canvas_buf, MAPS_CANVAS_W, MAPS_CANVAS_H, LV_IMG_CF_TRUE_COLOR);
        lv_canvas_fill_bg(maps_canvas, lv_color_white(), LV_OPA_COVER);
    }
    maps_marker=lv_obj_create(p); lv_obj_set_size(maps_marker, 32, 32); lv_obj_set_style_radius(maps_marker, LV_RADIUS_CIRCLE, 0); lv_obj_set_style_bg_opa(maps_marker, LV_OPA_TRANSP, 0); lv_obj_set_style_border_width(maps_marker, 3, 0);
    maps_info=lv_label_create(p); lv_obj_add_flag(maps_info, LV_OBJ_FLAG_HIDDEN);
}
static void entry13(void){ Serial.println("[MAP] entry"); maps_load_stadia_key(); ui_gps_task_resume(); maps_waiting_wifi=false; maps_set_status("Waiting for GPS fix..."); if(maps_timer) lv_timer_del(maps_timer); maps_timer=lv_timer_create(maps_timer_cb, 2500, NULL); lv_timer_ready(maps_timer);}
static void exit13(void){ if(maps_timer){ lv_timer_del(maps_timer); maps_timer=NULL; } maps_waiting_wifi=false; }
static void destroy13(void){ if(maps_canvas_buf){ free(maps_canvas_buf); maps_canvas_buf=NULL; } if(maps_png_line_buf){ free(maps_png_line_buf); maps_png_line_buf=NULL; } if(maps_png_raw){ free(maps_png_raw); maps_png_raw=NULL; maps_png_raw_size=0; } }
static scr_lifecycle_t screen13 = {.create=create13,.entry=entry13,.exit=exit13,.destroy=destroy13};
#endif

//************************************[ screen 14 ]****************************************** image preview
#if 1
static lv_obj_t *image_preview_canvas = NULL;
static lv_obj_t *image_preview_status = NULL;
static lv_color_t *image_preview_buf = NULL;
static uint8_t *image_preview_png_raw = NULL;
static size_t image_preview_png_raw_size = 0;
static uint16_t *image_preview_line_buf = NULL;
static PNG image_preview_decoder;
static float image_preview_scale = 1.0f;
static int image_preview_offset_x = 0;
static int image_preview_offset_y = 0;
static int image_preview_scaled_w = 0;
static int image_preview_scaled_h = 0;
static const int IMAGE_PREVIEW_TOP_MARGIN = 70;

static bool png_read_file_to_psram(const char *path, uint8_t **out_data, size_t *out_size, String &err)
{
    if (!out_data || !out_size) { err = "Image memory error"; return false; }
    *out_data = NULL;
    *out_size = 0;
    if (!peri_buf[E_PERI_SD_CARD]) { err = "SD unavailable"; return false; }
    if (!sd_guard_lock(3000)) { err = "SD unavailable"; return false; }
    File f = SD.open(path, FILE_READ);
    if (!f || f.isDirectory()) { sd_guard_unlock(); err = "PNG open failed"; return false; }
    size_t sz = (size_t)f.size();
    if (sz < 16) { f.close(); sd_guard_unlock(); err = "PNG open failed"; return false; }
    uint8_t *raw = (uint8_t *)ps_malloc(sz);
    if (!raw) { f.close(); sd_guard_unlock(); err = "Image memory error"; return false; }
    size_t n = f.read(raw, sz);
    f.close();
    sd_guard_unlock();
    if (n != sz) { free(raw); err = "PNG open failed"; return false; }
    *out_data = raw;
    *out_size = sz;
    Serial.printf("[IMAGE] open path=%s\n", path);
    Serial.printf("[IMAGE] png size=%u\n", (unsigned)sz);
    return true;
}

static bool png_decode_scaled_to_canvas(const char *path, lv_color_t *canvas_buf, int canvas_w, int canvas_h, int *out_img_w, int *out_img_h, String &err)
{
    if (!canvas_buf || canvas_w <= 0 || canvas_h <= 0) { err = "Image memory error"; return false; }
    if (image_preview_png_raw) { free(image_preview_png_raw); image_preview_png_raw = NULL; image_preview_png_raw_size = 0; }
    if (!png_read_file_to_psram(path, &image_preview_png_raw, &image_preview_png_raw_size, err)) return false;
    if (!image_preview_line_buf) image_preview_line_buf = (uint16_t *)ps_malloc(4096 * sizeof(uint16_t));
    if (!image_preview_line_buf) { err = "Image memory error"; return false; }

    auto open_cb = [](PNGDRAW *pDraw) -> int { (void)pDraw; return 1; };
    int rc = image_preview_decoder.openRAM(image_preview_png_raw, (int)image_preview_png_raw_size, open_cb);
    if (rc != PNG_SUCCESS) { err = "PNG open failed"; return false; }

    int src_w = image_preview_decoder.getWidth();
    int src_h = image_preview_decoder.getHeight();
    if (src_w <= 0 || src_h <= 0) { image_preview_decoder.close(); err = "PNG decode failed"; return false; }
    if (src_w > 4096 || src_h > 4096) { image_preview_decoder.close(); err = "Image too large"; return false; }
    if (src_w > 4096) { image_preview_decoder.close(); err = "Image too large"; return false; }
    Serial.printf("[IMAGE] source w/h=%d/%d\n", src_w, src_h);

    float scale = (float)canvas_w / (float)src_w;
    int scaled_w = canvas_w;
    int scaled_h = (int)((float)src_h * scale);
    if (scaled_h > canvas_h) {
        scale = (float)canvas_h / (float)src_h;
        scaled_h = canvas_h;
        scaled_w = (int)((float)src_w * scale);
    }
    if (scaled_w < 1) scaled_w = 1;
    if (scaled_h < 1) scaled_h = 1;
    image_preview_scale = scale;
    image_preview_scaled_w = scaled_w;
    image_preview_scaled_h = scaled_h;
    image_preview_offset_x = (canvas_w - scaled_w) / 2;
    image_preview_offset_y = (canvas_h - scaled_h) / 2;
    Serial.printf("[IMAGE] scaled w/h=%d/%d\n", scaled_w, scaled_h);

    auto draw_cb = [](PNGDRAW *pDraw) -> int {
        if (!pDraw || !image_preview_buf || !image_preview_line_buf) return 0;
        image_preview_decoder.getLineAsRGB565(pDraw, image_preview_line_buf, PNG_RGB565_LITTLE_ENDIAN, 0xffffffff);
        int src_y = pDraw->y;
        int dst_y0 = image_preview_offset_y + (int)floorf(src_y * image_preview_scale);
        int dst_y1 = image_preview_offset_y + (int)floorf((src_y + 1) * image_preview_scale);
        if (dst_y1 <= dst_y0) dst_y1 = dst_y0 + 1;
        for (int src_x = 0; src_x < pDraw->iWidth; ++src_x) {
            uint16_t c = image_preview_line_buf[src_x];
            int r = ((c >> 11) & 0x1F) * 255 / 31;
            int g = ((c >> 5) & 0x3F) * 255 / 63;
            int b = (c & 0x1F) * 255 / 31;
            int gray = (299 * r + 587 * g + 114 * b) / 1000;
            lv_color_t pix = (gray < 128) ? lv_color_black() : lv_color_white();
            int dst_x0 = image_preview_offset_x + (int)floorf(src_x * image_preview_scale);
            int dst_x1 = image_preview_offset_x + (int)floorf((src_x + 1) * image_preview_scale);
            if (dst_x1 <= dst_x0) dst_x1 = dst_x0 + 1;
            for (int yy = dst_y0; yy < dst_y1; ++yy) {
                if (yy < 0 || yy >= (LV_VER_RES - IMAGE_PREVIEW_TOP_MARGIN)) continue;
                for (int xx = dst_x0; xx < dst_x1; ++xx) {
                    if (xx < 0 || xx >= LV_HOR_RES) continue;
                    image_preview_buf[yy * LV_HOR_RES + xx] = pix;
                }
            }
        }
        return 1;
    };

    rc = image_preview_decoder.openRAM(image_preview_png_raw, (int)image_preview_png_raw_size, draw_cb);
    if (rc != PNG_SUCCESS) { err = "PNG open failed"; return false; }
    rc = image_preview_decoder.decode(NULL, 0);
    image_preview_decoder.close();
    if (rc != PNG_SUCCESS) { err = "PNG decode failed"; return false; }
    if (out_img_w) *out_img_w = scaled_w;
    if (out_img_h) *out_img_h = scaled_h;
    Serial.printf("[IMAGE] decode result=ok\n");
    return true;
}

static void screen14_back(lv_event_t *e){ if(e->code == LV_EVENT_CLICKED) scr_mgr_pop(false); }
static void create14(lv_obj_t *parent)
{
    scr_back_btn_create(parent, "Image", screen14_back);
    image_preview_status = lv_label_create(parent);
    lv_obj_align(image_preview_status, LV_ALIGN_TOP_LEFT, 20, 72);
    lv_label_set_text(image_preview_status, "");
    image_preview_canvas = lv_canvas_create(parent);
    lv_obj_set_size(image_preview_canvas, LV_HOR_RES, LV_VER_RES - IMAGE_PREVIEW_TOP_MARGIN);
    lv_obj_align(image_preview_canvas, LV_ALIGN_BOTTOM_MID, 0, 0);
    if (!image_preview_buf) image_preview_buf = (lv_color_t *)ps_malloc(LV_HOR_RES * (LV_VER_RES - IMAGE_PREVIEW_TOP_MARGIN) * sizeof(lv_color_t));
    if (image_preview_buf) {
        lv_canvas_set_buffer(image_preview_canvas, image_preview_buf, LV_HOR_RES, LV_VER_RES - IMAGE_PREVIEW_TOP_MARGIN, LV_IMG_CF_TRUE_COLOR);
        lv_canvas_fill_bg(image_preview_canvas, lv_color_white(), LV_OPA_COVER);
    }
}
static void entry14(void)
{
    if (!image_preview_canvas || !image_preview_status || !image_preview_buf) return;
    lv_canvas_fill_bg(image_preview_canvas, lv_color_white(), LV_OPA_COVER);
    if (image_open_path[0] == '\0') { lv_label_set_text(image_preview_status, "No image selected"); return; }
    lv_label_set_text(image_preview_status, "Loading image...");
    String err = "";
    int out_w = 0, out_h = 0;
    if (png_decode_scaled_to_canvas(image_open_path, image_preview_buf, LV_HOR_RES, LV_VER_RES - IMAGE_PREVIEW_TOP_MARGIN, &out_w, &out_h, err)) {
        lv_label_set_text(image_preview_status, "");
    } else {
        lv_label_set_text(image_preview_status, err.c_str());
        Serial.printf("[IMAGE] decode result=%s\n", err.c_str());
    }
    lv_obj_invalidate(image_preview_canvas);
}
static void exit14(void) {}
static void destroy14(void)
{
    if (image_preview_buf) { free(image_preview_buf); image_preview_buf = NULL; }
    if (image_preview_line_buf) { free(image_preview_line_buf); image_preview_line_buf = NULL; }
    if (image_preview_png_raw) { free(image_preview_png_raw); image_preview_png_raw = NULL; image_preview_png_raw_size = 0; }
}
static scr_lifecycle_t screen14 = {.create=create14,.entry=entry14,.exit=exit14,.destroy=destroy14};
#endif

//************************************[ screen 9 ]****************************************** shutdown
#if 1
static void scr8_btn_event_cb(lv_event_t * e)
{
    if(e->code == LV_EVENT_CLICKED){
        // ui_full_refresh();
        scr_mgr_pop(false);
    }
}

static void scr8_shutdown_timer_event(lv_timer_t *t)
{
    lv_timer_del(t);
    ui_epd_clean();
    ui_shutdown();
}

static void create8(lv_obj_t *parent)
{
    if(battery_25896_is_vbus_in()) 
    {
        lv_obj_t * label = lv_label_create(parent);
        lv_obj_set_width(label, lv_pct(98));
        lv_obj_set_style_text_font(label, &Font_Mono_Bold_25, LV_PART_MAIN);
        lv_obj_set_style_text_color(label, lv_color_hex(EPD_COLOR_TEXT), LV_PART_MAIN);
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
        lv_label_set_text(label, "The shutdown function can only be used when the "
                            "battery is connected alone, and cannot be shut down when connected to USB.");
        lv_obj_center(label);

        // back 
        scr_back_btn_create(parent, "Shoutdown", scr8_btn_event_cb);
    } 
    else 
    {
        ui_shutdown_vcom(5000);

        lv_obj_t * img = lv_img_create(parent);
        lv_img_set_src(img, &img_start);
        lv_obj_center(img);

        lv_timer_create(scr8_shutdown_timer_event, 2000, (void *)parent);
    }
}

static void entry8(void) {
    
}
static void exit8(void) {
}
static void destroy8(void) { 

}

static scr_lifecycle_t screen8 = {
    .create = create8,
    .entry = entry8,
    .exit  = exit8,
    .destroy = destroy8,
};
#endif
//************************************[ screen 10 ]****************************************** sleep
#if 1
static void scr9_btn_event_cb(lv_event_t * e)
{
    if(e->code == LV_EVENT_CLICKED){
        // ui_full_refresh();
        scr_mgr_pop(false);
    }
}

static void scr9_shutdown_timer_event(lv_timer_t *t)
{
    lv_timer_del(t);
    ui_sleep();
}

static void create9(lv_obj_t *parent)
{
    scr_back_btn_create(parent, "Sleep", scr9_btn_event_cb);

    lv_timer_create(scr9_shutdown_timer_event, 3000, NULL);
}

static void entry9(void) {
    
}
static void exit9(void) {
}
static void destroy9(void) { 

}

static scr_lifecycle_t screen9 = {
    .create = create9,
    .entry = entry9,
    .exit  = exit9,
    .destroy = destroy9,
};
#endif
//************************************[ UI ENTRY ]******************************************
static lv_obj_t *menu_keypad;
static lv_timer_t *menu_timer = NULL;

void menu_taskbar_update_timer_cb(lv_timer_t *t)
{
    // update taskbar buf
    static int sec = 0;
    sec++;

    uint8_t h = 0, m = 0, s = 0;
    bool charge = 0;
    bool finish = 0;
    bool wifi = 0;
    int percent = 0;

    
    if(sec % 10 == 0)
    {
        ui_clock_get_time(&h, &m, &s);
        finish = ui_battery_27220_get_charge_finish();
        percent = ui_battery_27220_get_percent();

        if(taskbar_statue[TASKBAR_ID_TIME_MINUTE] != m)
        {
            char time_buf[16] = {0};
            format_time_12h(h, m, time_buf, sizeof(time_buf), NULL);
            lv_label_set_text_fmt(menu_taskbar_time, "%s", time_buf);
            taskbar_statue[TASKBAR_ID_TIME_HOUR] = h;
            taskbar_statue[TASKBAR_ID_TIME_MINUTE] = m;
        }

        if(taskbar_statue[TASKBAR_ID_CHARGE_FINISH] != finish) 
        {
            if(finish){
                lv_label_set_text_fmt(menu_taskbar_charge, "%s", LV_SYMBOL_OK);
            } else {
                lv_label_set_text_fmt(menu_taskbar_charge, "%s", LV_SYMBOL_CHARGE);
            }
            taskbar_statue[TASKBAR_ID_CHARGE_FINISH] = finish;
        }

        if(taskbar_statue[TASKBAR_ID_BATTERY_PERCENT] != percent) 
        {
            lv_label_set_text_fmt(menu_taskbar_battery_percent, "%d", percent);
            lv_label_set_text_fmt(menu_taskbar_battery, "%s", ui_battert_27220_get_percent_level());
            taskbar_statue[TASKBAR_ID_BATTERY_PERCENT] = percent;
        }
    }

    charge = ui_battery_27220_get_input();
    if(taskbar_statue[TASKBAR_ID_CHARGE] != charge) 
    {
        if(charge) {
            lv_obj_clear_flag(menu_taskbar_charge, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(menu_taskbar_charge, LV_OBJ_FLAG_HIDDEN);
        }
        taskbar_statue[TASKBAR_ID_CHARGE] = charge;
    }

    wifi = ui_wifi_get_status();
    if(taskbar_statue[TASKBAR_ID_WIFI] != wifi)
    {
        if(wifi) {
            lv_obj_clear_flag(menu_taskbar_wifi, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(menu_taskbar_wifi, LV_OBJ_FLAG_HIDDEN);
        }
        taskbar_statue[TASKBAR_ID_WIFI] = wifi;
    }
}

void ui_entry(void)
{
    lv_disp_t *disp = lv_disp_get_default();
    disp->theme = lv_theme_mono_init(disp, false, LV_FONT_DEFAULT);

    taskbar_update_timer = lv_timer_create(menu_taskbar_update_timer_cb, 1000, NULL);
    lv_timer_pause(taskbar_update_timer);

    scr_mgr_init();
    scr_mgr_set_bg_color(EPD_COLOR_BG);
    scr_mgr_register(SCREEN0_ID,   &screen0);   // menu
    scr_mgr_register(SCREEN1_ID,   &screen1);   // clock
    scr_mgr_register(SCREEN2_ID,   &screen2);   // lora
    scr_mgr_register(SCREEN2_1_ID, &screen2_1); //  - Auto Send
    scr_mgr_register(SCREEN2_2_ID, &screen2_2); //  - Manual Send
    scr_mgr_register(SCREEN2_3_ID, &screen2_3); //  - Lora Setting
    scr_mgr_register(SCREEN3_ID,   &screen3);   // sd card
    scr_mgr_register(SCREEN4_ID,   &screen4);   // setting
    scr_mgr_register(SCREEN4_1_ID, &screen4_1); //  - About System
    scr_mgr_register(SCREEN4_2_ID, &screen4_2); //  - Set EPD Vcom
    scr_mgr_register(SCREEN4_3_ID, &screen4_3); //  - Set Date & Time
    scr_mgr_register(SCREEN5_ID,   &screen5);   // test
    scr_mgr_register(SCREEN6_ID,   &screen6);   // wifi
    scr_mgr_register(SCREEN7_ID,   &screen7);   // battery
    scr_mgr_register(SCREEN8_ID,   &screen8);   // shutdown
    scr_mgr_register(SCREEN9_ID,   &screen9);   // sleep
    scr_mgr_register(SCREEN10_ID,  &screen10);  // gps
    scr_mgr_register(SCREEN11_ID,  &screen11);  // markdown
    scr_mgr_register(SCREEN12_ID,  &screen12);  // web browser
    scr_mgr_register(SCREEN13_ID,  &screen13);  // maps
    scr_mgr_register(SCREEN14_ID,  &screen14);  // image preview

    scr_mgr_switch(SCREEN0_ID, false); // set root screen
    disp_request_boot_replace();
    scr_mgr_set_anim(LV_SCR_LOAD_ANIM_NONE, LV_SCR_LOAD_ANIM_NONE, LV_SCR_LOAD_ANIM_NONE);
}
