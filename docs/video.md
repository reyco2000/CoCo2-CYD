# Video Subsystem — ESP32_CoCo2_XRoar_Port

## Overview

The video pipeline renders the MC6847 VDG (Video Display Generator) output to an SPI TFT display using the TFT_eSPI library. The VDG produces 256×
192 palette-indexed scanlines; the HAL converts these to RGB565 pixels and pushes them to the TFT via a sprite framebuffer.

## Hardware

| Parameter | Value |
|-----------|-------|
| Display types | ILI9341 (type 0), ST7789 (type 1), ST7796 (type 3) |
| Current config | ST7789, 320×240 (`DISPLAY_TYPE=1`) |
| SPI pins | CS=10, DC=2, RST=4, MOSI=11, SCLK=12 |
| Backlight | GPIO 5 (`PIN_TFT_BL`) |
| SPI speed | 40 MHz |
| Library | TFT_eSPI (configured via `User_Setup.h`) |

The ST7796 variant provides 480×320 resolution. All pin assignments and display type are in `config.h`.

## Pipeline

The video pipeline is **split across two ESP32 cores**. Core 0 (the
`cpu_emu` task) runs CPU + VDG emulation and captures a per-frame snapshot
of palette indices. Core 1 (the Arduino loop) unpacks the snapshot into the
sprite framebuffer, then pushes it over SPI.

```
Core 0 (cpu_emu task)                  Core 1 (Arduino loop)
─────────────────────────              ──────────────────────────────────
MC6847 VDG                             render_snapshot
  ├─ line_buffer[256]                    ├─ pixel_chunks[8] (4 bpp packed)
  ├─ pack to 4 bpp into snapshot         ├─ vram_chunks[2] (shadow source)
  └─ via snapshot_row_ptr()              └─ line_valid[192]

machine_run_frame_cpu_only:                          ┌──── frame_ready
  262 × machine_run_scanline                         │
  hal_audio_commit_frame                             ▼
  memcpy VRAM → vram_chunks ──────────►  hal_video_snapshot_fill_sprite:
  give frame_ready                         ├─ shadow compare (2 × memcmp)
  take render_done (blocks) ◄────────────  ├─ unpack 4 bpp → sprite FB
                                           │   (palette → RGB565)
                                           └─ give render_done ─────────┐
                                                                         │
                                       hal_video_push_sprite_only:       │
                                         └─ sprite->pushSprite() ◄───────┘
                                            (~20 ms SPI, runs in
                                             parallel with Core 0's
                                             next frame)
```

### Per-Scanline Flow (Core 0)

1. `machine_run_scanline()` runs CPU cycles for one scanline
2. For active scanlines (0–191), `mc6847_render_scanline()` fills
   `vdg.line_buffer[]` with palette indices (0–11)
3. The machine layer packs the 256 indices to 128 bytes (4 bpp, two pixels
   per byte) and writes them via `snapshot_row_ptr(snap, line)` into the
   correct heap chunk. `line_valid[line]` is set to 1.
