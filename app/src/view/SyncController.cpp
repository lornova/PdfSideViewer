#include "view/SyncController.h"

#include <algorithm>

int SyncController::SlotOf(const PaneWindow& pane) const {
    for (int s = 0; s < kPaneSlots; ++s)
        if (m_panes[static_cast<size_t>(s)] == &pane)
            return s;
    return -1;
}

int SyncController::RefSlot() const {
    for (int s = 0; s < kPaneSlots; ++s)
        if (m_panes[static_cast<size_t>(s)])
            return s;
    return -1;
}

int SyncController::JoinedRefSlot() const {
    for (int s = 0; s < kPaneSlots; ++s)
        if (m_joined[static_cast<size_t>(s)])
            return s;
    return -1;
}

bool SyncController::AllPanesReady() const {
    bool any = false;
    for (PaneWindow* p : m_panes) {
        if (!p)
            continue;
        if (!p->HasDocument())
            return false;
        any = true;
    }
    return any;
}

bool SyncController::SlotGridsAligned() const {
    uint64_t epoch = 0;
    for (PaneWindow* p : m_panes) {
        if (!p)
            continue;
        const uint64_t v = p->AlignmentGapsVersion();
        if (v == 0)
            return false;
        if (epoch == 0)
            epoch = v;
        else if (epoch != v)
            return false;
    }
    return epoch != 0;
}

void SyncController::RecaptureAnchor() {
    // Captured over the JOINED subset, based at the first joined slot: an
    // empty pane must not suspend the pairing of the loaded ones (its
    // SyncPosition() would be a meaningless 0), and a pane still RESTORING
    // its saved view reports a document but a position in flux - it joins on
    // its DocumentOpened, when that position is final. Unjoined entries keep
    // the reset value. The anchor arithmetic is translation-invariant, so
    // which joined slot serves as base does not matter as long as one capture
    // used one base.
    m_anchor = {};
    const int ref = JoinedRefSlot();
    if (ref < 0)
        return;
    const double base = m_panes[static_cast<size_t>(ref)]->SyncPosition();
    for (int s = 0; s < kPaneSlots; ++s)
        if (m_joined[static_cast<size_t>(s)])
            m_anchor[static_cast<size_t>(s)] =
                m_panes[static_cast<size_t>(s)]->SyncPosition() - base;
}

void SyncController::RecaptureZoomAnchor() {
    // Joined subset, like RecaptureAnchor: a doc-less or still-restoring
    // pane's Zoom() would poison every ratio in the tuple (the restore path
    // fires FitZoomChanged mid-flight, which lands here).
    m_zoomAnchor = {1.0f, 1.0f, 1.0f};
    const int ref = JoinedRefSlot();
    if (ref < 0)
        return;
    const float base = m_panes[static_cast<size_t>(ref)]->Zoom(); // zoom is always >= kMinZoom
    for (int s = 0; s < kPaneSlots; ++s)
        if (m_joined[static_cast<size_t>(s)])
            m_zoomAnchor[static_cast<size_t>(s)] =
                m_panes[static_cast<size_t>(s)]->Zoom() / base;
}

bool SyncController::HasAutoPoints() const {
    for (const SyncPoint& p : m_points)
        if (!p.manual)
            return true;
    return false;
}

void SyncController::NotifyMapChanged() {
    if (m_onMapChanged)
        m_onMapChanged();
}

PerPane<int> SyncController::PagesHere() const {
    PerPane<int> pages{};
    for (int s = 0; s < kPaneSlots; ++s)
        if (m_panes[static_cast<size_t>(s)])
            pages[static_cast<size_t>(s)] =
                static_cast<int>(m_panes[static_cast<size_t>(s)]->SyncPosition());
    return pages;
}

int SyncController::PointIndexHere() const {
    const int ref = RefSlot();
    if (ref < 0 || !AllPanesReady())
        return -1;
    const PerPane<int> here = PagesHere();
    const size_t r = static_cast<size_t>(ref);
    // Points strictly increase in EVERY active coordinate, so the reference
    // page can belong to at most one of them: find that one, then require the
    // rest of the tuple to agree (a partial match is a different point, and
    // AddPointHere would drop it as incomparable).
    const auto at = std::lower_bound(m_points.begin(), m_points.end(), here[r],
                                     [r](const SyncPoint& p, int v) { return p.page[r] < v; });
    if (at == m_points.end() || at->page[r] != here[r])
        return -1;
    for (int s = 0; s < kPaneSlots; ++s) {
        const size_t i = static_cast<size_t>(s);
        if (m_panes[i] && at->page[i] != here[i])
            return -1;
    }
    return static_cast<int>(at - m_points.begin());
}

