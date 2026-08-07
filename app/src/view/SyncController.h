#pragma once

#include "PaneSlots.h"
#include "PaneWindow.h"
#include "framework.h"

#include <functional>
#include <vector>

// One WinMerge-style synchronization point: the WHOLE pages the user (or the
// bookmark matcher) declared aligned, one per pane SLOT. Entries for inactive
// slots are unused. label carries the numbering key ("1.2") for generated
// points, empty for manual ones (display only).
struct SyncPoint {
    PerPane<int> page{};
    bool manual = false; // manual points survive re-generation and win conflicts
    std::wstring label;
};

// Mediates the active panes' views. Positions are exchanged in page units
// (pageIndex + fraction, sampled at the viewport center), so documents with
// different page formats and different zoom levels stay aligned (R2), and the
// pairing is a user-adjustable delta anchor captured at lock time (R1):
// pre-scroll each pane, enable sync, and the offset is preserved. Holding Alt
// scrolls only the focused pane while the anchor tracks the adjustment. Zoom
// sync keeps its own ratio anchor and drives absolute targets, so clamping at
// the zoom bounds self-heals instead of corrupting the relationship.
//
// Both anchors are stored RELATIVE TO A REFERENCE SLOT (the first JOINED
// pane), not as one scalar: a leader converts its position to reference units
// and every follower converts back out. With two panes that is arithmetically
// the single "right minus left" delta of the pre-slot design.
// Anchor sync operates on the JOINED subset of the active panes: a pane joins
// only when its DocumentOpened has been PROCESSED here, not as soon as it
// holds a document. The distinction is load-bearing: a (re)opening pane
// reports HasDocument() while its saved view is still being restored, and the
// restore's own Scrolled echo would otherwise drive the established panes
// through a never-captured anchor entry, corrupting their alignment right
// before the DocumentOpened recapture freezes the damage in. An empty pane
// (unopened centre, closed or failed document) neither anchors nor follows,
// and the loaded ones keep syncing around it. Point-map operations, by
// contrast, require every active pane to hold a document (a tuple has one
// coordinate per active slot).
//
// Sync points: an ordered list of whole-page tuples turns the anchors into a
// piecewise-constant INTEGER delta. Alignment is per page (one page of one
// document is one page of the others, like WinMerge's line map), never a
// fractional scroll offset; the within-page fraction transfers unchanged.
// Between two points a follower is clamped just short of its next point, so it
// WAITS at the end of its own section while the leader crosses pages that have
// no counterpart, and resumes seamlessly when the leader reaches the point. The
// empty map degenerates to the plain anchor, bit-identical to the behavior
// described above (and Alt/re-lock recapture only applies then: a non-empty map
// is authoritative). Invariant: points strictly increase in EVERY active
// coordinate.
class SyncController {
public:
    // Inactive slots are nullptr. Safe at any time: the anchors are recaptured
    // by the callers that already do so on every document/mode change. Panes
    // installed with a document have already been through their DocumentOpened
    // (a mode switch reuses live panes), so they join immediately; a pane
    // still restoring joins on ITS DocumentOpened, never before.
    void SetPanes(const PerPane<PaneWindow*>& panes) {
        m_panes = panes;
        for (int s = 0; s < kPaneSlots; ++s) {
            PaneWindow* p = m_panes[static_cast<size_t>(s)];
            m_joined[static_cast<size_t>(s)] = p && p->HasDocument();
        }
    }
    const PerPane<PaneWindow*>& Panes() const { return m_panes; }

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
    // Captures every active pane's current page as a manual point. The new
    // point wins: existing points that would break the all-coordinates
    // monotonicity are removed. False unless every active pane has a document.
    bool AddPointHere();
    // Index of the point whose tuple is exactly the pages the active panes are
    // showing right now, -1 if there is none (and whenever AddPointHere would
    // refuse, so one call answers both "is the button pressed" and "does the
    // command add or remove"). Cheap enough for the frame to poll it per scroll
    // tick: a binary search on the reference coordinate, then one comparison.
    int PointIndexHere() const;
    void RemovePoint(size_t index); // emptying the map resumes the plain anchor
    void ClearPoints();
    // Replaces the generated points; manual points are kept and win on
    // conflict. Candidates may arrive unsorted or inconsistent: the greedy
    // monotone subset (in reference-slot order) survives. Returns how many
    // candidates were inserted. Consumes any parked regen.
    size_t SetGeneratedPoints(std::vector<SyncPoint> candidates);
    // Parked regeneration cue: DocumentOpened clears the map, but an auto map
    // must survive a reload of every pane (several DocumentOpened in a row, the
    // first with the siblings still opening) and a failed intermediate reload
    // (broken half-written compile), so "the map had generated points" is
    // remembered here until MainWindow either regenerates or cancels it on a
    // path change / explicit clear.
    bool AutoRegenPending() const { return m_regenPending; }
    void CancelAutoRegen() { m_regenPending = false; }
    // One-shot: drive every follower of `leader` to the mapped position
    // (visible feedback right after generation). No-op unless scroll-syncing
    // with a non-empty map and every active pane has a document.
    void RealignFollower(PaneWindow& leader);

