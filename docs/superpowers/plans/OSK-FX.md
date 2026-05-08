# OSK-FX: Eliminate F1/F2/F5/KB Button Flicker Implementation Plan

> Steps use checkbox (`- [ ]`) syntax for tracking progress.

**Goal:** Stop F1/F2/F5/KB buttons from flickering during active emulation by replacing unconditional per-frame TFT redraws with a dirty-flag guard.

**Architecture:** Add `_hotzoneDrawn` boolean member to `CoCo2Keyboard`. `drawHotZoneIndicator()` skips all 12 SPI draw calls when the flag is set. `invalidateHotzone()` resets it. Three callers must reset it: `hide()` (does `fillScreen` → clears top border), `_drawKeyboard()` (does `fillScreen` → clears top border before rebuilding OSK), and the end of `touch_cal_run()` (multiple `fillScreen` calls clobber the entire display). No other caller is needed — the sprite push covers only y≥24, never touching the button row at y=4–21.

**Tech Stack:** Arduino/ESP32, TFT_eSPI, C++

---

## Background

Root cause: `hal_keyboard_draw_overlay()` → `drawHotZoneIndicator()` runs every loop iteration. It calls `_drawTopButton()` four times, each issuing three sequential SPI writes to the live TFT (`fillRoundRect`, `drawRoundRect`, `drawString`). The user sees each intermediate paint state — background flash, then border, then label — 60× per second. That is the blink.

The buttons never change appearance (no pressed/unpressed visual state). They only need to be repainted after something clears the top border area (y=0–23 px).

Screen-clearing events that erase the top border:
| Code location | Call | Effect |
|---|---|---|
| `CoCo2Keyboard.h:366` inside `hide()` | `fillScreen(TFT_BLACK)` | Erases entire display |
| `CoCo2Keyboard.h:480` inside `_drawKeyboard()` | `fillScreen(COCO_KB_BODY)` | Erases entire display before drawing OSK keys |
| `hal_touch_cal.cpp:111,125,136,172` | `fillScreen(TFT_BLACK)` | Erases entire display during calibration UI |

The sprite push (`hal_video.cpp:319,334`) goes to `SPR_X, SPR_Y` = (32, 24) and covers only y=24–215 — it never touches y=0–23 and is not a cause.

---

## File Map

| File | Change |
|---|---|
| `CoCo2-CYD/src/hal/CoCo2Keyboard.h` | Add `_hotzoneDrawn` member; guard `drawHotZoneIndicator()`; add `invalidateHotzone()`; reset in `hide()` and `_drawKeyboard()` |
| `CoCo2-CYD/src/hal/hal_keyboard.h` | Declare `hal_keyboard_invalidate_hotzone()` |
| `CoCo2-CYD/src/hal/hal_keyboard.cpp` | Implement `hal_keyboard_invalidate_hotzone()` |
| `CoCo2-CYD/src/hal/hal_touch_cal.cpp` | Call `hal_keyboard_invalidate_hotzone()` at end of `touch_cal_run()` |

---

## Task 1: Add Dirty Flag and Guard to CoCo2Keyboard

**Files:**
- Modify: `CoCo2-CYD/src/hal/CoCo2Keyboard.h`

### Step 1.1 — Find the private member block in CoCo2Keyboard

The class has private members near the bottom. Look for declarations like `TFT_eSPI* _tft` or `bool _visible`. The `_hotzoneDrawn` flag goes there.

- [ ] Run to locate the private member block:

```bash
grep -n "_tft\|_visible\|bool _\|uint8_t _\|int16_t _" CoCo2-CYD/src/hal/CoCo2Keyboard.h | tail -20
```

Note the line number of the private member block — you'll add `_hotzoneDrawn` there.

### Step 1.2 — Add `_hotzoneDrawn` to the private member block

- [ ] Add the member alongside the other private booleans (e.g. after `bool _visible` or similar). The exact surrounding context may differ — match what you find:

```cpp
    bool _hotzoneDrawn = false;
```

### Step 1.3 — Add `invalidateHotzone()` as a public method

- [ ] Place this method in the public section, near `drawHotZoneIndicator()` (around line 405):

```cpp
    void invalidateHotzone() { _hotzoneDrawn = false; }
```

### Step 1.4 — Guard `drawHotZoneIndicator()` with the dirty flag

`drawHotZoneIndicator()` currently looks like:

```cpp
    void drawHotZoneIndicator() {
        _drawTopButton(COCO_KB_BTN_F1_X, "F1");
        _drawTopButton(COCO_KB_BTN_F2_X, "F2");
        _drawTopButton(COCO_KB_BTN_F5_X, "F5");
        _drawTopButton(COCO_KB_BTN_KB_X, "KB");
    }
```

- [ ] Replace it with:

```cpp
    void drawHotZoneIndicator() {
        if (_hotzoneDrawn) return;
        _drawTopButton(COCO_KB_BTN_F1_X, "F1");
        _drawTopButton(COCO_KB_BTN_F2_X, "F2");
        _drawTopButton(COCO_KB_BTN_F5_X, "F5");
        _drawTopButton(COCO_KB_BTN_KB_X, "KB");
        _hotzoneDrawn = true;
    }
```

### Step 1.5 — Invalidate in `hide()` so next overlay call repaints

`hide()` (line ~363) does `fillScreen(TFT_BLACK)` which erases the buttons. The main loop's `drawHotZoneIndicator()` must repaint them on the next frame.

- [ ] Add `invalidateHotzone();` as the first line of `hide()`:

