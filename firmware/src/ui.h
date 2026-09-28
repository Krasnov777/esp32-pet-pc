// Screens and the mode machine.
//
// Everything is redrawn from scratch into U8g2's full frame buffer and pushed
// in one transfer — ~5 times a second for a still screen, 25 while something
// moves (typing, a transition, the screensaver). There is no page/partial
// update bookkeeping, which is what made the ESPHome version's
// `display.page.show` + `component.update` pairs (and the 1 Hz interval that
// re-showed the idle page forever) necessary in the first place.
#pragma once

#include <Arduino.h>

namespace ui {

// Values are stored in settings (the start screen, the retro screen mask), so they
// never change; the centre key's order is the ORDER table in ui.cpp.
enum class Mode : uint8_t {
    Clock      = 0,
    Weather    = 1,
    Info       = 2,
    BlockClock = 3,   // big PETSCII block digits
    Listing    = 4,   // live data as a BASIC program LISTing
    Basic      = 5,   // READY. prompt; the centre key runs commands
    Home       = 6,   // Home Assistant entities; the centre key picks and toggles
    COUNT      = 7,
};

void begin();

// Draw a frame if one is due. `force` skips the rate limit — used when
// something must be on the glass before a blocking call (the desk POST).
void tick(bool force = false);

void set_mode(Mode m);
void next_mode();          // next screen enabled in settings::retro().screens
void prev_mode();          // previous one
Mode mode();

// The centre key. Usually next screen / weather refresh. On the BASIC screen
// a press runs the next command and a hold leaves; on HOME a press moves the
// highlight through the toggleable entities (and past the last one, to the
// next screen) and a hold toggles the highlighted one. A press that only
// ends the screensaver does nothing else.
void center_short();
void center_long();

// Ask for a weather fetch now; says so on screen unless the tape sequence
// will (the centre hold, and the outer keys' "refresh weather" action).
void refresh_weather();

// Full-screen overlay. ms == 0 keeps it up until the next toast or clear.
void toast(const char* title, const char* sub, uint16_t ms);
void clear_toast();

// Boot/network screen — shown until set_mode() is called.
void boot_status(const char* line1, const char* line2);

// Tape-loading sequence. The main loop calls tape_start() instead of
// weather::fetch_now() when tape_wanted(); the sequence does the fetch itself
// at its LOADING step and reports the result as program output.
bool tape_wanted();
bool tape_busy();
void tape_start();

// Something is moving on the glass (effect, typing, screensaver, tape). The
// main loop holds background HA reads meanwhile, so they do not stutter it.
bool animating();

}  // namespace ui
