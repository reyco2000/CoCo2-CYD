#include "../../config.h"
#if DISK_ENABLED

/*
 * =============================================================
 *   CoCo2-CYD Beta-1 March 2026 - CoCo 2 Emulator for ESP32 CYD
 *   (C) 2026 Reinaldo Torres / CoCo Byte Club
 *   https://github.com/reyco2000/CoCo2-CYD
 *   Based on XRoar Emulator by Ciaran Anscomb
 *   CO-developed with Claude Code (Anthropic)
 *   MIT License
 * =============================================================
 *  File   : sv_disk.cpp
 *  Module : WD1793-compatible FDC emulation — PSRAM disk cache with INTRQ/NMI/HALT mechanism
 * =============================================================
 */

/*
 * sv_disk.cpp - Virtual floppy disk controller (WD1793-compatible)
 *
 * Command-level FDC emulation with INTRQ tracking.
 * Sectors are read and written directly from the SD card file via
 * seek()+read()/write() — no full-image cache needed.
 *
 * DSKCON sequence: issue command → enable NMI in DSKREG → wait for NMI.
 * INTRQ is latched when a command completes. NMI fires when BOTH
 * INTRQ is asserted AND NMI is enabled in DSKREG ($FF40 bit 7).
 * INTRQ is cleared on status register read (matching XRoar behavior).
 *
 * CRITICAL NOTES:
 * - Sectors are 1-based (1-18), NOT 0-based
 * - NMI fires via INTRQ + DSKREG NMI enable, NOT immediately
 */

#include "sv_disk.h"
#include "../utils/debug.h"
#include <SD.h>

// WRITE TRACK state machine constants
#define WT_IDLE        0
#define WT_ID_FIELD    1
#define WT_WAIT_DAM    2
#define WT_DATA_FIELD  3
#define WT_TRACK_BYTES 6250  // Standard DD track length in bytes

// Signal HALT to CPU (matching XRoar: signal_halt)
static void signal_halt(SV_DiskController* fdc, bool halted) {
    if (fdc->halt_callback) {
        fdc->halt_callback(fdc->callback_context, halted);
    }
}

// Signal NMI to CPU (matching XRoar: signal_nmi, now edge-aware)
static void signal_nmi(SV_DiskController* fdc, bool active) {
    if (fdc->nmi_callback) {
        fdc->nmi_callback(fdc->callback_context, active);
    }
}

// INTRQ callback — matches XRoar rsdos.c set_intrq()
// When INTRQ asserts: disable HALT, release CPU, fire NMI via density gating
// When INTRQ clears: deassert NMI line
static void set_intrq(SV_DiskController* fdc, bool value) {
    fdc->intrq = value;
    if (value) {
        // INTRQ asserted: disable HALT and release CPU
        fdc->halt_enable = false;
        signal_halt(fdc, false);
    }
    // NMI gating: matches XRoar rsdos.c set_intrq() exactly
    // NMI fires when !density AND intrq; otherwise NMI deasserted
    if (!fdc->density && fdc->intrq) {
        signal_nmi(fdc, true);
    } else {
        signal_nmi(fdc, false);
    }
}

// DRQ callback — matches XRoar rsdos.c set_drq()
// DRQ high = data ready → release HALT
// DRQ low = waiting → engage HALT if enabled
static void set_drq(SV_DiskController* fdc, bool value) {
    fdc->drq = value;
    if (value) {
        // Data ready → release CPU
        signal_halt(fdc, false);
    } else {
        // No data → halt CPU if halt is enabled
        if (fdc->halt_enable) {
            signal_halt(fdc, true);
        }
    }
}

static uint32_t disk_data_size(const SV_DiskImage* img) {
    return (uint32_t)img->tracks * img->sides
         * img->sectors_per_track * img->sector_size;
}

// Returns byte offset into image data (after header), or UINT32_MAX on error.
// Sectors are 1-based (CoCo BASIC uses 1-18).
static uint32_t sector_offset(SV_DiskImage* img, uint8_t track, uint8_t sector) {
    if (sector < 1 || sector > img->sectors_per_track) return UINT32_MAX;
    if (track >= img->tracks) return UINT32_MAX;

    uint32_t offset = (uint32_t)track * img->sectors_per_track * img->sector_size;
    offset += (uint32_t)(sector - 1) * img->sector_size;

    if (offset + img->sector_size > disk_data_size(img)) return UINT32_MAX;
    return offset;
}

