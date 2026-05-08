/*
 * =============================================================
 *   CoCo2-CYD Beta-1 March 2026 - CoCo 2 Emulator for ESP32 CYD
 *   (C) 2026 Reinaldo Torres / CoCo Byte Club
 *   https://github.com/reyco2000/CoCo2-CYD
 *   Based on XRoar Emulator by Ciaran Anscomb
 *   CO-developed with Claude Code (Anthropic)
 *   MIT License
 * =============================================================
 *  File   : CoCo2Keyboard.h
 *  Module : On-screen CoCo 2 keyboard — header-only TFT keyboard with PIA matrix mapping
 * =============================================================
 */

/*
 * CoCo2Keyboard.h
 * 
 * On-screen TRS-80 Color Computer 2 keyboard for ESP32 CYD
 * (Cheap Yellow Display — ESP32-2432S028R)
 *
 * Header-only library. Simply #include this file and call the API.
 *
 * The keyboard renders the physical CoCo 2 layout (4 rows + spacebar)
 * and maps each key back to its PIA matrix position (PA row, PB col).
 *
 * Activate by touching the top-right corner of the screen.
 *
 * Dependencies: TFT_eSPI (must be pre-configured for your CYD)
 *
 * License: MIT
 */

#ifndef COCO2_KEYBOARD_H
#define COCO2_KEYBOARD_H

#include <TFT_eSPI.h>

// ============================================================================
// Configuration — override any of these BEFORE including this header
// ============================================================================

// Screen geometry (landscape 320x240)
#ifndef COCO_KB_SCREEN_W
#define COCO_KB_SCREEN_W       320
#endif
#ifndef COCO_KB_SCREEN_H
#define COCO_KB_SCREEN_H       240
#endif

// Activation hot-zone (top-right corner)
#ifndef COCO_KB_HOTZONE_X
#define COCO_KB_HOTZONE_X      280
#endif
#ifndef COCO_KB_HOTZONE_Y
#define COCO_KB_HOTZONE_Y      0
#endif
#ifndef COCO_KB_HOTZONE_W
#define COCO_KB_HOTZONE_W      40
#endif
#ifndef COCO_KB_HOTZONE_H
#define COCO_KB_HOTZONE_H      40
#endif

// Function key buttons (always visible, aligned left of KB indicator)
// Button visual: 32×18 at y=4, same style as KB indicator
#define COCO_KB_FNBTN_W        32
#define COCO_KB_FNBTN_H        18
#define COCO_KB_FNBTN_Y        4
#define COCO_KB_FNBTN_GAP      4      // gap between buttons
// Positions: KB is at (SCREEN_W - 36), buttons go left from there
#define COCO_KB_BTN_KB_X       (COCO_KB_SCREEN_W - 36)
#define COCO_KB_BTN_F5_X       (COCO_KB_BTN_KB_X - (COCO_KB_FNBTN_W + COCO_KB_FNBTN_GAP) * 1)
#define COCO_KB_BTN_F2_X       (COCO_KB_BTN_KB_X - (COCO_KB_FNBTN_W + COCO_KB_FNBTN_GAP) * 2)
#define COCO_KB_BTN_F1_X       (COCO_KB_BTN_KB_X - (COCO_KB_FNBTN_W + COCO_KB_FNBTN_GAP) * 3)

// Virtual function key ASCII codes (not part of CoCo matrix)
#define COCO_KEY_F1            0xF1
#define COCO_KEY_F2            0xF2
#define COCO_KEY_F5            0xF5

// Key sizing
#define COCO_KB_KEY_H          38     // key height in pixels
#define COCO_KB_SPACE_H        28     // spacebar height
#define COCO_KB_GAP            1      // horizontal gap between keys
#define COCO_KB_ROW_GAP        2      // vertical gap between rows
#define COCO_KB_RADIUS         3      // key corner radius
#define COCO_KB_START_X        6      // left margin for key rows
#define COCO_KB_START_Y        40     // top bezel height
#define COCO_KB_SPACE_X        64     // spacebar x position
#define COCO_KB_SPACE_W        190    // spacebar width

// Timing
#ifndef COCO_KB_DEBOUNCE_MS
#define COCO_KB_DEBOUNCE_MS    200    // debounce between key presses
#endif
#ifndef COCO_KB_HOLD_MS
#define COCO_KB_HOLD_MS        80     // how long key stays "held" in matrix
#endif
#ifndef COCO_KB_FLASH_MS
#define COCO_KB_FLASH_MS       100    // visual press flash duration
#endif

