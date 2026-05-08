/*
 * =============================================================
 *   CoCo2-CYD Beta-1 March 2026 - CoCo 2 Emulator for ESP32 CYD
 *   (C) 2026 Reinaldo Torres / CoCo Byte Club
 *   https://github.com/reyco2000/CoCo2-CYD
 *   Based on XRoar Emulator by Ciaran Anscomb
 *   CO-developed with Claude Code (Anthropic)
 *   MIT License
 * =============================================================
 *  File   : sv_menu.h
 *  Module : Supervisor OSD main menu interface
 * =============================================================
 */

/*
 * sv_menu.h - Main menu for supervisor OSD
 */

#ifndef SV_MENU_H
#define SV_MENU_H

#include <stdint.h>

// Forward declaration
struct Supervisor_t;

enum SV_MenuAction : uint8_t {
    SV_ACT_MOUNT_DISK,
    SV_ACT_DISK_MANAGER,
    SV_ACT_MACHINE_SELECT,
    SV_ACT_RESET,
    SV_ACT_SETTINGS,
    SV_ACT_ABOUT,
    SV_ACT_DEBUG_DUMP,
    SV_ACT_RESUME,
};

struct SV_MenuItem {
    const char* label;
    SV_MenuAction action;
    const char* value;  // Right-aligned value text, NULL if none
};

void sv_menu_init(Supervisor_t* sv);
void sv_menu_on_key(Supervisor_t* sv, uint8_t hid_usage, bool pressed);
void sv_menu_on_touch(Supervisor_t* sv, uint16_t x, uint16_t y);
void sv_menu_render(Supervisor_t* sv);
void sv_menu_update_values(Supervisor_t* sv);

#endif // SV_MENU_H
