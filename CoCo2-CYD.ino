/*
 * =============================================================
 *   CoCo2-CYD Beta-1 March 2026 - CoCo 2 Emulator for ESP32 CYD
 *   (C) 2026 Reinaldo Torres / CoCo Byte Club
 *   https://github.com/reyco2000/CoCo2-CYD
 *   Based on XRoar Emulator by Ciaran Anscomb
 *   CO-developed with Claude Code (Anthropic)
 *   MIT License
 * =============================================================
 *  File   : CoCo2-CYD.ino
 *  Module : Main Arduino sketch — setup/loop entry point
 * =============================================================
 */

#include "config.h"
#include "src/core/machine.h"
#include "src/hal/hal.h"
#include "src/hal/usb_kbd_host.h"
#include "src/supervisor/supervisor.h"
#include "src/utils/debug.h"

// Uncomment to enable LOADM verification test (serial command 'R' to run)
// #define RUN_INTEGRATION_TESTS 1

#ifdef RUN_INTEGRATION_TESTS
#include "src/tests/integration_test.h"
#endif

Machine coco;

// Pinned CPU emulation task running on Core 0. Produces a render snapshot
// each frame; the Arduino loop (Core 1) consumes it via the frame_ready /
// render_done semaphores in machine.cpp.
static TaskHandle_t s_cpu_task_handle = nullptr;

