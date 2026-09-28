// ─────────────────────────────────────────────────────────────────────────────
// Hardware pin map — Waveshare ESP32-C6-Zero. Nothing else in the tree
// hard-codes a GPIO number. See WIRING.md for the physical wiring.
//
// Every signal sits on the two edge headers (no bottom pads needed), and the
// pins below deliberately avoid the C6's special-purpose ones:
//   GPIO4, 5, 8, 9, 15  strapping pins (8 also drives the onboard WS2812,
//                        9 is the BOOT button)
//   GPIO12, 13          native USB D-/D+ — the flashing/serial port
//   GPIO16, 17          UART0 TX/RX, kept free for a fallback console
//
// The OLED lands on four *consecutive* header pins — GND, 3V3, GP0, GP1 — in
// the same order as the usual SSD1306 module (GND VCC SCL SDA), so one
// straight 4-wire ribbon connects it.
//
// GPIO0/1 double as the 32 kHz crystal pins on the bare chip; the Zero fits
// no crystal, so they are ordinary GPIOs here.
// ─────────────────────────────────────────────────────────────────────────────
#pragma once

#include <Arduino.h>

namespace board {

// ── SSD1306 128x64, I²C ──────────────────────────────────────────────────────
constexpr uint8_t I2C_SCL   = 0;      // GP0
constexpr uint8_t I2C_SDA   = 1;      // GP1
constexpr uint8_t OLED_ADDR = 0x3C;   // 7-bit
constexpr uint8_t OLED_W    = 128;
constexpr uint8_t OLED_H    = 64;

// ── Key switches (one leg to GPIO, other to GND; internal pull-up →
//    pressed reads LOW) ───────────────────────────────────────────────────────
constexpr uint8_t BTN_LEFT   = 18;    // GP18 — desk preset A
constexpr uint8_t BTN_CENTER = 19;    // GP19 — mode switch
constexpr uint8_t BTN_RIGHT  = 20;    // GP20 — desk preset B

// ── Onboard WS2812 RGB LED (not used yet — free status light for later) ─────
constexpr uint8_t RGB_LED = 8;

}  // namespace board
