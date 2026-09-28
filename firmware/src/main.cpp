// Entry point — brings up the panel, the network and the pollers, then runs a
// cooperative loop. Network calls block briefly and are bounded; the ones a
// key starts put "sending..." on the glass first (see ARCHITECTURE.md).
//
// On the ESP32 this all runs in the Arduino loop task; WiFi, mDNS and the TCP
// stack live in their own FreeRTOS tasks, so MDNS needs no per-loop update.
#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <esp_system.h>

#include "buttons.h"
#include "actions.h"
#include "display.h"
#include "ha.h"
#include "settings.h"
#include "timekeeper.h"
#include "ui.h"
#include "weather.h"
#include "web_server.h"

#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef OTA_PASSWORD
#define OTA_PASSWORD "pet-pc"
#endif

namespace {

constexpr uint32_t WIFI_TIMEOUT_MS  = 25000;
constexpr uint32_t WIFI_RETRY_MS    = 30000;
constexpr uint32_t AP_RETRY_MS      = 5 * 60 * 1000;

// Set when the home network failed at boot and the hotspot came up instead.
bool     ap_fallback = false;
uint32_t ap_since    = 0;

String ap_ssid() {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char buf[26];
    snprintf(buf, sizeof(buf), "PET-PC-%02X%02X", mac[4], mac[5]);
    return String(buf);
}

bool try_sta() {
    const auto& c = settings::state();
    if (strlen(c.wifi_ssid) == 0) {
        Serial.println(F("[wifi] no SSID configured — going straight to AP mode"));
        return false;
    }

    WiFi.persistent(false);          // credentials live in our NVS image
    // On the ESP32 core the hostname must be set before the STA interface
    // comes up, or DHCP announces the default "esp32c6-XXXX".
    WiFi.setHostname(c.hostname);
    WiFi.mode(WIFI_STA);
    // Modem sleep adds latency to every inbound request (web UI, OTA). This
    // device is mains powered, so keep the radio awake.
    WiFi.setSleep(false);
    WiFi.setAutoReconnect(true);
    WiFi.begin(c.wifi_ssid, c.wifi_pass);
    Serial.printf("[wifi] STA -> '%s'\n", c.wifi_ssid);

    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_TIMEOUT_MS) {
        char sub[26];
        snprintf(sub, sizeof(sub), "%lus", (unsigned long)((millis() - t0) / 1000));
        ui::boot_status(c.wifi_ssid, sub);
        buttons::tick();
        delay(120);
    }
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println(F("[wifi] STA timed out"));
        return false;
    }
    Serial.printf("[wifi] connected, IP %s\n", WiFi.localIP().toString().c_str());
    return true;
}

void start_ap() {
    WiFi.mode(WIFI_AP);
    String ssid = ap_ssid();
    WiFi.softAP(ssid.c_str());
    Serial.printf("[wifi] AP '%s' on %s\n", ssid.c_str(),
                  WiFi.softAPIP().toString().c_str());
    ap_fallback = true;
    ap_since    = millis();
    ui::boot_status(ssid.c_str(), WiFi.softAPIP().toString().c_str());
}

void setup_ota() {
    ArduinoOTA.setHostname(settings::state().hostname);
    ArduinoOTA.setPassword(OTA_PASSWORD);
    // The core waits only 1 s for each chunk, then gives up after three
    // nudges — and espota on the host then hangs forever. In the PET case the
    // link sits around -80 dBm, where multi-second TCP retransmit gaps are
    // routine, so that default made OTA fail at random a few KB in.
    ArduinoOTA.setTimeout(10000);
    ArduinoOTA.onStart([]() { ui::toast("OTA", "starting", 0); });
    ArduinoOTA.onProgress([](unsigned int done, unsigned int total) {
        static uint8_t last_pct = 255;
        uint8_t pct = total ? done * 100 / total : 0;
        if (pct == last_pct) return;
        last_pct = pct;
        char sub[8];
        snprintf(sub, sizeof(sub), "%u%%", pct);
        ui::toast("OTA", sub, 0);
    });
    ArduinoOTA.onEnd([]()   { ui::toast("OTA", "rebooting", 0); });
    ArduinoOTA.onError([](ota_error_t e) {
        Serial.printf("[ota] error %u\n", e);
        ui::toast("OTA", "failed", 3000);
    });
    ArduinoOTA.begin();
    Serial.println(F("[ota] ready on :3232"));
}

