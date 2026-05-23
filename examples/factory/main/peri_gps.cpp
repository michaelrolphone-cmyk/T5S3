
#include "utilities.h"
#include "peripheral.h"
#include "main.h"
#include <TinyGPS++.h>
#include <time.h>
#include <math.h>
#include "esp_heap_caps.h"

/* clang-format off */

TinyGPSPlus gps;
typedef struct {
    bool valid;
    double lat;
    double lon;
    double speed_kmph;
    uint32_t satellites;
    uint32_t chars_processed;
    uint32_t fix_age_ms;
    uint32_t captured_ms;
    bool date_valid;
    bool time_valid;
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} gps_fix_snapshot_t;
static bool GPS_Recovery();
bool setupGPS();
void displayInfo();
static bool gps_csv_ensure_dir();
static bool gps_csv_make_path(char *out, size_t out_len, bool *dated, const gps_fix_snapshot_t *snapshot);
static void gps_csv_make_timestamp(char *out, size_t out_len, const gps_fix_snapshot_t *snapshot);
static bool gps_csv_append_snapshot_fix(const gps_fix_snapshot_t *snapshot);
static void gps_debug_log(const char *msg);
static bool gps_snapshot_fix_valid(const gps_fix_snapshot_t *snapshot, uint32_t now_ms);
static void gps_log_status(const gps_fix_snapshot_t *snapshot);
static void gps_status_csv_append(const gps_fix_snapshot_t *snapshot);
static void gps_log_parser_heartbeat(uint32_t now_ms);
void gps_logger_task(void *param);
void gps_service_loop(void);

TaskHandle_t gps_handle = NULL;
static TaskHandle_t gps_logger_handle = NULL;
static gps_fix_snapshot_t gps_latest_fix = {0};
static portMUX_TYPE gps_fix_mux = portMUX_INITIALIZER_UNLOCKED;
double gps_lat=0, gps_lng=0, gps_altitude=0, gps_speed=0;
uint16_t gps_year=0;
uint8_t gps_month=0, gps_day=0;
uint8_t gps_hour=0, gps_minute=0, gps_second=0;
static uint32_t gps_vsat=0;
static bool gps_ready = false;
static bool gps_logger_ready = false;
static int gps_last_sync_minute = -1;
static uint32_t gps_last_csv_write_ms = 0;
static const uint32_t GPS_CSV_PERIOD_MS = 10000;
static const bool GPS_SERIAL_VERBOSE = false;
static const bool GPS_BRIDGE_SERIAL_INPUT = false;
static const uint32_t GPS_HEARTBEAT_PERIOD_MS = 10000;
static uint32_t gps_last_heartbeat_ms = 0;
static uint32_t gps_last_no_data_warn_ms = 0;

uint8_t buffer[256];

bool gps_init(void)
{   
    bool result = false;
    gps_ready = false;
    gps_handle = NULL;

    // L76K GPS USE 9600 BAUDRATE
    result = setupGPS();
    if(!result) {
        // Set u-blox m10q gps baudrate 38400
        SerialGPS.begin(38400, SERIAL_8N1, BOARD_GPS_RXD, BOARD_GPS_TXD);
        SerialGPS.setTimeout(10);
        result = GPS_Recovery();
        if (!result) {
            SerialGPS.updateBaudRate(9600);
            result = GPS_Recovery();
            if (!result) {
                Serial.println("GPS Connect failed~!");
                result = false;
            }
            SerialGPS.updateBaudRate(38400);
        }
    }

    if(result) {
        gps_ready = true;
        Serial.println("GPS Task Create...!");
        gps_task_create();
        if (peri_buf[E_PERI_SD_CARD]) {
            gps_debug_log("[GPS CSV] boot: logger active");
        }
    } else {
        SerialGPS.end();
    }
    return result;
}


