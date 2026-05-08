# Touch Calibration Feature — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a runtime XPT2046 touchscreen calibration routine triggered by holding GPIO0 (BOOT button) for 5 seconds — shows 6 crosshair targets, collects raw ADC values, saves min/max constants to NVS, restarts.

**Architecture:** A new self-contained module `hal_touch_cal.cpp/.h` handles boot button polling, calibration UI, NVS persistence, and restart. `hal_keyboard.cpp` is modified to use runtime calibration variables (instead of compile-time `config.h` constants) and exposes two thin public wrappers the new module needs. All wiring happens in `hal.cpp`.

**Tech Stack:** ESP32 Arduino, TFT_eSPI (direct panel drawing), ESP32 Preferences (NVS flash), software SPI XPT2046 touch (existing bit-bang driver).

---

## File Map

| File | Action | Purpose |
|------|--------|---------|
| `src/hal/hal_touch_cal.h` | **Create** | Public interface: `touch_cal_load_nvs()`, `touch_cal_check_boot_button()` |
| `src/hal/hal_touch_cal.cpp` | **Create** | Boot button detection, calibration UI, NVS save/load, `ESP.restart()` |
| `src/hal/hal_keyboard.cpp` | **Modify** | Runtime cal variables, `hal_keyboard_read_raw_xy()`, `hal_keyboard_set_touch_cal()` |
| `src/hal/hal.h` | **Modify** | Declare `hal_keyboard_read_raw_xy()` and `hal_keyboard_set_touch_cal()` |
| `src/hal/hal.cpp` | **Modify** | Call `touch_cal_check_boot_button()` in `hal_process_input()` |

---

## Task 1: Add runtime calibration variables and public wrappers to hal_keyboard.cpp + hal.h

**Files:**
- Modify: `CoCo2-CYD/src/hal/hal_keyboard.cpp` (lines 101–104 area and 437–440)
- Modify: `CoCo2-CYD/src/hal/hal.h` (after line 115, keyboard section)

- [ ] **Step 1: Add four runtime calibration statics inside the `#if TOUCH_KB_ENABLED` block in `hal_keyboard.cpp`**

  Find the existing static block at ~line 101:
  ```cpp
  static CoCo2Keyboard*       s_osk             = nullptr;
  static bool                 s_osk_initialized = false;
  static bool                 s_osk_was_visible = false;
  static bool                 s_prev_touched    = false;
  ```
  Add four lines **before** that block (still inside `#if TOUCH_KB_ENABLED`):
  ```cpp
  static uint16_t s_touch_x_min = TOUCH_X_MIN;
  static uint16_t s_touch_x_max = TOUCH_X_MAX;
  static uint16_t s_touch_y_min = TOUCH_Y_MIN;
  static uint16_t s_touch_y_max = TOUCH_Y_MAX;
  ```

- [ ] **Step 2: Replace hardcoded defines with the new statics in the `map()` calls (~line 439)**

  Change:
  ```cpp
  tx = (uint16_t)constrain(map(raw_y, TOUCH_X_MIN, TOUCH_X_MAX, 0, 319), 0, 319);
  ty = (uint16_t)constrain(map(raw_x, TOUCH_Y_MIN, TOUCH_Y_MAX, 0, 239), 0, 239);
  ```
  To:
  ```cpp
  tx = (uint16_t)constrain(map(raw_y, s_touch_x_min, s_touch_x_max, 0, 319), 0, 319);
  ty = (uint16_t)constrain(map(raw_x, s_touch_y_min, s_touch_y_max, 0, 239), 0, 239);
  ```

- [ ] **Step 3: Add `hal_keyboard_set_touch_cal()` and `hal_keyboard_read_raw_xy()` to `hal_keyboard.cpp`**

  Add these two functions **after** the `xpt2046_read_xy()` static function (~line 95, before `s_osk` declarations), still inside `#if TOUCH_KB_ENABLED`:
  ```cpp
  void hal_keyboard_set_touch_cal(uint16_t x_min, uint16_t x_max,
                                   uint16_t y_min, uint16_t y_max) {
      s_touch_x_min = x_min;
      s_touch_x_max = x_max;
      s_touch_y_min = y_min;
      s_touch_y_max = y_max;
  }

  bool hal_keyboard_read_raw_xy(uint16_t* out_x, uint16_t* out_y) {
  #if TOUCH_KB_ENABLED
      return xpt2046_read_xy(out_x, out_y);
  #else
      return false;
  #endif
  }
  ```

  Note: `hal_keyboard_read_raw_xy` must be placed **outside** the `#if TOUCH_KB_ENABLED` block that wraps the statics, so it can provide the `return false` fallback. Move it after the `#endif` of that block, or add a separate guard inside the function body as shown above.

