// Ui.cpp — implementation of every screen and widget.
#include "Ui.h"

#include <math.h>

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------
void Ui::begin() {
  tft_.init();
  tft_.setRotation(TFT_ROTATION);
  tft_.fillScreen(col::bg);
  // One full-screen 16-bit canvas we draw into and blit each frame.
  spr_.setColorDepth(16);
  spr_.createSprite(SCR_W, SCR_H);
  spr_.setTextWrap(false);
}

// ---------------------------------------------------------------------------
// Alignment diagnostic: green field, concentric borders at 0/2/4 px inset,
// and coloured corner blocks so a single photo reveals the exact edge offset.
//   white block  = our top-left (0,0)      cyan block    = our top-right
//   magenta block= our bottom-left         (bottom-right = plain)
// On each physical edge, any non-green strip OUTSIDE the red border is
// un-painted GRAM => that edge's CGRAM offset is too large by that many pixels.
// ---------------------------------------------------------------------------
void Ui::alignTest() {
  spr_.fillSprite(TFT_GREEN);
  spr_.drawRect(0, 0, SCR_W, SCR_H, TFT_RED);              // extreme edge
  spr_.drawRect(2, 2, SCR_W - 4, SCR_H - 4, TFT_YELLOW);   // +2px reference
  spr_.drawRect(4, 4, SCR_W - 8, SCR_H - 8, TFT_BLUE);     // +4px reference
  spr_.fillRect(0, 0, 9, 9, TFT_WHITE);                    // top-left marker
  spr_.fillRect(SCR_W - 9, 0, 9, 9, TFT_CYAN);             // top-right marker
  spr_.fillRect(0, SCR_H - 9, 9, 9, TFT_MAGENTA);          // bottom-left marker
  spr_.setTextDatum(MC_DATUM);
  spr_.setTextColor(TFT_BLACK, TFT_GREEN);
  spr_.drawString("ALIGN", SCR_W / 2, SCR_H / 2, 2);
  push();
}

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------
void Ui::centerText(const char* s, int y, uint8_t font, uint16_t color) {
  spr_.setTextDatum(MC_DATUM);
  spr_.setTextColor(color, col::bg);
  spr_.drawString(s, SCR_W / 2, y, font);
}

void Ui::btGlyph(int cx, int cy, int halfH, uint16_t color) {
  const float w = halfH * 0.55f;
  const int Tx = cx,        Ty = cy - halfH;
  const int Bx = cx,        By = cy + halfH;
  const int URx = cx + w,   URy = cy - halfH / 2;
  const int LRx = cx + w,   LRy = cy + halfH / 2;
  const int ULx = cx - w,   ULy = cy - halfH / 2;
  const int LLx = cx - w,   LLy = cy + halfH / 2;
  const float t = max(1.6f, halfH * 0.18f);
  spr_.drawWideLine(Tx, Ty, Bx, By, t, color, col::bg);   // spine
  spr_.drawWideLine(Tx, Ty, LRx, LRy, t, color, col::bg); // top -> lower-right
  spr_.drawWideLine(Bx, By, URx, URy, t, color, col::bg); // bottom -> upper-right
  spr_.drawWideLine(URx, URy, LLx, LLy, t, color, col::bg); // cross
  spr_.drawWideLine(LRx, LRy, ULx, ULy, t, color, col::bg); // cross
}

void Ui::usbGlyph(int cx, int cy, int halfH, uint16_t color) {
  // stylised USB trident: stem, round base, forked head (square + round tip)
  spr_.drawFastVLine(cx, cy - halfH, halfH * 2 + 1, color);
  spr_.fillCircle(cx, cy + halfH, 1, color);          // round base
  spr_.fillRect(cx - 1, cy - halfH - 1, 3, 2, color); // arrow head top
  spr_.drawLine(cx, cy - 1, cx - 3, cy - 3, color);   // left branch
  spr_.fillRect(cx - 4, cy - 4, 2, 2, color);         // square tip
  spr_.drawLine(cx, cy + 1, cx + 3, cy - 1, color);   // right branch
  spr_.fillCircle(cx + 3, cy - 1, 1, color);          // round tip
}