void gps_service_loop(void)
{
    if (!gps_ready) {
        return;
    }

    if (GPS_BRIDGE_SERIAL_INPUT) {
        while (Serial.available()) {
            SerialGPS.write(Serial.read());
        }
    }

    while (SerialGPS.available()) {
        int c = SerialGPS.read();
        if (gps.encode(c)) {
            displayInfo();
            if (gps.location.isValid()) {
                gps_fix_snapshot_t snapshot = {0};
                snapshot.valid = true;
                snapshot.lat = gps.location.lat();
                snapshot.lon = gps.location.lng();
                snapshot.speed_kmph = gps.speed.isValid() ? gps.speed.kmph() : 0.0;
                snapshot.satellites = gps.satellites.isValid() ? gps.satellites.value() : 0;
                snapshot.chars_processed = gps.charsProcessed();
                snapshot.fix_age_ms = gps.location.age();
                snapshot.captured_ms = millis();
                snapshot.date_valid = gps.date.isValid();
                snapshot.time_valid = gps.time.isValid();
                if (snapshot.date_valid) {
                    snapshot.year = gps.date.year();
                    snapshot.month = gps.date.month();
                    snapshot.day = gps.date.day();
                }
                if (snapshot.time_valid) {
                    snapshot.hour = gps.time.hour();
                    snapshot.minute = gps.time.minute();
                    snapshot.second = gps.time.second();
                }
                portENTER_CRITICAL(&gps_fix_mux);
                gps_latest_fix = snapshot;
                portEXIT_CRITICAL(&gps_fix_mux);
            }
        }
    }

    uint32_t now = millis();
    if ((now > 30000) && (gps.charsProcessed() < 10) && (now - gps_last_no_data_warn_ms >= 1000)) {
        gps_last_no_data_warn_ms = now;
        Serial.println(F("No GPS detected: check wiring."));
    }
    if (now - gps_last_heartbeat_ms >= GPS_HEARTBEAT_PERIOD_MS) {
        gps_last_heartbeat_ms = now;
        gps_log_parser_heartbeat(now);
    }
    if (!gps_logger_ready) {
        gps_fix_snapshot_t snapshot = {0};
        portENTER_CRITICAL(&gps_fix_mux);
        snapshot = gps_latest_fix;
        portEXIT_CRITICAL(&gps_fix_mux);
        if (gps_snapshot_fix_valid(&snapshot, now) && (now - gps_last_csv_write_ms >= GPS_CSV_PERIOD_MS)) {
            if (gps_csv_append_snapshot_fix(&snapshot)) {
                gps_last_csv_write_ms = now;
            }
        }
    }
}

void gps_task(void *param)
{
    uint32_t last_stack_log_ms = 0;
    while(1)
    {
        gps_service_loop();
        uint32_t now = millis();
        if (now - last_stack_log_ms >= GPS_HEARTBEAT_PERIOD_MS) {
            last_stack_log_ms = now;
            UBaseType_t parser_hwm = uxTaskGetStackHighWaterMark(NULL);
            Serial.printf("[GPS TASK] parser stack_hwm=%u\n", (unsigned)parser_hwm);
        }
        delay(1);
    }
}

void gps_logger_task(void *param)
{
    static uint32_t last_status_csv_ms = 0;
    while (1) {
        gps_fix_snapshot_t snapshot = {0};
        portENTER_CRITICAL(&gps_fix_mux);
        snapshot = gps_latest_fix;
        portEXIT_CRITICAL(&gps_fix_mux);

        uint32_t now = millis();
        if (now - last_status_csv_ms >= 60000) {
            last_status_csv_ms = now;
            gps_log_status(&snapshot);
            gps_status_csv_append(&snapshot);
            UBaseType_t logger_hwm = uxTaskGetStackHighWaterMark(NULL);
            Serial.printf("[GPS TASK] logger stack_hwm=%u\n", (unsigned)logger_hwm);
        }
        if (gps_snapshot_fix_valid(&snapshot, now) && (now - gps_last_csv_write_ms >= GPS_CSV_PERIOD_MS)) {
            if (gps_csv_append_snapshot_fix(&snapshot)) {
                gps_last_csv_write_ms = now;
            }
        }
        delay(1000);
    }
}

