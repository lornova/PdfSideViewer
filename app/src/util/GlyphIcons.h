#pragma once

#include "framework.h"

#include <commctrl.h>
#include <span>

// One glyph cell of the imagelist. mirrorOverlay draws the same glyph a
// second time mirrored horizontally on top of itself: composes symmetric
// icons (the 4-corner full-screen arrows = E740 plus its mirror) that Segoe
// MDL2 Assets does not ship as single codepoints.
struct GlyphSpec {
    wchar_t ch = 0;
    bool mirrorOverlay = false;
};

// Renders one Segoe MDL2 Assets glyph per cell into a 32bpp ARGB imagelist
// (glyphs are toolbar icons here: crisp at any DPI, no binary assets to
// maintain). Each glyph is a single UTF-16 unit; MDL2 icons all live in the
// BMP private-use area. The caller owns the returned imagelist.
HIMAGELIST CreateGlyphImageList(std::span<const GlyphSpec> glyphs, int glyphPx, int cellPx,
                                COLORREF color);

// One label cell: a very short string set in the UI font and drawn as an icon.
// underline adds a rule beneath it (the find bar's whole-word toggle).
struct LabelSpec {
    PCWSTR text = nullptr;
    bool underline = false;
};

// Same imagelist as CreateGlyphImageList, with short TEXT in place of the MDL2
// glyph: neither Segoe MDL2 Assets nor Segoe Fluent Icons ships anything for
// match case, whole word or regular expression, which is why every editor
// draws those as the letters themselves ("Aa", "ab", ".*"). The type size is
// fitted once for the WHOLE strip, so the cells read as one control instead of
// as labels that happen to sit next to each other. The caller owns the result.
HIMAGELIST CreateLabelImageList(std::span<const LabelSpec> labels, int cellPx, COLORREF color);
