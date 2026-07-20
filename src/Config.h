// Config.h — hardware pins, tunables and the shared colour palette.
//
// Two hardware personalities, picked at compile time by UX_LED:
//
//   UX_LED unset (default) — LilyGO T-QT Pro (ESP32-S3), 128x128 GC9A01 TFT
//     over HSPI, two buttons. Display pins live in
//     lib/TFT_eSPI/User_Setups/Setup211_LilyGo_T_QT_Pro_S3.h (BL=10, MOSI=2,
//     SCLK=3, CS=5, DC=6, RST=1). Do not duplicate them here.
//
//   UX_LED=1 — headless: a generic ESP32-S3 dev board driving its onboard LED
//     and its BOOT button. See Ux.h for how the backend is selected.
#pragma once

#include <Arduino.h>

#if UX_LED
// ---------------------------------------------------------------------------
// Headless (LED + one button)
// ---------------------------------------------------------------------------
// Every value below is a `#ifndef` default so a -D build flag (or
// PLATFORMIO_BUILD_FLAGS) overrides it without a redefinition warning.

// LED_KIND selectors. LED_RGB drives an addressable WS2812/SK68xx through the
// core's neopixelWrite(); LED_MONO drives a plain LED through an LEDC channel.
#define LED_RGB  1
#define LED_MONO 2

#ifndef LED_KIND
#define LED_KIND LED_RGB
#endif

// There is no portable "onboard LED" pin, and the core's RGB_BUILTIN is no help
// here: arduino-esp32's generic esp32s3 variant hardcodes PIN_NEOPIXEL 48, which
// is wrong on every DevKitC-1 v1.1. So the pin is always explicit.
//
//   DevKitC-1 v1.1  38 (default)   DevKitC-1 v1.0  48
//   Adafruit QT Py  39 (needs 38 driven high as LED power)
//   XIAO ESP32S3    21 with -DLED_KIND=LED_MONO -DLED_ACTIVE_LOW=1
//
// Unsure which you have? Set UI_ALIGN_TEST 1 below and the board cycles
// red/green/blue on boot — no cycle means the pin is wrong.
#ifndef LED_PIN
#define LED_PIN 38
#endif

// LED_MONO only: 1 when the LED is wired to sink through the pin (XIAO).
#ifndef LED_ACTIVE_LOW
#define LED_ACTIVE_LOW 0
#endif

// Global brightness ceiling, 0..255, applied once at the write. Onboard devkit
// LEDs are genuinely painful at full scale, and this thing lives next to a TV.
#ifndef LED_BRIGHTNESS
#define LED_BRIGHTNESS 40
#endif

// BOOT is the only readable button on a generic devkit (RESET/EN is wired to
// the reset line, not a GPIO). It is a strapping pin at boot only; reading it
// at runtime is fine — the T-QT's left button is GPIO0 too.
#ifndef BTN_PIN
#define BTN_PIN 0
#endif

// Hold BTN_PIN this long to forget the controller and reboot into pairing.
// Long enough that it cannot be hit by accident, short enough to not feel stuck.
static const uint32_t FORGET_HOLD_MS = 2000;

#else
// ---------------------------------------------------------------------------
// Physical controls — LilyGO T-QT Pro
// ---------------------------------------------------------------------------
static const uint8_t PIN_BTN_LEFT  = 0;   // BOOT button (active-low)
static const uint8_t PIN_BTN_RIGHT = 47;  // side button  (active-low)
static const uint8_t PIN_BAT_VOLT  = 4;   // battery sense ADC (unused for now)
#endif

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
// Sleep behaviour
// ---------------------------------------------------------------------------
// 1 (default): when the PC sleeps, drop the BLE link so the pad can power
// itself down — Guide light off, battery saved, like a real console. Pressing
// Guide powers the pad back on, it reconnects, and the reconnect wakes the PC
// (costs the reconnect, ~2-5 s). 0: hold the link all night; a Guide press on
// the still-connected pad wakes instantly, at the pad's battery's expense.
#ifndef SLEEP_RELEASES_PAD
#define SLEEP_RELEASES_PAD 1
#endif

// How long after sleep before the bridge listens for the pad again. Must
// outlive the pad's own link-loss search (~15-20 s, then it powers off):
// resume scanning any earlier and a still-searching pad slips straight back
// in — which the wake trigger would read as a phantom "wake the PC".
#ifndef PAD_RELEASE_GRACE_MS
#define PAD_RELEASE_GRACE_MS 60000UL
#endif

// Suspend must hold this long before the pad is released. The suspend flag
// drops briefly during a wake attempt's detach pulse, and a host climbing out
// of a deep sleep state can take several seconds after that pulse before its
// frame traffic returns; without this debounce that quiet gap read as a fresh
// sleep — kicking off the very pad that just reconnected to wake the PC.
// Long enough to outlast a slow resume, short enough that the pad still goes
// dark promptly on a real sleep (its own search runs 15-20 s regardless).
#ifndef PAD_RELEASE_DEBOUNCE_MS
#define PAD_RELEASE_DEBOUNCE_MS 10000UL
#endif

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
