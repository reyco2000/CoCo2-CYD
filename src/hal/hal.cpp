/*
 * =============================================================
 *   CoCo2-CYD Beta-1 March 2026 - CoCo 2 Emulator for ESP32 CYD
 *   (C) 2026 Reinaldo Torres / CoCo Byte Club
 *   https://github.com/reyco2000/CoCo2-CYD
 *   Based on XRoar Emulator by Ciaran Anscomb
 *   CO-developed with Claude Code (Anthropic)
 *   MIT License
 * =============================================================
 *  File   : hal.cpp
 *  Module : HAL top-level — initialization and per-frame dispatch to subsystem drivers
 * =============================================================
 */

/*
 * hal.cpp - Top-level HAL initialization and frame dispatch
 *
 * Calls into individual subsystem init/update functions.
 */

#include "hal.h"
#include "../utils/debug.h"
#include "usb_kbd_host.h"  // stubs when USE_USB_HOST=0
#include "hal_touch_cal.h"

void hal_init(void) {
    DEBUG_PRINT("HAL: initializing subsystems...");
    // NOTE: hal_storage_init() is called from setup() AFTER hal_video_init().
    // TFT_eSPI's internal spi.begin() with default VSPI pins (GPIO 18/19/23/5)
    // hijacks those GPIO matrix outputs — running SD init last lets HSPI take
    // them back without an explicit restore step.
    hal_audio_init();
    hal_keyboard_init();
#if JOYSTICK_ENABLED
    hal_joystick_init();
#endif
    DEBUG_PRINT("HAL: init complete (video + storage deferred)");
}

void hal_process_input(void) {
    hal_keyboard_tick();
    hid_host_process();        // no-op when USE_USB_HOST=0
    hal_keyboard_update_touch(); // no-op when TOUCH_KB_ENABLED=0
    touch_cal_check_boot_button();
#if JOYSTICK_ENABLED
    hal_joystick_update();
#endif
}

void hal_render_frame(void) {
    // Video present is called by machine_run_frame() after all scanlines
    // Nothing additional needed here
}
