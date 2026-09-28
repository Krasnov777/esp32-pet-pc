#include "term.h"
#include "display.h"
#include "pet.h"

namespace term {

// ── Screen ──────────────────────────────────────────────────────────────────

void Screen::clear() {
    memset(cells_, 0, sizeof(cells_));
    row_ = col_ = 0;
    wrapped_ = false;
}

void Screen::scroll() {
    memmove(cells_[0], cells_[1], (ROWS - 1) * COLS);
    memset(cells_[ROWS - 1], 0, COLS);
}

void Screen::newline() {
    col_ = 0;
    wrapped_ = false;
    if (++row_ >= ROWS) {
        scroll();
        row_ = ROWS - 1;
    }
}

void Screen::put(char c) {
    if (c == '\n') { newline(); return; }
    // The wrap is deferred until a character actually needs the next line, so
    // a line of exactly COLS characters followed by RETURN leaves no blank row.
    if (col_ >= COLS) {
        newline();
        wrapped_ = true;
    }
    cells_[row_][col_++] = (uint8_t)c < 0x20 ? c : toupper((unsigned char)c);
}

void Screen::emit(const char* s, size_t i) {
    char c = s[i];
    if (c == '\n') { newline(); return; }
    if (c == ' ' && col_ == 0 && wrapped_) return;      // no leading space on a wrapped line

    bool word_start = c != ' ' && (i == 0 || s[i - 1] == ' ');
    if (word_start && col_ > 0) {
        size_t len = 0;
        while (s[i + len] && s[i + len] != ' ' && s[i + len] != '\n') len++;
        // A word longer than a whole line has to break anyway, so let it
        // start where it is instead of wasting the rest of this one.
        if (len <= COLS && col_ + len > COLS) {
            newline();
            wrapped_ = true;
        }
    }
    put(c);
}

void Screen::print(const char* s) {
    for (size_t i = 0; s[i]; i++) emit(s, i);
}

void Screen::draw(int x0, int y0, bool cursor) const {
    U8G2& g = display::u8g2();
    pet::font();
    g.setFontPosTop();
    for (uint8_t r = 0; r < ROWS; r++) {
        for (uint8_t c = 0; c < COLS; c++) {
            char ch = cells_[r][c];
            int x = x0 + c * pet::CELL, y = y0 + r * pet::CELL;
            if ((uint8_t)ch > ' ')  g.drawGlyph(x, y, ch);
            else if (ch)            pet::glyph(x, y, ch);
        }
    }
    g.setFontPosBaseline();
    if (cursor && col_ < COLS) pet::cursor(x0 + col_ * pet::CELL, y0 + row_ * pet::CELL);
}

// ── Script ──────────────────────────────────────────────────────────────────

namespace {
constexpr uint16_t RETURN_MS = 250;   // the beat between the last key and RETURN
}

void Script::push(Kind k, const char* s, uint16_t arg, Fn fn) {
    if (count_ >= CAP) {
        Serial.println(F("[term] script queue full — step dropped"));
        return;
    }
    Step& st = q_[(head_ + count_) % CAP];
    st.kind = k;
    st.arg  = arg;
    st.fn   = fn;
    strlcpy(st.text, s ? s : "", sizeof(st.text));
    count_++;
}

void Script::pop() {
    head_ = (head_ + 1) % CAP;
    count_--;
    live_ = false;
}

void Script::type(const char* s, uint8_t cps) { push(Kind::Type, s, cps ? cps : 1, nullptr); }
void Script::out(const char* s)               { push(Kind::Out, s, 0, nullptr); }
void Script::wait(uint16_t ms)                { push(Kind::Wait, nullptr, ms, nullptr); }
void Script::call(Fn fn)                      { push(Kind::Call, nullptr, 0, fn); }

void Script::clear() {
    count_ = 0;
    live_  = false;
}

bool Script::typing() const { return count_ > 0 && live_ && q_[head_].kind == Kind::Type; }

void Script::tick(uint32_t now) {
    while (count_) {
        Step& s = q_[head_];
        if (!live_) {
            live_    = true;
            armed_   = false;
            started_ = now;
            typed_   = 0;
        }
        switch (s.kind) {
            case Kind::Out:
                scr_.println(s.text);
                break;

            case Kind::Wait:
                if (now - started_ < s.arg) return;
                break;

            case Kind::Type: {
                size_t   len = strlen(s.text);
                uint32_t el  = now - started_;
                size_t   due = el * s.arg / 1000 + 1;     // first key lands on the first frame
                while (typed_ < len && typed_ < due) scr_.emit(s.text, typed_++);
                if (typed_ < len) return;
                if (el < len * 1000UL / s.arg + RETURN_MS) return;
                scr_.newline();
                break;
            }

            case Kind::Call: {
                if (!armed_) { armed_ = true; return; }
                Fn fn = s.fn;
                pop();                  // first, so fn() can queue what comes next
                if (fn) fn();
                now = millis();         // fn() may have blocked (the weather fetch)
                continue;
            }
        }
        pop();
    }
}

}  // namespace term
