#include "web_server.h"
#include "actions.h"
#include "display.h"
#include "ha.h"
#include "settings.h"
#include "timekeeper.h"
#include "ui.h"
#include "weather.h"
#include "web_page.h"

#include <WebServer.h>
#include <WiFi.h>
#include <ArduinoJson.h>

namespace web {
namespace {

WebServer server(80);
uint32_t reboot_at = 0;

void send_state() {
    JsonDocument doc;
    JsonObject root = doc.to<JsonObject>();
    settings::to_json(root);

    JsonObject live = root["live"].to<JsonObject>();
    bool up = WiFi.status() == WL_CONNECTED;
    live["ip"]   = up ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
    live["rssi"] = up ? WiFi.RSSI() : 0;
    live["fw"]   = FW_VERSION;
    live["heap"] = ESP.getFreeHeap();
    live["mode"] = (uint8_t)ui::mode();
    // The mirror below keeps updating while the glass is dark, so say so.
    live["screen_on"] = display::powered();
    live["time_ok"] = timekeeper::synced();
    live["uptime_s"] = millis() / 1000;
    live["ap"]       = (WiFi.getMode() & WIFI_AP) != 0;   // the hotspot is up (home WiFi failed at boot)

    JsonObject hl = live["ha"].to<JsonObject>();
    hl["status"] = ha::status_text();
    JsonArray vals = hl["values"].to<JsonArray>();
    for (uint8_t i = 0; i < settings::HA_SLOTS; i++) {
        const ha::Entity& e = ha::entity(i);
        vals.add(ha::kind(i) == ha::Kind::Unused ? "" : e.value);
    }

    weather::Current w = weather::get();
    JsonObject wx = root["wx"].to<JsonObject>();
    wx["valid"]  = w.valid;
    wx["temp"]   = w.temp_c;
    wx["desc"]   = w.desc;
    wx["age_s"]  = weather::data_age_s();

    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
}

void handle_settings() {
    String body = server.arg("plain");
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
        Serial.printf("[web] settings POST %u B — parse failed: %s\n",
                      (unsigned)body.length(), err.c_str());
        server.send(400, "application/json", "{\"error\":\"bad json\"}");
        return;
    }

    // New WiFi credentials only take effect on the next boot, so the client
    // needs to know whether to reboot us.
    char prev_ssid[sizeof(settings::state().wifi_ssid)];
    char prev_pass[sizeof(settings::state().wifi_pass)];
    strlcpy(prev_ssid, settings::state().wifi_ssid, sizeof(prev_ssid));
    strlcpy(prev_pass, settings::state().wifi_pass, sizeof(prev_pass));

    bool changed = settings::apply_json(doc.as<JsonVariantConst>());
    bool wifi_changed = strcmp(prev_ssid, settings::state().wifi_ssid) != 0
                     || strcmp(prev_pass, settings::state().wifi_pass) != 0;
    if (changed) {
        settings::save();
        // Re-apply the settings that other modules cache.
        timekeeper::begin();
        weather::refresh_now();
        ha::reconfigure();
    }
    Serial.printf("[web] settings POST %u B -> %s%s (ssid='%s')\n",
                  (unsigned)body.length(), changed ? "saved" : "no change",
                  wifi_changed ? ", wifi changed" : "", settings::state().wifi_ssid);

    JsonDocument resp;
    resp["changed"]      = changed;
    resp["wifi_changed"] = wifi_changed;
    String out;
    serializeJson(resp, out);
    server.send(200, "application/json", out);
}

// The web page's virtual keys: the same actions as the real ones.
// ?key=left|center|right&hold=0|1
void handle_key() {
    String k = server.arg("key");
    bool hold = server.arg("hold") == "1";
    const char* result = "done";
    if (k == "left")        result = actions::run(actions::Side::Left, hold);
    else if (k == "right")  result = actions::run(actions::Side::Right, hold);
    else if (k == "center") hold ? ui::center_long() : ui::center_short();
    else { server.send(400, "application/json", "{\"error\":\"key must be left, center or right\"}"); return; }

    JsonDocument resp;
    resp["result"] = result;
    String out;
    serializeJson(resp, out);
    server.send(200, "application/json", out);
}

void handle_ha_test() {
    int code = ha::test();
    JsonDocument resp;
    resp["ok"]   = code == 200;
    resp["http"] = code;
    String out;
    serializeJson(resp, out);
    server.send(200, "application/json", out);
}

void handle_shot() {
    size_t n = display::bmp_size();
    uint8_t* bmp = (uint8_t*)malloc(n);
    if (!bmp) { server.send(503, "text/plain", "out of memory"); return; }
    display::to_bmp(bmp);
    server.setContentLength(n);
    server.send(200, "image/bmp", "");
    server.client().write(bmp, n);
    free(bmp);
}

}  // namespace

void begin() {
    // send_P streams the page straight from flash instead of copying ~30 KB into a String.
    server.on("/", HTTP_GET, []() { server.send_P(200, "text/html", INDEX_HTML); });
    server.on("/api/state",    HTTP_GET,  send_state);
    server.on("/api/settings", HTTP_POST, handle_settings);
    server.on("/api/key",      HTTP_POST, handle_key);
    server.on("/api/mode",     HTTP_POST, []() {
        ui::next_mode();
        server.send(200, "application/json", "{\"ok\":true}");
    });
    server.on("/api/reboot",   HTTP_POST, []() {
        server.send(200, "application/json", "{\"ok\":true}");
        reboot_at = millis() + 500;      // let the response flush first
    });
    server.on("/api/ha/test",  HTTP_POST, handle_ha_test);
    server.on("/shot.bmp",     HTTP_GET,  handle_shot);
    server.onNotFound([]() { server.send(404, "text/plain", "not found"); });
    server.begin();
    Serial.println(F("[web] listening on :80"));
}

void tick() {
    server.handleClient();
    if (reboot_at && (int32_t)(millis() - reboot_at) >= 0) ESP.restart();
}

}  // namespace web
