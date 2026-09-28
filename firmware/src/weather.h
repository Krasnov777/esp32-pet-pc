// Current conditions + today's range from Open-Meteo.
//
// Fetched over plain HTTP: the payload is a public weather reading with no
// credentials attached, so TLS buys nothing here. (The C6 has the heap and
// flash for it now — switching to WiFiClientSecure is an option if the API
// ever goes HTTPS-only.) The host is a setting, so it can also be pointed at
// a LAN proxy without a code change.
//
// No API key, no account. One request every settings.weather_min minutes.
#pragma once

#include <Arduino.h>

namespace weather {

// Icon classes the renderer knows how to draw (see icons.cpp).
enum class Icon : uint8_t {
    Sun = 0, Moon, PartlySun, PartlyMoon, Cloud, Rain, Snow, Thunder, Fog,
};

struct Current {
    float   temp_c;
    float   feels_c;
    uint8_t humidity;        // %
    float   wind_kmh;
    int16_t wind_deg;        // meteorological, 0 = from north
    float   day_max_c;
    float   day_min_c;
    uint8_t rain_prob;       // today's max precipitation probability, %
    Icon    icon;
    char    desc[24];
    bool    valid;
};

enum class Status : uint8_t {
    NoWifi,
    Fetching,   // no successful fetch yet
    Ok,
    Error,      // last attempt failed; a retry is scheduled
};

void begin();

// The main loop asks due() and then either fetches straight away or hands the
// fetch to the UI, which plays the tape-loading sequence around fetch_now().
bool due();                  // a fetch should happen now (never true without WiFi)
void fetch_now();            // blocking GET + parse, bounded by the HTTP timeout
void refresh_now();          // make due() true (long-press centre key, LOAD "WEATHER")
bool forced();               // a refresh_now() is still pending

Current  get();
Status   status();
uint32_t data_age_s();       // since the last successful fetch (UINT32_MAX if never)

// "NNE" etc. for a meteorological bearing.
const char* compass(int16_t deg);

}  // namespace weather