void gps_task_create(void)
{
    if (gps_handle != NULL) {
        return;
    }

    size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t largest_internal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    Serial.printf("[GPS TASK] create parser pre free_internal=%u largest_internal=%u free_psram=%u\n",
                  (unsigned)free_internal, (unsigned)largest_internal, (unsigned)free_psram);

    static const size_t GPS_PARSER_MIN_LARGEST_INTERNAL = 3072;
    if (largest_internal < GPS_PARSER_MIN_LARGEST_INTERNAL) {
        Serial.println("[GPS TASK WARN] insufficient internal heap for parser task; using polling parser");
        Serial.println("[GPS TASK WARN] parser task create failed; using polling parser");
        Serial.println("[GPS] hardware init ok; parser mode=polling");
        gps_logger_ready = false;
        return;
    }

    BaseType_t parser_rc = xTaskCreate(gps_task, "gps_task", 1024 * 3, NULL, GPS_PRIORITY, &gps_handle);
    Serial.printf("[GPS TASK] parser create rc=%d handle=%p\n", (int)parser_rc, gps_handle);
    if (gps_handle == NULL) {
        Serial.println("[GPS TASK WARN] parser task create failed; using polling parser");
        Serial.println("[GPS] hardware init ok; parser mode=polling");
        gps_logger_ready = false;
        return;
    }
    Serial.println("[GPS] hardware init ok; parser task started");

    UBaseType_t logger_priority = (GPS_PRIORITY > 0) ? (GPS_PRIORITY - 1) : GPS_PRIORITY;
    free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    largest_internal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    Serial.printf("[GPS TASK] create logger pre free_internal=%u largest_internal=%u free_psram=%u\n",
                  (unsigned)free_internal, (unsigned)largest_internal, (unsigned)free_psram);
    BaseType_t logger_rc = xTaskCreate(gps_logger_task, "gps_logger_task",
                                         1024 * 4, NULL, logger_priority, &gps_logger_handle);
    Serial.printf("[GPS TASK] logger create rc=%d handle=%p\n", (int)logger_rc, gps_logger_handle);
    gps_logger_ready = (gps_logger_handle != NULL);
    if (!gps_logger_ready) {
        Serial.println("[GPS TASK WARN] logger task create failed; parser remains active");
    }

    gps_ready = true;
    gps_debug_log(gps_logger_ready ? "[GPS CSV] task running; logger active" : "[GPS CSV] logger unavailable; parser-only CSV mode active");
}


static void gps_log_parser_heartbeat(uint32_t now_ms)
{
    (void)now_ms;
    uint32_t chars = gps.charsProcessed();
    bool valid = gps.location.isValid();
    uint32_t age = gps.location.age();
    uint32_t sats = gps.satellites.isValid() ? gps.satellites.value() : 0;
    double lat = gps.location.isValid() ? gps.location.lat() : gps_lat;
    double lon = gps.location.isValid() ? gps.location.lng() : gps_lng;
    const char *mode = (gps_handle != NULL) ? "task" : "polling";
    Serial.printf("[GPS] mode=%s chars=%u valid=%u age=%u sats=%u lat=%.6f lon=%.6f\n",
                  mode, (unsigned)chars, valid ? 1U : 0U, (unsigned)age, (unsigned)sats, lat, lon);
}

uint32_t gps_get_charsProcessed(void)
{
    return gps.charsProcessed();
}

void gps_task_suspend(void)
{
    if (gps_handle != NULL) {
        vTaskSuspend(gps_handle);
    }
}

void gps_task_resume(void)
{
    if (gps_handle != NULL) {
        vTaskResume(gps_handle);
    }
}

void gps_get_coord(double *lat, double *lng)
{
    *lat = gps_lat;
    *lng = gps_lng;
}

