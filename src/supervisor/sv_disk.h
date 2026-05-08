/*
 * =============================================================
 *   CoCo2-CYD Beta-1 March 2026 - CoCo 2 Emulator for ESP32 CYD
 *   (C) 2026 Reinaldo Torres / CoCo Byte Club
 *   https://github.com/reyco2000/CoCo2-CYD
 *   Based on XRoar Emulator by Ciaran Anscomb
 *   CO-developed with Claude Code (Anthropic)
 *   MIT License
 * =============================================================
 *  File   : sv_disk.h
 *  Module : Virtual FDC interface — WD1793-compatible disk controller (.DSK/.VDK format)
 * =============================================================
 */

/*
 * sv_disk.h - Virtual floppy disk controller (WD1793-compatible)
 *
 * Simplified FDC for CoCo Disk BASIC and OS-9 compatibility.
 * Supports .DSK (JVC) and .VDK disk image formats.
 * Mapped at $FF40-$FF5F in the CoCo address space.
 */

#ifndef SV_DISK_H
#define SV_DISK_H

#include "../../config.h"

#if DISK_ENABLED

#include <stdint.h>
#include <stdbool.h>
#include <FS.h>

#define SV_DISK_MAX_DRIVES   4

// Standard CoCo disk geometry
#define DISK_TRACKS          35
#define DISK_SECTORS         18
#define DISK_SECTOR_SIZE     256
#define DISK_TRACK_SIZE      (DISK_SECTORS * DISK_SECTOR_SIZE)
#define DISK_STANDARD_SIZE   (DISK_TRACKS * DISK_TRACK_SIZE)  // 161280

struct SV_DiskImage {
    char     path[256];
    File     file;           // Kept open for write-back on flush/eject
    bool     mounted;
    bool     dirty;
    bool     read_only;
    uint8_t  tracks;
    uint8_t  sectors_per_track;
    uint16_t sector_size;
    uint8_t  sides;
    uint16_t header_size;
    uint32_t image_size;

    uint32_t data_size;      // Image data bytes after header (tracks*spt*sector_size)
};

struct SV_DiskController {
    // WD1793 registers
    uint8_t command;
    uint8_t status;
    uint8_t track;
    uint8_t sector;
    uint8_t data;

    // Drive select latch ($FF40) — matching XRoar rsdos.c
    uint8_t drive_select;
    bool    motor_on;
    bool    halt_enable;   // Bit 7: DRQ → CPU HALT (XRoar: halt_enable)
    bool    density;       // Bit 5 XOR'd: gates NMI path (XRoar: ic1_density)

    // Internal state
    bool    busy;
    bool    drq;
    bool    intrq;         // INTRQ flag (latched until status read or new cmd)
    uint8_t intrq_defer;   // Countdown: fire INTRQ after N more scanline ticks
    int     step_direction;

    // Sector buffer
    uint8_t  sector_buf[256];
    uint16_t buf_pos;
    uint16_t buf_len;
    bool     reading;
    bool     writing;

    // WRITE TRACK (format) state machine
    bool     write_track;       // True during WRITE TRACK command
    uint8_t  wt_state;          // 0=idle, 1=id_field, 2=wait_dam, 3=data_field
    uint8_t  wt_f5_count;       // Consecutive $F5 bytes seen
    uint8_t  wt_id_pos;         // Position within 4-byte ID field
    uint8_t  wt_id_sector;      // Sector number from ID field
    uint16_t wt_data_pos;       // Position within sector data
    uint16_t wt_byte_count;     // Total bytes received for this track

    // Drives
    SV_DiskImage drives[SV_DISK_MAX_DRIVES];

    // Callbacks (wired by supervisor to CPU)
    void (*nmi_callback)(void* ctx, bool active);
    void (*halt_callback)(void* ctx, bool halted);
    void* callback_context;
};

void sv_disk_init(SV_DiskController* fdc);
void sv_disk_reset(SV_DiskController* fdc);

// CPU bus interface ($FF40-$FF5F)
uint8_t sv_disk_read(SV_DiskController* fdc, uint16_t address);
void    sv_disk_write(SV_DiskController* fdc, uint16_t address, uint8_t value);

// Call once per CPU instruction to handle deferred INTRQ
void sv_disk_tick(SV_DiskController* fdc);

// Disk image management
bool sv_disk_mount(SV_DiskController* fdc, uint8_t drive, const char* path);
void sv_disk_eject(SV_DiskController* fdc, uint8_t drive);
bool sv_disk_is_mounted(SV_DiskController* fdc, uint8_t drive);
const char* sv_disk_get_path(SV_DiskController* fdc, uint8_t drive);

void sv_disk_flush(SV_DiskController* fdc, uint8_t drive);
void sv_disk_flush_all(SV_DiskController* fdc);

// Read one sector directly from the disk file into dst (256 bytes).
// Returns true on success. Intended for integration tests and diagnostic code.
bool sv_disk_read_sector_raw(SV_DiskController* fdc, uint8_t drive,
                             uint8_t track, uint8_t sector, uint8_t* dst);

bool sv_disk_detect_geometry(SV_DiskImage* img);

#else  // !DISK_ENABLED — empty stubs so Machine struct and callers compile

#include <stdint.h>
#include <stdbool.h>

#define SV_DISK_MAX_DRIVES 0

struct SV_DiskImage {};

struct SV_DiskController {
    void (*nmi_callback)(void* ctx, bool active);
    void (*halt_callback)(void* ctx, bool halted);
    void* callback_context;
};

static inline void    sv_disk_init(SV_DiskController*)                              {}
static inline void    sv_disk_reset(SV_DiskController*)                             {}
static inline uint8_t sv_disk_read(SV_DiskController*, uint16_t)                   { return 0xFF; }
static inline void    sv_disk_write(SV_DiskController*, uint16_t, uint8_t)         {}
static inline void    sv_disk_tick(SV_DiskController*)                              {}
static inline bool    sv_disk_mount(SV_DiskController*, uint8_t, const char*)      { return false; }
static inline void    sv_disk_eject(SV_DiskController*, uint8_t)                   {}
static inline bool    sv_disk_is_mounted(SV_DiskController*, uint8_t)              { return false; }
static inline const char* sv_disk_get_path(SV_DiskController*, uint8_t)            { return ""; }
static inline void    sv_disk_flush(SV_DiskController*, uint8_t)                   {}
static inline void    sv_disk_flush_all(SV_DiskController*)                        {}

#endif // DISK_ENABLED

#endif // SV_DISK_H
