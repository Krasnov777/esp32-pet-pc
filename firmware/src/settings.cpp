#include "settings.h"

#include <Preferences.h>
#include <string.h>

#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif

namespace settings {
namespace {

// Bump a VERSION whenever its struct's layout changes — a mismatch falls back
// to defaults rather than reinterpreting old bytes as the new struct.
constexpr uint32_t MAGIC         = 0x50455450;   // 'PETP'
constexpr uint8_t  VERSION       = 1;
constexpr uint32_t RETRO_MAGIC   = 0x50455452;   // 'PETR'
constexpr uint8_t  RETRO_VERSION = 2;            // 2: HOME screen bit in `screens`
constexpr uint32_t HA_MAGIC      = 0x50455448;   // 'PETH'
constexpr uint8_t  HA_VERSION    = 2;            // 2: per-entity Value/Switch type
constexpr uint8_t  HOME_SCREEN_BIT = 1 << 6;     // ui::Mode::Home
constexpr uint32_t KEYS_MAGIC    = 0x5045544B;   // 'PETK'
constexpr uint8_t  KEYS_VERSION  = 1;

// Same layout the single Config blob has always had, so existing images load.
template <typename T>
struct Stored {
    uint32_t magic;
    uint8_t  version;
    uint8_t  _pad[3];
    T        cfg;
    uint32_t crc;
};

Stored<Config> blob;
Stored<Retro>  retro_blob;
Stored<Ha>     ha_blob;
Stored<Keys>   keys_blob;

// The v1 HA layout — Ha without `type`. Only here to carry a v1 image (and
// the token in it) over to v2 instead of resetting it.
struct HaV1 {
    char     url[72];
    char     token[256];
    char     entity[HA_SLOTS][48];
    char     label[HA_SLOTS][12];
    uint16_t poll_s;
};

// NVS namespace/keys. NVS is the ESP32's wear-levelled key-value store in its
// own flash partition, so it survives OTA updates and needs no sector maths.
constexpr const char* NVS_NS        = "petpc";
constexpr const char* NVS_KEY       = "cfg";
constexpr const char* NVS_RETRO_KEY = "retro";
constexpr const char* NVS_HA_KEY    = "ha";
constexpr const char* NVS_KEYS_KEY  = "keys";

// JSON names for KeyActionType, in enum order.
const char* const KEY_TYPE_NAMES[KA_COUNT] = {
    "none", "http", "screen", "next", "prev", "weather", "ha_toggle", "ha_service", "same",
};
const char* const KEY_SIDE_NAMES[2]   = {"left", "right"};
const char* const KEY_PHASE_NAMES[2]  = {"press", "hold"};
Preferences prefs;

uint32_t crc32(const void* data, size_t len) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    uint32_t crc = 0xFFFFFFFFu;
    while (len--) {
        crc ^= *p++;
        for (uint8_t i = 0; i < 8; i++)
            crc = (crc >> 1) ^ (0xEDB88320u & (-(int32_t)(crc & 1)));
    }
    return ~crc;
}

template <typename T>
uint32_t crc_of(const Stored<T>& s) {
    return crc32(&s, offsetof(Stored<T>, crc));
}

// A missing key or a blob of the wrong size (struct layout changed) reads as
// "no image", the same as a bad magic, version or CRC.
template <typename T>
bool load(const char* key, uint32_t magic, uint8_t version, Stored<T>& s) {
    return prefs.getBytesLength(key) == sizeof(s)
        && prefs.getBytes(key, &s, sizeof(s)) == sizeof(s)
        && s.magic == magic && s.version == version && s.crc == crc_of(s);
}

template <typename T>
void store(const char* key, uint32_t magic, uint8_t version, Stored<T>& s) {
    s.magic   = magic;
    s.version = version;
    s.crc     = crc_of(s);
    // Skip the write when the stored image is already byte-identical, so
    // calling save() liberally costs no flash wear.
    static Stored<T> on_flash;
    if (prefs.getBytes(key, &on_flash, sizeof(s)) == sizeof(s)
        && memcmp(&on_flash, &s, sizeof(s)) == 0) return;
    // A failure here means the settings silently did not stick, so say so.
    if (prefs.putBytes(key, &s, sizeof(s)) != sizeof(s))
        Serial.printf("[settings] NVS write of '%s' FAILED\n", key);
}

void seed_defaults(Config& c) {
    c = Config{};
    strlcpy(c.wifi_ssid, WIFI_SSID,     sizeof(c.wifi_ssid));
    strlcpy(c.wifi_pass, WIFI_PASSWORD, sizeof(c.wifi_pass));
    strlcpy(c.hostname,  "pet-pc", sizeof(c.hostname));

    // Greenwich, until someone sets their own — the web page has a "use this
    // browser's location" button and a time-zone list for exactly that.
    c.lat = 51.4779f;
    c.lon = -0.0015f;
    strlcpy(c.tz,  "GMT0BST,M3.5.0/1,M10.5.0", sizeof(c.tz));
    strlcpy(c.ntp, "pool.ntp.org", sizeof(c.ntp));
    c.clock_24h = true;

    strlcpy(c.weather_host, "api.open-meteo.com", sizeof(c.weather_host));
    c.weather_min = 15;

    // 200 is showroom-bright for a desk box and burns the panel for nothing:
    // OLED ageing goes with drive current, so a third of it buys back several
    // times the life and still reads fine in a lit room. 0 at night = off.
    c.contrast_day   = 70;
    c.contrast_night = 0;
    c.night_from     = 23;
    c.night_to       = 7;
    c.burn_in_shift  = true;
    c.start_mode     = 3;    // ui::Mode::BlockClock
}

void seed_retro(Retro& r) {
    r = Retro{};
    r.screens   = SCREENS_ALL;
    r.saver_min = 20;
    r.fx        = true;
    r.tape      = true;
    r.boot      = true;
}

void clamp(Config& c) {
    if (c.weather_min < 5)   c.weather_min = 5;
    if (c.weather_min > 180) c.weather_min = 180;
    if (c.night_from > 23)   c.night_from = 0;
    if (c.night_to   > 23)   c.night_to   = 0;
    if (c.hostname[0] == '\0') strlcpy(c.hostname, "pet-pc", sizeof(c.hostname));
    // NUL-terminate defensively: a truncated flash write must not hand an
    // unterminated buffer to snprintf/strlcpy later.
    c.wifi_ssid[sizeof(c.wifi_ssid) - 1] = 0;
    c.wifi_pass[sizeof(c.wifi_pass) - 1] = 0;
    c.hostname[sizeof(c.hostname) - 1]   = 0;
    c.tz[sizeof(c.tz) - 1]               = 0;
    c.ntp[sizeof(c.ntp) - 1]             = 0;
    c.weather_host[sizeof(c.weather_host) - 1] = 0;
    for (uint8_t i = 0; i < DESK_SLOTS; i++) {
        c.desk_url[i][sizeof(c.desk_url[i]) - 1]     = 0;
        c.desk_label[i][sizeof(c.desk_label[i]) - 1] = 0;
    }
}

}  // namespace

