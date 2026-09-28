// CRT-style effects, applied to the finished frame just before it is sent.
//
// A screen draws itself as usual; apply() then rewrites the U8g2 buffer. The
// wipe and the roll need the frame that was on the glass before the change,
// so start() snapshots the buffer — which still holds the last frame sent,
// because nothing clears it until the next draw.
#pragma once

#include <Arduino.h>

namespace fx {

enum class Kind : uint8_t {
    None,
    Wipe,       // the new screen is painted in behind a scan line, top to bottom
    Roll,       // vertical-hold slip: old frame rolls up, new one follows it in
    Collapse,   // power-off: squeeze to a bright line, shrink the line to a dot
    Expand,     // power-on: the reverse
};

void start(Kind k);
void cancel();
bool active();
Kind kind();

// Post-process the freshly drawn frame. Ends the effect when its time is up;
// a finished Collapse leaves the buffer dark.
void apply();

}  // namespace fx