void gps_get_data(uint16_t *year, uint8_t *month, uint8_t *day)
{
    *year = gps_year;
    *month = gps_month;
    *day = gps_day;
}

void gps_get_time(uint8_t *hour, uint8_t *minute, uint8_t *second)
{
    *hour = gps_hour;
    *minute = gps_minute;
    *second = gps_second;
}

void gps_get_satellites(uint32_t *vsat)
{
    *vsat = gps_vsat;   // Visible Satellites
}

void gps_get_speed(double *speed)
{
    *speed = gps_speed;
}

bool gps_is_ready(void)
{
    return gps_ready;
}

bool gps_has_serial_data(void)
{
    return (millis() > 30000) && (gps.charsProcessed() > 0);
}

bool gps_has_fix(void)
{
    return gps.location.isValid();
}

/* clang-format on */
void displayInfo()
{
    if (gps.location.isValid())
    {
        gps_lat = gps.location.lat();
        gps_lng = gps.location.lng();
    }

    if (gps.date.isValid())
    {
        gps_year = gps.date.year();
        gps_month = gps.date.month();
        gps_day = gps.date.day();
    }

    if (gps.time.isValid())
    {
        gps_hour = gps.time.hour();
        gps_minute = gps.time.minute();
        gps_second = gps.time.second();
    }

    if(gps.satellites.isValid())
    {
        gps_vsat = gps.satellites.value();
    }

    if(gps.speed.isValid())
    {
        gps_speed = gps.speed.kmph();
    }

    if (GPS_SERIAL_VERBOSE) {
        Serial.print(F("Location: "));
        if (gps.location.isValid())
        {
            Serial.print(gps_lat, 6);
            Serial.print(F(","));
            Serial.print(gps_lng, 6);
        }
        else
        {
            Serial.print(F("INVALID"));
        }

        Serial.print(F("  Date/Time: "));
        if (gps.date.isValid())
        {
            Serial.print(gps_month);
            Serial.print(F("/"));
            Serial.print(gps_day);
            Serial.print(F("/"));
            Serial.print(gps_year);
        }
        else
        {
            Serial.print(F("INVALID"));
        }

        Serial.print(F(" "));
        if (gps.time.isValid())
        {
            if (gps_hour < 10)
                Serial.print(F("0"));
            Serial.print(gps_hour);
            Serial.print(F(":"));
            if (gps_minute < 10)
                Serial.print(F("0"));
            Serial.print(gps_minute);
            Serial.print(F(":"));
            if (gps_second < 10)
                Serial.print(F("0"));
            Serial.print(gps_second);
            Serial.print(F("."));
        }
        else
        {
            Serial.print(F("INVALID"));
        }

        Serial.print(F("  Satellites: "));
        if(gps.satellites.isValid())
        {
            Serial.print(gps_vsat);
            Serial.print(F(" "));
        }

        Serial.print(F("  Speed: "));
        if(gps.speed.isValid())
        {
            Serial.print(gps_speed);
            Serial.print(F(" "));
        }

        Serial.println();
    }

    if (peri_buf[E_PERI_RTC] && gps.date.isValid() && gps.time.isValid()) {
        if (gps_second != 0 || gps_minute == gps_last_sync_minute) {
            return;
        }

        const char *old_tz = getenv("TZ");
        char old_tz_buf[64] = {0};
        if (old_tz != NULL) {
            strncpy(old_tz_buf, old_tz, sizeof(old_tz_buf) - 1);
        }

        setenv("TZ", "UTC0", 1);
        tzset();
        struct tm tm_utc = {0};
        tm_utc.tm_year = gps_year - 1900;
        tm_utc.tm_mon = gps_month - 1;
        tm_utc.tm_mday = gps_day;
        tm_utc.tm_hour = gps_hour;
        tm_utc.tm_min = gps_minute;
        tm_utc.tm_sec = gps_second;

        time_t utc_epoch = mktime(&tm_utc);
        if (utc_epoch <= 0) {
            if (old_tz != NULL) {
                setenv("TZ", old_tz_buf, 1);
            } else {
                unsetenv("TZ");
            }
            tzset();
            return;
        }

        // GPS reports UTC. Convert to Mountain Time with DST support.
        // This handles standard time (MST, UTC-7) and daylight time (MDT, UTC-6).
        setenv("TZ", "MST7MDT,M3.2.0/2,M11.1.0/2", 1);
        tzset();
        struct tm tm_mt;
        localtime_r(&utc_epoch, &tm_mt);

        rtc.setDateTime(tm_mt.tm_year + 1900, tm_mt.tm_mon + 1, tm_mt.tm_mday,
                        tm_mt.tm_hour, tm_mt.tm_min, tm_mt.tm_sec);
        gps_last_sync_minute = gps_minute;
        Serial.printf("RTC synced from GPS (Mountain w/DST): %04d-%02d-%02d %02d:%02d:%02d\n",
                      tm_mt.tm_year + 1900, tm_mt.tm_mon + 1, tm_mt.tm_mday,
                      tm_mt.tm_hour, tm_mt.tm_min, tm_mt.tm_sec);

        if (old_tz != NULL) {
            setenv("TZ", old_tz_buf, 1);
        } else {
            unsetenv("TZ");
        }
        tzset();
    }
}

