// A 16x8 PET-style text screen, and a script player that "types" into it.
//
// Screen is the character grid: it prints upper case, wraps at word
// boundaries, scrolls when it runs off the bottom and stores the pet::Glyph
// graphics cells alongside text. Script queues steps against a Screen — type
// a line at human speed, print a line instantly, pause, run a callback — and
// plays them from ui::tick(), so the boot banner, the tape sequence, the BASIC
// screen and the screensaver are all just short scripts.
#pragma once

#include <Arduino.h>

namespace term {

constexpr uint8_t COLS = 16;
constexpr uint8_t ROWS = 8;

class Screen {
public:
    void clear();
    void print(const char* s);                 // word-wrapped; '\n' starts a new line
    void println(const char* s) { print(s); newline(); }
    void put(char c);                          // one cell, no word logic (graphics, maze)
    void newline();

    // Emit s[i] with word wrap: at the start of a word that will not fit on
    // what is left of the line, move to the next line first.
    void emit(const char* s, size_t i);

    void draw(int x0, int y0, bool cursor) const;

private:
    void scroll();

    char    cells_[ROWS][COLS] = {};
    uint8_t row_ = 0;
    uint8_t col_ = 0;          // == COLS means "line full, wrap on the next char"
    bool    wrapped_ = false;  // the current line was started by an automatic wrap
};

class Script {
public:
    using Fn = void (*)();

    explicit Script(Screen& s) : scr_(s) {}

    void type(const char* s, uint8_t cps = 14);   // typed out, then RETURN
    void out(const char* s);                       // printed at once, as program output
    void wait(uint16_t ms);
    // Runs on the frame *after* the one that reached it, so whatever the
    // script printed just before is on the glass before a blocking call.
    void call(Fn fn);

    void clear();                                  // drop everything still queued
    bool busy() const { return count_ > 0; }
    bool typing() const;                           // a Type step is in progress

    void tick(uint32_t now);                       // advance; called once per frame

private:
    enum class Kind : uint8_t { Type, Out, Wait, Call };
    struct Step {
        Kind     kind;
        uint16_t arg;      // Type: chars per second, Wait: ms
        Fn       fn;
        char     text[40];
    };
    static constexpr uint8_t CAP = 24;

    void push(Kind k, const char* s, uint16_t arg, Fn fn);
    void pop();

    Screen&  scr_;
    Step     q_[CAP];
    uint8_t  head_  = 0;
    uint8_t  count_ = 0;
    bool     live_  = false;   // the head step has started
    bool     armed_ = false;   // the head Call step has had its frame
    uint32_t started_ = 0;
    uint8_t  typed_   = 0;
};

}  // namespace term
