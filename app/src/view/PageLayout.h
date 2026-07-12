#pragma once

#include "framework.h"

#include <optional>
#include <vector>

// Continuous vertical layout of pages in "content space": device pixels at the
// current zoom, origin at the top-left of the whole document canvas. Pages are
// horizontally centered on the widest page. Prefix sums of slot bottoms allow
// binary search of the first visible slot.
//
// Slots vs pages: the geometry arrays are per SLOT, where a slot is either a
// real page or an ALIGNMENT GAP (an empty page-sized hole inserted so two
// synced documents line up side by side, WinMerge-style). Real-page identity
// is preserved through the slot map; every real-page query (PageRect,
// PageSizePx, HitTest, ToContent) maps through it, so with an empty gap list
// the layout is bit-identical to the pre-slot behavior. Fit inputs
// (MaxPageSizePt, SumPageHeightsPt, PageCount) see REAL pages only: a gap must
// never drive the fit zoom.
class PageLayout {
public:
    // One alignment gap: an empty slot inserted immediately BEFORE the given
    // real page, sized like the counterpart page it mirrors (PDF points).
    struct AlignmentGap {
        int beforePage = 0;
        D2D1_SIZE_F sizePt{};
    };

    // Also clears the alignment gaps: a new pagination invalidates beforePage,
    // and session/swap/reload restores must land in a no-gap layout.
    void SetPages(std::vector<D2D1_SIZE_F> pageSizesPt);
    void Clear();
    // Sorted by beforePage; entries past the page count are dropped. The
    // caller relayouts (Update) afterwards.
    void SetAlignmentGaps(std::vector<AlignmentGap> gaps);
    bool HasAlignmentGaps() const { return !m_gaps.empty(); }
    const std::vector<AlignmentGap>& AlignmentGaps() const { return m_gaps; }

    // scale: PDF points -> device pixels (zoom * dpi / 72); gap and margin in
    // device pixels.
    void Update(float scale, float gap, float margin);

    int PageCount() const { return static_cast<int>(m_sizesPt.size()); } // REAL pages
    int SlotCount() const { return static_cast<int>(m_slotReal.size()); }
    bool Empty() const { return m_sizesPt.empty(); }
    int SlotToReal(int slot) const { return m_slotReal[static_cast<size_t>(slot)]; } // -1 = gap
    int RealToSlot(int page) const { return m_realSlot[static_cast<size_t>(page)]; }
    // Largest real page whose slot <= slot (-1 if none) / smallest real page
    // whose slot >= slot (PageCount() if none).
    int PrevRealPage(int slot) const;
    int NextRealPage(int slot) const;

    // Largest page dimensions in points; drives the fit-width/fit-page zooms.
    D2D1_SIZE_F MaxPageSizePt() const { return m_maxSizePt; }
    // Sum of page heights in points; estimates the content height at a
    // prospective zoom without running a full layout.
    float SumPageHeightsPt() const { return m_sumHeightsPt; }
    // Gap contributions to that estimate (the fit ratio itself never sees gaps).
    float SumGapHeightsPt() const { return m_sumGapHeightsPt; }
    int GapCount() const { return static_cast<int>(m_gaps.size()); }
    float TotalWidth() const { return m_totalW; }
    float TotalHeight() const { return m_totalH; }
    D2D1_RECT_F PageRect(int page) const { return SlotRect(RealToSlot(page)); }
    D2D1_RECT_F SlotRect(int slot) const;

    // Original page size in PDF points (zoom-independent; feeds the sibling
    // pane's alignment-gap silhouettes).
    D2D1_SIZE_F PageSizePt(int page) const { return m_sizesPt[static_cast<size_t>(page)]; }
    // Quantized page pixel size, exact (never recompute it from PageRect edge
    // differences: tops/bottoms are float and lose ±1 px past 2^24 content px,
    // which would desynchronize the tile grid from the render worker's).
    D2D1_SIZE_F PageSizePx(int page) const {
        const auto idx = static_cast<size_t>(RealToSlot(page));
        return D2D1::SizeF(m_widths[idx], m_heights[idx]);
    }

    // First SLOT whose bottom edge is below the given content-space y.
    int FirstVisibleSlot(float contentY) const;
    // Real page at/near a content y (gap resolves to the following page);
    // clamped to [0, PageCount-1]; -1 when empty. Scroll-tip helper.
    int NearestRealPageAt(float contentY) const;

    // Accumulated gap pixels above a content y: subtracting it converts a
    // scroll offset to the exact offset the same view has in a no-gap layout
    // (removing a gap slot shifts every later top by height + inter-slot gap,
    // all integral). Persist normalization.
    float GapPixelsAbove(float contentY) const;

    struct PagePoint {
        int page = 0; // REAL page
        float x = 0;  // within-page position in PDF points
        float y = 0;
    };
    std::optional<PagePoint> HitTest(float contentX, float contentY) const; // gaps -> nullopt
    D2D1_POINT_2F ToContent(const PagePoint& p) const;

private:
    void RebuildSlotMap(); // slot<->real tables; depends only on pages + gaps

    std::vector<D2D1_SIZE_F> m_sizesPt;     // REAL pages
    std::vector<AlignmentGap> m_gaps;       // sorted by beforePage
    std::vector<int> m_slotReal;            // per slot: real page index, -1 = gap
    std::vector<int> m_realSlot;            // per real page: its slot index
    std::vector<D2D1_SIZE_F> m_slotSizePt;  // per slot: page or gap silhouette
    std::vector<float> m_tops;    // content px (integral), per SLOT
    std::vector<float> m_bottoms; // content px (integral), monotonically increasing
    std::vector<float> m_lefts;   // content px (integral)
    std::vector<float> m_widths;  // content px, quantized like fz_round_rect
    std::vector<float> m_heights; // content px, quantized like fz_round_rect
    float m_scale = 1.0f;
    float m_interGap = 0; // pre-rounded inter-slot spacing of the last Update
    float m_totalW = 0;
    float m_totalH = 0;
    D2D1_SIZE_F m_maxSizePt{};
    float m_sumHeightsPt = 0;
    float m_sumGapHeightsPt = 0;
};
