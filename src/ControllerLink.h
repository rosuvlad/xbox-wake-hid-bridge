// ControllerLink.h — thin wrapper around the NimBLE Xbox library.
//
// Responsibilities:
//   * own the asukiaaa Core (targeted reconnect when a pad is bonded,
//     open scan when pairing a new one)
//   * remember the bonded pad's MAC in NVS so upgrades/reboots don't re-pair
//   * turn raw Xbox reports into a normalised PadSnapshot
//   * measure the input report rate
//
// Kept header-only + inline; it is included only by main.cpp.
#pragma once

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <XboxSeriesXControllerESP32_asukiaaa.hpp>

#include "Config.h"
#include "PadSnapshot.h"

class ControllerLink {
 public:
  void begin() {
    prefs_.begin("xbridge", false);
    bondAddr_ = prefs_.getString("padAddr", "");
    prefs_.end();

    // A stored MAC → connect only to that pad (fast, silent reconnect).
    // No MAC → open scan so any Xbox pad in pairing mode can be adopted.
    core_ = new XboxSeriesXControllerESP32_asukiaaa::Core(bondAddr_);
    core_->begin();
  }

  void loop() {
    if (!core_ || released_) return;
    core_->onLoop();

    // Rate meter: the library stamps every notification; count the changes.
    unsigned long at = core_->getReceiveNotificationAt();
    if (at != lastNotifAt_) {
      lastNotifAt_ = at;
      rateAccum_++;
    }
    unsigned long now = millis();
    if (now - rateWindowStart_ >= 1000) {
      rateHz_ = rateAccum_;
      rateAccum_ = 0;
      rateWindowStart_ = now;
      // Link quality once per second — getRssi() is an HCI round-trip, so it
      // must not run every frame or from a callback.
      auto* c = XboxSeriesXControllerESP32_asukiaaa::pConnectedClient;
      if (c && c->isConnected()) {
        rssi_ = (int8_t)c->getRssi();
        connItvl_ = c->getConnInfo().getConnInterval();
      } else {
        rssi_ = 0;
        connItvl_ = 0;
      }
    }
  }

  // Host asleep: let go of the pad so it can power itself down (Guide light
  // off). While released, loop() neither services the BLE core nor scans, so
  // the pad sees a dead link and sleeps after its own search timeout. The
  // library only ever reconnects from onLoop() — its disconnect callback just
  // flags state — so skipping onLoop() is a complete freeze; stopping the
  // (4 s burst) scan closes the one in-flight window. Un-releasing resumes
  // the bonded targeted reconnect.
  void setReleased(bool on) {
    if (on == released_) return;
    released_ = on;
    if (!on) return;
    auto* c = XboxSeriesXControllerESP32_asukiaaa::pConnectedClient;
    if (c && c->isConnected()) c->disconnect();
    NimBLEDevice::getScan()->stop();
  }

  bool isReleased() const { return released_; }

  bool isConnected() const { return core_ && core_->isConnected(); }

  bool isReceiving() const {
    return core_ && core_->isConnected() &&
           !core_->isWaitingForFirstNotification();
  }

  uint8_t failedCount() const {
    return core_ ? core_->getCountFailedConnection() : 0;
  }

  // millis() stamp of the last input notification (0 if none) — used to
  // detect fresh input and forward it to USB with minimal latency.
  unsigned long receiveNotificationAt() const {
    return core_ ? core_->getReceiveNotificationAt() : 0;
  }

  // Rebuild the controller's own 16-byte HID input report from the parsed
  // state. That is byte-identical to the USB HID input report we expose.
  bool buildUsbReport(uint8_t* buf16) {
    return core_ && core_->xboxNotif.toArr(buf16, 16) == 0;
  }

  // Forward a raw Xbox rumble report (8 bytes, report-id 3 payload) straight
  // to the controller's writable HID characteristic.
  void sendRawRumble(const uint8_t* data, size_t len) {
    if (core_ && core_->isConnected()) core_->writeHIDReport((uint8_t*)data, len);
  }

