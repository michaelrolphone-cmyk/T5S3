#include <Arduino.h>
#include <esp_heap_caps.h>
#include <epdiy.h>

extern "C" void __real_epd_poweron(void);

#ifndef EPD_POWERON_MIN_INTERNAL_FREE
#define EPD_POWERON_MIN_INTERNAL_FREE 4096
#endif

#ifndef EPD_POWERON_MIN_INTERNAL_LARGEST
#define EPD_POWERON_MIN_INTERNAL_LARGEST 512
#endif

extern "C" void __wrap_epd_poweron(void)
{
    const size_t free_i = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    const size_t largest_i = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);

    if (free_i < EPD_POWERON_MIN_INTERNAL_FREE || largest_i < EPD_POWERON_MIN_INTERNAL_LARGEST) {
        Serial.printf("[EPD POWER GUARD] skip epd_poweron low_internal_heap free=%u largest=%u min_free=%u min_largest=%u\n",
                      (unsigned)free_i,
                      (unsigned)largest_i,
                      (unsigned)EPD_POWERON_MIN_INTERNAL_FREE,
                      (unsigned)EPD_POWERON_MIN_INTERNAL_LARGEST);
        return;
    }

    __real_epd_poweron();
}
