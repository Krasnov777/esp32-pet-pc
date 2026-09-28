// What the outer keys do. Each key has a press and a hold action, set on the
// web page (settings::Keys): an HTTP request, a screen, next/previous screen,
// a weather refresh, or a Home Assistant toggle / service call.
//
// Actions that go to the network block, like the 1.x desk POST did: the
// toast with "sending..." is on the glass first, the result replaces it.
#pragma once

#include <Arduino.h>

namespace actions {

enum class Side : uint8_t { Left = 0, Right = 1 };

// Run the configured action for a press (hold = false) or a hold. Returns a
// short result for the web UI: "sent", "failed (404)", "no wifi", "done",
// "nothing set"…
const char* run(Side side, bool hold);

}  // namespace actions
