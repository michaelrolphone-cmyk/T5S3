from pathlib import Path
import re
Import("env")

p = Path(env["PROJECT_DIR"]) / "examples" / "factory" / "main" / "main.cpp"
s = p.read_text(encoding="utf-8")

pattern = re.compile(
    r'\n\s*Serial\.printf\("\[BUTTON RAW\] gpio48=%d pressed=%d ready=%d fallback=%d\\n",\s*\n\s*gpio_raw,\s*gpio_pressed,\s*gpio_btn_ready,\s*use_pca_fallback\);\s*\n',
    re.MULTILINE,
)

replacement = '''
#if defined(DEBUG_BUTTON_RAW_LOG) && DEBUG_BUTTON_RAW_LOG
        static uint32_t last_button_raw_log_ms = 0;
        uint32_t raw_log_now = millis();
        if ((raw_log_now - last_button_raw_log_ms) >= 1000) {
            last_button_raw_log_ms = raw_log_now;
            Serial.printf("[BUTTON RAW] gpio48=%d pressed=%d ready=%d fallback=%d\\n",
                          gpio_raw, gpio_pressed, gpio_btn_ready, use_pca_fallback);
        }
#endif
'''

s2, n = pattern.subn("\n" + replacement, s, count=1)
if n > 0:
    p.write_text(s2, encoding="utf-8")
    print("[PATCH] BUTTON RAW spam gated behind DEBUG_BUTTON_RAW_LOG")
elif "DEBUG_BUTTON_RAW_LOG" in s:
    print("[PATCH] BUTTON RAW log already gated")
elif "[BUTTON RAW]" in s:
    raise RuntimeError("BUTTON RAW marker remains but known printf pattern did not match")
else:
    print("[PATCH] BUTTON RAW log not present")
