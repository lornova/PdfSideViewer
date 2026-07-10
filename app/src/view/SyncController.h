#pragma once

#include "PaneWindow.h"
#include "framework.h"

// Mediates the two panes' views. Positions are exchanged in page units
// (pageIndex + fraction, sampled at the viewport center), so documents with
// different page formats and different zoom levels stay aligned (R2), and the
// pairing is a user-adjustable delta anchor captured at lock time (R1):
// pre-scroll each pane, enable sync, and the offset is preserved. Holding Alt
// scrolls only the focused pane while the anchor tracks the adjustment. Zoom
// sync keeps its own ratio anchor and drives absolute targets, so clamping at
// the zoom bounds self-heals instead of corrupting the relationship.
class SyncController {
public:
    SyncController(PaneWindow& left, PaneWindow& right) : m_left(left), m_right(right) {}

    void SetScrollSync(bool on) {
        m_scrollSync = on;
        if (on)
            RecaptureAnchor();
    }
    void SetZoomSync(bool on) {
        m_zoomSync = on;
        if (on)
            RecaptureZoomAnchor();
    }
    bool ScrollSync() const { return m_scrollSync; }
    bool ZoomSync() const { return m_zoomSync; }

    void OnViewChanged(PaneWindow& source, PaneWindow::ViewEvent event, float zoomRatio);

private:
    void RecaptureAnchor();
    void RecaptureZoomAnchor();

    PaneWindow& m_left;
    PaneWindow& m_right;
    bool m_scrollSync = false;
    bool m_zoomSync = false;
    bool m_applying = false; // reentrancy guard: our own writes echo back here
    double m_anchor = 0;     // right position - left position, page units
    float m_zoomAnchor = 1.0f; // right zoom / left zoom
};
