#include "ha.h"
#include "settings.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <math.h>

namespace ha {
namespace {

// Home Assistant is on the LAN: a healthy request takes tens of ms. The
// bounds only matter when it is down, and then the back-off keeps them from
// stalling the loop every few seconds.
constexpr uint16_t CONNECT_MS    = 1500;
constexpr uint16_t TIMEOUT_MS    = 3000;
constexpr uint32_t MIN_STEP_MS   = 1000;
constexpr uint32_t FOCUS_STEP_MS = 1000;
constexpr uint32_t BACKOFF_MS    = 60000;
constexpr uint32_t RECHECK_MS    = 1500;     // re-read after a toggle

using settings::HA_SLOTS;

Entity   ents[HA_SLOTS];
Status   st            = Status::Off;
int      last_code     = 0;
uint8_t  next_slot     = 0;
uint32_t last_req_ms   = 0;
uint32_t backoff_until = 0;
bool     focus         = false;
int8_t   recheck_slot  = -1;
uint32_t recheck_at    = 0;

// Kept across requests so HTTP keep-alive can reuse the connection — which
// matters most for https, where every new connection is a TLS handshake.
WiFiClient       plain;
WiFiClientSecure secure;
HTTPClient       http;

const Entity EMPTY = {};

// The 8x8 face is ASCII only: "°C" becomes "C", "m³" becomes "m".
void ascii(char* dst, size_t cap, const char* src) {
    size_t o = 0;
    for (; *src && o + 1 < cap; src++)
        if ((uint8_t)*src >= 0x20 && (uint8_t)*src < 0x80) dst[o++] = *src;
    dst[o] = '\0';
}

void set_value(Entity& e, Kind k, const char* state, const char* unit) {
    e.on = false;
    if (!strcmp(state, "unavailable")) { strlcpy(e.value, "N/A", sizeof(e.value)); return; }
    if (!strcmp(state, "unknown") || !state[0]) { strlcpy(e.value, "?", sizeof(e.value)); return; }

    if (k == Kind::Toggle) e.on = !strcmp(state, "on") || !strcmp(state, "open");

    char* end = nullptr;
    float v = strtof(state, &end);
    if (k == Kind::Value && end != state && *end == '\0') {
        char u[8];
        ascii(u, sizeof(u), unit);
        int decimals = (fabsf(v) < 100 && v != (float)(int)v) ? 1 : 0;
        // "21.4C", "45W", but "612 ppm" / "1013 hPa" — a word reads better
        // spaced, when there is room for it.
        char num[12];
        snprintf(num, sizeof(num), "%.*f", decimals, v);
        bool spaced = strlen(u) > 1 && isalpha((unsigned char)u[0])
                   && strlen(num) + 1 + strlen(u) < sizeof(e.value);
        snprintf(e.value, sizeof(e.value), spaced ? "%s %s" : "%s%s", num, u);
        return;
    }
    ascii(e.value, sizeof(e.value), state);
}

// A failure that says something about HA as a whole pauses polling; one bad
// entity (404, say) only marks that entity.
void note_failure(int code) {
    if (code == 401 || code == 403) st = Status::AuthError;
    else if (code < 0)              st = Status::Unreachable;
    else return;
    backoff_until = millis() + BACKOFF_MS;
    Serial.printf("[ha] %s (HTTP %d) — pausing %us\n",
                  st == Status::AuthError ? "token rejected" : "unreachable",
                  code, (unsigned)(BACKOFF_MS / 1000));
}

// One request against the configured server. Returns the HTTP status, or a
// negative HTTPClient error for a transport failure.
int request(const char* path, const char* post_body, String* body) {
    const auto& c = settings::ha();
    char url[160];
    snprintf(url, sizeof(url), "%s%s", c.url, path);
    bool tls = strncmp(url, "https://", 8) == 0;

    http.setReuse(true);
    http.setConnectTimeout(CONNECT_MS);
    http.setTimeout(TIMEOUT_MS);
    if (!(tls ? http.begin(secure, url) : http.begin(plain, url))) return HTTPC_ERROR_CONNECTION_REFUSED;

    char auth[sizeof(c.token) + 8];
    snprintf(auth, sizeof(auth), "Bearer %s", c.token);
    http.addHeader("Authorization", auth);

    int code;
    if (post_body) {
        http.addHeader("Content-Type", "application/json");
        code = http.POST(post_body);
    } else {
        code = http.GET();
    }
    // Read the body even when it is not wanted: an unread response would
    // stop the connection being reused.
    if (code > 0) {
        String b = http.getString();
        if (body) *body = b;
    }
    http.end();
    last_code = code;
    return code;
}

bool read(uint8_t slot) {
    const auto& c = settings::ha();
    char path[64];
    snprintf(path, sizeof(path), "/api/states/%s", c.entity[slot]);

    String body;
    int code = request(path, nullptr, &body);
    Entity& e = ents[slot];
    e.seen = true;
    if (code != 200) {
        e.ok = false;
        strlcpy(e.value, code == 404 ? "NO ENT" : "ERR", sizeof(e.value));
        Serial.printf("[ha] %s -> HTTP %d\n", c.entity[slot], code);
        note_failure(code);
        return false;
    }

    // A light's attributes run to a kilobyte; keep only what is shown.
    JsonDocument filter;
    filter["state"] = true;
    filter["attributes"]["unit_of_measurement"] = true;
    filter["attributes"]["friendly_name"]       = true;
    JsonDocument doc;
    if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) {
        e.ok = false;
        strlcpy(e.value, "ERR", sizeof(e.value));
        return false;
    }

