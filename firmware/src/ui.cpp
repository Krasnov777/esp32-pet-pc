#include "ui.h"
#include "board_config.h"
#include "buttons.h"
#include "display.h"
#include "fx.h"
#include "ha.h"
#include "icons.h"
#include "pet.h"
#include "settings.h"
#include "term.h"
#include "timekeeper.h"
#include "weather.h"

#include <WiFi.h>
#include <esp_random.h>

namespace ui {

static_assert((uint8_t)Mode::COUNT == settings::SCREENS,
              "settings::SCREENS must match ui::Mode — the retro screen mask depends on it");

namespace {

constexpr uint32_t FRAME_MS      = 200;      // 5 fps is plenty for a clock
constexpr uint32_t ANIM_MS       = 40;       // 25 fps while something moves; a frame costs ~25 ms of I²C
constexpr uint32_t SHIFT_MS      = 120000;   // burn-in nudge every 2 minutes
constexpr uint32_t BLINK_MS      = 300;      // PET cursor half-period
constexpr uint32_t SAVER_RUN_MS  = 60000;    // the maze runs a minute, then the screen comes back
constexpr uint32_t ATTRACT_MS    = 30000;    // an idle BASIC screen runs a command by itself
constexpr uint32_t HOME_SEL_MS   = 30000;    // the HOME highlight goes away after this long idle

// Night blanking: a night contrast of 0 means "panel off", not "very dim".
// A press wakes it for WAKE_MS at a level that reads in a dark room without
// lighting up the room.
constexpr uint32_t WAKE_MS       = 10000;
constexpr uint8_t  WAKE_CONTRAST = 24;

// The order the centre key walks the screens in. Mode values are stored, so
// the rotation order lives here rather than in the enum.
constexpr Mode ORDER[] = {
    Mode::Clock, Mode::BlockClock, Mode::Weather, Mode::Home, Mode::Listing, Mode::Basic, Mode::Info,
};
static_assert(sizeof(ORDER) / sizeof(ORDER[0]) == (size_t)Mode::COUNT, "every Mode in ORDER once");

Mode     cur_mode   = Mode::Clock;
bool     in_boot    = true;
char     boot1[24]  = "";
char     boot2[26]  = "";
uint32_t boot_t0    = 0;
uint32_t boot_heap  = 0;

char     toast_title[18] = "";
char     toast_sub[22]   = "";
bool     toast_on        = false;
uint32_t toast_until     = 0;

uint32_t last_frame_ms = 0;
uint32_t last_wake     = 0;
int8_t   ox = 0, oy = 0;

// One terminal for everything that types: the tape sequence, the BASIC
// screen and the screensaver. Only one of them owns it at a time.
term::Screen con;
term::Script script(con);
term::Screen listing_scr;         // redrawn from scratch every frame

bool     tape_on      = false;    // a tape sequence is queued or playing
bool     tape_overlay = false;    // ...full screen, rather than inline on the BASIC screen
bool     saver_on     = false;
uint32_t saver_since  = 0;
bool     swallow_center = false;  // the press that ended the screensaver
bool     collapsing   = false;    // the power-off effect is playing before night blank
uint32_t basic_last   = 0;        // last command on the BASIC screen (attract timer)
uint8_t  basic_next   = 0;
int8_t   home_sel     = -1;       // highlighted HA slot on the HOME screen, -1 = none
int8_t   home_busy    = -1;       // slot whose toggle request is in flight

// ── helpers ─────────────────────────────────────────────────────────────────

void centred(const char* s, int y) {
    U8G2& g = display::u8g2();
    int w = g.getUTF8Width(s);
    g.drawUTF8(ox + (board::OLED_W - w) / 2, oy + y, s);
}

void right(const char* s, int x_right, int y) {
    U8G2& g = display::u8g2();
    g.drawUTF8(ox + x_right - g.getUTF8Width(s), oy + y, s);
}

void at(int x, int y, const char* s) {
    display::u8g2().drawUTF8(ox + x, oy + y, s);
}

void hline(int y) {
    display::u8g2().drawHLine(ox, oy + y, board::OLED_W - ox);
}

// A 16-column character screen fills the full 128 px, so it takes at most
// 1 px of the sideways shift; the glyphs' blank right column absorbs that.
int term_x() { return ox > 1 ? 1 : ox; }

// Both dim thresholds are hours; a window that wraps midnight (23 → 7) is the
// normal case, so the comparison has to handle from > to. Without NTP there is
// no defensible answer, so the day level wins — being too bright at 3 a.m. is
// better than a screen that stays dark because time never synced.
bool in_night_window() {
    const auto& c = settings::state();
    if (c.night_from == c.night_to) return false;
    struct tm t;
    if (!timekeeper::local(t)) return false;
    return (c.night_from < c.night_to)
               ? (t.tm_hour >= c.night_from && t.tm_hour < c.night_to)
               : (t.tm_hour >= c.night_from || t.tm_hour < c.night_to);
}

uint8_t scheduled_contrast() {
    const auto& c = settings::state();
    return in_night_window() ? c.contrast_night : c.contrast_day;
}

// Screens are authored against a 126x62 safe area: ox adds up to 2 px to the
// right and oy up to 1 px down, so 62 is the lowest baseline a screen may use.
void update_shift() {
    if (!settings::state().burn_in_shift) { ox = oy = 0; return; }
    uint32_t n = millis() / SHIFT_MS;
    ox = n % 3;                 // 0..2 px right
    oy = (n / 3) % 2;           // 0..1 px down
}

void uptime_text(char* buf, size_t cap) {
    uint32_t s = millis() / 1000;
    uint32_t d = s / 86400;
    s %= 86400;
    if (d > 0) snprintf(buf, cap, "%ud %02u:%02u", (unsigned)d, (unsigned)(s / 3600), (unsigned)((s % 3600) / 60));
    else       snprintf(buf, cap, "%02u:%02u:%02u", (unsigned)(s / 3600), (unsigned)((s % 3600) / 60), (unsigned)(s % 60));
}

// "3m" / "2h" / "--" for a data age in seconds.
void age_text(char* buf, size_t cap, uint32_t age_s) {
    if (age_s == UINT32_MAX)  strlcpy(buf, "--", cap);
    else if (age_s < 90)      snprintf(buf, cap, "%us", (unsigned)age_s);
    else if (age_s < 5400)    snprintf(buf, cap, "%um", (unsigned)(age_s / 60));
    else                      snprintf(buf, cap, "%uh", (unsigned)(age_s / 3600));
}

// TI$ — the PET's clock variable, HHMMSS. Before the first NTP sync it counts
// up from power-on, which is exactly what a PET's did until someone set it.
void ti_string(char* buf, size_t cap) {
    struct tm t;
    if (timekeeper::local(t)) {
        snprintf(buf, cap, "%02d%02d%02d", t.tm_hour, t.tm_min, t.tm_sec);
    } else {
        uint32_t s = millis() / 1000;
        snprintf(buf, cap, "%02u%02u%02u",
                 (unsigned)(s / 3600 % 24), (unsigned)(s / 60 % 60), (unsigned)(s % 60));
    }
}

// ── BASIC-flavoured output, shared by LIST, the BASIC screen and the tape ───

using Emit = void (*)(const char*);

void out_line(const char* s) { script.out(s); }
void ready() { script.out("READY."); }

// The live data as a program listing. Five lines, so with READY. and the
// cursor it fits the 8-row screen even when W$ wraps.
void listing(Emit emit) {
    char b[40];
    weather::Current w = weather::get();
    if (w.valid) {
        snprintf(b, sizeof(b), "10 T=%.1f:F=%.0f", w.temp_c, w.feels_c);                    emit(b);
        snprintf(b, sizeof(b), "20 W$=\"%s\"", w.desc);                                     emit(b);
        snprintf(b, sizeof(b), "30 H=%u:R=%u", w.humidity, w.rain_prob);                    emit(b);
        snprintf(b, sizeof(b), "40 V=%.0f:D$=\"%s\"", w.wind_kmh, weather::compass(w.wind_deg)); emit(b);
    } else {
        emit("10 REM NO WEATHER YET");
    }
    char ti[8];
    ti_string(ti, sizeof(ti));
    snprintf(b, sizeof(b), "50 TI$=\"%s\"", ti);
    emit(b);
}

// What `RUN` prints after the tape has loaded WEATHER.
void report(Emit emit) {
    char b[40];
    weather::Current w = weather::get();
    snprintf(b, sizeof(b), "%.1fC FEELS %.0fC", w.temp_c, w.feels_c);               emit(b);
    emit(w.desc);
    snprintf(b, sizeof(b), "HUM %u%% RAIN %u%%", w.humidity, w.rain_prob);         emit(b);
    snprintf(b, sizeof(b), "WIND %.0f %s", w.wind_kmh, weather::compass(w.wind_deg)); emit(b);
}

// ── tape ────────────────────────────────────────────────────────────────────

void tape_end() {
    tape_on    = false;
    basic_last = millis();
    if (tape_overlay) {
        tape_overlay = false;
        if (settings::retro().fx) fx::start(fx::Kind::Roll);
    }
}

// Runs at the LOADING step. The frame showing LOADING is already on the
// glass (term::Script::call guarantees it), so the blocking GET is visible.
void tape_fetch() {
    weather::fetch_now();
    if (weather::status() == weather::Status::Ok) {
        ready();
        script.type("RUN");
        report(out_line);
    } else {
        script.out("?LOAD  ERROR");
    }
    ready();
    if (tape_overlay) script.wait(2200);    // time to read it before the screen returns
    script.call(tape_end);
}

// ── BASIC screen ────────────────────────────────────────────────────────────

void cmd_list() { listing(out_line); ready(); }

void cmd_ti() {
    char b[8];
    ti_string(b, sizeof(b));
    script.out(b);
    ready();
}

void cmd_dt() {
    char b[24];
    timekeeper::format_date(b, sizeof(b));
    script.out(b);
    ready();
}

void cmd_fre() {
    char b[16];
    snprintf(b, sizeof(b), " %u", (unsigned)ESP.getFreeHeap());   // BASIC prints a sign column
    script.out(b);
    ready();
}

void cmd_ip() {
    bool up = WiFi.status() == WL_CONNECTED;
    script.out((up ? WiFi.localIP() : WiFi.softAPIP()).toString().c_str());
    ready();
}

struct Cmd {
    const char*      text;
    term::Script::Fn run;     // nullptr = LOAD "WEATHER", which is the tape sequence
};

const Cmd CMDS[] = {
    {"LIST",             cmd_list},
    {"?TI$",             cmd_ti},
    {"LOAD \"WEATHER\"", nullptr},
    {"?FRE(0)",          cmd_fre},
    {"?DT$",             cmd_dt},
    {"?IP$",             cmd_ip},
};
constexpr uint8_t N_CMDS = sizeof(CMDS) / sizeof(CMDS[0]);

void basic_enter() {
    basic_last = millis();
    if (tape_on) return;      // the tape keeps playing on this same terminal
    script.clear();
    con.clear();
    con.println("**** PET-PC ****");
    con.println("C:RUN  HOLD:EXIT");
    con.println("READY.");
}

// Run the next command. The attract loop skips LOAD so an idle screen never
// goes to the network on its own.
void basic_run(bool attract) {
    if (script.busy() || tape_on) return;
    basic_last = millis();
    for (uint8_t tries = 0; tries < N_CMDS; tries++) {
        const Cmd& c = CMDS[basic_next];
        basic_next = (basic_next + 1) % N_CMDS;
        if (c.run) {
            script.type(c.text);
            script.call(c.run);
            return;
        }
        if (attract) continue;
        if (WiFi.status() != WL_CONNECTED) {
            script.type(c.text);
            script.out("?DEVICE NOT PRESENT  ERROR");
            ready();
        } else {
            weather::refresh_now();   // the main loop starts the tape, typed LOAD and all
        }
        return;
    }
}

// ── screensaver ─────────────────────────────────────────────────────────────

void saver_start() {
    saver_on    = true;
    saver_since = millis();
    script.clear();
    con.clear();
    if (settings::retro().fx) fx::start(fx::Kind::Roll);
    script.type("10 PRINT CHR$(205.5+RND(1));", 20);
    script.type("20 GOTO 10", 20);
    script.type("RUN", 20);
}

void saver_stop() {
    saver_on  = false;
    last_wake = millis();       // the idle clock starts over
    script.clear();
    if (settings::retro().fx) fx::start(fx::Kind::Roll);
    if (cur_mode == Mode::Basic) basic_enter();
}

// Two maze cells per frame — about 50 characters a second, close to what the
// one-liner managed on a real PET.
void saver_feed() {
    for (uint8_t i = 0; i < 2; i++)
        con.put((esp_random() & 1) ? pet::DIAG_UP : pet::DIAG_DN);
}

// A program that is running hides the cursor; one being typed shows it.
bool cursor_visible(uint32_t now) {
    if (script.typing()) return true;
    if (script.busy() || saver_on) return false;
    return (now / BLINK_MS) % 2 == 0;
}

// ── screens ─────────────────────────────────────────────────────────────────

void draw_boot() {
    U8G2& g = display::u8g2();
    g.setFont(u8g2_font_7x13B_tf);
    centred("PET-PC", 22);
    g.setFont(u8g2_font_6x12_tf);
    centred(boot1, 42);
    g.setFont(u8g2_font_5x7_tf);
    centred(boot2, 56);
}

// Power-on the PET way: a blank moment, the banner, the free memory counting
// up, READY. — and then the network status as the first lines of output.
void draw_boot_retro() {
    uint32_t t = millis() - boot_t0;
    if (t < 400) return;

    pet::text(8, 0, "*** PET-PC ***");
    pet::text(24, 8, "BASIC 4.0");

    char b[24];
    float p = (t - 400) / 900.0f;
    if (p > 1) p = 1;
    snprintf(b, sizeof(b), "%uK BYTES FREE", (unsigned)(boot_heap / 1024 * p));
    pet::text(0, 24, b);
    if (t < 1500) return;

    pet::text(0, 40, "READY.");
    pet::text(0, 48, boot1);
    pet::text(0, 56, boot2);
    size_t n = strlen(boot2);
    if (n < term::COLS && (millis() / BLINK_MS) % 2 == 0) pet::cursor(n * pet::CELL, 56);
}

void draw_clock() {
    U8G2& g = display::u8g2();
    char buf[32];

    g.setFont(u8g2_font_5x7_tf);
    timekeeper::format_date(buf, sizeof(buf));
    at(1, 7, buf);
    icons::wifi_bars(ox + 114, oy + 0, WiFi.RSSI(), WiFi.status() == WL_CONNECTED);
    hline(9);

    timekeeper::format_time(buf, sizeof(buf));
    g.setFont(u8g2_font_logisoso28_tn);
    centred(buf, 41);

    hline(44);

    weather::Current w = weather::get();
    if (!w.valid) {
        g.setFont(u8g2_font_5x7_tf);
        centred(weather::status() == weather::Status::NoWifi ? "weather: offline"
                                                             : "weather: loading...", 58);
        return;
    }

    icons::weather_icon(ox + 0, oy + 46, w.icon, 16);

    g.setFont(u8g2_font_6x12_tf);
    snprintf(buf, sizeof(buf), "%.1f°C", w.temp_c);
    at(19, 55, buf);

    g.setFont(u8g2_font_5x7_tf);
    snprintf(buf, sizeof(buf), "%.0f/%.0f°", w.day_max_c, w.day_min_c);
    right(buf, board::OLED_W - 1, 54);
    at(19, 62, w.desc);
}

// Big digits, 3x5 PETSCII cells each: full blocks for the strokes and the
// half-cell triangles for the rounded corners, the way PETSCII art draws them.
constexpr uint8_t o = 0, X = pet::FULL, BR = pet::TRI_BR, BL = pet::TRI_BL,
                  TR = pet::TRI_TR, TL = pet::TRI_TL;
constexpr uint8_t DASH = 10, BLANK = 11;
const uint8_t BIG[12][5][3] = {
    {{BR, X, BL}, {X, o, X}, {X, o, X}, {X, o, X}, {TR, X, TL}},     // 0
    {{BR, X, o }, {o, X, o}, {o, X, o}, {o, X, o}, {X, X, X }},      // 1
    {{BR, X, BL}, {o, o, X}, {BR, X, TL}, {X, o, o}, {X, X, X}},     // 2
    {{X, X, BL}, {o, o, X}, {o, X, X}, {o, o, X}, {X, X, TL}},       // 3
    {{X, o, X}, {X, o, X}, {X, X, X}, {o, o, X}, {o, o, X}},         // 4
    {{X, X, X}, {X, o, o}, {X, X, BL}, {o, o, X}, {X, X, TL}},       // 5
    {{BR, X, BL}, {X, o, o}, {X, X, BL}, {X, o, X}, {TR, X, TL}},    // 6
    {{X, X, X}, {o, o, X}, {o, BR, TL}, {o, X, o}, {o, X, o}},       // 7
    {{BR, X, BL}, {X, o, X}, {X, X, X}, {X, o, X}, {TR, X, TL}},     // 8
    {{BR, X, BL}, {X, o, X}, {TR, X, X}, {o, o, X}, {X, X, TL}},     // 9
    {{o, o, o}, {o, o, o}, {X, X, X}, {o, o, o}, {o, o, o}},         // -
    {},                                                              // blank
};

void big_digit(int x, int y, uint8_t d) {
    for (uint8_t r = 0; r < 5; r++)
        for (uint8_t c = 0; c < 3; c++)
            if (BIG[d][r][c]) pet::glyph(x + c * pet::CELL, y + r * pet::CELL, BIG[d][r][c]);
}

// Right-align PET text so its last cell ends at the screen edge.
void pet_right(int y, const char* s) {
    pet::text(term_x() + board::OLED_W - (int)strlen(s) * pet::CELL, oy + y, s);
}

// Date and temperature across the top, the condition along the bottom — a
// 16-column row is too narrow for both "12.4C" and "PARTLY CLOUDY".
void draw_block_clock() {
    char buf[32];
    bool h12 = !settings::state().clock_24h;
    timekeeper::format_date(buf, sizeof(buf));
    pet::text(ox, oy, buf);

    weather::Current w = weather::get();
    if (w.valid) {
        snprintf(buf, sizeof(buf), "%.0fC", w.temp_c);
        pet_right(0, buf);
        strlcpy(buf, w.desc, sizeof(buf));
    } else {
        strlcpy(buf, weather::status() == weather::Status::NoWifi ? "OFFLINE" : "LOADING...", sizeof(buf));
    }
    if (h12) buf[term::COLS - 3] = '\0';     // room for " AM"
    pet::text(ox, oy + 55, buf);

    struct tm t;
    bool ok = timekeeper::local(t);
    uint8_t d[4] = {DASH, DASH, DASH, DASH};
    if (ok) {
        int h = t.tm_hour;
        if (h12) {
            pet_right(55, h < 12 ? "AM" : "PM");
            h %= 12;
            if (h == 0) h = 12;
        }
        d[0] = h / 10;
        d[1] = h % 10;
        d[2] = t.tm_min / 10;
        d[3] = t.tm_min % 10;
        if (h12 && d[0] == 0) d[0] = BLANK;
    }

    // 24 px digits with a 2 px gap, and an 8 px cell for the colon.
    constexpr int Y = 12;
    big_digit(ox + 10, oy + Y, d[0]);
    big_digit(ox + 36, oy + Y, d[1]);
    big_digit(ox + 68, oy + Y, d[2]);
    big_digit(ox + 94, oy + Y, d[3]);
    if (!ok || t.tm_sec % 2 == 0) {
        U8G2& g = display::u8g2();
        g.drawBox(ox + 62, oy + Y + 10, 4, 4);
        g.drawBox(ox + 62, oy + Y + 26, 4, 4);
    }
}

void draw_weather() {
    U8G2& g = display::u8g2();
    char buf[36];

    g.setFont(u8g2_font_5x7_tf);
    at(1, 7, "WEATHER");
    age_text(buf, sizeof(buf), weather::data_age_s());
    right(buf, board::OLED_W - 1, 7);
    hline(9);

    weather::Current w = weather::get();
    if (!w.valid) {
        g.setFont(u8g2_font_6x12_tf);
        centred("no data yet", 34);
        g.setFont(u8g2_font_5x7_tf);
        centred(weather::status() == weather::Status::NoWifi ? "waiting for wifi"
                                                             : "fetching...", 48);
        return;
    }

    icons::weather_icon(ox + 2, oy + 12, w.icon, 24);

    g.setFont(u8g2_font_7x13B_tf);
    snprintf(buf, sizeof(buf), "%.1f°C", w.temp_c);
    at(32, 26, buf);

    g.setFont(u8g2_font_5x7_tf);
    snprintf(buf, sizeof(buf), "feels %.0f°", w.feels_c);
    at(32, 36, buf);
    at(2, 45, w.desc);
    hline(47);

    snprintf(buf, sizeof(buf), "hum %u%%", w.humidity);
    at(2, 55, buf);
    snprintf(buf, sizeof(buf), "%.0f km/h %s", w.wind_kmh, weather::compass(w.wind_deg));
    right(buf, board::OLED_W - 1, 55);

    snprintf(buf, sizeof(buf), "%.0f°/%.0f°", w.day_max_c, w.day_min_c);
    at(2, 62, buf);
    snprintf(buf, sizeof(buf), "rain %u%%", w.rain_prob);
    right(buf, board::OLED_W - 1, 62);
}

// ── HOME: Home Assistant ────────────────────────────────────────────────────

bool home_has_toggle() {
    for (uint8_t s = 0; s < settings::HA_SLOTS; s++)
        if (ha::kind(s) == ha::Kind::Toggle) return true;
    return false;
}

// One row per configured entity: name on the left, value flush right, the
// highlighted one in reverse video. The footer says what the keys do, or
// why there is nothing to show.
void draw_home() {
    int  x = term_x();
    char b[24];
    pet::text(x, oy, "HOME");
    struct tm t;
    if (timekeeper::local(t)) {
        snprintf(b, sizeof(b), "%02d:%02d", t.tm_hour, t.tm_min);
        pet_right(0, b);
    }

    if (!ha::configured()) {
        pet::text(x, oy + 16, "NOT SET UP.");
        pet::text(x, oy + 32, "ADD URL, TOKEN");
        pet::text(x, oy + 40, "AND ENTITIES ON");
        pet::text(x, oy + 48, "THE WEB PAGE");
        return;
    }

    uint8_t row = 1;
    for (uint8_t s = 0; s < settings::HA_SLOTS && row < 7; s++) {
        if (ha::kind(s) == ha::Kind::Unused) continue;
        const ha::Entity& e = ha::entity(s);
        const char* val = s == home_busy ? "..." : e.value;

        char line[term::COLS + 1];
        memset(line, ' ', term::COLS);
        line[term::COLS] = '\0';
        size_t vl = strnlen(val, 9);
        memcpy(line + term::COLS - vl, val, vl);
        size_t room = term::COLS - vl - 2;        // a gutter column, a space before the value
        memcpy(line + 1, e.name, strnlen(e.name, room));
        pet::text(x, oy + row * pet::CELL, line, s == home_sel);
        row++;
    }

    const char* foot = nullptr;
    switch (ha::status()) {
        case ha::Status::NoWifi:      foot = "?NO WIFI";         break;
        case ha::Status::AuthError:   foot = "?TOKEN REJECTED";  break;
        case ha::Status::Unreachable: foot = "?HA UNREACHABLE";  break;
        case ha::Status::Connecting:  foot = "CONNECTING...";    break;
        default:
            if (home_has_toggle()) foot = home_sel >= 0 ? "C:NEXT  HOLD:TGL" : "C:PICK A SWITCH";
            break;
    }
    if (foot) pet::text(x, oy + 7 * pet::CELL, foot);
}

// The row shows "..." on the glass before the POST blocks, like the desk
// toast; a failure gets a toast, a success just shows the new state.
void home_toggle(uint8_t s) {
    home_busy = s;
    tick(true);
    ha::Result r = ha::toggle(s);
    home_busy = -1;
    if (r == ha::Result::Ok) { tick(true); return; }

    char sub[22];
    if (r == ha::Result::NoWifi) strlcpy(sub, "no wifi", sizeof(sub));
    else snprintf(sub, sizeof(sub), "failed (%d)", ha::last_http_code());
    toast(ha::entity(s).name, sub, 2000);
}

void draw_listing(uint32_t now) {
    listing_scr.clear();
    listing([](const char* l) { listing_scr.println(l); });
    listing_scr.println("READY.");
    listing_scr.draw(term_x(), oy, (now / BLINK_MS) % 2 == 0);
}

void draw_info() {
    U8G2& g = display::u8g2();
    char buf[56];          // SSID (32) + RSSI, before it gets clipped on screen
    const auto& c = settings::state();

    g.setFont(u8g2_font_5x7_tf);
    at(1, 7, "SYSTEM");
    right(FW_VERSION, board::OLED_W - 1, 7);
    hline(9);

    at(1, 19, "host");
    at(34, 19, c.hostname);

    bool up = WiFi.status() == WL_CONNECTED;
    at(1, 28, "ip");
    at(34, 28, up ? WiFi.localIP().toString().c_str() : WiFi.softAPIP().toString().c_str());

    at(1, 37, "wifi");
    if (up) snprintf(buf, sizeof(buf), "%s %ddBm", c.wifi_ssid, (int)WiFi.RSSI());
    else    snprintf(buf, sizeof(buf), "AP mode");
    at(34, 37, buf);

    at(1, 46, "up");
    uptime_text(buf, sizeof(buf));
    at(34, 46, buf);

    at(1, 55, "heap");
    snprintf(buf, sizeof(buf), "%u B", (unsigned)ESP.getFreeHeap());
    at(34, 55, buf);

    char age[8];
    age_text(age, sizeof(age), weather::data_age_s());
    at(1, 62, "time");
    snprintf(buf, sizeof(buf), "%s  wx %s",
             timekeeper::synced() ? "synced" : "no ntp", age);
    at(34, 62, buf);
}

void draw_toast() {
    U8G2& g = display::u8g2();
    g.drawRFrame(ox + 3, oy + 7, board::OLED_W - 7 - ox, 48, 5);
    g.setFont(u8g2_font_7x13B_tf);
    centred(toast_title, 32);
    g.setFont(u8g2_font_6x12_tf);
    centred(toast_sub, 48);
}

uint32_t frame_ms() {
    bool moving = fx::active() || script.busy() || saver_on
               || (in_boot && settings::retro().boot);
    return moving ? ANIM_MS : FRAME_MS;
}

// HOME is only in the rotation once there is something to show on it.
bool mode_enabled(Mode m) {
    if (!(settings::retro().screens & (1 << (uint8_t)m))) return false;
    return m != Mode::Home || ha::configured();
}

}  // namespace

void begin() {
    uint8_t m = settings::state().start_mode;
    cur_mode = m < (uint8_t)Mode::COUNT ? (Mode)m : Mode::Clock;
    display::set_contrast(settings::state().contrast_day);
}

void boot_status(const char* line1, const char* line2) {
    in_boot = true;
    if (!boot_t0) {
        boot_t0   = millis();
        boot_heap = ESP.getFreeHeap();
    }
    strlcpy(boot1, line1 ? line1 : "", sizeof(boot1));
    strlcpy(boot2, line2 ? line2 : "", sizeof(boot2));
    tick(true);
}

void set_mode(Mode m) {
    if (m >= Mode::COUNT) m = Mode::Clock;
    if (settings::retro().fx && (in_boot || m != cur_mode))
        fx::start(in_boot ? fx::Kind::Roll : fx::Kind::Wipe);

    in_boot   = false;
    cur_mode  = m;
    last_wake = millis();
    clear_toast();
    saver_on  = false;          // the web UI can change screens under the saver
    if (tape_on) {
        // A tape sequence plays inline on the BASIC screen and full screen
        // over any other, so it follows the screen change instead of stopping.
        tape_overlay = m != Mode::Basic;
    } else {
        script.clear();
    }
    if (m == Mode::Basic) basic_enter();
    home_sel = -1;
    ha::set_focus(m == Mode::Home);
    tick(true);
}

// Step through ORDER by `dir` (+1 / -1), skipping screens that are off.
static void step_mode(int dir) {
    constexpr int N = (int)Mode::COUNT;
    int i = 0;
    while (i < N && ORDER[i] != cur_mode) i++;
    for (int k = 1; k <= N; k++) {
        Mode m = ORDER[((i + dir * k) % N + N) % N];
        if (mode_enabled(m)) { set_mode(m); return; }
    }
    set_mode(Mode::Clock);
}

void next_mode() { step_mode(+1); }
void prev_mode() { step_mode(-1); }

Mode mode() { return cur_mode; }

void center_short() {
    if (swallow_center || saver_on) { swallow_center = false; return; }
    if (!in_boot && cur_mode == Mode::Basic) { basic_run(false); return; }
    if (!in_boot && cur_mode == Mode::Home && ha::configured()) {
        for (int8_t s = home_sel + 1; s < (int8_t)settings::HA_SLOTS; s++) {
            if (ha::kind(s) == ha::Kind::Toggle) {
                home_sel = s;
                tick(true);
                return;
            }
        }
        home_sel = -1;           // past the last light: on to the next screen
    }
    next_mode();
}

void center_long() {
    if (swallow_center || saver_on) { swallow_center = false; return; }
    if (!in_boot && cur_mode == Mode::Basic) { next_mode(); return; }
    if (!in_boot && cur_mode == Mode::Home && home_sel >= 0) { home_toggle(home_sel); return; }
    refresh_weather();
}

void refresh_weather() {
    weather::refresh_now();
    // The tape sequence is feedback enough; otherwise say something happened.
    if (WiFi.status() != WL_CONNECTED) ui::toast("Weather", "no wifi", 1500);
    else if (!tape_wanted())           ui::toast("Weather", "refreshing", 1200);
}

void toast(const char* title, const char* sub, uint16_t ms) {
    strlcpy(toast_title, title ? title : "", sizeof(toast_title));
    strlcpy(toast_sub,   sub   ? sub   : "", sizeof(toast_sub));
    toast_on    = true;
    toast_until = ms ? millis() + ms : 0;
    last_wake   = millis();   // a timed toast leaves a wake tail behind it
    fx::cancel();             // desk feedback is never animated in
    tick(true);
}

void clear_toast() {
    toast_on    = false;
    toast_until = 0;
}

bool tape_wanted() {
    if (in_boot || saver_on || toast_on || tape_on) return false;
    // Retries after a failure stay silent — an API outage should not replay
    // the tape every 30 s. An explicit refresh still gets it.
    if (weather::status() == weather::Status::Error && !weather::forced()) return false;
    // On the BASIC screen an explicit LOAD always gets its tape: it is the
    // command's output. Scheduled refreshes follow the setting like anywhere.
    if (cur_mode == Mode::Basic && weather::forced()) return true;
    return settings::retro().tape && display::powered();
}

bool tape_busy() { return tape_on; }

bool animating() { return fx::active() || script.busy() || saver_on || tape_on; }

void tape_start() {
    bool inline_ = !saver_on && cur_mode == Mode::Basic;
    tape_on      = true;
    tape_overlay = !inline_;
    if (tape_overlay) {
        script.clear();
        con.clear();
        if (settings::retro().fx) fx::start(fx::Kind::Wipe);
    }
    basic_last = millis();
    script.type("LOAD \"WEATHER\"");
    script.out("PRESS PLAY ON TAPE #1");
    script.wait(900);
    script.out("OK");
    script.wait(500);
    script.out("SEARCHING FOR WEATHER");
    script.wait(1100);
    script.out("FOUND WEATHER");
    script.wait(600);
    script.out("LOADING");
    script.call(tape_fetch);
}

void tick(bool force) {
    uint32_t now = millis();
    const auto& r = settings::retro();

    if (toast_on && toast_until && (int32_t)(now - toast_until) >= 0) {
        toast_on = false;
        force    = true;
    }
    if (!force && now - last_frame_ms < frame_ms()) return;
    last_frame_ms = now;

    update_shift();

    // A dark pixel is the only pixel that does not age, so the blank window is
    // worth more to the panel than the pixel shift and the dim schedule put
    // together. Any press wakes it — including the desk buttons, which still
    // do their job on the same press; nothing here swallows an event.
    bool pressed = (int32_t)(buttons::last_activity_ms() - last_wake) > 0;
    if (pressed) last_wake = buttons::last_activity_ms();

    // Screensaver: after saver_min idle minutes, 10 PRINT for a minute. A
    // press ends it; if that press is the centre key, it does nothing else.
    if (saver_on) {
        if (pressed || now - saver_since >= SAVER_RUN_MS) {
            if (pressed && buttons::held(buttons::Id::Center)) swallow_center = true;
            saver_stop();
        }
    } else if (r.saver_min && !in_boot && !toast_on && !tape_on && !script.busy()
               && cur_mode != Mode::Basic && display::powered()
               && now - last_wake >= r.saver_min * 60000UL) {
        saver_start();
    }

    if (home_sel >= 0 && now - last_wake >= HOME_SEL_MS) home_sel = -1;

    if (!in_boot && !saver_on && !tape_on && cur_mode == Mode::Basic
        && !script.busy() && now - basic_last >= ATTRACT_MS)
        basic_run(true);

    script.tick(now);
    if (saver_on && !script.busy()) saver_feed();

    bool blanking = in_night_window() && settings::state().contrast_night == 0;
    bool awake    = in_boot || toast_on || now - last_wake < WAKE_MS;
    bool lit      = !blanking || awake;

    // With the CRT effects on, the panel goes dark the way a CRT did — the
    // picture collapses to a line and a dot — and comes back the same way.
    if (r.fx) {
        if (!lit && display::powered()) {
            if (!collapsing) {
                collapsing = true;
                fx::start(fx::Kind::Collapse);
            }
            lit = fx::active();             // keep the glass on until it has played
        } else {
            if (collapsing && fx::kind() == fx::Kind::Collapse) fx::cancel();
            collapsing = false;
            if (lit && !display::powered()) fx::start(fx::Kind::Expand);
        }
    }

    if (lit) display::set_contrast(blanking ? WAKE_CONTRAST : scheduled_contrast());

    U8G2& g = display::u8g2();
    g.clearBuffer();
    if (toast_on)                    draw_toast();
    else if (in_boot)                r.boot ? draw_boot_retro() : draw_boot();
    else if (tape_overlay || saver_on) con.draw(term_x(), oy, cursor_visible(now));
    else switch (cur_mode) {
        case Mode::Weather:    draw_weather();       break;
        case Mode::Info:       draw_info();          break;
        case Mode::BlockClock: draw_block_clock();   break;
        case Mode::Listing:    draw_listing(now);    break;
        case Mode::Home:       draw_home();          break;
        case Mode::Basic:      con.draw(term_x(), oy, cursor_visible(now)); break;
        default:               draw_clock();         break;
    }
    fx::apply();

    // The buffer is redrawn either way, so /shot.bmp keeps mirroring the frame
    // the device would be showing. A dark panel just never sees the transfer —
    // and the fresh frame goes out before the glass lights up, so waking does
    // not flash whatever was left in GDDRAM ten minutes ago.
    if (lit) {
        g.sendBuffer();
        display::set_power(true);
    } else {
        display::set_power(false);
    }
}

}  // namespace ui
