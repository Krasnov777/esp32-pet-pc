#include "actions.h"
#include "ha.h"
#include "settings.h"
#include "ui.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

namespace actions {
namespace {

constexpr uint16_t TIMEOUT_MS = 4000;

char result[24];

const char* done(const char* s) {
    strlcpy(result, s, sizeof(result));
    return result;
}

// ESPHome's web server serves buttons under their display name, so a stored
// URL may keep the readable "Desk Position 3" form; the spaces are encoded on
// the way out. The buffer takes a URL of nothing but spaces at full length.
void encode_spaces(const char* in, char* out, size_t cap) {
    size_t o = 0;
    for (const char* p = in; *p && o + 4 < cap; p++) {
        if (*p == ' ') { out[o++] = '%'; out[o++] = '2'; out[o++] = '0'; }
        else           { out[o++] = *p; }
    }
    out[o] = '\0';
}

// The request itself. Returns the HTTP status, or a negative HTTPClient error.
int http_request(const settings::KeyAction& a) {
    char url[sizeof(a.url) * 3 + 1];
    encode_spaces(a.url, url, sizeof(url));

    WiFiClient       plain;
    WiFiClientSecure secure;
    bool tls = strncmp(url, "https://", 8) == 0;
    if (tls) secure.setInsecure();

    HTTPClient http;
    http.setTimeout(TIMEOUT_MS);
    if (!(tls ? http.begin(secure, url) : http.begin(plain, url))) return HTTPC_ERROR_CONNECTION_REFUSED;

    int code;
    if (a.method == settings::METHOD_GET) {
        code = http.GET();
    } else {
        // ESPHome answers with an empty 200, but only to a POST that declares
        // its (empty) body. The ESP8266 HTTPClient sent Content-Length: 0 for
        // POST(""); the ESP32 one omits the header for an empty payload, and
        // ESPHome's web server does not accept a bodyless POST without it.
        http.addHeader("Content-Length", "0");
        code = http.POST("");
    }
    http.end();
    Serial.printf("[key] %s %s -> HTTP %d\n",
                  a.method == settings::METHOD_GET ? "GET" : "POST", url, code);
    return code;
}

void result_text(char* out, size_t cap, bool ok, int code) {
    if (ok)            strlcpy(out, "sent", cap);
    else if (code > 0) snprintf(out, cap, "failed (%d)", code);
    else               strlcpy(out, "no answer", cap);
}

}  // namespace

const char* run(Side side, bool hold) {
    const settings::Keys& keys = settings::keys();
    uint8_t base = side == Side::Left ? settings::LEFT_PRESS : settings::RIGHT_PRESS;
    const settings::KeyAction* a = &keys.act[base + (hold ? 1 : 0)];
    if (hold && a->type == settings::KA_SAME) a = &keys.act[base];

    char title[20];
    if (a->label[0])                strlcpy(title, a->label, sizeof(title));
    else if (a->entity[0])          strlcpy(title, a->entity, sizeof(title));
    else                            strlcpy(title, side == Side::Left ? "Left key" : "Right key", sizeof(title));

    switch (a->type) {
        case settings::KA_SCREEN:  ui::set_mode((ui::Mode)a->screen); return done("done");
        case settings::KA_NEXT:    ui::next_mode();                   return done("done");
        case settings::KA_PREV:    ui::prev_mode();                   return done("done");
        case settings::KA_WEATHER: ui::refresh_weather();             return done("done");

        case settings::KA_HTTP: {
            if (!a->url[0]) { ui::toast(title, "no url set", 1500); return done("no url set"); }
            if (WiFi.status() != WL_CONNECTED) { ui::toast(title, "no wifi", 2000); return done("no wifi"); }
            ui::toast(title, "sending...", 0);        // on the glass before we block
            int code = http_request(*a);
            char sub[22];
            result_text(sub, sizeof(sub), code >= 200 && code < 300, code);
            ui::toast(title, sub, 2000);
            return done(sub);
        }

        case settings::KA_HA_TOGGLE:
        case settings::KA_HA_SERVICE: {
            bool toggle = a->type == settings::KA_HA_TOGGLE;
            if (!ha::ready()) { ui::toast(title, "HA not set up", 2000); return done("HA not set up"); }
            if (toggle && !a->entity[0]) { ui::toast(title, "no entity set", 1500); return done("no entity set"); }
            if (!toggle && !a->service[0]) { ui::toast(title, "no service set", 1500); return done("no service set"); }
            ui::toast(title, "sending...", 0);
            ha::Result r = ha::call(toggle ? "homeassistant.toggle" : a->service, a->entity);
            char sub[22];
            if (r == ha::Result::NoWifi)             strlcpy(sub, "no wifi", sizeof(sub));
            else if (r == ha::Result::NotConfigured) strlcpy(sub, "bad service", sizeof(sub));
            else result_text(sub, sizeof(sub), r == ha::Result::Ok, ha::last_http_code());
            ui::toast(title, sub, 2000);
            return done(sub);
        }

        default:
            return done("nothing set");
    }
}

}  // namespace actions
