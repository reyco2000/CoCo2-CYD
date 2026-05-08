/*
 * =============================================================
 *   CoCo2-CYD Beta-1 March 2026 - CoCo 2 Emulator for ESP32 CYD
 *   (C) 2026 Reinaldo Torres / CoCo Byte Club
 *   https://github.com/reyco2000/CoCo2-CYD
 *   Based on XRoar Emulator by Ciaran Anscomb
 *   CO-developed with Claude Code (Anthropic)
 *   MIT License
 * =============================================================
 *  File   : rom_loader.cpp
 *  Module : ROM loader implementation — load Dragon/CoCo ROM images with CRC-32 validation
 * =============================================================
 */


/*
 * rom_loader.cpp - ROM loading utilities
 */

#include "rom_loader.h"
#include "../utils/debug.h"

uint32_t rom_crc32(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            if (crc & 1) crc = (crc >> 1) ^ 0xEDB88320;
            else         crc >>= 1;
        }
    }
    return crc ^ 0xFFFFFFFF;
}

bool rom_validate(const uint8_t* data, size_t len, const char* name) {
    uint32_t crc = rom_crc32(data, len);
    DEBUG_PRINTF("  ROM '%s': CRC32 = 0x%08X", name, crc);

    // Check against known CRCs
    switch (crc) {
    case CRC_BAS13:     DEBUG_PRINT("    -> Color BASIC 1.3"); return true;
    case CRC_BAS12:     DEBUG_PRINT("    -> Color BASIC 1.2"); return true;
    case CRC_EXTBAS11:  DEBUG_PRINT("    -> Extended BASIC 1.1"); return true;
    case CRC_EXTBAS10:  DEBUG_PRINT("    -> Extended BASIC 1.0"); return true;
    case CRC_DISK11:    DEBUG_PRINT("    -> Disk BASIC 1.1"); return true;
    case CRC_DISK11B:   DEBUG_PRINT("    -> Disk BASIC 1.1"); return true;
    case CRC_DISK10:    DEBUG_PRINT("    -> Disk BASIC 1.0"); return true;
    default:
        DEBUG_PRINT("    -> Unknown ROM (may still work)");
        return false;
    }
}
