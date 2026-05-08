/*
 * =============================================================
 *   CoCo2-CYD Beta-1 March 2026 - CoCo 2 Emulator for ESP32 CYD
 *   (C) 2026 Reinaldo Torres / CoCo Byte Club
 *   https://github.com/reyco2000/CoCo2-CYD
 *   Based on XRoar Emulator by Ciaran Anscomb
 *   CO-developed with Claude Code (Anthropic)
 *   MIT License
 * =============================================================
 *  File   : integration_test.h
 *  Module : Integration test framework interface — keyboard injection, VRAM diagnostics
 * =============================================================
 */

/*
 * CoCo ESP32 - Integration Test Framework
 *
 * Serial commands:
 *   'R' - run configured tests
 *   'S' - print last report
 *   'D' - dump VRAM hex
 *   'T' - dump screen as text
 */

#ifndef INTEGRATION_TEST_H
#define INTEGRATION_TEST_H

#include <stdint.h>
#include <stddef.h>
#include "../core/machine.h"

class IntegrationTest {
public:
    explicit IntegrationTest(Machine* m);

    // --- Tests ---
    bool test_basic_hello_world();
    bool test_basic_circle();

    // --- Screen helpers ---
    void inject_keystrokes(const char* text, int delay_frames = 3);
    bool wait_for_screen_text(const char* text, int timeout_frames = 360);
    void capture_vram_snapshot(uint8_t* out, size_t max_bytes);
    bool compare_vram_region(uint16_t offset, const uint8_t* expected, size_t len);

    // --- VRAM diagnostics ---
    void dump_vram_hex(int rows = 16);
    void dump_screen_text();

    // --- Run / report ---
    void run_all(bool stop_on_failure = false);
    void print_report();

    // --- Serial command interface ---
    void process_serial_command(char cmd);

    // --- Headless mode ---
    void set_headless(bool enabled) { headless = enabled; }
    bool is_headless() const { return headless; }

private:
    Machine* machine;
    int pass_count = 0;
    int fail_count = 0;
    bool headless = true;

    void run_frames(int count);
    bool ensure_booted();

    struct TestResult {
        const char* name;
        bool passed;
        uint32_t elapsed_frames;
        uint32_t elapsed_ms;
    };
    static const int MAX_TESTS = 16;
    TestResult results[MAX_TESTS];
    int result_count = 0;

    void record(const char* name, bool passed, uint32_t frames, uint32_t ms);

    // --- Keyboard injection internals ---
    struct KeyEvent {
        uint8_t row;
        uint8_t col;
        bool shift;
        bool pressed;
    };
    static const int KEY_QUEUE_SIZE = 512;
    KeyEvent key_queue[KEY_QUEUE_SIZE];
    int kq_head = 0;
    int kq_tail = 0;
    int kq_count = 0;

    int key_hold_frames = 3;
    int key_gap_frames = 2;
    int key_timer = 0;
    bool key_active = false;

    void queue_key_event(uint8_t row, uint8_t col, bool shift, bool pressed);
    void process_key_queue();
    void release_all_keys();
    void drain_key_queue(int settle_frames = 30);

    static bool ascii_to_matrix(char c, uint8_t& row, uint8_t& col, bool& shift_out);
    static char vram_to_ascii(uint8_t vram_byte);

    void capture_screen_text(char* buf, size_t buf_size);
    bool screen_contains(const char* text);

    uint32_t frame_counter = 0;
    bool boot_verified = false;
};

#endif // INTEGRATION_TEST_H