// Colors — authentic CoCo 2 palette (silver body, white keys)
#define COCO_KB_BODY           0xC618  // silver-gray case body
#define COCO_KB_BEZEL_DARK     0x9CD3  // darker border for bezel edge
#define COCO_KB_KEY_NORMAL     0xFFFF  // white keys
#define COCO_KB_KEY_PRESSED    0x07E0  // green CoCo phosphor flash
#define COCO_KB_KEY_BREAK      0xF800  // red BREAK key
#define COCO_KB_KEY_SHIFT_ACT  0x2E6A  // dark green when SHIFT active
#define COCO_KB_KEY_SPECIAL    0xEF5D  // slightly off-white for special keys
#define COCO_KB_KEY_TEXT       0x0000  // black text on keys
#define COCO_KB_KEY_TEXT2      0x8410  // gray for shifted chars
#define COCO_KB_KEY_BORDER     0xA514  // medium gray border
#define COCO_KB_KEY_HILITE     0xFFFF  // highlight edge (top/left)
#define COCO_KB_KEY_SHADOW     0x7BEF  // shadow edge (bottom/right)
#define COCO_KB_BREAK_TEXT     0xFFFF  // white text on red BREAK key
#define COCO_KB_LABEL_COLOR    0x4A49  // dark gray for bottom label text
#define COCO_KB_HOTZONE_BG     0x39E7  // dark indicator bg
#define COCO_KB_HOTZONE_TEXT   0x07E0  // green indicator text

// Top label colors
#define COCO_KB_DOT_RED        0xF800  // RGB indicator dot
#define COCO_KB_DOT_GREEN      0x07E0
#define COCO_KB_DOT_BLUE       0x001F

// ============================================================================
// Key types
// ============================================================================
#define COCO_KTYPE_NORMAL      0   // standard key (letter, number, symbol)
#define COCO_KTYPE_ARROW       1   // arrow key (drawn as triangle glyph)
#define COCO_KTYPE_BREAK       2   // red BREAK key
#define COCO_KTYPE_SHIFT       3   // SHIFT key (toggleable)
#define COCO_KTYPE_ENTER       4   // wide ENTER key
#define COCO_KTYPE_SPECIAL     5   // CLR, etc.
#define COCO_KTYPE_SPACE       6   // full-width spacebar

// Arrow directions for COCO_KTYPE_ARROW
#define COCO_ARROW_UP          0
#define COCO_ARROW_DOWN        1
#define COCO_ARROW_LEFT        2
#define COCO_ARROW_RIGHT       3

// ============================================================================
// Data structures
// ============================================================================

// Definition of a single physical key
struct CoCoKeyDef {
    const char* label;       // display text ("A", "BRK", etc.)
    const char* shiftLabel;  // shifted label shown small at top (NULL if none)
    uint8_t  matrixRow;      // PIA PA line (0–6)
    uint8_t  matrixCol;      // PIA PB line (0–7)
    uint8_t  width;          // pixel width of this key
    uint8_t  type;           // COCO_KTYPE_*
    uint8_t  arrowDir;       // arrow direction (only for COCO_KTYPE_ARROW)
};

// Event returned when a key is pressed
struct CoCoKeyEvent {
    uint8_t row;             // PA line (0–6)
    uint8_t col;             // PB line (0–7)
    bool    pressed;         // true = key down
    bool    shifted;         // SHIFT modifier was active
    char    ascii;           // resolved ASCII character (0 if modifier/special)
};

// Runtime rectangle for hit-testing a drawn key
struct CoCoKeyRect {
    int16_t x, y, w, h;
    uint8_t keyIndex;        // global index into flat key array
};

// ============================================================================
// Physical keyboard layout data
//
// CoCo keyboard matrix (ROM-verified):
//         PB0    PB1    PB2    PB3    PB4    PB5    PB6    PB7
// PA0:     @      A      B      C      D      E      F      G
// PA1:     H      I      J      K      L      M      N      O
// PA2:     P      Q      R      S      T      U      V      W
// PA3:     X      Y      Z     UP    DOWN   LEFT  RIGHT   SPACE
// PA4:     0      1      2      3      4      5      6      7
// PA5:     8      9      :      ;      ,      -      .      /
// PA6:   ENTER  CLEAR  BREAK  (n/a)  (n/a)  (n/a)  (n/a)  SHIFT
// ============================================================================

