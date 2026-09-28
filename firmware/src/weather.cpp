#include "weather.h"
#include "settings.h"

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

namespace weather {
namespace {

constexpr uint32_t RETRY_MS   = 30 * 1000;
constexpr uint32_t TIMEOUT_MS = 8000;

Current  cur = {};
Status   st  = Status::Fetching;
uint32_t last_ok_ms  = 0;
uint32_t last_try_ms = 0;
bool     have_data   = false;
bool     force       = false;

struct Kind { Icon day; Icon night; const char* desc; };

// WMO weather codes as used by Open-Meteo's `weather_code`.
Kind kind_of(int code) {
    switch (code) {
        case 0:  return {Icon::Sun,       Icon::Moon,       "Clear"};
        case 1:  return {Icon::PartlySun, Icon::PartlyMoon, "Mainly clear"};
        case 2:  return {Icon::PartlySun, Icon::PartlyMoon, "Partly cloudy"};
        case 3:  return {Icon::Cloud,     Icon::Cloud,      "Overcast"};
        case 45: case 48:
                 return {Icon::Fog,       Icon::Fog,        "Fog"};
        case 51: case 53: case 55:
                 return {Icon::Rain,      Icon::Rain,       "Drizzle"};
        case 56: case 57:
                 return {Icon::Rain,      Icon::Rain,       "Freezing drizzle"};
        case 61: case 63: case 65:
                 return {Icon::Rain,      Icon::Rain,       "Rain"};
        case 66: case 67:
                 return {Icon::Rain,      Icon::Rain,       "Freezing rain"};
        case 71: case 73: case 75: case 77:
                 return {Icon::Snow,      Icon::Snow,       "Snow"};
        case 80: case 81: case 82:
                 return {Icon::Rain,      Icon::Rain,       "Showers"};
        case 85: case 86:
                 return {Icon::Snow,      Icon::Snow,       "Snow showers"};
        case 95: return {Icon::Thunder,   Icon::Thunder,    "Thunderstorm"};
        case 96: case 99:
                 return {Icon::Thunder,   Icon::Thunder,    "Storm, hail"};
        default: return {Icon::Cloud,     Icon::Cloud,      "Clouds"};
    }
}

bool fetch() {
    const auto& c = settings::state();

    // Sized for the longest query below plus a full-length custom host —
    // a truncated URL would fail in a way that looks like an API outage.
    char url[384];
    snprintf(url, sizeof(url),
             "http://%s/v1/forecast?latitude=%.4f&longitude=%.4f"
             "&current=temperature_2m,relative_humidity_2m,apparent_temperature,"
             "weather_code,wind_speed_10m,wind_direction_10m,is_day"
             "&daily=temperature_2m_max,temperature_2m_min,precipitation_probability_max"
             "&forecast_days=1&timezone=auto",
             c.weather_host, c.lat, c.lon);

    WiFiClient  net;
    HTTPClient  http;
    http.setTimeout(TIMEOUT_MS);
    // HTTP/1.0 → no chunked transfer encoding, which keeps getStream() a plain
    // byte stream that ArduinoJson can read straight through.
    http.useHTTP10(true);
    if (!http.begin(net, url)) {
        Serial.println(F("[weather] malformed URL"));
        return false;
    }

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        Serial.printf("[weather] HTTP %d\n", code);
        http.end();
        return false;
    }

    // Parse straight off the socket through a filter, so only these fields are
    // ever allocated — the full response is ~1.5 KB, the filtered document a
    // couple hundred bytes.
    JsonDocument filter;
    JsonObject fc = filter["current"].to<JsonObject>();
    fc["temperature_2m"]        = true;
    fc["relative_humidity_2m"]  = true;
    fc["apparent_temperature"]  = true;
    fc["weather_code"]          = true;
    fc["wind_speed_10m"]        = true;
    fc["wind_direction_10m"]    = true;
    fc["is_day"]                = true;
    JsonObject fd = filter["daily"].to<JsonObject>();
    fd["temperature_2m_max"]          = true;
    fd["temperature_2m_min"]          = true;
    fd["precipitation_probability_max"] = true;

    JsonDocument doc;
    DeserializationError err =
        deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    http.end();
    if (err) {
        Serial.printf("[weather] parse: %s\n", err.c_str());
        return false;
    }

    JsonObjectConst j = doc["current"];
    if (j.isNull() || !j["temperature_2m"].is<float>()) {
        Serial.println(F("[weather] response missing current block"));
        return false;
    }

    Current n = {};
    n.temp_c    = j["temperature_2m"]       | 0.0f;
    n.feels_c   = j["apparent_temperature"] | n.temp_c;
    n.humidity  = (uint8_t)(j["relative_humidity_2m"] | 0);
    n.wind_kmh  = j["wind_speed_10m"]       | 0.0f;
    n.wind_deg  = j["wind_direction_10m"]   | 0;
    n.day_max_c = doc["daily"]["temperature_2m_max"][0] | n.temp_c;
    n.day_min_c = doc["daily"]["temperature_2m_min"][0] | n.temp_c;
    n.rain_prob = (uint8_t)(doc["daily"]["precipitation_probability_max"][0] | 0);

    Kind k = kind_of(j["weather_code"] | 3);
    bool day = (j["is_day"] | 1) != 0;
    n.icon = day ? k.day : k.night;
    strlcpy(n.desc, k.desc, sizeof(n.desc));
    n.valid = true;

    cur        = n;
    have_data  = true;
    last_ok_ms = millis();
    Serial.printf("[weather] %.1fC %s (feels %.1f, %u%%, %.0f km/h)\n",
                  n.temp_c, n.desc, n.feels_c, n.humidity, n.wind_kmh);
    return true;
}

}  // namespace

void begin() {
    cur = {};
    st  = Status::Fetching;
}

void refresh_now() { force = true; }

bool forced() { return force; }

bool due() {
    if (WiFi.status() != WL_CONNECTED) {
        if (!have_data) st = Status::NoWifi;
        return false;
    }
    uint32_t now = millis();
    uint32_t period_ms = (uint32_t)settings::state().weather_min * 60UL * 1000UL;
    // Any state but Ok retries on the short interval — including NoWifi, so a
    // first fetch that never happened is attempted once the network is back.
    return force
        || last_try_ms == 0
        || (have_data && now - last_ok_ms >= period_ms)
        || (st != Status::Ok && now - last_try_ms >= RETRY_MS);
}

void fetch_now() {
    force       = false;
    last_try_ms = millis();
    // A failed refresh keeps the last good reading on screen — the UI decides
    // what to show from data_age_s(), and Error just schedules the retry.
    st = fetch() ? Status::Ok : Status::Error;
}

Current get() { return cur; }
Status  status() { return st; }

uint32_t data_age_s() {
    return have_data ? (millis() - last_ok_ms) / 1000 : UINT32_MAX;
}

const char* compass(int16_t deg) {
    static const char* const NAMES[] = {
        "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
        "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW",
    };
    if (deg < 0) deg += 360;
    // round(deg / 22.5) without floating point
    return NAMES[(((int32_t)deg * 10 + 112) / 225) % 16];
}

}  // namespace weather
