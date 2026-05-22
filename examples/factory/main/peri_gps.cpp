
#include "utilities.h"
#include "peripheral.h"
#include "main.h"
#include <TinyGPS++.h>
#include <time.h>
#include <math.h>

/* clang-format off */

TinyGPSPlus gps;
static bool GPS_Recovery();
bool setupGPS();
void displayInfo();
static bool gps_csv_ensure_dir();
static bool gps_csv_make_daily_path(char *out, size_t out_len);
static bool gps_csv_make_timestamp(char *out, size_t out_len);
static void gps_csv_append_fix(double lat, double lon, double speed_kmph, uint32_t satellites);
static void gps_debug_log(const char *msg);
static void gps_capture_pending_fix();
static bool gps_csv_time_ready();
static bool gps_should_log_fix(double lat, double lon);
static void gps_try_write_pending_fix();

TaskHandle_t gps_handle = NULL;
double gps_lat=0, gps_lng=0, gps_altitude=0, gps_speed=0;
uint16_t gps_year=0;
uint8_t gps_month=0, gps_day=0;
uint8_t gps_hour=0, gps_minute=0, gps_second=0;
static uint32_t gps_vsat=0;
static bool gps_ready = false;
static int gps_last_sync_minute = -1;
static uint32_t last_gps_diag_ms = 0;
static uint32_t last_gps_sd_diag_ms = 0;
static bool gps_pending_fix_valid = false;
static double gps_pending_lat = 0.0;
static double gps_pending_lon = 0.0;
static double gps_pending_speed_kmph = 0.0;
static uint32_t gps_pending_satellites = 0;
static uint32_t gps_pending_fix_ms = 0;
static double gps_last_logged_lat = 999.0;
static double gps_last_logged_lon = 999.0;
static uint32_t gps_last_logged_ms = 0;

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
        Serial.println("GPS Task Create...!");
        gps_task_create();
        if (peri_buf[E_PERI_SD_CARD]) {
            gps_debug_log("[GPS CSV] boot: logger active");
        }
        result = (gps_handle != NULL);
    } else {
        SerialGPS.end();
    }
    return result;
}

void gps_task(void *param)
{
    while(1)
    {
        while (Serial.available()) {
            SerialGPS.write(Serial.read());
        }

        while (SerialGPS.available()) {
            int c = SerialGPS.read();
            // Serial.write(c);
            if (gps.encode(c)) {
                displayInfo();
                if (gps.location.isUpdated() && gps.location.isValid()) {
                    gps_capture_pending_fix();
                }
                gps_try_write_pending_fix();
            }
        }
        gps_try_write_pending_fix();

        if (millis() - last_gps_diag_ms > 10000) {
            last_gps_diag_ms = millis();
            Serial.printf("[GPS] chars=%lu fix_valid=%d loc_updated=%d date_valid=%d time_valid=%d sats=%lu sd=%d\n",
                          gps.charsProcessed(),
                          gps.location.isValid(),
                          gps.location.isUpdated(),
                          gps.date.isValid(),
                          gps.time.isValid(),
                          gps.satellites.isValid() ? gps.satellites.value() : 0,
                          peri_buf[E_PERI_SD_CARD]);
        }
        if (millis() - last_gps_sd_diag_ms > 60000) {
            last_gps_sd_diag_ms = millis();
            char diag[160] = {0};
            snprintf(diag, sizeof(diag),
                     "[GPS] chars=%lu fix_valid=%d date_valid=%d time_valid=%d sats=%lu sd=%d pending=%d",
                     gps.charsProcessed(),
                     gps.location.isValid(),
                     gps.date.isValid(),
                     gps.time.isValid(),
                     gps.satellites.isValid() ? gps.satellites.value() : 0,
                     peri_buf[E_PERI_SD_CARD],
                     gps_pending_fix_valid);
            gps_debug_log(diag);
        }

        if (millis() > 30000 && gps.charsProcessed() < 10) {
            Serial.println(F("No GPS detected: check wiring."));
            gps_debug_log("[GPS] No GPS detected: chars not increasing");
            delay(1000);
        }
        delay(1);
    }
}