void Ui::powerGlyph(int cx, int cy, int r, uint16_t color) {
  spr_.drawCircle(cx, cy, r, color);
  spr_.drawCircle(cx, cy, r - 1, color);
  // vertical break line through the top
  spr_.fillRect(cx - r, cy - r - 2, 2 * r, 4, col::bg);
  spr_.drawWideLine(cx, cy - r - 3, cx, cy + 1, 3, color, col::bg);
}

void Ui::checkGlyph(int cx, int cy, int r, uint16_t color) {
  spr_.drawWideLine(cx - r, cy, cx - r / 3, cy + r * 0.7f, 3, color, col::bg);
  spr_.drawWideLine(cx - r / 3, cy + r * 0.7f, cx + r, cy - r * 0.8f, 3, color,
                    col::bg);
}

void Ui::warnGlyph(int cx, int cy, int r, uint16_t color) {
  spr_.fillTriangle(cx, cy - r, cx - r, cy + r, cx + r, cy + r, color);
  spr_.fillRect(cx - 1, cy - r / 2, 3, r, col::bg);        // bang stem
  spr_.fillRect(cx - 1, cy + r / 2 + 1, 3, 3, col::bg);    // bang dot
}

void Ui::pulseRings(int cx, int cy, uint32_t nowMs, uint16_t color) {
  const float period = 1400.0f;
  for (int k = 0; k < 2; ++k) {
    float phase = fmodf(nowMs / period + k * 0.5f, 1.0f);
    int r = (int)(6 + phase * 26);
    // dim the ring as it expands so it reads like sonar
    uint16_t c = phase < 0.6f ? color : col::line;
    spr_.drawCircle(cx, cy, r, c);
  }
}

void Ui::ellipsis(int cx, int y, uint32_t nowMs, uint16_t color) {
  int n = (nowMs / 350) % 4;  // 0..3 dots
  char dots[4] = {0};
  for (int i = 0; i < n; ++i) dots[i] = '.';
  char buf[20];
  snprintf(buf, sizeof(buf), "searching%-3s", dots);  // fixed width -> no jitter
  spr_.setTextDatum(MC_DATUM);
  spr_.setTextColor(color, col::bg);
  spr_.drawString(buf, cx, y, 1);
}

void Ui::stepHeader(int step) {
  spr_.setTextDatum(TL_DATUM);
  spr_.setTextColor(col::green, col::bg);
  spr_.drawString("PAIRING", 4, 3, 1);
  char b[8];
  snprintf(b, sizeof(b), "%d/2", step);
  spr_.setTextDatum(TR_DATUM);
  spr_.setTextColor(col::textDim, col::bg);
  spr_.drawString(b, SCR_W - 4, 3, 1);
  // two-segment progress under the header
  for (int i = 0; i < 2; ++i) {
    int x = 4 + i * 62;
    uint16_t c = (i < step) ? col::green : col::line;
    spr_.fillRoundRect(x, 14, 58, 3, 1, c);
  }
}

// ---------------------------------------------------------------------------
// Status bar (Live / Diagnostics)
// ---------------------------------------------------------------------------
void Ui::batteryPill(int x, int y, const PadSnapshot& s) {
  const int w = 16, h = 9;
  spr_.drawRect(x, y, w, h, col::textDim);
  spr_.fillRect(x + w, y + 2, 2, h - 4, col::textDim);  // nub
  if (s.batteryKnown) {
    int fill = (int)roundf((w - 2) * (s.battery / 100.0f));
    uint16_t c = s.battery <= 15 ? col::warn : col::batt;
    if (fill > 0) spr_.fillRect(x + 1, y + 1, fill, h - 2, c);
  } else {
    spr_.setTextDatum(MC_DATUM);
    spr_.setTextColor(col::textDim, col::bg);
    spr_.drawString("?", x + w / 2, y + h / 2 - 1, 1);
  }
}

