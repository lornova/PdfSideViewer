#pragma once

#include "DxResources.h"
#include "engine/Document.h"
#include "engine/SyncTex.h"
#include "framework.h"
#include "util/FileWatcher.h"
#include "view/PageLayout.h"

#include <map>
#include <optional>
#include <set>
#include <tuple>

// Posted by the pane's FileWatcher when the open document changed on disk.
// Deliberately OUTSIDE the WM_PSV_FIRST..WM_PSV_LAST drain range: it carries
// no heap payload and must not be treated as a worker result.
constexpr UINT WM_PSV_FILE_CHANGED = WM_APP + 10;

// One side of the split view: a full single-document viewer presenting through
// its own DXGI flip-model swapchain via Direct2D. Owns the Document (worker
// thread), the continuous page layout and the view state (scroll, zoom).
// Drawing happens in device pixels (D2D1_UNIT_MODE_PIXELS).
class PaneWindow {
public:
    static void RegisterWindowClass(HINSTANCE hinst);

    PaneWindow(DxResources& dx, PCWSTR placeholderHint);
    ~PaneWindow();

    PaneWindow(const PaneWindow&) = delete;
    PaneWindow& operator=(const PaneWindow&) = delete;

    void Create(HWND parent, int childId);
    HWND Hwnd() const { return m_hwnd; }

    // Fit modes are virtual zooms recomputed on every relayout (so also on
    // pane resize and DPI change); any manual zoom gesture reverts to Manual.
    enum class ZoomMode { Manual = 0, FitWidth = 1, FitPage = 2 };

    void OpenDocument(std::wstring path);
    // Like OpenDocument, but restores the given view once the document loads.
    void OpenDocumentWithView(std::wstring path, float zoom, float scrollX, float scrollY,
                              ZoomMode zoomMode);
    // Back to the empty placeholder; the worker drops the fz_document (and
    // with it the file handle). Fires DocumentOpened so the frame refreshes
    // outline/status like any other document-state change.
    void CloseDocument();
    void SetDarkMode(bool dark);
    // Live language switch: the other placeholder texts are composed at paint
    // time (so the repaint alone refreshes them), but the empty-state hint is
    // captured at construction.
    void SetPlaceholderHint(PCWSTR hint) {
        m_hint = hint;
        Invalidate();
    }
    void OnDpiChanged(UINT dpi);
    void SetZoomMode(ZoomMode mode);
    ZoomMode GetZoomMode() const { return m_zoomMode; }
    // Configurable zoom mode applied to every FRESH document (session/reload
    // restores still override it through OpenDocumentWithView).
    void SetDefaultZoomMode(ZoomMode mode) { m_defaultZoomMode = mode; }
    // Options override for SPI_GETWHEELSCROLLLINES; 0 = use the system value
    // (which is also the only way to get WHEEL_PAGESCROLL semantics).
    void SetWheelLinesOverride(int lines) { m_wheelLinesOverride = lines; }

    // Continuous = classic whole-document scrolling. Paged = the view is
    // clamped to one page and edge scrolling flips pages. The mode is global
    // (MainWindow owns the authoritative copy) but cached per pane because
    // ClampScroll/ContentOrigin/DrawDocument sit on hot paths.
    enum class ScrollMode { Continuous = 0, Paged = 1 };
    void SetScrollMode(ScrollMode mode);
    ScrollMode GetScrollMode() const { return m_scrollMode; }
    // Ctrl+4/Ctrl+5 in a pane select the global mode (continuous / paged);
    // the pane knows no command ids, so the frame injects the action (same
    // pattern as m_openSibling).
    void SetScrollModeRequestHandler(std::function<void(ScrollMode)> handler) {
        m_onScrollModeRequest = std::move(handler);
    }

