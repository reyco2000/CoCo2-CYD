# ESP32_CoCo2_XRoar_Port Performance Analysis & Optimization Prompt

> **Usage:** Feed this prompt to Claude Code (or any LLM assistant) from the project root `/home/pi/proyectos/XRoar2026/`. Re-run after adding new features to catch regressions and find new optimization opportunities.

---

## Task

Perform a comprehensive performance analysis of the ESP32_CoCo2_XRoar_Port emulator and produce a prioritized list of optimizations to maximize emulation speed on ESP32-S3 (N16R8: 240 MHz dual-core, 8 MB PSRAM, 16 MB flash).

**Primary goal:** Reach 60 FPS (real-time 0.895 MHz MC6809 emulation) or as close as possible.
**Current baseline:** ~26 FPS (~0.43x real-time), boot to "DISK EXTENDED BASIC" prompt takes ~7 seconds.

---

## Optimizations Already Implemented

The following optimizations have been completed. Do NOT re-recommend these. Verify they haven't regressed and build on top of them.

### OPT-3: Replace drawPixel() with Direct Framebuffer Write (2026-03-16)
- `hal_video.cpp`: Replaced per-pixel `drawPixel()` loop with direct `sprite->getPointer()` + `palette_swapped[]` row write in 1:1 display mode. Must use byte-swapped RGB565 (`palette_swapped`) for direct framebuffer writes (TFT_eSPI stores pixels big-endian).

### OPT-4: Copy VDG Font ROM to DRAM (2026-03-16)
- `mc6847.cpp`: Renamed PROGMEM array to `font_6847_flash`, added DRAM copy `font_6847[768]` via `memcpy_P()` in `mc6847_init()`, replaced all `pgm_read_byte()` with direct array access. +768 bytes DRAM.

### OPT-5: Precompute Per-Scanline Cycle Targets (2026-03-20)
- `machine.cpp`: Added static `scanline_cycle_targets[262]` table (uint16_t, 524 bytes DRAM), precomputed in `machine_run_frame()` before scanline loop. `machine_run_scanline()` now does a single array lookup instead of multiply+divide per scanline. Cycle accuracy preserved.

### OPT-6: Disable DEBUG_ENABLED (2026-03-16)
- `config.h`: Changed `DEBUG_ENABLED` from 1 to 0. Eliminates debug print overhead and conditional checks in hot paths.

### Branchless CC Flag Helpers (2026-03-21)
- `mc6809.cpp`: Replaced branching CC_PUT + ALU helpers with branchless compute-and-mask variants. Contributed ~+2 FPS improvement (from ~23.5 to ~25-27 FPS).

---

## Optimizations Tested and Rejected

Do NOT re-recommend these. The rationale is documented below.

### OPT-1: IRAM_ATTR on Hot CPU Functions — TESTED, HARMFUL (2026-03-20)
- Added IRAM_ATTR to `execute_one()`, `execute_page2()`, `execute_page3()`, `check_interrupts()`, `mc6809_run()`, `machine_read()`, `machine_write()`, `mc6821_read()`, `mc6821_write()`, `pia_update_irq_a()`, `pia_update_irq_b()`. Build succeeded (42% flash, 7% DRAM) but **FPS dropped from 27 to 25** (-2 FPS). Changes reverted.
- **Root cause:** `execute_one()` is ~1,800 lines of switch/case. Forcing it into IRAM consumed too much limited space, causing DRAM pressure. The ESP32-S3 flash cache already handles these hot code paths adequately.
- **Conclusion:** IRAM_ATTR is only beneficial for small ISRs (like `audio_timer_isr()` which already has it). Do not apply to large functions.

### OPT-8: Pack CPU Interrupt Flags into Bitfield — LIKELY NEGLIGIBLE
- The MC6809 struct is only 44 bytes (fits in a single cache line). OPT-1 results confirm the CPU struct is already well-cached in DRAM. Packing bools into a bitfield adds code complexity with negligible benefit. Skip unless all higher-impact optimizations are exhausted.

---

## Known Issues from Past Optimizations

