#include "GlyphIcons.h"

#include <cstdint>

namespace {

// The half both builders share: one horizontal 32bpp top-down DIB strip of
// count * cellPx pixels that ImageList_Add later slices into cells. GDI text
// output writes no alpha, so everything is drawn white-on-black and the gray
// level is converted to coverage in Finish.
class CellStrip {
public:
    ~CellStrip() { Release(); }

    bool Begin(int cells, int px) {
        m_count = cells;
        m_cellPx = px;
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = cells * px;
        bi.bmiHeader.biHeight = -px; // top-down
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;

        m_dc = CreateCompatibleDC(nullptr);
        if (!m_dc)
            return false;
        void* raw = nullptr;
        m_dib = CreateDIBSection(m_dc, &bi, DIB_RGB_COLORS, &raw, nullptr, 0);
        if (!m_dib) {
            DeleteDC(m_dc);
            m_dc = nullptr;
            return false;
        }
        m_bits = static_cast<uint32_t*>(raw);
        m_oldBmp = SelectObject(m_dc, m_dib);
        SetBkMode(m_dc, TRANSPARENT);
        SetTextColor(m_dc, RGB(255, 255, 255));
        // GM_ADVANCED for the mirror passes: TrueType output follows the world
        // transform there (including the flip), which GM_COMPATIBLE ignores.
        SetGraphicsMode(m_dc, GM_ADVANCED);
        return true;
    }

    HDC Dc() const { return m_dc; }

    // Recolours the coverage and hands over the imagelist; the strip is spent
    // afterwards either way. The caller must have restored the DC's font.
    HIMAGELIST Finish(COLORREF color) {
        GdiFlush(); // GDI batches: flush before reading the DIB bits

        // comctl32 v6 alpha-blends 32bpp imagelists as premultiplied ARGB.
        const size_t total = static_cast<size_t>(m_count) * static_cast<size_t>(m_cellPx) *
                             static_cast<size_t>(m_cellPx);
        const uint32_t r = GetRValue(color);
        const uint32_t g = GetGValue(color);
        const uint32_t b = GetBValue(color);
        for (size_t i = 0; i < total; ++i) {
            const uint32_t a = m_bits[i] & 0xFFu; // grayscale render: any channel is coverage
            m_bits[i] = (a << 24) | ((r * a / 255) << 16) | ((g * a / 255) << 8) | (b * a / 255);
        }

        SelectObject(m_dc, m_oldBmp);
        DeleteDC(m_dc);
        m_dc = nullptr;

        HIMAGELIST himl = ImageList_Create(m_cellPx, m_cellPx, ILC_COLOR32, m_count, 0);
        if (himl && ImageList_Add(himl, m_dib, nullptr) < 0) {
            ImageList_Destroy(himl);
            himl = nullptr;
        }
        DeleteObject(m_dib);
        m_dib = nullptr;
        m_bits = nullptr;
        return himl;
    }

private:
    void Release() {
        if (m_dc) {
            SelectObject(m_dc, m_oldBmp);
            DeleteDC(m_dc);
        }
        if (m_dib)
            DeleteObject(m_dib);
    }

    HDC m_dc = nullptr;
    HBITMAP m_dib = nullptr;
    uint32_t* m_bits = nullptr;
    HGDIOBJ m_oldBmp = nullptr;
    int m_count = 0;
    int m_cellPx = 0;
};

} // namespace

HIMAGELIST CreateGlyphImageList(std::span<const GlyphSpec> glyphs, int glyphPx, int cellPx,
                                COLORREF color) {
    const int count = static_cast<int>(glyphs.size());
    if (count == 0 || glyphPx <= 0 || cellPx <= 0)
        return nullptr;

    CellStrip strip;
    if (!strip.Begin(count, cellPx))
        return nullptr;
    HDC dc = strip.Dc();

    // ANTIALIASED_QUALITY, never ClearType: subpixel RGB fringes would corrupt
    // the grayscale-to-coverage conversion in Finish.
    HFONT font = CreateFontW(-glyphPx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                             DEFAULT_PITCH | FF_DONTCARE, L"Segoe MDL2 Assets");
    const HGDIOBJ oldFont = SelectObject(dc, font);
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
    SelectObject(dc, oldFont);
    if (font)
        DeleteObject(font);
    return strip.Finish(color);
}

