# Architecture

A guided tour of the firmware: what each module owns, how a frame gets drawn,
how a button press becomes an HTTP request, and where to put new code.

---

## Shape of the thing

One core, no RTOS tasks, no async framework. Everything is a cooperative tick
from `loop()`:

```
loop()
 ├─ buttons::tick()      poll 3 pins, debounce, emit Short/Long events
 ├─ handle_buttons()     translate events into actions (main.cpp)
 ├─ ArduinoOTA.handle()
 ├─ web::tick()          serve one HTTP request if any is pending
 ├─ timekeeper::tick()   notice the first successful SNTP sync
 ├─ weather_tick()       if a fetch is due: fetch, or hand it to the tape sequence
 ├─ ha::tick()           read one HA entity if one is due (skipped while animating)
 ├─ wifi_watchdog()      reconnect if the association has been down >30 s
 ├─ ap_retry()           hotspot idle for 5 min → restart to retry the home network
 ├─ ui::tick()           advance scripts/effects, redraw + push a frame
 │                       (every 200 ms, or 40 ms while something moves)
 └─ delay(2)             yield to the SDK
```

On the ESP32-C6 this is the Arduino `loop` task; WiFi, lwIP and mDNS run in
their own FreeRTOS tasks, so a blocking call no longer starves the radio the
way it did on the ESP8266. The cooperative shape is kept anyway: a stalled
`loop()` still means stalled buttons, a frozen screen and an unserved web UI.
So every network call in the tick chain is a synchronous `HTTPClient` request
that is small, bounded and either rare or made visible — no async client.

### Blocking calls

| Call | Where from | Bound | How the stall is handled |
|------|-----------|-------|--------------------------|
| `actions::run()` — a key's HTTP request | button handler, `/api/key` | 4 s timeout | "sending…" toast is on the glass first |
| `actions::run()` → `ha::call()` — a key's HA toggle / service | button handler, `/api/key` | 1.5 s connect + 3 s | "sending…" toast first |
| `ha::toggle()` — `homeassistant/toggle` POST | button handler (HOME, centre hold) | 1.5 s connect + 3 s | the row shows `...` first |
| `ha::tick()` → one `GET /api/states/<id>` | loop, one entity per step | 1.5 s connect + 3 s; tens of ms on the LAN | skipped while anything animates; 60 s back-off when HA is unreachable or rejects the token |
| `weather::fetch_now()` — Open-Meteo GET | loop, every `weather_min` minutes | 8 s timeout | with the tape on, runs at the `LOADING` step, already on the glass |
| `ha::test()` — `GET /api/` | `/api/ha/test` | as `ha::toggle()` | the page waits for the answer |

A key action is the model: whatever it drives should react *now*, an async
HTTP client is more machinery than one request deserves, and the stall is made
visible by pushing the "sending…" frame to the OLED before the request starts:

```cpp
ui::toast(title, "sending...", 0);     // ui::toast() forces a frame
int code = http_request(action);       // blocks, bounded by 4 s
ui::toast(title, result, 2000);
```