bool SyncController::AddPointHere() {
    const int ref = RefSlot();
    if (ref < 0 || !AllPanesReady())
        return false;
    SyncPoint fresh;
    fresh.manual = true;
    fresh.page = PagesHere();
    // The new point wins: drop every point not strictly on one side of it in
    // EVERY active coordinate (this also covers exact duplicates). With three
    // panes far more points are incomparable than with two, so this removes
    // more of a generated map - by design, the manual point is authoritative.
    std::erase_if(m_points, [&](const SyncPoint& p) {
        bool allBelow = true;
        bool allAbove = true;
        for (int s = 0; s < kPaneSlots; ++s) {
            const size_t i = static_cast<size_t>(s);
            if (!m_panes[i])
                continue;
            allBelow = allBelow && p.page[i] < fresh.page[i];
            allAbove = allAbove && p.page[i] > fresh.page[i];
        }
        return !(allBelow || allAbove);
    });
    const size_t r = static_cast<size_t>(ref);
    const int key = fresh.page[r];
    const auto at = std::lower_bound(m_points.begin(), m_points.end(), key,
                                     [r](const SyncPoint& p, int v) { return p.page[r] < v; });
    m_points.insert(at, std::move(fresh));
    NotifyMapChanged();
    return true;
}

void SyncController::RemovePoint(size_t index) {
    if (index >= m_points.size())
        return;
    m_points.erase(m_points.begin() + static_cast<ptrdiff_t>(index));
    if (m_points.empty())
        OnMapEmptied();
    NotifyMapChanged();
}

void SyncController::ClearPoints() {
    // An explicit clear also cancels any parked regen, even when the map is
    // already empty: mid-reload (map cleared, regen parked) the user's clear
    // means "do not bring it back".
    m_regenPending = false;
    if (m_points.empty())
        return;
    m_points.clear();
    OnMapEmptied();
    NotifyMapChanged();
}

void SyncController::RestorePoints(std::vector<SyncPoint> points) {
    if (points.empty())
        return;
    m_points = std::move(points);
    NotifyMapChanged();
}

void SyncController::ApplySilently(const std::function<void()>& fn) {
    const bool prev = m_applying;
    m_applying = true;
    fn();
    m_applying = prev;
}

void SyncController::OnMapEmptied() {
    // Plain-anchor mode resumes at the current positions, without a jump.
    if (m_scrollSync)
        RecaptureAnchor();
}

size_t SyncController::SetGeneratedPoints(std::vector<SyncPoint> candidates) {
    m_regenPending = false; // regeneration consumes the parked cue
    const int ref = RefSlot();
    if (ref < 0)
        return 0;
    const size_t r = static_cast<size_t>(ref);
    const size_t erased = std::erase_if(m_points, [](const SyncPoint& p) { return !p.manual; });
    std::stable_sort(candidates.begin(), candidates.end(),
                     [r](const SyncPoint& a, const SyncPoint& b) { return a.page[r] < b.page[r]; });
    // Strictly increasing in EVERY active coordinate. Sorting by one
    // coordinate is enough to build the chain greedily because the accepted
    // points are, by that same predicate, sorted in all of them.
    const auto below = [&](const SyncPoint& a, const SyncPoint& b) {
        for (int s = 0; s < kPaneSlots; ++s)
            if (m_panes[static_cast<size_t>(s)] &&
                !(a.page[static_cast<size_t>(s)] < b.page[static_cast<size_t>(s)]))
                return false;
        return true;
    };
    size_t inserted = 0;
    for (SyncPoint& c : candidates) {
        c.manual = false;
        const auto at = std::lower_bound(m_points.begin(), m_points.end(), c.page[r],
                                         [r](const SyncPoint& p, int v) { return p.page[r] < v; });
        // Accept only strictly between its neighbors: one predicate is the
        // greedy monotone filter (out-of-order bookmarks are dropped) AND the
        // manual-wins merge rule.
        if (at != m_points.begin() && !below(*(at - 1), c))
            continue;
        if (at != m_points.end() && !below(c, *at))
            continue;
        m_points.insert(at, std::move(c));
        ++inserted;
    }
    if (m_points.empty())
        OnMapEmptied(); // had only stale generated points and kept none
    if (erased > 0 || inserted > 0)
        NotifyMapChanged();
    return inserted;
}