    // ------------------------------------------------------------- sync API --
    // Positions are in page units (pageIndex + fraction within the page),
    // sampled at the viewport center: format- and zoom-independent, so two
    // documents with different page sizes stay aligned page-for-page.
    // FitZoomChanged: a fit recomputation moved the zoom without a user
    // gesture (resize, splitter drag); sync must refresh anchors, not
    // propagate, or the stale ratio causes discontinuous jumps.
    enum class ViewEvent { Scrolled, Zoomed, DocumentOpened, FocusGained, FitZoomChanged };
    using ViewChangedHandler = std::function<void(PaneWindow&, ViewEvent, float zoomRatio)>;
    void SetViewChangedHandler(ViewChangedHandler handler) {
        m_onViewChanged = std::move(handler);
    }
    void SetOpenSiblingHandler(std::function<void(std::wstring)> handler) {
        m_openSibling = std::move(handler);
    }
    // Double-click on an empty (or failed) pane asks the frame to show the
    // open dialog for this pane; the pane itself owns no dialogs.
    void SetOpenRequestHandler(std::function<void()> handler) {
        m_onOpenRequest = std::move(handler);
    }
    bool HasDocument() const { return m_state == State::Open && !m_layout.Empty(); }
    int PageCount() const { return m_layout.PageCount(); }
    double SyncPosition() const;
    void ScrollToSyncPosition(double pos);
    // Slot units (slotIndex + fraction at the viewport center). With
    // alignment gaps applied in both panes the layouts are slot-aligned, so
    // scroll sync becomes IDENTITY on these coordinates.
    double VirtualSyncPosition() const;
    void ScrollToVirtualSyncPosition(double pos);
    void ApplyZoomRatio(float ratio);
    void SetManualZoom(float zoom);

    // Alignment gaps (WinMerge-style empty slots) and sync-point markers,
    // both pushed by the frame whenever the sync-point map changes. Gaps
    // relayout with the position preserved in real-page units; markers only
    // repaint (anchor glyphs + scrollbar tick strip). The version stamps the
    // frame's gap epoch: virtual (slot-unit) sync is only valid between two
    // panes holding the SAME nonzero version - a (re)opened pane resets to 0
    // (SetPages cleared its gaps), which closes the reload window where one
    // pane is already gapless while the map still exists.
    void SetAlignmentGaps(std::vector<PageLayout::AlignmentGap> gaps, uint64_t version);
    uint64_t AlignmentGapsVersion() const { return m_gapsVersion; }
    struct SyncMarker {
        int page = 0;
        bool manual = false; // manual points draw opaque, generated ones faded
        std::wstring label;  // numbering key ("1.2"); empty for manual points
    };
    void SetSyncMarkers(std::vector<SyncMarker> markers); // sorted by page
    // Options-dialog visibility switches for the two marker renderings.
    void SetMarkerVisibility(bool anchors, bool ticks) {
        m_showAnchorMarks = anchors;
        m_showTickStrip = ticks;
        HideAnchorTip();
        Invalidate();
    }
    D2D1_SIZE_F PageSizePt(int page) const { return m_layout.PageSizePt(page); }

    // ---------------------------------------------------------- text search --
    using SearchStatusHandler =
        std::function<void(PaneWindow&, int activeMatch, int totalMatches, bool done)>;
    void SetSearchStatusHandler(SearchStatusHandler handler) {
        m_onSearchStatus = std::move(handler);
    }
    void StartSearch(const std::wstring& needle);
    void ClearSearch();
    void GotoMatch(int delta); // +1 next, -1 previous; wraps around

    // ------------------------------------------------------ persisted state --
    const std::wstring& DocumentPath() const { return m_docPath; }
    float Zoom() const { return m_zoom; }
    float ScrollX() const { return m_scrollX; }
    float ScrollY() const { return m_scrollY; }
    // Unlike HasDocument(), a pane still opening (or that failed to open)
    // keeps its intended document for persistence: closing the app mid-open
    // must not wipe it from settings.ini. A deliberate CloseDocument resets
    // to Empty (and the frame drops its session fallback), so a closed pane
    // genuinely persists as empty.
    bool HasPersistableDocument() const { return m_state != State::Empty && !m_docPath.empty(); }
    // While a restore is pending the live zoom/scroll are the reset values;
    // persist the parked restore view instead.
    float PersistZoom() const { return m_hasRestoreView ? m_restoreZoom : m_zoom; }
    float PersistScrollX() const { return m_hasRestoreView ? m_restoreScrollX : m_scrollX; }
    // Normalized to the NO-GAP coordinate space: every restore lands in a
    // gapless layout (SetPages clears the alignment gaps), so saved offsets
    // must be gap-free too. X needs no treatment: gap widths are capped at
    // the real pages' width, so TotalWidth is gap-invariant.
    float PersistScrollY() const;
    ZoomMode PersistZoomMode() const {
        return m_hasRestoreView ? m_restoreZoomMode : m_zoomMode;
    }

    // Document outline (flattened tree; empty when the document has none).
    const std::vector<Document::OutlineItem>& Outline() const { return m_outline; }
    void GotoOutlineItem(int index);

