#include "buttons.h"
#include "board_config.h"

namespace buttons {
namespace {

constexpr uint32_t DEBOUNCE_MS = 25;
constexpr uint32_t LONG_MS     = 800;

constexpr uint8_t PINS[(int)Id::COUNT] = {
    board::BTN_LEFT, board::BTN_CENTER, board::BTN_RIGHT,
};

struct State {
    bool     stable_down  = false;
    bool     last_raw     = false;
    uint32_t last_change  = 0;
    uint32_t pressed_at   = 0;
    bool     long_fired   = false;
    Event    pending      = Event::None;
};

State st[(int)Id::COUNT];

uint32_t activity_ms = 0;

}  // namespace

void begin() {
    for (uint8_t i = 0; i < (uint8_t)Id::COUNT; i++) {
        pinMode(PINS[i], INPUT_PULLUP);
        st[i] = State{};
    }
}

void tick() {
    uint32_t now = millis();
    for (uint8_t i = 0; i < (uint8_t)Id::COUNT; i++) {
        State& s = st[i];
        bool raw = digitalRead(PINS[i]) == LOW;     // pull-up → LOW means pressed

        if (raw != s.last_raw) {
            s.last_raw    = raw;
            s.last_change = now;
            continue;                                // wait out the bounce
        }
        if (now - s.last_change < DEBOUNCE_MS) continue;

        if (raw && !s.stable_down) {                 // press edge
            s.stable_down = true;
            s.pressed_at  = now;
            s.long_fired  = false;
            activity_ms   = now;
        } else if (!raw && s.stable_down) {          // release edge
            s.stable_down = false;
            if (!s.long_fired) s.pending = Event::Short;
        } else if (raw && !s.long_fired && now - s.pressed_at >= LONG_MS) {
            s.long_fired = true;                     // fire while still held
            s.pending    = Event::Long;
        }
    }
}

Event take(Id id) {
    State& s = st[(uint8_t)id];
    Event e = s.pending;
    s.pending = Event::None;
    return e;
}

bool held(Id id) { return st[(uint8_t)id].stable_down; }

uint32_t last_activity_ms() { return activity_ms; }

}  // namespace buttons