HaType ha_default_type(const char* id) {
    static const char* const SWITCHABLE[] = {
        "light.", "switch.", "fan.", "input_boolean.", "automation.", "cover.",
    };
    for (const char* p : SWITCHABLE)
        if (strncmp(id, p, strlen(p)) == 0) return HA_SWITCH;
    return HA_VALUE;
}

namespace {

void seed_ha(Ha& h) {
    h = Ha{};
    h.poll_s = 30;
}

// "http://ha.lan:8123/" and "http://ha.lan:8123" are the same server; paths
// are appended with their own leading slash.
void normalize_url(char* url) {
    size_t n = strlen(url);
    while (n > 0 && (url[n - 1] == '/' || url[n - 1] == ' ')) url[--n] = '\0';
}

// Entity ids are lower-case [a-z0-9_.]; a pasted one often has a stray space.
void normalize_entity(char* e) {
    size_t o = 0;
    for (size_t i = 0; e[i]; i++)
        if (e[i] != ' ') e[o++] = tolower((unsigned char)e[i]);
    e[o] = '\0';
}

void clamp(Ha& h) {
    h.url[sizeof(h.url) - 1]     = 0;
    h.token[sizeof(h.token) - 1] = 0;
    normalize_url(h.url);
    for (uint8_t i = 0; i < HA_SLOTS; i++) {
        h.entity[i][sizeof(h.entity[i]) - 1] = 0;
        h.label[i][sizeof(h.label[i]) - 1]   = 0;
        normalize_entity(h.entity[i]);
        if (h.type[i] > HA_SWITCH) h.type[i] = ha_default_type(h.entity[i]);
    }
    if (h.poll_s < 10)  h.poll_s = 10;
    if (h.poll_s > 600) h.poll_s = 600;
}

// First boot of 1.3: the old desk presets become the press actions, so a
// device keeps doing what it did. A fresh device (no presets) gets screen
// navigation on the outer keys instead. Holds default to "same as press".
void seed_keys(Keys& k, const Config& c) {
    k = Keys{};
    for (uint8_t side = 0; side < 2; side++) {
        KeyAction& press = k.act[side * 2];
        if (c.desk_url[side][0]) {
            press.type   = KA_HTTP;
            press.method = METHOD_POST;
            strlcpy(press.url,   c.desk_url[side],   sizeof(press.url));
            strlcpy(press.label, c.desk_label[side], sizeof(press.label));
        } else {
            press.type = side == 0 ? KA_PREV : KA_NEXT;
        }
        k.act[side * 2 + 1].type = KA_SAME;
    }
}

void clamp(Keys& k) {
    for (uint8_t i = 0; i < KEY_ACTIONS; i++) {
        KeyAction& a = k.act[i];
        bool hold = i % 2 == 1;
        if (a.type >= KA_COUNT || (a.type == KA_SAME && !hold)) a.type = KA_NONE;
        if (a.method > METHOD_GET) a.method = METHOD_POST;
        if (a.screen >= SCREENS) a.screen = 0;
        a.label[sizeof(a.label) - 1]     = 0;
        a.url[sizeof(a.url) - 1]         = 0;
        a.entity[sizeof(a.entity) - 1]   = 0;
        a.service[sizeof(a.service) - 1] = 0;
        normalize_entity(a.entity);
        normalize_entity(a.service);
    }
}

void clamp(Retro& r) {
    r.screens &= SCREENS_ALL;
    if (r.screens == 0) r.screens = 1;           // the clock, at least
    if (r.saver_min > 240) r.saver_min = 240;
}

bool copy_str(char* dst, size_t cap, JsonVariantConst v) {
    if (v.isNull() || !v.is<const char*>()) return false;
    const char* s = v.as<const char*>();
    if (strncmp(dst, s, cap) == 0) return false;
    strlcpy(dst, s, cap);
    return true;
}

template <typename T>
bool copy_num(T& dst, JsonVariantConst v) {
    if (v.isNull()) return false;
    T n = v.as<T>();
    if (n == dst) return false;
    dst = n;
    return true;
}

// A required text field: a blank value never comes from someone meaning it,
// only from a form that never loaded — so it leaves the stored one alone.
bool copy_required(char* dst, size_t cap, JsonVariantConst v) {
    if (!v.is<const char*>() || !v.as<const char*>()[0]) return false;
    return copy_str(dst, cap, v);
}

}  // namespace

