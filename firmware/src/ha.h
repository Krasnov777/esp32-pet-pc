// Home Assistant over its REST API, with a long-lived access token.
//
// Reads: GET /api/states/<entity_id>, one entity per step, round-robin — so
// each blocking request is a single small GET, never the whole house. The
// round takes settings.ha.poll_s, or about a second per entity while the HOME
// screen is up. Writes: POST /api/services/homeassistant/toggle, which HA
// maps to the entity's own domain (light.toggle, switch.toggle…).
//
// Same shape as `weather`: a tick() that decides when to fetch, snapshot
// getters, and a Status so the screen can say why it is empty.
#pragma once

#include <Arduino.h>

namespace ha {

enum class Kind : uint8_t {
    Unused,    // empty slot
    Toggle,    // configured as a Switch: shown ON/OFF, toggled from the keys
    Value,     // configured as a Value: shown as HA reports it
};

struct Entity {
    char name[12];     // configured label, else HA's friendly_name
    char value[10];    // display text: "21.4C", "ON", "612ppm", "N/A"
    bool on;           // Toggle kinds: the state is "on" / "open"
    bool ok;           // the last read succeeded
    bool seen;         // read at least once
};

enum class Status : uint8_t {
    Off,           // no URL, token or entities configured
    NoWifi,
    Connecting,    // configured, nothing read yet
    Ok,
    AuthError,     // 401/403 — the token is wrong or revoked
    Unreachable,   // no connection / timeout; retried after a back-off
};

enum class Result : uint8_t { Ok, NoWifi, NotConfigured, Failed };

void begin();
void reconfigure();          // settings changed: drop readings and the open connection
void tick();

// While the HOME screen is showing, read faster (and retry at once after a
// back-off), so what is on the glass is seconds old rather than a round old.
void set_focus(bool on);

bool          configured();   // URL, token and at least one HOME entity
bool          ready();        // URL and token — enough for key actions
Kind          kind(uint8_t slot);
const Entity& entity(uint8_t slot);
Status        status();
int           last_http_code();

// Blocking POST (bounded by the timeouts). Flips the shown state at once and
// re-reads the entity shortly after, since a light reports its new state
// only once the bulb has answered.
Result toggle(uint8_t slot);

// Any service, "domain.service", with an optional entity_id — what the outer
// keys use (homeassistant.toggle, scene.turn_on, input_select.select_next…).
// If the entity is on the HOME screen it is re-read shortly after.
Result call(const char* service, const char* entity_id);

// GET /api/ — 200 means URL and token are both good. Returns the HTTP code,
// or a negative transport error.
int test();

const char* status_text();   // for the web UI: "ok", "auth", "unreachable"…

}  // namespace ha