static bool disk_read_sector(SV_DiskImage* img, uint8_t track, uint8_t sector, uint8_t* dst) {
    uint32_t off = sector_offset(img, track, sector);
    if (off == UINT32_MAX) return false;
    if (!img->file.seek(img->header_size + off)) return false;
    int got = img->file.read(dst, img->sector_size);
    return got == (int)img->sector_size;
}

static bool disk_write_sector(SV_DiskImage* img, uint8_t track, uint8_t sector,
                              const uint8_t* src, bool flush_now) {
    if (img->read_only) return false;
    uint32_t off = sector_offset(img, track, sector);
    if (off == UINT32_MAX) return false;
    if (!img->file.seek(img->header_size + off)) return false;
    size_t wrote = img->file.write(src, img->sector_size);
    if (wrote != img->sector_size) return false;
    if (flush_now) img->file.flush();
    return true;
}

// Execute FDC command
static void fdc_execute_command(SV_DiskController* fdc, uint8_t cmd) {
    SV_DiskImage* disk = &fdc->drives[fdc->drive_select];
    uint8_t cmd_type = cmd & 0xF0;

    // Clear any in-progress transfer state from previous command
    fdc->reading = false;
    fdc->writing = false;
    fdc->write_track = false;
    fdc->drq = false;
    fdc->intrq_defer = 0;

    fdc->command = cmd;
    fdc->busy = true;
    fdc->status = 0x01;  // BUSY

    switch (cmd_type) {
        case 0x00:  // RESTORE
            fdc->track = 0;
            fdc->step_direction = -1;
            fdc->status = 0x04;  // TRACK 0
            if (!disk->mounted) fdc->status |= 0x80;
            fdc->busy = false;
            break;

        case 0x10:  // SEEK
            fdc->track = fdc->data;
            fdc->status = (fdc->track == 0) ? 0x04 : 0x00;
            if (!disk->mounted) fdc->status |= 0x80;
            fdc->busy = false;
            break;

        case 0x80: case 0x90:  // READ SECTOR
            if (!disk->mounted || !disk->file) {
                fdc->status = 0x80;  // NOT READY
                fdc->busy = false;
                break;
            }
            {
                if (!disk_read_sector(disk, fdc->track, fdc->sector, fdc->sector_buf)) {
                    memset(fdc->sector_buf, 0, sizeof(fdc->sector_buf));
                    fdc->status = 0x10;  // RECORD NOT FOUND
                    fdc->busy = false;
                    break;
                }

                fdc->buf_pos = 0;
                fdc->buf_len = disk->sector_size;
                fdc->reading = true;
                fdc->drq = true;
                fdc->data = fdc->sector_buf[0];
                fdc->status = 0x03;  // BUSY + DRQ
            }
            break;

        case 0xA0: case 0xB0:  // WRITE SECTOR
            if (!disk->mounted || !disk->file) {
                fdc->status = 0x80;
                fdc->busy = false;
                break;
            }
            if (disk->read_only) {
                fdc->status = 0x40;  // WRITE PROTECT
                fdc->busy = false;
                break;
            }
            fdc->buf_pos = 0;
            fdc->buf_len = disk->sector_size;
            fdc->writing = true;
            fdc->drq = true;
            fdc->status = 0x03;  // BUSY + DRQ
            break;

        case 0xE0:  // READ TRACK (not needed, return empty)
            fdc->status = 0x00;
            fdc->busy = false;
            break;

        case 0xF0:  // WRITE TRACK (format)
            if (!disk->mounted || !disk->file) {
                fdc->status = 0x80;
                fdc->busy = false;
                break;
            }
            if (disk->read_only) {
                fdc->status = 0x40;  // WRITE PROTECT
                fdc->busy = false;
                break;
            }
            // Initialize write-track state machine
            fdc->write_track = true;
            fdc->wt_state = WT_IDLE;
            fdc->wt_f5_count = 0;
            fdc->wt_id_pos = 0;
            fdc->wt_id_sector = 1;
            fdc->wt_data_pos = 0;
            fdc->wt_byte_count = 0;
            fdc->drq = true;
            fdc->status = 0x03;  // BUSY + DRQ
            break;

        case 0xD0:  // FORCE INTERRUPT
            fdc->busy = false;
            fdc->reading = false;
            fdc->writing = false;
            fdc->write_track = false;
            fdc->drq = false;
            fdc->status = 0x00;
            if (fdc->track == 0) fdc->status = 0x04;
            if (!disk->mounted) fdc->status |= 0x80;
            // Bit 3 = immediate interrupt (like XRoar)
            if (cmd & 0x08) {
                set_intrq(fdc, true);
            }
            return;  // Don't fall through to set_intrq below

        default:
            // STEP / STEP IN / STEP OUT
            if (cmd_type >= 0x20 && cmd_type <= 0x7F) {
                if (cmd_type >= 0x40 && cmd_type <= 0x5F) fdc->step_direction = 1;
                if (cmd_type >= 0x60 && cmd_type <= 0x7F) fdc->step_direction = -1;
                int new_track = (int)fdc->track + fdc->step_direction;
                if (new_track < 0) new_track = 0;
                if (new_track > 79) new_track = 79;
                if (cmd & 0x10) fdc->track = (uint8_t)new_track;
                fdc->status = 0x00;
                if (fdc->track == 0) fdc->status = 0x04;
                if (!disk->mounted) fdc->status |= 0x80;
                fdc->busy = false;
            } else {
                fdc->status = 0x00;
                fdc->busy = false;
            }
            break;
    }

    // Assert INTRQ when command completes (not mid-transfer)
    if (!fdc->busy) {
        set_intrq(fdc, true);
    }
}