static bool gps_csv_ensure_dir()
{
    if (!peri_buf[E_PERI_SD_CARD]) {
        gps_debug_log("[GPS CSV] SD unavailable");
        return false;
    }
    if (!sd_guard_lock(2000)) {
        gps_debug_log("[GPS CSV] SD lock failed");
        return false;
    }
    if (!SD.exists("/gps")) {
        if (!SD.mkdir("/gps")) {
            sd_guard_unlock();
            gps_debug_log("[GPS CSV] failed to create /gps directory");
            return false;
        }
        if (SD.exists("/gps")) {
            gps_debug_log("[GPS CSV] created /gps directory");
        } else {
            gps_debug_log("[GPS CSV] /gps directory still missing after mkdir");
            sd_guard_unlock();
            return false;
        }
    }
    sd_guard_unlock();
    return true;
}

static bool gps_csv_make_path(char *out, size_t out_len, bool *dated, const gps_fix_snapshot_t *snapshot)
{
    *dated = false;

    uint16_t year = 0;
    uint8_t month = 0;
    uint8_t day = 0;

    if (snapshot->date_valid &&
        snapshot->year >= 2000 &&
        snapshot->month >= 1 && snapshot->month <= 12 &&
        snapshot->day >= 1 && snapshot->day <= 31) {
        year = snapshot->year;
        month = snapshot->month;
        day = snapshot->day;
    } else if (peri_buf[E_PERI_RTC]) {
        RTC_DateTime dt = rtc.getDateTime();
        if (dt.year >= 2000 &&
            dt.month >= 1 && dt.month <= 12 &&
            dt.day >= 1 && dt.day <= 31) {
            year = dt.year;
            month = dt.month;
            day = dt.day;
        }
    }

    if (year >= 2000) {
        snprintf(out, out_len, "/gps/%04u-%02u-%02u.csv", year, month, day);
        *dated = true;
        return true;
    }

    snprintf(out, out_len, "/gps/undated.csv");
    return true;
}