// Row 0: Numbers (13 keys, 12×22px + BREAK 31px = 307px total with gaps)
static const CoCoKeyDef COCO_ROW0[] = {
    {"1",   "!",  4, 1, 22, COCO_KTYPE_NORMAL, 0},
    {"2",   "\"", 4, 2, 22, COCO_KTYPE_NORMAL, 0},
    {"3",   "#",  4, 3, 22, COCO_KTYPE_NORMAL, 0},
    {"4",   "$",  4, 4, 22, COCO_KTYPE_NORMAL, 0},
    {"5",   "%",  4, 5, 22, COCO_KTYPE_NORMAL, 0},
    {"6",   "&",  4, 6, 22, COCO_KTYPE_NORMAL, 0},
    {"7",   "'",  4, 7, 22, COCO_KTYPE_NORMAL, 0},
    {"8",   "(",  5, 0, 22, COCO_KTYPE_NORMAL, 0},
    {"9",   ")",  5, 1, 22, COCO_KTYPE_NORMAL, 0},
    {"0",   "", 4, 0, 22, COCO_KTYPE_NORMAL, 0},
    {":",   "*",  5, 2, 22, COCO_KTYPE_NORMAL, 0},
    {"-",   "=",  5, 5, 22, COCO_KTYPE_NORMAL, 0},
    {"BRK", NULL, 6, 2, 31, COCO_KTYPE_BREAK,  0},
};

// Row 1: Top letters (14 keys, all 21px = 307px total with gaps)
static const CoCoKeyDef COCO_ROW1[] = {
    {NULL,  NULL, 3, 3, 21, COCO_KTYPE_ARROW, COCO_ARROW_UP},     // UP
    {"Q",   NULL, 2, 1, 21, COCO_KTYPE_NORMAL, 0},
    {"W",   NULL, 2, 7, 21, COCO_KTYPE_NORMAL, 0},   // PA2,PB7
    {"E",   NULL, 0, 5, 21, COCO_KTYPE_NORMAL, 0},
    {"R",   NULL, 2, 2, 21, COCO_KTYPE_NORMAL, 0},
    {"T",   NULL, 2, 4, 21, COCO_KTYPE_NORMAL, 0},
    {"Y",   NULL, 3, 1, 21, COCO_KTYPE_NORMAL, 0},
    {"U",   NULL, 2, 5, 21, COCO_KTYPE_NORMAL, 0},
    {"I",   NULL, 1, 1, 21, COCO_KTYPE_NORMAL, 0},
    {"O",   NULL, 1, 7, 21, COCO_KTYPE_NORMAL, 0},
    {"P",   NULL, 2, 0, 21, COCO_KTYPE_NORMAL, 0},
    {"@",   NULL, 0, 0, 21, COCO_KTYPE_NORMAL, 0},
    {NULL,  NULL, 3, 5, 21, COCO_KTYPE_ARROW, COCO_ARROW_LEFT},   // LEFT
    {NULL,  NULL, 3, 6, 21, COCO_KTYPE_ARROW, COCO_ARROW_RIGHT},  // RIGHT
};

// Row 2: Home row (13 keys, 11×21 + ENTER 36px + CLR 28px = 307px with gaps)
static const CoCoKeyDef COCO_ROW2[] = {
    {NULL,    NULL, 3, 4, 21, COCO_KTYPE_ARROW,   COCO_ARROW_DOWN},  // DOWN
    {"A",     NULL, 0, 1, 21, COCO_KTYPE_NORMAL,  0},
    {"S",     NULL, 2, 3, 21, COCO_KTYPE_NORMAL,  0},
    {"D",     NULL, 0, 4, 21, COCO_KTYPE_NORMAL,  0},
    {"F",     NULL, 0, 6, 21, COCO_KTYPE_NORMAL,  0},
    {"G",     NULL, 0, 7, 21, COCO_KTYPE_NORMAL,  0},
    {"H",     NULL, 1, 0, 21, COCO_KTYPE_NORMAL,  0},
    {"J",     NULL, 1, 2, 21, COCO_KTYPE_NORMAL,  0},
    {"K",     NULL, 1, 3, 21, COCO_KTYPE_NORMAL,  0},
    {"L",     NULL, 1, 4, 21, COCO_KTYPE_NORMAL,  0},
    {";",     "+",  5, 3, 21, COCO_KTYPE_NORMAL,  0},
    {"ENTER", NULL, 6, 0, 36, COCO_KTYPE_ENTER,   0},
    {"CLR",   NULL, 6, 1, 28, COCO_KTYPE_SPECIAL, 0},
};

