/*
 * =============================================================
 *   CoCo2-CYD Beta-1 March 2026 - CoCo 2 Emulator for ESP32 CYD
 *   (C) 2026 Reinaldo Torres / CoCo Byte Club
 *   https://github.com/reyco2000/CoCo2-CYD
 *   Based on XRoar Emulator by Ciaran Anscomb
 *   CO-developed with Claude Code (Anthropic)
 *   MIT License
 * =============================================================
 *  File   : hal.h
 *  Module : Hardware Abstraction Layer interface — all platform I/O declarations
 * =============================================================
 */

/*
 * hal.h - Hardware Abstraction Layer interface
 *
 * All platform-specific I/O goes through these functions.
 * Implementations are in the individual hal_*.cpp files.
 */

#ifndef HAL_H
#define HAL_H

#include <Arduino.h>
#include "../../config.h"

// ============================================================
// Top-level HAL control
// ============================================================

// Initialize all HAL subsystems
void hal_init(void);

// Process all pending input
void hal_process_input(void);

// Render the current frame to the display
void hal_render_frame(void);

// ============================================================
// Video subsystem
// ============================================================

// Initialize the display hardware
void hal_video_init(void);

// Set the MC6847 display mode
//   mode bits: CSS, GM2, GM1, GM0, A/G, A/S, INT/EXT, INV
void hal_video_set_mode(uint8_t mode);

// Render one scanline of video data
//   line: scanline number (0-191 for active area)
//   pixels: pointer to pixel data for this line
//   width: number of pixels in the line
void hal_video_render_scanline(int line, const uint8_t* pixels, int width);

// Present the completed frame to the display
// Performs VRAM shadow compare: skips SPI push if screen unchanged (OPT-16)
//   ram:      pointer to CoCo RAM (64KB)
//   vdg_base: SAM F0-F6 display base address
//   vdg_mode: VDG mode byte (AG|GM|CSS bits)
void hal_video_present(const uint8_t* ram, uint16_t vdg_base, uint8_t vdg_mode);

// ============================================================
// Audio subsystem
// ============================================================

// Initialize audio output hardware
void hal_audio_init(void);

// Write a single audio sample (mono or stereo depending on config)
void hal_audio_write_sample(int16_t left, int16_t right);

// Set audio volume (0-255)
void hal_audio_set_volume(uint8_t volume);

// Write single-bit audio (PIA1 port B bit 1)
void hal_audio_write_bit(bool value);

// Write 6-bit DAC audio (PIA1 port A bits 2-7, value 0-63)
void hal_audio_write_dac(uint8_t dac6);

// Call once per frame from main loop — detects end-of-sound and prints frequency debug
void hal_audio_debug_tick(void);

// Capture current audio level for the current scanline (call once per scanline)
void hal_audio_capture_scanline(void);

// Commit the scanline audio buffer for ISR playback (call at frame end)
void hal_audio_commit_frame(void);

// ============================================================
// Keyboard subsystem
// ============================================================

// Initialize keyboard input
void hal_keyboard_init(void);

// Scan keyboard matrix for a given column
//   column: 0-7 (active low)
//   Returns: row data (active low, bits 0-6)
uint8_t hal_keyboard_scan(uint8_t column);

// Tick deferred key releases (call once per frame)
void hal_keyboard_tick(void);

// Process touch input and feed OSK events into matrix (call once per frame)
void hal_keyboard_update_touch(void);

// Returns true while the on-screen keyboard is visible (sprite push suppressed)
bool hal_keyboard_osk_visible(void);

// Draw the hotzone indicator buttons (F1/F2/F5/KB) in the TFT border
void hal_keyboard_draw_overlay(void);

// Force the next hal_keyboard_draw_overlay() call to repaint the hotzone buttons
void hal_keyboard_invalidate_hotzone(void);

// Read raw XPT2046 ADC values (used by touch calibration routine)
bool hal_keyboard_read_raw_xy(uint16_t* out_x, uint16_t* out_y);

// Override touch calibration constants at runtime (persisted via NVS)
void hal_keyboard_set_touch_cal(uint16_t x_min, uint16_t x_max,
                                 uint16_t y_min, uint16_t y_max);

// ============================================================
// Joystick subsystem
// ============================================================

#if JOYSTICK_ENABLED
void    hal_joystick_init(void);
uint8_t hal_joystick_read_axis(int port, int axis);
uint8_t hal_joystick_read_button(int port);
bool    hal_joystick_compare(int port, int axis, uint8_t dac_value);
void    hal_joystick_update(void);
#else
static inline void    hal_joystick_init(void)                                {}
static inline uint8_t hal_joystick_read_axis(int, int)                      { return 0; }
static inline uint8_t hal_joystick_read_button(int)                         { return 0; }
static inline bool    hal_joystick_compare(int, int, uint8_t)               { return false; }
static inline void    hal_joystick_update(void)                             {}
#endif

// Set machine pointer for keyboard hotkeys (F2 reset, etc.)
struct Machine;
void hal_keyboard_set_machine(Machine* m);


// Force full display repaint (invalidates VRAM shadow compare)
void hal_video_force_repaint(void);

// Toggle FPS overlay
void hal_video_toggle_fps_overlay(void);

// Diagnostic: fill sprite with color and push (used to verify SPI post-init)
void hal_video_push_test_color(uint16_t color);

// ============================================================
// Keyboard injection (for integration tests)
// ============================================================

// Press a CoCo key by matrix position (row, col)
void hal_keyboard_press(uint8_t row, uint8_t col);

// Release a CoCo key by matrix position
void hal_keyboard_release(uint8_t row, uint8_t col);

// Release all keys
void hal_keyboard_release_all(void);

// ============================================================
// Storage subsystem
// ============================================================

// Initialize storage (SD card or flash filesystem)
bool hal_storage_init(void);

// Load a file into a buffer
//   path: file path (relative to storage root)
//   buffer: destination buffer
//   size: max bytes to read
//   Returns: true on success
bool hal_storage_load_file(const char* path, uint8_t* buffer, size_t size);

// Check if a file exists
bool hal_storage_file_exists(const char* path);

#endif // HAL_H
