#include "timekeeper.h"
#include "settings.h"

#include <sys/time.h>

namespace timekeeper {
namespace {

// Anything before 2023-01-01 is the RTC's uninitialised value, not a real time.
constexpr time_t PLAUSIBLE_AFTER = 1672531200;

uint32_t last_sync_ms = 0;
bool     have_time    = false;

}  // namespace

void begin() {
    const auto& c = settings::state();
    // configTzTime is the ESP32 core's TZ-string variant (the ESP8266 core
    // overloaded configTime for this); DST rules come from the POSIX string.
    configTzTime(c.tz, c.ntp, "time.google.com");
    Serial.printf("[time] SNTP %s, TZ %s\n", c.ntp, c.tz);
}

void tick() {
    time_t now = time(nullptr);
    if (now > PLAUSIBLE_AFTER) {
        if (!have_time) Serial.println(F("[time] first sync"));
        have_time    = true;
        last_sync_ms = millis();
    }
}

bool synced() { return have_time; }

uint32_t synced_ago_s() {
    return have_time ? (millis() - last_sync_ms) / 1000 : UINT32_MAX;
}

bool local(struct tm& out) {
    if (!have_time) return false;
    time_t now = time(nullptr);
    localtime_r(&now, &out);
    return true;
}

void format_time(char* buf, size_t cap) {
    struct tm t;
    if (!local(t)) { strlcpy(buf, "--:--", cap); return; }
    if (settings::state().clock_24h) {
        snprintf(buf, cap, "%02d:%02d", t.tm_hour, t.tm_min);
    } else {
        int h = t.tm_hour % 12;
        if (h == 0) h = 12;
        snprintf(buf, cap, "%d:%02d", h, t.tm_min);
    }
}

void format_date(char* buf, size_t cap) {
    struct tm t;
    if (!local(t)) { strlcpy(buf, "no time", cap); return; }
    strftime(buf, cap, "%a %d %b", &t);
}

}  // namespace timekeeper
