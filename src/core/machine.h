/*
 * =============================================================
 *   CoCo2-CYD Beta-1 March 2026 - CoCo 2 Emulator for ESP32 CYD
 *   (C) 2026 Reinaldo Torres / CoCo Byte Club
 *   https://github.com/reyco2000/CoCo2-CYD
 *   Based on XRoar Emulator by Ciaran Anscomb
 *   CO-developed with Claude Code (Anthropic)
 *   MIT License
 * =============================================================
 *  File   : machine.h
 *  Module : CoCo 2 machine interface — CPU, memory map, I/O dispatch, and IRQ routing
 * =============================================================
 */

/*
 * machine.h - CoCo/Dragon system integration
 *
 * Wires together CPU, PIAs, VDG, and SAM into a complete machine.
 * Handles memory map, I/O dispatch, frame timing, and IRQ routing.
 *
 * CoCo 2 memory map (64K):
 *   $0000-$7FFF  RAM (32K visible, full 64K with SAM page select)
 *   $8000-$9FFF  Extended BASIC ROM (8K)
 *   $A000-$BFFF  Color BASIC ROM (8K)
 *   $C000-$FEFF  Cartridge ROM / Disk BASIC (16K max)
 *   $FF00-$FF1F  PIA0 (keyboard, joystick, hsync/vsync)
 *   $FF20-$FF3F  PIA1 (cassette, sound, VDG mode, cart IRQ)
 *   $FF40-$FF5F  Disk controller (if present)
 *   $FFC0-$FFDF  SAM control bits (write-only)
 *   $FFF0-$FFFF  Interrupt vectors (from ROM)
 */

#ifndef MACHINE_H
#define MACHINE_H

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "mc6809.h"
#include "mc6821.h"
#include "mc6847.h"
#include "sam6883.h"
#include "../supervisor/sv_disk.h"
#include "../../config.h"
#include "render_snapshot.h"

typedef struct Machine {
    // --- Core chips ---
    MC6809   cpu;
    MC6821   pia0;      // $FF00-$FF03: keyboard, joystick, hsync/vsync
    MC6821   pia1;      // $FF20-$FF23: cassette, sound, VDG mode, cart IRQ
    MC6847   vdg;
    SAM6883  sam;
    SV_DiskController fdc;  // $FF40-$FF5F: WD1793 floppy disk controller

    // --- Memory (allocated from PSRAM if available, else heap) ---
    uint8_t* ram;           // 64 KB main RAM
    const uint8_t* rom_basic;   // 8 KB Color BASIC ($A000-$BFFF)
    const uint8_t* rom_extbas;  // 8 KB Extended BASIC ($8000-$9FFF)
    const uint8_t* rom_cart;    // 16 KB Cartridge / Disk BASIC ($C000-$FEFF)

    size_t   ram_size;
    bool     rom_basic_loaded;
    bool     rom_extbas_loaded;
    bool     rom_cart_loaded;

    // --- Timing ---
    uint32_t cycles_per_frame;    // 14916 for NTSC
    uint32_t cycles_this_frame;
    uint32_t scanline;            // Current scanline (0-261)

    // --- Flags ---
    bool     initialized;
    bool     cart_inserted;
    bool     ntsc;                // true = 60 Hz NTSC, false = 50 Hz PAL
    uint32_t frame_count;
} Machine;

// Initialize machine (allocate memory, wire components)
void machine_init(Machine* m);

// Load ROM images from storage
// rom_path: base directory, e.g. "/roms"
bool machine_load_roms(Machine* m, const char* rom_path = ROM_BASE_PATH);

// Reset machine (cold reset)
void machine_reset(Machine* m);

// Run one video frame: 262 scanlines with interleaved CPU + VDG
// Single-core fallback path: runs cpu_only then presents synchronously.
void machine_run_frame(Machine* m);

// Run one frame's worth of CPU+VDG+audio and fill the render snapshot.
// Blocks on s_sem_render_done before starting and gives s_sem_frame_ready
// at the end. Designed for the Core-0 emulator task; the Core-1 loop is
// the consumer.
void machine_run_frame_cpu_only(Machine* m);

// Run one scanline (~57 CPU cycles + VDG render if active)
void machine_run_scanline(Machine* m);

// --- Dual-core render handshake -------------------------------------------
// Create the binary semaphores guarding the render snapshot. Must be called
// once after machine_init() and before the CPU task starts. render_done is
// primed (given) so the producer can fill the first frame immediately.
void machine_init_render_handshake(void);

// Accessors for the renderer (Core 1 loop).
const render_snapshot_t* machine_get_render_snapshot(void);
SemaphoreHandle_t        machine_get_frame_ready_sem(void);
SemaphoreHandle_t        machine_get_render_done_sem(void);

// Supervisor pause flag — checked at top of CPU task loop, frame-aligned.
void machine_emulation_set_enabled(bool en);
bool machine_emulation_is_enabled(void);

// Free all allocated memory
void machine_free(Machine* m);

// Memory access (used by CPU callbacks)
uint8_t machine_read(uint16_t addr);
void machine_write(uint16_t addr, uint8_t val);

#endif // MACHINE_H
