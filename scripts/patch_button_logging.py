from pathlib import Path
Import("env")

p = Path(env["PROJECT_DIR"]) / "examples" / "factory" / "main" / "main.cpp"
lines = p.read_text(encoding="utf-8").splitlines(keepends=True)
out = []
i = 0
removed = 0

while i < len(lines):
    line = lines[i]

    if "#if defined(DEBUG_BUTTON_RAW_LOG)" in line:
        removed += 1
        i += 1
        while i < len(lines) and "#endif" not in lines[i]:
            i += 1
        if i < len(lines):
            i += 1
        continue

    if 'Serial.printf("[BUTTON RAW]' in line:
        removed += 1
        i += 1
        while i < len(lines) and "use_pca_fallback);" not in lines[i]:
            i += 1
        if i < len(lines):
            i += 1
        continue

    out.append(line)
    i += 1

p.write_text("".join(out), encoding="utf-8")
print(f"[PATCH] removed BUTTON RAW polling log blocks: {removed}")