static void gps_csv_make_timestamp(char *out, size_t out_len, const gps_fix_snapshot_t *snapshot)
{
    uint16_t year = 0;
    uint8_t month = 0;
    uint8_t day = 0;
    uint8_t hour = 0;
    uint8_t minute = 0;
    uint8_t second = 0;

    bool have_date = false;
    bool have_time = false;

    if (snapshot->date_valid &&
        snapshot->year >= 2000 &&
        snapshot->month >= 1 && snapshot->month <= 12 &&
        snapshot->day >= 1 && snapshot->day <= 31) {
        year = snapshot->year;
        month = snapshot->month;
        day = snapshot->day;
        have_date = true;
    } else if (peri_buf[E_PERI_RTC]) {
        RTC_DateTime dt = rtc.getDateTime();
        if (dt.year >= 2000 &&
            dt.month >= 1 && dt.month <= 12 &&
            dt.day >= 1 && dt.day <= 31) {
            year = dt.year;
            month = dt.month;
            day = dt.day;
            have_date = true;
        }
    }

    if (snapshot->time_valid) {
        hour = snapshot->hour;
        minute = snapshot->minute;
        second = snapshot->second;
        have_time = true;
    } else if (peri_buf[E_PERI_RTC]) {
        RTC_DateTime dt = rtc.getDateTime();
        hour = dt.hour;
        minute = dt.minute;
        second = dt.second;
        have_time = true;
    }

    if (have_date && have_time) {
        snprintf(out, out_len, "%04u-%02u-%02uT%02u:%02u:%02uZ",
                 year, month, day, hour, minute, second);
        return;
    }

    snprintf(out, out_len, "millis:%lu", (unsigned long)millis());
}

static bool gps_csv_append_snapshot_fix(const gps_fix_snapshot_t *snapshot)
{
    double lat = snapshot->lat;
    double lon = snapshot->lon;
    if (lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0 || (lat == 0.0 && lon == 0.0)) {
        gps_debug_log("[GPS CSV] skipped invalid coordinate");
        return false;
    }

    if (!gps_csv_ensure_dir()) {
        return false;
    }

    char path[40] = {0};
    char timestamp[40] = {0};
    bool dated = false;

    gps_csv_make_path(path, sizeof(path), &dated, snapshot);
    gps_csv_make_timestamp(timestamp, sizeof(timestamp), snapshot);

    if (!sd_guard_lock(3000)) { gps_debug_log("[GPS CSV] SD lock failed for append"); return false; }
    bool exists = SD.exists(path);
    File f = SD.open(path, FILE_APPEND);
    if (!f) {
        char msg[96];
        snprintf(msg, sizeof(msg), "[GPS CSV] failed to open %s", path);
        gps_debug_log(msg);
        sd_guard_unlock();
        return false;
    }

    if (!exists) {
        f.println("timestamp,latitude,longitude,speed_kmph,satellites,fix_age_ms,chars_processed,dated_file");
    }

    f.printf("%s,%.8f,%.8f,%.2f,%u,%lu,%lu,%u\n",
             timestamp,
             lat,
             lon,
             snapshot->speed_kmph,
             snapshot->satellites,
             (unsigned long)snapshot->fix_age_ms,
             (unsigned long)snapshot->chars_processed,
             dated ? 1 : 0);

    f.close();
    sd_guard_unlock();

    char msg[128];
    snprintf(msg, sizeof(msg), "[GPS CSV] wrote %s lat=%.8f lon=%.8f sats=%u",
             path, lat, lon, snapshot->satellites);
    Serial.println(msg);
    gps_debug_log(msg);

    return true;
}

static bool gps_snapshot_fix_valid(const gps_fix_snapshot_t *snapshot, uint32_t now_ms)
{
    if (!snapshot->valid) return false;
    double lat = snapshot->lat;
    double lon = snapshot->lon;

    if (lat < -90.0 || lat > 90.0) return false;
    if (lon < -180.0 || lon > 180.0) return false;
    if (lat == 0.0 && lon == 0.0) return false;
    if ((now_ms - snapshot->captured_ms) > 30000) return false;

    return true;
}