// Data register read ($FF4B) during READ SECTOR
//
// DSKCON read loop: LDA $FF4B / STA ,X+ / BRA LOOP  (broken by NMI)
//
// We can't model cycle-accurate HALT (no propagation delay), so bytes
// are delivered instantly — the CPU reads all 256 bytes at full speed.
//
// For the last byte (256th read): we can't fire INTRQ immediately
// because NMI would preempt STA ,X+ (the CPU hasn't stored the byte
// yet).  Instead, we defer: on the NEXT $FF4B read (byte 257 attempt,
// after STA has completed), we fire INTRQ.  NMI preempts the stale
// byte 257's STA — which is exactly what the DSKCON NMI handler
// expects (it manipulates the stack to skip the read loop).
//
// sv_disk_tick provides a safety-net fallback (intrq_defer countdown)
// in case the CPU doesn't loop back to $FF4B for some reason.
//
static uint8_t fdc_read_data(SV_DiskController* fdc) {
    uint8_t val = fdc->data;

    if (fdc->reading && fdc->buf_pos < fdc->buf_len) {
        fdc->buf_pos++;
        if (fdc->buf_pos < fdc->buf_len) {
            fdc->data = fdc->sector_buf[fdc->buf_pos];
            fdc->drq = true;
        } else {
            // Transfer complete.  Defer INTRQ until the byte-257 read.
            // Use a large defer count so sv_disk_tick doesn't fire
            // before the byte-257 path (which fires in ~14 CPU cycles).
            fdc->reading = false;
            fdc->drq = false;
            fdc->busy = false;
            fdc->status = 0x00;
            fdc->intrq_defer = 5;  // safety net: 5 scanlines (~285 cycles)
        }
    } else if (fdc->intrq_defer > 0 && !fdc->intrq) {
        // Post-transfer: CPU looped back to LDA $FF4B (byte 257).
        // STA ,X+ for byte 256 has already executed.  Fire INTRQ now
        // so NMI breaks out before this stale read gets stored.
        fdc->intrq_defer = 0;
        set_intrq(fdc, true);
    }

    return val;
}

// WRITE TRACK byte-level state machine
// Parses the raw format stream to extract sector data and writes to cache.
// CoCo DSKINI format stream per sector:
//   ... $F5 $F5 $F5 $FE [track] [side] [sector] [size] $F7 ...
//   ... $F5 $F5 $F5 $FB [256 bytes data] $F7 ...
// $F5 = address mark prefix, $FE = ID AM, $FB = Data AM, $F7 = CRC