void gps_task_create(void)
{
    if (gps_handle != NULL) {
        return;
    }

    if (xTaskCreate(gps_task, "gps_task", 1024 * 3, NULL, GPS_PRIORITY, &gps_handle) != pdPASS) {
        Serial.println("GPS task create failed!");
        gps_handle = NULL;
        gps_ready = false;
        return;
    }

    gps_ready = true;
    gps_debug_log("[GPS CSV] task running; logger active");
}

uint32_t gps_get_charsProcessed(void)
{
    return gps_ready ? gps.charsProcessed() : 0;
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

/* clang-format on */
void displayInfo()
{
    Serial.print(F("Location: "));
    if (gps.location.isValid())
    {
        gps_lat = gps.location.lat();
        gps_lng = gps.location.lng();
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
        gps_year = gps.date.year();
        gps_month = gps.date.month();
        gps_day = gps.date.day();
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
        gps_hour = gps.time.hour();
        gps_minute = gps.time.minute();
        gps_second = gps.time.second();

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
        gps_vsat = gps.satellites.value();
        Serial.print(gps_vsat);
        Serial.print(F(" "));
    }

    Serial.print(F("  Speed: "));
    if(gps.speed.isValid())
    {
        gps_speed = gps.speed.kmph();
        Serial.print(gps_speed);
        Serial.print(F(" "));
    }

    Serial.println();

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
    if (!SD.exists("/gps")) {
        if (!SD.mkdir("/gps")) {
            gps_debug_log("[GPS CSV] failed to create /gps directory");
            return false;
        }
        if (SD.exists("/gps")) {
            gps_debug_log("[GPS CSV] created /gps directory");
        } else {
            gps_debug_log("[GPS CSV] /gps directory still missing after mkdir");
            return false;
        }
    }
    return true;
}

static bool gps_csv_make_daily_path(char *out, size_t out_len)
{
    uint16_t year = 0;
    uint8_t month = 0;
    uint8_t day = 0;

    if (gps.date.isValid() && gps.date.year() >= 2000 && gps.date.month() >= 1 && gps.date.month() <= 12 &&
        gps.date.day() >= 1 && gps.date.day() <= 31) {
        year = gps.date.year();
        month = gps.date.month();
        day = gps.date.day();
    } else if (peri_buf[E_PERI_RTC]) {
        RTC_DateTime dt = rtc.getDateTime();
        if (dt.year >= 2000 && dt.month >= 1 && dt.month <= 12 && dt.day >= 1 && dt.day <= 31) {
            year = dt.year;
            month = dt.month;
            day = dt.day;
        }
    }

    if (year == 0 || month == 0 || day == 0) {
        return false;
    }

    snprintf(out, out_len, "/gps/%04u-%02u-%02u.csv", year, month, day);
    return true;
}

static bool gps_csv_make_timestamp(char *out, size_t out_len)
{
    uint16_t year = 0;
    uint8_t month = 0;
    uint8_t day = 0;
    uint8_t hour = 0;
    uint8_t minute = 0;
    uint8_t second = 0;

    if (gps.date.isValid() && gps.date.year() >= 2000 && gps.date.month() >= 1 && gps.date.month() <= 12 &&
        gps.date.day() >= 1 && gps.date.day() <= 31) {
        year = gps.date.year();
        month = gps.date.month();
        day = gps.date.day();
    } else if (peri_buf[E_PERI_RTC]) {
        RTC_DateTime dt = rtc.getDateTime();
        if (dt.year >= 2000 && dt.month >= 1 && dt.month <= 12 && dt.day >= 1 && dt.day <= 31) {
            year = dt.year;
            month = dt.month;
            day = dt.day;
        }
    }

    if (gps.time.isValid()) {
        hour = gps.time.hour();
        minute = gps.time.minute();
        second = gps.time.second();
    } else if (peri_buf[E_PERI_RTC]) {
        RTC_DateTime dt = rtc.getDateTime();
        hour = dt.hour;
        minute = dt.minute;
        second = dt.second;
    } else {
        return false;
    }

    if (year == 0 || month == 0 || day == 0) {
        return false;
    }

    snprintf(out, out_len, "%04u-%02u-%02uT%02u:%02u:%02uZ", year, month, day, hour, minute, second);
    return true;
}

static void gps_csv_append_fix(double lat, double lon, double speed_kmph, uint32_t satellites)
{
    if (lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0 || (lat == 0.0 && lon == 0.0)) {
        gps_debug_log("[GPS CSV] skipped invalid passed coordinate");
        return;
    }

    if (!gps_csv_ensure_dir()) {
        return;
    }

    char path[32] = {0};
    char timestamp[32] = {0};
    if (!gps_csv_make_daily_path(path, sizeof(path)) || !gps_csv_make_timestamp(timestamp, sizeof(timestamp))) {
        gps_debug_log("[GPS CSV] date/time invalid");
        return;
    }

    bool exists = SD.exists(path);
    File f = SD.open(path, FILE_APPEND);
    if (!f) {
        gps_debug_log("[GPS CSV] CSV open failed");
        return;
    }

    if (!exists) {
        f.println("timestamp,latitude,longitude,speed_kmph,satellites");
    }

    f.printf("%s,%.8f,%.8f,%.2f,%u\n", timestamp, lat, lon, speed_kmph, satellites);
    f.close();
    gps_last_logged_lat = lat;
    gps_last_logged_lon = lon;
    gps_last_logged_ms = millis();
    gps_debug_log("[GPS CSV] CSV row written");

    Serial.printf("[GPS CSV] logged %s lat=%.8f lon=%.8f sat=%u speed=%.2f\n",
                  path, lat, lon, satellites, speed_kmph);
}

static void gps_debug_log(const char *msg)
{
    Serial.println(msg);

    if (!peri_buf[E_PERI_SD_CARD]) {
        return;
    }

    if (!SD.exists("/gps")) {
        SD.mkdir("/gps");
    }

    File f = SD.open("/gps/gps_debug.txt", FILE_APPEND);
    if (!f) {
        return;
    }

    f.printf("%lu,%s\n", (unsigned long)millis(), msg);
    f.close();
}

static void gps_capture_pending_fix()
{
    if (!gps.location.isValid()) {
        gps_debug_log("[GPS CSV] location invalid");
        return;
    }
    double lat = gps.location.lat();
    double lon = gps.location.lng();
    if (lat == 0.0 && lon == 0.0) {
        gps_debug_log("[GPS CSV] ignored 0,0 coordinate");
        return;
    }
    gps_pending_lat = lat;
    gps_pending_lon = lon;
    gps_pending_speed_kmph = gps.speed.isValid() ? gps.speed.kmph() : 0.0;
    gps_pending_satellites = gps.satellites.isValid() ? gps.satellites.value() : 0;
    gps_pending_fix_ms = millis();
    gps_pending_fix_valid = true;
    gps_debug_log("[GPS CSV] pending fix stored");
    gps_debug_log("[GPS CSV] pending fix captured");
}

static bool gps_csv_time_ready()
{
    if (gps.date.isValid() && gps.date.year() >= 2000 && gps.time.isValid()) return true;
    if (peri_buf[E_PERI_RTC]) {
        RTC_DateTime dt = rtc.getDateTime();
        if (dt.year >= 2000 && dt.month >= 1 && dt.month <= 12 && dt.day >= 1 && dt.day <= 31) return true;
    }
    return false;
}

static bool gps_should_log_fix(double lat, double lon)
{
    uint32_t now = millis();
    if (gps_last_logged_lat > 900.0 || gps_last_logged_lon > 900.0) return true;
    double dlat = fabs(lat - gps_last_logged_lat);
    double dlon = fabs(lon - gps_last_logged_lon);
    if (dlat > 0.00001 || dlon > 0.00001) return true;
    if (now - gps_last_logged_ms >= 10000) return true;
    return false;
}

static void gps_try_write_pending_fix()
{
    if (!gps_pending_fix_valid) return;
    if (!gps_csv_time_ready()) {
        static uint32_t last_wait_log_ms = 0;
        if (millis() - last_wait_log_ms > 10000) {
            last_wait_log_ms = millis();
            gps_debug_log("[GPS CSV] pending fix waiting for valid date/time");
        }
        return;
    }
    if (!gps_should_log_fix(gps_pending_lat, gps_pending_lon)) {
        gps_pending_fix_valid = false;
        return;
    }
    gps_csv_append_fix(gps_pending_lat, gps_pending_lon, gps_pending_speed_kmph, gps_pending_satellites);
    gps_debug_log("[GPS CSV] pending fix written");
    gps_pending_fix_valid = false;
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