void SyncController::DriveFollowers(PaneWindow& leader) {
    // With a map and alignment gaps enabled every layout is slot-aligned, so
    // the exchange is IDENTITY on virtual coordinates: a follower scrolls
    // THROUGH its gaps 1:1. Otherwise the piecewise map (or the plain anchor)
    // drives real-page units - the gaps-off waiting behavior. The gap-epoch
    // check closes the reload window: the restore dance fires Scrolled after
    // SetPages cleared the reloading pane's gaps (its version resets to 0)
    // but BEFORE DocumentOpened clears the map, and driving mismatched slot
    // layouts in slot units would yank the followers; MapTarget's real-page
    // units are layout-shape-invariant, so falling back is always safe.
    const int lead = SlotOf(leader);
    if (lead < 0)
        return;
    if (!m_points.empty() && m_gapsEnabled && SlotGridsAligned()) {
        const double virt = leader.VirtualSyncPosition();
        for (int s = 0; s < kPaneSlots; ++s)
            if (m_panes[static_cast<size_t>(s)] && s != lead)
                m_panes[static_cast<size_t>(s)]->ScrollToVirtualSyncPosition(virt);
        return;
    }
    const double pos = leader.SyncPosition();
    // Plain anchor: out to reference units, then back out to each follower.
    // Unjoined panes are skipped, not driven: their anchor entries were never
    // captured (RecaptureAnchor confines itself to the joined subset).
    const double base = pos - m_anchor[static_cast<size_t>(lead)];
    for (int s = 0; s < kPaneSlots; ++s) {
        PaneWindow* p = m_panes[static_cast<size_t>(s)];
        if (!p || s == lead || !m_joined[static_cast<size_t>(s)])
            continue;
        p->ScrollToSyncPosition(
            m_points.empty() ? base + m_anchor[static_cast<size_t>(s)] : MapTarget(lead, s, pos));
    }
}

double SyncController::MapTarget(int leadSlot, int followSlot, double pos) const {
    // Piecewise-constant integer delta: last point whose leading coordinate
    // <= pos rules the segment (before the first point its delta
    // extrapolates). The follower is clamped just short of its NEXT point, so
    // it waits on the last page of its own section while the leader crosses
    // pages with no counterpart, and resumes seamlessly when the leader
    // reaches the point; leading from the short side instead jumps the
    // surplus in one block. The two directions are therefore not exact
    // inverses: m_applying stops the echo, and every scroll re-drives the
    // followers from the leader's authoritative position anyway.
    //
    // The epsilon exists to ATTRIBUTE the wait position to the section's last
    // page, and page attribution happens after a page-fraction -> pixel ->
    // page round trip (ScrollToSyncPosition quantizes the scroll to whole
    // pixels, SyncPosition reads the page back under the viewport center), so
    // it must survive that quantization: a sub-pixel epsilon parks the center
    // ON the page boundary and the counter shows the NEXT section's first
    // page on some DPI/zoom combinations (seen at 96-DPI RDP metrics). 1% of
    // a page stays >= 1px down to ~100px page heights and is still visually
    // "the end of the page".
    //
    // The map is sorted by the reference coordinate, but strict monotonicity
    // in every coordinate makes it sorted by the LEADING one too, so the
    // binary search below is valid whichever pane leads.
    constexpr double kNextPointEps = 0.01;
    const size_t li = static_cast<size_t>(leadSlot);
    const size_t fi = static_cast<size_t>(followSlot);
    const auto next = std::upper_bound(
        m_points.begin(), m_points.end(), pos,
        [li](double v, const SyncPoint& p) { return v < p.page[li]; });
    const SyncPoint& seg = (next == m_points.begin()) ? m_points.front() : *(next - 1);
    double target = pos + (seg.page[fi] - seg.page[li]);
    if (next != m_points.begin() && next != m_points.end())
        target = std::min(target, static_cast<double>(next->page[fi]) - kNextPointEps);
    return target;
}

void SyncController::RealignFollower(PaneWindow& leader) {
    if (!m_scrollSync || m_points.empty())
        return;
    if (SlotOf(leader) < 0 || !AllPanesReady())
        return;
    // Save/restore, not set/clear: a caller already inside ApplySilently must
    // not find its guard dropped on return.
    ApplySilently([&] { DriveFollowers(leader); });
}