**Disk read corruption (LOAD/LOADM):** During implementation of some optimizations, disk reads produced corrupt data. The issue has been fixed, but any new optimization that touches memory layout, address decoding, or the machine_read/write path must be carefully tested with LOAD/LOADM commands to verify disk I/O integrity. Run a disk-based program after any memory-related changes.

---

## Constraints (DO NOT violate)

1. **Audio HAL must remain functional.** The LEDC PWM timer ISR at 22,050 Hz on GPIO17 (`hal_audio.cpp`) provides cycle-accurate audio for SOUND/PLAY commands. Do not remove, disable, or desynchronize it. Optimizations to audio are welcome (e.g., reducing ISR overhead, buffered DMA) as long as audible output remains correct.

2. **USB keyboard initialization waits are required.** The 250 ms bus reset + 150 ms re-enumeration in `usb_kbd_host.cpp` and the 3-second enumeration timeout in `ESP32_CoCo2_XRoar_Port.ino:59-70` are hardware requirements for USB Host HID. Do not reduce or remove these delays. Optimizations that overlap useful work with these waits are welcome.

3. **Cycle accuracy must be preserved for IRQ-dependent code.** The 60 Hz PIA0 CB1 timer IRQ drives SOUND, PLAY, TIMER, and the keyboard scanner. Per-scanline CPU budget via fixed-point accumulator (`machine.cpp`) must stay correct. Micro-optimizations inside the cycle loop are fine; removing cycle accounting is not.

4. **PSRAM disk cache must remain.** Entire disk images (~161 KB each) are cached in PSRAM to eliminate SD latency during emulation. Do not revert to per-sector SD reads.

5. **Target hardware is ESP32-S3 with Arduino framework.** Solutions must compile with `arduino-cli` using the esp32 board package. No ESP-IDF-only APIs unless wrapped for Arduino compatibility.

6. **Do not break existing working features:** display rendering, keyboard input, joystick, disk I/O (LOAD/LOADM must work correctly), supervisor/OSD menu (F1), FPS overlay (F5), or machine reset (F2).

7. **Disk I/O integrity after memory changes.** Any optimization that modifies memory layout, address decoding, or machine_read/write must be validated with LOAD and LOADM from disk to ensure no data corruption. This is a regression-prone area.

---

## Analysis Steps

### Step 1: Profile the Hot Path

Read and analyze the main emulation loop to identify where CPU time is spent:

- **`ESP32_CoCo2_XRoar_Port.ino`** — `loop()` entry point
- **`src/core/machine.cpp`** — `machine_run_frame()` and `machine_run_scanline()` (262 iterations/frame)
- **`src/core/mc6809.cpp`** — `mc6809_run()` inner loop and `execute_one()` — this is the single hottest function
- **`src/core/mc6809_opcodes.h`** — cycle tables (in PROGMEM — not used at runtime, can be ignored)
- **`src/core/mc6821.cpp`** — PIA read/write (called on every I/O access)
- **`src/core/sam6883.cpp`** — SAM address counter updates per scanline
- **`src/hal/hal_video.cpp`** — `hal_video_render_scanline()` (192 calls/frame) and `hal_video_present()` (SPI push)

For each component, estimate its per-frame cost in microseconds and identify:
- Unnecessary work per iteration (redundant checks, repeated calculations)
- Memory access patterns (PSRAM latency ~4-10x slower than DRAM)
- Function call overhead in tight loops
- Branch prediction misses in switch/case dispatchers

### Step 2: Analyze Memory Placement

ESP32-S3 memory hierarchy matters enormously:
- **IRAM** (~32 KB free): Fastest instruction execution — but OPT-1 showed large functions don't benefit (flash cache is adequate). Only useful for small ISRs.
- **DRAM** (~200 KB free): Fast data access (~10 ns)
- **PSRAM** (8 MB): ~50-100 ns random access, acceptable for bulk storage

Check current placement of:
- CPU state struct (`MC6809` — confirmed in DRAM, 44 bytes, well-cached)
- CoCo RAM (64 KB — in PSRAM via `ps_malloc()`). The 6809 spends ~60-70% of memory accesses in the first 2 KB (zero page, stack, BASIC workspace). Could these be split to DRAM?
- VDG character ROM and palette tables (font already copied to DRAM — see OPT-4)
- PIA register structs
- The TFT sprite framebuffer (~98 KB — must stay in PSRAM)
- Disk image caches (~161 KB each — must stay in PSRAM)

