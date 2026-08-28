# Vendored libraries

PlatformIO compiles everything in this directory automatically, so each folder
here is a third-party library kept in-tree rather than fetched at build time.
Two different reasons are at work.

## Shipped with the board

`TFT_eSPI`, `lvgl` (+ `lv_conf.h`), `OneButton` and
`SparkFun_BNO080_Arduino_Library` arrived with LilyGO's T-QT Pro examples.
`TFT_eSPI` in particular carries the panel wiring for this exact board in its
`User_Setup`, which the registry copy does not, so it cannot be swapped for a
`lib_deps` entry without re-deriving that configuration.

The headless builds don't use any of it, and `platformio.ini` `lib_ignore`s
`TFT_eSPI` and `OneButton` there — see the note in that file about why the
dependency finder needs telling explicitly.

## Pinned, then unpublished

`XboxSeriesXControllerESP32_asukiaaa` (1.0.9) and its two dependencies,
`XboxControllerNotificationParser` (1.0.4) and
`XboxSeriesXHIDReportBuilder_asukiaaa` (1.0.1), were `lib_deps` entries until
2026-08, when the registry unpublished the whole 1.0.x line — 1.1.1 is now the
oldest version it will serve. Every CI build began failing with
`UnknownPackageError`, on branches whose code had not changed.

They are vendored verbatim at the versions the pin always named. Upgrading to
1.1.x may well be fine, but the BLE pad link is the one part of this firmware
that cannot be verified without an Xbox controller and a board in hand, and a
registry outage is the worst possible moment to find out. Vendoring keeps the
known-good code and takes the build off someone else's release schedule.

Unmodified from upstream (MIT, © Asuki Kono):
<https://github.com/asukiaaa/arduino-XboxSeriesXControllerESP32>. To move to
1.1.x later, delete these three folders and restore the `lib_deps` line.
