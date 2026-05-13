# ESP32_CoCo2_XRoar_Port Performance Analysis Report

> **Baseline (pre-OPT-9):** ~25–27 FPS scrolling graphics, ~64 FPS text static
> **Current (post-OPT-9 dual-core):** ~34 FPS scrolling graphics, ~70 FPS text static
> **Target:** 30-35 FPS (practical target); DMA ceiling ~48 FPS (physical SPI limit)
> **Hardware:** ESP32 (CYD, no PSRAM); some sections below still reference the
> original ESP32-S3 N16R8 build and may be stale for the CYD port.
> **Last updated:** 2026-05-12
>
> **Implementation log:**
> - [x] OPT-3: Replace drawPixel() with direct FB write (2026-03-16)
> - [x] OPT-6: Set DEBUG_ENABLED=0 (2026-03-16) — **REGRESSED to 1, needs re-fix**
> - [x] OPT-4: Copy font ROM to DRAM (2026-03-16)
> - [x] OPT-1: IRAM_ATTR on hot CPU functions — TESTED, NOT NEEDED (2026-03-20)
> - [x] OPT-5: Precompute scanline cycle targets (2026-03-20)
> - [x] Branchless CC flag helpers (2026-03-21) — ~+2 FPS
> - [ ] OPT-6b: Re-fix DEBUG_ENABLED=0 regression
> - [x] OPT-11: Compiler -O2 for hot files (2026-03-25) — +1.5 FPS idle, +0.5 FPS graphics
> - [x] OPT-12: Reduce check_interrupts frequency — REJECTED (violates 6809 spec)
> - [ ] OPT-2: Split RAM: zero page + stack in DRAM
> - [ ] OPT-13: Inline machine_read/write fast path for RAM
> - [ ] OPT-14: Move ROM arrays to DRAM
> - [ ] OPT-15: Dirty-tile text mode optimization
> - [ ] OPT-7: Overlap USB wait with emulation — DEFERRED (keyboard needs full 3s enumeration window)
> - [x] OPT-16: VRAM shadow compare — skip SPI push on unchanged screen (2026-03-26) — +37 FPS text, +18 FPS static gfx
> - [ ] OPT-10: Adaptive frame skip
> - [x] **OPT-9: Dual-core rendering — CPU on Core 0, SPI push on Core 1** (2026-05-12) — **+9 FPS scrolling graphics (25→34), +6 FPS text (64→70)**
> - [ ] ~~OPT-8: Pack CPU flags into bitfield~~ — SKIP (negligible)
>
> **OPT-9 details:**
> - CPU emulation pinned to Core 0 via `xTaskCreatePinnedToCore` (`cpu_emu` task, 8 KB stack, priority 2).
> - Core 0 produces a render snapshot (4 bpp palette indices for 192×256 pixels in 8 × 3 KB heap chunks, plus 6 KB VRAM in 2 × 3 KB chunks) each frame.
> - Core 1 (`loop()`) consumes the snapshot via `frame_ready`/`render_done` binary semaphores. The renderer releases `render_done` **after** filling the sprite but **before** the slow `pushSprite`, so Core 0 starts the next frame in parallel with the ~20 ms SPI transfer.
> - Two concurrency hazards required attention:
>   1. `ram[]` accessed concurrently → solved by snapshotting VRAM region into the handshake struct.
>   2. Sprite framebuffer written by VDG concurrently with Core-1 `pushSprite` → solved by capturing palette indices into a separate snapshot pixel buffer; Core 1 converts to RGB565 in the sprite, then pushes.
> - Supervisor pauses Core 0 via a `machine_emulation_set_enabled(false)` flag plus a `frame_ready` drain at OSD activate.
> - Memory: snapshot = ~30 KB heap (split into 3 KB chunks to fit fragmented post-init heap; 6 KB+ single allocs unreliable on CYD).
>
> **Known issue:** Disk read corruption (LOAD/LOADM) occurred during past memory-related optimizations. Fixed, but any change to memory layout or machine_read/write must be validated with LOAD/LOADM.

---

## 1. Current Performance Profile

**Budget:** 16,667 us per frame at 60 FPS
**Actual:** ~38,462 us per frame at 26 FPS (2.3x over budget)

### Estimated Per-Frame Microsecond Breakdown

