// SNTP-backed wall clock.
//
// The timezone is a POSIX TZ string rather than a fixed UTC offset, so DST
// changeovers are handled by the C library instead of by a hand-rolled rule
// (the default is Europe/London). Configurable from the web UI.
#pragma once

#include <Arduino.h>
#include <time.h>

namespace timekeeper {

void begin();       // apply TZ + start SNTP (safe to call again after a config change)
void tick();

bool synced();                  // true once SNTP has delivered a plausible time
uint32_t synced_ago_s();        // seconds since the last successful sync

// Fills `out` with local time. Returns false before the first sync.
bool local(struct tm& out);

// "14:32" / "2:32" honouring settings.clock_24h.
void format_time(char* buf, size_t cap);
// "Wed 17 Sep"
void format_date(char* buf, size_t cap);

}  // namespace timekeeper