// Row 3: Bottom row (12 keys, 2×SHIFT 43px + 10×21px = 307px with gaps)
static const CoCoKeyDef COCO_ROW3[] = {
    {"SHIFT", NULL, 6, 7, 43, COCO_KTYPE_SHIFT,  0},   // left SHIFT
    {"Z",     NULL, 3, 2, 21, COCO_KTYPE_NORMAL, 0},
    {"X",     NULL, 3, 0, 21, COCO_KTYPE_NORMAL, 0},
    {"C",     NULL, 0, 3, 21, COCO_KTYPE_NORMAL, 0},
    {"V",     NULL, 2, 6, 21, COCO_KTYPE_NORMAL, 0},   // PA2,PB6
    {"B",     NULL, 0, 2, 21, COCO_KTYPE_NORMAL, 0},
    {"N",     NULL, 1, 6, 21, COCO_KTYPE_NORMAL, 0},
    {"M",     NULL, 1, 5, 21, COCO_KTYPE_NORMAL, 0},
    {",",     "<",  5, 4, 21, COCO_KTYPE_NORMAL, 0},
    {".",     ">",  5, 6, 21, COCO_KTYPE_NORMAL, 0},
    {"/",     "?",  5, 7, 21, COCO_KTYPE_NORMAL, 0},
    {"SHIFT", NULL, 6, 7, 43, COCO_KTYPE_SHIFT,  0},   // right SHIFT
};

// Row 4: Spacebar (1 key)
static const CoCoKeyDef COCO_ROW4[] = {
    {"SPACE", NULL, 3, 7, COCO_KB_SPACE_W, COCO_KTYPE_SPACE, 0},
};

// Row metadata
#define COCO_NUM_VROWS   5
static const CoCoKeyDef* COCO_ROWS[]   = {COCO_ROW0, COCO_ROW1, COCO_ROW2, COCO_ROW3, COCO_ROW4};
static const uint8_t     COCO_ROW_CNT[] = {13, 14, 13, 12, 1};
static const int16_t     COCO_ROW_Y[]   = {40, 80, 120, 160, 200};
#define COCO_KB_TOTAL_KEYS 53

// ============================================================================
// ASCII lookup tables (CoCo ROM-accurate)
// ============================================================================
static const char COCO_ASCII_NORMAL[7][8] = {
    {'@','A','B','C','D','E','F','G'},       // PA0
    {'H','I','J','K','L','M','N','O'},       // PA1
    {'P','Q','R','S','T','U','V','W'},       // PA2
    {'X','Y','Z', 0x5E, 0x0A, 0x08, 0x09, 0x20}, // PA3: X Y Z ^ LF BS TAB SPC
    {'0','1','2','3','4','5','6','7'},       // PA4
    {'8','9',':',';',',','-','.','/'},       // PA5
    {0x0D, 0x0C, 0x03, 0, 0, 0, 0, 0},     // PA6: ENTER CLEAR BREAK _ _ _ _ SHIFT
};

static const char COCO_ASCII_SHIFTED[7][8] = {
    {'@','A','B','C','D','E','F','G'},       // PA0: letters unchanged on CoCo 2
    {'H','I','J','K','L','M','N','O'},       // PA1
    {'P','Q','R','S','T','U','V','W'},       // PA2
    {'X','Y','Z', 0x5E, 0x0A, 0x08, 0x09, 0x20}, // PA3: same
    {'0','!','"','#','$','%','&','\''},      // PA4: shifted numbers
    {'(',')', '*','+','<','=','>','?'},      // PA5: shifted symbols
    {0x0D, 0x0C, 0x03, 0, 0, 0, 0, 0},     // PA6: same
};

// ============================================================================
// CoCo2Keyboard class
// ============================================================================

class CoCo2Keyboard {
public:
    // Initialize the keyboard (call once in setup())
    void begin(TFT_eSPI* tft) {
        _tft = tft;
        _visible = false;
        _shiftActive = false;
        _wasTouched = false;
        _keyReady = false;
        _keyHeld = false;
        _lastTouchTime = 0;
        _flashKeyIdx = -1;
        _flashTime = 0;
        _numRects = 0;
        memset(&_lastEvent, 0, sizeof(_lastEvent));
    }

