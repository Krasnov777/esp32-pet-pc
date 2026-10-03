# Changelog

Firmware versions are the `FW_VERSION` in `firmware/platformio.ini`; the
running one is on the System screen and in `/api/state` (`live.fw`).
Settings live in NVS and survive every update below — each entry says when
a settings blob was migrated.

## 1.3.1 — 2026-10-03

- A weather response without a weather code now shows the condition as `?`
  instead of falling back to "Overcast", so a broken reading can no longer pass
  for real weather.

## 1.3.0 — 2026-09-28

- **Configurable outer keys.** Left and right each have a press and a hold
  action: an HTTP request (POST/GET, http or https), show a screen, next or
  previous screen, refresh the weather, toggle an HA entity, or call any HA
  service (`scene.turn_on`, `input_select.select_next`…). The centre key is
  unchanged. Replaces the fixed desk presets.
- Settings: new `petpc/keys` blob, seeded from the old desk presets, so an
  updated device keeps doing what it did. A fresh device gets previous/next
  screen on the outer keys; the author's desk URLs are gone from the
  defaults. The `desk` JSON key and `POST /api/desk` are removed; use
  `keys` and `POST /api/key?key=left|center|right&hold=0|1`.
- **New configuration page.** Tabs (Live, Keys, Screens, Display, Home
  Assistant, Weather & time, Network, System), help text on every setting,
  virtual keys with the live screen, time-zone presets, settings download.
  PET look: scanlines, phosphor glow, CRT power-on, tab wipe, green / amber /
  white — CSS only, with an off switch.
- **Safer saving.** Save is disabled until the settings have loaded (the
  blank-form problem is gone), only changed fields are sent, fields are
  validated first, and WiFi / HA URL changes ask before they take effect. The
  device ignores a blank device name, time zone, time server or weather host.
- **Hotspot retry.** A box that fell back to its hotspot at boot restarts
  every five minutes, while nobody is connected to the hotspot, to try the
  home network again — no more stranding after a power cut.
- Long URLs with many spaces are no longer cut off.

## 1.2.2 — 2026-09-28

- **Start screen is a setting.** A *Start screen* dropdown in the Display
  card picks the screen shown after the boot banner; new devices start on the
  **Block clock**. The box no longer remembers the last screen used, so a key
  press never writes to flash.

## 1.2.1 — 2026-09-28

- **Value / Switch per HA entity.** Each Home Assistant entity is marked
  *Value* (shown read-only) or *Switch* (ON/OFF, toggled from the HOME
  screen). Typing an entity id pre-selects the type from its domain.
- Settings: `petpc/ha` v1 → v2. Existing entities are typed from their domain;
  URL and token carry over.
- HOME footer reads `C:PICK A SWITCH`.

## 1.2.0 — 2026-09-28

- **Home Assistant.** A HOME screen shows up to six HA entities, read over the
  REST API with a long-lived token; on HOME, centre press highlights the next
  light, centre hold toggles it (`homeassistant/toggle`). HOME joins the
  rotation after Weather once HA is configured.
- Web page: Home Assistant card — URL (http or https), write-only token,
  entities with labels, read interval, live values, *Test connection*
  (`POST /api/ha/test`).
- Changing the HA URL without re-entering the token clears the token.
- Settings: new `petpc/ha` blob; `petpc/retro` v1 → v2 switches the HOME
  screen on without touching the other retro options.
- Flash grows ~120 KB for TLS (~1.36 MB of 1.9 MB).

## 1.1.0 — 2026-09-28

- **Retro screens.** Block clock (PETSCII block digits), LIST (live data as a
  BASIC listing), BASIC prompt (the centre key types and runs commands;
  attract mode after 30 s idle).
- **Boot banner** — `*** PET-PC ***`, `BASIC 4.0`, free heap counting up,
  `READY.`
- **Tape loading** — `PRESS PLAY ON TAPE #1` … `LOADING` around every weather
  fetch, then `RUN` prints the reading.
- **Screensaver** — the `10 PRINT` maze for a minute after 20 idle minutes.
- **CRT effects** — scan-line wipe between screens, vertical roll for
  automatic changes, collapse to a dot at night blank and expand on wake.
- Web page: Retro card (screens in the rotation, screensaver time, effects,
  tape, boot banner).
- Screens animate at 25 fps, still screens stay at 5 fps.
- A first weather fetch that failed for lack of WiFi is now retried when
  WiFi comes back.
- Settings: new `petpc/retro` blob; `petpc/cfg` untouched.

## 1.0.0 — 2026-09-27

- ESP32-C6-Zero port of mini-esp-pc, the author's earlier ESP8266 desk box:
  Clock, Weather and System screens; Megadesk presets on the outer keys;
  config page, REST API and live screen mirror; NVS settings; OTA; hotspot
  fallback; burn-in shift and night blank.
- Desk POSTs send an explicit `Content-Length: 0` (ESPHome rejects them
  without it on the ESP32 client).
- OTA per-chunk timeout raised to 10 s for the weak signal inside the case.
- No buzzer: the hardware is keys and display only.
