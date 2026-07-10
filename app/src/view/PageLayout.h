#pragma once

#include "framework.h"

#include <optional>
#include <vector>

// Continuous vertical layout of pages in "content space": device pixels at the
// current zoom, origin at the top-left of the whole document canvas. Pages are
// horizontally centered on the widest page. Prefix sums of page bottoms allow
// binary search of the first visible page.
class PageLayout {
public:
    void SetPages(std::vector<D2D1_SIZE_F> pageSizesPt);
    void Clear();

    // scale: PDF points -> device pixels (zoom * dpi / 72); gap and margin in
    // device pixels.
    void Update(float scale, float gap, float margin);

    int PageCount() const { return static_cast<int>(m_sizesPt.size()); }
    bool Empty() const { return m_sizesPt.empty(); }
    // Largest page dimensions in points; drives the fit-width/fit-page zooms.
    D2D1_SIZE_F MaxPageSizePt() const { return m_maxSizePt; }
    // Sum of page heights in points; estimates the content height at a
    // prospective zoom without running a full layout.
    float SumPageHeightsPt() const { return m_sumHeightsPt; }
    float TotalWidth() const { return m_totalW; }
    float TotalHeight() const { return m_totalH; }
    D2D1_RECT_F PageRect(int i) const;

    // Quantized page pixel size, exact (never recompute it from PageRect edge
    // differences: tops/bottoms are float and lose ±1 px past 2^24 content px,
    // which would desynchronize the tile grid from the render worker's).
    D2D1_SIZE_F PageSizePx(int i) const {
        const auto idx = static_cast<size_t>(i);
        return D2D1::SizeF(m_widths[idx], m_heights[idx]);
    }

    // First page whose bottom edge is below the given content-space y.
    int FirstVisible(float contentY) const;

    struct PagePoint {
        int page = 0;
        float x = 0; // within-page position in PDF points
        float y = 0;
    };
    std::optional<PagePoint> HitTest(float contentX, float contentY) const;
    D2D1_POINT_2F ToContent(const PagePoint& p) const;

private:
    std::vector<D2D1_SIZE_F> m_sizesPt;
    std::vector<float> m_tops;    // content px (integral)
    std::vector<float> m_bottoms; // content px (integral), monotonically increasing
    std::vector<float> m_lefts;   // content px (integral)
    std::vector<float> m_widths;  // content px, quantized like fz_round_rect
    std::vector<float> m_heights; // content px, quantized like fz_round_rect
    float m_scale = 1.0f;
    float m_totalW = 0;
    float m_totalH = 0;
    D2D1_SIZE_F m_maxSizePt{};
    float m_sumHeightsPt = 0;
};
