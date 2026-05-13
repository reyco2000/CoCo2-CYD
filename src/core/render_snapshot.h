/*
 * =============================================================
 *  File   : render_snapshot.h
 *  Module : Render snapshot — handoff buffer between the CPU task
 *           (Core 0) and the display renderer (Core 1).
 * =============================================================
 */

#ifndef RENDER_SNAPSHOT_H
#define RENDER_SNAPSHOT_H

#include <stdint.h>

// Palette-index pixel snapshot captured by the Core-0 CPU task during each
// frame. Pixels are packed 4 bits per pixel (two pixels per byte, low nibble
// = even x, high nibble = odd x). The VDG palette uses ≤ 12 colors so 4 bits
// is sufficient. Packing halves the buffer from 48 KB to 24 KB, which is
// critical on standard ESP32 (no PSRAM) — otherwise the 96 KB sprite
// allocation following us in setup() runs out of contiguous heap.
//
// VDG_ACTIVE_HEIGHT = 192, VDG_ACTIVE_WIDTH = 256 (hardcoded so this header
// stays self-contained — keep in sync with mc6847.h if those ever change).
// Pixels are stored as eight 3 KB heap chunks (24 rows × 128 bytes each).
// VRAM (for the OPT-16 shadow compare) is split into two 3 KB halves.
// Post-init heap on standard ESP32 is fragmented enough that even 6 KB
// chunks run out after a few allocs; 3 KB chunks reliably fit. Total
// snapshot ≈ 30 KB across 10 small allocations.
#define SNAPSHOT_PIXEL_CHUNKS  8
#define SNAPSHOT_CHUNK_ROWS    24
#define SNAPSHOT_ROW_BYTES     128                                  // 256 px @ 4 bpp
#define SNAPSHOT_CHUNK_BYTES   (SNAPSHOT_CHUNK_ROWS * SNAPSHOT_ROW_BYTES)  // 3072
#define SNAPSHOT_VRAM_CHUNKS   2
#define SNAPSHOT_VRAM_CHUNK    3072                                 // 2 × 3 KB = 6 KB

typedef struct render_snapshot {
    uint8_t* pixel_chunks[SNAPSHOT_PIXEL_CHUNKS];  // 8 × 3 KB
    uint8_t* vram_chunks[SNAPSHOT_VRAM_CHUNKS];    // 2 × 3 KB
    uint8_t  line_valid[192];
    uint16_t vdg_base;
    uint8_t  vdg_mode;
    uint16_t vram_size;
    uint32_t frame_id;
} render_snapshot_t;

static inline uint8_t* snapshot_row_ptr(render_snapshot_t* s, int line) {
    int q   = line / SNAPSHOT_CHUNK_ROWS;
    int sub = line % SNAPSHOT_CHUNK_ROWS;
    return &s->pixel_chunks[q][sub * SNAPSHOT_ROW_BYTES];
}

static inline const uint8_t* snapshot_row_cptr(const render_snapshot_t* s, int line) {
    int q   = line / SNAPSHOT_CHUNK_ROWS;
    int sub = line % SNAPSHOT_CHUNK_ROWS;
    return &s->pixel_chunks[q][sub * SNAPSHOT_ROW_BYTES];
}

#endif // RENDER_SNAPSHOT_H
