// SSD1306 panel ownership: I²C bring-up, the U8g2 full-frame buffer, contrast.
//
// Full-buffer mode costs 1 KB of RAM (128x64/8) and is worth it here: every
// screen is drawn from scratch each frame and pushed in one transfer, so there
// is never a half-updated frame on the glass.
#pragma once

#include <Arduino.h>
#include <U8g2lib.h>

namespace display {

void begin();

// The shared draw target. Screens draw into the buffer; ui::tick() flushes.
U8G2& u8g2();

// SSD1306 contrast register (0..255) — used for the night-dim schedule.
void set_contrast(uint8_t value);
uint8_t contrast();

// Panel power (SSD1306 0xAE/0xAF, via U8g2's power-save). Off means the glass
// is dark and not one pixel is ageing, which is what the night-blank window in
// ui.cpp uses. Switching back on invalidates the cached contrast, so the next
// set_contrast() is guaranteed to reach the controller.
void set_power(bool on);
bool powered();

// 1-bit BMP of the current frame, for the web UI's live screen mirror.
// Writes exactly bmp_size() bytes into `out`.
size_t bmp_size();
void   to_bmp(uint8_t* out);

}  // namespace display
