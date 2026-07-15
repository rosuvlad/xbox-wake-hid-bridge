# Xbox Controller USB Wake Bridge

An **ESP32-S3 bridge that turns a Bluetooth Xbox Wireless Controller into a
wired USB Xbox controller** — and lets the controller **wake a sleeping PC**.

It connects to the controller over Bluetooth LE, presents itself to the PC as a
genuine Xbox Wireless Controller over native USB, forwards every input with
minimal latency, forwards **rumble** back to the pad, and sends a **USB remote
wakeup** when you press the Guide button on a suspended PC. A 0.85" 128×128
colour screen gives a friendly pairing experience and a live debug view.

Hardware: **LilyGO T-QT Pro** (ESP32-S3, 128×128 GC9A01 TFT, two buttons).

---

## Status

| Area | State |
| --- | --- |
| Out-of-box pairing wizard (BLE) | ✅ working |
| Persistent bond + silent auto-reconnect (NVS) | ✅ working |
| Live debug views (values / pad diagram / bridge dashboard) | ✅ working |
| USB HID — emulates a real Xbox Wireless Controller | ✅ working (inputs verified on PC) |
| Rumble (PC → controller, all 4 motors) | ✅ path + on-connect self-test buzz + on-screen viz |
| USB remote wake (Guide button wakes a suspended PC) | ✅ firmware done — needs host-side wake enabled (see below) |
| Battery / rate / latency telemetry | ✅ shown on the Bridge page |
| RSSI, BLE connection interval | ✅ shown on the Bridge page (polled 1 Hz) |

---

## Why

