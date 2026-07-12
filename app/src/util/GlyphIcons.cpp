#include "GlyphIcons.h"

#include <cstdint>

HIMAGELIST CreateGlyphImageList(std::span<const GlyphSpec> glyphs, int glyphPx, int cellPx,
                                COLORREF color) {
    const int count = static_cast<int>(glyphs.size());
    if (count == 0 || glyphPx <= 0 || cellPx <= 0)
        return nullptr;

    // One horizontal strip: ImageList_Add slices it into `count` cells.
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = count * cellPx;
    bi.bmiHeader.biHeight = -cellPx; // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc)
        return nullptr;
    void* bitsRaw = nullptr;
    HBITMAP dib = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bitsRaw, nullptr, 0);
    if (!dib) {
        DeleteDC(dc);
        return nullptr;
    }

    // GDI text output writes no alpha, so the glyphs are drawn white-on-black
    // and the gray level is then converted to coverage. ANTIALIASED_QUALITY,
    // never ClearType: subpixel RGB fringes would corrupt that conversion.
    HFONT font = CreateFontW(-glyphPx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, L"Segoe MDL2 Assets");
    const HGDIOBJ oldBmp = SelectObject(dc, dib);
    const HGDIOBJ oldFont = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    // GM_ADVANCED for the mirror passes: TrueType output follows the world
    // transform there (including the flip), which GM_COMPATIBLE ignores.
    SetGraphicsMode(dc, GM_ADVANCED);
    for (int i = 0; i < count; ++i) {
        RECT cell{i * cellPx, 0, (i + 1) * cellPx, cellPx};
        DrawTextW(dc, &glyphs[static_cast<size_t>(i)].ch, 1, &cell,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
        if (glyphs[static_cast<size_t>(i)].mirrorOverlay) {
            // Mirror around the cell's vertical center line (x' = 2c - x):
            // the cell rect maps onto itself, so the same DrawText overlays
            // the flipped copy exactly. White-on-black, so overlapping
            // strokes just saturate before the coverage conversion below.
            XFORM flip{-1.0f, 0.0f, 0.0f, 1.0f, static_cast<FLOAT>((2 * i + 1) * cellPx), 0.0f};
            SetWorldTransform(dc, &flip);
            DrawTextW(dc, &glyphs[static_cast<size_t>(i)].ch, 1, &cell,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
            ModifyWorldTransform(dc, nullptr, MWT_IDENTITY);
        }
    }
    GdiFlush(); // GDI batches: flush before reading the DIB bits

    // comctl32 v6 alpha-blends 32bpp imagelists as premultiplied ARGB.
    auto* px = static_cast<uint32_t*>(bitsRaw);
    const size_t total = static_cast<size_t>(count) * static_cast<size_t>(cellPx) *
                         static_cast<size_t>(cellPx);
    const uint32_t r = GetRValue(color);
    const uint32_t g = GetGValue(color);
    const uint32_t b = GetBValue(color);
    for (size_t i = 0; i < total; ++i) {
        const uint32_t a = px[i] & 0xFFu; // grayscale render: any channel is coverage
        px[i] = (a << 24) | ((r * a / 255) << 16) | ((g * a / 255) << 8) | (b * a / 255);
    }

    SelectObject(dc, oldFont);
    SelectObject(dc, oldBmp);
    if (font)
        DeleteObject(font);
    DeleteDC(dc);

    HIMAGELIST himl = ImageList_Create(cellPx, cellPx, ILC_COLOR32, count, 0);
    if (himl && ImageList_Add(himl, dib, nullptr) < 0) {
        ImageList_Destroy(himl);
        himl = nullptr;
    }
    DeleteObject(dib);
    return himl;
}