    // Call every loop iteration with touch state
    void update(uint16_t touchX, uint16_t touchY, bool touched) {
        // Handle flash animation expiry
        _updateFlash();

        if (!touched) {
            _wasTouched = false;
            return;
        }

        // Only process new touches (finger just went down)
        if (_wasTouched) return;

        uint32_t now = millis();
        if (now - _lastTouchTime < COCO_KB_DEBOUNCE_MS) return;

        _wasTouched = true;
        _lastTouchTime = now;

        // --- Function button checks (always active) ---
        if (_checkFnButton(touchX, touchY, COCO_KB_BTN_F1_X, COCO_KEY_F1)) return;
        if (_checkFnButton(touchX, touchY, COCO_KB_BTN_F2_X, COCO_KEY_F2)) return;
        if (_checkFnButton(touchX, touchY, COCO_KB_BTN_F5_X, COCO_KEY_F5)) return;

        // --- Hot-zone check (KB toggle) ---
        if (touchX >= COCO_KB_HOTZONE_X &&
            touchX <  COCO_KB_HOTZONE_X + COCO_KB_HOTZONE_W &&
            touchY >= COCO_KB_HOTZONE_Y &&
            touchY <  COCO_KB_HOTZONE_Y + COCO_KB_HOTZONE_H) {
            if (_visible) hide(); else show();
            return;
        }

        if (!_visible) return;

        // --- Hit-test against drawn keys ---
        int idx = _hitTest(touchX, touchY);
        if (idx < 0) return;

        _processKeyPress(idx);
    }

    // Is the keyboard currently visible?
    bool isVisible() const { return _visible; }

    // Show the keyboard overlay
    void show() {
        _visible = true;
        _drawKeyboard();
    }

    // Hide the keyboard overlay (fills with black)
    void hide() {
        invalidateHotzone();
        _visible = false;
        _shiftActive = false;
        _tft->fillScreen(TFT_BLACK);
    }

    // Toggle visibility
    void toggle() {
        if (_visible) hide(); else show();
    }

    // Is a new key event available?
    bool hasKey() const { return _keyReady; }

    // Retrieve the last key event (clears the ready flag)
    CoCoKeyEvent getKey() {
        _keyReady = false;
        return _lastEvent;
    }

    // Fill a 7-byte array with current PIA row states.
    // Each byte = PA row (bits 0–7 for PB cols), active-LOW (0 = pressed).
    // Useful for emulators that poll the PIA matrix.
    void getMatrixState(uint8_t* pa) {
        for (int i = 0; i < 7; i++) pa[i] = 0xFF;

        // SHIFT toggle
        if (_shiftActive) {
            pa[6] &= ~(1 << 7);
        }

        // Currently held key (auto-expires after COCO_KB_HOLD_MS)
        if (_keyHeld) {
            if (millis() - _keyHeldTime < COCO_KB_HOLD_MS) {
                pa[_heldRow] &= ~(1 << _heldCol);
            } else {
                _keyHeld = false;
            }
        }
    }

    // Draw the always-visible button bar (F1, F2, F5, KB) in the top-right area
    void drawHotZoneIndicator() {
        if (_hotzoneDrawn) return;
        _drawTopButton(COCO_KB_BTN_F1_X, "F1");
        _drawTopButton(COCO_KB_BTN_F2_X, "F2");
        _drawTopButton(COCO_KB_BTN_F5_X, "F5");
        _drawTopButton(COCO_KB_BTN_KB_X, "KB");
        _hotzoneDrawn = true;
    }

    // Force a repaint on the next drawHotZoneIndicator() call
    void invalidateHotzone() { _hotzoneDrawn = false; }

    // Get the SHIFT toggle state
    bool isShiftActive() const { return _shiftActive; }

private:
    TFT_eSPI*    _tft;
    bool         _visible;
    bool         _shiftActive;
    bool         _hotzoneDrawn = false;
    bool         _wasTouched;
    bool         _keyReady;
    bool         _keyHeld;
    uint32_t     _lastTouchTime;
    uint32_t     _keyHeldTime;
    uint8_t      _heldRow;
    uint8_t      _heldCol;
    CoCoKeyEvent _lastEvent;

    // Flash animation state
    int16_t      _flashKeyIdx;   // index in _rects (-1 = no flash)
    uint32_t     _flashTime;

    // Runtime key rectangles
    CoCoKeyRect  _rects[COCO_KB_TOTAL_KEYS];
    uint8_t      _numRects;

