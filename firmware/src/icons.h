// Weather glyphs drawn with U8g2 primitives rather than loaded from a font.
//
// U8g2's bundled open-iconic weather set is fine, but its glyph codepoints are
// easy to get subtly wrong and the shapes are fixed at 8/16/24/48 px. Drawing
// discs, boxes and lines gives exactly the sun/cloud/rain/snow/storm vocabulary
// this display needs, at whatever size a screen asks for, for less flash.
#pragma once

#include <Arduino.h>
#include "weather.h"

namespace icons {

// Draw `ic` into a `size` x `size` box with its top-left at (x, y).
// Tuned for size 16 (home screen) and size 24 (weather screen).
void weather_icon(int x, int y, weather::Icon ic, uint8_t size = 16);

// Signal-strength staircase, 12x8, bars filled from `rssi` (dBm).
// rssi == 0 means "not associated" and draws a crossed-out box.
void wifi_bars(int x, int y, int32_t rssi, bool connected);

}  // namespace icons
