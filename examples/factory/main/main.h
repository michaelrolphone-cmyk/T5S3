#pragma once
// include 
#include <Arduino.h>
// #include "epd_driver.h"
#include "utilities.h"
// #include "firasans.h"
#include "esp_adc_cal.h"
#include <Wire.h>
#include "TouchDrvGT911.hpp"
#include <SensorPCF8563.hpp>
#include <WiFi.h>
#include <esp_sntp.h>
#include <RadioLib.h>
#include <FS.h>
#include <SPI.h>
#include <SD.h>
#include <EEPROM.h>
#include "bq27220.h"
#define XPOWERS_CHIP_BQ25896
#include <XPowersLib.h>
#include "board/pca9555.h"
#include <TinyGPS++.h>

#include "FS.h"
#include "SD.h"
#include "SPI.h"

#define GLOBAL_BUF_LEN 48
extern char global_buf[GLOBAL_BUF_LEN];

#define SYSTEM_SLEEP_IMAGE_PATH "/system/display/sleep.png"
#define LEGACY_SLEEP_IMAGE_PATH "/sleep.png"
#define FALLBACK_SLEEP_ICON_PATH "/icons/apps/sleep.png"

// io_extend
extern "C" {
    void io_extend_lora_gps_power_on(bool en);
    uint8_t read_io(int io);
    void set_config(i2c_port_t port, uint8_t config_value, int high_port);
    bool button_read(void);
}

// peripheral
// |        RTC (PCF8563)         |
// | :--------------------------: |
// |        BQ25896 (MPU)         |
// |     BQ27220 (Coulometer)     |
// |           SD Card            |
// |        GT911 (Tocuh)         |
// |     PCA9535 (IO extend)      |
// | TPS651851 (Ink Screen Power) |
// |        Lora (SX1262)         |
// |             GPS              |
enum {
    E_PERI_INK_POWER = 0,
    E_PERI_BQ25896,
    E_PERI_BQ27220,
    E_PERI_RTC,
    E_PERI_TOUCH,
    E_PERI_LORA,
    E_PERI_SD_CARD,
    E_PERI_GPS,
    E_PERI_WIFI,
    E_PERI_MAX,
};

extern bool peri_buf[E_PERI_MAX];

// gps
extern TaskHandle_t gps_handle;
extern TinyGPSPlus gps;
extern double gps_lat, gps_lng;
extern uint16_t gps_year;
extern uint8_t gps_month, gps_day;
extern uint8_t gps_hour, gps_minute, gps_second;

// lora
extern SX1262 radio;

extern BQ27220 bq27220;

// bq25896
extern XPowersPPM PPM;

// display refresh mode
void disp_full_refresh(void);
void disp_full_clean(void);
void dips_clean(void);
void disp_refresh_screen(void);
void disp_request_normal_frame(void);
void disp_request_screen_replace(void);
void disp_request_boot_replace(void);
void disp_request_full_clear(void);
bool disp_show_sleep_png_from_sd(const char *preferred_path);
bool display_begin_shutdown_sequence(uint32_t timeout_ms);
void display_cancel_pending_updates_for_shutdown(void);
bool display_show_shutdown_image_from_sd(const char *path);
void sd_guard_init();
bool sd_guard_lock(uint32_t timeout_ms);
void sd_guard_unlock();
bool sd_card_ensure_ready();
bool sd_card_ensure_ready_locked(const char *owner = NULL);
const char *sd_card_mount_point();

void indev_touch_en();
void indev_touch_dis();
void touch_begin_home_transition_guard(uint32_t min_block_ms);
bool touch_home_transition_guard_active(void);
bool touch_reject_stale_home_event(void);
bool ui_is_ui_thread(void);

// Touch
extern TouchDrvGT911 touch;

// RTC
extern SensorPCF8563 rtc;