void Ui::statusBar(const PadSnapshot& s, LivePage page) {
  uint16_t bt = s.receiving ? col::green : (s.connected ? col::amber : col::line);
  btGlyph(8, 7, 5, bt);

  // USB link: grey = unplugged, amber = PC suspended, green = active
  uint16_t usb = !bridge_.usbReady ? col::line
                 : bridge_.usbSuspended ? col::amber
                                        : col::green;
  usbGlyph(24, 7, 5, usb);

  // page dots (centre)
  for (int i = 0; i < (int)LivePage::_count; ++i) {
    uint16_t c = (i == (int)page) ? col::text : col::line;
    spr_.fillCircle(80 + i * 8, 7, 2, c);
  }

  batteryPill(SCR_W - 20, 2, s);
}

// ---------------------------------------------------------------------------
// Boot + OOBE screens
// ---------------------------------------------------------------------------
void Ui::splash(float progress) {
  clear();
  btGlyph(SCR_W / 2, 34, 14, col::green);
  centerText("XBOX", 64, 4, col::text);
  centerText("BRIDGE", 86, 2, col::green);
  int w = (int)(96 * constrain(progress, 0.0f, 1.0f));
  spr_.fillRoundRect(16, 108, 96, 4, 2, col::panel);
  if (w > 0) spr_.fillRoundRect(16, 108, w, 4, 2, col::green);
  push();
}

void Ui::oobeStep1(uint32_t nowMs) {
  clear();
  stepHeader(1);
  // gentle breathing power icon
  int r = 18 + (int)(2 * sinf(nowMs / 350.0f));
  powerGlyph(SCR_W / 2, 52, r, col::greenHi);
  centerText("Turn your", 84, 2, col::text);
  centerText("controller ON", 100, 2, col::text);
  centerText("press the Xbox button", 118, 1, col::textDim);
  push();
}

void Ui::oobeStep2(uint32_t nowMs) {
  clear();
  stepHeader(2);
  pulseRings(SCR_W / 2, 48, nowMs, col::green);
  btGlyph(SCR_W / 2, 48, 12, col::green);
  centerText("Hold the small", 82, 2, col::text);
  centerText("PAIR button", 98, 2, col::green);
  centerText("on the back ~3s", 112, 1, col::textDim);
  ellipsis(SCR_W / 2, 123, nowMs, col::textDim);
  push();
}

void Ui::found(uint32_t nowMs) {
  clear();
  pulseRings(SCR_W / 2, 46, nowMs, col::greenHi);
  btGlyph(SCR_W / 2, 46, 13, col::greenHi);
  centerText("Found it!", 84, 4, col::text);
  centerText("connecting", 108, 1, col::textDim);
  ellipsis(SCR_W / 2, 120, nowMs, col::textDim);
  push();
}

void Ui::paired(const String& addr) {
  clear();
  spr_.fillCircle(SCR_W / 2, 42, 22, col::green);
  checkGlyph(SCR_W / 2, 42, 12, col::bg);
  centerText("Paired!", 78, 4, col::green);
  // show the tail of the MAC so the user recognises their pad
  String tail = addr.length() >= 5 ? addr.substring(addr.length() - 5) : addr;
  char b[24];
  snprintf(b, sizeof(b), "Xbox pad  %s", tail.c_str());
  centerText(b, 100, 1, col::textDim);
  centerText("saved - won't ask again", 114, 1, col::green);
  push();
}

