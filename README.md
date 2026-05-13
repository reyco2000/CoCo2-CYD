# CoCo2-CYD — TRS-80 Color Computer 2 Emulator for ESP CYD

A full **TRS-80 Color Computer 2 (CoCo 2)** emulator running on the **ESP CYD** (Cheap Yellow Display — ESP32-2432S028R). Ported from the [XRoar](http://www.6809.org.uk/xroar/) emulator by Ciaran Anscomb.

**Beta-1 — 2026**

---

## Features

- **Complete MC6809 CPU** — all opcodes, accurate cycle counts, NMI/FIRQ/IRQ
- **MC6847 VDG** — text mode and all 8 semigraphics/graphics modes
- **Dual MC6821 PIA** — keyboard matrix, 60 Hz vsync IRQ, audio control
- **SAM6883** address multiplexer — memory mapping and VDG counter
- **WD1793 floppy disk controller** — `.DSK` (JVC) and `.VDK` formats via SD card
- **On-Screen Display (OSD)** — disk browser, disk manager, machine reset
- **Touchscreen keyboard** — full CoCo key layout via XPT2046 touch (built-in on CYD)
- **Touch calibration** — 6-point calibration triggered by holding BOOT 5 s at power-on; saved to NVS
- **Audio output** — 6-bit DAC + single-bit via GPIO26 (built-in buzzer/speaker)
- **64 KB RAM** — full CoCo 2 configuration
- **~25–27 FPS** active emulation; ~64 FPS text mode (VRAM shadow skips static frames)

---

## Target Hardware — ESP CYD (ESP32-2432S028R)

The **Cheap Yellow Display** is a self-contained ESP32 development board with a built-in 320×240 ILI9341 TFT, XPT2046 resistive touchscreen, SD card slot, and buzzer. No external wiring required.

| Component        | Built-in on CYD |
|------------------|-----------------|
| MCU              | ESP32 (dual-core, 240 MHz, no PSRAM) |
| Display          | ILI9341 320×240 TFT (VSPI) |
| Touchscreen      | XPT2046 resistive (software SPI) |
| SD card          | Micro SD slot (separate SPI) |
| Audio            | Passive buzzer on GPIO26 (ESP32 DAC) |
| USB              | USB-UART only (no USB OTG) |

### Pin Reference

| Function       | GPIO | Notes |
|----------------|------|-------|
| TFT CS         | 15   | ILI9341 SPI display |
| TFT DC         | 2    | Data/Command |
| TFT MOSI       | 13   | SPI data |
| TFT SCLK       | 14   | SPI clock |
| TFT MISO       | 12   | SPI read |
| TFT Backlight  | 21   | Active HIGH |
| SD CS          | 5    | Onboard SD slot (separate SPI bus) |
| SD MOSI        | 23   | |
| SD MISO        | 19   | |
| SD SCLK        | 18   | |
| Audio          | 26   | ESP32 built-in DAC |
| Touch CS       | 33   | XPT2046 (software SPI) |
| Touch MOSI     | 32   | |
| Touch MISO     | 39   | |
| Touch SCLK     | 25   | |
| Touch IRQ      | 36   | |

> The TFT and SD card use **separate SPI buses** — no bus contention during emulation.

---

## Build Requirements

### Tool — arduino-cli

All compile and upload operations use **arduino-cli**. Install it from [arduino.cc/en/software/cli](https://arduino.cc/en/software/cli).

Verify installation:

```bash
arduino-cli version
```

### Board package

Install the ESP32 board package (tested with **3.3.3**):

```bash
arduino-cli core update-index
arduino-cli core install esp32:esp32
```

### Required Libraries

Install via arduino-cli:

```bash
arduino-cli lib install "TFT_eSPI"
arduino-cli lib install "SD"
```

> **TFT_eSPI** requires configuration — see [TFT_eSPI Setup](#tft_espi-setup) below.

---

## TFT_eSPI Setup

The CYD uses the **ILI9341_2** driver with specific pin assignments. Copy the provided template over the default `User_Setup.h`:

```bash
cp templates_for_TFT_eSP/User_Setup.h.ST7789_240x320.h \
   ~/Arduino/libraries/TFT_eSPI/User_Setup.h
```

> Use the `User_Setup.h.ST7789_240x320.h` template — it is configured for the CYD ILI9341 display with the correct pins and `TFT_INVERSION_ON`.

If you prefer to edit `User_Setup.h` manually, the key settings are:

```cpp
#define ILI9341_2_DRIVER
#define TFT_WIDTH  240
#define TFT_HEIGHT 320
#define TFT_CS   15
#define TFT_DC    2
#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_MISO 12
#define TFT_BL   21
#define TFT_INVERSION_ON
#define SPI_FREQUENCY  40000000
```

---

## Creating ROM Header Files

The emulator embeds CoCo ROMs directly into flash as C arrays (`USE_EMBEDDED_ROMS=1` in `config.h`). You must generate these headers from your own ROM files before compiling.

**You need these three ROM files** (obtain legally from a real CoCo 2 or a licensed source):

| ROM file        | Size  | Description           |
|-----------------|-------|-----------------------|
| `bas13.rom`     | 8 KB  | Color BASIC 1.3       |
| `extbas11.rom`  | 8 KB  | Extended BASIC 1.1    |
| `disk11.rom`    | 8 KB  | Disk BASIC 1.1        |

### Generate headers with rom2header.py

From the **project root** (`CoCo2-CYD/` parent directory):

```bash
python3 tools/rom2header.py roms/bas13.rom     bas13    > CoCo2-CYD/src/roms/bas13_rom.h
python3 tools/rom2header.py roms/extbas11.rom  extbas11 > CoCo2-CYD/src/roms/extbas11_rom.h
python3 tools/rom2header.py roms/disk11.rom    disk11   > CoCo2-CYD/src/roms/disk11_rom.h
```

Each command reads the binary ROM file and writes a `PROGMEM` C array header. The script also prints the filename and byte count as a comment at the top of each header.

Verify the files were created:

```bash
ls -lh CoCo2-CYD/src/roms/*.h
```

You should see `bas13_rom.h`, `extbas11_rom.h`, and `disk11_rom.h`, each around **16 KB** (8 KB binary → hex array text).

> ROM files are CRC-32 validated at boot. If the CRC does not match a known-good value, the emulator will print a warning to Serial but will still attempt to run.

---

## Compile & Upload

All commands are run from the **`CoCo2-CYD/` parent directory** (one level above the `.ino` file).

### Compile only

```bash
arduino-cli compile --fqbn esp32:esp32:esp32 CoCo2-CYD/
```

### Upload only

```bash
arduino-cli upload --fqbn esp32:esp32:esp32 --port /dev/ttyUSB0 CoCo2-CYD/
```

### Compile + Upload in one step

```bash
arduino-cli compile --fqbn esp32:esp32:esp32 CoCo2-CYD/ && \
arduino-cli upload --fqbn esp32:esp32:esp32 --port /dev/ttyUSB0 CoCo2-CYD/
```

> Replace `/dev/ttyUSB0` with your actual port. On macOS it may be `/dev/cu.usbserial-*`. Run `arduino-cli board list` to find it.

### Monitor serial output

```bash
arduino-cli monitor --port /dev/ttyUSB0 --config baudrate=115200
```

Startup diagnostics, ROM CRC results, and debug output appear here.

---

## SD Card Setup

Format a MicroSD card as **FAT32**. Disk images can go anywhere on the card; the OSD file browser lets you navigate to them.

```
/
└── (your .DSK and .VDK disk image files — any location)
```

ROMs are **not** needed on the SD card when `USE_EMBEDDED_ROMS=1` (the default).

### Supported disk formats

| Format | Extension | Notes |
|--------|-----------|-------|
| JVC    | `.DSK`    | Fully supported |
| VDK    | `.VDK`    | 12-byte header, fully supported |
| DMK    | `.DMK`    | Recognized but not mountable |

---

## Touchscreen Keyboard Controls

The CYD touchscreen acts as the CoCo keyboard. Function keys on the OSK:

| Touch key | Action |
|-----------|--------|
| F1        | Toggle OSD supervisor menu |
| F2        | Machine reset (prompts for confirmation) |
| F5        | Toggle FPS overlay |

### Touch Calibration

If the touchscreen feels mis-aligned, the emulator includes a built-in 6-point calibration procedure:

1. Hold the **BOOT** button for **5 seconds** while the device is powered on (before the BASIC prompt appears).
2. Tap each of the 6 crosshair targets displayed on screen in order.
3. Calibration data is written to **NVS** and loaded automatically on every subsequent boot.

To force fixed calibration values from `config.h` instead of NVS, set `TOUCH_CAL_OVERRIDE 1` in `config.h` and adjust the `TOUCH_X_MIN`, `TOUCH_X_MAX`, `TOUCH_Y_MIN`, `TOUCH_Y_MAX` constants to match your panel.

---

## On-Screen Display (OSD)

Press **F1** on the touchscreen to open the supervisor overlay. Emulation pauses while the OSD is active.

- **Mount Disk** — browse the SD card and mount a `.DSK` or `.VDK` image to drives 0–3
- **Disk Manager** — view mounted drives and eject disks
- **Reset Machine** — warm or cold reset with confirmation dialog
- **About** — version info and free heap

Touch the **F1** hotzone again (top-left of the OSK) or tap **Back** inside the OSD to return to emulation.

---

## Key config.h Settings

| Setting               | Default | Description |
|-----------------------|---------|-------------|
| `USE_EMBEDDED_ROMS`   | 1       | 1 = ROMs baked into flash (requires `.h` headers); 0 = load from SD `/roms/` |
| `DISPLAY_TYPE`        | 0       | 0 = ILI9341, 1 = ST7789, 3 = ST7796 |
| `DISPLAY_SCALE_MODE`  | 0       | 0 = 1:1 centered, 1 = scaled fill, 2 = zoom |
| `AUDIO_PITCH_TRIM`    | -6      | Pitch correction offset (–6 ≈ 1% accuracy vs XRoar) |
| `TOUCH_KB_ENABLED`    | 1       | XPT2046 touchscreen OSK |
| `TOUCH_CAL_OVERRIDE`  | 0       | 1 = use fixed `TOUCH_X/Y_MIN/MAX` from `config.h` instead of NVS calibration |
| `JOYSTICK_ENABLED`    | 0       | Disabled — GPIO36 conflicts with XPT2046 IRQ |
| `USE_USB_HOST`        | 0       | Disabled — standard ESP32 has no USB OTG |
| `USE_PSRAM`           | 0       | Disabled — CYD has no PSRAM |

---

## Architecture Overview

```
CoCo2-CYD.ino  — setup() + loop()
     |
     +-- HAL Layer (src/hal/)
     |     hal_video.cpp    — ILI9341 TFT via TFT_eSPI, VRAM shadow compare
     |     hal_audio.cpp    — ESP32 DAC, pitch-corrected scanline buffer, 22050 Hz ISR
     |     hal_keyboard.cpp — XPT2046 touch → CoCo key matrix
     |     hal_storage.cpp  — SD card, ROM loading
     |
     +-- Emulation Core (src/core/)
     |     mc6809.cpp       — Full Motorola 6809 CPU
     |     mc6821.cpp       — Dual PIA (keyboard, audio, vsync IRQ)
     |     mc6847.cpp       — VDG (text + graphics modes)
     |     sam6883.cpp      — Address mux + VDG display counter
     |     machine.cpp      — System wiring, memory map, frame loop
     |
     +-- Supervisor / OSD (src/supervisor/)
           supervisor.cpp   — State machine, NVS persistence
           sv_filebrowser   — SD card directory browser
           sv_disk.cpp      — WD1793 FDC + direct SD sector I/O
           sv_render.cpp    — Blue-theme OSD renderer
```

### Dual-core split

CPU emulation runs on **Core 0** while the display SPI push runs on **Core 1** in
parallel. The CPU task captures a per-frame render snapshot (palette indices +
VRAM region) and hands it off via two binary semaphores (`frame_ready` /
`render_done`). The renderer releases `render_done` as soon as it has filled
the sprite buffer from the snapshot — *before* the slow `pushSprite` — so the
next frame's CPU work overlaps with the SPI transfer.

```
Core 0 (cpu_emu task, priority 2)        Core 1 (Arduino loopTask)
───────────────────────────────────      ──────────────────────────────────
take render_done                         hal_process_input()
machine_run_frame_cpu_only(coco)         supervisor_update_and_render()
  ├─ 262 × machine_run_scanline          take frame_ready ─┐
  │   └─ pack 4 bpp indices into        │                  │
  │      pixel snapshot chunks          │  fill sprite     │ (CPU runs
  ├─ audio capture/commit               │  from snapshot   │  next frame
  └─ memcpy VRAM → snapshot chunks      │   (3 ms)         │  in parallel)
give frame_ready ───────────────────────►give render_done ─┘
vTaskDelay(1)                            pushSprite (~20 ms over SPI)
                                         hal_keyboard_draw_overlay()
```

Performance: text mode ~70 FPS (renderer skips most pushes via shadow
compare), graphics mode ~34 FPS (was ~25 FPS before the split).

### Main loop order

```
Core 1 loop():
  1. hal_process_input()             — XPT2046 touch + deferred key releases
  2. supervisor_update_and_render()  — OSD (skips emulation while active)
  3. take frame_ready                — wait for snapshot from Core 0
  4. hal_video_snapshot_fill_sprite()— shadow compare + 4 bpp unpack into sprite
  5. give render_done                — Core 0 may now overwrite snapshot
  6. hal_video_push_sprite_only()    — pushSprite over SPI (slow; CPU runs in parallel)
  7. hal_keyboard_draw_overlay()     — OSK hotzone indicator on TFT border

Core 0 cpu_emu task:
  1. take render_done                — block until renderer freed the snapshot
  2. machine_run_frame_cpu_only()    — 262 scanlines: CPU + VDG + SAM + FDC + audio
                                       (writes palette indices into snapshot
                                        instead of the sprite framebuffer)
  3. give frame_ready                — hand snapshot to Core 1
  4. vTaskDelay(1)                   — feed IDLE0 / task watchdog
```

---

## Known Limitations

- No TFT MISO wired on CYD — brief black flash when closing OSD (repaints next frame)
- Joystick disabled (`JOYSTICK_ENABLED=0`) — GPIO36 conflict with XPT2046 IRQ
- Max 128 file entries visible in the SD file browser
- DMK disk format not mountable
- Machine type fixed to CoCo 2 at compile time (`MACHINE_TYPE 3` in `config.h`)
- No physical keyboard — touchscreen OSK only (no USB OTG on standard ESP32)

---

## Project Structure

```
CoCo2-CYD/
  CoCo2-CYD.ino         Main sketch (setup + loop)
  config.h              Hardware config, pins, timing

  src/core/
    machine.cpp/.h      System integration, memory map, frame loop, Core-0 handshake
    render_snapshot.h   Dual-core snapshot struct (4 bpp pixel chunks + VRAM chunks)
    mc6809.cpp/.h       MC6809 CPU (all opcodes, cycle-accurate)
    mc6821.cpp/.h       MC6821 PIA (2 instances)
    mc6847.cpp/.h       MC6847 VDG (text + 8 graphics modes)
    sam6883.cpp/.h      SAM6883 (address mux + VDG counter)

  src/hal/
    hal_video.cpp       TFT_eSPI display, sprite, VRAM shadow compare, snapshot fill/push
    hal_audio.cpp       ESP32 DAC audio, scanline buffer, ISR
    hal_keyboard.cpp    CoCo matrix mapping, XPT2046 touch, deferred release
    CoCo2Keyboard.h     On-screen keyboard layout + touch dispatch
    hal_storage.cpp     SD card init + ROM loading

  src/supervisor/
    supervisor.cpp/.h   OSD lifecycle, state machine, NVS persistence
    sv_menu.cpp/.h      Main menu items and key handling
    sv_filebrowser.cpp/.h  SD card directory browser
    sv_disk.cpp/.h      WD1793 FDC + direct SD sector I/O
    sv_render.cpp/.h    OSD rendering engine (blue theme)
    sv_debug.cpp/.h     Memory dump to Serial (S-Record / Intel HEX)

  src/roms/
    rom_loader.cpp/.h   CRC-32 validation; SD fallback loader
    bas13_rom.h         Color BASIC 1.3 (generated — not in repo)
    extbas11_rom.h      Extended BASIC 1.1 (generated — not in repo)
    disk11_rom.h        Disk BASIC 1.1 (generated — not in repo)

  templates_for_TFT_eSP/
    User_Setup.h.ST7789_240x320.h   TFT_eSPI config for CYD ILI9341

tools/
  rom2header.py         Converts .rom binary to PROGMEM C header

roms/
  bas13.rom             ROM binaries (not redistributed)
  extbas11.rom
  disk11.rom
```

---

## Credits

- **Reinaldo Torres / CoCo Byte Club** — ESP32 CYD port
- **Ciaran Anscomb** — [XRoar](http://www.6809.org.uk/xroar/) (original emulator source)
- **Claude Code (Anthropic)** — co-development of the ESP32 port
- **Bodmer** — [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI) display library

## License

Licensed under the **GNU General Public License v3.0 or later** — see [LICENSE](LICENSE) for the full text.

This project is derived from [XRoar](https://www.6809.org.uk/xroar/) by Ciaran Anscomb.
Current ESP32 CYD port by Reinaldo Torres / CoCoByte Club —
<https://github.com/reyco2000/CoCo2-CYD>
## Contact
Reinaldo Torres — chipshift@cocobyte.co  
GitHub: [@reyco2000](https://github.com/reyco2000)
