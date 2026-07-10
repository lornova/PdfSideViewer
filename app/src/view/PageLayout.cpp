#include "view/PageLayout.h"

void PageLayout::SetPages(std::vector<D2D1_SIZE_F> pageSizesPt) {
    m_sizesPt = std::move(pageSizesPt);
    m_maxSizePt = {};
    m_sumHeightsPt = 0;
    for (const auto& size : m_sizesPt) {
        m_maxSizePt.width = std::max(m_maxSizePt.width, size.width);
        m_maxSizePt.height = std::max(m_maxSizePt.height, size.height);
        m_sumHeightsPt += size.height;
    }
}

void PageLayout::Clear() {
    m_maxSizePt = {};
    m_sumHeightsPt = 0;
    m_sizesPt.clear();
    m_tops.clear();
    m_bottoms.clear();
    m_lefts.clear();
    m_widths.clear();
    m_heights.clear();
    m_totalW = 0;
    m_totalH = 0;
}

void PageLayout::Update(float scale, float gap, float margin) {
    m_scale = scale;
    gap = std::floor(gap + 0.5f);
    margin = std::floor(margin + 0.5f);
    const size_t n = m_sizesPt.size();
    m_tops.resize(n);
    m_bottoms.resize(n);
    m_lefts.resize(n);
    m_widths.resize(n);
    m_heights.resize(n);

    float maxW = 0;
    for (size_t i = 0; i < n; ++i) {
        // Quantize exactly like fz_round_rect on origin-normalized bounds
        // (ceil with a small contraction epsilon): the layout rect then equals
        // the rendered pixmap size, so the up-to-date blit is 1:1 and crisp.
        m_widths[i] = std::ceil(m_sizesPt[i].width * scale - 0.001f);
        m_heights[i] = std::ceil(m_sizesPt[i].height * scale - 0.001f);
        maxW = std::max(maxW, m_widths[i]);
    }

    float y = margin;
    for (size_t i = 0; i < n; ++i) {
        m_tops[i] = y;
        m_bottoms[i] = y + m_heights[i];
        m_lefts[i] = std::floor(margin + (maxW - m_widths[i]) / 2.0f + 0.5f);
        y += m_heights[i] + gap;
    }
    m_totalW = n ? maxW + 2.0f * margin : 0.0f;
    m_totalH = n ? y - gap + margin : 0.0f;
}

D2D1_RECT_F PageLayout::PageRect(int i) const {
    const auto idx = static_cast<size_t>(i);
    return D2D1::RectF(m_lefts[idx], m_tops[idx], m_lefts[idx] + m_widths[idx], m_bottoms[idx]);
}

int PageLayout::FirstVisible(float contentY) const {
    const auto it = std::upper_bound(m_bottoms.begin(), m_bottoms.end(), contentY);
    return static_cast<int>(it - m_bottoms.begin());
}

std::optional<PageLayout::PagePoint> PageLayout::HitTest(float contentX, float contentY) const {
    const int page = FirstVisible(contentY);
    if (page >= PageCount())
        return std::nullopt;
    const D2D1_RECT_F rect = PageRect(page);
    if (contentX < rect.left || contentX > rect.right || contentY < rect.top ||
        contentY > rect.bottom)
        return std::nullopt;
    return PagePoint{page, (contentX - rect.left) / m_scale, (contentY - rect.top) / m_scale};
}

D2D1_POINT_2F PageLayout::ToContent(const PagePoint& p) const {
    const auto idx = static_cast<size_t>(p.page);
    return D2D1::Point2F(m_lefts[idx] + p.x * m_scale, m_tops[idx] + p.y * m_scale);
}