static void fdc_write_track_byte(SV_DiskController* fdc, uint8_t value) {
    fdc->wt_byte_count++;

    uint8_t f5 = fdc->wt_f5_count;

    // Track consecutive $F5 bytes (address mark prefix)
    if (value == 0xF5) {
        fdc->wt_f5_count++;
        // $F5 is consumed by address mark logic, not sector data
        goto check_done;
    }

    fdc->wt_f5_count = 0;

    switch (fdc->wt_state) {
        case WT_IDLE:
        case WT_WAIT_DAM:
            if (f5 >= 3 && value == 0xFE) {
                // ID Address Mark — next 4 bytes are track/side/sector/size
                fdc->wt_state = WT_ID_FIELD;
                fdc->wt_id_pos = 0;
            } else if (f5 >= 3 && value == 0xFB) {
                // Data Address Mark — next sector_size bytes are data
                fdc->wt_state = WT_DATA_FIELD;
                fdc->wt_data_pos = 0;
            }
            // $F7 (CRC) and gap bytes are ignored
            break;

        case WT_ID_FIELD:
            // 4 bytes: track, side, sector, size_code
            if (fdc->wt_id_pos == 2) {
                fdc->wt_id_sector = value;  // Capture sector number
            }
            fdc->wt_id_pos++;
            if (fdc->wt_id_pos >= 4) {
                fdc->wt_state = WT_WAIT_DAM;
            }
            break;

        case WT_DATA_FIELD: {
            SV_DiskImage* disk = &fdc->drives[fdc->drive_select];
            fdc->sector_buf[fdc->wt_data_pos] = value;
            fdc->wt_data_pos++;
            if (fdc->wt_data_pos >= disk->sector_size) {
                bool ok = disk_write_sector(disk, fdc->track, fdc->wt_id_sector,
                                           fdc->sector_buf, false);
                if (!ok) fdc->status = 0x20;  // WRITE FAULT
                fdc->wt_state = WT_IDLE;
            }
            break;
        }
    }

check_done:
    // Track complete after enough bytes received
    if (fdc->wt_byte_count >= WT_TRACK_BYTES) {
        SV_DiskImage* disk = &fdc->drives[fdc->drive_select];
        if (disk->file) disk->file.flush();
        fdc->write_track = false;
        fdc->drq = false;
        fdc->busy = false;
        fdc->status = 0x00;
        set_intrq(fdc, true);
        return;
    }

    // Request next byte
    set_drq(fdc, true);
}

// Data register write ($FF4B) during WRITE SECTOR
static void fdc_write_data(SV_DiskController* fdc, uint8_t value) {
    // WRITE TRACK (format) data handling
    if (fdc->write_track) {
        fdc_write_track_byte(fdc, value);
        return;
    }

    if (fdc->writing && fdc->buf_pos < fdc->buf_len) {
        fdc->sector_buf[fdc->buf_pos] = value;
        fdc->buf_pos++;

        if (fdc->buf_pos >= fdc->buf_len) {
            SV_DiskImage* disk = &fdc->drives[fdc->drive_select];
            bool ok = disk_write_sector(disk, fdc->track, fdc->sector, fdc->sector_buf, true);
            fdc->writing = false;
            fdc->drq = false;
            fdc->busy = false;
            fdc->status = ok ? 0x00 : 0x20;  // WRITE FAULT on failure
            set_intrq(fdc, true);
        } else {
            fdc->drq = true;
        }
    }
}

// DSKREG write ($FF40) — matches XRoar rsdos.c ff40_write()
static void fdc_write_drive_select(SV_DiskController* fdc, uint8_t value) {
    // Drive select: bits 0-2 are individual drive select lines
    if (value & 0x01)      fdc->drive_select = 0;
    else if (value & 0x02) fdc->drive_select = 1;
    else if (value & 0x04) fdc->drive_select = 2;
    else                   fdc->drive_select = 0;

    if (fdc->drive_select >= SV_DISK_MAX_DRIVES)
        fdc->drive_select = 0;

    fdc->motor_on = (value & 0x08) != 0;

    // Density bit with XOR (matching XRoar: octet ^= 0x20)
    // This inverts bit 5 so that the "normal" CoCo setting (bit5=1)
    // results in density=false, which enables NMI via set_intrq path.
    uint8_t xored = value ^ 0x20;
    fdc->density = (xored & 0x20) != 0;

    // NMI gating: matches XRoar rsdos.c ff40_write()
    // NMI fires when !ic1_density AND intrq_flag; otherwise NMI deasserted
    if (!fdc->density && fdc->intrq) {
        signal_nmi(fdc, true);
    } else {
        signal_nmi(fdc, false);
    }

    // Bit 7 = HALT enable (NOT NMI!) — matches XRoar: halt_enable = octet & 0x80
    fdc->halt_enable = (value & 0x80) != 0;

    // INTRQ disables HALT (XRoar: if (intrq_flag) halt_enable = 0)
    if (fdc->intrq) fdc->halt_enable = false;

    // Apply HALT state to CPU
    signal_halt(fdc, fdc->halt_enable && !fdc->drq);
}

