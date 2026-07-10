#pragma once

#include "framework.h"

#include <commctrl.h>
#include <span>

// Renders one Segoe MDL2 Assets glyph per cell into a 32bpp ARGB imagelist
// (glyphs are toolbar icons here: crisp at any DPI, no binary assets to
// maintain). Each element of `glyphs` is a single UTF-16 unit; MDL2 icons all
// live in the BMP private-use area. The caller owns the returned imagelist.
HIMAGELIST CreateGlyphImageList(std::span<const wchar_t> glyphs, int glyphPx, int cellPx,
                                COLORREF color);
