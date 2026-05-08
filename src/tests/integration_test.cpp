#include "../../config.h"
#if DISK_ENABLED  // Integration tests require disk subsystem

/*
 * =============================================================
 *   CoCo2-CYD Beta-1 March 2026 - CoCo 2 Emulator for ESP32 CYD
 *   (C) 2026 Reinaldo Torres / CoCo Byte Club
 *   https://github.com/reyco2000/CoCo2-CYD
 *   Based on XRoar Emulator by Ciaran Anscomb
 *   CO-developed with Claude Code (Anthropic)
 *   MIT License
 * =============================================================
 *  File   : integration_test.cpp
 *  Module : Integration test framework — keyboard injection and VRAM screen inspection
 * =============================================================
 */

/*
 * CoCo ESP32 - Integration Test Framework Implementation
 *
 * KEYBOARD MATRIX (verified from BASIC ROM KEYIN routine):
 * hal_keyboard_press(row, col) where row=PA bit, col=PB bit.
 * key_matrix[col] bit row = 0 when pressed (active LOW).
 * BASIC computes key_code = PA_row * 8 + PB_column.
 *
 *         PB0  PB1  PB2  PB3  PB4  PB5  PB6  PB7
 * PA0:     @    A    B    C    D    E    F    G
 * PA1:     H    I    J    K    L    M    N    O
 * PA2:     P    Q    R    S    T    U    V    W
 * PA3:     X    Y    Z   UP  DOWN LEFT RIGHT SPACE
 * PA4:     0    1    2    3    4    5    6    7
 * PA5:     8    9    :    ;    ,    -    .    /
 * PA6:   ENTER CLEAR BREAK ---  ---  ---  ---  SHIFT
 *
 * VRAM text screen: 32x16 chars at $0400, MC6847 internal charset.
 * VRAM byte encoding:
 *   $00-$1F -> characters '@' through '_' (uppercase + symbols)
 *   $20-$3F -> space through '?' (ASCII 0x20-0x3F directly)
 *   $40-$5F -> inverse '@' through '_'
 *   $60-$7F -> inverse space through '?'
 *   $80-$FF -> semigraphics-4 blocks
 */

#include "integration_test.h"
#include "../../config.h"
#include "../hal/hal.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <Arduino.h>

// CoCo text screen constants
static const uint16_t TEXT_SCREEN_ADDR = 0x0400;
static const int SCREEN_COLS = 32;
static const int SCREEN_ROWS = 16;
static const int SCREEN_SIZE = SCREEN_COLS * SCREEN_ROWS;

// CoCo SHIFT key: PA6, PB7
static const uint8_t SHIFT_ROW = 6;
static const uint8_t SHIFT_COL = 7;

// ============================================================================
// ASCII -> CoCo matrix position (verified from BASIC ROM KEYIN)
// ============================================================================

bool IntegrationTest::ascii_to_matrix(char c, uint8_t& row, uint8_t& col, bool& shift_out) {
    shift_out = false;
    if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';

    if (c == '@')              { row = 0; col = 0; return true; }
    if (c >= 'A' && c <= 'G')  { row = 0; col = 1 + (c - 'A'); return true; }
    if (c >= 'H' && c <= 'O')  { row = 1; col = c - 'H'; return true; }
    if (c >= 'P' && c <= 'W')  { row = 2; col = c - 'P'; return true; }
    if (c >= 'X' && c <= 'Z')  { row = 3; col = c - 'X'; return true; }
    if (c == ' ')              { row = 3; col = 7; return true; }
    if (c == '0')              { row = 4; col = 0; return true; }
    if (c >= '1' && c <= '7')  { row = 4; col = 1 + (c - '1'); return true; }
    if (c == '8')              { row = 5; col = 0; return true; }
    if (c == '9')              { row = 5; col = 1; return true; }
    if (c == ':')              { row = 5; col = 2; return true; }
    if (c == ';')              { row = 5; col = 3; return true; }
    if (c == ',')              { row = 5; col = 4; return true; }
    if (c == '-')              { row = 5; col = 5; return true; }
    if (c == '.')              { row = 5; col = 6; return true; }
    if (c == '/')              { row = 5; col = 7; return true; }
    if (c == '\n' || c == '\r') { row = 6; col = 0; return true; }

    // Shifted punctuation
    if (c == '!') { row = 4; col = 1; shift_out = true; return true; }
    if (c == '"') { row = 4; col = 2; shift_out = true; return true; }
    if (c == '#') { row = 4; col = 3; shift_out = true; return true; }
    if (c == '$') { row = 4; col = 4; shift_out = true; return true; }
    if (c == '%') { row = 4; col = 5; shift_out = true; return true; }
    if (c == '&') { row = 4; col = 6; shift_out = true; return true; }
    if (c == '\'') { row = 4; col = 7; shift_out = true; return true; }
    if (c == '(') { row = 5; col = 0; shift_out = true; return true; }
    if (c == ')') { row = 5; col = 1; shift_out = true; return true; }
    if (c == '*') { row = 5; col = 2; shift_out = true; return true; }
    if (c == '+') { row = 5; col = 3; shift_out = true; return true; }
    if (c == '<') { row = 5; col = 4; shift_out = true; return true; }
    if (c == '=') { row = 5; col = 5; shift_out = true; return true; }
    if (c == '>') { row = 5; col = 6; shift_out = true; return true; }
    if (c == '?') { row = 5; col = 7; shift_out = true; return true; }

    return false;
}