// ============================================================
// Public API
// ============================================================

void sv_disk_init(SV_DiskController* fdc) {
    memset(fdc, 0, sizeof(SV_DiskController));
    fdc->step_direction = -1;
    fdc->status = 0x04;  // Track 0
    for (int i = 0; i < SV_DISK_MAX_DRIVES; i++) {
        fdc->drives[i].mounted = false;
        fdc->drives[i].dirty = false;
        fdc->drives[i].data_size = 0;
        fdc->drives[i].sectors_per_track = DISK_SECTORS;
        fdc->drives[i].sector_size = DISK_SECTOR_SIZE;
        fdc->drives[i].tracks = DISK_TRACKS;
        fdc->drives[i].sides = 1;
    }
}

void sv_disk_reset(SV_DiskController* fdc) {
    fdc->command = 0;
    fdc->status = 0x04;
    fdc->track = 0;
    fdc->sector = 1;
    fdc->data = 0;
    fdc->busy = false;
    fdc->drq = false;
    fdc->intrq = false;
    fdc->intrq_defer = 0;
    fdc->reading = false;
    fdc->writing = false;
    fdc->write_track = false;
    fdc->step_direction = -1;
    fdc->drive_select = 0;
    fdc->motor_on = false;
    fdc->halt_enable = false;
    fdc->density = false;
}

uint8_t sv_disk_read(SV_DiskController* fdc, uint16_t address) {
    // $FF40-$FF47: DSKREG (drive select latch, write-only — reads return 0)
    if (address < 0xFF48) {
        return 0x00;
    }

    // $FF48-$FF4F: WD1793 FDC registers (mirrored every 4)
    switch (address & 0x03) {
        case 0: {  // Status ($FF48)
            uint8_t s = fdc->status;
            // Dynamically update NOT READY based on current mount state
            SV_DiskImage* disk = &fdc->drives[fdc->drive_select];
            if (disk->mounted) {
                s &= ~0x80;  // Clear NOT READY
            } else {
                s |= 0x80;   // Set NOT READY
            }
            // DRQ in bit 1 for Type II/III commands
            if (fdc->reading || fdc->writing || fdc->write_track) {
                if (fdc->drq) s |= 0x02;
                else s &= ~0x02;
            }
            // Clear INTRQ on status read (per WD279x datasheet / XRoar)
            set_intrq(fdc, false);
            return s;
        }
        case 1: return fdc->track;     // $FF49
        case 2: return fdc->sector;    // $FF4A
        case 3: return fdc_read_data(fdc);  // $FF4B
    }

    return 0x00;
}

void sv_disk_write(SV_DiskController* fdc, uint16_t address, uint8_t value) {
    // $FF40-$FF47: DSKREG (drive select latch)
    if (address < 0xFF48) {
        fdc_write_drive_select(fdc, value);
        return;
    }

    // $FF48-$FF4F: WD1793 FDC registers (mirrored every 4)
    switch (address & 0x03) {
        case 0:  // Command ($FF48)
            // Clear INTRQ when a new command is issued (per XRoar/datasheet)
            set_intrq(fdc, false);
            fdc_execute_command(fdc, value);
            break;
        case 1:  // Track ($FF49)
            fdc->track = value;
            break;
        case 2:  // Sector ($FF4A)
            fdc->sector = value;
            break;
        case 3:  // Data ($FF4B)
            fdc->data = value;
            fdc_write_data(fdc, value);
            break;
    }
}

void sv_disk_tick(SV_DiskController* fdc) {
    // Safety-net fallback: if the byte-257 $FF4B read doesn't fire
    // INTRQ (e.g., CPU stuck for some reason), the countdown fires
    // it after N scanlines.  Normally the byte-257 path fires in
    // ~14 CPU cycles (well within a single scanline), so this never
    // triggers in practice.
    if (fdc->intrq_defer > 0) {
        fdc->intrq_defer--;
        if (fdc->intrq_defer == 0) {
            set_intrq(fdc, true);
        }
    }
}