// ---------------------------------------------------------------------------
// Recovery / utility screens
// ---------------------------------------------------------------------------
void Ui::reconnecting(uint32_t nowMs, bool showPairHint) {
  clear();
  pulseRings(SCR_W / 2, 46, nowMs, col::amber);
  btGlyph(SCR_W / 2, 46, 12, col::amber);
  centerText("Controller off", 84, 2, col::text);
  centerText("waiting to reconnect", 100, 1, col::textDim);
  if (showPairHint)
    centerText("hold LEFT btn: pair new", 120, 1, col::textDim);
  push();
}

void Ui::diagnostics(const PadSnapshot& s, uint32_t uptimeMs) {
  clear();
  spr_.setTextDatum(TL_DATUM);
  spr_.setTextColor(col::green, col::bg);
  spr_.drawString("DIAGNOSTICS", 4, 3, 1);
  spr_.drawFastHLine(0, 13, SCR_W, col::line);

  char line[28];
  int y = 20;
  auto row = [&](const char* txt) {
    spr_.setTextColor(col::text, col::bg);
    spr_.drawString(txt, 4, y, 1);
    y += 12;
  };

  snprintf(line, sizeof(line), "MAC %s",
           s.addr.length() ? s.addr.c_str() : "--");
  row(line);
  snprintf(line, sizeof(line), "Link %s",
           s.receiving ? "STREAMING" : (s.connected ? "CONNECTED" : "DOWN"));
  row(line);
  snprintf(line, sizeof(line), "Rate %u Hz", s.rateHz);
  row(line);
  if (s.batteryKnown)
    snprintf(line, sizeof(line), "Batt %u%%", s.battery);
  else
    snprintf(line, sizeof(line), "Batt --");
  row(line);

  uint32_t sec = uptimeMs / 1000;
  snprintf(line, sizeof(line), "Up %02u:%02u:%02u", sec / 3600,
           (sec % 3600) / 60, sec % 60);
  row(line);
  row("FW m1: oobe + debug");

  spr_.setTextDatum(BC_DATUM);
  spr_.setTextColor(col::textDim, col::bg);
  spr_.drawString("any button = back", SCR_W / 2, SCR_H - 2, 1);
  push();
}

void Ui::forgetConfirm() {
  clear();
  warnGlyph(SCR_W / 2, 34, 16, col::warn);
  centerText("Forget", 66, 4, col::text);
  centerText("controller?", 88, 2, col::text);
  centerText("erases pairing", 104, 1, col::textDim);
  centerText("hold RIGHT to confirm", 120, 1, col::warn);
  push();
}