void SyncController::OnViewChanged(PaneWindow& source, PaneWindow::ViewEvent event,
                                   float /*zoomRatio*/) {
    if (event == PaneWindow::ViewEvent::FitZoomChanged) {
        // A fit recomputation moved this pane's zoom without a user gesture
        // (resize, splitter drag, Ctrl+2/3): refresh the pairing instead of
        // driving the siblings, or the next real zoom gesture would apply a
        // stale ratio as a discontinuous jump. Handled BEFORE the m_applying
        // guard: a silent alignment-gap relayout can move a fit zoom too (the
        // v-bar prediction counts gap heights), and recapturing never drives
        // anything, so it is safe at any moment.
        if (m_zoomSync)
            RecaptureZoomAnchor();
        return;
    }

    if (event == PaneWindow::ViewEvent::DocumentOpening) {
        // A (re)open just STARTED: the pane leaves the joined set immediately.
        // An already-joined pane re-opening (auto-reload, swap and rotation,
        // an MRU open over a live document) would otherwise keep leading with
        // anchors captured for the PREVIOUS document, and the pre-gate
        // FitZoomChanged recapture would sample its unsettled zoom. It
        // rejoins at its DocumentOpened, position final. Handled BEFORE the
        // m_applying guard: leaving must never be swallowed.
        const int opening = SlotOf(source);
        if (opening >= 0)
            m_joined[static_cast<size_t>(opening)] = false;
        return;
    }

    if (m_applying)
        return;

    if (event == PaneWindow::ViewEvent::FocusGained)
        return; // focus changes are not view movements

    if (event == PaneWindow::ViewEvent::DocumentOpened) {
        // A (re)opened document invalidates the captured pairing AND the point
        // map (points reference the previous pagination; MainWindow re-derives
        // generated points after an auto-reload). Park the regen cue BEFORE
        // clearing - and keep it parked across further DocumentOpened events
        // (several panes reloading from one build, a failed reload in between)
        // - or the second event would find an empty map and drop the cue.
        // This is also the ONE place a pane joins (or, closed/failed, leaves)
        // the anchor sync: its restored view is final only now, so the
        // recaptures below fold it in at the settled position.
        const int opened = SlotOf(source);
        if (opened >= 0)
            m_joined[static_cast<size_t>(opened)] = source.HasDocument();
        m_regenPending = m_regenPending || HasAutoPoints();
        const bool hadPoints = !m_points.empty();
        m_points.clear();
        if (m_scrollSync)
            RecaptureAnchor();
        if (m_zoomSync)
            RecaptureZoomAnchor();
        // LAST, with coherent state: the frame's reaction relayouts the panes
        // (gap collapse) and may re-enter with view events.
        if (hadPoints)
            NotifyMapChanged();
        return;
    }

    // The source must have JOINED, but the other active panes need not: sync
    // operates on the joined subset, so an empty third pane (unopened centre,
    // cancelled picker, closed or failed document) never suspends the pairing
    // of the loaded ones. Requiring joined - not merely HasDocument() - also
    // discards the source's OWN restore echo: a (re)opening pane fires
    // Scrolled while its saved view lands, BEFORE its DocumentOpened, and
    // driving the siblings through its never-captured anchor entry would
    // corrupt their established alignment.
    const int src = SlotOf(source);
    if (src < 0 || !m_joined[static_cast<size_t>(src)])
        return;

    m_applying = true;

    if (event == PaneWindow::ViewEvent::Zoomed && m_zoomSync) {
        // Drive an absolute target derived from the zoom anchor. Forwarding
        // only the per-event ratio would be silently dropped while a sibling
        // sits clamped at kMin/kMaxZoom, and the reverse ratios WOULD apply on
        // the way back, corrupting the relationship permanently.
        const float base = source.Zoom() / m_zoomAnchor[static_cast<size_t>(src)];
        for (int s = 0; s < kPaneSlots; ++s) {
            PaneWindow* other = m_panes[static_cast<size_t>(s)];
            if (!other || s == src || !m_joined[static_cast<size_t>(s)])
                continue;
            const float target = base * m_zoomAnchor[static_cast<size_t>(s)];
            other->ApplyZoomRatio(target / other->Zoom());
        }
    }

    if (m_scrollSync) {
        if (GetKeyState(VK_MENU) < 0) {
            // Alt held: temporary unlock, adjust one pane freely. With the
            // plain anchor the pairing tracks each adjustment (events fire
            // after the source moved), so it is already current when Alt is
            // released. With sync points the map stays authoritative: the
            // deviation is reabsorbed by the next locked scroll, and the
            // permanent-fix flow is Alt-adjust then "Add Sync Point Here".
            if (m_points.empty())
                RecaptureAnchor();
        } else {
            DriveFollowers(source);
        }
    }

    m_applying = false;
}