void begin() {
    prefs.begin(NVS_NS, false);
    if (!load(NVS_KEY, MAGIC, VERSION, blob)) {
        Serial.println(F("[settings] no valid image — seeding defaults"));
        memset(blob._pad, 0, sizeof(blob._pad));
        seed_defaults(blob.cfg);
    }
    if (!load(NVS_RETRO_KEY, RETRO_MAGIC, RETRO_VERSION, retro_blob)) {
        // Version 1 has the same layout; it only predates the HOME screen,
        // whose bit it has clear. Switch that on rather than resetting.
        if (load(NVS_RETRO_KEY, RETRO_MAGIC, 1, retro_blob)) {
            Serial.println(F("[settings] retro image v1 — adding the HOME screen"));
            retro_blob.cfg.screens |= HOME_SCREEN_BIT;
        } else {
            Serial.println(F("[settings] no retro image — seeding defaults"));
            memset(retro_blob._pad, 0, sizeof(retro_blob._pad));
            seed_retro(retro_blob.cfg);
        }
    }
    if (!load(NVS_HA_KEY, HA_MAGIC, HA_VERSION, ha_blob)) {
        memset(ha_blob._pad, 0, sizeof(ha_blob._pad));
        seed_ha(ha_blob.cfg);
        static Stored<HaV1> v1;
        if (load(NVS_HA_KEY, HA_MAGIC, 1, v1)) {
            Serial.println(F("[settings] HA image v1 — typing entities by domain"));
            Ha& h = ha_blob.cfg;
            memcpy(h.url,    v1.cfg.url,    sizeof(h.url));
            memcpy(h.token,  v1.cfg.token,  sizeof(h.token));
            memcpy(h.entity, v1.cfg.entity, sizeof(h.entity));
            memcpy(h.label,  v1.cfg.label,  sizeof(h.label));
            h.poll_s = v1.cfg.poll_s;
            for (uint8_t i = 0; i < HA_SLOTS; i++) h.type[i] = ha_default_type(h.entity[i]);
        } else {
            Serial.println(F("[settings] no HA image — seeding defaults"));
        }
    }
    if (!load(NVS_KEYS_KEY, KEYS_MAGIC, KEYS_VERSION, keys_blob)) {
        Serial.println(F("[settings] no keys image — seeding from the desk presets"));
        memset(keys_blob._pad, 0, sizeof(keys_blob._pad));
        seed_keys(keys_blob.cfg, blob.cfg);
    }
    save();     // clamps, and writes only a blob that was just seeded
}

