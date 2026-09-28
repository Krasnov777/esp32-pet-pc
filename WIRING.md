# Wiring — ESP32-C6-Zero build

How the Waveshare **ESP32-C6-Zero**, the SSD1306 OLED and three MX-style key
switches connect. That's the whole build — no buzzer, no extra parts.

The firmware side of this table is
[`firmware/src/board_config.h`](firmware/src/board_config.h) — if you wire
anything differently, change it there and nowhere else.

---

## Pin map

| Signal | C6-Zero pin | Header | Goes to |
|--------|-------------|--------|---------|
| GND        | GND  | left, pin 2 | common ground bus (everything) |
| 3.3 V      | 3V3  | left, pin 3 | OLED VCC |
| I²C SCL    | GP0  | left, pin 4 | OLED SCL |
| I²C SDA    | GP1  | left, pin 5 | OLED SDA |
| Left key   | GP18 | right, pin 5 | switch leg A (leg B → GND) |
| Centre key | GP19 | right, pin 6 | switch leg A (leg B → GND) |
| Right key  | GP20 | right, pin 7 | switch leg A (leg B → GND) |

*Header pins counted from the USB-C end.*

Free for later features: **GP2, GP3** (ADC-capable, e.g. a light sensor or a
battery monitor), **GP14, GP21, GP22**, and **GP23** on the bottom pads. The onboard
**RGB LED on GP8** is also free to use as a status light.

Pins deliberately **not** used: GP4/5/8/9/15 (strapping pins — the wrong
level at reset stops the board booting), GP12/13 (the USB port you flash
through), and TX/RX (GP16/17), which are kept for a serial console.

---

## Diagram

```
                         ┌─────[ USB-C ]─────┐
                    5V ──┤ 5V            TX  ├── (free, UART console)
      ┌──────────── GND ─┤ GND           RX  ├── (free, UART console)
      │  ┌──────── 3V3 ──┤ 3V3          GP14 ├── (free)
      │  │  ┌───── GP0 ──┤ GP0          GP15 ├── (don't use — strapping)
      │  │  │  ┌── GP1 ──┤ GP1          GP18 ├── LEFT key   leg 1
      │  │  │  │         ┤ GP2          GP19 ├── CENTRE key leg 1
      │  │  │  │         ┤ GP3          GP20 ├── RIGHT key  leg 1
      │  │  │  │         ┤ GP4          GP21 ├── (free)
      │  │  │  │         ┤ GP5          GP22 ├── (free)
      │  │  │  │         └─── ESP32-C6-Zero ───┘
      │  │  │  │
      │  │  │  │   ┌────────────────┐        ┌─────┐ ┌─────┐ ┌─────┐
      ├──┼──┼──┼───┤ GND            │        │LEFT │ │CENTR│ │RIGHT│  key switches
      │  └──┼──┼───┤ VCC   SSD1306  │        └──┬──┘ └──┬──┘ └──┬──┘  (leg 2 of each)
      │     └──┼───┤ SCL   128x64   │           │       │       │
      │        └───┤ SDA            │           │       │       │
      │            └────────────────┘           │       │       │
      └─────────────────── GND bus ─────────────┴───────┴───────┘
```

The OLED's four pins land on **four consecutive header pins** — GND, 3V3, GP0,
GP1 — in the same order most SSD1306 modules print them (GND VCC SCL SDA). A
straight 4-wire ribbon or Dupont strip goes across with no crossed wires.
**Check your module's silkscreen first:** some print VCC GND, or SDA before
SCL. If yours differs, cross the wires to match, or swap `I2C_SCL`/`I2C_SDA` in
`board_config.h`.

---

## The one-GND problem

The C6-Zero breaks out **only one GND pin**, and four things need ground (the
OLED and three switches). Build a small ground bus:

- **Switches:** run one bare wire (or a length of solid-core with the
  insulation stripped) across the second leg of all three switches, soldering
  it to each. That daisy-chain is the key ground — one wire leaves it.