| Component | Est. us/frame | % of frame | Notes |
|-----------|--------------|------------|-------|
| **CPU emulation** (`execute_one()` x ~14,916 cycles) | ~20,000 | 52% | ~1.34 us/instruction avg, includes memory callbacks |
| **Memory access overhead** (PSRAM latency) | ~6,600 | 17% | ~170K accesses/frame, 65% to first 2 KB hitting PSRAM at 50-100 ns penalty |
| **Function pointer indirection** (`mem_read/write`) | ~2,550 | 7% | ~170K indirect calls x ~15 ns (pipeline flush) |
| **VDG rendering** (`mc6847_render_scanline` x 192) | ~4,500 | 12% | Font lookup + pixel expansion to RGB565 |
| **SPI frame push** (`pushSprite`) | ~1,750 avg | 5% | ~3,500 us every 2nd frame (FRAME_SKIP=1), blocks CPU |
| **check_interrupts overhead** | ~1,500 | 4% | Called every instruction, ~4,500 calls/frame x ~0.33 us |
| **SAM + PIA + FDC per-scanline** | ~1,500 | 4% | sv_disk_tick + sam6883 + PIA transitions |
| **Total** | **~38,400** | **230%** | **Need to cut to 16,667** |

### Top 5 Remaining Bottlenecks

1. **CoCo RAM in PSRAM** — 65% of ~170K memory accesses/frame hit PSRAM for zero page/stack/workspace (~6,600 us penalty)
2. **Function pointer indirection in memory access** — every mem_read/write goes through indirect call + address decode chain (~2,550 us)
3. **check_interrupts() called every instruction** — 3 boolean checks + function call overhead per instruction (~1,500 us)
4. ~~**SPI push blocks CPU emulation**~~ — **SOLVED by OPT-16** for static screens; still ~3,500 us per push for scrolling/active graphics
5. **Compiler -Os instead of -O2** — size optimization leaves performance on the table for switch/case dispatch, inlining, loop unrolling (~2,000-3,000 us estimated)

---

## 2. Optimization Recommendations (Prioritized by Impact/Effort)

### OPT-1: Add IRAM_ATTR to Hot CPU Functions — NOT NEEDED

- **Impact:** ~~HIGH (est. +8-12 FPS)~~ **NEGATIVE** — FPS dropped from 27 to 25
- **Effort:** Easy (add attribute annotations)
- **Risk:** ~~Low~~ **PROVEN HARMFUL**
- **Status:** TESTED 2026-03-20 — Added IRAM_ATTR to `execute_one()`, `execute_page2()`, `execute_page3()`, `check_interrupts()`, `mc6809_run()`, `machine_read()`, `machine_write()`, `mc6821_read()`, `mc6821_write()`, `pia_update_irq_a()`, `pia_update_irq_b()`. Build succeeded (42% flash, 7% DRAM) but FPS decreased by ~2. **Changes reverted.**
- **Root cause:** `execute_one()` is ~1,800 lines of switch/case generating a very large code block. Forcing it into IRAM consumed too much of the limited space, likely causing DRAM pressure and displacing data that was more critical to keep local. The ESP32-S3 flash cache already handles these hot code paths adequately at the current execution rate.
- **Conclusion:** IRAM_ATTR is only beneficial for small ISRs (like `audio_timer_isr()` which already has it). Do not apply to large functions.

### OPT-2: Move CoCo Zero Page + Stack to DRAM

- **Impact:** HIGH (est. +5-8 FPS)
- **Effort:** Medium (split allocation, add fast-path check in machine_read/write)
- **Risk:** Medium (must handle all edge cases in address decoding)
- **Disk I/O check:** REQUIRED — modifies machine_read/write. Must validate LOAD/LOADM after implementation.
- **Details:** Currently ALL 64KB of CoCo RAM is in PSRAM (~50-100 ns random access vs ~10 ns for DRAM). The 6809 spends ~60-70% of memory accesses in the first 2 KB:
  - Zero page ($0000-$00FF) — via direct page addressing mode
  - Stack ($01xx area) — every JSR, RTS, interrupt, PUSH, PULL
  - System variables ($0100-$0400) — BASIC workspace
  - Text screen ($0400-$05FF) — also in this range

  Allocate first 2 KB from DRAM (`malloc`), rest from PSRAM:
  ```cpp
  // In machine_init():
  m->ram_fast = (uint8_t*)malloc(2048);  // DRAM: $0000-$07FF
  m->ram_slow = (uint8_t*)ps_malloc(COCO_RAM_SIZE - 2048); // PSRAM: $0800+
  ```

  In `machine_read()`:
  ```cpp
  if (addr < 0x0800) return m->ram_fast[addr];        // DRAM fast path
  if (addr < COCO_RAM_SIZE) return m->ram_slow[addr - 0x0800]; // PSRAM
  ```

  PSRAM backing must stay synchronized for VDG rendering (which reads VRAM at $0400+). Writes must go to both `ram_fast` and the PSRAM array, OR VDG reads from `ram_fast` for addresses < 0x0800.

  Math: 65% of 170,000 accesses/frame x 60 ns PSRAM penalty = ~6,600 us. Even with dual-write overhead, net savings ~5,000+ us.
