# Memory Usage — CoCo2-CYD

**Hardware:** ESP32-2432S028R (standard ESP32, no PSRAM)  
**Total usable DRAM:** ~320 KB  
**PSRAM:** None (`USE_PSRAM=0`)

---

## Flash (PROGMEM) — Zero RAM Cost

ROMs are declared with `PROGMEM` and are memory-mapped directly from flash. The machine pointers (`m->rom_basic`, `m->rom_extbas`, `m->rom_cart`) point into flash — no copy is made into RAM.

| Source file | Size | CoCo address |
|---|---|---|
| `src/roms/bas13_rom.h` | 8 KB | `$A000–$BFFF` |
| `src/roms/extbas11_rom.h` | 8 KB | `$8000–$9FFF` |
| `src/roms/disk11_rom.h` | 8 KB | `$C000–$DFFF` |
| **Total** | **24 KB** | in flash only |

Setting `USE_EMBEDDED_ROMS=0` in `config.h` switches to SD card loading, which would require 24 KB of heap for these buffers.

---

## DRAM Static Segment — Always Allocated

These are compile-time globals placed in the BSS/data segment. They consume DRAM before `setup()` runs and are never freed.

| Symbol | Size | Source | Notes |
|---|---|---|---|
| `s_machine_ram[65536]` | 64 KB | `machine.cpp` | Emulated CoCo 64K RAM; static to avoid heap fragmentation |
| `vram_shadow[6144]` | 6 KB | `hal_video.cpp` | VRAM change detector — skips SPI push on static frames |
| `SV_DiskController` (in `Machine`) | ~400 B | `sv_disk.h` | Includes 256-byte `sector_buf` + WD1793 registers |
| `key_matrix[8]`, `deferred_releases[8×3]`, misc | ~100 B | `hal_keyboard.cpp` | Keyboard matrix state |
| **Total static** | **~70 KB** | | |

---

## DRAM Heap — Runtime Allocations

These are allocated with `malloc` / `new` at runtime. The largest one (`sprite`) is allocated early in `hal_video_init()` and must succeed before other allocations fragment the heap — which is why the sketch initialises video before `machine_init()`.

| Allocation | Size | When | Source |
|---|---|---|---|
| Video sprite (256×192×2 bytes RGB565) | 96 KB | `hal_video_init()` | `hal_video.cpp` — tries PSRAM first, falls back to heap; on CYD always heap |
| `SV_FileEntry[128]` file browser list | ~5 KB | First file browser open | `sv_filebrowser.cpp` — one `malloc`, never freed |
| `CoCo2Keyboard` OSK object | ~1 KB | First touch frame after video init | `hal_keyboard.cpp` — lazy init |
| **Total heap** | **~102 KB** | | |

---

## Summary

```
Region          Where     Size     Notes
─────────────────────────────────────────────────────────────
Embedded ROMs   Flash     24 KB    PROGMEM, zero RAM cost
─────────────────────────────────────────────────────────────
Static segment  DRAM      70 KB    Permanent (machine RAM 64K + VRAM shadow 6K)
Heap peak       DRAM     102 KB    Video sprite 96K + file browser 5K + OSK 1K
─────────────────────────────────────────────────────────────
Total DRAM used           172 KB   of ~320 KB available
Free heap (est.)          ~148 KB  after all init completes
─────────────────────────────────────────────────────────────
PSRAM                     0        Not present on standard CYD
```

---

## Allocation Order (Critical)

The init sequence in `setup()` is ordered to guarantee the 96 KB sprite allocation succeeds:

1. `hal_init()` — keyboard + audio init; no large allocations
2. `hal_video_init()` — **sprite allocated here (96 KB)**; must run before anything fragments the heap
3. `hal_storage_init()` — SD card; runs after TFT to reclaim VSPI GPIO pins
4. `machine_init()` — `s_machine_ram` is static so heap is not involved, but chips are wired here
5. `machine_load_roms()` — points ROM pointers to flash; no allocation
6. `supervisor_init()` — small struct init; file entry array deferred until first browse

---

## Disk I/O — No Cache

There is no disk image cache. `sv_disk.cpp` reads and writes sectors directly from the SD card via `seek() + read()/write()` using the 256-byte `sector_buf` already counted in the static segment. SD access latency is present on every sector operation during emulation.

---

## Adding PSRAM (Future)

If a PSRAM-equipped ESP32 board is used (e.g. ESP32 WROVER), set `USE_PSRAM=1` in `config.h`. `machine_alloc()` will then use `ps_malloc()` for ROM buffers (only relevant when `USE_EMBEDDED_ROMS=0`). The video sprite already tries `PSRAM_ENABLE` first and would move to PSRAM automatically, freeing ~96 KB of DRAM heap.