  // Fire a short buzz on all four motors (self-test / "connected" feedback).
  // power 0..100, duration in hundredths of a second.
  void pulseRumble(uint8_t power, uint8_t centiSec) {
    if (!core_ || !core_->isConnected()) return;
    uint8_t r[8] = {0x0F, power, power, power, power, centiSec, 0, 0};
    core_->writeHIDReport(r, sizeof(r));
  }

  bool hasBond() const { return bondAddr_.length() > 0; }

  // Cheap single-button probe (no snapshot allocation) for the wake trigger.
  bool xboxPressed() const { return core_ && core_->xboxNotif.btnXbox; }

  // The USB-identity chord (View + Menu + D-pad Down), sampled as cheaply as
  // xboxPressed() — this runs every loop iteration.
  bool identityComboPressed() const {
    if (!core_) return false;
    const auto& n = core_->xboxNotif;
    return n.btnSelect && n.btnStart && n.btnDirDown;
  }

  // Persist whatever pad we are currently linked to as THE bonded pad.
  void saveBondFromCurrent() {
    if (!core_ || !core_->isConnected()) return;
    String addr = core_->buildDeviceAddressStr();
    if (addr.length() == 0) return;
    bondAddr_ = addr;
    prefs_.begin("xbridge", false);
    prefs_.putString("padAddr", addr);
    prefs_.end();
  }

  // Erase the bond. Caller restarts to drop back into the pairing OOBE.
  void forgetBond() {
    prefs_.begin("xbridge", false);
    prefs_.remove("padAddr");
    prefs_.end();
    bondAddr_ = "";
  }

  PadSnapshot snapshot() const {
    PadSnapshot s;
    if (!core_) return s;
    s.connected = core_->isConnected();
    s.receiving = isReceiving();
    s.addr = core_->buildDeviceAddressStr();
    s.rateHz = rateHz_;
    s.rssi = rssi_;
    s.connItvlUnits = connItvl_;

    // Lib 1.0.9 has no battery-received timestamp; it stays 0 until the first
    // battery notification arrives, so treat any non-zero value as "known".
    if (core_->battery > 0) {
      s.batteryKnown = true;
      s.battery = core_->battery > 100 ? 100 : core_->battery;
    }

    const auto& n = core_->xboxNotif;
    s.a = n.btnA;    s.b = n.btnB;   s.x = n.btnX;   s.y = n.btnY;
    s.lb = n.btnLB;  s.rb = n.btnRB;
    s.ls = n.btnLS;  s.rs = n.btnRS;
    s.view = n.btnSelect; s.menu = n.btnStart;
    s.xbox = n.btnXbox;   s.share = n.btnShare;
    s.up = n.btnDirUp;    s.down = n.btnDirDown;
    s.left = n.btnDirLeft; s.right = n.btnDirRight;

    s.lx = normAxis(n.joyLHori, false);
    s.ly = normAxis(n.joyLVert, INVERT_STICK_Y);
    s.rx = normAxis(n.joyRHori, false);
    s.ry = normAxis(n.joyRVert, INVERT_STICK_Y);
    s.lt = n.trigLT / 1023.0f;
    s.rt = n.trigRT / 1023.0f;
    return s;
  }

 private:
  // uint16 axis 0..65535 (centre 32767) -> -1..+1, optional inversion.
  static float normAxis(uint16_t v, bool invert) {
    float f = (v / 65535.0f) * 2.0f - 1.0f;
    if (f > 1.0f) f = 1.0f;
    if (f < -1.0f) f = -1.0f;
    return invert ? -f : f;
  }

  XboxSeriesXControllerESP32_asukiaaa::Core* core_ = nullptr;
  Preferences prefs_;
  String bondAddr_;
  bool released_ = false;

  unsigned long lastNotifAt_ = 0;
  uint16_t rateAccum_ = 0;
  uint16_t rateHz_ = 0;
  unsigned long rateWindowStart_ = 0;
  int8_t rssi_ = 0;         // dBm, cached 1 Hz
  uint16_t connItvl_ = 0;   // connection interval, 1.25 ms units, cached 1 Hz
};
