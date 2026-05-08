/*
 * =============================================================
 *   CoCo2-CYD Beta-1 March 2026 - CoCo 2 Emulator for ESP32 CYD
 *   (C) 2026 Reinaldo Torres / CoCo Byte Club
 *   https://github.com/reyco2000/CoCo2-CYD
 *   Based on XRoar Emulator by Ciaran Anscomb
 *   CO-developed with Claude Code (Anthropic)
 *   MIT License
 * =============================================================
 *  File   : hal_touch_cal.h
 *  Module : XPT2046 touch calibration interface — NVS load and boot-button trigger
 * =============================================================
 */

#pragma once

#include "../../config.h"

// Load saved XPT2046 calibration from NVS into runtime variables.
// Falls back to config.h compile-time defaults if no NVS data exists.
// Also initialises GPIO0 (BOOT button) as INPUT_PULLUP.
// Call once during keyboard init.
void touch_cal_load_nvs(void);

// Poll GPIO0 for 5-second hold; draws countdown in TFT border.
// Launches full-screen calibration UI when hold completes.
// Call once per frame from hal_process_input().
#if TOUCH_KB_ENABLED
void touch_cal_check_boot_button(void);
#else
static inline void touch_cal_check_boot_button(void) {}
#endif