void save() {
    clamp(blob.cfg);
    clamp(retro_blob.cfg);
    clamp(ha_blob.cfg);
    clamp(keys_blob.cfg);
    store(NVS_KEY, MAGIC, VERSION, blob);
    store(NVS_RETRO_KEY, RETRO_MAGIC, RETRO_VERSION, retro_blob);
    store(NVS_HA_KEY, HA_MAGIC, HA_VERSION, ha_blob);
    store(NVS_KEYS_KEY, KEYS_MAGIC, KEYS_VERSION, keys_blob);
}

void reset_to_defaults() {
    seed_defaults(blob.cfg);
    seed_retro(retro_blob.cfg);
    seed_ha(ha_blob.cfg);
    seed_keys(keys_blob.cfg, blob.cfg);
    save();
}

Config& state() { return blob.cfg; }
Retro&  retro() { return retro_blob.cfg; }
Ha&     ha()    { return ha_blob.cfg; }
Keys&   keys()  { return keys_blob.cfg; }

bool apply_json(JsonVariantConst p) {
    Config& c = blob.cfg;
    bool ch = false;

    JsonVariantConst w = p["wifi"];
    if (!w.isNull()) {
        ch |= copy_str(c.wifi_ssid, sizeof(c.wifi_ssid), w["ssid"]);
        // An empty password field means "leave it alone" — the UI never
        // receives the stored one, so it cannot echo it back.
        if (w["pass"].is<const char*>() && strlen(w["pass"].as<const char*>()) > 0)
            ch |= copy_str(c.wifi_pass, sizeof(c.wifi_pass), w["pass"]);
        ch |= copy_required(c.hostname, sizeof(c.hostname), w["hostname"]);
    }

    JsonVariantConst l = p["loc"];
    if (!l.isNull()) {
        ch |= copy_num(c.lat, l["lat"]);
        ch |= copy_num(c.lon, l["lon"]);
        ch |= copy_required(c.tz,  sizeof(c.tz),  l["tz"]);
        ch |= copy_required(c.ntp, sizeof(c.ntp), l["ntp"]);
    }
    ch |= copy_num(c.clock_24h, p["clock_24h"]);

    JsonVariantConst wx = p["weather"];
    if (!wx.isNull()) {
        ch |= copy_required(c.weather_host, sizeof(c.weather_host), wx["host"]);
        ch |= copy_num(c.weather_min, wx["minutes"]);
    }

    JsonVariantConst d = p["display"];
    if (!d.isNull()) {
        ch |= copy_num(c.contrast_day,   d["contrast_day"]);
        ch |= copy_num(c.contrast_night, d["contrast_night"]);
        ch |= copy_num(c.night_from,     d["night_from"]);
        ch |= copy_num(c.night_to,       d["night_to"]);
        ch |= copy_num(c.burn_in_shift,  d["burn_in_shift"]);
        ch |= copy_num(c.start_mode,     d["start_mode"]);
    }

    Retro& r = retro_blob.cfg;
    JsonVariantConst rv = p["retro"];
    if (!rv.isNull()) {
        ch |= copy_num(r.screens,   rv["screens"]);
        ch |= copy_num(r.saver_min, rv["saver_min"]);
        ch |= copy_num(r.fx,        rv["fx"]);
        ch |= copy_num(r.tape,      rv["tape"]);
        ch |= copy_num(r.boot,      rv["boot"]);
    }

    Ha& h = ha_blob.cfg;
    JsonVariantConst hv = p["ha"];
    if (!hv.isNull()) {
        char prev_url[sizeof(h.url)];
        strlcpy(prev_url, h.url, sizeof(prev_url));
        if (copy_str(h.url, sizeof(h.url), hv["url"])) normalize_url(h.url);
        bool url_changed = strcmp(prev_url, h.url) != 0;
        ch |= url_changed;

        const char* tok = hv["token"].is<const char*>() ? hv["token"].as<const char*>() : "";
        if (tok[0]) {
            ch |= copy_str(h.token, sizeof(h.token), hv["token"]);
        } else if (url_changed && h.token[0]) {
            // The token was entered for the old server. Sending it to a new
            // one needs someone to type it again — otherwise anybody on the
            // LAN could point the box at their own host and read it off.
            h.token[0] = '\0';
            ch = true;
            Serial.println(F("[settings] HA URL changed without a token — token cleared"));
        }

        JsonArrayConst ents = hv["entities"];
        uint8_t i = 0;
        for (JsonObjectConst e : ents) {
            if (i >= HA_SLOTS) break;
            bool id_changed = copy_str(h.entity[i], sizeof(h.entity[i]), e["id"]);
            if (id_changed) normalize_entity(h.entity[i]);   // before its domain is looked at
            ch |= id_changed;
            ch |= copy_str(h.label[i],  sizeof(h.label[i]),  e["label"]);
            const char* t = e["type"].is<const char*>() ? e["type"].as<const char*>() : nullptr;
            // An explicit type wins; a new entity without one gets its
            // domain's default; otherwise the type stays as it was.
            uint8_t type = h.type[i];
            if (t)               type = strcmp(t, "switch") == 0 ? HA_SWITCH : HA_VALUE;
            else if (id_changed) type = ha_default_type(h.entity[i]);
            if (type != h.type[i]) { h.type[i] = type; ch = true; }
            i++;
        }
        ch |= copy_num(h.poll_s, hv["poll_s"]);
    }

    Keys& k = keys_blob.cfg;
    JsonVariantConst kv = p["keys"];
    if (!kv.isNull()) {
        for (uint8_t side = 0; side < 2; side++) {
            for (uint8_t phase = 0; phase < 2; phase++) {
                JsonVariantConst av = kv[KEY_SIDE_NAMES[side]][KEY_PHASE_NAMES[phase]];
                if (av.isNull()) continue;
                KeyAction& a = k.act[side * 2 + phase];
                if (av["type"].is<const char*>()) {
                    const char* t = av["type"].as<const char*>();
                    for (uint8_t i = 0; i < KA_COUNT; i++) {
                        if (strcmp(t, KEY_TYPE_NAMES[i]) == 0 && a.type != i) { a.type = i; ch = true; }
                    }
                }
                if (av["method"].is<const char*>()) {
                    uint8_t m = strcmp(av["method"].as<const char*>(), "GET") == 0 ? METHOD_GET : METHOD_POST;
                    if (m != a.method) { a.method = m; ch = true; }
                }
                ch |= copy_num(a.screen, av["screen"]);
                ch |= copy_str(a.label,   sizeof(a.label),   av["label"]);
                ch |= copy_str(a.url,     sizeof(a.url),     av["url"]);
                ch |= copy_str(a.entity,  sizeof(a.entity),  av["entity"]);
                ch |= copy_str(a.service, sizeof(a.service), av["service"]);
            }
        }
    }

    if (ch) {
        clamp(c);
        clamp(r);
        clamp(h);
        clamp(k);
    }
    return ch;
}

