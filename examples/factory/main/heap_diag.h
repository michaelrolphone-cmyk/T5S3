#pragma once

#include <Arduino.h>
#include <esp_heap_caps.h>

void heap_diag_mark(const char *tag);

#define HEAP_DIAG_EXPR(label, expr) \
    ([&]() { \
        heap_diag_mark("before " label); \
        auto _heap_diag_result = (expr); \
        heap_diag_mark("after " label); \
        return _heap_diag_result; \
    }())

#define HEAP_DIAG_VOID(label, expr) \
    do { \
        heap_diag_mark("before " label); \
        (expr); \
        heap_diag_mark("after " label); \
    } while (0)
