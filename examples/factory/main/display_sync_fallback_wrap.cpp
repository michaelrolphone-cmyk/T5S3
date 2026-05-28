#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

extern "C" TaskHandle_t __real_xTaskCreateStaticPinnedToCore(
    TaskFunction_t pxTaskCode,
    const char * const pcName,
    const uint32_t ulStackDepth,
    void * const pvParameters,
    UBaseType_t uxPriority,
    StackType_t * const puxStackBuffer,
    StaticTask_t * const pxTaskBuffer,
    const BaseType_t xCoreID);

extern "C" TaskHandle_t __wrap_xTaskCreateStaticPinnedToCore(
    TaskFunction_t pxTaskCode,
    const char * const pcName,
    const uint32_t ulStackDepth,
    void * const pvParameters,
    UBaseType_t uxPriority,
    StackType_t * const puxStackBuffer,
    StaticTask_t * const pxTaskBuffer,
    const BaseType_t xCoreID)
{
    if (pcName && strcmp(pcName, "disp_flush_task") == 0) {
        Serial.println("[DISPLAY LIFECYCLE] async display worker intentionally disabled; using synchronous LVGL flush fallback");
        return NULL;
    }

    return __real_xTaskCreateStaticPinnedToCore(
        pxTaskCode,
        pcName,
        ulStackDepth,
        pvParameters,
        uxPriority,
        puxStackBuffer,
        pxTaskBuffer,
        xCoreID);
}
