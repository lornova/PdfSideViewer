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
    // A new pagination invalidates every gap's beforePage, and restores
    // (session, swap, reload) must interpret their offsets in a no-gap layout.
    m_gaps.clear();
    m_sumGapHeightsPt = 0;
    RebuildSlotMap();
}

void PageLayout::Clear() {
    m_maxSizePt = {};
    m_sumHeightsPt = 0;
    m_sumGapHeightsPt = 0;
    m_sizesPt.clear();
    m_gaps.clear();
    m_slotReal.clear();
    m_realSlot.clear();
    m_slotSizePt.clear();
    m_tops.clear();
    m_bottoms.clear();
    m_lefts.clear();
    m_widths.clear();
    m_heights.clear();
    m_totalW = 0;
    m_totalH = 0;
}

void PageLayout::SetAlignmentGaps(std::vector<AlignmentGap> gaps) {
    m_gaps = std::move(gaps);
    m_sumGapHeightsPt = 0;
    for (const AlignmentGap& g : m_gaps)
        m_sumGapHeightsPt += g.sizePt.height;
    RebuildSlotMap();
}

void PageLayout::RebuildSlotMap() {
    const int n = PageCount();
    m_slotReal.clear();
    m_slotSizePt.clear();
    m_realSlot.assign(static_cast<size_t>(n), 0);
    size_t g = 0;
    for (int p = 0; p < n; ++p) {
        // <= consumes out-of-order stragglers defensively; entries with
        // beforePage >= n simply never match and are dropped.
        while (g < m_gaps.size() && m_gaps[g].beforePage <= p) {
            m_slotReal.push_back(-1);
            m_slotSizePt.push_back(m_gaps[g].sizePt);
            ++g;
        }
        m_realSlot[static_cast<size_t>(p)] = static_cast<int>(m_slotReal.size());
        m_slotReal.push_back(p);
        m_slotSizePt.push_back(m_sizesPt[static_cast<size_t>(p)]);
    }
}

void PageLayout::Update(float scale, float gap, float margin) {
    m_scale = scale;
    gap = std::floor(gap + 0.5f);
    margin = std::floor(margin + 0.5f);
    m_interGap = gap;
    const size_t slots = m_slotReal.size();
    m_tops.resize(slots);
    m_bottoms.resize(slots);
    m_lefts.resize(slots);
    m_widths.resize(slots);
    m_heights.resize(slots);

    float maxW = 0;
    for (size_t s = 0; s < slots; ++s) {
        // Quantize exactly like fz_round_rect on origin-normalized bounds
        // (ceil with a small contraction epsilon): the layout rect then equals
        // the rendered pixmap size, so the up-to-date blit is 1:1 and crisp.
        // Gap slots are never blitted but get the same integral treatment so
        // every later slot's top stays integral (the pixel snap survives).
        m_widths[s] = std::ceil(m_slotSizePt[s].width * scale - 0.001f);
        m_heights[s] = std::ceil(m_slotSizePt[s].height * scale - 0.001f);
        if (m_slotReal[s] >= 0)
            maxW = std::max(maxW, m_widths[s]);
    }
    for (size_t s = 0; s < slots; ++s) {
        // Gap widths are capped at the real pages' width so TotalWidth (and
        // with it PersistScrollX and the horizontal centering) stays
        // gap-invariant.
        if (m_slotReal[s] < 0)
            m_widths[s] = std::min(m_widths[s], maxW);
    }

    float y = margin;
    for (size_t s = 0; s < slots; ++s) {
        m_tops[s] = y;
        m_bottoms[s] = y + m_heights[s];
        m_lefts[s] = std::floor(margin + (maxW - m_widths[s]) / 2.0f + 0.5f);
        y += m_heights[s] + gap;
    }
    m_totalW = slots ? maxW + 2.0f * margin : 0.0f;
    m_totalH = slots ? y - gap + margin : 0.0f;
}

D2D1_RECT_F PageLayout::SlotRect(int slot) const {
    const auto idx = static_cast<size_t>(slot);
    return D2D1::RectF(m_lefts[idx], m_tops[idx], m_lefts[idx] + m_widths[idx], m_bottoms[idx]);
}

int PageLayout::PrevRealPage(int slot) const {
    for (int s = std::min(slot, SlotCount() - 1); s >= 0; --s)
        if (m_slotReal[static_cast<size_t>(s)] >= 0)
            return m_slotReal[static_cast<size_t>(s)];
    return -1;
}

int PageLayout::NextRealPage(int slot) const {
    for (int s = std::max(slot, 0); s < SlotCount(); ++s)
        if (m_slotReal[static_cast<size_t>(s)] >= 0)
            return m_slotReal[static_cast<size_t>(s)];
    return PageCount();
}

int PageLayout::FirstVisibleSlot(float contentY) const {
    const auto it = std::upper_bound(m_bottoms.begin(), m_bottoms.end(), contentY);
    return static_cast<int>(it - m_bottoms.begin());
}

int PageLayout::NearestRealPageAt(float contentY) const {
    if (m_sizesPt.empty())
        return -1;
    const int slot = FirstVisibleSlot(contentY);
    int page;
    if (slot >= SlotCount())
        page = PageCount() - 1;
    else if (SlotToReal(slot) >= 0)
        page = SlotToReal(slot);
    else
        page = NextRealPage(slot); // inside a gap: the page it leads to
    return std::clamp(page, 0, PageCount() - 1);
}

float PageLayout::GapPixelsAbove(float contentY) const {
    if (m_gaps.empty())
        return 0;
    float total = 0;
    for (size_t s = 0; s < m_slotReal.size(); ++s) {
        if (m_slotReal[s] >= 0)
            continue;
        // Removing this gap slot shifts everything below it up by exactly
        // height + inter-slot spacing (all integral); a y INSIDE the gap maps
        // to the no-gap boundary.
        total += std::clamp(contentY - m_tops[s], 0.0f, m_heights[s] + m_interGap);
    }
    return total;
}

std::optional<PageLayout::PagePoint> PageLayout::HitTest(float contentX, float contentY) const {
    const int slot = FirstVisibleSlot(contentY);
    if (slot >= SlotCount())
        return std::nullopt;
    const int page = SlotToReal(slot);
    if (page < 0)
        return std::nullopt; // alignment gaps have no content
    const D2D1_RECT_F rect = SlotRect(slot);
    if (contentX < rect.left || contentX > rect.right || contentY < rect.top ||
        contentY > rect.bottom)
        return std::nullopt;
    return PagePoint{page, (contentX - rect.left) / m_scale, (contentY - rect.top) / m_scale};
}

D2D1_POINT_2F PageLayout::ToContent(const PagePoint& p) const {
    const auto idx = static_cast<size_t>(RealToSlot(p.page));
    return D2D1::Point2F(m_lefts[idx] + p.x * m_scale, m_tops[idx] + p.y * m_scale);
}