void to_json(JsonObject out, bool include_secrets) {
    const Config& c = blob.cfg;

    JsonObject w = out["wifi"].to<JsonObject>();
    w["ssid"]     = c.wifi_ssid;
    w["hostname"] = c.hostname;
    if (include_secrets) w["pass"] = c.wifi_pass;
    else                 w["pass_set"] = strlen(c.wifi_pass) > 0;

    JsonObject l = out["loc"].to<JsonObject>();
    l["lat"] = c.lat;
    l["lon"] = c.lon;
    l["tz"]  = c.tz;
    l["ntp"] = c.ntp;
    out["clock_24h"] = c.clock_24h;

    JsonObject wx = out["weather"].to<JsonObject>();
    wx["host"]    = c.weather_host;
    wx["minutes"] = c.weather_min;

    JsonObject d = out["display"].to<JsonObject>();
    d["contrast_day"]   = c.contrast_day;
    d["contrast_night"] = c.contrast_night;
    d["night_from"]     = c.night_from;
    d["night_to"]       = c.night_to;
    d["burn_in_shift"]  = c.burn_in_shift;
    d["start_mode"]     = c.start_mode;

    const Retro& r = retro_blob.cfg;
    JsonObject rv = out["retro"].to<JsonObject>();
    rv["screens"]   = r.screens;
    rv["saver_min"] = r.saver_min;
    rv["fx"]        = r.fx;
    rv["tape"]      = r.tape;
    rv["boot"]      = r.boot;

    const Ha& h = ha_blob.cfg;
    JsonObject hv = out["ha"].to<JsonObject>();
    hv["url"] = h.url;
    if (include_secrets) hv["token"] = h.token;
    else                 hv["token_set"] = strlen(h.token) > 0;
    JsonArray ents = hv["entities"].to<JsonArray>();
    for (uint8_t i = 0; i < HA_SLOTS; i++) {
        JsonObject e = ents.add<JsonObject>();
        e["id"]    = h.entity[i];
        e["label"] = h.label[i];
        e["type"]  = h.type[i] == HA_SWITCH ? "switch" : "value";
    }
    hv["poll_s"] = h.poll_s;

    const Keys& k = keys_blob.cfg;
    JsonObject kv = out["keys"].to<JsonObject>();
    for (uint8_t side = 0; side < 2; side++) {
        JsonObject sv = kv[KEY_SIDE_NAMES[side]].to<JsonObject>();
        for (uint8_t phase = 0; phase < 2; phase++) {
            const KeyAction& a = k.act[side * 2 + phase];
            JsonObject av = sv[KEY_PHASE_NAMES[phase]].to<JsonObject>();
            av["type"]    = KEY_TYPE_NAMES[a.type < KA_COUNT ? a.type : (uint8_t)KA_NONE];
            av["label"]   = a.label;
            av["url"]     = a.url;
            av["method"]  = a.method == METHOD_GET ? "GET" : "POST";
            av["screen"]  = a.screen;
            av["entity"]  = a.entity;
            av["service"] = a.service;
        }
    }
}

}  // namespace settings
