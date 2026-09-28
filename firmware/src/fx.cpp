#include "fx.h"
#include "board_config.h"
#include "display.h"

namespace fx {
namespace {

constexpr int    W     = board::OLED_W;
constexpr int    H     = board::OLED_H;
constexpr size_t BYTES = W * H / 8;

// U8g2's full buffer: 8 vertically stacked pixels per byte, LSB on top,
// pages of 128 bytes. Two scratch frames cost 2 KB of RAM.
uint8_t old_frame[BYTES];
uint8_t out[BYTES];

Kind     cur = Kind::None;
uint32_t t0  = 0;

uint16_t duration(Kind k) {
    switch (k) {
        case Kind::Wipe:     return 360;
        case Kind::Roll:     return 520;
        case Kind::Collapse: return 480;
        case Kind::Expand:   return 360;
        default:             return 1;
    }
}

inline bool px(const uint8_t* b, int x, int y) { return b[(y >> 3) * W + x] & (1 << (y & 7)); }
inline void set(uint8_t* b, int x, int y)      { b[(y >> 3) * W + x] |= 1 << (y & 7); }

// Copy source row sy into destination row dy. `dst` starts out cleared, so
// only the lit pixels need writing.
void copy_row(uint8_t* dst, int dy, const uint8_t* src, int sy) {
    if (dy < 0 || dy >= H || sy < 0 || sy >= H) return;
    for (int x = 0; x < W; x++)
        if (px(src, x, sy)) set(dst, x, dy);
}

void hline(uint8_t* b, int y, int x0, int x1) {
    if (y < 0 || y >= H) return;
    if (x0 < 0) x0 = 0;
    if (x1 > W) x1 = W;
    for (int x = x0; x < x1; x++) set(b, x, y);
}

// Squeeze `src` into `h` rows centred on the screen's middle line.
void squeeze(uint8_t* dst, const uint8_t* src, int h) {
    int top = (H - h) / 2;
    for (int y = 0; y < h; y++) copy_row(dst, top + y, src, y * H / h);
}

float ease_out(float t) { return 1 - (1 - t) * (1 - t); }
float ease_in(float t)  { return t * t; }

}  // namespace

void start(Kind k) {
    memcpy(old_frame, display::u8g2().getBufferPtr(), BYTES);
    cur = k;
    t0  = millis();
}

void cancel()  { cur = Kind::None; }
bool active()  { return cur != Kind::None; }
Kind kind()    { return cur; }

void apply() {
    if (cur == Kind::None) return;
    uint8_t* buf = display::u8g2().getBufferPtr();

    float t = (millis() - t0) / (float)duration(cur);
    if (t >= 1) {
        if (cur == Kind::Collapse) memset(buf, 0, BYTES);   // stays dark until power-off
        cur = Kind::None;
        return;
    }

    memset(out, 0, BYTES);
    switch (cur) {
        case Kind::Wipe: {
            int edge = (int)(t * H);
            for (int y = 0; y < H; y++) copy_row(out, y, y < edge ? buf : old_frame, y);
            hline(out, edge, 0, W);                          // the beam
            break;
        }
        case Kind::Roll: {
            // `s` rows of the new frame have come up from the bottom. The
            // first few rows behind the old frame stay black — the vertical
            // blanking bar a rolling CRT shows between two pictures.
            int s    = (int)(ease_out(t) * H);
            int seam = H - s;
            for (int y = 0; y < seam; y++) copy_row(out, y, old_frame, y + s);
            for (int y = seam + 3; y < H; y++) copy_row(out, y, buf, y - seam);
            break;
        }
        case Kind::Collapse: {
            constexpr float SPLIT = 0.6f;       // first squeeze, then shrink the line
            if (t < SPLIT) {
                int h = (int)(H * (1 - ease_in(t / SPLIT)));
                if (h < 1) h = 1;
                squeeze(out, buf, h);
                if (h <= 3) hline(out, H / 2, 0, W);
            } else {
                float u = (t - SPLIT) / (1 - SPLIT);
                int   w = (int)(W * (1 - ease_out(u)));
                if (w < 2) w = 2;                             // the last dot
                hline(out, H / 2, (W - w) / 2, (W + w) / 2);
            }
            break;
        }
        case Kind::Expand: {
            constexpr float SPLIT = 0.35f;
            if (t < SPLIT) {
                int w = (int)(W * ease_out(t / SPLIT));
                if (w < 2) w = 2;
                hline(out, H / 2, (W - w) / 2, (W + w) / 2);
            } else {
                int h = (int)(H * ease_out((t - SPLIT) / (1 - SPLIT)));
                if (h < 1) h = 1;
                squeeze(out, buf, h);
            }
            break;
        }
        default:
            break;
    }
    memcpy(buf, out, BYTES);
}

}  // namespace fx