- **Constraints check:** No constraints violated. PSRAM disk cache unaffected.

### OPT-3: Replace drawPixel() with Direct Framebuffer Write (1:1 Mode) — DONE

- **Impact:** HIGH (est. +3-5 FPS)
- **Effort:** Easy (10 lines of code)
- **Risk:** Low
- **Status:** IMPLEMENTED 2026-03-16 — `hal_video.cpp:191-198`: replaced `drawPixel()` loop with direct `sprite->getPointer()` + `palette_swapped[]` row write.
- **Details:** Must use `palette_swapped` (byte-swapped RGB565) for direct framebuffer writes, as TFT_eSPI stores pixels in big-endian format in the sprite buffer.
- **Constraints check:** No constraints violated.

### OPT-4: Copy VDG Font ROM to DRAM — DONE

- **Impact:** MEDIUM (est. +1-3 FPS)
- **Effort:** Easy (3 lines)
- **Risk:** Low
- **Status:** IMPLEMENTED 2026-03-16 — `mc6847.cpp`: renamed PROGMEM array to `font_6847_flash`, added DRAM `font_6847[768]` copy via `memcpy_P()` in `mc6847_init()`, replaced `pgm_read_byte()` with direct array access. +768 bytes DRAM.
- **Constraints check:** No constraints violated.

### OPT-5: Precompute Per-Scanline Cycle Targets — DONE

- **Impact:** LOW-MEDIUM (est. +0.5-1 FPS)
- **Effort:** Easy (5 lines)
- **Risk:** Low
- **Status:** IMPLEMENTED 2026-03-20 — `machine.cpp`: added static `scanline_cycle_targets[262]` table (uint16_t, 524 bytes DRAM), precomputed in `machine_run_frame()` before scanline loop. `machine_run_scanline()` now does a single array lookup instead of multiply+divide. Flash reduced by ~1.8 KB.
- **Constraints check:** Cycle accuracy preserved — same fixed-point calculation, just moved out of hot loop.

### OPT-6: Disable DEBUG_ENABLED for Production Builds — DONE (REGRESSED)

- **Impact:** LOW-MEDIUM (est. +0.5-1 FPS)
- **Effort:** Easy (1 line change)
- **Risk:** None
- **Status:** IMPLEMENTED 2026-03-16, but **REGRESSED back to DEBUG_ENABLED=1** as of 2026-03-25. `config.h:34` currently reads `#define DEBUG_ENABLED 1`. Needs re-fix.
- **Note:** `mc6847_set_mode()` calls `DEBUG_PRINTF` on every VDG mode change, which fires during BASIC ROM initialization via SAM writes ($FFC0-$FFDF). Not a steady-state hot-path issue, but adds boot overhead.
- **Constraints check:** No constraints violated.

### OPT-7: Overlap USB Keyboard Wait with Emulation Start — DEFERRED