// ============================================================================
// VRAM byte -> ASCII
// ============================================================================

char IntegrationTest::vram_to_ascii(uint8_t vram_byte) {
    if (vram_byte & 0x80) return '#';
    uint8_t ch = vram_byte & 0x3F;
    if (ch < 0x20) return (char)(ch + 0x40);
    return (char)ch;
}

// ============================================================================
// Constructor
// ============================================================================

IntegrationTest::IntegrationTest(Machine* m) : machine(m) {
    memset(results, 0, sizeof(results));
    memset(key_queue, 0, sizeof(key_queue));
}

// ============================================================================
// Frame execution
// ============================================================================

void IntegrationTest::run_frames(int count) {
    for (int i = 0; i < count; i++) {
        process_key_queue();
        machine_run_frame(machine);
        frame_counter++;
    }
}

// ============================================================================
// Keyboard injection
// ============================================================================

void IntegrationTest::queue_key_event(uint8_t row, uint8_t col, bool shift, bool pressed) {
    if (kq_count >= KEY_QUEUE_SIZE) return;
    key_queue[kq_tail].row = row;
    key_queue[kq_tail].col = col;
    key_queue[kq_tail].shift = shift;
    key_queue[kq_tail].pressed = pressed;
    kq_tail = (kq_tail + 1) % KEY_QUEUE_SIZE;
    kq_count++;
}

void IntegrationTest::release_all_keys() {
    hal_keyboard_release_all();
    key_active = false;
    key_timer = 0;
}

void IntegrationTest::process_key_queue() {
    if (key_timer > 0) { key_timer--; return; }
    if (key_active) { release_all_keys(); key_timer = key_gap_frames; return; }
    if (kq_count == 0) return;

    KeyEvent ev = key_queue[kq_head];
    kq_head = (kq_head + 1) % KEY_QUEUE_SIZE;
    kq_count--;

    if (ev.pressed) {
        if (ev.shift) hal_keyboard_press(SHIFT_ROW, SHIFT_COL);
        hal_keyboard_press(ev.row, ev.col);
        key_active = true;
        key_timer = key_hold_frames;
    } else {
        hal_keyboard_release(ev.row, ev.col);
        if (ev.shift) hal_keyboard_release(SHIFT_ROW, SHIFT_COL);
    }
}

void IntegrationTest::drain_key_queue(int settle_frames) {
    while (kq_count > 0 || key_active || key_timer > 0) run_frames(1);
    if (settle_frames > 0) run_frames(settle_frames);
}

void IntegrationTest::inject_keystrokes(const char* text, int delay_frames) {
    key_hold_frames = delay_frames > 0 ? delay_frames : 3;

    Serial.printf("  [kbd] inject: \"");
    for (const char* p = text; *p; p++)
        Serial.printf("%c", (*p >= 0x20 && *p < 0x7F) ? *p : '.');
    Serial.printf("\"\n");

    while (*text) {
        uint8_t row, col;
        bool shift;
        if (ascii_to_matrix(*text, row, col, shift)) {
            queue_key_event(row, col, shift, true);
            queue_key_event(row, col, shift, false);
        }
        text++;
    }
}

// ============================================================================
// Screen text helpers
// ============================================================================