    // ------------------------------------------------------------ page labels --
    // /PageLabels label for a 0-based page (empty when the document has none).
    const std::wstring& PageLabel(int pageIndex) const;
    // The one shared page formatter (status bar, scrollbar tooltip, go-to):
    // "label (N/count)" when a label exists and differs from the ordinal,
    // plain "N / count" otherwise.
    std::wstring FormatPageText(int pageIndex) const;
    // Exact case-insensitive label match; -1 when no label matches.
    int FindPageByLabel(const std::wstring& text) const;
    // Top-aligned jump to a 0-based page (adopts it in paged mode).
    void GotoPage(int pageIndex);

    // ---------------------------------------------------------------- synctex --
    // Inverse search resolved: the frame owns launching the editor. A null
    // hit reports failure; hadData tells "nothing at that position" apart
    // from "no .synctex file at all".
    using InverseSearchHandler =
        std::function<void(PaneWindow&, const SyncTexIndex::InverseHit* hit, bool hadData)>;
    void SetInverseSearchHandler(InverseSearchHandler handler) {
        m_onInverseSearch = std::move(handler);
    }
    // Forward search: scroll to the boxes for texPath:line and flash them.
    // False = no synctex data or the line resolves nowhere in this document.
    bool ForwardSearchTo(const std::wstring& texPath, int line);

private:
    enum class State { Empty, Opening, Open, Error };

    struct CachedBitmap {
        ComPtr<ID2D1Bitmap> bitmap; // last completed render (previews may be stale-scale)
        float bitmapScale = 0;
        uint64_t lastUsed = 0;  // frame counter; untouched tiles get evicted
        uint64_t pendingId = 0; // nonzero while a render request is in flight
        float pendingScale = 0;
        float failedScale = 0; // scale whose bitmap upload failed; do not re-request
    };
    struct TileKey {
        int page;
        int res;
        int row;
        int col;
        friend bool operator<(const TileKey& a, const TileKey& b) {
            return std::tie(a.page, a.res, a.row, a.col) < std::tie(b.page, b.res, b.row, b.col);
        }
    };

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    // Graphics plumbing
    void EnsureSwapChain();
    void CreateTargetBitmap();
    void ReleaseTarget();
    void HandleDeviceLost();
    void ScheduleRecovery();
    void DropDeviceDependentPageBitmaps();
    void OnResize(UINT width, UINT height);
    void Render();
    // m_hwnd may legitimately be null before Create(); InvalidateRect(NULL)
    // would invalidate every window on the desktop.
    void Invalidate() {
        if (m_hwnd)
            InvalidateRect(m_hwnd, nullptr, FALSE);
    }

    // Drawing
    void DrawContent();
    void DrawPlaceholder(ID2D1SolidColorBrush* brush);
    void DrawDocument(ID2D1SolidColorBrush* brush);
    void EnsureTextFormat();

    // Document events (posted by the worker)
    void OnDocOpened(std::unique_ptr<Document::OpenResult> result);
    void OnPageRendered(std::unique_ptr<Document::RenderResult> result);
    void OnTextPage(std::unique_ptr<Document::TextPageResult> result);
    void OnLinks(std::unique_ptr<Document::LinksResult> result);
    void OnSearchResult(std::unique_ptr<Document::SearchResult> result);

    // Text, selection, links (all on UI-side models; no MuPDF calls)
    struct CaretPos {
        int page = 0;
        int line = 0;
        int ch = 0;
        friend auto operator<=>(const CaretPos&, const CaretPos&) = default;
    };
    std::optional<PageLayout::PagePoint> PagePointAt(POINT client) const;
    std::optional<CaretPos> CaretAt(POINT client, bool clampToNearest);
    void EnsureTextPage(int page, bool urgent);
    void EnsureLinks(int page);
    bool TextAt(int page, float px, float py) const;
    const Document::LinkInfo* LinkAt(int page, float px, float py) const;
    void ActivateLink(const Document::LinkInfo& link);
    void ClearSelection();
    std::wstring SelectionText() const;
    void CopySelection();
    void DrawOverlays(ID2D1SolidColorBrush* brush, int page, const D2D1_RECT_F& dest,
                      float scale);
    void DrawAnchorMarker(ID2D1SolidColorBrush* brush, const SyncMarker& marker,
                          const D2D1_RECT_F& dest);
    // Shared by the draw and the hover hit test (dest = the page's viewport
    // rect): beside the top-left corner, or inside it when the gutter is thin.
    D2D1_RECT_F AnchorMarkerRect(const D2D1_RECT_F& dest) const;
    void EnsureAnchorTip();
    void UpdateAnchorTip(POINT client); // hover tooltip: the point's numbering
    void HideAnchorTip();
    void NotifySearchStatus();
    void ScrollToMatch(const Document::SearchMatch& match);