- Join the key-ground wire and the OLED GND at one point: a solder joint
  heat-shrunk in the case, or a small Wago 221 lever connector. Then run
  **one** wire to the C6 GND pin. (Or run the chain's end straight to the GND
  pin and splice the OLED's GND into it — either works.)

---

## Key switches (MX-style)

A mechanical keyboard switch is just a normally-open contact with two metal
legs. It has **no polarity**, and with only three keys there is no matrix, so
**no diodes** either.

```
     ┌─────────────┐   (switch seen from underneath)
     │   ●     ●   │   ● = metal legs — either one to GPIO, the other to GND
     │      ◯      │   ◯ = centre plastic post (and 2 side posts on 5-pin)
     └─────────────┘
```

- **Wiring:** leg 1 → its GPIO (GP18/19/20), leg 2 → the ground bus. The
  firmware turns on the C6's internal pull-up, so the pin reads HIGH when
  released and LOW when pressed. No external resistors needed.
- **Debounce:** MX contacts bounce for about 5 ms. The firmware waits out 25 ms
  per edge, so no RC filter is needed.
- **Mounting in the PET case:** the standard MX plate cutout is a
  **14.0 × 14.0 mm** square in a **1.5 mm**-thick plate, so the switch clips
  snap in. 3D-printed plates usually need 14.0–14.1 mm and a 1.5 mm lip at
  the cutout, even if the rest of the panel is thicker. Key spacing is
  19.05 mm (1u) centre to centre.
- **Soldering vs. sockets:** solder wires straight to the legs (tin the leg
  first, and keep the iron brief — the plastic housing softens), or press the
  switches into **Kailh hot-swap sockets** glued into the printed case and
  solder the wires to the sockets. Sockets let you swap switch types later
  without touching the iron.
- **Switch feel:** with no buzzer, the switch itself is the only feedback that
  a press registered, so **clicky (blue)** or **tactile (brown)** switches
  suit this build better than linears — and a clicky switch fits the retro
  look.
- **Switches with an LED slot:** the firmware doesn't drive key LEDs yet. If
  you want lit keys later, wire each LED's anode through a ~330 Ω resistor to
  a free GPIO (GP2/3/14/21/22) and the cathode to GND. That's a later feature, so
  just leave the LED holes empty for now.

---

## OLED (SSD1306, I²C)

- **Power it from 3V3, not 5V.** Most modules have a regulator and happily
  take 5 V, but their SDA/SCL pull-up resistors go to VCC. At 5 V the I²C
  lines would sit at 5 V, and **the ESP32-C6's pins are not 5 V tolerant.**
- Keep the SCL/SDA wires under ~20 cm. The module's own 4.7–10 kΩ pull-ups
  are enough, so no extra resistors are needed.
- Address 0x3C (the usual one). If the screen stays black and the serial log
  shows the rest of the firmware running, the module may be strapped to 0x3D —
  change `OLED_ADDR` in `board_config.h`.
- If you ever swap in a **1.3" SH1106** panel for a bigger PET screen, it
  looks identical but needs a different U8g2 constructor in `display.cpp`
  (`U8G2_SH1106_128X64_NONAME_F_HW_I2C`). Same wiring.

---

## Power

The C6-Zero runs from its USB-C port, with 5 V in and its onboard regulator
making the 3V3 rail. The OLED draws ~20 mA, well within what the 3V3 pin
can supply. For the PET case, route the USB-C port
to the back panel. A short USB-C panel-mount extension keeps the board
itself inside.

---

## Fitting it into the PET case

- **USB-C access** — it's also the flashing port. After the first flash,
  updates go over WiFi (OTA), but leave a way to reach the port for recovery.
- **RST and BOOT buttons** sit next to the USB-C connector. A pin-sized hole
  in the case lets you reach BOOT for a recovery flash without opening the
  shell.
- **RGB LED** (GP8, next to the buttons): a light pipe or clear filament
  window to the front panel would make a nice "power" lamp for a later
  firmware feature.
- **WiFi antenna** — the ceramic antenna is at the far end from USB-C. Keep
  it away from metal (screws, a metal badge, the ground-bus wire) and don't
  bury it behind the OLED PCB.

---

## First power-on checklist

1. **Before connecting the OLED**, flash the firmware over USB-C and open
   `pio device monitor`. You should see `[boot] pet-pc <version>` (the
   `FW_VERSION` in `platformio.ini`) and the WiFi log.
2. Power off, connect the OLED, power on. It should show the boot screen.
3. Press each key and watch the log/screen. Centre should cycle screens, left
   and right should show the desk toast. If a key does nothing, check its GND
   leg first — the daisy-chain is the usual suspect.
4. Browse to `http://pet-pc.local/`. The first boot on the new board
   starts from defaults — set location, desk URLs and display preferences
   there. (Settings are not shared with the old mini-esp-pc box.)