void IntegrationTest::capture_screen_text(char* buf, size_t buf_size) {
    if (!buf || buf_size == 0) return;
    int count = SCREEN_SIZE;
    if (count >= (int)buf_size) count = (int)buf_size - 1;
    for (int i = 0; i < count; i++)
        buf[i] = vram_to_ascii(machine->ram[TEXT_SCREEN_ADDR + i]);
    buf[count] = '\0';
}

bool IntegrationTest::screen_contains(const char* text) {
    char screen[SCREEN_SIZE + 1];
    capture_screen_text(screen, sizeof(screen));
    return strstr(screen, text) != nullptr;
}

bool IntegrationTest::wait_for_screen_text(const char* text, int timeout_frames) {
    for (int f = 0; f < timeout_frames; f++) {
        if (screen_contains(text)) return true;
        run_frames(1);
    }
    return false;
}

void IntegrationTest::capture_vram_snapshot(uint8_t* out, size_t max_bytes) {
    if (!out || max_bytes == 0) return;
    size_t len = (max_bytes < (size_t)SCREEN_SIZE) ? max_bytes : (size_t)SCREEN_SIZE;
    memcpy(out, &machine->ram[TEXT_SCREEN_ADDR], len);
}

bool IntegrationTest::compare_vram_region(uint16_t offset, const uint8_t* expected, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (machine->ram[TEXT_SCREEN_ADDR + offset + i] != expected[i])
            return false;
    }
    return true;
}

// ============================================================================
// VRAM diagnostics
// ============================================================================

void IntegrationTest::dump_vram_hex(int rows) {
    if (rows > SCREEN_ROWS) rows = SCREEN_ROWS;
    Serial.println("\n--- VRAM Hex Dump ($0400) ---");
    Serial.println("       +0 +1 +2 +3 +4 +5 +6 +7 +8 +9 +A +B +C +D +E +F  ASCII");
    for (int r = 0; r < rows; r++) {
        uint16_t addr = TEXT_SCREEN_ADDR + r * SCREEN_COLS;
        for (int half = 0; half < 2; half++) {
            uint16_t la = addr + half * 16;
            Serial.printf("$%04X: ", la);
            char ascii[17];
            for (int i = 0; i < 16; i++) {
                uint8_t b = machine->ram[la + i];
                Serial.printf("%02X ", b);
                ascii[i] = vram_to_ascii(b);
                if (ascii[i] < ' ' || ascii[i] > '~') ascii[i] = '.';
            }
            ascii[16] = '\0';
            Serial.printf(" %s\n", ascii);
        }
    }
    Serial.println("----------------------------");
}

void IntegrationTest::dump_screen_text() {
    Serial.println("\n--- Screen Text ---");
    Serial.println("+--------------------------------+");
    char line[SCREEN_COLS + 1];
    for (int r = 0; r < SCREEN_ROWS; r++) {
        for (int c = 0; c < SCREEN_COLS; c++) {
            line[c] = vram_to_ascii(machine->ram[TEXT_SCREEN_ADDR + r * SCREEN_COLS + c]);
            if (line[c] < ' ' || line[c] > '~') line[c] = '.';
        }
        line[SCREEN_COLS] = '\0';
        Serial.printf("|%s|\n", line);
    }
    Serial.println("+--------------------------------+");
}

// ============================================================================
// Ensure booted helper
// ============================================================================

bool IntegrationTest::ensure_booted() {
    if (boot_verified && screen_contains("OK")) {
        release_all_keys();
        for (int i = 0; i < 8; i++)
            machine->ram[0x0152 + i] = 0xFF;
        run_frames(15);
        return true;
    }
    machine_reset(machine);
    release_all_keys();
    frame_counter = 0;
    if (wait_for_screen_text("OK", 360)) {
        boot_verified = true;
        run_frames(15);
        return true;
    }
    return false;
}

// ============================================================================
// Result recording
// ============================================================================

void IntegrationTest::record(const char* name, bool passed, uint32_t frames, uint32_t ms) {
    if (result_count < MAX_TESTS) {
        results[result_count].name = name;
        results[result_count].passed = passed;
        results[result_count].elapsed_frames = frames;
        results[result_count].elapsed_ms = ms;
        result_count++;
    }
    if (passed) pass_count++; else fail_count++;
}