    // Store type per rect for redraw
    uint8_t      _rectType[COCO_KB_TOTAL_KEYS];
    // Store row/index per rect for redraw
    uint8_t      _rectVRow[COCO_KB_TOTAL_KEYS];
    uint8_t      _rectVIdx[COCO_KB_TOTAL_KEYS];

    // ---------------------------------------------------------------
    // Top button bar (F1, F2, F5, KB)
    // ---------------------------------------------------------------

    void _drawTopButton(int16_t x, const char* label) {
        int16_t y = COCO_KB_FNBTN_Y;
        _tft->fillRoundRect(x, y, COCO_KB_FNBTN_W, COCO_KB_FNBTN_H, 3, COCO_KB_HOTZONE_BG);
        _tft->drawRoundRect(x, y, COCO_KB_FNBTN_W, COCO_KB_FNBTN_H, 3, COCO_KB_HOTZONE_TEXT);
        _tft->setTextFont(1);
        _tft->setTextSize(1);
        _tft->setTextColor(COCO_KB_HOTZONE_TEXT);
        _tft->setTextDatum(MC_DATUM);
        _tft->drawString(label, x + COCO_KB_FNBTN_W / 2, y + COCO_KB_FNBTN_H / 2 + 1);
        _tft->setTextDatum(TL_DATUM);
    }

    // Check if a function button was tapped; if so, generate event and return true
    bool _checkFnButton(uint16_t tx, uint16_t ty, int16_t btnX, char ascii) {
        // Tap area slightly padded around the visual button
        if (tx >= btnX - 2 && tx < btnX + COCO_KB_FNBTN_W + 2 &&
            ty >= 0         && ty < COCO_KB_FNBTN_Y + COCO_KB_FNBTN_H + 8) {
            _lastEvent.row = 7;         // virtual row (not in CoCo matrix)
            _lastEvent.col = 0;
            _lastEvent.pressed = true;
            _lastEvent.shifted = false;
            _lastEvent.ascii = ascii;
            _keyReady = true;
            return true;
        }
        return false;
    }

    // ---------------------------------------------------------------
    // Drawing
    // ---------------------------------------------------------------

    void _drawKeyboard() {
        // Fill entire screen with body color
        _tft->fillScreen(COCO_KB_BODY);

        // Draw TRS-80 nameplate at top
        _drawNameplate();

        // Draw bezel border
        _tft->drawRect(0, 0, COCO_KB_SCREEN_W, COCO_KB_SCREEN_H, COCO_KB_BEZEL_DARK);
        _tft->drawRect(1, 1, COCO_KB_SCREEN_W - 2, COCO_KB_SCREEN_H - 2, COCO_KB_BEZEL_DARK);

        // Build rects and draw keys row by row
        _numRects = 0;
        for (int vr = 0; vr < COCO_NUM_VROWS; vr++) {
            int16_t x = (vr == 4) ? COCO_KB_SPACE_X : COCO_KB_START_X;
            int16_t y = COCO_ROW_Y[vr];
            int16_t h = (vr == 4) ? COCO_KB_SPACE_H : COCO_KB_KEY_H;
            const CoCoKeyDef* row = COCO_ROWS[vr];

            for (int ki = 0; ki < COCO_ROW_CNT[vr]; ki++) {
                const CoCoKeyDef& key = row[ki];
                int16_t kw = key.width;

                // Store rect for hit testing
                uint8_t ri = _numRects;
                _rects[ri] = {x, y, kw, h, ri};
                _rectType[ri] = key.type;
                _rectVRow[ri] = (uint8_t)vr;
                _rectVIdx[ri] = (uint8_t)ki;
                _numRects++;

                // Draw the key
                _drawSingleKey(x, y, kw, h, key, false);

                x += kw + COCO_KB_GAP;
            }
        }

        // Draw "Color Computer 2" label at bottom bezel
        _drawBottomLabel();

        // Draw KB hotzone indicator so it's visible on the keyboard too
        invalidateHotzone();
        drawHotZoneIndicator();
    }

