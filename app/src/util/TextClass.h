#pragma once

#include "framework.h"

// Unicode character classification for the two places that need to know what a
// "word" is: the bookmark matcher (intro words like "Kapitel"/"розділ") and the
// whole-word search filter. GetStringTypeW classifies straight from the Unicode
// tables with NO locale input; IsCharAlphaW is documented to follow the language
// the USER selected and a CRT iswalpha follows the C locale - either would make
// the result depend on the host Windows configuration. Thread-safe and not UI
// bound: the document worker calls this while filtering search hits.

// Codepoint form: MuPDF hands out int codepoints, which can sit outside the BMP
// (LaTeX documents print italics from the mathematical alphanumeric block).
// GetStringTypeW classifies a surrogate PAIR and writes the pair's class into
// both elements, so the pair is passed whole rather than one unit at a time.
inline WORD CharTypeOf(char32_t c) {
    wchar_t units[2] = {};
    int count = 1;
    if (c <= 0xFFFF) {
        units[0] = static_cast<wchar_t>(c);
    } else if (c <= 0x10FFFF) {
        const char32_t rest = c - 0x10000;
        units[0] = static_cast<wchar_t>(0xD800 + (rest >> 10));
        units[1] = static_cast<wchar_t>(0xDC00 + (rest & 0x3FF));
        count = 2;
    } else {
        return 0;
    }
    WORD types[2] = {};
    if (!GetStringTypeW(CT_CTYPE1, units, count, types))
        return 0;
    return types[0];
}

// A letter: accents, Cyrillic and Greek included.
inline bool IsWordLetter(char32_t c) {
    return (CharTypeOf(c) & C1_ALPHA) != 0;
}

// A word character for boundary tests. Letters plus DIGITS, so "3.14" and "H2O"
// read as single words. The underscore is deliberately OUT: this classifies
// prose printed in a PDF, not identifiers in source code, and treating "_" as a
// letter would only ever merge a word with the rule character drawn next to it.
inline bool IsWordChar(char32_t c) {
    return (CharTypeOf(c) & (C1_ALPHA | C1_DIGIT)) != 0;
}