    if (c.label[slot][0]) strlcpy(e.name, c.label[slot], sizeof(e.name));
    else                  ascii(e.name, sizeof(e.name), doc["attributes"]["friendly_name"] | c.entity[slot]);
    set_value(e, kind(slot), doc["state"] | "", doc["attributes"]["unit_of_measurement"] | "");
    e.ok = true;
    st   = Status::Ok;
    return true;
}

uint8_t used_count() {
    uint8_t n = 0;
    for (uint8_t i = 0; i < HA_SLOTS; i++)
        if (settings::ha().entity[i][0]) n++;
    return n;
}

}  // namespace

void begin() { reconfigure(); }

void reconfigure() {
    http.end();
    plain.stop();
    secure.stop();
    secure.setInsecure();    // a LAN box with a self-signed or private-CA cert is the norm

    const auto& c = settings::ha();
    for (uint8_t i = 0; i < HA_SLOTS; i++) {
        ents[i] = Entity{};
        // Until the first read, show the label or the entity id.
        const char* id  = c.entity[i];
        const char* dot = strchr(id, '.');
        strlcpy(ents[i].name, c.label[i][0] ? c.label[i] : (dot ? dot + 1 : id), sizeof(ents[i].name));
        strlcpy(ents[i].value, "--", sizeof(ents[i].value));
    }
    next_slot     = 0;
    last_req_ms   = 0;
    backoff_until = 0;
    recheck_slot  = -1;
    st = configured() ? Status::Connecting : Status::Off;
}

bool ready() {
    const auto& c = settings::ha();
    return c.url[0] && c.token[0];
}

bool configured() {
    const auto& c = settings::ha();
    return c.url[0] && c.token[0] && used_count() > 0;
}

void set_focus(bool on) {
    if (on && !focus) {
        backoff_until = 0;       // someone is looking: try now rather than in a minute
        last_req_ms   = 0;
    }
    focus = on;
}