    void _drawNameplate() {
        // Rounded rectangle enclosing the nameplate, left-aligned
        int16_t bx = 6;
        int16_t by = 3;
        int16_t bw = 72;
        int16_t bh = 32;
        _tft->drawRoundRect(bx, by, bw, bh, 4, COCO_KB_BEZEL_DARK);

        // "CoCo 2" title — black text, left-aligned inside the box
        _tft->setTextFont(2);
        _tft->setTextSize(1);
        _tft->setTextColor(COCO_KB_KEY_TEXT, COCO_KB_BODY);
        _tft->setTextDatum(MC_DATUM);
        _tft->drawString("CoCo 2", bx + bw / 2, by + 11);

        // RGB indicator squares centered below title inside box
        int16_t dotSize = 5;
        int16_t dotY = by + 20;
        int16_t dotX = bx + bw / 2 - 12;
        _tft->fillRect(dotX,      dotY, dotSize, dotSize, COCO_KB_DOT_RED);
        _tft->fillRect(dotX + 9,  dotY, dotSize, dotSize, COCO_KB_DOT_GREEN);
        _tft->fillRect(dotX + 18, dotY, dotSize, dotSize, COCO_KB_DOT_BLUE);

        _tft->setTextDatum(TL_DATUM);
    }

    void _drawBottomLabel() {
        _tft->setTextFont(1);
        _tft->setTextSize(1);
        _tft->setTextColor(COCO_KB_LABEL_COLOR, COCO_KB_BODY);
        _tft->setTextDatum(MC_DATUM);
        _tft->drawString("TRS-80 Color Computer 2", COCO_KB_SCREEN_W / 2, 234);
        _tft->setTextDatum(TL_DATUM);
    }

    void _drawSingleKey(int16_t x, int16_t y, int16_t w, int16_t h,
                        const CoCoKeyDef& key, bool pressed) {
        // Choose fill color based on type and state
        uint16_t fillColor;
        uint16_t textColor = COCO_KB_KEY_TEXT;

        if (pressed) {
            fillColor = COCO_KB_KEY_PRESSED;
            textColor = COCO_KB_KEY_TEXT;
        } else {
            switch (key.type) {
                case COCO_KTYPE_BREAK:
                    fillColor = COCO_KB_KEY_BREAK;
                    textColor = COCO_KB_BREAK_TEXT;
                    break;
                case COCO_KTYPE_SHIFT:
                    fillColor = _shiftActive ? COCO_KB_KEY_SHIFT_ACT : COCO_KB_KEY_NORMAL;
                    textColor = _shiftActive ? COCO_KB_BREAK_TEXT : COCO_KB_KEY_TEXT;
                    break;
                case COCO_KTYPE_ENTER:
                case COCO_KTYPE_SPECIAL:
                    fillColor = COCO_KB_KEY_SPECIAL;
                    break;
                case COCO_KTYPE_SPACE:
                    fillColor = COCO_KB_KEY_NORMAL;
                    break;
                default:
                    fillColor = COCO_KB_KEY_NORMAL;
                    break;
            }
        }

        // Draw key body with rounded corners
        _tft->fillRoundRect(x, y, w, h, COCO_KB_RADIUS, fillColor);

        // 3D raised effect — highlight top/left, shadow bottom/right
        if (!pressed) {
            // Top highlight
            _tft->drawFastHLine(x + COCO_KB_RADIUS, y, w - 2 * COCO_KB_RADIUS, COCO_KB_KEY_HILITE);
            // Left highlight
            _tft->drawFastVLine(x, y + COCO_KB_RADIUS, h - 2 * COCO_KB_RADIUS, COCO_KB_KEY_HILITE);
            // Bottom shadow
            _tft->drawFastHLine(x + COCO_KB_RADIUS, y + h - 1, w - 2 * COCO_KB_RADIUS, COCO_KB_KEY_SHADOW);
            // Right shadow
            _tft->drawFastVLine(x + w - 1, y + COCO_KB_RADIUS, h - 2 * COCO_KB_RADIUS, COCO_KB_KEY_SHADOW);
        }

        // Border
        _tft->drawRoundRect(x, y, w, h, COCO_KB_RADIUS, COCO_KB_KEY_BORDER);

        // --- Label rendering ---
        _tft->setTextColor(textColor);
        _tft->setTextDatum(MC_DATUM);

        if (key.type == COCO_KTYPE_ARROW) {
            // Draw arrow triangle glyph
            _drawArrow(x, y, w, h, key.arrowDir, textColor);
        }
        else if (key.shiftLabel != NULL) {
            // Dual-label: shifted char small at top, main char below center
            // Shift label (small, gray)
            if (!pressed) {
                _tft->setTextColor(COCO_KB_KEY_TEXT2);
            }
            _tft->setTextFont(1);
            _tft->setTextSize(1);
            _tft->drawString(key.shiftLabel, x + w / 2, y + 10);

            // Main label (larger)
            _tft->setTextColor(textColor);
            _tft->setTextFont(2);
            _tft->setTextSize(1);
            _tft->drawString(key.label, x + w / 2, y + h / 2 + 5);
        }
        else if (key.label != NULL) {
            // Single label
            if (strlen(key.label) <= 2) {
                // Short label — use larger font
                _tft->setTextFont(2);
                _tft->setTextSize(1);
                _tft->drawString(key.label, x + w / 2, y + h / 2);
            } else {
                // Multi-char label (BRK, ENTER, CLR, SHIFT, SPACE)
                _tft->setTextFont(1);
                _tft->setTextSize(1);
                _tft->drawString(key.label, x + w / 2, y + h / 2);
            }
        }

        _tft->setTextDatum(TL_DATUM);
    }

