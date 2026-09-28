#include "pet.h"
#include "display.h"

namespace pet {

void font() { display::u8g2().setFont(u8g2_font_pxplusibmcgathin_8r); }

void glyph(int x, int y, uint8_t g) {
    U8G2& d = display::u8g2();
    switch (g) {
        case DIAG_DN:           // two pixels thick, so the maze reads on a small panel
            for (int i = 0; i < CELL; i++) d.drawHLine(x + i, y + i, i < CELL - 1 ? 2 : 1);
            break;
        case DIAG_UP:
            for (int i = 0; i < CELL; i++) d.drawHLine(x + i, y + CELL - 1 - i, i < CELL - 1 ? 2 : 1);
            break;
        case FULL:
            d.drawBox(x, y, CELL, CELL);
            break;
        // The triangles are filled row by row rather than with drawTriangle(),
        // whose edge rasterisation leaves gaps where two cells meet.
        case TRI_BR:
            for (int r = 0; r < CELL; r++) d.drawHLine(x + CELL - 1 - r, y + r, r + 1);
            break;
        case TRI_BL:
            for (int r = 0; r < CELL; r++) d.drawHLine(x, y + r, r + 1);
            break;
        case TRI_TR:
            for (int r = 0; r < CELL; r++) d.drawHLine(x + r, y + r, CELL - r);
            break;
        case TRI_TL:
            for (int r = 0; r < CELL; r++) d.drawHLine(x, y + r, CELL - r);
            break;
        case CHECKER:
            for (int r = 0; r < CELL; r++)
                for (int c = r & 1; c < CELL; c += 2) d.drawPixel(x + c, y + r);
            break;
    }
}

void text(int x, int y, const char* s, bool reverse) {
    char up[40];
    size_t n = 0;
    for (; s[n] && n < sizeof(up) - 1; n++) up[n] = toupper((unsigned char)s[n]);
    up[n] = '\0';

    U8G2& d = display::u8g2();
    font();
    d.setFontPosTop();
    if (reverse) {
        d.drawBox(x, y, n * CELL, CELL);
        d.setDrawColor(0);
        d.setFontMode(1);      // transparent, so only the glyph pixels are cut out
    }
    d.drawStr(x, y, up);
    if (reverse) {
        d.setDrawColor(1);
        d.setFontMode(0);
    }
    d.setFontPosBaseline();    // every other screen positions text by baseline
}

void cursor(int x, int y) { display::u8g2().drawBox(x, y, CELL, CELL); }

}  // namespace pet