This is built for **DIY [Bazzite](https://bazzite.gg/) game consoles** — homemade
couch/living-room boxes (mini-PCs, handhelds, custom builds) running the
SteamOS-like Bazzite distro. On a real console the gamepad turns the machine on
and rumbles; a DIY Bazzite box connected to a Bluetooth Xbox controller gets
neither, because the PC is asleep (S3) with Bluetooth powered down, so nothing is
listening for the controller.

This bridge gives a DIY console that console-like behaviour:

* the bridge — not the PC — stays paired to the controller over BLE while the box sleeps,
* press the **Xbox/Guide** button → the bridge sends a USB HID **remote wake** → the console resumes,
* all input is then forwarded over USB with **< 2 ms** of bridge overhead, and **rumble** flows back.

To the PC it looks exactly like a wired Xbox Wireless Controller, so Steam Input
and every game treat it as a first-class Xbox pad. No Wi-Fi or networking is
involved — the bridge is purely `BLE host` ⇄ `USB HID device`.

---

## Hardware — LilyGO T-QT Pro

**Board used:** LilyGO T-QT Pro (ESP32-S3) —
[AliExpress](https://www.aliexpress.com/item/1005012607153696.html) ·
[LilyGo store](https://www.lilygo.cc/products/t-qt-pro)

| Function | Pin | Notes |
| --- | --- | --- |
| Display (GC9A01, 128×128, HSPI) | BL=10, MOSI=2, SCLK=3, CS=5, DC=6, RST=1 | vendored patched `TFT_eSPI`, rotation 2 |
| Left button (BOOT) | GPIO0 | active-low, also the download-mode button |
| Right button | GPIO47 | active-low |
| Battery sense | GPIO4 | ADC (not yet used) |
| USB | native ESP32-S3 USB | the gamepad + wake interface |

Two chip variants are supported via PlatformIO environments:
`T-QT-Pro-N4R2` (4 MB flash, 2 MB PSRAM — **default**) and `T-QT-Pro-N8`
(8 MB flash, no PSRAM).

---

## Architecture

The bridge is a near-**transparent** relay: the report the controller emits over
BLE is *the same byte format* as a wired Xbox controller's USB HID report, and the
Xbox rumble report is *the same byte format* on both sides — so inputs and rumble
pass through almost 1:1.

```
        Bluetooth LE                         Native USB HID
 ┌───────────────┐  16-byte HID  ┌───────────────┐  16-byte HID   ┌──────────┐
 │ Xbox Wireless │ ───────────▶  │   ESP32-S3    │ ─────────────▶ │          │
 │  Controller   │   input       │    bridge     │  input (id 1)  │  Bazzite │
 │  (model 1914) │ ◀───────────  │ (T-QT Pro)    │ ◀───────────── │    PC    │
 └───────────────┘  8-byte rumble└───────────────┘  8-byte rumble └──────────┘
                                        │  (id 3)
                                        │  USB remote-wake  ▲ Guide button
                                        └───────── resume signalling ─┘
```

### Data paths

* **Input (pad → PC):** NimBLE notification → `XboxControllerNotificationParser`
  → `xboxNotif.toArr()` rebuilds the 16-byte report → `USBHID.SendReport(id 1)`.
  Forwarded on every fresh BLE notification (not frame-gated) for low latency.
* **Rumble (PC → pad):** host writes USB HID output report id 3 (8 bytes:
  enable-mask + 4 motor magnitudes 0–100 + duration/delay/loop) → forwarded
  verbatim to the controller's writable HID characteristic via
  `Core::writeHIDReport()`. No scaling — the two formats are identical.
* **Remote wake (pad → PC):** when the USB bus is suspended, an **Xbox/Guide
  button** press (edge-detected) calls `tud_remote_wakeup()`, which drives USB
  resume signalling to wake the host.
* **Bond persistence:** on first pair the controller MAC is stored in NVS
  (`Preferences`), so later boots reconnect silently and firmware updates never
  require re-pairing.

### Why NimBLE (not Bluepad32)

The controller is read with the **NimBLE**-based
`XboxSeriesXControllerESP32_asukiaaa` library — it matches the spec's stated BLE
stack, works well on the ESP32-S3 (BLE 5), decodes every input, and exposes
rumble via `XboxSeriesXHIDReportBuilder`. The USB side is native TinyUSB, an
independent peripheral, so both coexist on one chip.

### USB identity

We present **VID `0x045E` / PID `0x0B13`** ("Xbox Wireless Controller",
HID-only, CDC disabled) using the real **283-byte HID report descriptor** dumped
from an Xbox Series controller (model 1914, BLE). This makes Linux/Steam bind the
proper Xbox driver so button mapping and rumble "just work". Because CDC is off,
there is no USB serial console — debugging is done on-screen and PC-side
(`dmesg`, `jstest`).

### Source layout

```
src/
  main.cpp          state machine, button handling, the USB service loop
  Config.h          pins, colours, timings, the CGRAM offset toggle
  AppState.h        Screen + LivePage enums
  PadSnapshot.h     decoded controller state (UI-facing, no BLE types)
  BridgeState.h     USB / PC / rumble state  (UI-facing, no USB types)
  ControllerLink.h  NimBLE Xbox wrapper: connect, bond NVS, report builder, rumble
  UsbGamepad.h/.cpp TinyUSB HID device: descriptor, input, rumble output, wake
  Ui.h / Ui.cpp     full-screen TFT_eSprite renderer — every screen + widget
board/              LilyGO board definition (esp32-s3-t-qt-pro.json)
lib/TFT_eSPI/       vendored, LilyGO-patched GC9A01 driver (do not upgrade)
```

The three layers are deliberately decoupled: BLE (`ControllerLink`) and USB
(`UsbGamepad`) feed plain structs (`PadSnapshot`, `BridgeState`) that the UI
renders. The display is refreshed at ~30 fps from the main loop and **never from
a BLE or USB callback** (per spec).

---

## Screens & controls

### Controls

| Action | Result |
| --- | --- |
| **Short Left** | previous page |
| **Short Right** | next page |
| **Long Left** | pair a new controller (forgets + reboots into pairing) |
| **Long Right** | forget controller (confirm, then erase bond) |
| **Long Both** | diagnostics |
| **Xbox/Guide button** | wakes the PC when it is suspended |

### Out-of-box pairing (first run, or after "forget")

```
┌────────────────────┐   ┌────────────────────┐   ┌────────────────────┐
│ PAIRING       1/2  │   │ PAIRING       2/2  │   │        ✓✓✓         │
│  ▓▓▓░░░░  ░░░░░░░  │   │  ▓▓▓▓▓▓░  ▓▓▓▓▓▓░  │   │      Paired!       │
│       ( ⏻ )        │▶ │      (( ᛒ ))       │▶ │  Xbox pad   E:2A    │
│   Turn your        │   │   Hold the small   │   │  saved - won't     │
│  controller ON     │   │    PAIR button     │   │     ask again      │
│ press Xbox button  │   │  on the back ~3s   │   │                    │
└────────────────────┘   │    searching...    │   └────────────────────┘
                         └────────────────────┘        → live view
```

Known controller on boot → skips straight to **Reconnecting** ("Controller off —
waiting to reconnect"), then the live view when it powers on. A short
**connected buzz** fires on the pad as a rumble self-test.

### Live views (cycle with Left/Right)

```
   VALUES (default)            PAD DIAGRAM
┌────────────────────┐   ┌────────────────────┐
│ ᛒ ⇄     ● ○ ○   🔋 │   │ ᛒ ⇄     ○ ● ○   🔋 │
│ LS  x+0.42 y-0.88  │   │ LT[▓▓░]    [░░░]RT  │
│ RS  x-0.05 y+0.01  │   │ [ LB ]      [ RB ] │
│ LT [====   ] 72%   │   │   ╭─╮        (Y)   │
│ RT [       ]  0%   │   │   │o│      (X)(B)  │
│ HELD  A RB XBOX    │   │   ╰─╯        (A)   │
│ batt 64%    120 Hz │   │  ✛  V G M    ╭─╮   │
└────────────────────┘   │ dpad         │o│  │
                         └────────────────────┘

   BRIDGE  (BLE ⇄ USB ⇄ PC)
┌────────────────────────┐
│ ᛒ ⇄        ○ ○ ●    🔋 │
│ BLE   streaming        │
│   -54dBm       15ms    │  ← RSSI · BLE connection interval
│   batt 64%     120Hz   │  ← controller battery · BLE input rate
│ USB   active           │
│   out 118Hz   lat 0ms  │  ← USB report rate · BLE→USB latency
│ PC    awake            │
│ RMBL  L80 R40 T0       │  ← live rumble motor levels (idle when 0)
└────────────────────────┘
```

* **Values** — normalised sticks (−1..+1), trigger %, live held-button list,
  battery, USB report rate.
* **Pad** — buttons light in their colours, sticks are moving dots, triggers are
  bars; orange edge bars show live **rumble** grip power.
* **Bridge** — both link halves with **RSSI** and **BLE connection interval**,
  inferred PC state, USB report rate and BLE→USB latency, and live rumble motor
  levels.

Status bar (all live pages): **ᛒ** BLE state · **⇄** USB state (grey unplugged /
amber PC-suspended / green active) · page dots · battery.

### Other

```
   WAKING PC              DIAGNOSTICS (long both)      FORGET (long right)
┌────────────────┐    ┌────────────────────┐    ┌────────────────────┐
│      (⇄)       │    │ DIAGNOSTICS        │    │        /!\         │
│   Waking PC    │    │ MAC aa:bb:..:2a    │    │      Forget        │
│ remote wake    │    │ Link STREAMING     │    │    controller?     │
│    sent        │    │ Rate 120 Hz ...    │    │ hold RIGHT confirm │
└────────────────┘    └────────────────────┘    └────────────────────┘
```

---

## Build

Requires [PlatformIO](https://platformio.org/) (`pio` CLI or the VS Code
extension). All libraries are pinned in `platformio.ini` and fetched
automatically; `lib/TFT_eSPI` is vendored (the LilyGO-patched GC9A01 driver —
**do not** replace it with the upstream library or the panel init breaks).

```bash
# default 4MB/PSRAM board
pio run -e T-QT-Pro-N4R2

# 8MB / no-PSRAM variant
pio run -e T-QT-Pro-N8
```

Pinned dependencies: `NimBLE-Arduino@1.4.3`,
`XboxSeriesXControllerESP32_asukiaaa@1.0.9` (pulls
`XboxControllerNotificationParser` + `XboxSeriesXHIDReportBuilder`), `OneButton`,
`TFT_eSPI` (vendored), `Preferences`, `USB` (core). Platform
`espressif32@6.12.0` (arduino-esp32 2.0.17).

> First build note: if `esptool` fails with `No module named 'intelhex'`, run
> `~/.platformio/penv/Scripts/python -m pip install intelhex` once.

---

## Flash

```bash
pio run -e T-QT-Pro-N4R2 -t upload --upload-port COM3   # adjust port
```

Because the firmware runs **HID-only** (no USB CDC), the board does **not**
expose a serial port while the app is running, so esptool can't auto-reset it
into the bootloader. Put it into **download mode** first:

1. Keep the USB cable connected.
2. **Press and hold the LEFT button** (BOOT / GPIO0).
3. While holding, **unplug and replug USB** (or tap RST if present).
4. **Release** the button — a COM port reappears; run the upload.

### Display alignment test

`Config.h` has `#define UI_ALIGN_TEST 0`. Set it to `1` to boot into a
green/coloured-border alignment pattern used to measure the GC9A01 CGRAM edge
offset (this panel needed `rowstart = 1` for rotation 2, fixed in
`lib/TFT_eSPI/TFT_Drivers/GC9A01_Rotation.h`). Leave it `0` for normal use.

---

## Remote wake — BIOS & OS setup

Three things must be true at once: the bridge stays **powered** while the box
sleeps, the **firmware** signals wake (✅ done — the config descriptor advertises
remote-wakeup and the Guide button calls `tud_remote_wakeup()`), and the **OS has
armed** this device as a wake source. The rest is host configuration, and on a
DIY Bazzite console it's where wake usually fails.

### 1. BIOS / UEFI

Enter setup (Del/F2 at boot) and set these wherever your board exposes them
(names vary by vendor):

| Setting | Set to | Why |
| --- | --- | --- |
| **ErP / EuP Ready** | **Disabled** | ErP cuts +5V standby to the USB ports in sleep, so the bridge loses power and can't signal. **The single most common cause of "won't wake."** |
| **Deep Sleep / Deep Sx** | **Disabled** | "Deep Sx" powers USB down in S3; disabling keeps the ports live. |
| **Wake on USB / USB Wake Support / Resume by USB / Power On By USB** | **Enabled** | Lets a USB device resume the system. |
| **XHCI Hand-off** | **Enabled** | Hands the USB (xHCI) controller to the OS. |
| (laptops/handhelds) **USB power in sleep / charging** | **Enabled** | Keeps the port powered while suspended. |

Plug the bridge into a **rear / motherboard USB port** where possible —
front-panel headers and external hubs are far less reliably powered or
wake-capable in sleep.

### 2. Pick the sleep state (Bazzite / Linux)

Check which suspend modes the kernel offers (the one in `[brackets]` is active):

```bash
cat /sys/power/mem_sleep      # e.g. "s2idle [deep]"  or  "[s2idle] deep"
```

* **`deep`** = ACPI **S3** (suspend-to-RAM) — the classic path; USB remote-wake
  works via bus resume signalling.
* **`s2idle`** = suspend-to-idle (freeze) — also wakeable by USB, but *only* if
  the device's runtime wakeup is enabled (step 3). Many modern boards default to
  `s2idle`; if yours supports `deep`, prefer it.

Force S3 persistently (immutable-OS friendly — Bazzite is atomic/rpm-ostree):

```bash
rpm-ostree kargs --append=mem_sleep_default=deep    # then reboot
```

### 3. Arm the bridge as a wake source — the #1 Linux gotcha

USB HID devices default to **wakeup disabled**. Enable it for our emulated pad
(and confirm the USB controller above it can wake):

```bash
# find the bridge (VID 045e / PID 0b13) and enable its wakeup
for d in /sys/bus/usb/devices/*; do
  [ -f "$d/idVendor" ] || continue
  if [ "$(cat $d/idVendor)" = "045e" ] && [ "$(cat $d/idProduct)" = "0b13" ]; then
    echo "bridge at $d"; echo enabled | sudo tee "$d/power/wakeup"
  fi
done

# the xHCI controller must also be a wake source
grep -i xhc /proc/acpi/wakeup            # want *enabled*; if it says *disabled*:
# echo XHC | sudo tee /proc/acpi/wakeup  # toggles it (name may be XHC/XHC0/XHCI)
```

Make it **persistent** with a udev rule (`/etc/udev` is writable on Bazzite):

```bash
echo 'SUBSYSTEM=="usb", ATTRS{idVendor}=="045e", ATTRS{idProduct}=="0b13", ATTR{power/wakeup}="enabled"' \
  | sudo tee /etc/udev/rules.d/99-xbox-wake-bridge.rules
sudo udevadm control --reload && sudo udevadm trigger
```

### 4. Test & verify

```bash
systemctl suspend                              # box sleeps
# press the Xbox/Guide button — the box should resume
dmesg | grep -iE 'wake|resume|xhci' | tail     # shows the wake source after resume
cat /sys/bus/usb/devices/*/power/wakeup         # the bridge's should read "enabled"
```

On resume the device's screen briefly shows **"Waking PC"** to confirm the
firmware fired. If nothing happens: re-check `power/wakeup` is `enabled`, ErP is
**off**, and (for `deep`) the port stays powered in sleep.

### Windows (if the same box dual-boots)

Device Manager → **Xbox Wireless Controller** → *Power Management* →
tick **"Allow this device to wake the computer"**, and disable **Fast Startup**
(Control Panel → Power Options) so the machine uses real S3.

---

## References / used docs

* **Board** — [LilyGO T-QT](https://github.com/Xinyuan-LilyGO/T-QT) (pinout, patched `TFT_eSPI`/GC9A01, board JSON)
* **BLE controller** — [asukiaaa/XboxSeriesXControllerESP32](https://github.com/asukiaaa/arduino-XboxSeriesXControllerESP32),
  [XboxControllerNotificationParser](https://github.com/asukiaaa/arduino-XboxControllerNotificationParser),
  [XboxSeriesXHIDReportBuilder](https://github.com/asukiaaa/arduino-XboxSeriesXHIDReportBuilder)
* **BLE stack** — [h2zero/NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino)
* **USB HID descriptor** — [DJm00n/ControllersInfo](https://github.com/DJm00n/ControllersInfo)
  (`xboxone_model_1914_bluetoothle_hid_report_descriptor`)
* **Display** — [Bodmer/TFT_eSPI](https://github.com/Bodmer/TFT_eSPI)
* **Buttons** — [mathertel/OneButton](https://github.com/mathertel/OneButton)