    void _drawArrow(int16_t kx, int16_t ky, int16_t kw, int16_t kh,
                    uint8_t dir, uint16_t color) {
        // Center of key
        int16_t cx = kx + kw / 2;
        int16_t cy = ky + kh / 2;
        int16_t sz = 6; // half-size of arrow

        switch (dir) {
            case COCO_ARROW_UP:
                _tft->fillTriangle(cx, cy - sz, cx - sz, cy + sz, cx + sz, cy + sz, color);
                break;
            case COCO_ARROW_DOWN:
                _tft->fillTriangle(cx, cy + sz, cx - sz, cy - sz, cx + sz, cy - sz, color);
                break;
            case COCO_ARROW_LEFT:
                _tft->fillTriangle(cx - sz, cy, cx + sz, cy - sz, cx + sz, cy + sz, color);
                break;
            case COCO_ARROW_RIGHT:
                _tft->fillTriangle(cx + sz, cy, cx - sz, cy - sz, cx - sz, cy + sz, color);
                break;
        }
    }

    // Redraw a single key by its rect index (used for flash animation)
    void _redrawKeyByIndex(uint8_t ri, bool pressed) {
        uint8_t vr = _rectVRow[ri];
        uint8_t vi = _rectVIdx[ri];
        const CoCoKeyDef& key = COCO_ROWS[vr][vi];
        _drawSingleKey(_rects[ri].x, _rects[ri].y, _rects[ri].w, _rects[ri].h, key, pressed);
    }

    // ---------------------------------------------------------------
    // Touch handling
    // ---------------------------------------------------------------

    int _hitTest(uint16_t tx, uint16_t ty) {
        for (int i = 0; i < _numRects; i++) {
            CoCoKeyRect& r = _rects[i];
            if (tx >= r.x && tx < r.x + r.w &&
                ty >= r.y && ty < r.y + r.h) {
                return i;
            }
        }
        return -1;
    }

    void _processKeyPress(int rectIdx) {
        uint8_t vr = _rectVRow[rectIdx];
        uint8_t vi = _rectVIdx[rectIdx];
        const CoCoKeyDef& key = COCO_ROWS[vr][vi];

        // Handle SHIFT toggle
        if (key.type == COCO_KTYPE_SHIFT) {
            _shiftActive = !_shiftActive;
            // Redraw BOTH shift keys to reflect state
            for (int i = 0; i < _numRects; i++) {
                if (_rectType[i] == COCO_KTYPE_SHIFT) {
                    _redrawKeyByIndex(i, false);
                }
            }
            return;
        }

        // Flash the pressed key green
        _flashKeyIdx = rectIdx;
        _flashTime = millis();
        _redrawKeyByIndex(rectIdx, true);

        // Build key event
        _lastEvent.row = key.matrixRow;
        _lastEvent.col = key.matrixCol;
        _lastEvent.pressed = true;
        _lastEvent.shifted = _shiftActive;

        // Resolve ASCII
        if (_shiftActive) {
            _lastEvent.ascii = COCO_ASCII_SHIFTED[key.matrixRow][key.matrixCol];
        } else {
            _lastEvent.ascii = COCO_ASCII_NORMAL[key.matrixRow][key.matrixCol];
        }

        _keyReady = true;

        // Set held state for matrix polling
        _keyHeld = true;
        _keyHeldTime = millis();
        _heldRow = key.matrixRow;
        _heldCol = key.matrixCol;
    }

    // Update flash animation (restore key to normal after flash duration)
    void _updateFlash() {
        if (_flashKeyIdx >= 0 && (millis() - _flashTime >= COCO_KB_FLASH_MS)) {
            _redrawKeyByIndex(_flashKeyIdx, false);
            _flashKeyIdx = -1;
        }
    }
};

#endif // COCO2_KEYBOARD_H
