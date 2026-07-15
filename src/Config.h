// Config.h — hardware pins, tunables and the shared colour palette.
//
// Board: LilyGO T-QT Pro (ESP32-S3), 128x128 GC9A01 TFT over HSPI.
// Display pins live in lib/TFT_eSPI/User_Setups/Setup211_LilyGo_T_QT_Pro_S3.h
// (BL=10, MOSI=2, SCLK=3, CS=5, DC=6, RST=1). Do not duplicate them here.
#pragma once

#include <Arduino.h>

// ---------------------------------------------------------------------------
// Physical controls
// ---------------------------------------------------------------------------
static const uint8_t PIN_BTN_LEFT  = 0;   // BOOT button (active-low)
static const uint8_t PIN_BTN_RIGHT = 47;  // side button  (active-low)
static const uint8_t PIN_BAT_VOLT  = 4;   // battery sense ADC (unused for now)

// Match the working LilyGO examples: rotation 2 puts (0,0) top-left for us.
static const uint8_t TFT_ROTATION = 2;

// Set to 1 to boot into a display-alignment test pattern (used to measure the
// GC9A01 CGRAM edge offset). Leave 0 for normal operation.
#define UI_ALIGN_TEST 0

// ---------------------------------------------------------------------------
// Screen geometry
// ---------------------------------------------------------------------------
static const int16_t SCR_W = 128;
static const int16_t SCR_H = 128;

// ---------------------------------------------------------------------------
// Timing (ms)
// ---------------------------------------------------------------------------
static const uint32_t SPLASH_MS        = 1200;   // logo dwell
static const uint32_t OOBE_STEP1_MS    = 2600;   // "turn pad on" dwell before step 2
static const uint32_t PAIRED_CELEBRATE_MS = 1600; // "Paired!" dwell before Live
static const uint32_t FRAME_MS         = 33;     // ~30 fps render cadence
static const uint32_t RECONNECT_HINT_MS = 8000;  // show "hold L to pair new" after this

// ---------------------------------------------------------------------------
// Controller value conventions
// ---------------------------------------------------------------------------
// Xbox BLE reports vertical sticks with 0 = up. Flip so "up" reads positive
// on the values page and the dot moves up on screen. Toggle if it feels wrong.
static const bool INVERT_STICK_Y = true;

// ---------------------------------------------------------------------------
// Colour palette (RGB565). Xbox-flavoured, tuned for a tiny dim panel.
// ---------------------------------------------------------------------------
namespace col {
  static const uint16_t bg       = 0x0000;  // near-black
  static const uint16_t panel    = 0x18E3;  // dark grey card
  static const uint16_t line     = 0x39E7;  // hairline / inactive outline
  static const uint16_t text     = 0xFFFF;  // white
  static const uint16_t textDim  = 0x9CD3;  // muted grey
  static const uint16_t green    = 0x2648;  // Xbox green (#107C10-ish)
  static const uint16_t greenHi  = 0x5F0B;  // brighter green for accents
  static const uint16_t red      = 0xE8E4;  // B button
  static const uint16_t blue     = 0x2D7F;  // X button
  static const uint16_t amber    = 0xFD00;  // Y button
  static const uint16_t warn     = 0xFCC0;  // amber warning
  static const uint16_t batt     = 0x4FEA;  // battery green
}