    // View state
    float DesiredScale() const { return m_zoom * static_cast<float>(m_dpi) / 72.0f; }
    float DipToPx(float dip) const { return dip * static_cast<float>(m_dpi) / 96.0f; }
    SIZE ViewportPx() const;
    float SyncCenterY() const; // sync sampling height, horizontal-bar-invariant
    D2D1_POINT_2F ContentOrigin() const;
    void RelayoutDocument();
    void UpdateFitZoom(); // recompute m_zoom for FitWidth/FitPage
    void ClampScroll();
    // Paged mode: the scroll range that keeps the current page filling the
    // viewport. fits => lo == hi, the single position centering the page
    // vertically (may be negative for page 0).
    struct PagedBand {
        float lo;
        float hi;
        bool fits;
    };
    PagedBand PagedBandFor(int slot) const;
    bool PagedActive() const {
        return m_scrollMode == ScrollMode::Paged && m_state == State::Open && !m_layout.Empty();
    }
    bool AtBandEdge(const PagedBand& band, int dir) const; // dir: +1 down/next, -1 up/prev
    void AdoptPage(int page); // programmatic jumps make their target the current page
    void AdoptSlot(int slot); // paged flip cursor; a slot may be an alignment gap
    void PagedFlip(int dir);
    void PagedStepY(float dy);
    void PagedWheelY(int delta, float pxPerDetent);
    void ScrollTo(float x, float y);
    void ScrollBy(float dx, float dy);
    void ZoomAt(POINT viewPt, float newZoom);
    void UpdateScrollBars();
    void OnScrollMessage(UINT msg, WPARAM wParam);
    void OnMouseWheel(WPARAM wParam, LPARAM lParam, bool horizontal);
    bool OnKeyDown(WPARAM key);
    static int TileResFor(float pageWpx, float pageHpx);
    void EnsureRequested(CachedBitmap& entry, int page, float scale, int res, int row, int col,
                         bool urgent);
    void EvictStale(int firstKeep, int lastKeep);

    void ResetDocumentState(); // shared clear list: the current document goes away
    // Scrollbar-drag page tooltip ("ix (9/314)" beside the vertical bar).
    void EnsureScrollTip();
    void UpdateScrollTip(float trackY);
    void HideScrollTip();

    void TryReloadDocument();
    void InverseSearchAt(POINT client);
    void FlashSyncTarget(int pageIndex, std::vector<Document::RectPt> rects);

    static constexpr UINT_PTR kRecoveryTimer = 1;
    static constexpr UINT kMaxRecoveryAttempts = 5;
    // Auto-reload: fire this long after the LAST change notification (LaTeX
    // writes the PDF in bursts), then retry on the same cadence while the
    // producer still holds the file.
    static constexpr UINT_PTR kReloadTimer = 2;
    static constexpr UINT kReloadDebounceMs = 500;
    static constexpr UINT kMaxReloadRetries = 20;
    // SyncTeX forward-search flash: transient highlight, one-shot timer.
    static constexpr UINT_PTR kSyncFlashTimer = 3;
    static constexpr UINT kSyncFlashMs = 1500;
    static constexpr float kMinZoom = 0.125f;
    static constexpr float kMaxZoom = 8.0f;
    // Paged mode: pre-event position vs band edge tolerance. Half a pixel
    // absorbs float noise and fractional touchpad residue without mistaking
    // "one pixel short of the edge" for "at the edge".
    static constexpr float kPagedEdgeEps = 0.5f;
    // Pages whose pixel size exceeds ~1.5x this (geometric mean) are split
    // into a 2^res x 2^res tile grid; previews are capped to this size too.
    static constexpr float kMaxTilePx = 2048.0f;

    DxResources& m_dx;
    HWND m_hwnd = nullptr;
    UINT m_dpi = 96;
    unsigned m_dxGeneration = 0; // DxResources::Generation() our resources were built on
    UINT m_recoveryAttempts = 0; // consecutive graphics failures, reset on successful present
    bool m_dark = false;
    bool m_focused = false;
    std::wstring m_hint;
    std::wstring m_docPath;
    std::wstring m_errorText;

    ComPtr<IDXGISwapChain1> m_swapChain;
    ComPtr<ID2D1DeviceContext> m_d2dContext;
    ComPtr<ID2D1Bitmap1> m_targetBitmap;
    ComPtr<IDWriteTextFormat> m_textFormat;
    ComPtr<IDWriteTextFormat> m_markerFormat; // anchor glyph ("Segoe UI Symbol")
    UINT m_textFormatDpi = 0;
    // The dash style belongs to the D2D factory, which Discard() resets too:
    // it follows the device generation, not the DWrite lifetime.
    ComPtr<ID2D1StrokeStyle> m_dashStroke;

