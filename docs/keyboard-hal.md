# Keyboard HAL — CoCo2-CYD

## Overview

Translates touchscreen input into a CoCo 7×8 keyboard matrix that the emulated PIA0 can scan. On the CYD (standard ESP32), the primary input device is an XPT2046 touchscreen driving an on-screen keyboard (OSK).

**Files:**

| File | Purpose |
|---|---|
| `hal_keyboard.cpp` | XPT2046 bit-bang SPI, HID-to-matrix mapping, OSK dispatch, deferred release |
| `CoCo2Keyboard.h` | On-screen CoCo keyboard layout, touch hit detection, key event queue |
| `usb_kbd_host.h/.cpp` | USB HID driver — compiled but **disabled** (`USE_USB_HOST=0` on CYD) |
| `hal.cpp` | Calls `hal_keyboard_tick()` + `hal_keyboard_update_touch()` each frame |
| `hal.h` | Public API declarations |

---

## Architecture — Touch Pipeline

```
XPT2046 touch IC ──soft-SPI bit-bang──► hal_keyboard.cpp
  CS=33, MOSI=32, SCLK=25, MISO=39, IRQ=36

                            │ xpt2046_read_xy()  (3-sample average)
                            │ map raw ADC → screen coords (0-319, 0-239)
                            ▼
                 ┌───────────────────────┐
                 │  hal_keyboard_update  │  called from hal_process_input() each frame
                 │  _touch()             │
                 └──────────┬────────────┘
                            │
              ┌─────────────┴─────────────┐
              │                           │
    supervisor active?            supervisor inactive
              │                           │
              ▼                           ▼
  supervisor_on_touch(x,y)      CoCo2Keyboard::update(x,y,touched)
  (routes touch to active         (hit-tests OSK layout)
   OSD screen)                          │
                                 CoCoKeyEvent dequeued
                                        │
                          ┌─────────────┴────────────┐
                          │                          │
                    function key                  CoCo key
                  (row==7, ascii)              set_key(col,row)
                          │                   defer_release()
                 _osk_dispatch_fn()
                 F1/F2/F5 actions
```

**Key constraint:** XPT2046 uses software SPI bit-bang to avoid interfering with the SD card. The CYD's SD card is on VSPI (CS=5, MOSI=23, MISO=19, SCLK=18). Using hardware `SPIClass(VSPI)` for touch would overwrite the SD's GPIO mapping on first `begin()` call, breaking all subsequent SD reads.

---

## XPT2046 Touch Layer (`hal_keyboard.cpp`)

### Initialization (`xpt2046_init`)

Sets up GPIO pins for software SPI bit-bang:
- CS, SCLK, MOSI → OUTPUT (CS starts HIGH)
- MISO, IRQ → INPUT

Called lazily on first `hal_keyboard_update_touch()` after video init, since `hal_video_get_tft()` handle is required by the OSK.

### Touch Read (`xpt2046_read_xy`)

1. Check `IRQ` pin — returns `false` if no touch
2. Assert CS LOW
3. Read 3 samples each of X (cmd `0xD0`) and Y (cmd `0x90`) channels; average them
4. Deassert CS HIGH
5. Map raw ADC values to screen coordinates using calibration constants from `config.h`:

```cpp
tx = map(raw_y, TOUCH_X_MIN, TOUCH_X_MAX, 0, 319);  // raw_y → horizontal
ty = map(raw_x, TOUCH_Y_MIN, TOUCH_Y_MAX, 0, 239);  // raw_x → vertical
```

Note: raw `0xD0` (X+ channel) tracks the vertical screen axis; raw `0x90` (Y+ channel) tracks horizontal. Channel naming is electrical, not screen-coordinate.

### Calibration Constants (`config.h`)

```cpp
#define TOUCH_X_MIN   250   // raw_y at left edge
#define TOUCH_X_MAX  3830   // raw_y at right edge
#define TOUCH_Y_MIN   385   // raw_x at top edge
#define TOUCH_Y_MAX  3750   // raw_x at bottom edge
```

Adjust these if touch coordinates feel off. Collect corner samples via serial debug output.

---

## On-Screen Keyboard Layer (`CoCo2Keyboard.h`)

