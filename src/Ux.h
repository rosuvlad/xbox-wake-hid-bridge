// Ux.h — picks the presentation backend at compile time.
//
// Both backends expose the same methods, so main.cpp's state machine is written
// once and never learns which one it drives. The TFT build sees a screen; the
// headless build sees one LED and one button.
//
// The build must also exclude the other backend's .cpp and its libraries — see
// build_src_filter / lib_ignore in platformio.ini. PlatformIO compiles all of
// src/ and its dependency finder does not evaluate #if, so this header alone is
// not enough to keep TFT_eSPI out of a headless image.
#pragma once

#if UX_LED
#include "UiLed.h"
using Ux = UiLed;
#else
#include "Ui.h"
using Ux = Ui;
#endif