### Step 3: Evaluate CPU Emulation Optimizations

The MC6809 CPU emulator is the core bottleneck (~52% of frame time). Analyze `mc6809.cpp` for:

1. **Opcode dispatch mechanism** — Giant switch/case (~1,800 lines). Could computed goto be faster on Xtensa? Note: OPT-1 showed flash cache handles the switch adequately, so benefit may be moderate.
2. **Addressing mode decoding** — Are indexed/indirect modes computed inline or via function calls?
3. **Condition code (CC) flag updates** — Branchless CC helpers already implemented (2026-03-21). Are there further lazy evaluation opportunities?
4. **Memory read/write functions** — How many layers of indirection exist? Each function call in a tight loop costs ~10-20 ns on ESP32. Can the address decode fast path be inlined?
5. **Interrupt checking** — Is `check_interrupts()` called every instruction? Can it be deferred to once per N instructions or once per scanline?
6. **Common instruction sequences** — Can frequent opcode pairs (e.g., LDA+STA, LDX+STX) be fused?
7. **Compiler optimization flags** — Current: Arduino ESP32 default (`-Os`). Could `-O2` or `-O3` be set for performance-critical files? No `platformio.ini`, `build_opt.h`, or custom build flags exist.

### Step 4: Evaluate Video Rendering Optimizations

Read `hal_video.cpp` and `mc6847.cpp` to analyze:

1. **Frame skip value** — Currently `FRAME_SKIP=1` (renders every 2nd frame). Could adaptive frame skipping maintain audio sync while reducing rendering load when behind?
2. **SPI transfer** — Is `pushSprite()` using DMA? Could partial screen updates (dirty-rectangle tracking) reduce transfer volume?
3. **Character mode optimization** — In text mode (most common during BASIC), 32x16 character cells could be cached and only re-rendered when VRAM changes (dirty-tile tracking).
4. **Scanline rendering** — Could the VDG render directly into the sprite framebuffer instead of an intermediate buffer?
5. **Color depth** — Is 16-bit (RGB565) necessary? Could 8-bit indexed color reduce memory bandwidth?

Note: `drawPixel()` replacement (OPT-3) and font DRAM copy (OPT-4) are already done.

### Step 5: Evaluate Dual-Core Utilization

Currently:
- **Core 0:** USB Host HID tasks only (mostly idle between key events)
- **Core 1:** Everything else (emulation + rendering + audio ISR + input polling)

Could work be offloaded to Core 0?
- Render video on Core 0 while Core 1 runs CPU emulation (double-buffer scanline data)
- Run disk controller logic on Core 0
- Pre-decode next frame's character tiles on Core 0

Analyze synchronization overhead (FreeRTOS mutex/semaphore/queue costs) vs. parallelism gains.

### Step 6: Evaluate Boot Time Optimizations