// ---------------------------------------------------------------------------
// Live: values page (default)
// ---------------------------------------------------------------------------
void Ui::liveValues(const PadSnapshot& s) {
  clear();
  statusBar(s, LivePage::Values);
  spr_.setTextDatum(TL_DATUM);

  char b[28];
  int y = 18;
  spr_.setTextColor(col::text, col::bg);
  snprintf(b, sizeof(b), "LS  x%+.2f y%+.2f", s.lx, s.ly);
  spr_.drawString(b, 4, y, 1);
  y += 12;
  snprintf(b, sizeof(b), "RS  x%+.2f y%+.2f", s.rx, s.ry);
  spr_.drawString(b, 4, y, 1);

  // triggers, each on its own row and identical:  LT [====bar====] NN%
  y += 15;
  triggerBarH(20, y, 62, 8, s.lt, false, "LT");
  spr_.setTextDatum(TL_DATUM);  // triggerBarH leaves the datum right-aligned
  spr_.setTextColor(col::textDim, col::bg);
  snprintf(b, sizeof(b), "%d%%", (int)roundf(s.lt * 100));
  spr_.drawString(b, 88, y, 1);
  y += 13;
  triggerBarH(20, y, 62, 8, s.rt, false, "RT");
  spr_.setTextDatum(TL_DATUM);  // must reset again for RT's percentage
  spr_.setTextColor(col::textDim, col::bg);
  snprintf(b, sizeof(b), "%d%%", (int)roundf(s.rt * 100));
  spr_.drawString(b, 88, y, 1);

  // held buttons
  y += 16;
  spr_.setTextColor(col::textDim, col::bg);
  spr_.drawString("HELD", 4, y, 1);

  char held[40] = {0};
  auto add = [&](bool on, const char* name) {
    if (!on) return;
    if (strlen(held) + strlen(name) + 1 >= sizeof(held)) return;
    if (held[0]) strcat(held, " ");
    strcat(held, name);
  };
  add(s.a, "A"); add(s.b, "B"); add(s.x, "X"); add(s.y, "Y");
  add(s.lb, "LB"); add(s.rb, "RB"); add(s.ls, "L3"); add(s.rs, "R3");
  add(s.up, "UP"); add(s.down, "DN"); add(s.left, "LF"); add(s.right, "RT");
  add(s.view, "VIEW"); add(s.menu, "MENU"); add(s.xbox, "XBOX");
  add(s.share, "SHARE");
  spr_.setTextColor(col::greenHi, col::bg);
  spr_.drawString(held[0] ? held : "-", 34, y, 1);

  // footer telemetry
  spr_.setTextDatum(BL_DATUM);
  spr_.setTextColor(col::textDim, col::bg);
  if (s.batteryKnown)
    snprintf(b, sizeof(b), "batt %u%%", s.battery);
  else
    snprintf(b, sizeof(b), "batt --");
  spr_.drawString(b, 4, SCR_H - 2, 1);
  spr_.setTextDatum(BR_DATUM);
  snprintf(b, sizeof(b), "%u Hz", s.rateHz);
  spr_.drawString(b, SCR_W - 4, SCR_H - 2, 1);

  push();
}

// ---------------------------------------------------------------------------
// Live: pad diagram page
// ---------------------------------------------------------------------------
void Ui::triggerBarH(int x, int y, int w, int h, float v, bool rightToLeft,
                     const char* label) {
  spr_.drawRoundRect(x, y, w, h, 2, col::line);
  int fill = (int)roundf((w - 2) * constrain(v, 0.0f, 1.0f));
  if (fill > 0) {
    if (rightToLeft)
      spr_.fillRect(x + w - 1 - fill, y + 1, fill, h - 2, col::greenHi);
    else
      spr_.fillRect(x + 1, y + 1, fill, h - 2, col::greenHi);
  }
  if (label) {
    spr_.setTextColor(col::textDim, col::bg);
    if (rightToLeft) {
      spr_.setTextDatum(TL_DATUM);
      spr_.drawString(label, x + w + 3, y, 1);
    } else {
      spr_.setTextDatum(TR_DATUM);
      spr_.drawString(label, x - 3, y, 1);
    }
  }
}

void Ui::stickRing(int cx, int cy, int r, float nx, float ny, bool pressed) {
  spr_.drawCircle(cx, cy, r, pressed ? col::greenHi : col::line);
  if (pressed) spr_.drawCircle(cx, cy, r - 1, col::greenHi);
  spr_.fillCircle(cx, cy, 1, col::line);
  int dx = cx + (int)(nx * (r - 3));
  int dy = cy - (int)(ny * (r - 3));  // ny up-positive -> screen up
  spr_.fillCircle(dx, dy, 3, pressed ? col::greenHi : col::text);
}

void Ui::faceButton(int cx, int cy, int r, char label, uint16_t color,
                    bool pressed) {
  char s[2] = {label, 0};
  if (pressed) {
    spr_.fillCircle(cx, cy, r, color);
    spr_.setTextColor(col::bg, color);
  } else {
    spr_.drawCircle(cx, cy, r, color);
    spr_.setTextColor(color, col::bg);
  }
  spr_.setTextDatum(MC_DATUM);
  spr_.drawString(s, cx, cy, 1);
}