HIMAGELIST CreateLabelImageList(std::span<const LabelSpec> labels, int cellPx, COLORREF color) {
    const int count = static_cast<int>(labels.size());
    if (count == 0 || cellPx <= 0)
        return nullptr;
    for (const LabelSpec& label : labels)
        if (!label.text || !*label.text)
            return nullptr;

    CellStrip strip;
    if (!strip.Begin(count, cellPx))
        return nullptr;
    HDC dc = strip.Dc();

    const int rule = std::max(1, cellPx / 16);   // underline thickness
    const int margin = std::max(1, cellPx / 16); // HORIZONTAL only, see below
    bool anyUnderline = false;
    for (const LabelSpec& label : labels)
        anyUnderline = anyUnderline || label.underline;
    // The rule band is the ONLY vertical reservation, and every cell reserves it
    // whether underlined or not, so the letters share one baseline across the
    // strip. No top or bottom margin: the font's own ascent and descent leading
    // already keeps the ink off the edges, while on the 16-px cell of a 96-DPI
    // screen those margins cost three rows out of sixteen - enough to drive the
    // fitted size down to where "a" and "b" close into blobs.
    const int textW = cellPx - 2 * margin;
    const int textH = cellPx - (anyUnderline ? rule : 0);
    if (textW <= 0 || textH <= 0)
        return nullptr;

    // One size for the whole strip, found by shrinking until every label fits:
    // DrawText reports the LINE height, which exceeds the em size asked for, so
    // the first candidates are expected to fail. FW_NORMAL: at these sizes the
    // extra weight closes the counters of "a" and "e" and the pair reads as a
    // smudge rather than as letters.
    HFONT font = nullptr;
    for (int em = textH; em >= 5 && !font; --em) {
        HFONT candidate = CreateFontW(-em, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                      DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                      ANTIALIASED_QUALITY, VARIABLE_PITCH | FF_SWISS, L"Segoe UI");
        if (!candidate)
            break;
        const HGDIOBJ prev = SelectObject(dc, candidate);
        bool fits = true;
        for (const LabelSpec& label : labels) {
            RECT measure{};
            DrawTextW(dc, label.text, -1, &measure,
                      DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX | DT_LEFT);
            if (measure.right - measure.left > textW || measure.bottom - measure.top > textH)
                fits = false;
        }
        SelectObject(dc, prev);
        if (fits)
            font = candidate;
        else
            DeleteObject(candidate);
    }
    if (!font)
        return nullptr;

    const HGDIOBJ oldFont = SelectObject(dc, font);
    HBRUSH ink = CreateSolidBrush(RGB(255, 255, 255));
    for (int i = 0; i < count; ++i) {
        const LabelSpec& label = labels[static_cast<size_t>(i)];
        const int left = i * cellPx;
        RECT cell{left + margin, 0, left + cellPx - margin, textH};
        // No DT_NOCLIP here, unlike the MDL2 path: these cells sit in one DIB
        // strip, and a label wider than its cell would bleed into the next.
        DrawTextW(dc, label.text, -1, &cell, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        if (!label.underline || !ink)
            continue;
        RECT measure{};
        DrawTextW(dc, label.text, -1, &measure,
                  DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX | DT_LEFT);
        const int width = std::min(textW, static_cast<int>(measure.right - measure.left));
        const int x = left + (cellPx - width) / 2;
        // A GDI rule rather than an underlined FONT: at these sizes the font's
        // own underline lands on a fractional row and antialiases to a smear.
        RECT bar{x, textH, x + width, textH + rule};
        FillRect(dc, &bar, ink);
    }
    SelectObject(dc, oldFont);
    DeleteObject(font);
    if (ink)
        DeleteObject(ink);
    return strip.Finish(color);
}
