// Debounced edge detection for the three front buttons.
//
// The ESPHome config read these pins raw with INPUT_PULLUP and no `inverted:`
// flag, which meant its binary sensors were on while the button was *released*
// and `on_press:` actually fired on release. Here the polarity is explicit:
// pressed == LOW, and every event is debounced.
#pragma once

#include <Arduino.h>

namespace buttons {

enum class Id : uint8_t { Left = 0, Center = 1, Right = 2, COUNT = 3 };

enum class Event : uint8_t {
    None,
    Short,   // released before LONG_MS
    Long,    // fired the moment the hold passes LONG_MS (no release needed)
};

void begin();
void tick();

// Consume the pending event for a button (returns None if there is none).
Event take(Id id);

// True while the button is physically held down.
bool held(Id id);

// millis() of the most recent press edge on any of the three — the wake signal
// for the display's night-blank window. Stays 0 until the first press, and is
// never consumed, so reading it cannot swallow a button event.
uint32_t last_activity_ms();

}  // namespace buttons