    State m_state = State::Empty;
    Document m_doc;
    FileWatcher m_watcher; // auto-reload: watches m_docPath while a document is open
    UINT m_reloadRetries = 0;
    SyncTexIndex m_synctex; // lazy; reset with the document (auto-reload included)
    struct SyncMark {
        int pageIndex = -1; // -1 = no active flash
        std::vector<Document::RectPt> rects;
    } m_syncMark;
    InverseSearchHandler m_onInverseSearch;
    PageLayout m_layout;
    std::vector<Document::OutlineItem> m_outline;
    std::vector<std::wstring> m_pageLabels; // empty when the doc has no /PageLabels
    ZoomMode m_zoomMode = ZoomMode::FitPage;
    float m_zoom = 1.0f;
    float m_scrollX = 0; // content px
    float m_scrollY = 0;
    ScrollMode m_scrollMode = ScrollMode::Continuous;
    int m_currentSlot = 0;  // paged mode: the one SLOT drawn and clamped to (the
                            // flip cursor walks alignment gaps too; render keys,
                            // counter and SyncPosition derive the real page)
    float m_wheelAccum = 0; // paged mode: signed wheel credit toward one detent
    std::vector<SyncMarker> m_syncMarkers; // sorted by page; drives ticks + anchors
    bool m_showAnchorMarks = true; // Options: draw the anchor glyphs
    bool m_showTickStrip = true;   // Options: draw the scrollbar tick strip
    uint64_t m_gapsVersion = 0; // see SetAlignmentGaps; 0 = no valid gap epoch
    ZoomMode m_defaultZoomMode = ZoomMode::FitPage; // for fresh documents
    int m_wheelLinesOverride = 0;                   // 0 = system wheel lines
    HWND m_scrollTip = nullptr; // lazy tracking tooltip for scrollbar drags
    HWND m_anchorTip = nullptr; // lazy tracking tooltip for anchor-mark hovers
    int m_anchorTipPage = -1;   // page whose anchor the tip is showing; -1 = hidden
    std::map<int, CachedBitmap> m_previews; // whole page, capped size; tile fallback
    std::map<TileKey, CachedBitmap> m_tiles; // exact-scale tiles when res > 0
    uint64_t m_frame = 0; // bumped per DrawDocument; drives tile eviction
    uint64_t m_nextRequestId = 1;
    uint64_t m_openGen = 0; // matches the pending OpenAsync; gates OnDocOpened

    ViewChangedHandler m_onViewChanged;
    std::function<void(std::wstring)> m_openSibling; // second file of a multi-drop
    std::function<void(ScrollMode)> m_onScrollModeRequest; // Ctrl+4/5: frame-owned global mode
    std::function<void()> m_onOpenRequest;           // double-click on an empty pane

    // UI-side text/link models for visible (and selection-spanning) pages.
    std::map<int, std::vector<Document::TextLine>> m_textPages;
    std::set<int> m_textPending;
    std::map<int, std::vector<Document::LinkInfo>> m_links;
    std::set<int> m_linksPending;

    bool m_selecting = false; // mouse captured, dragging a selection
    bool m_hasSelection = false;
    CaretPos m_selAnchor;
    CaretPos m_selFocus;
    POINT m_dragStart{};
    bool m_suppressLinkClick = false;  // armed by WM_LBUTTONDBLCLK
    bool m_clickActivatedLink = false; // first click of a double-click navigated

    SearchStatusHandler m_onSearchStatus;
    std::wstring m_searchNeedle;
    std::wstring m_pendingSearch; // typed while the document was still opening
    uint64_t m_searchSeq = 0; // matches Document::StartSearch ids
    std::vector<Document::SearchMatch> m_matches; // page order
    int m_activeMatch = -1;
    bool m_searchDone = true;

    bool m_hasRestoreView = false; // view to apply when the pending open lands
    float m_restoreZoom = 1.0f;
    float m_restoreScrollX = 0;
    float m_restoreScrollY = 0;
    ZoomMode m_restoreZoomMode = ZoomMode::FitWidth;

    // Last values pushed to SetScrollInfo, to avoid re-triggering WM_SIZE.
    int m_sbV[3] = {-1, -1, -1}; // max, page, pos
    int m_sbH[3] = {-1, -1, -1};
    // Bumped on every UpdateScrollBars entry; detects the synchronous WM_SIZE
    // re-entry caused by SetScrollInfo toggling a bar's visibility.
    unsigned m_sbEpoch = 0;
    int m_resizeDepth = 0; // belt-and-braces cap on WM_SIZE re-entry
};