void Ui::bumper(int x, int y, int w, int h, const char* label, bool pressed) {
  if (pressed) {
    spr_.fillRoundRect(x, y, w, h, 3, col::greenHi);
    spr_.setTextColor(col::bg, col::greenHi);
  } else {
    spr_.drawRoundRect(x, y, w, h, 3, col::line);
    spr_.setTextColor(col::textDim, col::bg);
  }
  spr_.setTextDatum(MC_DATUM);
  spr_.drawString(label, x + w / 2, y + h / 2, 1);
}

void Ui::dpadWidget(int cx, int cy, int r, const PadSnapshot& s) {
  spr_.drawRect(cx - 4, cy - r, 8, 2 * r + 1, col::line);
  spr_.drawRect(cx - r, cy - 4, 2 * r + 1, 8, col::line);
  if (s.up) spr_.fillRect(cx - 3, cy - r + 1, 6, r - 4, col::greenHi);
  if (s.down) spr_.fillRect(cx - 3, cy + 4, 6, r - 4, col::greenHi);
  if (s.left) spr_.fillRect(cx - r + 1, cy - 3, r - 4, 6, col::greenHi);
  if (s.right) spr_.fillRect(cx + 4, cy - 3, r - 4, 6, col::greenHi);
}

void Ui::centerBadge(int cx, int cy, const char* label, bool pressed) {
  uint16_t c = pressed ? col::greenHi : col::line;
  spr_.drawCircle(cx, cy, 6, c);
  if (pressed) spr_.fillCircle(cx, cy, 5, col::greenHi);
  spr_.setTextDatum(MC_DATUM);
  spr_.setTextColor(pressed ? col::bg : col::textDim,
                    pressed ? col::greenHi : col::bg);
  spr_.drawString(label, cx, cy, 1);
}

void Ui::livePad(const PadSnapshot& s) {
  clear();
  statusBar(s, LivePage::Pad);

  // triggers along the top
  triggerBarH(24, 18, 34, 7, s.lt, false, "LT");
  triggerBarH(70, 18, 34, 7, s.rt, true, "RT");

  // bumpers
  bumper(6, 28, 42, 10, "LB", s.lb);
  bumper(80, 28, 42, 10, "RB", s.rb);

  // sticks
  stickRing(32, 66, 18, s.lx, s.ly, s.ls);
  stickRing(98, 100, 14, s.rx, s.ry, s.rs);

  // ABXY diamond
  faceButton(98, 44, 9, 'Y', col::amber, s.y);
  faceButton(82, 60, 9, 'X', col::blue, s.x);
  faceButton(114, 60, 9, 'B', col::red, s.b);
  faceButton(98, 76, 9, 'A', col::green, s.a);

  // dpad
  dpadWidget(30, 104, 14, s);

  // centre badges: View / Xbox / Menu / Share
  centerBadge(54, 120, "V", s.view);
  centerBadge(64, 120, "G", s.xbox);
  centerBadge(74, 120, "M", s.menu);
  centerBadge(6, 120, "S", s.share);

  // Rumble from the PC: outer-edge grip bars grow with left/right motor power.
  if (millis() - bridge_.lastRumbleMs < 400) {
    int lh = bridge_.rmbLeft * 44 / 100;
    int rh = bridge_.rmbRight * 44 / 100;
    if (lh > 0) spr_.fillRect(0, 106 - lh, 3, lh, col::warn);
    if (rh > 0) spr_.fillRect(SCR_W - 3, 106 - rh, 3, rh, col::warn);
  }

  push();
}

