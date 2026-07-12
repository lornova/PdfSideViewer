#pragma once

#include "PaneWindow.h"
#include "framework.h"

#include <functional>
#include <vector>

// One WinMerge-style synchronization point: a pair of WHOLE pages the user
// (or the bookmark matcher) declared aligned. label carries the numbering
// key ("1.2") for generated points, empty for manual ones (display only).
struct SyncPoint {
    int left = 0;
    int right = 0;
    bool manual = false; // manual points survive re-generation and win conflicts
    std::wstring label;
};

// Mediates the two panes' views. Positions are exchanged in page units
// (pageIndex + fraction, sampled at the viewport center), so documents with
// different page formats and different zoom levels stay aligned (R2), and the
// pairing is a user-adjustable delta anchor captured at lock time (R1):
// pre-scroll each pane, enable sync, and the offset is preserved. Holding Alt
// scrolls only the focused pane while the anchor tracks the adjustment. Zoom
// sync keeps its own ratio anchor and drives absolute targets, so clamping at
// the zoom bounds self-heals instead of corrupting the relationship.
//
// Sync points: an ordered list of whole-page pairs turns the single anchor
// into a piecewise-constant INTEGER delta. Alignment is per page (one page of
// one document is one page of the other, like WinMerge's line map), never a
// fractional scroll offset; the within-page fraction transfers unchanged.
// Between two points the follower is clamped just short of its next point, so
// it WAITS at the end of its own section while the leader crosses pages that
// have no counterpart, and resumes seamlessly when the leader reaches the
// point. The empty map degenerates to the plain anchor, bit-identical to the
// behavior described above (and Alt/re-lock recapture only applies then: a
// non-empty map is authoritative). Invariant: points strictly increase in
// BOTH coordinates.
class SyncController {
public:
    SyncController(PaneWindow& left, PaneWindow& right) : m_left(left), m_right(right) {}

    void SetScrollSync(bool on) {
        m_scrollSync = on;
        if (on && m_points.empty()) // a non-empty point map is authoritative
            RecaptureAnchor();
    }
    void SetZoomSync(bool on) {
        m_zoomSync = on;
        if (on)
            RecaptureZoomAnchor();
    }
    bool ScrollSync() const { return m_scrollSync; }
    bool ZoomSync() const { return m_zoomSync; }

    // ------------------------------------------------------------- sync points
    const std::vector<SyncPoint>& Points() const { return m_points; }
    bool HasPoints() const { return !m_points.empty(); }
    bool HasAutoPoints() const; // any generated point: drives post-reload regeneration
    // Captures both panes' current pages as a manual point. The new point
    // wins: existing points that would break the both-coordinates
    // monotonicity are removed. False when either pane has no document.
    bool AddPointHere();
    void RemovePoint(size_t index); // emptying the map resumes the plain anchor
    void ClearPoints();
    // Replaces the generated points; manual points are kept and win on
    // conflict. Candidates may arrive unsorted or inconsistent: the greedy
    // monotone subset (in left order) survives. Returns how many candidates
    // were inserted. Consumes any parked regen.
    size_t SetGeneratedPoints(std::vector<SyncPoint> candidates);
    // Parked regeneration cue: DocumentOpened clears the map, but an auto map
    // must survive a reload of BOTH panes (two DocumentOpened in a row, the
    // first with the sibling still opening) and a failed intermediate reload
    // (broken half-written compile), so "the map had generated points" is
    // remembered here until MainWindow either regenerates or cancels it on a
    // path change / explicit clear.
    bool AutoRegenPending() const { return m_regenPending; }
    void CancelAutoRegen() { m_regenPending = false; }
    // One-shot: drive the sibling of `leader` to the mapped position (visible
    // feedback right after generation). No-op unless scroll-syncing with a
    // non-empty map and both panes have documents.
    void RealignFollower(PaneWindow& leader);

    // Fired after EVERY change to the point map, including the implicit clear
    // on DocumentOpened (which has no frame-visible call site): the frame
    // reacts by rebuilding alignment gaps and marker lists for both panes.
    void SetMapChangedHandler(std::function<void()> handler) {
        m_onMapChanged = std::move(handler);
    }
    // Mirrors the "Show Alignment Gaps" menu toggle. Gaps on + non-empty map:
    // the panes' layouts are slot-aligned and scroll sync is IDENTITY on
    // virtual (slot) coordinates; off = the waiting behavior of MapTarget.
    void SetAlignmentGapsEnabled(bool on) { m_gapsEnabled = on; }
    bool AlignmentGapsEnabled() const { return m_gapsEnabled; }
    // Reinstalls a complete map wholesale (swap mirroring). The caller
    // guarantees the both-coordinates-increasing invariant. Fires the map
    // change; touches neither the anchors, the sync flags nor the parked
    // regen (mirrored generated points re-arm regen via HasAutoPoints on the
    // next reload). No-op on empty input.
    void RestorePoints(std::vector<SyncPoint> points);
    // Routes programmatic pane moves (gap rebuilds) past the sync echo,
    // exactly like the controller's own writes. Reentrant-safe.
    void ApplySilently(const std::function<void()>& fn);
    // Re-captures the zoom pairing at the panes' current zooms: the frame
    // uses it after driving BOTH panes to an absolute preset (zoom sync on),
    // which bypasses the normal ratio routing.
    void ResyncZoomAnchor() { RecaptureZoomAnchor(); }

    void OnViewChanged(PaneWindow& source, PaneWindow::ViewEvent event, float zoomRatio);

private:
    void RecaptureAnchor();
    void RecaptureZoomAnchor();
    double MapTarget(bool leftLeads, double pos) const; // requires !m_points.empty()
    void OnMapEmptied();
    void NotifyMapChanged();
    void DriveFollower(PaneWindow& leader, PaneWindow& follower); // caller holds m_applying

    PaneWindow& m_left;
    PaneWindow& m_right;
    bool m_scrollSync = false;
    bool m_zoomSync = false;
    bool m_applying = false; // reentrancy guard: our own writes echo back here
    double m_anchor = 0;     // right position - left position, page units
    float m_zoomAnchor = 1.0f; // right zoom / left zoom
    std::vector<SyncPoint> m_points; // sorted; strictly increasing in BOTH coords
    bool m_regenPending = false;     // see AutoRegenPending()
    bool m_gapsEnabled = true;       // see SetAlignmentGapsEnabled()
    std::function<void()> m_onMapChanged;
};