void handle_buttons() {
    using buttons::Event;
    using buttons::Id;

    // Outer keys: whatever the web page set for a press and for a hold
    // (a hold defaults to "same as press", so it never feels dead).
    switch (buttons::take(Id::Left)) {
        case Event::Short: actions::run(actions::Side::Left, false); break;
        case Event::Long:  actions::run(actions::Side::Left, true);  break;
        default:           break;
    }
    switch (buttons::take(Id::Right)) {
        case Event::Short: actions::run(actions::Side::Right, false); break;
        case Event::Long:  actions::run(actions::Side::Right, true);  break;
        default:           break;
    }

    // The centre key's meaning depends on the screen (the BASIC screen runs
    // commands with it), so ui decides.
    switch (buttons::take(Id::Center)) {
        case Event::Short: ui::center_short(); break;
        case Event::Long:  ui::center_long();  break;
        default:           break;
    }
}

// A due weather fetch either happens right here or is handed to the UI,
// which plays the tape-loading sequence and fetches at its LOADING step.
void weather_tick() {
    if (!weather::due() || ui::tape_busy()) return;
    if (ui::tape_wanted()) ui::tape_start();
    else                   weather::fetch_now();
}

const char* reset_reason() {
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON:  return "power-on";
        case ESP_RST_SW:       return "software";
        case ESP_RST_PANIC:    return "panic";
        case ESP_RST_INT_WDT:
        case ESP_RST_TASK_WDT:
        case ESP_RST_WDT:      return "watchdog";
        case ESP_RST_BROWNOUT: return "brownout";
        case ESP_RST_USB:      return "usb";
        case ESP_RST_EXT:      return "external";
        default:               return "other";
    }
}

void wifi_watchdog() {
    static uint32_t last_ok_ms = 0;
    if (WiFi.getMode() != WIFI_STA) return;
    uint32_t now = millis();
    if (WiFi.status() == WL_CONNECTED) {
        last_ok_ms = now;
    } else if (now - last_ok_ms > WIFI_RETRY_MS) {
        last_ok_ms = now;                // rate-limit the attempts
        Serial.println(F("[wifi] down >30 s — reconnecting"));
        WiFi.reconnect();
    }
}

// After a power cut the router is often slower than the 25 s the box gives
// it at boot, and the hotspot would stay up for good. So an idle hotspot —
// nobody joined to it — restarts the box every few minutes to try the home
// network again. Someone configuring it over the hotspot is never cut off.
void ap_retry() {
    if (!ap_fallback || !settings::state().wifi_ssid[0]) return;
    if (WiFi.softAPgetStationNum() > 0) { ap_since = millis(); return; }
    if (millis() - ap_since < AP_RETRY_MS) return;
    Serial.println(F("[wifi] hotspot idle — restarting to retry the home network"));
    ESP.restart();
}

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(120);
    Serial.printf("\n[boot] pet-pc " FW_VERSION " — reset: %s\n", reset_reason());

    settings::begin();
    display::begin();
    ui::begin();
    buttons::begin();

    ui::boot_status("starting", FW_VERSION);

    bool online = try_sta();
    if (!online) start_ap();

    if (online && MDNS.begin(settings::state().hostname)) {
        MDNS.addService("http", "tcp", 80);
        Serial.printf("[mdns] %s.local\n", settings::state().hostname);
    }

    setup_ota();
    web::begin();
    timekeeper::begin();
    weather::begin();
    ha::begin();

    // Hold the network summary on screen long enough to read the IP.
    uint32_t t0 = millis();
    while (millis() - t0 < 1500) { ui::tick(); web::tick(); delay(10); }

    ui::set_mode((ui::Mode)settings::state().start_mode);
}

void loop() {
    buttons::tick();
    handle_buttons();

    ArduinoOTA.handle();
    web::tick();

    timekeeper::tick();
    weather_tick();
    if (!ui::animating()) ha::tick();    // a background GET would stutter an animation
    wifi_watchdog();
    ap_retry();

    ui::tick();
    delay(2);        // let the SDK run its tasks instead of spinning
}