A HOME-screen toggle is the same pattern: the row shows `...`, then the
`homeassistant/toggle` POST runs from the button handler. The tape sequence's
fetch is too: the `LOADING` line is on the glass before the GET starts (see
[Scripts](#scripts-the-pet-terminal)).

---

## Modules

| Module | Owns | Depends on |
|--------|------|-----------|
| `board_config.h` | Every GPIO number in the project | — |
| `settings` | `Config`, `Retro`, `Ha` and `Keys` structs, their NVS blobs and migrations, JSON patch/serialise | ArduinoJson, Preferences |
| `display` | `Wire` bring-up, the U8g2 object, contrast, panel power, BMP capture | board_config |
| `icons` | Weather + WiFi glyphs drawn from primitives | display, weather |
| `pet` | The 8x8 text face, PETSCII graphics cells (blocks, triangles, maze diagonals) | display |
| `term` | 16x8 character `Screen` (wrap, scroll) and the `Script` player that types into it | display, pet |
| `fx` | CRT wipe, roll, collapse and expand, applied to the finished frame | display |
| `ui` | Screens, mode machine, toasts, burn-in shift, night blank, BASIC screen, tape sequence, screensaver, HOME screen | buttons, display, fx, ha, icons, pet, settings, term, timekeeper, weather |
| `buttons` | Debounced Short/Long events for 3 pins | board_config |
| `timekeeper` | SNTP, POSIX timezone, formatted date/time strings | settings |
| `weather` | Open-Meteo poll (`due()` / `fetch_now()`), `Current` snapshot, `Status` | settings |
| `ha` | Home Assistant REST: round-robin entity reads, toggle, connection test, `Status` | settings |
| `actions` | The outer keys' press/hold actions: HTTP, screens, weather, HA toggle/service | ha, settings, ui |
| `web_page.h` | The configuration page — HTML, CSS and JS in one flash constant | — |
| `web_server` | REST API, `/shot.bmp`, serving the page | everything above |
| `main` | Boot order, WiFi/AP fallback and retry, OTA, key dispatch, the tick chain | everything above |

Dependencies only point downward. `ui` reads from `weather` and `timekeeper`;
neither knows the UI exists, which is what keeps adding a screen cheap.

---

## Rendering

U8g2 runs in **full buffer** mode: 1 KB of RAM holding the whole 128x64 frame,
drawn from scratch every time and pushed in one I²C transfer.

```cpp
g.clearBuffer();
draw_clock();          // or draw_weather() / draw_info() / draw_toast()
g.sendBuffer();
```

There is no partial update, no page list, no dirty-region tracking. At 5 fps a
full transfer at 400 kHz costs about 25 ms of the 200 ms budget, and in exchange
a screen is just a function that draws — which is exactly what the ESPHome
version's `display.page.show` + `component.update` pairs were working around.

The interval drops to 40 ms (25 fps) only while something moves: a script is
typing or playing, a CRT effect is running, the screensaver is on, or the
retro boot banner is up. At that rate the transfer takes most of each frame,
which is why still screens stay at 5 fps.

Three things are applied globally, in `ui::tick()`, before the screen function
runs:

- **Burn-in shift.** Every two minutes the whole frame moves by `ox` ∈ 0..2 and
  `oy` ∈ 0..1 pixels. Screen functions never touch raw coordinates — they go
  through `at()`, `centred()`, `right()` and `hline()`, which add the offset.
- **Night dim, or night off.** `in_night_window()` compares the local hour
  against the configured window (which normally wraps midnight, e.g. 23 → 7).
  Inside it, `contrast_night` applies — and a `contrast_night` of 0 means the
  panel is powered down (`display::set_power(false)`) rather than driven very
  dim, because only a dark pixel stops ageing. `buttons::last_activity_ms()`
  and a lingering toast wake it for `WAKE_MS` at `WAKE_CONTRAST`; the press
  that woke it is not consumed, so the outer keys still work in the dark.
  While blanked the buffer is still redrawn (so `/shot.bmp` keeps mirroring the
  frame) but never transferred, and the transfer precedes the power-on so
  waking cannot flash stale GDDRAM.
- **Toast precedence.** A toast is a full screen, not an overlay, and outranks
  the boot screen, the tape sequence, the screensaver and the current mode.
  It also cancels any running effect: key feedback is never animated in.

After the screen has drawn, `fx::apply()` rewrites the buffer if an effect is
running. The wipe and the roll need the previous frame; `fx::start()` copies it
out of the U8g2 buffer, which still holds the last frame sent because nothing
clears it until the next draw. That costs 2 KB of RAM for two scratch frames.
With effects on, going dark at night plays the collapse first and powers the
panel down when it ends; waking plays the expand.

### Scripts: the PET terminal

The BASIC screen, the tape sequence and the screensaver share one
`term::Screen` (16x8 cells, word wrap, scroll, upper case) driven by one
`term::Script`. A script is a queue of steps: `type` (at ~14 characters a
second, then a beat and RETURN), `out` (print a line at once, as program
output), `wait`, and `call`. A `call` step runs on the frame *after* the one
that reached it, so whatever the script printed just before is already on the
glass when the callback blocks — the tape's `LOADING` line, then the GET.
Callbacks can queue further steps; the tape's fetch callback queues the
`RUN` and the reading it prints.

Only one of the three owns the terminal at a time: the screensaver never
starts during a tape or on the BASIC screen, and a tape over the BASIC screen
plays inline on its terminal instead of clearing it. The LIST screen uses a
second `Screen` of its own, rebuilt every frame.

### Adding a screen

1. Add a value to `ui::Mode` before `COUNT`. The values are stored (the
   start mode, the screen mask), so never renumber the existing ones.
2. Bump `settings::SCREENS` to match — a `static_assert` in `ui.cpp` insists
   — and add the name to the `SCREENS` list in the web page's script.
3. Put it where it belongs in the `ORDER` table in `ui.cpp`; the centre key
   walks that table, skipping screens that are switched off.
4. Write `draw_yourthing()` in the anonymous namespace of `ui.cpp`, using
   `at/centred/right/hline` (or `pet::text` / `pet::glyph` at `ox`, `oy`) so it
   inherits the burn-in shift.
5. Add a `case` to the switch at the bottom of `ui::tick()`.

A `settings::retro().screens` stored before your change has the new bit
clear. Bump `RETRO_VERSION` and, in `settings::begin()`, load the previous
version and set the bit — the layout is the same, so nothing else is lost.
That is how the v1 → v2 step added HOME.

If it needs data from the network, give it a module of its own with the same
shape as `weather`: a `tick()` that decides when to fetch, a snapshot getter,
and a `Status` enum so the screen can say *why* it is empty rather than just
showing nothing.

---

## Settings and persistence

The `Config` struct is stored as a single `{magic, version, cfg, crc}` blob under
the `petpc/cfg` key in the ESP32's NVS partition; the retro options and the
Home Assistant connection are blobs of the same shape under `petpc/retro` and
`petpc/ha`. A missing key, a size mismatch,
a wrong magic, a bumped `VERSION` or a failed CRC all fall back to seeded
defaults instead of reinterpreting stale bytes — for that blob only.

That is why the retro options are not fields of `Config`: growing `Config`
changes its size, and a size mismatch resets it to defaults, WiFi login and
all, on every device that takes the update. A new blob is seeded on its own
and leaves the existing one alone.

| Key | Struct | Magic | Version | History |
|-----|--------|-------|---------|---------|
| `petpc/cfg`   | `Config` | `PETP` | 1 | unchanged since 1.0 |
| `petpc/retro` | `Retro`  | `PETR` | 2 | v1 (1.1.0) → v2 (1.2.0): same layout, HOME bit set in `screens` |
| `petpc/ha`    | `Ha`     | `PETH` | 2 | v1 (1.2.0) → v2 (1.2.1): `type[]` appended, filled from each entity's domain |
| `petpc/keys`  | `Keys`   | `PETK` | 1 | new in 1.3.0: seeded from `Config`'s old desk presets (press = POST to them), or previous/next screen on a fresh device |

`Config` still carries the 1.x desk presets (`desk_url`, `desk_label`), only
to seed `keys` once — removing them would change `Config`'s size and reset it.
They are no longer in the JSON.

A migration is a second `load()` of the previous version, into the previous
struct when the layout changed (`HaV1` in `settings.cpp`), then a copy.
`begin()` ends with `save()`, which writes the blob back at the new version.
When a layout changes again, keep the old struct and add a step rather than
bumping the version bare — a bare bump throws the HA token away.

```
begin()  → per blob: prefs.getBytes → validate, or seed defaults → save()
save()   → per blob: clamp → recompute CRC → compare with stored blob → putBytes if different
```

`save()` is safe to call liberally: it reads the stored blob back and skips the
write when nothing changed, so an unchanged image costs no flash write. The
screen the centre key is on is not persisted at all: the box always comes up
on the configured start screen (`display.start_mode`, Block clock by
default), so a key press never costs a flash write.

`apply_json()` / `to_json()` are the whole REST surface for configuration. The
WiFi password is write-only: `to_json()` exposes a `pass_set` boolean instead of
the value, and an empty `pass` in a patch means "leave it alone", so the web UI
can round-trip settings without ever seeing or clobbering the password. The HA
token works the same way (`token_set`), with one addition: a patch that
changes `ha.url` without a new token clears the stored one, so the token is
never sent to a host nobody entered it for. A patch only touches the keys it
contains — `{"display":{"start_mode":3}}` changes the start screen and
nothing else — which is what the web page relies on: it sends only the fields
that changed. Blank `wifi.hostname`, `loc.tz`, `loc.ntp` and `weather.host`
are ignored, so no client can blank the settings the box cannot run without.

---

## Network lifecycle

```
setup()
 ├─ try_sta()        25 s, with the attempt counter on screen
 │   └─ fail → start_ap()   "PET-PC-XXXX", IP shown on screen
 ├─ MDNS.begin(hostname)    (STA only)
 ├─ setup_ota()             ArduinoOTA, progress drawn as a toast
 ├─ web::begin()            port 80
 ├─ timekeeper::begin()     configTime(tz, ntp, ...)
 ├─ weather::begin()
 ├─ ha::begin()             reset readings; nothing is fetched yet
 └─ set_mode(start_mode)    after 1.5 s on the boot screen; the first
                            weather fetch (with its tape) follows at once
```

`WiFi.persistent(false)` — the credentials of record are the ones in the NVS
image, and letting the SDK keep its own copy is a good way to end up with two
disagreeing sources of truth. `WiFi.setSleep(false)` because modem sleep adds
latency to every inbound request; this box is mains powered. The hostname is set
*before* `WiFi.mode()` — on the ESP32 core, setting it afterwards is ignored
until the next reconnect.

OTA uses `ArduinoOTA` with its per-chunk timeout raised from the core's 1 s
to 10 s (`setTimeout(10000)`). With the 1 s default, the board gave up after
three nudges whenever TCP paused to retransmit — routine at the −80 dBm the
board sees inside the case — and espota on the host then hung forever at 0 %.

The watchdog in `loop()` exists because `WiFi.setAutoReconnect(true)` is not
always enough: an association can drop and stay down silently. If the status has
been non-connected for 30 s, it calls `WiFi.reconnect()` once, then waits
another 30 s. It only runs in STA mode. A box that fell back to the hotspot at
boot is handled by `ap_retry()` instead: with an SSID configured and nobody
joined to the hotspot for five minutes, it restarts to try the home network
again — restarting is simpler and more reliable than juggling AP+STA mode, and
never cuts off someone who is fixing the settings over the hotspot.

Everything that polls checks `WiFi.status()` itself and degrades instead of
failing: `weather` keeps the last good reading and lets `data_age_s()` tell the
UI it has gone stale; a key action says "no wifi" on its toast;
`ha` keeps the last values and reports `NoWifi`, `Unreachable` or `AuthError`
for the HOME screen's footer, pausing a minute after the last two.

`ha` keeps its `HTTPClient` and `WiFiClient`/`WiFiClientSecure` across
requests with `setReuse(true)`, so polling rides one keep-alive connection
(one TLS handshake, not one per read). That is also why it reads the body of
every response, even one it does not need: an unread body stops the
connection being reused.

---

## The configuration page

`web_page.h` is the whole page — no filesystem, no external resources (it has
to work over the hotspot, with no internet). `send_P()` streams it from flash.

The form is built by the script from a schema: each `field()` call names a
tab, a JSON path in `/api/state`, a label, help text and its checks, and the
same object then fills itself from the state, reports whether it changed,
validates itself and contributes its path to the patch. Compound editors (the
screen mask, the HA entity table) register with `custom()` and supply
`get`/`set`/`check`. Adding a setting is one `field()` line plus the firmware
side in `settings.cpp`.

Save is disabled until `/api/state` has loaded, then sends only changed
fields; secrets (WiFi password, HA token) count as changed only when typed.
The CRT look — scanlines, glow, vignette, power-on, tab wipe, cursor — is CSS
only, off with the **CRT FX** button or `prefers-reduced-motion`.

To test it without the device, serve the page's HTML (the string between
`R"HTML(` and `)HTML"`) next to a mock of `/api/state`; everything else is
in the page.

---

## Screen capture

`/shot.bmp` converts the live U8g2 buffer into a 1-bit BMP. The two layouts
disagree in both directions, which is the entire trick:

| | U8g2 buffer | BMP |
|---|---|---|
| Byte covers | 8 pixels **vertically** | 8 pixels **horizontally** |
| Bit 0 is | the **top** pixel | the **rightmost** pixel |
| First row stored | top | bottom |

So `display::to_bmp()` walks rows top-down, reads
`buf[(y / 8) * 128 + x] & (1 << (y % 8))`, and writes it into
`row[x / 8] |= 0x80 >> (x % 8)` of the row at `(63 - y)`. At 128 px wide a BMP
row is exactly 16 bytes, so the usual 4-byte row padding is a no-op here.

The response is streamed with an explicit `setContentLength()` followed by a
raw `client().write()`, so the 1078-byte image never has to exist as a `String`.

---

## ESP32-C6 porting notes

PET-PC started as a port of the ESP8266 firmware in mini-esp-pc. These are the
differences that were not just renamed headers, several of which failed
silently. They are worth knowing before touching the network code.

| Area | ESP8266 behaviour | ESP32-C6 (Arduino core 3.x) | Where |
|------|-------------------|------------------------------|-------|
| Empty POST | `POST("")` sends `Content-Length: 0` | header **omitted** for an empty body; ESPHome rejects the request | `actions.cpp` adds it explicitly |
| OTA | ESP8266 OTA tolerates stalls | 1 s per-chunk timeout, then silent abort; espota hangs | `ArduinoOTA.setTimeout(10000)` |
| Timezone | `configTime(tz, ntp…)` | `configTzTime(tz, ntp…)` | `timekeeper.cpp` |
| Hostname | `WiFi.hostname()` any time | `WiFi.setHostname()` **before** `WiFi.mode()` | `main.cpp` |
| Modem sleep | `WIFI_NONE_SLEEP` | `WiFi.setSleep(false)` | `main.cpp` |
| mDNS | `MDNS.update()` every loop | runs in its own task; no update call | `main.cpp` |
| Settings | emulated EEPROM + `commit()` | NVS via `Preferences`, one blob per struct | `settings.cpp` |
| Reset reason | `ESP.getResetReason()` | `esp_reset_reason()` | `main.cpp` |
| Serial | UART adapter | native USB Serial/JTAG (`ARDUINO_USB_CDC_ON_BOOT=1`) | `platformio.ini` |
| Toolchain | stock `espressif8266` | **pioarduino** platform, needs PlatformIO Core ≥ 6.2 | `platformio.ini` |