    // Fired after EVERY change to the point map, including the implicit clear
    // on DocumentOpened (which has no frame-visible call site): the frame
    // reacts by rebuilding alignment gaps and marker lists for every pane.
    void SetMapChangedHandler(std::function<void()> handler) {
        m_onMapChanged = std::move(handler);
    }
    // Mirrors the "Show Alignment Gaps" menu toggle. Gaps on + non-empty map:
    // the panes' layouts are slot-aligned and scroll sync is IDENTITY on
    // virtual (slot) coordinates; off = the waiting behavior of MapTarget.
    void SetAlignmentGapsEnabled(bool on) { m_gapsEnabled = on; }
    bool AlignmentGapsEnabled() const { return m_gapsEnabled; }
    // Reinstalls a complete map wholesale (swap mirroring). The caller
    // guarantees the all-coordinates-increasing invariant. Fires the map
    // change; touches neither the anchors, the sync flags nor the parked
    // regen (mirrored generated points re-arm regen via HasAutoPoints on the
    // next reload). No-op on empty input.
    void RestorePoints(std::vector<SyncPoint> points);
    // Routes programmatic pane moves (gap rebuilds) past the sync echo,
    // exactly like the controller's own writes. Reentrant-safe.
    void ApplySilently(const std::function<void()>& fn);
    // Re-captures the zoom pairing at the panes' current zooms: the frame
    // uses it after driving EVERY pane to an absolute preset (zoom sync on),
    // which bypasses the normal ratio routing.
    void ResyncZoomAnchor() { RecaptureZoomAnchor(); }

    void OnViewChanged(PaneWindow& source, PaneWindow::ViewEvent event, float zoomRatio);

    // The slot a pane occupies, -1 if it is not an active pane of this
    // controller. Public: the frame maps point tuples the same way.
    int SlotOf(const PaneWindow& pane) const;
    // First active slot: the coordinate the point map is sorted by (with a
    // non-empty map every active pane holds a document, so it coincides with
    // the anchors' ready reference below).
    int RefSlot() const;

private:
    // First JOINED slot, -1 if none: the base the anchors are captured against.
    int JoinedRefSlot() const;
    void RecaptureAnchor();
    void RecaptureZoomAnchor();
    // Requires !m_points.empty(); both slots must be active.
    double MapTarget(int leadSlot, int followSlot, double pos) const;
    void OnMapEmptied();
    void NotifyMapChanged();
    void DriveFollowers(PaneWindow& leader); // caller holds m_applying
    bool AllPanesReady() const;              // every active pane has a document
    // The whole page every active pane is showing, by slot (inactive slots stay
    // 0). One definition of "the pages here" for both the add and the lookup.
    PerPane<int> PagesHere() const;
    // Every active pane carries the same NONZERO alignment-gap epoch, so the
    // slot grids are known to be mutually consistent. All-or-nothing on
    // purpose: driving one follower in slot units and another in real-page
    // units would put two different alignments on screen at once.
    bool SlotGridsAligned() const;

    PerPane<PaneWindow*> m_panes{};
    // A pane participates in anchor sync only once ITS DocumentOpened was
    // processed (see the class comment): set there, cleared the moment a
    // (re)open STARTS (DocumentOpening), sampled by SetPanes on install.
    PerPane<bool> m_joined{};
    bool m_scrollSync = false;
    bool m_zoomSync = false;
    bool m_applying = false; // reentrancy guard: our own writes echo back here
    PerPane<double> m_anchor{};                      // position - reference position, page units
    PerPane<float> m_zoomAnchor{1.0f, 1.0f, 1.0f};   // zoom / reference zoom
    std::vector<SyncPoint> m_points;  // sorted; strictly increasing in EVERY active coord
    bool m_regenPending = false;      // see AutoRegenPending()
    bool m_gapsEnabled = true;        // see SetAlignmentGapsEnabled()
    std::function<void()> m_onMapChanged;
};