Current boot to "DISK EXTENDED BASIC" takes ~7 seconds. Analyze:
- Can ROM loading be parallelized with display init?
- Can the USB keyboard wait (3 s) overlap with emulation start? (User doesn't need keyboard until prompt appears)
- Can SD card initialization be sped up (SPI frequency, fewer retries)?
- Can supervisor/NVS state loading be deferred?

### Step 7: Audit for Unnecessary Work

Scan all source files for:
- `DEBUG_PRINT`/`DEBUG_PRINTF` calls in hot paths (DEBUG_ENABLED=0 now, but verify no unconditional Serial output remains)
- `delay()` or `vTaskDelay()` calls in the emulation path
- Unnecessary `yield()` calls
- Floating-point math where integer math suffices
- Dynamic memory allocation (malloc/new) during emulation (should only happen at init)
- String formatting or Serial output in per-frame code

### Step 8: Compiler and Build-Level Optimizations

Check:
- Current compiler optimization level (Arduino ESP32 default is `-Os`)
- Could `-O2` or `-O3` be set for performance-critical files via build flags?
- Could link-time optimization (LTO) help?
- Is the ESP32-S3 CPU frequency set to 240 MHz? (Check `ESP.getCpuFreqMHz()`)
- Are there beneficial `__attribute__((always_inline))` annotations for small hot helpers?

Note: `IRAM_ATTR` has been proven harmful for large functions (see OPT-1). Only recommend it for small ISR-sized functions.

---

## Output Format

Produce a report with:

### 1. Current Performance Profile
- Estimated microsecond breakdown per frame (16,667 us budget at 60 FPS)
- Identification of the top 3-5 remaining bottlenecks with estimated time savings
- Comparison against the 26 FPS baseline (~38,462 us/frame actual)

### 2. Optimization Recommendations (Prioritized)

For each optimization:
- **Title:** One-line description
- **Impact:** Estimated FPS improvement (High/Medium/Low)
- **Effort:** Implementation difficulty (Easy/Medium/Hard)
- **Risk:** Chance of breaking something (Low/Medium/High)
- **Details:** What to change, which files, key code snippets
- **Disk I/O check:** If the change touches memory layout or address decoding, note that LOAD/LOADM testing is required
- **Constraints check:** Confirm it doesn't violate any constraint listed above

Order by impact-to-effort ratio (best bang-for-buck first).

Do NOT recommend optimizations listed in the "Already Implemented" or "Tested and Rejected" sections above.

### 3. Quick Wins (< 1 hour each)
### 4. Medium-Term Optimizations (1-4 hours each)
### 5. Major Refactors (> 4 hours, significant speedup)
### 6. Boot Time Improvements (separate section)

---

## Files to Read

At minimum, read these files in full before producing recommendations:

```
ESP32_CoCo2_XRoar_Port/config.h
ESP32_CoCo2_XRoar_Port/ESP32_CoCo2_XRoar_Port.ino
ESP32_CoCo2_XRoar_Port/src/core/mc6809.cpp
ESP32_CoCo2_XRoar_Port/src/core/mc6809.h
ESP32_CoCo2_XRoar_Port/src/core/mc6809_opcodes.h
ESP32_CoCo2_XRoar_Port/src/core/machine.cpp
ESP32_CoCo2_XRoar_Port/src/core/machine.h
ESP32_CoCo2_XRoar_Port/src/core/mc6821.cpp
ESP32_CoCo2_XRoar_Port/src/core/mc6821.h
ESP32_CoCo2_XRoar_Port/src/core/mc6847.cpp
ESP32_CoCo2_XRoar_Port/src/core/mc6847.h
ESP32_CoCo2_XRoar_Port/src/core/sam6883.cpp
ESP32_CoCo2_XRoar_Port/src/core/sam6883.h
ESP32_CoCo2_XRoar_Port/src/hal/hal.h
ESP32_CoCo2_XRoar_Port/src/hal/hal.cpp
ESP32_CoCo2_XRoar_Port/src/hal/hal_video.cpp
ESP32_CoCo2_XRoar_Port/src/hal/hal_audio.cpp
ESP32_CoCo2_XRoar_Port/src/hal/hal_keyboard.cpp
ESP32_CoCo2_XRoar_Port/src/hal/usb_kbd_host.cpp
ESP32_CoCo2_XRoar_Port/src/supervisor/sv_disk.cpp
ESP32_CoCo2_XRoar_Port/src/supervisor/sv_disk.h
```

Also check for any `platformio.ini`, `build_opt.h`, or Arduino build flag files that control compiler options (none existed as of 2026-03-20).

---

## Re-Run Checklist

When re-running this prompt after adding new features:

1. Update the "Current baseline" FPS number at the top
2. Move any newly completed optimizations to the "Already Implemented" section
3. Move any newly rejected optimizations to the "Tested and Rejected" section
4. Check if any new files were added that belong in the "Files to Read" list
5. Verify constraints still apply (new audio backend? different display? etc.)
6. Look for new hot paths introduced by the feature
7. Re-validate that previous optimizations haven't regressed
8. Test LOAD/LOADM after any memory-related changes (regression-prone area)