`CoCo2Keyboard` renders a full CoCo keyboard layout on the TFT and translates touch positions into `CoCoKeyEvent` structs.

### Key Event Routing (`hal_keyboard_update_touch` → `_osk_dispatch_fn`)

Events with `row == 7` are function keys dispatched by `_osk_dispatch_fn`:

| ASCII code | Action |
|---|---|
| `0xF1` | `supervisor_toggle()` |
| `0xF2` | `sv_disk_flush_all()` + `machine_reset()` |
| `0xF5` | `hal_video_toggle_fps_overlay()` |

All other events inject directly into the CoCo matrix via `set_key(col, row)` + `defer_release()`.

### Visibility and Frame Suppression

- While OSK is visible: `hal_keyboard_osk_visible()` returns `true`
- Main loop suppresses `hal_render_frame()` when OSK covers the screen
- `hal_keyboard_draw_overlay()` always draws the hotzone indicator on the TFT border
- When OSK hides: `hal_video_force_repaint()` is called to flush 3 frames after close

---

## USB HID Layer (`usb_kbd_host.cpp`) — Disabled on CYD

`USE_USB_HOST=0` in `config.h` disables the USB stack. The code is compiled but `hid_host_begin()` is a no-op in this configuration. It is retained for potential future use (e.g., if running on an ESP32-S3 board with USB OTG).

When enabled (`USE_USB_HOST=1`), the USB HID layer provides an 8-byte boot protocol keyboard driver on Core 0 via ESP-IDF USB Host Library. Events are queued to Core 1 via FreeRTOS queue and dispatched through the same `on_hid_key()` callback that the OSK uses.

---

## HID → CoCo Matrix Mapping

Both USB HID and OSK use the same `KeyMap` struct and `KEY_MAP[]` array:

**CoCo keyboard matrix** (ROM-verified, 7 rows × 8 columns):

```
        PB0    PB1    PB2    PB3    PB4    PB5    PB6    PB7
PA0:     @      A      B      C      D      E      F      G
PA1:     H      I      J      K      L      M      N      O
PA2:     P      Q      R      S      T      U      V      W
PA3:     X      Y      Z     UP    DOWN   LEFT  RIGHT   SPACE
PA4:     0      1      2      3      4      5      6      7
PA5:     8      9      :      ;      ,      -      .      /
PA6:   ENTER  CLEAR  BREAK  (n/a)  (n/a)  (n/a)  (n/a)  SHIFT
```

**Matrix encoding:** `key_matrix[col]` indexed by PB column (0-7). Each bit is a PA row (0-6). **Active LOW** — bit=0 means pressed.

**`KeyMap` struct:**
```cpp
struct KeyMap {
    uint8_t hid_usage;       // USB HID usage code (used by OSK too)
    uint8_t req_modifier;    // 0=any, else modifier must be held
    uint8_t col;             // PB column (0-7)
    uint8_t row;             // PA row (0-6)
    bool    needs_shift;     // Assert CoCo SHIFT
    bool    suppress_shift;  // Suppress CoCo SHIFT (PC shift produced this)
};
```

**Notable PC-to-CoCo translations:**

| PC Key | HID | CoCo Key | Notes |
|---|---|---|---|
| Backspace | `0x2A` | LEFT ARROW | PA3,PB5 |
| ESC | `0x29` | BREAK | PA6,PB2 |
| Pause | `0x48` | BREAK | PA6,PB2 |
| Insert | `0x49` | CLEAR | PA6,PB1 |
| Delete | `0x4C` | CLEAR | PA6,PB1 |
| Shift+2 | `0x1F`+MOD | @ | PA0,PB0, suppress CoCo SHIFT |
| Shift+; | `0x33`+MOD | : | PA5,PB2, suppress CoCo SHIFT |

---

## Deferred Key Release

**Problem:** CoCo BASIC's KEYIN routine scans the matrix over multiple frames with debouncing. A fast touch tap (press+release in same frame) is invisible to the emulated CPU.

**Solution:** `DeferredRelease` system holds keys in the matrix for `MIN_HOLD_FRAMES = 4` frames before releasing.

