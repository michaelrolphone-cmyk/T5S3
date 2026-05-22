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

// Touch
TouchDrvGT911 touch;

// RTC
SensorPCF8563 rtc;

// LVGL
#define DISP_BUF_SIZE (epd_rotated_display_width() * epd_rotated_display_height())
#define EPD_IMAGE_BUF_SIZE (((epd_rotated_display_width() + 1) / 2) * epd_rotated_display_height())
uint8_t *decodebuffer = NULL;
uint8_t *displaybuffer = NULL;
volatile bool disp_flush_enabled = true;
volatile bool indev_touch_enabled = true;
static volatile bool touch_ignore_until_release = false;
static volatile bool home_button_pending = false;
static volatile uint32_t touch_ignore_start_ms = 0;
bool disp_refr_is_busy = false;
static volatile bool disp_flush_pending = false;
static volatile bool framebuffer_dirty = false;
static TaskHandle_t disp_flush_handle = NULL;
static SemaphoreHandle_t framebuffer_mutex = NULL;

/*********************************************************************************
 *                                   TASK
 * *******************************************************************************/
void btn_task(void *param)
{
    bool boot_btn_pressed = false;
    // Seed with the current level to avoid a false release edge right after boot.
    bool ioext_btn_pressed = button_read();

    while(1)
    {
        if (digitalRead(BOARD_BOOT_BTN) == LOW)
        {
            if (!boot_btn_pressed) {
                boot_btn_pressed = true;
                // Use the same button GPIO as deep-sleep wake source
                scr_mgr_switch(0, false);
                ui_sleep();
            }
        }
        else {
            boot_btn_pressed = false;
        }

        // Read the IO expander key level every cycle and toggle on release edge.
        // Relying solely on INT can miss transitions once the line returns high.
        if(button_read()) {
            ioext_btn_pressed = true;
        }
        else {
            if(ioext_btn_pressed) {
                int bl = 0;
                ui_setting_get_backlight(&bl);
                ui_setting_set_backlight(bl == 0 ? 1 : 0);
            }
            ioext_btn_pressed = false;
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
    checkError(epd_hl_update_screen(&hl, MODE_GL16, epd_ambient_temperature()));
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
    checkError(epd_hl_update_screen(&hl, MODE_GC16, epd_ambient_temperature()));
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
    checkError(epd_hl_update_screen(&hl, MODE_DU, epd_ambient_temperature()));
    epd_poweroff();

    epd_draw_rotated_image(rener_area, decodebuffer, epd_hl_get_framebuffer(&hl));
    epd_poweron();
    checkError(epd_hl_update_screen(&hl, MODE_GL16, epd_ambient_temperature()));
    epd_poweroff();
}

/*********************************************************************************
 *                            STATIC  FUNCTION
 * *******************************************************************************/
static void disp_flush_task(void *param)
{
    (void)param;
    while (1) {
        if (disp_flush_pending) {
            disp_flush_pending = false;
            framebuffer_dirty = false;

            EpdRect full_area = {
                .x = 0,
                .y = 0,
                .width = epd_rotated_display_width(),
                .height = epd_rotated_display_height(),
            };

            if (framebuffer_mutex && decodebuffer && displaybuffer) {
                if (xSemaphoreTake(framebuffer_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                    memcpy(displaybuffer, decodebuffer, EPD_IMAGE_BUF_SIZE);
                    xSemaphoreGive(framebuffer_mutex);
                } else {
                    vTaskDelay(pdMS_TO_TICKS(1));
                    continue;
                }
            }

            if(ui_refresh_get_mode() == UI_REFRESH_MODE_FAST)
            {
                epd_draw_rotated_image(full_area, displaybuffer, epd_hl_get_framebuffer(&hl));
                epd_poweron();
                checkError(epd_hl_update_screen(&hl, MODE_GC16, epd_ambient_temperature()));
                epd_poweroff();
            }
            else if(ui_refresh_get_mode() == UI_REFRESH_MODE_NORMAL)
            {
                epd_draw_rotated_image(full_area, displaybuffer, epd_hl_get_framebuffer(&hl));
                epd_poweron();
                checkError(epd_hl_update_screen(&hl, MODE_GC16, epd_ambient_temperature()));
                epd_poweroff();
            }
            else if(ui_refresh_get_mode() == UI_REFRESH_MODE_NEAT)
            {
                disp_full_refresh();
                epd_draw_rotated_image(full_area, displaybuffer, epd_hl_get_framebuffer(&hl));
                epd_poweron();
                checkError(epd_hl_update_screen(&hl, MODE_GC16, epd_ambient_temperature()));
                epd_poweroff();
            }

            if (framebuffer_dirty) {
                disp_flush_pending = true;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

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

#if 0   // Mirror screen or not
        for(int i = 0; i < h ; i++) {
            for(int j = 0; j < w / 2; j++) {
                lv_color_t t = *(color_p + (i * w) + j);
                *(color_p + (i * w) + j) = *(color_p + (i * w) + (w - j - 1));
                *(color_p + (i * w) + (w - j - 1)) = t;
            }
        }
#endif
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
        framebuffer_dirty = true;
        // printf("[disp_flush] x1:%d, y1:%d, w:%d, h:%d\n", area->x1, area->y1, w, h);
    }
    if(ui_refresh_get_mode() == UI_REFRESH_MODE_FAST) 
    {
        disp_flush_pending = true;
    } 
    else if(ui_refresh_get_mode() == UI_REFRESH_MODE_NORMAL)
    {
        disp_flush_pending = true;
    } 
    else if(ui_refresh_get_mode() == UI_REFRESH_MODE_NEAT)
    {
        disp_flush_pending = true;
    }
    /* Inform the graphics library that you are ready with the flushing */
    lv_disp_flush_ready(disp);
}

static void my_input_read(lv_indev_drv_t * drv, lv_indev_data_t*data)
{
    static int16_t x=0, y=0;
    bool pressed = indev_touch_enabled && touch.isPressed();

    (void)drv;
    if(touch_ignore_until_release) {
        if(!pressed || (millis() - touch_ignore_start_ms > 1500)) {
            touch_ignore_until_release = false;
        }
        data->state = LV_INDEV_STATE_RELEASED;
    }
    else if(pressed) {
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
    // Start LVGL backing buffers from a clean logical screen to avoid
    // carrying previous boot/status text into subsequent partial updates.
    epd_hl_set_all_white(&hl);
    const uint8_t *epd_framebuffer = (const uint8_t *)epd_hl_get_framebuffer(&hl);
    if (decodebuffer && displaybuffer && epd_framebuffer) {
        memcpy(decodebuffer, epd_framebuffer, EPD_IMAGE_BUF_SIZE);
        memcpy(displaybuffer, epd_framebuffer, EPD_IMAGE_BUF_SIZE);
    }
    framebuffer_mutex = xSemaphoreCreateMutex();
    lv_disp_draw_buf_init(&draw_buf, lv_disp_buf_1, lv_disp_buf_2, DISP_BUF_SIZE);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = epd_rotated_display_width();
    disp_drv.ver_res = epd_rotated_display_height();
    disp_drv.flush_cb = disp_flush;
    // disp_drv.render_start_cb = dips_render_start_cb;
    disp_drv.draw_buf = &draw_buf;
    // Force LVGL to always flush a complete frame. This avoids stale pixels
    // being preserved between screen transitions when only dirty rectangles
    // are invalidated.
    disp_drv.full_refresh = 1;
    lv_disp_drv_register(&disp_drv);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);      /*Basic initialization*/
    indev_drv.type = LV_INDEV_TYPE_POINTER;                 /*See below.*/
    indev_drv.read_cb = my_input_read;              /*See below.*/
    /*Register the driver in LVGL and save the created input device object*/
    // static lv_indev_t * my_indev = lv_indev_drv_register(&indev_drv);
    lv_indev_drv_register(&indev_drv);
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
        // Prevent the remaining touch-release sequence from re-triggering app clicks.
        touch_ignore_start_ms = millis();
        touch_ignore_until_release = true;
        home_button_pending = true;
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

    heap_caps_print_heap_info(MALLOC_CAP_INTERNAL);
    heap_caps_print_heap_info(MALLOC_CAP_SPIRAM);

    epd_poweron();
    epd_clear();
    epd_poweroff();

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

    // Set input current limit, default is 500mA
    PPM.setInputCurrentLimit(3250);

    Serial.printf("getInputCurrentLimit: %d mA\n", PPM.getInputCurrentLimit());

    // Disable current limit pin
    PPM.disableCurrentLimitPin();

    // Set the charging target voltage, Range:3840 ~ 4608mV ,step:16 mV
    PPM.setChargeTargetVoltage(4208);

    // Set the precharge current , Range: 64mA ~ 1024mA ,step:64mA
    PPM.setPrechargeCurr(64);

    // The premise is that Limit Pin is disabled, or it will only follow the maximum charging current set by Limi tPin.
    // Set the charging current , Range:0~5056mA ,step:64mA
    PPM.setChargerConstantCurr(1024);

    // Get the set charging current
    PPM.getChargerConstantCurr();
    Serial.printf("getChargerConstantCurr: %d mA\n", PPM.getChargerConstantCurr());


    // To obtain voltage data, the ADC must be enabled first
    PPM.enableMeasure();

    // Turn on charging function
    // If there is no battery connected, do not turn on the charging function
    PPM.enableCharge();

    // Turn off charging function
    // If USB is used as the only power input, it is best to turn off the charging function,
    // otherwise the VSYS power supply will have a sawtooth wave, affecting the discharge output capability.
    // PPM.disableCharge();


    // The OTG function needs to enable OTG, and set the OTG control pin to HIGH
    // After OTG is enabled, if an external power supply is plugged in, OTG will be turned off

    PPM.enableOTG();
    PPM.disableOTG();
    // pinMode(OTG_ENABLE_PIN, OUTPUT);
    // digitalWrite(OTG_ENABLE_PIN, HIGH);

    return result;
}

static bool bq27220_init(void)
{
    return bq27220.init();
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
    SerialGPS.begin(38400, SERIAL_8N1, BOARD_GPS_RXD, BOARD_GPS_TXD);
    // // while (!Serial);

    SPI.begin(BOARD_SPI_SCLK, BOARD_SPI_MISO, BOARD_SPI_MOSI);
    Wire.begin(BOARD_SDA, BOARD_SCL);

    pinMode(BOARD_BL_EN, OUTPUT);
    analogWrite(BOARD_BL_EN, 0); // Keep backlight off until user setting is applied
    pinMode(BOARD_BOOT_BTN, INPUT_PULLUP);

    // Init system
    ui_nvs_set_defaulat_param();

    peri_buf[E_PERI_BQ27220]    = bq27220_init();   // PMU --- 0x55
    
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

    peri_buf[E_PERI_BQ25896]    = bq25896_init();   // PMU --- 0x6B
    cursor_x = 100;
    cursor_y = epd_rotated_display_height() / 2 - 100 + 50;
    disp_init_status("BQ25896 Init ...", &cursor_x, &cursor_y, peri_buf[E_PERI_BQ25896]);

    peri_buf[E_PERI_RTC]        = rtc_pcf8563_init(); // RTC --- 0x51
    cursor_x = 100;
    cursor_y = epd_rotated_display_height() / 2 - 100 + 100;
    disp_init_status("RTC (PCF8563) Init ...", &cursor_x, &cursor_y, peri_buf[E_PERI_RTC]);

    peri_buf[E_PERI_TOUCH]      = touch_gt911_init();  // Touch --- 0x5D;
    cursor_x = 100;
    cursor_y = epd_rotated_display_height() / 2 - 100 + 150;
    disp_init_status("Touch (GT911) Init ...", &cursor_x, &cursor_y, peri_buf[E_PERI_TOUCH]);

    peri_buf[E_PERI_LORA]       = lora_sx1262_init();
    cursor_x = 100;
    cursor_y = epd_rotated_display_height() / 2 - 100 + 200;
    disp_init_status("LoRa (SX1262) Init ...", &cursor_x, &cursor_y, peri_buf[E_PERI_LORA]);

    peri_buf[E_PERI_SD_CARD]    = sd_card_init();
    cursor_x = 100;
    cursor_y = epd_rotated_display_height() / 2 - 100 + 250;
    disp_init_status("SD Card Init ...", &cursor_x, &cursor_y, peri_buf[E_PERI_SD_CARD]);

    peri_buf[E_PERI_GPS]        = gps_init();
    cursor_x = 100;
    cursor_y = epd_rotated_display_height() / 2 - 100 +300;
    disp_init_status("GPS Init ...", &cursor_x, &cursor_y, peri_buf[E_PERI_GPS]);

    printf("LVGL Init\n");
    lv_port_disp_init();
    xTaskCreatePinnedToCore(disp_flush_task, "disp_flush_task", 1024 * 6, NULL, 2, &disp_flush_handle, 0);

    printf("LVGL UI Entry\n");
    ui_entry();

    // task
    xTaskCreate(btn_task, "lora_task", 1024 * 3, NULL, INFARED_PRIORITY, &btn_handle);
}

void idf_loop() 
{
    if(home_button_pending) {
        home_button_pending = false;
        scr_mgr_switch(0, false); // Return to the main screen
    }
    lv_task_handler();
    ui_wifi_service_loop();
    delay(1);
}
