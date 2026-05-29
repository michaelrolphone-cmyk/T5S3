from pathlib import Path
Import("env")

p = Path(env["PROJECT_DIR"]) / "examples" / "factory" / "main" / "main.cpp"
s = p.read_text(encoding="utf-8")

old = '''        Serial.printf("[BUTTON RAW] gpio48=%d pressed=%d ready=%d fallback=%d\n",
                      gpio_raw, gpio_pressed, gpio_btn_ready, use_pca_fallback);
'''

new = '''#if defined(DEBUG_BUTTON_RAW_LOG) && DEBUG_BUTTON_RAW_LOG
        static uint32_t last_button_raw_log_ms = 0;
        uint32_t raw_log_now = millis();
        if ((raw_log_now - last_button_raw_log_ms) >= 1000) {
            last_button_raw_log_ms = raw_log_now;
            Serial.printf("[BUTTON RAW] gpio48=%d pressed=%d ready=%d fallback=%d\n",
                          gpio_raw, gpio_pressed, gpio_btn_ready, use_pca_fallback);
        }
#endif
'''

if old in s:
    s = s.replace(old, new, 1)
    p.write_text(s, encoding="utf-8")
    print("[PATCH] disabled BUTTON RAW spam unless DEBUG_BUTTON_RAW_LOG=1")
elif "[BUTTON RAW]" in s and "DEBUG_BUTTON_RAW_LOG" in s:
    print("[PATCH] BUTTON RAW log already gated")
else:
    raise RuntimeError("Could not locate BUTTON RAW Serial.printf block")
