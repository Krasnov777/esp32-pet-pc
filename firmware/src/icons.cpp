#include "icons.h"
#include "display.h"

namespace icons {
namespace {

// A cloud occupying (x .. x+13, y .. y+8) — two lobes over a flat base.
void cloud(U8G2& g, int x, int y, float s) {
    g.drawDisc(x + (int)(4 * s), y + (int)(5 * s), (int)(3 * s));
    g.drawDisc(x + (int)(9 * s), y + (int)(4 * s), (int)(4 * s));
    g.drawBox(x + (int)(2 * s), y + (int)(5 * s), (int)(12 * s), (int)(4 * s));
}

// Sun disc + eight rays, centred on (cx, cy).
void sun(U8G2& g, int cx, int cy, int r) {
    g.drawDisc(cx, cy, r);
    const int8_t dx[8] = {0, 0, 1, -1, 1, 1, -1, -1};
    const int8_t dy[8] = {1, -1, 0, 0, 1, -1, 1, -1};
    for (uint8_t i = 0; i < 8; i++) {
        int x0 = cx + dx[i] * (r + 2), y0 = cy + dy[i] * (r + 2);
        int x1 = cx + dx[i] * (r + 3), y1 = cy + dy[i] * (r + 3);
        g.drawLine(x0, y0, x1, y1);
    }
}

// Crescent: a disc with a second, offset disc punched back out of it.
void moon(U8G2& g, int cx, int cy, int r) {
    g.drawDisc(cx, cy, r);
    g.setDrawColor(0);
    g.drawDisc(cx + r / 2 + 1, cy - r / 3, r);
    g.setDrawColor(1);
}

void drops(U8G2& g, int x, int y, float s) {
    for (uint8_t i = 0; i < 3; i++) {
        int dx = x + (int)((3 + i * 4) * s);
        g.drawLine(dx + (int)(2 * s), y, dx, y + (int)(4 * s));
    }
}

void flakes(U8G2& g, int x, int y, float s) {
    for (uint8_t i = 0; i < 3; i++) {
        int cx = x + (int)((4 + i * 4) * s), cy = y + (int)(2 * s);
        g.drawPixel(cx, cy);
        g.drawLine(cx - 1, cy, cx + 1, cy);
        g.drawLine(cx, cy - 1, cx, cy + 1);
    }
}

void bolt(U8G2& g, int x, int y, float s) {
    int x0 = x + (int)(8 * s), y0 = y;
    g.drawTriangle(x0, y0,
                   x0 - (int)(4 * s), y0 + (int)(4 * s),
                   x0 - (int)(1 * s), y0 + (int)(3 * s));
    g.drawTriangle(x0 - (int)(1 * s), y0 + (int)(3 * s),
                   x0 + (int)(2 * s), y0 + (int)(3 * s),
                   x0 - (int)(3 * s), y0 + (int)(8 * s));
}

}  // namespace

void weather_icon(int x, int y, weather::Icon ic, uint8_t size) {
    U8G2& g = display::u8g2();
    float s = size / 16.0f;            // all geometry below is authored at 16 px

    switch (ic) {
        case weather::Icon::Sun:
            sun(g, x + size / 2, y + size / 2, (int)(4 * s));
            break;
        case weather::Icon::Moon:
            moon(g, x + size / 2, y + size / 2, (int)(6 * s));
            break;
        case weather::Icon::PartlySun:
            sun(g, x + (int)(11 * s), y + (int)(4 * s), (int)(2 * s));
            cloud(g, x, y + (int)(6 * s), s);
            break;
        case weather::Icon::PartlyMoon:
            moon(g, x + (int)(11 * s), y + (int)(4 * s), (int)(3 * s));
            cloud(g, x, y + (int)(6 * s), s);
            break;
        case weather::Icon::Cloud:
            cloud(g, x, y + (int)(4 * s), s);
            break;
        case weather::Icon::Rain:
            cloud(g, x, y, s);
            drops(g, x, y + (int)(11 * s), s);
            break;
        case weather::Icon::Snow:
            cloud(g, x, y, s);
            flakes(g, x, y + (int)(11 * s), s);
            break;
        case weather::Icon::Thunder:
            cloud(g, x, y, s);
            bolt(g, x, y + (int)(9 * s), s);
            break;
        case weather::Icon::Fog:
            cloud(g, x, y, s);
            for (uint8_t i = 0; i < 3; i++)
                g.drawHLine(x + (int)((1 + (i & 1) * 3) * s),
                            y + (int)((11 + i * 2) * s),
                            (int)(12 * s) - (int)((i & 1) * 4 * s));
            break;
    }
}

void wifi_bars(int x, int y, int32_t rssi, bool connected) {
    U8G2& g = display::u8g2();
    if (!connected) {
        g.drawFrame(x + 2, y + 1, 8, 7);
        g.drawLine(x + 2, y + 1, x + 9, y + 7);
        return;
    }
    // -90 dBm → 0 bars, -50 dBm and better → 4.
    int bars = (rssi + 95) / 11;
    if (bars < 1) bars = 1;
    if (bars > 4) bars = 4;
    for (int i = 0; i < 4; i++) {
        int h = 2 + i * 2;
        if (i < bars) g.drawBox(x + i * 3, y + 8 - h, 2, h);
        else          g.drawPixel(x + i * 3, y + 7);
    }
}

}  // namespace icons