```cpp
struct DeferredRelease {
    uint8_t col, row;
    uint8_t frames_left;  // 0 = slot free
};
```

- **`defer_release(col, row)`** — queues a release (up to `MAX_DEFERRED = 8` slots)
- **`tick_deferred_releases()`** — called once per frame via `hal_keyboard_tick()`, decrements counters, releases when zero
- If a key is re-pressed before its deferred release fires, the counter is refreshed

---

## PIA Integration

The emulated PIA0 calls `hal_keyboard_scan(column)` during its port-A read:

```cpp
uint8_t hal_keyboard_scan(uint8_t column) {
    return key_matrix[column];  // Active LOW: 0xFF = no keys pressed
}
```

The PIA selects columns via PB0-PB7 output, then reads PA0-PA6 input. BASIC's KEYIN routine scans all 8 columns and computes `key_code = PA_row * 8 + PB_column`.

---

## Public API Summary

| Function | File | Called from | Purpose |
|---|---|---|---|
| `hal_keyboard_init()` | hal_keyboard.cpp | `hal_init()` | Reset matrix, init deferred releases |
| `hal_keyboard_tick()` | hal_keyboard.cpp | `hal_process_input()` | Tick deferred releases (once per frame) |
| `hal_keyboard_update_touch()` | hal_keyboard.cpp | `hal_process_input()` | Read XPT2046, update OSK, inject keys |
| `hal_keyboard_scan(col)` | hal_keyboard.cpp | PIA0 port-A read | Return matrix column (active LOW) |
| `hal_keyboard_set_machine(m)` | hal_keyboard.cpp | `setup()` | Wire Machine pointer for F2 hotkey |
| `hal_keyboard_osk_visible()` | hal_keyboard.cpp | `loop()` | True when OSK covers screen |
| `hal_keyboard_draw_overlay()` | hal_keyboard.cpp | `loop()` | Draw hotzone indicator on TFT border |
| `hal_keyboard_press(row,col)` | hal_keyboard.cpp | Test code | Software key injection |
| `hal_keyboard_release(row,col)` | hal_keyboard.cpp | Test code | Software key release |
| `hal_keyboard_release_all()` | hal_keyboard.cpp | Init / reset | Clear entire matrix to 0xFF |

---

## Hardware Requirements — CYD

| Signal | GPIO | Notes |
|---|---|---|
| Touch CS | 33 | XPT2046 chip select |
| Touch MOSI | 32 | Software SPI data out |
| Touch SCLK | 25 | Software SPI clock |
| Touch MISO | 39 | Software SPI data in |
| Touch IRQ | 36 | Active LOW when screen touched |

**Important:** GPIO36 (IRQ) is also the only available ADC input on CYD. This conflicts with joystick ADC use — joystick is therefore disabled (`JOYSTICK_ENABLED=0`).

---

## Troubleshooting

### Touch not registering

1. Check `PIN_TOUCH_IRQ` (GPIO36) is LOW when touching screen
2. Verify touch calibration constants — add serial debug prints to `xpt2046_read_xy()` to see raw values
3. Check soft-SPI GPIO assignments match physical XPT2046 wiring (see Pin Reference in CLAUDE.md)

### Keys not registering in BASIC

1. Verify `hal_keyboard_scan()` is being called by PIA0 (check PIA port-B output selects columns)
2. Check deferred release timing — if `MIN_HOLD_FRAMES` is too low, KEYIN may miss keys (current: 4 frames)

### Keys stuck after touch

1. Check `deferred_releases[]` — if all 8 slots full, releases happen immediately
2. Ensure `hal_keyboard_release_all()` is called on reset

### Adding a new OSK function key

1. Add ASCII code `0xFn` in `CoCo2Keyboard.h` for the button
2. Add `case 0xFn:` in `_osk_dispatch_fn()` in `hal_keyboard.cpp`

### Adding a new CoCo key mapping (HID path)

1. Find the HID usage code (see USB HID Usage Tables spec)
2. Find the CoCo matrix position (row=PA, col=PB) from the matrix table above
3. Add a `KeyMap` entry to `KEY_MAP[]` array in `hal_keyboard.cpp`
4. Set `needs_shift`/`suppress_shift` as needed for modifier translation
