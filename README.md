# Xbox Controller USB Wake Bridge

[![Tests](https://github.com/rosuvlad/xbox-wake-hid-bridge/actions/workflows/tests.yml/badge.svg)](https://github.com/rosuvlad/xbox-wake-hid-bridge/actions/workflows/tests.yml)
[![Build & Release](https://github.com/rosuvlad/xbox-wake-hid-bridge/actions/workflows/release.yml/badge.svg)](https://github.com/rosuvlad/xbox-wake-hid-bridge/actions/workflows/release.yml)

An **ESP32-S3 bridge that turns a Bluetooth Xbox Wireless Controller into a
wired USB Xbox controller** — and lets the controller **wake a sleeping PC**.

It connects to the controller over Bluetooth LE, presents itself to the PC as a
genuine Xbox Wireless Controller over native USB, forwards every input with
minimal latency, forwards **rumble** back to the pad, and sends a **USB remote
wakeup** when you press the Guide button on a suspended PC.

It builds for two kinds of hardware from the same source:

* **LilyGO T-QT Pro** (default) — a 0.85" 128×128 colour screen gives a friendly
  pairing wizard and a live debug view.
* **Any generic ESP32-S3 dev board** (headless) — no screen; the onboard RGB LED
  shows state and the BOOT button re-pairs. See
  [Headless mode](#headless-mode-generic-esp32-s3--led). The bridge, USB and wake
  logic are identical — only the presentation layer differs, chosen at compile
  time by `UX_LED`.

---

## Host PC setup — required for wake (do this first)

> **Read this before anything else.** A perfectly flashed bridge still wakes
> nothing until the host is set up for it — this is where DIY consoles almost
> always get stuck, and it applies to every build (screen or headless). The
> flashing and hardware sections are below.

Three things must be true at once: the bridge stays **powered** while the box
sleeps, the **firmware** signals wake (✅ done — the config descriptor advertises
remote-wakeup and the Guide button drives USB resume signalling), and the **OS has
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
(and confirm the USB controller above it can wake). While there, also pin the
bridge's **runtime power management off**: power-tuning tools (powertop,
tlp, tuned's powersave profiles) may *autosuspend* an idle controller on a
fully awake desktop, and from the device's side of the cable that is
indistinguishable from the PC going to sleep — the bridge then breathes amber
and releases the pad as if the box were sleeping. The firmware handles it
gracefully, but pinning it off keeps the idle behavior honest:

```bash
# find the bridge (VID 045e / PID 0b13) and arm wake + disable autosuspend
for d in /sys/bus/usb/devices/*; do
  [ -f "$d/idVendor" ] || continue
  if [ "$(cat $d/idVendor)" = "045e" ] && [ "$(cat $d/idProduct)" = "0b13" ]; then
    echo "bridge at $d"
    echo enabled | sudo tee "$d/power/wakeup"
    echo on | sudo tee "$d/power/control"     # never autosuspend the bridge
  fi
done

# the xHCI controller must also be a wake source
grep -i xhc /proc/acpi/wakeup            # want *enabled*; if it says *disabled*:
# echo XHC | sudo tee /proc/acpi/wakeup  # toggles it (name may be XHC/XHC0/XHCI)
```

Make it **persistent** with a udev rule (`/etc/udev` is writable on Bazzite).
The second line covers the bridge's Xbox 360 identity, if you ever switch:

```bash
sudo tee /etc/udev/rules.d/99-xbox-wake-bridge.rules <<'EOF'
SUBSYSTEM=="usb", ATTRS{idVendor}=="045e", ATTRS{idProduct}=="0b13", ATTR{power/wakeup}="enabled", ATTR{power/control}="on"
SUBSYSTEM=="usb", ATTRS{idVendor}=="045e", ATTRS{idProduct}=="028e", ATTR{power/wakeup}="enabled", ATTR{power/control}="on"
EOF
sudo udevadm control --reload && sudo udevadm trigger
```

### 4. Test & verify

```bash
systemctl suspend                              # box sleeps
# press the Xbox/Guide button — the box should resume
dmesg | grep -iE 'wake|resume|xhci' | tail     # shows the wake source after resume
cat /sys/bus/usb/devices/*/power/wakeup         # the bridge's should read "enabled"
```

On resume the bridge briefly confirms it fired — **"Waking PC"** on the screen
build, a **white LED strobe** on the headless build. If nothing happens: re-check `power/wakeup` is `enabled`, ErP is
**off**, and (for `deep`) the port stays powered in sleep.

The wake stays armed even if the bridge itself resets mid-sleep (a brownout on
a marginal cable or port). It cannot re-enumerate while the PC is asleep, so
after ten seconds with no USB life it arms the wake anyway rather than waiting
for a host that only *it* can wake. Each boot prints its reset reason and a
running fault count over the serial console at 115200 baud:

```
boot: reset=1 brownouts=0 faults=0     # 1 = normal power-on
boot: reset=9 (BROWNOUT) brownouts=3 faults=0
```

Repeated brownouts point at power delivery, not firmware — try a shorter or
thicker USB cable, or a rear-panel port straight off the motherboard.

### Windows (if the same box dual-boots)

Device Manager → **Xbox Wireless Controller** → *Power Management* →
tick **"Allow this device to wake the computer"**, untick **"Allow the
computer to turn off this device to save power"** (Windows' selective suspend
of an idle controller looks like PC sleep to the bridge — same story as the
Linux autosuspend note above), and disable **Fast Startup**
(Control Panel → Power Options) so the machine uses real S3.

For actually *playing* on Windows, switch the bridge to its Xbox 360 identity —
see the next section.

---

## Two USB identities — Linux face and Windows face

The bridge can enumerate as either of two controllers. The choice is stored on
the device and survives reflashes and power cycles.

| Identity | Enumerates as | Best for | Why |
| --- | --- | --- | --- |
| **Xbox Series (HID)** — default | `045E:0B13`, "Xbox Wireless Controller" | **Linux / Bazzite** | The Series pad's own identity: exact 4-motor rumble (incl. trigger impulse), battery level, Share button. |
| **Xbox 360 (XInput)** | `045E:028E`, "Xbox 360 Controller" | **Windows** | Binds Windows' inbox XInput driver — the only route by which Steam and most Windows games see the bridge at all ([#6](https://github.com/rosuvlad/xbox-wake-hid-bridge/issues/6)). |

Windows never classifies the default identity as an Xbox controller (it is the
pad's *Bluetooth* PID arriving over USB — a combination in no Windows-side
database), so it shows up input-working but invisible to Steam. The Xbox 360
face fixes that natively. Its trade-offs, inherent to the 360 protocol: no
Share button, two-motor rumble (no trigger impulse), no battery level — which
is why it isn't the default on Linux, where the Series face is strictly better.

**To switch: hold `View + Menu + D-pad Down` on the controller for 3 seconds**
(pad connected, PC awake). The pad buzzes, the LED triple-flashes the target
face — **blue = Xbox 360**, **green = Xbox Series** (the TFT build prints it) —
and the bridge reboots into it. Hold the same chord again to switch back.
Verify on the PC: Device Manager (Windows) shows **Xbox 360 Controller** and
Steam lists it as one.

Ship a different first-boot default with `-DUSB_XUSB_DEFAULT=1` (the chord
still switches; NVS wins after the first boot). **Wake works identically in
both identities** — it operates below the driver layer.

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
| Dual USB identity (Series HID / X360 XInput for Windows) | ✅ pad-chord switchable, NVS-persisted |

---

## Roadmap

Features the bridge's position makes possible but that aren't built yet.

**Juggling multiple controllers** — these exploit the fact that the **NimBLE host
can hold several controller connections at once**, while the USB side is a
descriptor we fully control, so a single dongle can juggle multiple pads behind
whatever USB identity we choose.

* **Multiple pads → multiple XInput ports.** Pair 2–4 controllers and present
  them as up to four independent USB gamepads (a composite device, one HID
  interface per pad) — couch co-op from a single dongle, no per-player receiver.
* **Seamless hot-swap / failover.** When a controller's battery dies mid-session,
  switch to a second bonded pad *under the same USB identity*, so the game sees an
  uninterrupted controller and never drops the player.
* **Copilot mode.** Merge two physical controllers into one virtual pad — both
  people drive the same character. Useful for accessibility or for teaching a kid,
  mirroring the Xbox "Copilot" feature but for any BLE pad.

**Calibration & correction** — these exploit the other half of the bridge's
position: it **decodes and re-encodes every analog report**, so it can clean up
the stick and trigger values on the way through. Firmware-only, applied uniformly
in every game and on the desktop, with no anti-cheat exposure — it is calibration,
not automation.

* **Stick drift correction.** A radial deadzone kills jitter and mild drift; a
  learned centre offset fixes a biased rest position; a learned per-axis range
  rescales a worn stick back to full travel. Calibrated with an explicit gesture
  (e.g. hold View+Menu with the sticks untouched) so "at rest" is never guessed,
  and shown live on the existing pad-diagram view.
* **Trigger calibration & hair-trigger.** Fix a trigger that rests above zero or
  never reaches 100%, reshape its pull curve, or fire a digital press past a set
  threshold — the software version of the Elite controller's trigger locks. The
  triggers are model 1914's only analog buttons, so this is where any
  pressure-shaping lives (the face buttons are digital on the wire).

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

## Headless mode (generic ESP32-S3 + LED)

The T-QT Pro's screen is a debug luxury — the actual product (BLE pad ⇄ USB HID
+ wake) needs no display. The `S3-DevKit-LED` environment runs the exact same
bridge on any bare **ESP32-S3 dev board**, using its **onboard RGB LED** for
state and its **BOOT button** to re-pair. BLE, USB and the wake path are byte-for
-byte the same code; only the presentation layer is swapped (`UX_LED`, see
`src/Ux.h`).

> **Why ESP32-S3 specifically?** The bridge needs a *native USB device*
> controller (to publish the Xbox HID descriptor and call `tud_remote_wakeup()`)
> **and** BLE, on one chip. Only the ESP32-S3 has both. A classic ESP32 /
> WROOM-32 has BLE but no native USB (its USB socket is a CH340/CP2102 serial
> bridge); the S2 and P4 have USB but no BLE; the C3/C6/H2 USB is a fixed-function
> serial/JTAG port that can't be a custom HID device. So a "generic ESP32" board
> only works if it is an **ESP32-S3**.

### Which board / LED pin

Any ESP32-S3 board works, but the onboard-LED pin and flash size vary, so there
are **two ready-made release environments** — and a build flag for anything else:

| Board | Env / build | LED | Flash |
| --- | --- | --- | --- |
| **ESP32-S3-DevKitC-1 v1.1** | `pio run -e S3-DevKit-LED` | GPIO38 | 8 MB |
| **ESP32-S3 SuperMini** | `pio run -e S3-SuperMini-LED` | GPIO48 | 4 MB |
| ESP32-S3-DevKitC-1 v1.0 | `PLATFORMIO_BUILD_FLAGS="-DLED_PIN=48" pio run -e S3-DevKit-LED` | GPIO48 | 8 MB |
| Adafruit QT Py ESP32-S3 | `PLATFORMIO_BUILD_FLAGS="-DLED_PIN=39" pio run -e S3-DevKit-LED` | GPIO39¹ | 8 MB |
| Seeed XIAO ESP32S3 | `PLATFORMIO_BUILD_FLAGS="-DLED_KIND=LED_MONO -DLED_PIN=21 -DLED_ACTIVE_LOW=1" pio run -e S3-DevKit-LED` | GPIO21, plain | 8 MB |

¹ QT Py also needs GPIO38 driven high as LED power. Both release envs are built
and published by CI; the flag-override rows reuse an env's partition layout, so
match the flash size (the SuperMini env is the 4 MB one).

> **The LED pin is the one thing you can't detect from software.** DevKitC-1 v1.0
> vs v1.1 moved it from GPIO48 to GPIO38; a SuperMini is 48. If the LED stays
> dark after flashing, the pin is wrong — flip `LED_PIN`. To check in seconds,
> set `UI_ALIGN_TEST 1` in `Config.h` and reflash: the board cycles **red →
> green → blue** on boot when the pin is right, and stays dark when it isn't.

Mono boards (a single-colour LED, e.g. the XIAO) fold colour to PWM brightness,
so states separate by blink rhythm instead of hue.

### LED state map

| LED | Meaning |
| --- | --- |
| White ramp | booting |
| Blue breathe → fast blue blink | pairing: turn pad on → hold its Pair button |
| Green triple-flash | paired (bond saved) |
| Amber breathe (slow, 2 s) | reconnecting to a known pad |
| **Green, dim solid** | **streaming to an awake PC** (the normal good state) |
| Green breathe | pad linked, no PC seen since power-up (wrong port / flashed on the bench) |
| **Amber breathe (dim)** | **PC asleep, bridge armed** — press Guide to wake (an unplugged-but-powered board reads the same; the S3 has no VBUS sense) |
| Orange flash | rumble (brightness tracks motor strength) |
| White strobe | remote-wake sent |
| Blue or green triple-flash, then reboot | USB identity switched (blue = Xbox 360/XInput, green = Series) |
| Red ramp (deepening) | BOOT held — releases the bond at full red (2 s) |

### Controls

The two-button scheme collapses to one hold, because the T-QT's "pair new" and
"forget" long-presses were already the same operation (forget the bond + reboot
into pairing):

| Action | Result |
| --- | --- |
| **Xbox/Guide button** | wakes the PC when it is suspended (powers the pad on first — it reconnects, then the PC wakes) |
| **Hold BOOT ~2 s** | forget the controller and reboot into pairing (red ramp confirms) |
| **Hold View + Menu + D-pad Down 3 s** (on the pad) | switch USB identity — Series HID ↔ Xbox 360 XInput (blue/green flash, then reboot) |

### Building & flashing a devkit

```bash
pio run -e S3-DevKit-LED    -t upload --upload-port /dev/ttyUSB0   # DevKitC-1
pio run -e S3-SuperMini-LED -t upload --upload-port /dev/ttyACM0   # SuperMini
```

Flashing differs by how many USB ports the board has:

* **ESP32-S3-DevKitC-1 — two ports, easy.** It has a separate **UART port** (a
  CH340/CP2102 with its own auto-reset), so `esptool` drops it into the
  bootloader by itself — none of the "hold BOOT, unplug, replug" dance the
  [Flash](#flash) section describes for the screen board. Flash on the **UART**
  port; the **native USB** port (GPIO19/20) is the gamepad. That UART bridge is
  also a free **serial console**: because the firmware is HID-only
  (`ARDUINO_USB_CDC_ON_BOOT=0`), `Serial` maps to UART0, so `Serial.begin(115200)`
  prints there while native USB stays a pure gamepad — a stand-in for the
  on-screen diagnostics the headless build drops.

* **ESP32-S3 SuperMini — one port, download mode.** Its single Type-C **is** the
  native USB, so while the HID firmware runs there is no serial port to
  auto-reset (same as the T-QT). It has both buttons, so it's quick: **hold BOOT,
  tap RST, release BOOT** to enter download mode, then flash. One port means no
  serial console — use the LED (or `UI_ALIGN_TEST`) for feedback.

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
* **Remote wake (pad → PC):** when the PC sleeps the bridge, by default,
  **releases the BLE link so the pad powers itself down** — Guide light off,
  battery saved, like a real console (`-DSLEEP_RELEASES_PAD=0` keeps the pad
  connected all night instead). Pressing **Guide** powers the pad back on; it
  reconnects to the bridge, and the reconnect triggers USB resume signalling
  (driven at register level, with an ESP32 PHY-level disconnect pulse as the
  fallback — both for hosts that never armed remote wakeup, and for a root
  port powered down too deeply to receive resume signalling at all).
  With the release disabled, a Guide press on the still-connected pad fires
  the same wake path instantly. A 60 s grace after sleep
  (`PAD_RELEASE_GRACE_MS`) lets the pad finish its own link-loss search before
  the bridge listens again, so a still-searching pad can't fake a wake.
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
  Config.h          pins, colours, timings; the TFT vs LED (UX_LED) config split
  AppState.h        Screen + LivePage enums
  PadSnapshot.h     decoded controller state (UI-facing, no BLE types)
  BridgeState.h     USB / PC / rumble state  (UI-facing, no USB types)
  ControllerLink.h  NimBLE Xbox wrapper: connect, bond NVS, report builder, rumble
  UsbGamepad.h/.cpp TinyUSB HID device: descriptor, input, rumble output, wake
  Ux.h              picks the presentation backend at compile time (Ui | UiLed)
  Ui.h / Ui.cpp     full-screen TFT_eSprite renderer — every screen + widget
  UiLed.h/.cpp      headless backend: the same screens, rendered on one LED
  LedPattern.h      pure state→colour map (unit-tested; no Arduino)
  HoldButton.h      pure hold-to-forget detector (unit-tested; no Arduino)
test/               host-side Unity tests for LedPattern.h + HoldButton.h
board/              LilyGO board definition (esp32-s3-t-qt-pro.json)
lib/TFT_eSPI/       vendored, LilyGO-patched GC9A01 driver (do not upgrade)
scripts/            PlatformIO build hooks: FW_VERSION stamp, `mergebin` target
.github/workflows/  tests (native) + CI (build every board) + release (from main)
```

The layers are deliberately decoupled: BLE (`ControllerLink`) and USB
(`UsbGamepad`) feed plain structs (`PadSnapshot`, `BridgeState`) that the
presentation layer renders. That layer is a leaf: `TFT_eSPI` lives only in
`Ui.cpp`, so the headless build swaps it for `UiLed` (one LED) without touching
the bridge. The display is refreshed at ~30 fps from the main loop and **never
from a BLE or USB callback** (per spec).

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
| **Xbox/Guide button** | wakes the PC when it is suspended (powers the pad on first — it reconnects, then the PC wakes) |

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
# default 4MB/PSRAM board (LilyGO T-QT Pro, TFT)
pio run -e T-QT-Pro-N4R2

# 8MB / no-PSRAM variant
pio run -e T-QT-Pro-N8

# headless: generic ESP32-S3 dev board with onboard LED (see Headless mode)
pio run -e S3-DevKit-LED
```

### Unit tests

The pure logic — the LED state→colour map (`src/LedPattern.h`) and the
hold-to-forget detector (`src/HoldButton.h`) — is written Arduino-free (time is a
parameter, no pin access), so it runs on the host with no hardware:

```bash
pio test -e native
```

CI runs these on every push and **gates every board build on them** — nothing is
released while a test is red (see [Releases & CI](#releases--ci)).

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

## Releases & CI

GitHub Actions builds both boards on every push; merges to `main` publish a
release. Nothing is tagged or uploaded by hand.

| Where | What happens |
| --- | --- |
| feature branch | compiles both boards, publishes nothing |
| merge to `main` | compiles both boards, then tags and releases them |

Versions come from git tags and auto-increment the patch — the first release is
`v0.0.0`, and every merge after it reads the newest tag and bumps it (`v0.0.1`,
`v0.0.2`, …). The tag is created **after** a green build, so a failed build never
burns a version number. For a minor or major bump, push the tag yourself and the
next merge carries on from it:

```bash
git tag v0.1.0 && git push origin v0.1.0    # next merge to main -> v0.1.1
```

CI reads the board list straight out of `platformio.ini` — every `[env:NAME]` is
built and released, so adding a board there needs no workflow edit. (The one
exception is `[env:native]`, the host-side unit-test env, which the discovery
step filters out of the board matrix — it has no firmware to build.) Every board
build is gated on `pio test -e native` passing first, so a red test blocks both
CI and a release.

### What a release contains

One full set of images per board, e.g. for `v0.0.0` on `T-QT-Pro-N4R2`:

| Asset | Use |
| --- | --- |
| `…-merged.bin` | **the one you want** — the whole flash in one file, written at `0x0` |
| `…-firmware.bin` | just the app, for a board that already has a bootloader (`0x10000`) |
| `…-bootloader.bin` · `…-partitions.bin` · `…-boot_app0.bin` | the individual pieces |
| `…-flash-args.txt` | the offset each piece goes to |

### Flashing a release

No PlatformIO needed — a release flashes from the browser or with `esptool`
alone. Either way, put the board in **download mode** first (see
[Flash](#flash)): the HID-only firmware exposes no serial port, so nothing can
auto-reset it into the bootloader.

**From the browser, nothing to install** — open the
[ESPBoards web flasher](https://www.espboards.dev/tools/program/) in Chrome, Edge
or Opera (Firefox and Safari have no Web Serial API):

1. **Connect** → pick the board's COM/serial port; the ESP32-S3 is auto-detected.
2. **Flash firmware** → drag in `…-merged.bin`.
3. Set the address to **`0x0`** and flash.

> **The merged image goes to `0x0` — not `0x10000`.** It already contains the
> bootloader, partition table and app at their right places, so it starts at the
> very beginning of flash. `0x10000` is the app-only offset and belongs to
> `…-firmware.bin`; writing the merged image there produces a board that won't
> boot.

This is the easiest way to put firmware on a board that has never had PlatformIO
near it. To flash the individual images instead, use the offsets from that
board's `…-flash-args.txt` — which is exactly the "addresses from your build
output" the tool asks for.

**From the CLI:**

```bash
esptool.py --chip esp32s3 write_flash 0x0 \
  xbox-wake-hid-bridge-v0.0.0-T-QT-Pro-N4R2-merged.bin
```

### The build scripts

`platformio.ini` wires two scripts into every build, so a local build and a CI
build produce the same thing.

**`scripts/fw_version.py`** defines an `FW_VERSION` macro for every build — the
release tag in CI, `git describe --tags --always --dirty` locally. Nothing in
`src/` reads it yet; it exists so a build can identify itself (the diagnostics
screen is the obvious home for it). Override it for a one-off:

```bash
FW_VERSION=v9.9.9-test pio run -e T-QT-Pro-N4R2
```

**`scripts/merge_bin.py`** adds the `mergebin` target, which produces the release
images locally exactly as CI does:

```bash
pio run -e T-QT-Pro-N4R2 -t mergebin
# -> .pio/build/T-QT-Pro-N4R2/release/
#      merged.bin  firmware.bin  bootloader.bin  partitions.bin  boot_app0.bin
#      flash-args.txt
```

Useful for testing a release image before merging, or for flashing a board from
one file. It reads the flash offsets back out of the build environment instead of
hardcoding them, so the merged image always matches what `pio run -t upload`
would write — including the `qio`→`dio` remap the platform applies to the
bootloader header. ([PlatformIO has no built-in merge target.](https://github.com/platformio/platform-espressif32/issues/1078))

> Note: `board/esp32-s3-t-qt-pro.json` declares 4 MB for both variants, so the
> `T-QT-Pro-N8` image is built with a 4 MB flash size. That matches what
> `pio run -t upload` already writes, but it means the N8's upper 4 MB goes
> unused.

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