static void gps_log_status(const gps_fix_snapshot_t *snapshot)
{
    static uint32_t last_status_log_ms = 0;
    if (millis() - last_status_log_ms < 60000) {
        return;
    }
    last_status_log_ms = millis();

    double lat = snapshot->valid ? snapshot->lat : 0.0;
    double lon = snapshot->valid ? snapshot->lon : 0.0;
    uint32_t sats = snapshot->satellites;

    char msg[220] = {0};
    snprintf(msg, sizeof(msg),
             "[GPS STATUS] chars=%lu loc_valid=%d loc_age=%lu lat=%.8f lon=%.8f date_valid=%d time_valid=%d sats=%lu sd=%d last_csv_ms=%lu",
             (unsigned long)snapshot->chars_processed,
             snapshot->valid ? 1 : 0,
             (unsigned long)snapshot->fix_age_ms,
             lat,
             lon,
             snapshot->date_valid ? 1 : 0,
             snapshot->time_valid ? 1 : 0,
             (unsigned long)sats,
             peri_buf[E_PERI_SD_CARD] ? 1 : 0,
             (unsigned long)gps_last_csv_write_ms);
    gps_debug_log(msg);
}

static void gps_status_csv_append(const gps_fix_snapshot_t *snapshot)
{
    static uint32_t last_status_csv_ms = 0;
    if (millis() - last_status_csv_ms < 60000) {
        return;
    }
    last_status_csv_ms = millis();

    if (!gps_csv_ensure_dir()) {
        return;
    }

    const char *path = "/gps/gps_status.csv";
    if (!sd_guard_lock(2000)) { gps_debug_log("[GPS STATUS] SD lock failed"); return; }
    bool exists = SD.exists(path);
    File f = SD.open(path, FILE_APPEND);
    if (!f) {
        gps_debug_log("[GPS STATUS] failed to open /gps/gps_status.csv");
        sd_guard_unlock();
        return;
    }

    if (!exists) {
        f.println("timestamp_millis,chars_processed,location_valid,location_age_ms,date_valid,time_valid,satellites,sd_available");
    }

    f.printf("%lu,%lu,%u,%lu,%u,%u,%lu,%u\n",
             (unsigned long)millis(),
             (unsigned long)snapshot->chars_processed,
             snapshot->valid ? 1U : 0U,
             (unsigned long)snapshot->fix_age_ms,
             snapshot->date_valid ? 1U : 0U,
             snapshot->time_valid ? 1U : 0U,
             (unsigned long)snapshot->satellites,
             peri_buf[E_PERI_SD_CARD] ? 1U : 0U);
    f.close();
    sd_guard_unlock();
}

static void gps_debug_log(const char *msg)
{
    if (!peri_buf[E_PERI_SD_CARD]) {
        return;
    }

    if (!sd_guard_lock(2000)) return;
    if (!SD.exists("/gps")) {
        SD.mkdir("/gps");
    }

    File f = SD.open("/gps/gps_debug.txt", FILE_APPEND);
    if (!f) {
        sd_guard_unlock();
        return;
    }

    f.printf("%lu,%s\n", (unsigned long)millis(), msg);
    f.close();
    sd_guard_unlock();
}
/* clang-format off */

bool setupGPS()
{
    // L76K GPS USE 9600 BAUDRATE
    SerialGPS.begin(9600, SERIAL_8N1, BOARD_GPS_RXD, BOARD_GPS_TXD);
    SerialGPS.setTimeout(10);
    bool result = false;
    uint32_t startTimeout ;
    for (int i = 0; i < 3; ++i) {
        SerialGPS.write("$PCAS03,0,0,0,0,0,0,0,0,0,0,,,0,0*02\r\n");
        delay(5);
        // Get version information
        startTimeout = millis() + 3000;
        Serial.print("Try to init L76K . Wait stop .");
        while (SerialGPS.available()) {
            Serial.print(".");
            SerialGPS.readString();
            if (millis() > startTimeout) {
                Serial.println("Wait L76K stop NMEA timeout!");
                return false;
            }
            delay(1);
        };
        Serial.println();
        SerialGPS.flush();
        delay(200);

        SerialGPS.write("$PCAS06,0*1B\r\n");
        startTimeout = millis() + 500;
        String ver = "";
        while (!SerialGPS.available()) {
            if (millis() > startTimeout) {
                Serial.println("Get L76K timeout!");
                return false;
            }
            delay(1);
        }
        ver = SerialGPS.readStringUntil('\n');
        if (ver.startsWith("$GPTXT,01,01,02")) {
            Serial.println("L76K GNSS init succeeded, using L76K GNSS Module\n");
            result = true;
            break;
        }
        delay(500);
    }
    // Initialize the L76K Chip, use GPS + GLONASS
    SerialGPS.write("$PCAS04,5*1C\r\n");
    delay(250);
    SerialGPS.write("$PCAS03,1,1,1,1,1,1,1,1,1,1,,,0,0*02\r\n");
    delay(250);
    // Switch to Vehicle Mode, since SoftRF enables Aviation < 2g
    SerialGPS.write("$PCAS11,3*1E\r\n");
    return result;
}

