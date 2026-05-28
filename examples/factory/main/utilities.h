#pragma once

#define UI_T5_EPARPER_S3_PRO_VERSION    "v1.7-250915"  // Software version
#define BOARD_T5_EPARPER_S3_PRO_VERSION "v1.0-241224"  // Hardware version

// BOARD PIN DEFINE
#define BOARD_GPS_RXD       44
#define BOARD_GPS_TXD       43
#define SerialMon           Serial
#define SerialGPS           Serial2

#define BOARD_I2C_PORT      (0)
#define BOARD_SCL           (40)
#define BOARD_SDA           (39)

#define BOARD_SPI_MISO      (21)
#define BOARD_SPI_MOSI      (13)
#define BOARD_SPI_SCLK      (14)

#define BOARD_TOUCH_SCL     (BOARD_SCL)
#define BOARD_TOUCH_SDA     (BOARD_SDA)
#define BOARD_TOUCH_INT     (3)
#define BOARD_TOUCH_RST     (9)

#define BOARD_RTC_SCL       (BOARD_SCL)
#define BOARD_RTC_SDA       (BOARD_SDA)
#define BOARD_RTC_IRQ       (2)

#define BOARD_SD_MISO       (BOARD_SPI_MISO)
#define BOARD_SD_MOSI       (BOARD_SPI_MOSI)
#define BOARD_SD_SCLK       (BOARD_SPI_SCLK)
#define BOARD_SD_CS         (12)

#define BOARD_LORA_MISO     (BOARD_SPI_MISO)
#define BOARD_LORA_MOSI     (BOARD_SPI_MOSI)
#define BOARD_LORA_SCLK     (BOARD_SPI_SCLK)
#define BOARD_LORA_CS       (46)
#define BOARD_LORA_IRQ      (10)
#define BOARD_LORA_RST      (1)
#define BOARD_LORA_BUSY     (47)

#define BOARD_BL_EN         (11)
#define BOARD_PCA9535_INT   (38)
#define BOARD_BOOT_BTN      (0)

// Center/home button is wired to PCA9535 PC12 and read via button_read().
// GPIO38 is the PCA9535 interrupt line; do not read the button as ESP32 GPIO48.
#define BOARD_PCA_BUTTON_ACTIVE_HIGH   (0)

#ifdef __cplusplus
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <Arduino.h>
#include <string.h>

static inline BaseType_t t5s3_create_task_checked(TaskFunction_t task_fn,
                                                  const char *task_name,
                                                  const uint32_t stack_depth_bytes,
                                                  void *task_arg,
                                                  UBaseType_t priority,
                                                  TaskHandle_t *task_handle)
{
    if (task_name && strcmp(task_name, "lora_task") == 0) {
        // main.cpp currently creates btn_task late in boot using the historical
        // but misleading name "lora_task". At that point internal heap can be
        // below 2 KB, so the dynamic xTaskCreate() allocation silently fails.
        // Use static storage for that task so button polling still starts.
        static StaticTask_t button_task_tcb;
        static StackType_t button_task_stack[3072 / sizeof(StackType_t)];
        TaskHandle_t handle = xTaskCreateStatic(task_fn,
                                                task_name,
                                                sizeof(button_task_stack),
                                                task_arg,
                                                priority,
                                                button_task_stack,
                                                &button_task_tcb);
        if (task_handle) {
            *task_handle = handle;
        }
        Serial.printf("[BUTTON TASK] create_static name=%s stack_bytes=%u handle=%p free_heap=%u\n",
                      task_name,
                      (unsigned)sizeof(button_task_stack),
                      (void *)handle,
                      ESP.getFreeHeap());
        return handle ? pdPASS : errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY;
    }

    BaseType_t rc = xTaskCreatePinnedToCore(task_fn,
                                            task_name,
                                            stack_depth_bytes,
                                            task_arg,
                                            priority,
                                            task_handle,
                                            tskNO_AFFINITY);
    Serial.printf("[TASK] create_dynamic name=%s rc=%d handle=%p free_heap=%u\n",
                  task_name ? task_name : "<null>",
                  (int)rc,
                  task_handle ? (void *)*task_handle : NULL,
                  ESP.getFreeHeap());
    return rc;
}

#define xTaskCreate(task_fn, task_name, stack_depth_bytes, task_arg, priority, task_handle) \
    t5s3_create_task_checked((task_fn), (task_name), (stack_depth_bytes), (task_arg), (priority), (task_handle))
#endif
