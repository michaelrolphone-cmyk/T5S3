#ifndef __PERIPHERAL_H__
#define __PERIPHERAL_H__

#include <Arduino.h>

#define GPS_PRIORITY     (configMAX_PRIORITIES - 1)
#define LORA_PRIORITY    (configMAX_PRIORITIES - 2)
#define WS2812_PRIORITY  (configMAX_PRIORITIES - 3)
#define BATTERY_PRIORITY (configMAX_PRIORITIES - 4)
#define INFARED_PRIORITY (configMAX_PRIORITIES - 5)

// lora sx1262
#define LORA_FREQUENNCY     850.0
#define LORA_BANDWIDTH      125.0
#define LORA_OUTPUT_POWER   22 // -17 - 22 dBm
#define LORA_SPREAD_FACTOR  10

#define LORA_MODE_SEND 0
#define LORA_MODE_RECV 1

bool lora_sx1262_init(void);
void lora_set_mode(int mode);
int lora_get_mode(void);
void lora_receive_loop(void);
void lora_transmit(const char *str);
bool lora_get_recv(const char **str, int *rssi);
void lora_set_recv_flag(void);
void lora_sleep(void);
void lora_recv_suspend(void);
void lora_recv_resume(void);
void lora_param_set(void);

// gps u-blox m10q
bool gps_init(void);
void gps_task_create(void);
void gps_service_loop(void);
uint32_t gps_get_charsProcessed(void);
void gps_task_suspend(void);
void gps_task_resume(void);
void gps_get_coord(double *lat, double *lng);
void gps_get_data(uint16_t *year, uint8_t *month, uint8_t *day);
void gps_get_time(uint8_t *hour, uint8_t *minute, uint8_t *second);
void gps_get_satellites(uint32_t *vsat);
void gps_get_speed(double *speed);
bool gps_is_ready(void);
bool gps_has_serial_data(void);
bool gps_has_fix(void);

#endif