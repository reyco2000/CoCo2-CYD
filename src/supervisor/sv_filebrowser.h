/*
 * =============================================================
 *   CoCo2-CYD Beta-1 March 2026 - CoCo 2 Emulator for ESP32 CYD
 *   (C) 2026 Reinaldo Torres / CoCo Byte Club
 *   https://github.com/reyco2000/CoCo2-CYD
 *   Based on XRoar Emulator by Ciaran Anscomb
 *   CO-developed with Claude Code (Anthropic)
 *   MIT License
 * =============================================================
 *  File   : sv_filebrowser.h
 *  Module : File browser interface — disk image selection from SD card
 * =============================================================
 */

/*
 * sv_filebrowser.h - SD card file browser for disk image selection
 */

#ifndef SV_FILEBROWSER_H
#define SV_FILEBROWSER_H

#include "../../config.h"

#if DISK_ENABLED

#include <stdint.h>
#include <stdbool.h>

// Forward declaration
struct Supervisor_t;

#define SV_FB_VISIBLE_ITEMS    8
#define SV_FB_MAX_ENTRIES      128

struct SV_FileEntry {
    char     name[32];
    uint32_t size;
    bool     is_dir;
    bool     is_supported;
};

void sv_filebrowser_init(Supervisor_t* sv);
void sv_filebrowser_open(Supervisor_t* sv, const char* path, uint8_t target_drive);
void sv_filebrowser_on_key(Supervisor_t* sv, uint8_t hid_usage, bool pressed);
void sv_filebrowser_on_touch(Supervisor_t* sv, uint16_t x, uint16_t y);
void sv_filebrowser_render(Supervisor_t* sv);
const char* sv_filebrowser_get_selected_path(Supervisor_t* sv);

int  sv_fb_scan_directory(const char* path, SV_FileEntry* entries, int max_entries);
void sv_fb_sort_entries(SV_FileEntry* entries, int count);
bool sv_fb_is_disk_image(const char* filename);

#else  // !DISK_ENABLED

// Forward declare so supervisor.h can compile without knowing Supervisor_t fully yet
struct Supervisor_t;
static inline void sv_filebrowser_init(Supervisor_t*)                          {}
static inline void sv_filebrowser_open(Supervisor_t*, const char*, uint8_t)    {}
static inline void sv_filebrowser_on_key(Supervisor_t*, uint8_t, bool)         {}
static inline void sv_filebrowser_on_touch(Supervisor_t*, uint16_t, uint16_t)   {}
static inline void sv_filebrowser_render(Supervisor_t*)                        {}
static inline const char* sv_filebrowser_get_selected_path(Supervisor_t*)      { return nullptr; }

#endif // DISK_ENABLED

#endif // SV_FILEBROWSER_H
