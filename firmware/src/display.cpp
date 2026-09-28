#include "display.h"
#include "board_config.h"

#include <Wire.h>

namespace display {
namespace {

// HW-I²C, full frame buffer. Pin order in this constructor is (rotation,
// reset, clock, data) — not the usual SDA-first order.
U8G2_SSD1306_128X64_NONAME_F_HW_I2C dev(U8G2_R0, U8X8_PIN_NONE,
                                        board::I2C_SCL, board::I2C_SDA);

// Tracked so set_contrast() can skip redundant I²C writes — `known` guards the
// case where the wanted value happens to equal this initial one.
uint8_t current_contrast = 0;
bool    contrast_known   = false;

// begin() leaves the panel displaying, so this starts out true.
bool panel_on = true;

// 1 bpp BMP: 14-byte file header + 40-byte DIB header + 2-entry palette.
constexpr size_t HEADER_BYTES = 14 + 40 + 8;
constexpr size_t ROW_BYTES    = board::OLED_W / 8;          // 16 — already 4-aligned
constexpr size_t PIXEL_BYTES  = ROW_BYTES * board::OLED_H;

void put16(uint8_t* p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
void put32(uint8_t* p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

}  // namespace

void begin() {
    Wire.begin(board::I2C_SDA, board::I2C_SCL);
    dev.setI2CAddress(board::OLED_ADDR << 1);   // U8g2 wants the 8-bit address
    dev.setBusClock(400000);
    dev.begin();
    dev.enableUTF8Print();
    dev.clearBuffer();
    dev.sendBuffer();
}

U8G2& u8g2() { return dev; }

void set_contrast(uint8_t value) {
    if (contrast_known && value == current_contrast) return;
    current_contrast = value;
    contrast_known   = true;
    dev.setContrast(value);
}

uint8_t contrast() { return current_contrast; }

void set_power(bool on) {
    if (on == panel_on) return;
    panel_on = on;
    dev.setPowerSave(on ? 0 : 1);
    // U8g2 re-runs the SSD1306 display-on sequence (charge pump included) on
    // the way back up, so the contrast register cannot be assumed to have
    // survived — drop the cache and let the next set_contrast() write it.
    if (on) contrast_known = false;
}

bool powered() { return panel_on; }

size_t bmp_size() { return HEADER_BYTES + PIXEL_BYTES; }

void to_bmp(uint8_t* out) {
    memset(out, 0, bmp_size());

    out[0] = 'B'; out[1] = 'M';
    put32(out + 2, bmp_size());
    put32(out + 10, HEADER_BYTES);          // pixel data offset

    uint8_t* dib = out + 14;
    put32(dib + 0, 40);                     // BITMAPINFOHEADER
    put32(dib + 4, board::OLED_W);
    put32(dib + 8, board::OLED_H);          // positive → rows stored bottom-up
    put16(dib + 12, 1);                     // planes
    put16(dib + 14, 1);                     // bits per pixel
    put32(dib + 20, PIXEL_BYTES);
    put32(dib + 32, 2);                     // colours used

    uint8_t* pal = out + 54;                // index 0 = black, index 1 = white
    pal[4] = pal[5] = pal[6] = 0xFF;

    // U8g2 buffer layout: 8 vertically-stacked pixels per byte, LSB topmost,
    // pages laid out row-major. BMP wants MSB-leftmost, bottom row first.
    const uint8_t* buf = dev.getBufferPtr();
    uint8_t* px = out + HEADER_BYTES;
    for (uint8_t y = 0; y < board::OLED_H; y++) {
        const uint8_t* src = buf + (y / 8) * board::OLED_W;
        uint8_t  mask = 1 << (y % 8);
        uint8_t* row  = px + (board::OLED_H - 1 - y) * ROW_BYTES;
        for (uint8_t x = 0; x < board::OLED_W; x++)
            if (src[x] & mask) row[x / 8] |= 0x80 >> (x % 8);
    }
}

}  // namespace display