// ============================================================================
// Test: BASIC Hello World
// ============================================================================

bool IntegrationTest::test_basic_hello_world() {
    Serial.println("\n--- Test: BASIC Hello World ---");
    if (!ensure_booted()) {
        Serial.println("  [FAIL] Could not boot to BASIC prompt");
        return false;
    }
    inject_keystrokes("PRINT \"HELLO WORLD\"\n");
    bool ok = wait_for_screen_text("HELLO WORLD", 180);
    Serial.printf("  [%s] HELLO WORLD on screen\n", ok ? "PASS" : "FAIL");
    if (!ok) dump_screen_text();
    return ok;
}

// ============================================================================
// Test: BASIC Red Circle
// Draws a circle using PMODE 3 (128x96 4-color).
// CSS=0 color 3 = red. CIRCLE(128,96) is center of the logical 256x192 plane.
// Verification: after PCLS 0 zeros the buffer, any non-zero byte in the
// PMODE 3 page-1 graphics buffer ($1800-$23FF) confirms pixels were drawn.
// ============================================================================

bool IntegrationTest::test_basic_circle() {
    Serial.println("\n--- Test: BASIC Red Circle ---");
    if (!ensure_booted()) {
        Serial.println("  [FAIL] Could not boot to BASIC prompt");
        return false;
    }
    inject_keystrokes("10 PMODE 3,1:PCLS 0:SCREEN 1,0:CIRCLE(128,96),40,3\n");
    drain_key_queue(30);
    inject_keystrokes("20 FOR T=1 TO 500:NEXT T\n");
    drain_key_queue(30);
    inject_keystrokes("RUN\n");
    // drain typing + run 1800 frames (~30 s at 60 fps) for CIRCLE + FOR loop
    drain_key_queue(1800);

    // Scan PMODE 3 page-1 buffer ($1800-$23FF, 3072 bytes)
    bool found = false;
    for (uint16_t a = 0x1800; a <= 0x23FF; a++) {
        if (machine->ram[a] != 0x00) { found = true; break; }
    }
    Serial.printf("  [%s] Graphics pixels in PMODE buffer $1800-$23FF\n",
                  found ? "PASS" : "FAIL");
    if (!found) {
        // Dump first 32 bytes of the expected buffer for diagnosis
        Serial.print("  Buffer[$1800]: ");
        for (int i = 0; i < 32; i++) Serial.printf("%02X ", machine->ram[0x1800 + i]);
        Serial.println();
    }
    return found;
}

// ============================================================================
// Run all
// ============================================================================

void IntegrationTest::run_all(bool stop_on_failure) {
    pass_count = 0;
    fail_count = 0;
    result_count = 0;

    struct { const char* name; bool (IntegrationTest::*fn)(); } tests[] = {
        { "BASIC Hello World", &IntegrationTest::test_basic_hello_world },
        { "BASIC Red Circle",  &IntegrationTest::test_basic_circle      },
    };

    for (auto& t : tests) {
        uint32_t sm = millis();
        bool p = (this->*t.fn)();
        uint32_t em = millis() - sm;
        record(t.name, p, 0, em);
        Serial.printf("  %s — %s (%u ms)\n", t.name, p ? "PASS" : "FAIL", em);
        if (!p && stop_on_failure) break;
    }
}

// ============================================================================
// Print report
// ============================================================================

void IntegrationTest::print_report() {
    if (result_count == 0) {
        Serial.println("No test results. Run tests first (R).");
        return;
    }
    Serial.println("\n=== Last Test Report ===");
    for (int i = 0; i < result_count; i++) {
        Serial.printf("  [%s] %-22s %5u frames  %5u ms\n",
                      results[i].passed ? "PASS" : "FAIL",
                      results[i].name,
                      results[i].elapsed_frames,
                      results[i].elapsed_ms);
    }
    Serial.printf("Total: %d PASS  %d FAIL\n", pass_count, fail_count);
}

// ============================================================================
// Serial command interface
// ============================================================================

void IntegrationTest::process_serial_command(char cmd) {
    switch (cmd) {
        case 'R': case 'r':
            run_all(false);
            break;

        case 'S': case 's':
            print_report();
            break;

        case 'D': case 'd':
            dump_vram_hex(16);
            break;

        case 'T': case 't':
            dump_screen_text();
            break;
    }
}

#endif // DISK_ENABLED
