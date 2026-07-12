#include "view/SyncController.h"

#include <algorithm>

void SyncController::RecaptureAnchor() {
    if (m_left.HasDocument() && m_right.HasDocument())
        m_anchor = m_right.SyncPosition() - m_left.SyncPosition();
    else
        m_anchor = 0;
}

void SyncController::RecaptureZoomAnchor() {
    if (m_left.HasDocument() && m_right.HasDocument())
        m_zoomAnchor = m_right.Zoom() / m_left.Zoom(); // zoom is always >= kMinZoom
    else
        m_zoomAnchor = 1.0f;
}

bool SyncController::HasAutoPoints() const {
    for (const SyncPoint& p : m_points)
        if (!p.manual)
            return true;
    return false;
}

bool SyncController::AddPointHere() {
    if (!m_left.HasDocument() || !m_right.HasDocument())
        return false;
    const int l = static_cast<int>(m_left.SyncPosition());
    const int r = static_cast<int>(m_right.SyncPosition());
    // The new point wins: drop every point not strictly on one side of it in
    // BOTH coordinates (this also covers exact duplicates).
    std::erase_if(m_points, [l, r](const SyncPoint& p) {
        return !((p.left < l && p.right < r) || (p.left > l && p.right > r));
    });
    const auto at = std::lower_bound(m_points.begin(), m_points.end(), l,
                                     [](const SyncPoint& p, int v) { return p.left < v; });
    m_points.insert(at, SyncPoint{l, r, true, {}});
    return true;
}

void SyncController::RemovePoint(size_t index) {
    if (index >= m_points.size())
        return;
    m_points.erase(m_points.begin() + static_cast<ptrdiff_t>(index));
    if (m_points.empty())
        OnMapEmptied();
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
}

void SyncController::OnMapEmptied() {
    // Plain-anchor mode resumes at the current positions, without a jump.
    if (m_scrollSync)
        RecaptureAnchor();
}

size_t SyncController::SetGeneratedPoints(std::vector<SyncPoint> candidates) {
    m_regenPending = false; // regeneration consumes the parked cue
    std::erase_if(m_points, [](const SyncPoint& p) { return !p.manual; });
    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const SyncPoint& a, const SyncPoint& b) { return a.left < b.left; });
    size_t inserted = 0;
    for (SyncPoint& c : candidates) {
        c.manual = false;
        const auto at = std::lower_bound(m_points.begin(), m_points.end(), c.left,
                                         [](const SyncPoint& p, int v) { return p.left < v; });
        // Accept only strictly between its neighbors in BOTH coordinates: one
        // predicate is the greedy monotone filter (out-of-order bookmarks are
        // dropped) AND the manual-wins merge rule.
        if (at != m_points.begin()) {
            const SyncPoint& prev = *(at - 1);
            if (!(prev.left < c.left && prev.right < c.right))
                continue;
        }
        if (at != m_points.end()) {
            const SyncPoint& next = *at;
            if (!(c.left < next.left && c.right < next.right))
                continue;
        }
        m_points.insert(at, std::move(c));
        ++inserted;
    }
    if (m_points.empty())
        OnMapEmptied(); // had only stale generated points and kept none
    return inserted;
}

double SyncController::MapTarget(bool leftLeads, double pos) const {
    // Piecewise-constant integer delta: last point whose leading coordinate
    // <= pos rules the segment (before the first point its delta
    // extrapolates). The follower is clamped just short of its NEXT point
    // (same sub-page epsilon SyncPosition uses in paged mode), so it waits on
    // the last page of its own section while the leader crosses pages with no
    // counterpart, and resumes seamlessly when the leader reaches the point;
    // leading from the short side instead jumps the surplus in one block. The
    // two directions are therefore not exact inverses: m_applying stops the
    // echo, and every scroll re-drives the follower from the leader's
    // authoritative position anyway.
    constexpr double kNextPointEps = 0.0001;
    const auto lead = [leftLeads](const SyncPoint& p) { return leftLeads ? p.left : p.right; };
    const auto follow = [leftLeads](const SyncPoint& p) { return leftLeads ? p.right : p.left; };
    const auto next = std::upper_bound(
        m_points.begin(), m_points.end(), pos,
        [&lead](double v, const SyncPoint& p) { return v < lead(p); });
    const SyncPoint& seg = (next == m_points.begin()) ? m_points.front() : *(next - 1);
    double target = pos + (follow(seg) - lead(seg));
    if (next != m_points.begin() && next != m_points.end())
        target = std::min(target, follow(*next) - kNextPointEps);
    return target;
}

void SyncController::RealignFollower(PaneWindow& leader) {
    if (!m_scrollSync || m_points.empty())
        return;
    PaneWindow& other = (&leader == &m_left) ? m_right : m_left;
    if (!leader.HasDocument() || !other.HasDocument())
        return;
    m_applying = true;
    other.ScrollToSyncPosition(MapTarget(&leader == &m_left, leader.SyncPosition()));
    m_applying = false;
}

void SyncController::OnViewChanged(PaneWindow& source, PaneWindow::ViewEvent event,
                                   float /*zoomRatio*/) {
    if (m_applying)
        return;

    if (event == PaneWindow::ViewEvent::FocusGained)
        return; // focus changes are not view movements

    if (event == PaneWindow::ViewEvent::FitZoomChanged) {
        // A fit recomputation moved this pane's zoom without a user gesture
        // (resize, splitter drag, Ctrl+2/3): refresh the pairing instead of
        // driving the sibling, or the next real zoom gesture would apply a
        // stale ratio as a discontinuous jump.
        if (m_zoomSync)
            RecaptureZoomAnchor();
        return;
    }

    if (event == PaneWindow::ViewEvent::DocumentOpened) {
        // A (re)opened document invalidates the captured pairing AND the point
        // map (points reference the previous pagination; MainWindow re-derives
        // generated points after an auto-reload). Park the regen cue BEFORE
        // clearing - and keep it parked across further DocumentOpened events
        // (both panes reloading from one build, a failed reload in between) -
        // or the second event would find an empty map and drop the cue.
        m_regenPending = m_regenPending || HasAutoPoints();
        m_points.clear();
        if (m_scrollSync)
            RecaptureAnchor();
        if (m_zoomSync)
            RecaptureZoomAnchor();
        return;
    }

    PaneWindow& other = (&source == &m_left) ? m_right : m_left;
    if (!source.HasDocument() || !other.HasDocument())
        return;

    m_applying = true;

    if (event == PaneWindow::ViewEvent::Zoomed && m_zoomSync) {
        // Drive an absolute target derived from the zoom anchor. Forwarding
        // only the per-event ratio would be silently dropped while the
        // sibling sits clamped at kMin/kMaxZoom, and the reverse ratios WOULD
        // apply on the way back, corrupting the relationship permanently.
        const float target = (&source == &m_left) ? source.Zoom() * m_zoomAnchor
                                                  : source.Zoom() / m_zoomAnchor;
        other.ApplyZoomRatio(target / other.Zoom());
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
            const bool leftLeads = &source == &m_left;
            const double pos = source.SyncPosition();
            const double target = m_points.empty()
                                      ? (leftLeads ? pos + m_anchor : pos - m_anchor)
                                      : MapTarget(leftLeads, pos);
            other.ScrollToSyncPosition(target);
        }
    }

    m_applying = false;
}
