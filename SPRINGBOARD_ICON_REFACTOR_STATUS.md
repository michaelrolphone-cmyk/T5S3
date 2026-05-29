# Springboard Icon Refactor Status (H752-01)

## Date
- 2026-05-26 (UTC)

## Context
The current springboard implementation in `examples/factory/main/ui.cpp` decodes PNG icons into full LVGL true-color icon buffers and keeps these buffers resident for icons across both pages. This causes high PSRAM/heap pressure.

## What was done in this session
1. Located the springboard code path and identified the memory-heavy flow:
   - `create0()` allocates icon canvas buffers for both pages.
   - `springboard_decode_png_to_buf()` decodes PNGs directly to retained `lv_color_t` buffers.
   - `destroy0()` frees only at screen destruction, not per hidden page transition.
2. Confirmed icon routing table and alias handling locations that must not regress.
3. Attempted an in-place refactor, then reverted to avoid leaving a partially-broken state.

## Current state
- 2026-05-29 update: springboard icon `.sbi` payloads are now retained in internal SRAM after their first SD-cache read for the lifetime of the springboard screen.
- Page transitions still unload the active LVGL canvas objects and full-size visible buffers, but returning to a previously loaded page expands the retained SRAM payload instead of reopening the SD cache file.
- `destroy0()` releases both transient page resources and retained SRAM icon payloads.

## Root cause summary
The memory issue is caused by retaining full-size decoded icon buffers (`150x150 lv_color_t`) for every springboard icon on both pages simultaneously, instead of caching compact icon data and loading only the visible page.

## Step-by-step implementation plan

### Step 1 — Add cache format + helpers (no behavior change yet)
Implement in `examples/factory/main/ui.cpp`:
- Constants:
  - `SPRINGBOARD_ICON_CACHE_DIR = "/system/cache/icons"`
  - cache magic/version/format constants.
- Struct:
  - `springboard_icon_cache_header` with fields: magic, version, width, height, format, threshold, source_size, payload_size.
- Helpers:
  - `springboard_log_heap(tag)` using `esp_heap_caps.h`.
  - `springboard_make_cache_path(...)`.
  - directory creation helper for `/system`, `/system/cache`, `/system/cache/icons` under SD lock.

**Verification target after step 1**
- Build still passes.
- No runtime behavior changes expected.

### Step 2 — Add cache validation/load/build primitives
Implement:
- `springboard_icon_cache_valid(png_path, cache_path)`
- `springboard_build_icon_cache_from_png(...)`
- `springboard_load_cached_icon_to_lvgl_buffer(...)`
- Update PNG draw callback state to write mono 1bpp payload instead of direct LVGL canvas pixels.

Preserve:
- PNG signature checks.
- PNG size limits.
- source dimension limits.
- SD lock discipline.
- threshold behavior and centering/scaling.

**Verification target after step 2**
- Build passes.
- Logs show cache miss/build/hit paths on demand (manual runtime check).

### Step 3 — Split icon object creation from icon data loading
Refactor springboard icon runtime shape so each slot has:
- stable clickable container object,
- dynamic child (canvas for cache-loaded icon or fallback image),
- dynamic `visible_buf` allocated only when page is visible.

Add functions:
- `springboard_create_page_icons(...)`
- `springboard_load_page_icons(page)`
- `springboard_unload_page_icons(page)`
- `springboard_show_page(page)`

Behavior in `create0()`:
- create both pages and placeholder slots,
- hide page 2,
- load only page 0 icon buffers.

**Verification target after step 3**
- Build passes.
- Page 1 icons load and route correctly.
- Page 2 loads only after swipe.

### Step 4 — Wire gesture transitions to unload/load page icons
Update gesture page switching flow:
- unload previous page icon buffers,
- hide previous page,
- show next page,
- load next page icon buffers,
- keep page dots behavior unchanged.

**Verification target after step 4**
- Repeated swipes do not continuously leak memory.
- Logs show `[MEM][ICON]` before/after load/unload.

### Step 5 — Update `destroy0()` cleanup ownership rules
Ensure:
- unload page 0 + page 1 dynamic icon resources,
- clear runtime state,
- no double free of LVGL-owned objects,
- static fallback assets never freed.

**Verification target after step 5**
- Build passes.
- No crash on entering/exiting springboard screen repeatedly.

### Step 6 — Full acceptance verification
Run:
- `pio run -e T5_E_PAPER_S3_V7`

Manual runtime checks (device/serial):
- first boot: cache miss/build logs and created `.sbi` files,
- second boot: cache hit logs, no repeated PNG decode,
- page switch: unload/load logs and stable memory,
- no regressions in taskbar, gestures, dots, click routing, aliases, fallback.

## Known risks to watch while implementing
- Accidentally breaking icon click routing indices (0..12).
- Canvas child object still pointing to freed `visible_buf`.
- SD lock imbalance on early returns.
- Cache path collisions if sanitization is too aggressive.
- Writing cache with `FILE_WRITE` behavior (append vs truncate) depending on FS implementation; explicitly remove existing cache file before rewrite if needed.

## Commands run in this session
- `rg --files -g 'AGENTS.md'`
- `sed -n '1,260p' examples/factory/main/ui.cpp`
- `rg -n "springboard|menu_screen1|icon_buf|PNG" examples/factory/main/ui.cpp`
- `sed -n '330,980p' examples/factory/main/ui.cpp`
- `git checkout -- examples/factory/main/ui.cpp`

## Next immediate action
Run hardware/serial acceptance checks on a T5 ePaper S3 with icon files on SD:
- first springboard page visit should log `cache payload retained in sram`,
- returning to that page should log `sram reuse` without SD cache reads for already retained icons,
- repeated page transitions should keep click routing, aliases, fallback behavior, and heap usage stable.
