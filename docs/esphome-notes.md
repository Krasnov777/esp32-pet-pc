# From an ESPHome config to firmware

PET-PC's first life was an ESPHome YAML config. The YAML worked, but a few things in it were either bugs or fought the
platform. They are worth recording, because the same traps apply to any
ESPHome config with buttons and a display:

1. **Inverted buttons.** The pins were `INPUT_PULLUP` with no `inverted: true`,
   so the binary sensors read *on* while the buttons were *released*, and
   `on_press:` actually fired on release. Here `pressed == LOW` is explicit and
   every edge is debounced (25 ms).
2. **Dropped presses.** Each button ran a script containing `delay: 200ms` for
   the buzzer. ESPHome scripts default to `mode: single`, so any press arriving
   during those 200 ms was silently discarded — and the delay stalled the rest
   of the automation. (mini-esp-pc replaced it with a non-blocking beep queue;
   PET-PC has no buzzer at all.)
3. **The idle interval never stopped.** The 1 s `interval:` called
   `show_page(page_idle)` + `update()` *every second* forever once the timeout
   elapsed, redrawing an unchanged screen over I²C indefinitely. This firmware
   redraws at 5 fps from a single full frame buffer, which is both simpler and
   cheaper than page bookkeeping.
4. **Nothing was persisted.** Mode, location and endpoints were compile-time
   YAML. They are now settings in a CRC-checked NVS image, editable from a
   browser.
5. **Burn-in.** A static clock on an OLED for months is a burn-in risk the
   ESPHome version had no answer for. The panel runs at a modest day contrast
   (70, not the 200 it shipped with — ageing scales with drive current), the
   whole frame is nudged by up to 2 px every two minutes, and the night
   schedule switches the glass off entirely rather than dimming it. A press
   wakes it dim for ten seconds. Roughly a third of the hours in a day are
   removed from the panel's clock that way, and dark pixels do not age at all.

Home Assistant no longer sees this device — the native API is gone with the
YAML. The link now runs the other way: PET-PC reads and toggles HA entities
over HA's REST API (see [Home Assistant](../README.md#home-assistant)), but HA has no
entity for PET-PC itself. If that matters later, MQTT discovery is the cheap
way to add one.