bool sv_disk_detect_geometry(SV_DiskImage* img) {
    uint32_t size = img->image_size;

    // Check for VDK header (12 bytes)
    const char* ext = strrchr(img->path, '.');
    if (ext && (strcasecmp(ext, ".vdk") == 0)) {
        img->header_size = 12;
        size -= 12;
    } else {
        // JVC: check if file size has remainder when divided by 256
        img->header_size = size % DISK_SECTOR_SIZE;
    }

    uint32_t data_size = size - img->header_size;
    img->sector_size = DISK_SECTOR_SIZE;
    img->sectors_per_track = DISK_SECTORS;

    // Determine track count from data size
    uint32_t track_size = (uint32_t)img->sectors_per_track * img->sector_size;
    if (track_size == 0) return false;

    img->tracks = data_size / track_size;
    if (img->tracks == 0) return false;

    // Check for double-sided
    if (img->tracks > 80) {
        img->sides = 2;
        img->tracks /= 2;
    } else {
        img->sides = 1;
    }

    return true;
}

bool sv_disk_mount(SV_DiskController* fdc, uint8_t drive, const char* path) {
    if (drive >= SV_DISK_MAX_DRIVES) return false;

    SV_DiskImage* img = &fdc->drives[drive];

    if (img->mounted) {
        sv_disk_eject(fdc, drive);
    }

    // Try read-write first so writes go in-place without truncation
    img->file = SD.open(path, "r+");
    if (!img->file) {
        img->file = SD.open(path, FILE_READ);
        if (!img->file) {
            DEBUG_PRINTF("FDC: Failed to open %s", path);
            return false;
        }
        img->read_only = true;
    } else {
        img->read_only = false;
    }

    strncpy(img->path, path, sizeof(img->path) - 1);
    img->path[sizeof(img->path) - 1] = '\0';
    img->image_size = img->file.size();
    img->dirty = false;

    if (!sv_disk_detect_geometry(img)) {
        DEBUG_PRINTF("FDC: Bad geometry for %s (size=%lu)", path, img->image_size);
        img->file.close();
        return false;
    }

    img->data_size = disk_data_size(img);
    img->mounted = true;

    DEBUG_PRINTF("FDC: Mounted drive %d: %s (%dT/%dS/%dB, %lu bytes, %s)",
                 drive, path, img->tracks, img->sectors_per_track,
                 img->sector_size, img->data_size,
                 img->read_only ? "read-only" : "read-write");

    return true;
}

void sv_disk_eject(SV_DiskController* fdc, uint8_t drive) {
    if (drive >= SV_DISK_MAX_DRIVES) return;

    SV_DiskImage* img = &fdc->drives[drive];
    if (!img->mounted) return;

    if (img->file) img->file.close();

    img->mounted = false;
    img->dirty = false;
    img->path[0] = '\0';

    DEBUG_PRINTF("FDC: Ejected drive %d", drive);
}

bool sv_disk_is_mounted(SV_DiskController* fdc, uint8_t drive) {
    if (drive >= SV_DISK_MAX_DRIVES) return false;
    return fdc->drives[drive].mounted;
}

const char* sv_disk_get_path(SV_DiskController* fdc, uint8_t drive) {
    if (drive >= SV_DISK_MAX_DRIVES) return "";
    return fdc->drives[drive].path;
}

void sv_disk_flush(SV_DiskController* fdc, uint8_t drive) {
    if (drive >= SV_DISK_MAX_DRIVES) return;
    SV_DiskImage* img = &fdc->drives[drive];
    if (!img->mounted || img->read_only || !img->file) return;
    img->file.flush();
    img->dirty = false;
}

void sv_disk_flush_all(SV_DiskController* fdc) {
    for (int i = 0; i < SV_DISK_MAX_DRIVES; i++) {
        sv_disk_flush(fdc, i);
    }
}

bool sv_disk_read_sector_raw(SV_DiskController* fdc, uint8_t drive,
                             uint8_t track, uint8_t sector, uint8_t* dst) {
    if (drive >= SV_DISK_MAX_DRIVES) return false;
    SV_DiskImage* img = &fdc->drives[drive];
    if (!img->mounted || !img->file) return false;
    return disk_read_sector(img, track, sector, dst);
}

#endif // DISK_ENABLED
