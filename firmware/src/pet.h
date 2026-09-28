// PET look: the 8x8 character face and the PETSCII-style graphics cells.
//
// The real PET ROM font is not in U8g2, so text uses PxPlus IBM CGA "thin" —
// same 8x8 cell and the same single-pixel strokes. The graphics characters
// (blocks, triangles, the 10 PRINT diagonals) are drawn from primitives rather
// than from a font, so they are exact to the pixel and cost no flash.
//
// Everything here is in pixels; callers add the burn-in offset themselves.
#pragma once

#include <Arduino.h>

namespace pet {

constexpr int CELL = 8;   // 128x64 → a 16x8 character screen

// Graphics cells. Values sit below 0x20 so a terminal can store them in the
// same char grid as text and tell them apart from printable characters.
enum Glyph : uint8_t {
    DIAG_DN = 1,   // ╲  PETSCII 205 — half of the 10 PRINT maze
    DIAG_UP,       // ╱  PETSCII 206 — the other half
    FULL,          // █
    TRI_BR,        // ◢  lower-right half filled
    TRI_BL,        // ◣  lower-left
    TRI_TR,        // ◥  upper-right
    TRI_TL,        // ◤  upper-left
    CHECKER,       // ▒
};

void font();                                  // select the 8x8 text face
void glyph(int x, int y, uint8_t g);          // one graphics cell, top-left at (x, y)
// Upper-cased, top-left at (x, y). `reverse` is the PET's RVS mode: dark
// characters on a lit bar exactly as wide as the string.
void text(int x, int y, const char* s, bool reverse = false);
void cursor(int x, int y);                    // the solid block cursor

}  // namespace pet
