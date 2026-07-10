#include "view/SyncController.h"

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
        // A (re)opened document invalidates the captured pairing; recapture at
        // the current positions instead of yanking the sibling around.
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
            // Alt held: temporary unlock, adjust one pane freely. The anchor
            // tracks each adjustment (events fire after the source moved), so
            // it is already current when Alt is released and the first locked
            // scroll propagates normally instead of being swallowed.
            RecaptureAnchor();
        } else {
            const double pos = source.SyncPosition();
            const double target = (&source == &m_left) ? pos + m_anchor : pos - m_anchor;
            other.ScrollToSyncPosition(target);
        }
    }

    m_applying = false;
}