// ---------------------------------------------------------------------------
// Live: bridge dashboard (BLE + USB + PC + performance)
// ---------------------------------------------------------------------------
void Ui::liveBridge(const PadSnapshot& s) {
  clear();
  statusBar(s, LivePage::Bridge);
  spr_.setTextDatum(TL_DATUM);

  char l[16], r[16];
  int y = 18;
  auto section = [&](const char* name, uint16_t nameCol, const char* state,
                     uint16_t stateCol) {
    spr_.setTextColor(nameCol, col::bg);
    spr_.drawString(name, 4, y, 1);
    spr_.setTextColor(stateCol, col::bg);
    spr_.drawString(state, 40, y, 1);
    y += 11;
  };
  auto detail2 = [&](const char* left, const char* right) {
    spr_.setTextColor(col::textDim, col::bg);
    spr_.drawString(left, 12, y, 1);
    spr_.drawString(right, 74, y, 1);
    y += 12;
  };

  // BLE (controller side): state, then RSSI + interval, then battery + rate
  const char* ble = s.receiving ? "streaming" : (s.connected ? "link up" : "waiting");
  section("BLE", col::green, ble, s.receiving ? col::text : col::textDim);
  if (s.rssi) snprintf(l, sizeof(l), "%ddBm", s.rssi);
  else        snprintf(l, sizeof(l), "-- dBm");
  if (s.connItvlUnits) snprintf(r, sizeof(r), "%dms", s.connItvlUnits * 5 / 4);
  else                 snprintf(r, sizeof(r), "-- ms");
  detail2(l, r);
  if (s.batteryKnown) snprintf(l, sizeof(l), "batt %u%%", s.battery);
  else                snprintf(l, sizeof(l), "batt --");
  snprintf(r, sizeof(r), "%uHz", s.rateHz);
  detail2(l, r);

  // USB (PC side)
  uint16_t usbCol = !bridge_.usbReady ? col::line
                    : bridge_.usbSuspended ? col::amber : col::green;
  const char* usb = !bridge_.usbReady ? "unplugged"
                    : bridge_.usbSuspended ? "suspended" : "active";
  section("USB", usbCol, usb, bridge_.usbReady ? col::text : col::textDim);
  snprintf(l, sizeof(l), "out %uHz", bridge_.usbHz);
  snprintf(r, sizeof(r), "lat %ums", bridge_.latencyMs);
  detail2(l, r);

  // PC inferred state
  const char* pc = !bridge_.usbReady ? "--" : (bridge_.usbSuspended ? "asleep" : "awake");
  uint16_t pcCol = !bridge_.usbReady ? col::textDim
                   : bridge_.usbSuspended ? col::amber : col::greenHi;
  section("PC", col::text, pc, pcCol);

  // Rumble activity (PC -> controller)
  bool active = (millis() - bridge_.lastRumbleMs < 400) && bridge_.rumbleActive();
  if (active) {
    uint8_t trig = bridge_.rmbLT > bridge_.rmbRT ? bridge_.rmbLT : bridge_.rmbRT;
    snprintf(l, sizeof(l), "L%u R%u T%u", bridge_.rmbLeft, bridge_.rmbRight, trig);
    section("RMBL", col::warn, l, col::warn);
  } else {
    section("RMBL", col::warn, "idle", col::textDim);
  }

  push();
}

// ---------------------------------------------------------------------------
// Transient: remote-wake fired at the sleeping PC
// ---------------------------------------------------------------------------
void Ui::waking() {
  clear();
  spr_.fillCircle(SCR_W / 2, 44, 22, col::green);
  usbGlyph(SCR_W / 2, 44, 12, col::bg);
  centerText("Waking PC", 84, 4, col::text);
  centerText("remote wake sent", 106, 1, col::textDim);
  push();
}

void Ui::identitySwitch(bool xusb, uint32_t nowMs) {
  (void)nowMs;  // static on the TFT; the LED backend carries the rhythm
  const uint16_t accent = xusb ? col::blue : col::greenHi;
  clear();
  spr_.fillCircle(SCR_W / 2, 40, 22, accent);
  usbGlyph(SCR_W / 2, 40, 12, col::bg);
  centerText("USB identity", 74, 2, col::text);
  centerText(xusb ? "Xbox 360 (XInput)" : "Xbox Series (HID)", 92, 2, accent);
  centerText("rebooting...", 112, 1, col::textDim);
  push();
}
