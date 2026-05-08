/*
 * =============================================================
 *   CoCo2-CYD Beta-1 March 2026 - CoCo 2 Emulator for ESP32 CYD
 *   (C) 2026 Reinaldo Torres / CoCo Byte Club
 *   https://github.com/reyco2000/CoCo2-CYD
 *   Based on XRoar Emulator by Ciaran Anscomb
 *   CO-developed with Claude Code (Anthropic)
 *   MIT License
 * =============================================================
 *  File   : config.h
 *  Module : Hardware configuration — pin assignments, compile-time options, and build constants
 *  Target : ESP CYD (Cheap Yellow Display — ESP32-2432S028R)
 * =============================================================
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ============================================================
// Build options
// ============================================================

// Machine type: 0 = Dragon 32, 1 = Dragon 64, 2 = CoCo 1, 3 = CoCo 2
#define MACHINE_TYPE            3

// CPU variant: 0 = MC6809, 1 = HD6309
#define CPU_VARIANT             0

// RAM size in KB (16, 32, or 64)
#define RAM_SIZE_KB             64

// Enable debug output on Serial
#define DEBUG_ENABLED           1

// Target frames per second (NTSC=60, PAL=50)
#define TARGET_FPS              60

// ============================================================
// Display configuration
// ============================================================

// Display type: 0 = ILI9341 SPI (320x240)  <-- CYD uses ILI9341
//               1 = ST7789 SPI  (320x240)
//               2 = Composite out
//               3 = ST7796 SPI  (480x320)
#define DISPLAY_TYPE            0

// Display resolution derived from display type
#if DISPLAY_TYPE == 3
  #define DISPLAY_WIDTH         480
  #define DISPLAY_HEIGHT        320
#else
  #define DISPLAY_WIDTH         320
  #define DISPLAY_HEIGHT        240
#endif

// Display scale mode:
//   0 = 1:1 centered (256x192 native, black borders)
//   1 = Scaled fill  (nearest-neighbor stretch to fill display)
//   2 = Zoom centered (integer/fractional zoom, centered with black borders)
#define DISPLAY_SCALE_MODE      0

// Border size in pixels (applies to scale mode 1 only)
// Set to 0 for no border, 10-15 for a CoCo-style black frame
#define DISPLAY_BORDER          12

// Zoom factor x10 (applies to scale mode 2 only)
// Examples: 10 = 1.0x (same as mode 0), 15 = 1.5x, 20 = 2.0x
// Image is clipped if it exceeds display size
#define DISPLAY_ZOOM_X10        17

// SPI display pins — ESP CYD (ESP32-2432S028R) ILI9341
#define PIN_TFT_CS              15
#define PIN_TFT_DC              2
#define PIN_TFT_RST             -1   // Not wired on CYD
#define PIN_TFT_MOSI            13
#define PIN_TFT_SCLK            14
#define PIN_TFT_MISO            12
#define PIN_TFT_BL              21   // Backlight active HIGH

// SPI speed for display (Hz)
#define TFT_SPI_FREQ            40000000

// ============================================================
// Audio configuration
// ============================================================

// Audio output method: 0 = DAC (GPIO17), 1 = I2S, 2 = disabled
#define AUDIO_OUTPUT            0

// Audio sample rate
#define AUDIO_SAMPLE_RATE       22050

// Audio buffer size (samples)
#define AUDIO_BUFFER_SIZE       512

// I2S pins (if AUDIO_OUTPUT == 1)
//#define PIN_I2S_BCLK            26
//#define PIN_I2S_LRCLK           25
//#define PIN_I2S_DOUT            22

// DAC pin (if AUDIO_OUTPUT == 0, ESP32 DAC on GPIO25 or GPIO26)
// CYD has a built-in buzzer/speaker on GPIO26
#define PIN_DAC_OUT             26

// ============================================================
// Input configuration
// ============================================================

// USB keyboard
//#define PIN_PS2_DATA            16
//#define PIN_PS2_CLK             17

// Joystick analog pins — TBD for CYD (no built-in joystick)
#define PIN_JOY0_X              36
#define PIN_JOY0_Y              -1
#define PIN_JOY0_BTN            -1

#define PIN_JOY1_X              -1
#define PIN_JOY1_Y              -1
#define PIN_JOY1_BTN            -1

// ── On-Screen Touch Keyboard (XPT2046 resistive touch) ───────────────────
// CYD (ESP32-2432S028R) touch SPI pins are dedicated — NOT shared with TFT.
// TFT uses VSPI (GPIO13/14/12/15); touch uses its own bus via HSPI.
#define TOUCH_KB_ENABLED        1

#define PIN_TOUCH_CS            33
#define PIN_TOUCH_MOSI          32
#define PIN_TOUCH_SCLK          25
#define PIN_TOUCH_MISO          39
#define PIN_TOUCH_IRQ           36

// Raw ADC calibration — adjust TOUCH_X/Y_MIN/MAX if touch coordinates are off
#define TOUCH_X_MIN             250
#define TOUCH_X_MAX             3830
#define TOUCH_Y_MIN             385
#define TOUCH_Y_MAX             3750
#define TOUCH_ROTATION          1    // landscape; matches tft.setRotation(1)

// Set to 1 to ignore NVS-saved calibration and always use the values above.
// Useful when the NVS calibration is bad and the 6-point routine is unavailable.
#define TOUCH_CAL_OVERRIDE      0

// ============================================================
// Storage configuration
// ============================================================

// Storage type: 0 = SD card (SPI), 1 = SPIFFS, 2 = LittleFS
#define STORAGE_TYPE            0

// SD card SPI pins — ESP CYD (ESP32-2432S028R)
#define PIN_SD_CS               5
#define PIN_SD_MOSI             23
#define PIN_SD_MISO             19
#define PIN_SD_SCLK             18

// ROM file paths (on SD or SPIFFS)
#define ROM_BASE_PATH           "/roms"
#define ROM_BASIC_FILE          "bas13.rom"
#define ROM_EXT_BASIC_FILE      "extbas11.rom"
#define ROM_DISK_FILE           "disk11.rom"

// ============================================================
// Memory layout
// ============================================================

// Use PSRAM for emulated RAM if available
// Standard CYD (ESP32-2432S028R) has no PSRAM — keep 0
#define USE_PSRAM               0

// CYD Phase 1 port flags
#define USE_EMBEDDED_ROMS       1   // ROMs baked into flash, not loaded from SD
#define USE_USB_HOST            0   // No USB OTG on standard ESP32 CYD
#define DISK_ENABLED            1   // Direct per-sector SD access, no PSRAM cache
#define JOYSTICK_ENABLED        0   // Defer to Phase 2; floating ADC corrupts comparator

// CoCo memory map sizes
#define COCO_RAM_SIZE           (RAM_SIZE_KB * 1024)
#define COCO_ROM_SIZE           (32 * 1024)      // Max total ROM space

// Video RAM is part of main RAM, mapped by SAM
// MC6847 active display: 6144 bytes (text) or 6144 bytes (graphics)
#define VRAM_SIZE               6144

// ============================================================
// Timing
// ============================================================

// MC6809 clock: 0.895 MHz (NTSC) or 0.8856 MHz (PAL)
#define CPU_CLOCK_HZ            895000

// Cycles per video frame
#define CYCLES_PER_FRAME        (CPU_CLOCK_HZ / TARGET_FPS)

// Scanlines per frame (NTSC=262, PAL=312)
#define SCANLINES_PER_FRAME     262

// Active display scanlines
#define ACTIVE_SCANLINES        192

#endif // CONFIG_H