void tick() {
    if (!configured()) { st = Status::Off; return; }
    if (WiFi.status() != WL_CONNECTED) { st = Status::NoWifi; return; }
    if (st == Status::NoWifi) st = Status::Connecting;

    uint32_t now = millis();
    if (backoff_until && (int32_t)(now - backoff_until) < 0) return;
    backoff_until = 0;

    if (recheck_slot >= 0 && (int32_t)(now - recheck_at) >= 0) {
        uint8_t s = recheck_slot;
        recheck_slot = -1;
        read(s);
        last_req_ms = millis();
        return;
    }

    uint32_t step = focus ? FOCUS_STEP_MS
                          : (uint32_t)settings::ha().poll_s * 1000UL / used_count();
    if (step < MIN_STEP_MS) step = MIN_STEP_MS;
    if (last_req_ms && now - last_req_ms < step) return;

    for (uint8_t k = 0; k < HA_SLOTS; k++) {
        uint8_t s = next_slot;
        next_slot = (next_slot + 1) % HA_SLOTS;
        if (settings::ha().entity[s][0]) {
            read(s);
            break;
        }
    }
    last_req_ms = millis();
}

Kind kind(uint8_t slot) {
    if (slot >= HA_SLOTS || !settings::ha().entity[slot][0]) return Kind::Unused;
    return settings::ha().type[slot] == settings::HA_SWITCH ? Kind::Toggle : Kind::Value;
}

const Entity& entity(uint8_t slot) { return slot < HA_SLOTS ? ents[slot] : EMPTY; }

Status status() { return st; }
int    last_http_code() { return last_code; }

Result call(const char* service, const char* entity_id) {
    if (!ready()) return Result::NotConfigured;
    if (WiFi.status() != WL_CONNECTED) return Result::NoWifi;

    // "light.turn_on" → /api/services/light/turn_on
    const char* dot = strchr(service, '.');
    if (!dot || dot == service || !dot[1]) return Result::NotConfigured;
    char path[96];
    snprintf(path, sizeof(path), "/api/services/%.*s/%s", (int)(dot - service), service, dot + 1);

    char payload[80];
    if (entity_id && entity_id[0]) snprintf(payload, sizeof(payload), "{\"entity_id\":\"%s\"}", entity_id);
    else                           strlcpy(payload, "{}", sizeof(payload));

    int code = request(path, payload, nullptr);
    Serial.printf("[ha] %s %s -> HTTP %d\n", service, entity_id ? entity_id : "", code);
    if (code != 200) {
        note_failure(code);
        return Result::Failed;
    }
    st            = Status::Ok;
    backoff_until = 0;

    // Keep the HOME screen honest about an entity it shows.
    for (uint8_t s = 0; entity_id && s < HA_SLOTS; s++) {
        if (settings::ha().entity[s][0] && strcmp(settings::ha().entity[s], entity_id) == 0) {
            recheck_slot = s;
            recheck_at   = millis() + RECHECK_MS;
        }
    }
    return Result::Ok;
}

Result toggle(uint8_t slot) {
    if (kind(slot) != Kind::Toggle || !configured()) return Result::NotConfigured;
    Result r = call("homeassistant.toggle", settings::ha().entity[slot]);
    if (r != Result::Ok) return r;

    Entity& e = ents[slot];
    e.on = !e.on;
    strlcpy(e.value, e.on ? "ON" : "OFF", sizeof(e.value));
    return Result::Ok;
}

int test() {
    const auto& c = settings::ha();
    if (!c.url[0] || !c.token[0]) return 0;
    if (WiFi.status() != WL_CONNECTED) return HTTPC_ERROR_NOT_CONNECTED;
    int code = request("/api/", nullptr, nullptr);
    if (code == 200) st = Status::Ok;
    else             note_failure(code);
    return code;
}

const char* status_text() {
    switch (st) {
        case Status::Off:         return "off";
        case Status::NoWifi:      return "no wifi";
        case Status::Connecting:  return "connecting";
        case Status::Ok:          return "ok";
        case Status::AuthError:   return "token rejected";
        case Status::Unreachable: return "unreachable";
    }
    return "?";
}

}  // namespace ha
