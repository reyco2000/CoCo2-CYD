# Touch Calibration Feature — Design Spec
**Date:** 2026-05-05  
**Project:** CoCo2-CYD (ESP32 CYD emulator)

---

## Overview

Add a runtime XPT2046 touchscreen calibration feature triggered by holding the ESP32 BOOT button (GPIO0) for 5 seconds. Shows 6 on-screen crosshair targets, collects raw ADC values from each tap, computes calibration constants, saves them to NVS flash, and restarts the device. On every boot the saved constants override the compile-time `config.h` defaults.

---

## Trigger

- **Button:** GPIO0 (BOOT button, active LOW, internal pull-up)
- **Activation:** Hold for 5 continuous seconds
- **Feedback during hold:** Small countdown text drawn in the TFT top border (`"CAL: 4s..."`, `"CAL: 3s..."` etc.) using the existing border area (same region as the FPS overlay). Text is cleared if the button is released early.
- **Detection method:** Polled each frame via `millis()` — no interrupt needed.

---

## New Files

### `src/hal/hal_touch_cal.h`
Declares:
```cpp
void touch_cal_load_nvs();           // called once at keyboard init
void touch_cal_check_boot_button();  // called every frame from hal_process_input()
```
`touch_cal_run(TFT_eSPI*)` is internal — not exposed in the header.

### `src/hal/hal_touch_cal.cpp`
Implements all three functions (see sections below).

---

## Modified Files

### `src/hal/hal_keyboard.cpp`
- Replace direct `TOUCH_X_MIN`, `TOUCH_X_MAX`, `TOUCH_Y_MIN`, `TOUCH_Y_MAX` references in the `map()` calls (lines 439–440) with four `static uint16_t` runtime variables:
  ```cpp
  static uint16_t s_touch_x_min = TOUCH_X_MIN;
  static uint16_t s_touch_x_max = TOUCH_X_MAX;
  static uint16_t s_touch_y_min = TOUCH_Y_MIN;
  static uint16_t s_touch_y_max = TOUCH_Y_MAX;
  ```
- Call `touch_cal_load_nvs()` during keyboard init (inside `hal_keyboard_update_touch()` lazy-init block, after `xpt2046_init()`). `touch_cal_load_nvs()` updates the four statics if NVS data is present.
- Expose a setter so `touch_cal_load_nvs()` can write the statics:
  ```cpp
  void hal_keyboard_set_touch_cal(uint16_t x_min, uint16_t x_max,
                                   uint16_t y_min, uint16_t y_max);
  ```

### `src/hal/hal_video.cpp`
- No changes needed — `hal_video_get_tft()` already exists (line 327). `hal_touch_cal.cpp` will `extern`-declare it at the top of the file, exactly as `hal_keyboard.cpp` does at line 47.

### `src/hal/hal_keyboard.cpp`
- Replace the four `TOUCH_X/Y_MIN/MAX` references in `map()` with `static uint16_t` runtime variables (see above).
- Add a public wrapper (not static) so `hal_touch_cal.cpp` can call the raw ADC reader:
  ```cpp
  bool hal_keyboard_read_raw_xy(uint16_t* out_x, uint16_t* out_y) {
      return xpt2046_read_xy(out_x, out_y);
  }
  ```
- Add `hal_keyboard_set_touch_cal()` implementation.

### `src/hal/hal.h`
- Add declarations:
  ```cpp
  bool hal_keyboard_read_raw_xy(uint16_t* out_x, uint16_t* out_y);
  void hal_keyboard_set_touch_cal(uint16_t x_min, uint16_t x_max,
                                   uint16_t y_min, uint16_t y_max);
  ```

### `CoCo2-CYD.ino`
- Add `#include "src/hal/hal_touch_cal.h"`
- Add `touch_cal_check_boot_button()` call inside `hal_process_input()`, after touch update.

---

## Boot Button Detection (`touch_cal_check_boot_button`)

