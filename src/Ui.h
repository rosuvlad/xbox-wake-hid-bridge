// Ui.h — all rendering for the 128x128 panel.
//
// Everything is drawn into one full-screen 16-bit sprite and pushed in a
// single blit, so every frame is flicker-free. Screens are plain methods; the
// state machine in main.cpp decides which one to call each frame.
#pragma once

#include <TFT_eSPI.h>

#include "AppState.h"
#include "BridgeState.h"
#include "Config.h"
#include "PadSnapshot.h"

class Ui {
 public:
  void begin();

  // Diagnostic edge-alignment pattern (see UI_ALIGN_TEST)
  void alignTest();

  // Latest PC-facing state; the status bar reads it every frame.
  void setBridge(const BridgeState& b) { bridge_ = b; }

  // Boot + OOBE
  void splash(float progress);          // progress 0..1
  void oobeStep1(uint32_t nowMs);       // "1/2 turn pad on"
  void oobeStep2(uint32_t nowMs);       // "2/2 hold pair" + scanning
  void found(uint32_t nowMs);           // link up, syncing
  void paired(const String& addr);      // first-time success

  // Connected
  void liveValues(const PadSnapshot& s);
  void livePad(const PadSnapshot& s);
  void liveBridge(const PadSnapshot& s);   // BLE/USB/PC/perf dashboard

  // Transient: USB remote-wake was just fired at the sleeping PC
  void waking();

  // Recovery / utility
  void reconnecting(uint32_t nowMs, bool showPairHint);
  void diagnostics(const PadSnapshot& s, uint32_t uptimeMs);
  void forgetConfirm();

 private:
  void clear() { spr_.fillSprite(col::bg); }
  void push() { spr_.pushSprite(0, 0); }

  // Shared chrome / motion
  void statusBar(const PadSnapshot& s, LivePage page);
  void batteryPill(int x, int y, const PadSnapshot& s);
  void btGlyph(int cx, int cy, int halfH, uint16_t color);
  void usbGlyph(int cx, int cy, int halfH, uint16_t color);
  void powerGlyph(int cx, int cy, int r, uint16_t color);
  void checkGlyph(int cx, int cy, int r, uint16_t color);
  void warnGlyph(int cx, int cy, int r, uint16_t color);
  void pulseRings(int cx, int cy, uint32_t nowMs, uint16_t color);
  void ellipsis(int cx, int y, uint32_t nowMs, uint16_t color);
  void stepHeader(int step);                     // "1/2" style with dots
  void centerText(const char* s, int y, uint8_t font, uint16_t color);

  // Live-pad widgets
  void stickRing(int cx, int cy, int r, float nx, float ny, bool pressed);
  void triggerBarH(int x, int y, int w, int h, float v, bool rightToLeft,
                   const char* label);
  void faceButton(int cx, int cy, int r, char label, uint16_t color,
                  bool pressed);
  void bumper(int x, int y, int w, int h, const char* label, bool pressed);
  void dpadWidget(int cx, int cy, int r, const PadSnapshot& s);
  void centerBadge(int cx, int cy, const char* label, bool pressed);

  TFT_eSPI tft_;
  TFT_eSprite spr_{&tft_};
  BridgeState bridge_;
};