4. (Legacy single-core fallback: `hal_video_render_scanline()` writes RGB565
   directly into the sprite framebuffer — used only when the Core-0 task
   wasn't created, e.g. snapshot alloc failure.)

### Per-Frame Flow (Core 1)

1. `xSemaphoreTake(frame_ready, 20 ms)` — wait for snapshot from Core 0
2. `hal_video_snapshot_fill_sprite(snap)`:
   - VRAM shadow compare (walks 2 × 3 KB chunks against `vram_shadow[6144]`)
   - If unchanged AND `force_push_count == 0`, returns false → no push
   - Otherwise unpack each `line_valid` row's 128 packed bytes into 256
     palette indices, then convert to RGB565 in the sprite via the existing
     `hal_video_render_scanline()` per row
3. `xSemaphoreGive(render_done)` — Core 0 may now overwrite the snapshot
4. `hal_video_push_sprite_only()` — `sprite->pushSprite(SPR_X, SPR_Y)` over
   SPI. Runs in parallel with Core 0's next CPU+VDG frame.

### Frame Timing and VRAM Shadow Compare (OPT-16)

- 262 scanlines per frame (NTSC), 192 active
- `hal_video_present()` implements **VRAM shadow compare** (OPT-16, 2026-03-26): before pushing, `memcmp()` compares the current CoCo VRAM region against a 6,144-byte shadow buffer. If VRAM content, VDG mode, and SAM display base are all unchanged, the SPI push is skipped entirely
- On mode or base change, a 10-frame force-push window ensures multi-frame screen setup (e.g., game title screens) is fully captured
- `hal_video_force_repaint()` sets `force_push_count = 3` to force TFT pushes on demand — called when the supervisor OSD closes so the emulated display repaints over OSD residue (fixes blank screen after F1 toggle under OS-9 or any static-screen scenario)
- **Replaces the old blind `FRAME_SKIP=1`** (which pushed every 2nd frame regardless of changes) with intelligent dirty detection — dirty frames push immediately, unchanged frames are free
- **Measured performance**:
  - Pre-dual-core (single-thread): 64 FPS text static, 45 FPS graphics static, 25–27 FPS graphics scrolling
  - Post-dual-core (Core 0 CPU / Core 1 SPI in parallel, 2026-05-12): **~70 FPS text static**, **~34 FPS graphics scrolling** — CPU and SPI push now overlap because `render_done` is released as soon as the sprite is filled, before `pushSprite` runs

VRAM region sizes by mode:
| Mode | VRAM bytes | memcmp cost |
|------|-----------|-------------|
| Text (32×16) | 512 | ~0.5 us |
| CG1-RG1 (GM 0-1) | 1,024 | ~1 us |
| CG2 (GM 2) | 2,048 | ~2 us |
| RG2 (GM 3) | 1,536 | ~1.5 us |
| CG3-RG3 (GM 4-5) | 3,072 | ~3 us |
| CG6-RG6 (GM 6-7) | 6,144 | ~5 us |

## Display Scale Modes

Configured via `DISPLAY_SCALE_MODE` in `config.h`. Currently set to **mode 0**.

### Mode 0: 1:1 Centered (default)

- Sprite size: 256×192 (native VDG resolution)
- Centered on 320×240 display with black borders
- Sprite position: `((320-256)/2, (240-192)/2)` = (32, 24)
- Fastest mode — no scaling math, direct palette lookup per pixel

### Mode 1: Scaled Fill

- Sprite size: full display (320×240 or 480×320)
- VDG 256×192 is nearest-neighbor stretched to `(DISPLAY_WIDTH - 2*BORDER)` × `(DISPLAY_HEIGHT - 2*BORDER)`
- `DISPLAY_BORDER=12` → inner area is 296×216
- Uses pre-computed lookup tables (`x_map[]`, `y_start[]`, `y_count[]`) for fast scaling

### Mode 2: Zoom Centered

- Fixed zoom factor: `DISPLAY_ZOOM_X10 / 10` (default 1.7×)
- Zoomed size: `256*1.7=435`, `192*1.7=326` → clamped to display bounds
- Centered with black borders, clipped if zoom exceeds display
- Uses same lookup-table infrastructure as mode 1

## Scaling Implementation

Modes 1 and 2 share pre-computed tables built at init:

- **`x_map[dx]`** — for each destination X pixel, which source VDG column to sample
- **`y_start[vy]`** / **`y_count[vy]`** — for each VDG source line, the starting destination row and how many destination rows it maps to

Per scanline, `hal_video_render_scanline()`:
1. Builds `scaled_line[]` — one row of INNER_W pixels using `x_map` and the byte-swapped palette
2. Writes `scaled_line` into the sprite framebuffer for each destination row (`y_count[line]` copies via `memcpy`)

## Palette

The MC6847 uses 12 colors (indices 0–11), stored in a 16-entry array. RGB565 values are derived from XRoar's "ideal" NTSC data sheet values:

| Index | Name | RGB565 | Approx RGB |
|-------|------|--------|------------|
| 0 | Green | 0x0FE1 | (10, 255, 10) |
| 1 | Yellow | 0xFFE8 | (255, 255, 67) |
| 2 | Blue | 0x20B6 | (34, 20, 180) |
| 3 | Red | 0xB024 | (182, 5, 34) |
| 4 | White/Buff | 0xFFFF | (255, 255, 255) |
| 5 | Cyan | 0x0EAE | (10, 212, 112) |
| 6 | Magenta | 0xF8FF | (255, 28, 255) |
| 7 | Orange | 0xFA01 | (255, 66, 10) |
| 8 | Black | 0x0841 | (9, 9, 9) |
| 9 | Dark Green | 0x0200 | (0, 65, 0) |
| 10 | Dark Orange | 0x6800 | (108, 0, 0) |
| 11 | Bright Orange | 0xFDA8 | (255, 180, 67) |

Two palette arrays are maintained:
- **`palette_rgb565[16]`** — standard RGB565 values
- **`palette_swapped[16]`** — byte-swapped for direct framebuffer writes (TFT_eSPI stores pixels in big-endian byte order in the sprite buffer)

## Sprite Framebuffer

- Created via `TFT_eSprite` from TFT_eSPI
- 16-bit color depth (RGB565)
- Attempts PSRAM allocation first, falls back to heap
- Size depends on scale mode: 256×192 (mode 0), 320×240 (mode 1), or zoom-dependent (mode 2)
- Memory: `W × H × 2` bytes (e.g., 256×192 = 98,304 bytes)
- Scanline rendering writes directly to the framebuffer via `getPointer()`, bypassing `drawPixel()` for performance

## FPS Overlay

Toggled via `hal_video_toggle_fps_overlay()` (mapped to F5 in supervisor).

- Counts every emulated frame (not just displayed frames) for accurate FPS
- Updates FPS value once per second
- Draws text directly on the TFT (not in the sprite) after `pushSprite()`, so it overlays the border area
- Uses TFT_eSPI font 2 at position (2, 2)

## Render Snapshot (Dual-Core Handoff)

Defined in `src/core/render_snapshot.h` and allocated by
`machine_init_render_handshake()` in `machine.cpp`. The snapshot is the
*only* shared mutable state between Core 0 and Core 1; access is serialized
through the `frame_ready` / `render_done` binary semaphores.

```c
typedef struct render_snapshot {
    uint8_t* pixel_chunks[8];   // 8 × 3 KB heap chunks, 4 bpp packed
    uint8_t* vram_chunks[2];    // 2 × 3 KB heap chunks (= 6 KB VRAM)
    uint8_t  line_valid[192];   // mc6847 set this when it rendered the row
    uint16_t vdg_base;
    uint8_t  vdg_mode;
    uint16_t vram_size;         // actual bytes used (mode-dependent)
    uint32_t frame_id;
} render_snapshot_t;
```

Why so many small chunks? The CYD has no PSRAM; after the 96 KB sprite +
64 KB machine RAM + SD/SDK overhead, the post-init heap is fragmented
enough that 6 KB+ contiguous blocks become scarce. On-device diagnostic
showed 4 × 6 KB chunks succeed but a 5th fails. 3 KB chunks reliably fit.

The pixel buffer holds **palette indices**, not RGB565 — 4 bits per pixel
(VDG palette uses ≤ 12 colors so 4 bits is sufficient). This halves the
buffer from 48 KB to 24 KB. Core 0 packs two indices per byte at scanline
render time; Core 1 unpacks them just before the per-row palette lookup.

## Key Functions

| Function | Purpose |
|----------|---------|
| `hal_video_init()` | Init TFT, create sprite, build palette and scale tables |
| `hal_video_render_scanline()` | Convert one VDG scanline to RGB565 in sprite FB |
| `hal_video_present(ram, vdg_base, vdg_mode)` | Legacy single-core path: VRAM shadow compare + conditional push (used only when CPU task wasn't created) |
| `hal_video_snapshot_fill_sprite(snap)` | Dual-core phase 1: shadow compare + unpack snapshot into sprite. Returns true if push needed |
| `hal_video_push_sprite_only()` | Dual-core phase 2: `pushSprite` only — no snapshot access, safe to overlap with Core-0 next frame |
| `hal_video_present_snapshot(snap)` | Wrapper that calls fill then push (legacy/fallback) |
| `hal_video_set_mode()` | Stub — mode changes handled by VDG/PIA directly |
| `hal_video_force_repaint()` | Force next 3 frames to push (invalidates VRAM shadow) |
| `hal_video_toggle_fps_overlay()` | Toggle FPS counter |
| `hal_video_get_tft()` | Expose TFT instance for supervisor OSD rendering |

## Files

- `ESP32_CoCo2_XRoar_Port/config.h` — display type, resolution, scale mode, pins, SPI speed
- `ESP32_CoCo2_XRoar_Port/src/hal/hal_video.cpp` — all video HAL implementation
- `ESP32_CoCo2_XRoar_Port/src/hal/hal.h` — HAL interface declarations
- `ESP32_CoCo2_XRoar_Port/src/core/mc6847.h` — VDG constants and structure
- `ESP32_CoCo2_XRoar_Port/src/core/mc6847.cpp` — VDG scanline rendering (palette index output)
- `ESP32_CoCo2_XRoar_Port/src/core/machine.cpp` — frame loop calling render_scanline + present