```
static uint32_t s_hold_start_ms = 0;
static bool     s_counting      = false;
static int      s_last_sec_shown = -1;

bool held = (digitalRead(0) == LOW);

if (held && !s_counting) {
    s_hold_start_ms = millis();
    s_counting = true;
    s_last_sec_shown = -1;
}
if (!held) {
    if (s_counting) clear_countdown_text();
    s_counting = false;
    s_last_sec_shown = -1;
    return;
}

uint32_t elapsed = millis() - s_hold_start_ms;
int remaining = 5 - (int)(elapsed / 1000);

if (remaining != s_last_sec_shown && remaining > 0) {
    draw_countdown(remaining);   // draws in TFT top border
    s_last_sec_shown = remaining;
}
if (elapsed >= 5000) {
    s_counting = false;
    touch_cal_run(hal_video_get_tft());
}
```

`pinMode(0, INPUT_PULLUP)` is called once inside `touch_cal_load_nvs()`.

---

## Calibration UI (`touch_cal_run`)

### Screen setup
- Clear full TFT to black
- Draw title: `"Touch Calibration"` (white, large font, centered top)
- Draw subtitle: `"Tap each target precisely"` (grey, small font)

### 6 target points (screen coordinates)

| # | Screen X | Screen Y | Role |
|---|----------|----------|------|
| 1 | 20       | 20       | top-left |
| 2 | 299      | 20       | top-right |
| 3 | 160      | 20       | top-center |
| 4 | 20       | 219      | bottom-left |
| 5 | 299      | 219      | bottom-right |
| 6 | 160      | 219      | bottom-center |

### Per-point flow
1. Draw crosshair (20px horizontal + vertical lines) with a 10px circle at the target
2. Draw `"Point N/6"` label below target
3. Poll `hal_keyboard_read_raw_xy()` in a blocking loop (10ms delay between polls)
4. On touch-down: record `raw_x` and `raw_y`
5. Wait for touch-up
6. Draw green checkmark over crosshair, pause 300ms, advance to next point

### Calibration math
After collecting all 6 `{raw_x[i], raw_y[i]}` pairs:

```
// raw_y (0x90 channel) = horizontal axis → drives TOUCH_X_MIN/MAX
x_min = min(raw_y[0], raw_y[3])          // left points: 1, 4
x_max = max(raw_y[1], raw_y[4])          // right points: 2, 5

// raw_x (0xD0 channel) = vertical axis → drives TOUCH_Y_MIN/MAX
y_min = min(raw_x[0], raw_x[1], raw_x[2])  // top points: 1, 2, 3
y_max = max(raw_x[3], raw_x[4], raw_x[5])  // bottom points: 4, 5, 6
```

This matches the intentional axis swap in `hal_keyboard.cpp` (raw_y→screen X, raw_x→screen Y).

### Save and restart
```cpp
Preferences prefs;
prefs.begin("touch", false);
prefs.putUInt("x_min", x_min);
prefs.putUInt("x_max", x_max);
prefs.putUInt("y_min", y_min);
prefs.putUInt("y_max", y_max);
prefs.end();
```
Show `"Saved! Restarting..."` for 2000ms, then `ESP.restart()`.

---

## NVS Load (`touch_cal_load_nvs`)

```cpp
void touch_cal_load_nvs() {
    pinMode(0, INPUT_PULLUP);  // boot button init

    Preferences prefs;
    prefs.begin("touch", true);
    if (prefs.isKey("x_min")) {
        hal_keyboard_set_touch_cal(
            (uint16_t)prefs.getUInt("x_min", TOUCH_X_MIN),
            (uint16_t)prefs.getUInt("x_max", TOUCH_X_MAX),
            (uint16_t)prefs.getUInt("y_min", TOUCH_Y_MIN),
            (uint16_t)prefs.getUInt("y_max", TOUCH_Y_MAX)
        );
    }
    prefs.end();
}
```

If no NVS calibration exists, `s_touch_x/y_min/max` remain at their `config.h` defaults — no change to current behaviour.

---

## Axis Mapping Reference

| NVS key | Drives | Raw channel | Maps to |
|---------|--------|-------------|---------|
| `x_min` / `x_max` | `s_touch_x_min/max` | 0x90 (raw_y) | screen X 0–319 |
| `y_min` / `y_max` | `s_touch_y_min/max` | 0xD0 (raw_x) | screen Y 0–239 |

---

## Out of Scope

- Reset-to-defaults option (can be added later by erasing `"touch"` NVS namespace)
- Affine/skew correction (linear min/max is sufficient for XPT2046 panels)
- Calibration accessible via supervisor OSD menu