- **Impact:** LOW for FPS, HIGH for boot time (saves ~2-3 seconds)
- **Effort:** Medium
- **Risk:** Low (keyboard isn't needed until BASIC prompt)
- **Status:** DEFERRED 2026-03-28 — Tested: removing/reducing the 3s wait causes keyboard to miss initial keypresses. USB enumeration takes >1.5s after all other init completes (ESP_ERR_INVALID_ARG during late enumeration observed). The 3s wait is needed for reliable keyboard detection. Total boot is ~15s so 1.5s savings is marginal.
- **Details:** `ESP32_CoCo2_XRoar_Port.ino:59-70` blocks for up to 3 seconds waiting for USB keyboard. The emulated machine doesn't need keyboard until the BASIC prompt appears (~4 seconds into emulation). Start emulation immediately, check for keyboard in background:
  ```cpp
  // Remove the blocking wait loop entirely from setup()
  // Instead, in loop(), the first time hal_process_input() runs
  // it will naturally pick up the keyboard when it enumerates
  ```
  The USB Host HID runs on Core 0 and will enumerate independently. `hid_host_is_connected()` is already checked before using keyboard data.
- **Constraints check:** Does NOT violate constraint 2 — the 250ms bus reset + 150ms re-enum in `usb_kbd_host.cpp` remain unchanged. Only the 3-second blocking wait in the main loop is overlapped.

### OPT-8: Pack CPU Interrupt Flags into Bitfield — SKIP (NEGLIGIBLE)

- **Impact:** ~~LOW (est. +0.3-0.5 FPS)~~ **LIKELY NEGLIGIBLE** — OPT-1 results show the CPU struct is already well-cached in DRAM. The MC6809 struct is only 44 bytes (fits in a single cache line), so packing bools into a bitfield won't reduce cache misses.
- **Status:** SKIPPED — adds code complexity with negligible benefit.

### OPT-9: Move VDG Rendering + SPI Push to Core 0

- **Impact:** HIGH (est. +4-6 FPS)
- **Effort:** Hard (dual-core synchronization, double-buffering)
- **Risk:** High (race conditions, FreeRTOS overhead)
- **Details:** Currently Core 0 is nearly idle (only USB HID polling). Core 1 does CPU emulation + VDG render + SPI push sequentially. Architecture:
  1. Double-buffer the sprite framebuffer (two ~98 KB sprites in PSRAM)
  2. Core 1 runs CPU emulation, renders VDG scanlines into buffer A
  3. Core 0 pushes buffer B via SPI while Core 1 works on next frame
  4. At frame end, swap buffers using FreeRTOS semaphore (~1-2 us overhead)

  Net gain: the ~3,500 us SPI push becomes zero-cost on Core 1. VDG rendering (~4,500 us) can also be partially overlapped. Total: ~4,000-6,000 us savings.

  Trade-off: extra ~98 KB PSRAM for double buffer (plenty of free PSRAM).
- **Constraints check:** Audio ISR stays on Core 1 (timer-bound), cycle accuracy maintained.

### OPT-10: Adaptive Frame Skip

- **Impact:** LOW-MEDIUM (est. +1-2 FPS)
- **Effort:** Easy
- **Risk:** Low (visual quality tradeoff)
- **Details:** Current `FRAME_SKIP=1` pushes every 2nd frame. Make adaptive based on frame time:
  ```cpp
  if (frame_time_us > 20000) frame_skip = 2;      // Skip more when behind
  else if (frame_time_us < 15000) frame_skip = 0;  // Render more when ahead
  else frame_skip = 1;
  ```
- **Constraints check:** Audio remains synchronized to cycle count, not frame rendering.

### OPT-11: Compiler -O2 for Hot Files — DONE

- **Impact:** ~~MEDIUM (est. +2-4 FPS)~~ **MEASURED: +1.5 FPS idle BASIC, +0.5 FPS graphics mode**
- **Effort:** Easy (5 minutes, zero code changes)
- **Risk:** Low (no behavioral changes)
- **Status:** IMPLEMENTED 2026-03-25 — Added `#pragma GCC optimize("O2")` at the top of `mc6809.cpp` and `machine.cpp`. Flash increased from 42% to 43% (expected size/speed tradeoff). DRAM unchanged at 8%.
- **Details:** Arduino ESP32 default is `-Os` (optimize for size). `-O2` provides better inlining, switch/case jump table generation, and loop unrolling. The gain was lower than estimated, likely because the ESP32-S3 flash cache already handles the hot code paths well (consistent with OPT-1 findings).
- **Constraints check:** No constraints violated.

### OPT-12: Reduce check_interrupts Frequency — REJECTED

- **Impact:** ~~MEDIUM (est. +1-2 FPS)~~ **NOT SAFE**
- **Effort:** Easy
- **Risk:** ~~Low~~ **HIGH — violates 6809 hardware specification**
- **Status:** REJECTED 2026-03-25 — Analysis of 6809E hardware documentation shows this optimization is unsafe.
- **Root cause:** The MC6809E samples interrupt pins (NMI, FIRQ, IRQ) on the falling edge of Q clock and checks them **after every instruction completes**, before fetching the next opcode. This is not optional behavior — it is how the hardware works. Specifically:
  1. **NMI (disk controller):** The FDC's INTRQ connects directly to CPU NMI. When a disk command (READ/WRITE SECTOR) completes, NMI fires to pull the CPU out of the high-speed HALT-based data transfer loop. NMI is edge-triggered and latched — it only needs to be low for one cycle, but the CPU must check it promptly. Batching would delay NMI recognition, potentially corrupting disk I/O timing.
  2. **IRQ (60 Hz timer, keyboard):** Level-sensitive — must remain active until the CPU recognizes it. Batching could miss brief IRQ assertions.
  3. **FIRQ (cartridge):** Same level-sensitive requirement.
  4. **SYNC/CWAI instructions:** These halt the CPU and wait for an interrupt. The per-instruction check is essential for correct SYNC/CWAI behavior.
- **Conclusion:** `check_interrupts()` must be called after every `execute_one()`. The ~1,500 us/frame cost is the price of correct emulation. Optimization efforts should focus elsewhere (memory access speed, rendering offload).

### OPT-13: Inline machine_read/write Fast Path for RAM (NEW)

- **Impact:** HIGH (est. +3-5 FPS)
- **Effort:** Medium (2-3 hours)
- **Risk:** Medium — changes CPU/memory interface
- **Disk I/O check:** REQUIRED — modifies memory access path.
- **Details:** Current architecture has 3 layers of indirection per memory access:
  1. `mem_read(cpu, addr)` calls `cpu->read(addr)` via function pointer
  2. `machine_read(addr)` dereferences global `g_machine`
  3. Cascading if/else to determine address region

  Replace function pointer with direct call + inline fast path for RAM:
  ```cpp
  static inline uint8_t mem_read_fast(uint16_t addr) {
      // Fast path: RAM access (addr < 0x8000) — ~65% of all reads
      if (addr < 0x8000) return g_coco_ram[addr];
      // Slow path: ROM/IO
      return machine_read_slow(addr);
  }
  ```
  Eliminates indirect call for the common case (RAM reads), saving ~15 ns per access. Combined with OPT-2, RAM accesses become: one comparison + DRAM array access (~10 ns total vs current ~75 ns).
- **Constraints check:** No constraints violated. Must preserve all I/O address decoding for slow path.

### OPT-14: Move ROM Arrays to DRAM (NEW)

- **Impact:** LOW (est. +0.5-1 FPS)
- **Effort:** Easy (1 hour)
- **Risk:** Low
- **Disk I/O check:** REQUIRED — ROM-space reads change source pointer.
- **Details:** The 8K+8K+16K ROM arrays (32 KB total) are currently in PSRAM via `ps_malloc()`. ROM is read-only after boot. Use regular `malloc()` to place in DRAM instead. DRAM has ~200 KB free, so 32 KB is affordable. Eliminates PSRAM latency for all ROM fetches (opcode fetch from BASIC/ExtBAS ROM, vector reads).
- **Constraints check:** No constraints violated. Does not affect disk cache.

### OPT-15: Dirty-Tile Text Mode Optimization (NEW)

- **Impact:** MEDIUM (est. +1-2 FPS in text mode)
- **Effort:** Medium (2-3 hours)
- **Risk:** Low
- **Details:** In text mode (most common during BASIC), VDG re-renders all 32x16=512 character cells every frame. But at the BASIC prompt, very few characters change per frame.

  Add a 512-byte "previous VRAM" shadow buffer. At frame start, compare current VRAM ($0400-$05FF) with shadow. Only re-render character rows with changed bytes. For idle BASIC prompt, this skips 15 of 16 rows (~94% savings on VDG rendering = ~4,200 us saved). For graphics modes, skip optimization and render normally.
- **Constraints check:** No constraints violated. Does not affect cycle accuracy or audio.

### OPT-16: VRAM Shadow Compare — Skip SPI Push When Screen Unchanged — DONE

- **Impact:** ~~HIGH (est. +3-5 FPS average, up to +10 FPS on static screens)~~ **MEASURED: 64 FPS text, 45 FPS static graphics, 27 FPS scrolling graphics**
- **Effort:** Easy (1 hour)
- **Risk:** Low
- **Status:** IMPLEMENTED 2026-03-26 — `hal_video.cpp`: 6,144-byte VRAM shadow buffer + mode/base tracking. Replaces blind FRAME_SKIP with intelligent dirty detection. Added 10-frame force-push after mode/base changes to catch multi-frame title screen drawing (validated with ZAXXON). +6 KB DRAM (10% total).
- **Details:** Currently `hal_video_present()` pushes the full 98 KB sprite to TFT via SPI every 2nd frame (FRAME_SKIP=1), regardless of whether any pixel changed. The SPI push costs ~3,500 us per transfer (~1,750 us/frame average). On a static screen (BASIC `OK` prompt, waiting for input), this is entirely wasted.

  Add a 6,144-byte shadow VRAM buffer (max size for PMODE 4). Before each SPI push, `memcmp()` the current VDG display region against the shadow. If identical, skip `pushSprite()` entirely. If different, `memcpy()` to update shadow, then push.

  ```cpp
  // VRAM size lookup: bytes_per_row * height per GM value (from mc6847.cpp)
  static const uint16_t vram_sizes[8] = {1024,1024,2048,1536,3072,3072,6144,6144};

  // In hal_video_present(), before pushSprite — universal for ALL modes:
  uint16_t base = sam->vdg_base;                  // SAM F0-F6 display address
  uint8_t  mode = vdg->mode;                      // AG|GM|CSS packed byte
  uint16_t size = (mode & VDG_AG) ? vram_sizes[gm] : 512;  // text=512, graphics=table
  if (base == shadow_base && mode == shadow_mode &&
      memcmp(vram_shadow, ram + base, size) == 0)
      return;  // No change — skip SPI push (~3,500 us saved)
  memcpy(vram_shadow, ram + base, size);
  shadow_base = base;
  shadow_mode = mode;
  sprite->pushSprite(SPR_X, SPR_Y);
  ```

  VRAM region sizes by mode (from `mc6847.cpp` graphics mode table):
  | Mode | GM | bytes/row | rows | VRAM bytes | memcmp cost |
  |------|----|-----------|------|------------|-------------|
  | Text (32x16) | — (AG=0) | 32 | 16 | 512 | ~0.5 us |
  | CG1 (PMODE 0) | 0 | 16 | 64 | 1,024 | ~1 us |
  | RG1 | 1 | 16 | 64 | 1,024 | ~1 us |
  | CG2 (PMODE 1) | 2 | 32 | 64 | 2,048 | ~2 us |
  | RG2 (PMODE 2) | 3 | 16 | 96 | 1,536 | ~1.5 us |
  | CG3 (PMODE 3) | 4 | 32 | 96 | 3,072 | ~3 us |
  | RG3 | 5 | 16 | 192 | 3,072 | ~3 us |
  | CG6 | 6 | 32 | 192 | 6,144 | ~5 us |
  | RG6 (PMODE 4) | 7 | 32 | 192 | 6,144 | ~5 us |

  Edge cases handled by tracking `shadow_base` + `shadow_mode`:
  1. **VDG mode change** (CSS, GM, AG bits via PIA1/SAM) — screen appearance changes even if VRAM unchanged. `mode != shadow_mode` forces a push.
  2. **SAM display base change** (F0-F6 register writes at $FFC6-$FFD3) — display address shifts. `base != shadow_base` forces a push.
  3. **No separate text vs graphics paths** — same universal code for all modes. Only the `size` varies (512 for text, table lookup for graphics).

  Cost/benefit: `memcmp` of 6 KB from PSRAM = ~5 us worst case. SPI push = ~3,500 us. Even when the screen IS dirty, the 5 us overhead is negligible (0.14% of push cost). When static, saves the full 3,500 us.

  **Can replace FRAME_SKIP entirely:** Instead of blindly skipping 50% of frames, skip only truly unchanged frames. This gives better visual smoothness (dirty frames push immediately) with equal or fewer total SPI transfers.

  Combines well with OPT-15 (dirty-tile): OPT-16 skips the SPI push, OPT-15 skips the VDG rendering. Together they eliminate both rendering AND pushing costs for static screens.
- **Disk I/O check:** Not required — does not modify machine_read/write or memory layout.
- **Constraints check:** No constraints violated. Audio unaffected. Cycle accuracy preserved. Only affects SPI output path.

### Branchless CC Flag Helpers — DONE

- **Impact:** MEDIUM (~+2 FPS)
- **Status:** IMPLEMENTED 2026-03-21 — Replaced branching CC_PUT + ALU helpers with branchless compute-and-mask variants in `mc6809.cpp`. Contributed improvement from ~23.5 to ~25-27 FPS.

---

## 3. Quick Wins (< 1 hour each)

| # | Optimization | Est. FPS Gain | Effort | Status |
|---|-------------|---------------|--------|--------|
| ~~OPT-3~~ | ~~Replace drawPixel() with direct FB write~~ | ~~+3-5~~ | ~~Easy~~ | DONE |
| ~~OPT-6~~ | ~~Set DEBUG_ENABLED=0~~ | ~~+0.5-1~~ | ~~Easy~~ | DONE (regressed) |
| ~~OPT-4~~ | ~~Copy font ROM to DRAM~~ | ~~+1-3~~ | ~~Easy~~ | DONE |
| ~~OPT-5~~ | ~~Precompute scanline cycle targets~~ | ~~+0.5-1~~ | ~~Easy~~ | DONE |
| ~~CC~~ | ~~Branchless CC flag helpers~~ | ~~+2~~ | ~~Medium~~ | DONE |
| OPT-6b | Re-fix DEBUG_ENABLED=0 regression | +0.5 | 1 min | **TODO** |
| ~~OPT-11~~ | ~~`#pragma GCC optimize("O2")` on hot files~~ | ~~+2-4~~ +1.5 idle/+0.5 gfx | ~~5 min~~ | DONE |
| ~~OPT-12~~ | ~~Batch check_interrupts~~ | ~~+1-2~~ | ~~30 min~~ | **REJECTED** (violates 6809 spec) |
| ~~OPT-16~~ | ~~VRAM shadow compare — skip SPI on unchanged screen~~ | ~~+3-5 avg~~ **+37/+18/+0** | ~~1 hour~~ | DONE |
| OPT-10 | Adaptive frame skip | +1-2 | 30 min | **TODO** |

**Estimated gain from remaining quick wins: +5-9 FPS -> ~32.5-36.5 FPS**

## 4. Medium-Term Optimizations (1-4 hours each)

| # | Optimization | Est. FPS Gain | Complexity | Status |
|---|-------------|---------------|------------|--------|
| ~~OPT-1~~ | ~~IRAM_ATTR on hot CPU functions~~ | ~~+8-12~~ | ~~Easy~~ | **TESTED: -2 FPS. Rejected.** |
| OPT-2 | Split RAM: zero page + stack in DRAM | +5-8 | Modify machine_read/write | **TODO** |
| OPT-13 | Inline machine_read/write RAM fast path | +3-5 | Modify CPU/memory interface | **TODO** |
| OPT-14 | Move ROM arrays to DRAM (32 KB) | +0.5-1 | Change ps_malloc to malloc | **TODO** |
| OPT-15 | Dirty-tile text mode rendering | +1-2 | Add VRAM shadow buffer | **TODO** |
| OPT-7 | Overlap USB wait with emulation | boot -2-3s | Restructure setup() flow | **DEFERRED** — keyboard needs full 3s |

**Estimated gain from medium-term (with quick wins): ~38-48 FPS**

## 5. Major Refactors (> 4 hours, significant speedup)

| # | Optimization | Est. FPS Gain | Risk |
|---|-------------|---------------|------|
| OPT-9 | Dual-core: video rendering + SPI on Core 0 | +4-6 | High — synchronization, double-buffer |
| — | Computed goto dispatch (replace switch) | +1-3 | Medium — GCC extension, non-standard. OPT-1 results suggest flash cache handles switch well; benefit may be moderate |
| — | Flatten memory read (eliminate function pointer) | +3-6 | Medium — large refactor of address space (partially addressed by OPT-13) |
| — | Lazy flag evaluation (deferred CC computation) | +3-6 | High — must preserve all flag semantics (partially addressed by branchless CC helpers) |

## 6. Boot Time Improvements

**Current:** ~7 seconds to "DISK EXTENDED BASIC" prompt

### Boot Time Breakdown (Estimated)

| Phase | Time | Notes |
|-------|------|-------|
| Serial.begin + delay(500) | 500 ms | Fixed startup delay |
| hal_init (SD + audio + USB HID) | ~600 ms | SD init ~200ms, USB bus reset 250ms + re-enum 150ms |
| machine_init + ROM loading | ~300 ms | SD reads for 3 ROM files (24 KB total) |
| hal_video_init (TFT) | ~200 ms | SPI display init + fillScreen |
| machine_reset | ~10 ms | |
| supervisor_init + NVS load | ~100 ms | |
| USB keyboard wait (up to 3s) | 0-3000 ms | Blocks until keyboard or timeout |
| CoCo BASIC ROM cold-start | ~2000 ms | Emulated CPU init at 26 FPS (~0.43x real-time) |

### Boot Time Optimizations

| Optimization | Time Saved | Effort |
|-------------|-----------|--------|
| **OPT-7: Overlap USB wait** | ~1-3 s | Medium |
| **Reduce setup() `delay(500)`** | ~0.3 s | Easy (reduce to `delay(100)`) |
| **Increase SD SPI frequency** | ~0.05-0.1 s | Easy (raise from 4 MHz default to 20-25 MHz) |
| **Parallel ROM load + display init** | ~0.3-0.5 s | Medium (SD and TFT share SPI bus — need sequencing) |
| **Defer supervisor/NVS state load** | ~0.1-0.2 s | Easy |
| **Fix DEBUG_ENABLED regression** | ~0.05 s | Easy (eliminates DEBUG_PRINTF during boot SAM writes) |

**Estimated boot time after all optimizations: ~3-4 seconds**

---

## Implementation Priority Order

For maximum impact with minimum risk, implement in this order:

1. ~~**OPT-3** (drawPixel -> FB write)~~ — DONE
2. ~~**OPT-6** (DEBUG_ENABLED=0)~~ — DONE (regressed)
3. ~~**OPT-4** (font to DRAM)~~ — DONE
4. ~~**OPT-1** (IRAM_ATTR)~~ — **TESTED: harmful (-2 FPS), rejected**
5. ~~**OPT-5** (precompute cycle targets)~~ — DONE
6. ~~**Branchless CC flag helpers**~~ — DONE
7. **OPT-6b** (re-fix DEBUG_ENABLED=0 regression) — 1 minute
8. ~~**OPT-11** (compiler -O2 pragma on hot files)~~ — DONE (+1.5 idle/+0.5 gfx)
9. ~~**OPT-12** (batch check_interrupts)~~ — **REJECTED (violates 6809 spec)**
10. **OPT-2** (split RAM: zero page + stack in DRAM) — **highest single impact remaining**
11. **OPT-13** (inline RAM fast path) — combines well with OPT-2
12. **OPT-14** (ROM arrays to DRAM) — small gain, low risk
13. ~~**OPT-16** (VRAM shadow compare)~~ — **DONE (2026-03-26): 64 FPS text, 45 FPS static gfx, 27 FPS scrolling**
14. **OPT-10** (adaptive frame skip) — scroll-only benefit now (may help 27→29 FPS)
15. **OPT-15** (dirty-tile text mode) — marginal benefit now (text already 64 FPS)
16. **OPT-7** (overlap USB wait) — **DEFERRED** (keyboard needs full 3s enumeration)
17. ~~**OPT-8** (pack CPU flags)~~ — **negligible, skip**
18. **OPT-9** (dual-core rendering) — only if still under target FPS after above

---

## Projected FPS with All Remaining Optimizations

| Optimization | Est. FPS Gain | Cumulative |
|-------------|---------------|------------|
| Baseline (after OPT-11) | — | 27.5 FPS |
| **OPT-16: VRAM shadow compare** | **+37 text / +18 static gfx / +0 scroll** | **64 text / 45 static gfx / 27 scroll** |
| OPT-6b: Fix DEBUG_ENABLED regression | +0.5 | — |
| OPT-2: DRAM zero-page mirror | +5-8 | — |
| OPT-13: Inline RAM fast path | +3-5 | — |
| OPT-14: ROM to DRAM | +0.5-1 | — |
| OPT-10: Adaptive frame skip | +1-2 (scroll only) | — |
| OPT-15: Dirty-tile text mode | +1 (text only) | — |
| OPT-9: Core 0 video offload | +2-4 (scroll only) | — |

**Current measured (after OPT-16):**
- Text mode (static): **64 FPS** — exceeds DMA ceiling, CPU-bound frames are free
- Graphics mode (static): **45 FPS** — near DMA ceiling
- Graphics mode (scrolling VRAM): **27 FPS** — every frame is dirty, SPI is the bottleneck

**Key insight:** OPT-16 completely solved the static screen case. Remaining optimizations (OPT-2, OPT-13) now matter most for the **scrolling/active graphics** case (27 FPS) where every frame is dirty and both CPU emulation + SPI push cost are in play. OPT-9 (Core 0 SPI offload) becomes the highest-impact remaining optimization for scrolling, since it would overlap the ~3,500 us SPI push with CPU emulation.

---

## Key Findings Summary

### Memory Placement (Current State, updated 2026-03-25)

| Data | Location | Hot Path? | Recommendation | Status |
|------|----------|-----------|----------------|--------|
| MC6809 struct (44 bytes) | DRAM (stack/global) | Per-instruction | OK | OK |
| Machine struct (~600 bytes) | DRAM (global) | Per-instruction | OK | OK |
| CoCo RAM (64 KB) | PSRAM | Per-instruction | Split: first 2KB to DRAM | OPT-2 TODO |
| CoCo ROM (32 KB) | PSRAM | Per-fetch | Move to DRAM (32 KB affordable) | OPT-14 TODO |
| VDG font (768 bytes) | ~~Flash (PROGMEM)~~ DRAM | Per-scanline | ~~Copy to DRAM~~ | OPT-4 DONE |
| Opcode cycle table (256 bytes) | Flash (PROGMEM) | Not used at runtime | Ignore | N/A |
| TFT sprite buffer (~98 KB) | PSRAM | Per-frame | Must stay in PSRAM | OK |
| Disk images (~161 KB each) | PSRAM | On disk access | Must stay in PSRAM | OK |

### IRAM_ATTR Assessment (updated 2026-03-25)

IRAM_ATTR was tested on all hot CPU functions (OPT-1) and **proven harmful** (-2 FPS). The ESP32-S3 flash cache handles large hot functions adequately. Only `audio_timer_isr()` benefits from IRAM_ATTR (small ISR, already applied). **Do not apply IRAM_ATTR to large functions.**

### Build Configuration (updated 2026-03-25)

- **Compiler flags:** Arduino ESP32 default (`-Os` size optimization) — should add `-O2` via pragma (OPT-11)
- **No platformio.ini, build_opt.h, or custom build flags found**
- **DEBUG_ENABLED=1** — regressed, needs re-fix (OPT-6b)
- **No LTO enabled**