static void cpu_task(void* /*arg*/) {
    for (;;) {
        if (!machine_emulation_is_enabled()) {
            // Supervisor (OSD) active — pause emulation, frame-aligned.
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        machine_run_frame_cpu_only(&coco);
        // Yield so FreeRTOS IDLE0 runs and the task watchdog stays happy.
        vTaskDelay(1);
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);

    DEBUG_PRINT("=================================");
    DEBUG_PRINT("CoCo2-CYD - Starting up...");
    DEBUG_PRINTF("CPU freq: %d MHz", ESP.getCpuFreqMHz());
    DEBUG_PRINT("----- Memory Report -----");
    DEBUG_PRINTF("SRAM  total: %d bytes", ESP.getHeapSize());
    DEBUG_PRINTF("SRAM  free:  %d bytes", ESP.getFreeHeap());
    DEBUG_PRINTF("SRAM  used:  %d bytes", ESP.getHeapSize() - ESP.getFreeHeap());
    DEBUG_PRINTF("SRAM  min free ever: %d bytes", ESP.getMinFreeHeap());
#if USE_PSRAM
    DEBUG_PRINTF("PSRAM total: %d bytes", ESP.getPsramSize());
    DEBUG_PRINTF("PSRAM free:  %d bytes", ESP.getFreePsram());
    DEBUG_PRINTF("PSRAM used:  %d bytes", ESP.getPsramSize() - ESP.getFreePsram());
    DEBUG_PRINTF("PSRAM min free ever: %d bytes", ESP.getMinFreePsram());
#endif
    DEBUG_PRINT("-------------------------");
    DEBUG_PRINT("=================================");

    // Initialize HAL audio + keyboard (storage and video deferred — see below)
    hal_init();

    // Init display BEFORE machine_init: sprite needs a contiguous 98 KB block.
    // machine_init() allocates 64 KB for machine RAM which fragments the heap,
    // so the sprite must be claimed first.
    hal_video_init();

    // Init SD AFTER video: TFT_eSPI::begin() routes VSPI to default pins
    // (18/19/23/5) which collide with the SD card's HSPI pins. Initializing
    // storage last lets HSPI take those GPIO outputs back cleanly.
    hal_storage_init();

    // Initialize emulated machine (allocates 64 KB machine RAM)
    machine_init(&coco);

    // Load/validate ROM images (embedded in flash on CYD — no SD reads needed)
    if (!machine_load_roms(&coco)) {
        DEBUG_PRINT("WARNING: ROM loading failed - running without ROMs");
    }

    // Cold reset
    machine_reset(&coco);

    // Initialize supervisor (OSD menu, disk controller, NVS)
    supervisor_init(&coco);
    supervisor_load_state();  // Auto-mount last disks if enabled
    hal_keyboard_set_machine(&coco);

    DEBUG_PRINT("=== Post-Init Memory Report ===");
    DEBUG_PRINTF("SRAM  free:  %d bytes (used: %d)", ESP.getFreeHeap(), ESP.getHeapSize() - ESP.getFreeHeap());
#if USE_PSRAM
    DEBUG_PRINTF("PSRAM free:  %d bytes (used: %d)", ESP.getFreePsram(), ESP.getPsramSize() - ESP.getFreePsram());
#endif
    DEBUG_PRINT("===============================");

#if USE_USB_HOST
    // Wait for USB keyboard to enumerate (up to 3 seconds)
    {
        uint32_t kbd_wait_start = millis();
        while (!hid_host_is_connected() && (millis() - kbd_wait_start) < 3000) {
            delay(50);
        }
        if (hid_host_is_connected()) {
            DEBUG_PRINTF("USB Keyboard connected (%lu ms)", millis() - kbd_wait_start);
        } else {
            DEBUG_PRINT("USB Keyboard not detected (timeout) - will connect when plugged in");
        }
    }
#else
    DEBUG_PRINT("Keyboard: no USB host on CYD (Phase 2: touch/BT/PS2)");
#endif

    // Allocate the dual-core render snapshot now (small enough to fit in the
    // post-init heap; placed last so SD/sprite/RAM init order is unchanged
    // from the working single-core layout). Pin the emulation task to Core 0
    // so SPI display pushes on Core 1 run in parallel — but only if the
    // snapshot was allocated. On low-memory failure the loop() falls back to
    // synchronous emulation.
    machine_init_render_handshake();
    DEBUG_PRINTF("Render snapshot: %s (heap free %d)",
        machine_get_render_snapshot() ? "OK" : "FAILED",
        ESP.getFreeHeap());

    if (machine_get_render_snapshot()) {
        xTaskCreatePinnedToCore(
            cpu_task, "cpu_emu", 8192, nullptr,
            2 /* priority above default */, &s_cpu_task_handle,
            0 /* core 0 */);
        DEBUG_PRINT("CPU emulation task pinned to Core 0");
    } else {
        DEBUG_PRINT("CPU task not started — falling back to single-core path");
    }

    DEBUG_PRINT("Entering main loop...");

#ifdef RUN_INTEGRATION_TESTS
    Serial.println("\n*** Integration Test Ready ***");
    Serial.println("Commands: R=run tests, S=report, D=VRAM hex, T=screen text");
#endif
}

#ifdef RUN_INTEGRATION_TESTS
static IntegrationTest itest(&coco);
#endif

void loop() {
#ifdef RUN_INTEGRATION_TESTS
    // Check for serial test commands
    if (Serial.available()) {
        char c = Serial.read();
        if (c == 'R' || c == 'r' || c == 'S' || c == 's' ||
            c == 'D' || c == 'd' || c == 'T' || c == 't') {
            itest.process_serial_command(c);
        }
    }
#endif

    // Process host input (keyboard, joystick — includes F1 intercept)
    hal_process_input();

    // Check if supervisor is handling this frame
    if (supervisor_update_and_render()) {
        // Supervisor is active — emulation paused (CPU task self-throttles
        // when machine_emulation_is_enabled() returns false).
        yield();
        return;
    }

    if (s_cpu_task_handle) {
        // Dual-core: consume one render snapshot from the Core-0 CPU task.
        // We release the snapshot (give render_done) as soon as we've copied
        // it into the sprite, BEFORE the slow pushSprite. That way Core 0
        // runs the next frame's CPU+VDG in parallel with our SPI push.
        SemaphoreHandle_t frame_ready = machine_get_frame_ready_sem();
        SemaphoreHandle_t render_done = machine_get_render_done_sem();
        if (frame_ready && xSemaphoreTake(frame_ready, pdMS_TO_TICKS(20)) == pdTRUE) {
            const render_snapshot_t* snap = machine_get_render_snapshot();
            bool need_push = false;
            if (snap && !hal_keyboard_osk_visible()) {
                need_push = hal_video_snapshot_fill_sprite(snap);
            }
            // Snapshot is now in the sprite buffer; let Core 0 reuse it.
            if (render_done) xSemaphoreGive(render_done);
            if (need_push) {
                hal_video_push_sprite_only();
            }
        }
    } else {
        // Single-core fallback (snapshot alloc failed): run CPU + present
        // synchronously on Core 1 as the project did originally.
        machine_run_frame(&coco);
        if (!hal_keyboard_osk_visible()) {
            hal_render_frame();
        }
    }
    hal_keyboard_draw_overlay();  // hotzone always visible after emulation frames

    // Sound frequency debug — detect end-of-sound and report
    hal_audio_debug_tick();
}
