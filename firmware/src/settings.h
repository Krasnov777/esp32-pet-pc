// Persistent configuration, stored as blobs in the ESP32's NVS partition:
// Config under `cfg`, the retro-look options under `retro`, the Home
// Assistant connection under `ha`, and the outer keys' actions under `keys`.
//
// Why a blob and not a key per field: a versioned, CRC-checked struct is
// written atomically and validated in one place, and a layout change is
// handled by bumping its VERSION rather than by migrating individual keys.
// Bumping a VERSION resets that blob to defaults, so new options go in a new
// blob rather than growing Config (whose reset would lose the WiFi login).
//
// Everything here is editable from the web UI at http://<device>/ — including
// what the outer keys do, so a renamed webhook or HA entity is a config
// change, not a reflash.
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

namespace settings {

// Desk presets: one per outer button. Slot 0 = left key, slot 1 = right key.
constexpr uint8_t DESK_SLOTS = 2;

// Number of ui::Mode screens (ui.cpp static_asserts that they agree), and the
// mask with every one of them in the centre-key rotation.
constexpr uint8_t SCREENS     = 7;
constexpr uint8_t SCREENS_ALL = (1 << SCREENS) - 1;

struct Config {
    // ── network ──────────────────────────────────────────────────────────────
    char wifi_ssid[33];
    char wifi_pass[65];
    char hostname[24];

    // ── location / time ──────────────────────────────────────────────────────
    float lat;
    float lon;
    char  tz[48];            // POSIX TZ string, e.g. "CET-1CEST,M3.5.0,M10.5.0/3"
    char  ntp[40];
    bool  clock_24h;

    // ── weather ──────────────────────────────────────────────────────────────
    // Host only; the path is built in weather.cpp. Configurable so a local
    // proxy can be substituted without touching code (see README).
    char     weather_host[48];
    uint16_t weather_min;    // refresh interval, minutes

    // ── legacy: the 1.x desk presets ─────────────────────────────────────────
    // Only read once, to seed the `keys` blob on the first boot of 1.3 (the
    // left/right press actions). Kept so Config's layout — and with it the
    // WiFi login — survives the update; no longer exposed over the API.
    char desk_url[DESK_SLOTS][112];
    char desk_label[DESK_SLOTS][16];

    // ── display ──────────────────────────────────────────────────────────────
    uint8_t contrast_day;    // 0..255 (SSD1306 contrast register)
    uint8_t contrast_night;  // 0 = panel off for the whole window, not "dim"
    uint8_t night_from;      // hour [0..23] when the dim period starts
    uint8_t night_to;        // hour [0..23] when it ends; from == to disables
    bool    burn_in_shift;   // nudge the whole frame a few px every few minutes
    uint8_t start_mode;      // ui::Mode shown after the boot screen (a setting, not the last one used)
};

// The retro-look options. Kept in a blob of their own (NVS key `retro`) so
// adding them did not change Config's layout — which would have thrown away
// every existing device's WiFi credentials and settings on the update.
struct Retro {
    uint8_t screens;         // bit n set = ui::Mode n is in the centre-key rotation
    uint8_t saver_min;       // idle minutes before the 10 PRINT screensaver, 0 = off
    bool    fx;              // CRT transitions, and the collapse at night blank
    bool    tape;            // PRESS PLAY ON TAPE sequence around weather fetches
    bool    boot;            // BASIC banner at power-on instead of the plain boot card
};

// Home Assistant: where it is, how to get in, and which entities the HOME
// screen shows. Each entity is either a Value (shown, read-only) or a Switch
// (shown ON/OFF and toggled from the centre key).
constexpr uint8_t HA_SLOTS = 6;

enum HaType : uint8_t { HA_VALUE = 0, HA_SWITCH = 1 };

// The type an entity gets when nobody has picked one: Switch for the domains
// that have a toggle service, Value for everything else.
HaType ha_default_type(const char* entity_id);

struct Ha {
    char     url[72];                    // base URL, e.g. http://homeassistant.local:8123
    char     token[256];                 // long-lived access token — write-only over the API
    char     entity[HA_SLOTS][48];       // entity_id, "" = unused slot
    char     label[HA_SLOTS][12];        // shown name; "" = the entity's friendly_name
    uint16_t poll_s;                     // seconds for one full round of reads
    uint8_t  type[HA_SLOTS];             // HaType per slot
};

// The outer keys. Each has a press and a hold action; the centre key is
// fixed (screens, BASIC, HOME) and not configurable.
enum KeyActionType : uint8_t {
    KA_NONE = 0,
    KA_HTTP,          // request `url` with `method`
    KA_SCREEN,        // show screen `screen` (a ui::Mode)
    KA_NEXT,          // next screen in the rotation
    KA_PREV,          // previous screen in the rotation
    KA_WEATHER,       // refresh the weather now
    KA_HA_TOGGLE,     // homeassistant.toggle on `entity`
    KA_HA_SERVICE,    // call HA `service` ("domain.service"), `entity` optional
    KA_SAME,          // hold only: do the press action
    KA_COUNT,
};

// Not HTTP_GET/HTTP_POST: the web server library already owns those names.
enum KeyMethod : uint8_t { METHOD_POST = 0, METHOD_GET = 1 };

struct KeyAction {
    uint8_t type;            // KeyActionType
    uint8_t method;          // KA_HTTP: KeyMethod
    uint8_t screen;          // KA_SCREEN: ui::Mode value
    char    label[16];       // shown on the confirmation toast; "" = a sensible default
    char    url[112];        // KA_HTTP; spaces allowed, encoded on send
    char    entity[48];      // KA_HA_TOGGLE / KA_HA_SERVICE
    char    service[40];     // KA_HA_SERVICE, e.g. "input_select.select_next"
};

// Index into Keys::act.
enum KeySlot : uint8_t { LEFT_PRESS = 0, LEFT_HOLD, RIGHT_PRESS, RIGHT_HOLD, KEY_ACTIONS };

struct Keys {
    KeyAction act[KEY_ACTIONS];
};

// Lifecycle
void begin();                 // load from NVS, or seed defaults on first boot
void save();                  // CRC + write (only if bytes changed)
void reset_to_defaults();

// Access — the returned references are mutable; call save() to persist.
Config& state();
Retro&  retro();
Ha&     ha();
Keys&   keys();

// Apply a JSON patch. Returns true if anything actually changed.
// Keys: wifi.{ssid,pass,hostname}, loc.{lat,lon,tz,ntp}, clock_24h,
//       weather.{host,minutes},
//       display.{contrast_day,contrast_night,night_from,night_to,
//                burn_in_shift,start_mode},
//       retro.{screens,saver_min,fx,tape,boot},
//       ha.{url,token,entities[i].{id,label,type},poll_s}   (type: "value"|"switch"),
//       keys.{left,right}.{press,hold}.{type,label,url,method,screen,entity,service}
//         type:   none|http|screen|next|prev|weather|ha_toggle|ha_service|same
//         method: POST|GET
// Blank wifi.hostname, loc.tz, loc.ntp and weather.host are ignored: they are
// required, and a blank one only ever comes from a form that failed to load.
// An empty ha.token means "leave it alone". A new ha.url without a new token
// clears the stored token, so the token is never sent to a host it was not
// entered for.
bool apply_json(JsonVariantConst patch);

// Serialize into `out`. The WiFi password and the HA token are replaced by
// `pass_set` / `token_set` flags unless include_secrets is set.
void to_json(JsonObject out, bool include_secrets = false);

}  // namespace settings