static int getAck(uint8_t *buffer, uint16_t size, uint8_t requestedClass, uint8_t requestedID)
{
    uint16_t    ubxFrameCounter = 0;
    bool        ubxFrame = 0;
    uint32_t    startTime = millis();
    uint16_t    needRead;

    while (millis() - startTime < 800) {
        while (SerialGPS.available()) {
            int c = SerialGPS.read();
            switch (ubxFrameCounter) {
            case 0:
                if (c == 0xB5) {
                    ubxFrameCounter++;
                }
                break;
            case 1:
                if (c == 0x62) {
                    ubxFrameCounter++;
                } else {
                    ubxFrameCounter = 0;
                }
                break;
            case 2:
                if (c == requestedClass) {
                    ubxFrameCounter++;
                } else {
                    ubxFrameCounter = 0;
                }
                break;
            case 3:
                if (c == requestedID) {
                    ubxFrameCounter++;
                } else {
                    ubxFrameCounter = 0;
                }
                break;
            case 4:
                needRead = c;
                ubxFrameCounter++;
                break;
            case 5:
                needRead |=  (c << 8);
                ubxFrameCounter++;
                break;
            case 6:
                if (needRead >= size) {
                    ubxFrameCounter = 0;
                    break;
                }
                if (SerialGPS.readBytes(buffer, needRead) != needRead) {
                    ubxFrameCounter = 0;
                } else {
                    return needRead;
                }
                break;

            default:
                break;
            }
            delay(1);
        }
        delay(1);
    }
    return 0;
}

static bool GPS_Recovery()
{
    uint8_t cfg_clear1[] = {0xB5, 0x62, 0x06, 0x09, 0x0D, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x1C, 0xA2};
    uint8_t cfg_clear2[] = {0xB5, 0x62, 0x06, 0x09, 0x0D, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x1B, 0xA1};
    uint8_t cfg_clear3[] = {0xB5, 0x62, 0x06, 0x09, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x03, 0x1D, 0xB3};
    SerialGPS.write(cfg_clear1, sizeof(cfg_clear1));

    if (getAck(buffer, 256, 0x05, 0x01)) {
        Serial.println("Get ack successes!");
    }
    SerialGPS.write(cfg_clear2, sizeof(cfg_clear2));
    if (getAck(buffer, 256, 0x05, 0x01)) {
        Serial.println("Get ack successes!");
    }
    SerialGPS.write(cfg_clear3, sizeof(cfg_clear3));
    if (getAck(buffer, 256, 0x05, 0x01)) {
        Serial.println("Get ack successes!");
    }

    // UBX-CFG-RATE, Size 8, 'Navigation/measurement rate settings'
    uint8_t cfg_rate[] = {0xB5, 0x62, 0x06, 0x08, 0x00, 0x00, 0x0E, 0x30};
    SerialGPS.write(cfg_rate, sizeof(cfg_rate));
    if (getAck(buffer, 256, 0x06, 0x08)) {
        Serial.println("Get ack successes!");
    } else {
        return false;
    }
    return true;
}