- [ ] **Step 4: Add declarations to `hal.h`**

  After the `hal_keyboard_draw_overlay()` declaration (~line 115), add:
  ```cpp
  // Read raw XPT2046 ADC values (used by calibration routine)
  bool hal_keyboard_read_raw_xy(uint16_t* out_x, uint16_t* out_y);

  // Override touch calibration constants at runtime (persisted via NVS)
  void hal_keyboard_set_touch_cal(uint16_t x_min, uint16_t x_max,
                                   uint16_t y_min, uint16_t y_max);
  ```

- [ ] **Step 5: Compile to verify Task 1**

  Run:
  ```bash
  arduino-cli compile --fqbn esp32:esp32:esp32 CoCo2-CYD/
  ```
  Expected: clean compile, 0 errors. Fix any errors before continuing.

- [ ] **Step 6: Commit**

  ```bash
  git add CoCo2-CYD/src/hal/hal_keyboard.cpp CoCo2-CYD/src/hal/hal.h
  git commit -m "feat: add runtime touch calibration variables and public wrappers"
  ```

---

## Task 2: Create `hal_touch_cal.h`

**Files:**
- Create: `CoCo2-CYD/src/hal/hal_touch_cal.h`

- [ ] **Step 1: Create the header file**

  Create `CoCo2-CYD/src/hal/hal_touch_cal.h` with this content:
  ```cpp
  #pragma once

  // Load saved XPT2046 calibration from NVS into runtime variables.
  // Falls back to config.h compile-time defaults if no NVS data exists.
  // Also initialises GPIO0 (BOOT button) as INPUT_PULLUP.
  // Call once during keyboard init.
  void touch_cal_load_nvs(void);

  // Poll GPIO0 for 5-second hold; draws countdown in TFT border.
  // Launches full-screen calibration UI when hold completes.
  // Call once per frame from hal_process_input().
  void touch_cal_check_boot_button(void);
  ```

- [ ] **Step 2: Compile to verify**

  ```bash
  arduino-cli compile --fqbn esp32:esp32:esp32 CoCo2-CYD/
  ```
  Expected: clean compile.

- [ ] **Step 3: Commit**

  ```bash
  git add CoCo2-CYD/src/hal/hal_touch_cal.h
  git commit -m "feat: add hal_touch_cal.h interface"
  ```

---

## Task 3: Implement `hal_touch_cal.cpp`

**Files:**
- Create: `CoCo2-CYD/src/hal/hal_touch_cal.cpp`

- [ ] **Step 1: Create `hal_touch_cal.cpp` with includes and calibration constants**

  Create `CoCo2-CYD/src/hal/hal_touch_cal.cpp`:
  ```cpp
  #include "hal_touch_cal.h"
  #include "hal.h"
  #include "../../config.h"
  #include <Preferences.h>
  #include <TFT_eSPI.h>

  extern TFT_eSPI* hal_video_get_tft(void);

  // Screen coordinates for the 6 calibration targets
  struct CalTarget { uint16_t sx; uint16_t sy; };
  static const CalTarget CAL_TARGETS[6] = {
      { 20,  20  },  // top-left
      { 299, 20  },  // top-right
      { 160, 20  },  // top-center
      { 20,  219 },  // bottom-left
      { 299, 219 },  // bottom-right
      { 160, 219 },  // bottom-center
  };
  ```