```cpp
    void hide() {
        invalidateHotzone();          // ← add this line
        _tft->fillScreen(TFT_BLACK);
        // ... rest of hide body unchanged
    }
```

### Step 1.6 — Invalidate inside `_drawKeyboard()` before `drawHotZoneIndicator()`

`_drawKeyboard()` starts with `fillScreen(COCO_KB_BODY)` which erases the top border. It then calls `drawHotZoneIndicator()` at line ~520. Without a reset, the guard would skip the redraw because `_hotzoneDrawn` is still `true` from the previous frame.

- [ ] Add `invalidateHotzone();` on the line immediately before the `drawHotZoneIndicator()` call inside `_drawKeyboard()`:

```cpp
        // ... (existing OSK key drawing code) ...
        invalidateHotzone();           // ← add this line
        drawHotZoneIndicator();
```

### Step 1.7 — Compile check

- [ ] Run from the repo root:

```bash
arduino-cli compile --fqbn esp32:esp32:esp32 CoCo2-CYD/
```

Expected: zero errors, zero warnings related to `_hotzoneDrawn` or `invalidateHotzone`. Fix any before continuing.

- [ ] Commit:

```bash
git -C CoCo2-CYD add src/hal/CoCo2Keyboard.h
git -C CoCo2-CYD commit -m "fix: add hotzone dirty flag to stop F1/F2/F5/KB button flicker"
```

---

## Task 2: Expose Invalidate to C HAL Layer

Touch calibration (`hal_touch_cal.cpp`) does multiple `fillScreen` calls and needs to signal that the hotzone must be repainted, but it cannot reach `s_osk` directly (that's a private static in `hal_keyboard.cpp`). Add a thin HAL function.

**Files:**
- Modify: `CoCo2-CYD/src/hal/hal_keyboard.h`
- Modify: `CoCo2-CYD/src/hal/hal_keyboard.cpp`
- Modify: `CoCo2-CYD/src/hal/hal_touch_cal.cpp`

### Step 2.1 — Declare the new HAL function in `hal_keyboard.h`

- [ ] Find the existing `hal_keyboard_*` declarations in `hal_keyboard.h` and add:

```cpp
void hal_keyboard_invalidate_hotzone(void);
```

### Step 2.2 — Implement in `hal_keyboard.cpp`

`s_osk` is the static `CoCo2Keyboard*` pointer already used in the file.

- [ ] Add near the other `hal_keyboard_*` function implementations (any location after `s_osk` is declared):

```cpp
void hal_keyboard_invalidate_hotzone(void) {
#if TOUCH_KB_ENABLED
    if (s_osk_initialized && s_osk) s_osk->invalidateHotzone();
#endif
}
```

### Step 2.3 — Call it at the end of `touch_cal_run()` in `hal_touch_cal.cpp`

`touch_cal_run()` ends by restoring the display (final `fillScreen(TFT_BLACK)` at line ~172) and returning. The hotzone buttons are now gone. Add the call there.

- [ ] First confirm `hal_keyboard.h` is already included in `hal_touch_cal.cpp`:

```bash
grep "hal_keyboard" CoCo2-CYD/src/hal/hal_touch_cal.cpp
```

If the include is missing, add it at the top of `hal_touch_cal.cpp`:

```cpp
#include "hal_keyboard.h"
```

- [ ] At the very end of `touch_cal_run()`, just before the closing `}`, add:

```cpp
    hal_keyboard_invalidate_hotzone();
```

### Step 2.4 — Compile check

- [ ] Run:

```bash
arduino-cli compile --fqbn esp32:esp32:esp32 CoCo2-CYD/
```

Expected: zero errors, zero warnings. Fix any before continuing.

- [ ] Commit:

```bash
git -C CoCo2-CYD add src/hal/hal_keyboard.h src/hal/hal_keyboard.cpp src/hal/hal_touch_cal.cpp
git -C CoCo2-CYD commit -m "fix: invalidate hotzone after touch calibration clears screen"
```

---

## Task 3: Upload and Verify

### Step 3.1 — Upload to device

- [ ] Run:

```bash
arduino-cli compile --fqbn esp32:esp32:esp32 CoCo2-CYD/ && \
arduino-cli upload --fqbn esp32:esp32:esp32 --port /dev/ttyUSB0 CoCo2-CYD/
```

### Step 3.2 — Observe F1/F2/F5/KB buttons during BASIC ROM activity

Boot the device and let it reach the BASIC `OK` prompt. Type something to generate screen activity.

- [ ] Confirm: F1, F2, F5, KB buttons are **stable** — no flicker at all during active emulation.

### Step 3.3 — Exercise all screen-clearing paths

Verify the buttons reappear correctly after each event that clears the screen:

- [ ] **OSK show/hide:** Tap KB to open the on-screen keyboard, then close it. Buttons should be present and steady after close.
- [ ] **Touch calibration:** Hold BOOT for 5 seconds to trigger calibration. Complete or time out. Buttons should reappear when emulation resumes.
- [ ] **OSD (F1):** Open and close the supervisor OSD. Buttons should remain visible throughout (OSD starts at y=24, does not touch the top border).

---

## Self-Review Checklist

- Spec coverage: dirty flag ✓ | `drawHotZoneIndicator()` guard ✓ | `hide()` reset ✓ | `_drawKeyboard()` reset ✓ | calibration reset ✓ | compile verified ✓ | upload + visual confirm ✓
- No placeholders: all code is complete
- Type consistency: `invalidateHotzone()` named identically in declaration, implementation, and all call sites
- The sprite push path (y=24–215) is confirmed non-overlapping with the button row (y=4–21) — no change needed there
