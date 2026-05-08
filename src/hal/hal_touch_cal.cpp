/*
 * =============================================================
 *   CoCo2-CYD Beta-1 March 2026 - CoCo 2 Emulator for ESP32 CYD
 *   (C) 2026 Reinaldo Torres / CoCo Byte Club
 *   https://github.com/reyco2000/CoCo2-CYD
 *   Based on XRoar Emulator by Ciaran Anscomb
 *   CO-developed with Claude Code (Anthropic)
 *   MIT License
 * =============================================================
 *  File   : hal_touch_cal.cpp
 *  Module : XPT2046 touch calibration — 6-point NVS-backed calibration with TFT UI
 * =============================================================
 */

#include "hal_touch_cal.h"
#include "hal.h"
#include "../../config.h"
#include "../supervisor/supervisor.h"
#include <Preferences.h>
#include <TFT_eSPI.h>

extern TFT_eSPI* hal_video_get_tft(void);

struct CalTarget { uint16_t sx; uint16_t sy; };
static const CalTarget CAL_TARGETS[6] = {
    {  10,  10 },  // top-left
    { 309,  10 },  // top-right
    { 160,  10 },  // top-center
    {  10, 229 },  // bottom-left
    { 309, 229 },  // bottom-right
    { 160, 229 },  // bottom-center
};

void touch_cal_load_nvs(void) {
    pinMode(0, INPUT_PULLUP);
#if !TOUCH_CAL_OVERRIDE
    Preferences prefs;
    prefs.begin("touch", true);
    if (prefs.isKey("x_min")) {
        hal_keyboard_set_touch_cal(
            (uint16_t)prefs.getUInt("x_min", TOUCH_X_MIN),
            (uint16_t)prefs.getUInt("x_max", TOUCH_X_MAX),
            (uint16_t)prefs.getUInt("y_min", TOUCH_Y_MIN),
            (uint16_t)prefs.getUInt("y_max", TOUCH_Y_MAX)
        );
    }
    prefs.end();
#endif
}

static uint32_t s_hold_start_ms  = 0;
static bool     s_counting       = false;
static int      s_last_sec_shown = -1;

static void draw_countdown(TFT_eSPI* tft, int seconds_remaining) {
    char buf[12];
    snprintf(buf, sizeof(buf), "CAL: %ds  ", seconds_remaining);
    tft->setTextColor(TFT_YELLOW, TFT_BLACK);
    tft->drawString(buf, 4, 4, 2);
}

static void clear_countdown(TFT_eSPI* tft) {
    tft->fillRect(0, 0, 80, 20, TFT_BLACK);
}

static void touch_cal_run(TFT_eSPI* tft);  // forward declaration

void touch_cal_check_boot_button(void) {
    bool held = (digitalRead(0) == LOW);

    if (held && !s_counting) {
        s_hold_start_ms  = millis();
        s_counting       = true;
        s_last_sec_shown = -1;
    }
    if (!held) {
        if (s_counting && !supervisor_is_active()) {
            TFT_eSPI* tft = hal_video_get_tft();
            if (tft) clear_countdown(tft);
        }
        s_counting       = false;
        s_last_sec_shown = -1;
        return;
    }

    uint32_t elapsed   = millis() - s_hold_start_ms;
    int      remaining = 5 - (int)(elapsed / 1000);

    if (remaining > 0 && remaining != s_last_sec_shown) {
        if (!supervisor_is_active()) {
            TFT_eSPI* tft = hal_video_get_tft();
            if (tft) draw_countdown(tft, remaining);
        }
        s_last_sec_shown = remaining;
    }

    if (elapsed >= 5000) {
        s_counting = false;
        TFT_eSPI* tft = hal_video_get_tft();
        if (tft) touch_cal_run(tft);
    }
}

static void draw_crosshair(TFT_eSPI* tft, uint16_t x, uint16_t y, uint16_t color) {
    tft->drawLine(x - 15, y,      x + 15, y,      color);
    tft->drawLine(x,      y - 15, x,      y + 15, color);
    tft->drawCircle(x, y, 8, color);
}

static void touch_cal_run(TFT_eSPI* tft) {
    tft->fillScreen(TFT_BLACK);
    tft->setTextDatum(MC_DATUM);
    tft->setTextColor(TFT_WHITE, TFT_BLACK);
    tft->drawString("Touch Calibration", 160, 100, 4);
    tft->setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft->drawString("Tap each target precisely", 160, 130, 2);
    delay(1500);

    uint16_t raw_xs[6], raw_ys[6];

    for (int i = 0; i < 6; i++) {
        uint16_t sx = CAL_TARGETS[i].sx;
        uint16_t sy = CAL_TARGETS[i].sy;

        tft->fillScreen(TFT_BLACK);
        tft->setTextColor(TFT_WHITE, TFT_BLACK);
        char label[12];
        snprintf(label, sizeof(label), "Point %d / 6", i + 1);
        tft->drawString(label, 160, 115, 2);
        draw_crosshair(tft, sx, sy, TFT_WHITE);

        uint16_t rx = 0, ry = 0;
        uint32_t deadline = millis() + 30000;
        while (!hal_keyboard_read_raw_xy(&rx, &ry)) {
            if (millis() > deadline) {
                tft->fillScreen(TFT_BLACK);
                tft->setTextColor(TFT_RED, TFT_BLACK);
                tft->drawString("Timeout! Restarting...", 160, 115, 2);
                delay(2000);
                ESP.restart();
            }
            delay(10);
        }
        raw_xs[i] = rx;
        raw_ys[i] = ry;

        uint32_t lift_deadline = millis() + 5000;
        while (hal_keyboard_read_raw_xy(&rx, &ry)) {
            if (millis() > lift_deadline) break;
            delay(10);
        }

        draw_crosshair(tft, sx, sy, TFT_GREEN);
        delay(300);
    }

    // raw_y (0x90) = horizontal -> TOUCH_X_MIN/MAX
    uint16_t x_min = min(raw_ys[0], raw_ys[3]);
    uint16_t x_max = max(raw_ys[1], raw_ys[4]);
    // raw_x (0xD0) = vertical -> TOUCH_Y_MIN/MAX
    uint16_t y_min = min(raw_xs[0], min(raw_xs[1], raw_xs[2]));
    uint16_t y_max = max(raw_xs[3], max(raw_xs[4], raw_xs[5]));

    Preferences prefs;
    prefs.begin("touch", false);
    prefs.putUInt("x_min", x_min);
    prefs.putUInt("x_max", x_max);
    prefs.putUInt("y_min", y_min);
    prefs.putUInt("y_max", y_max);
    prefs.end();

    tft->fillScreen(TFT_BLACK);
    tft->setTextColor(TFT_GREEN, TFT_BLACK);
    tft->drawString("Saved! Restarting...", 160, 115, 2);
    delay(2000);
    ESP.restart();
}