- [ ] **Step 2: Add `touch_cal_load_nvs()`**

  Append to `hal_touch_cal.cpp`:
  ```cpp
  void touch_cal_load_nvs(void) {
      pinMode(0, INPUT_PULLUP);  // BOOT button, active LOW

      Preferences prefs;
      prefs.begin("touch", true);  // read-only
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

- [ ] **Step 3: Add boot button countdown helpers and `touch_cal_check_boot_button()`**

  Append to `hal_touch_cal.cpp`:
  ```cpp
  static uint32_t s_hold_start_ms  = 0;
  static bool     s_counting       = false;
  static int      s_last_sec_shown = -1;

  static void draw_countdown(TFT_eSPI* tft, int seconds_remaining) {
      char buf[12];
      snprintf(buf, sizeof(buf), "CAL: %ds  ", seconds_remaining);
      tft->setTextColor(TFT_YELLOW, TFT_BLACK);
      tft->drawString(buf, 4, 4, 2);
  }

  static void clear_countdown(TFT_eSPI* tft) {
      tft->fillRect(0, 0, 80, 20, TFT_BLACK);
  }

  // Forward declaration — defined in Step 4
  static void touch_cal_run(TFT_eSPI* tft);

  void touch_cal_check_boot_button(void) {
      bool held = (digitalRead(0) == LOW);

      if (held && !s_counting) {
          s_hold_start_ms  = millis();
          s_counting       = true;
          s_last_sec_shown = -1;
      }
      if (!held) {
          if (s_counting) {
              TFT_eSPI* tft = hal_video_get_tft();
              if (tft) clear_countdown(tft);
          }
          s_counting       = false;
          s_last_sec_shown = -1;
          return;
      }

      uint32_t elapsed   = millis() - s_hold_start_ms;
      int      remaining = 5 - (int)(elapsed / 1000);

      if (remaining > 0 && remaining != s_last_sec_shown) {
          TFT_eSPI* tft = hal_video_get_tft();
          if (tft) draw_countdown(tft, remaining);
          s_last_sec_shown = remaining;
      }

      if (elapsed >= 5000) {
          s_counting = false;
          TFT_eSPI* tft = hal_video_get_tft();
          if (tft) touch_cal_run(tft);
      }
  }
  ```

- [ ] **Step 4: Add `touch_cal_run()` — full calibration UI**

  Append to `hal_touch_cal.cpp`:
  ```cpp
  static void draw_crosshair(TFT_eSPI* tft, uint16_t x, uint16_t y, uint16_t color) {
      tft->drawLine(x - 15, y,      x + 15, y,      color);
      tft->drawLine(x,      y - 15, x,      y + 15, color);
      tft->drawCircle(x, y, 8, color);
  }

  static void touch_cal_run(TFT_eSPI* tft) {
      // Intro screen
      tft->fillScreen(TFT_BLACK);
      tft->setTextDatum(MC_DATUM);
      tft->setTextColor(TFT_WHITE, TFT_BLACK);
      tft->drawString("Touch Calibration", 160, 100, 4);
      tft->setTextColor(TFT_LIGHTGREY, TFT_BLACK);
      tft->drawString("Tap each target precisely", 160, 130, 2);
      delay(1500);

      uint16_t raw_xs[6], raw_ys[6];

      for (int i = 0; i < 6; i++) {
          uint16_t sx = CAL_TARGETS[i].sx;
          uint16_t sy = CAL_TARGETS[i].sy;

          tft->fillScreen(TFT_BLACK);
          tft->setTextColor(TFT_WHITE, TFT_BLACK);
          char label[12];
          snprintf(label, sizeof(label), "Point %d / 6", i + 1);
          tft->drawString(label, 160, 115, 2);
          draw_crosshair(tft, sx, sy, TFT_WHITE);

          // Wait for touch-down
          uint16_t rx = 0, ry = 0;
          while (!hal_keyboard_read_raw_xy(&rx, &ry)) {
              delay(10);
          }
          raw_xs[i] = rx;
          raw_ys[i] = ry;

          // Wait for touch-up
          while (hal_keyboard_read_raw_xy(&rx, &ry)) {
              delay(10);
          }

          // Confirm tap with green crosshair
          draw_crosshair(tft, sx, sy, TFT_GREEN);
          delay(300);
      }

      // Calibration math
      // raw_y (0x90 channel) is the horizontal axis → drives TOUCH_X_MIN/MAX
      uint16_t x_min = min(raw_ys[0], raw_ys[3]);         // left: points 1, 4
      uint16_t x_max = max(raw_ys[1], raw_ys[4]);         // right: points 2, 5
      // raw_x (0xD0 channel) is the vertical axis → drives TOUCH_Y_MIN/MAX
      uint16_t y_min = min(raw_xs[0], min(raw_xs[1], raw_xs[2]));  // top: 1,2,3
      uint16_t y_max = max(raw_xs[3], max(raw_xs[4], raw_xs[5]));  // bot: 4,5,6

      // Save to NVS namespace "touch"
      Preferences prefs;
      prefs.begin("touch", false);
      prefs.putUInt("x_min", x_min);
      prefs.putUInt("x_max", x_max);
      prefs.putUInt("y_min", y_min);
      prefs.putUInt("y_max", y_max);
      prefs.end();

      // Confirm and restart
      tft->fillScreen(TFT_BLACK);
      tft->setTextColor(TFT_GREEN, TFT_BLACK);
      tft->drawString("Saved! Restarting...", 160, 115, 2);
      delay(2000);
      ESP.restart();
  }
  ```

- [ ] **Step 5: Compile to verify Task 3**

  ```bash
  arduino-cli compile --fqbn esp32:esp32:esp32 CoCo2-CYD/
  ```
  Expected: clean compile, 0 errors.

- [ ] **Step 6: Commit**

  ```bash
  git add CoCo2-CYD/src/hal/hal_touch_cal.cpp
  git commit -m "feat: implement touch calibration UI and NVS persistence"
  ```

---

## Task 4: Wire up in `hal_keyboard.cpp` and `hal.cpp`

**Files:**
- Modify: `CoCo2-CYD/src/hal/hal_keyboard.cpp` (lazy-init block, ~line 426)
- Modify: `CoCo2-CYD/src/hal/hal.cpp` (~line 39)

- [ ] **Step 1: Include `hal_touch_cal.h` in `hal_keyboard.cpp`**

  Near the top of `hal_keyboard.cpp`, after the existing includes (around line 40), add:
  ```cpp
  #include "hal_touch_cal.h"
  ```

- [ ] **Step 2: Call `touch_cal_load_nvs()` in the keyboard lazy-init block**

  In `hal_keyboard_update_touch()`, inside the `if (!s_osk_initialized)` block, add the call after `xpt2046_init()`:

  Change:
  ```cpp
  xpt2046_init();  // software SPI — HSPI stays free for SD card
  s_osk = new CoCo2Keyboard();
  ```
  To:
  ```cpp
  xpt2046_init();        // software SPI — HSPI stays free for SD card
  touch_cal_load_nvs();  // load NVS calibration (also inits BOOT button)
  s_osk = new CoCo2Keyboard();
  ```

- [ ] **Step 3: Include `hal_touch_cal.h` and add the boot button check in `hal.cpp`**

  In `hal.cpp`, add the include after existing includes:
  ```cpp
  #include "hal_touch_cal.h"
  ```

  In `hal_process_input()`, add the boot button check after `hal_keyboard_update_touch()`:

  Change:
  ```cpp
  void hal_process_input(void) {
      hal_keyboard_tick();
      hid_host_process();
      hal_keyboard_update_touch();
  #if JOYSTICK_ENABLED
  ```
  To:
  ```cpp
  void hal_process_input(void) {
      hal_keyboard_tick();
      hid_host_process();
      hal_keyboard_update_touch();
      touch_cal_check_boot_button();
  #if JOYSTICK_ENABLED
  ```

- [ ] **Step 4: Final compile**

  ```bash
  arduino-cli compile --fqbn esp32:esp32:esp32 CoCo2-CYD/
  ```
  Expected: clean compile, 0 errors, 0 warnings about undeclared functions.

- [ ] **Step 5: Upload to CYD device**

  ```bash
  arduino-cli upload --fqbn esp32:esp32:esp32 --port /dev/ttyUSB0 CoCo2-CYD/
  ```

- [ ] **Step 6: Manual verification**

  On device:
  1. Boot normally — emulator starts as expected, no change in behaviour
  2. Hold BOOT button — after 1 second, `"CAL: 4s  "` appears in top-left TFT corner; countdown decrements each second
  3. Release before 5s — countdown text disappears, emulator continues normally
  4. Hold BOOT button for 5+ seconds — screen goes black, calibration intro appears
  5. Tap each of the 6 crosshairs in order — each turns green after tap
  6. Device shows `"Saved! Restarting..."` and reboots
  7. After reboot — emulator runs with new calibration; test touch accuracy

- [ ] **Step 7: Commit**

  ```bash
  git add CoCo2-CYD/src/hal/hal_keyboard.cpp CoCo2-CYD/src/hal/hal.cpp
  git commit -m "feat: wire up touch calibration — boot button triggers 6-point calibration screen"
  ```

---

## Self-Review Checklist

- [x] Boot button GPIO0 init → `touch_cal_load_nvs()` (Task 3, Step 2)
- [x] 5-second hold detection with countdown → `touch_cal_check_boot_button()` (Task 3, Step 3)
- [x] 6 crosshair targets in B arrangement (4 corners + 2 top/bottom centers) → `touch_cal_run()` (Task 3, Step 4)
- [x] Axis swap preserved: raw_y→x_min/max, raw_x→y_min/max (Task 3, Step 4 comments)
- [x] NVS save under namespace `"touch"`, keys `x_min/x_max/y_min/y_max` (Task 3, Step 4)
- [x] NVS load at keyboard init, fallback to config.h defaults (Task 3, Step 2)
- [x] Runtime variables replace compile-time defines in map() (Task 1, Step 2)
- [x] `ESP.restart()` after save (Task 3, Step 4)
- [x] `hal_keyboard_read_raw_xy()` public wrapper for static `xpt2046_read_xy()` (Task 1, Step 3)
- [x] `hal_keyboard_set_touch_cal()` setter for runtime variables (Task 1, Step 3)
- [x] `hal_video_get_tft()` reused via extern (already exists — no hal_video.cpp change needed)
