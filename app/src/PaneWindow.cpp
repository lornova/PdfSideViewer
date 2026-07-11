#include "PaneWindow.h"

#include "util/Strings.h"

#include <shellapi.h> // drag & drop

namespace {

constexpr PCWSTR kClassName = L"PsvPaneWindow";
constexpr float kScaleEpsilon = 0.001f;

std::wstring FileNameOf(const std::wstring& path) {
    const size_t pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? path : path.substr(pos + 1);
}

bool SameScale(float a, float b) {
    return std::abs(a - b) < kScaleEpsilon;
}

bool IsWordChar(uint32_t cp) {
    if (cp > 0xFFFF)
        return true; // beyond the BMP: treat as word content
    return cp == L'_' || iswalnum(static_cast<wint_t>(cp)) != 0;
}

void AppendCodepoint(std::wstring& s, uint32_t cp) {
    if (cp < 0x10000) {
        s.push_back(static_cast<wchar_t>(cp));
    } else {
        cp -= 0x10000;
        s.push_back(static_cast<wchar_t>(0xD800 + (cp >> 10)));
        s.push_back(static_cast<wchar_t>(0xDC00 + (cp & 0x3FF)));
    }
}

} // namespace

void PaneWindow::RegisterWindowClass(HINSTANCE hinst) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS; // double-click = select word
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hinst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);
}

PaneWindow::PaneWindow(DxResources& dx, PCWSTR placeholderHint) : m_dx(dx), m_hint(placeholderHint) {}

PaneWindow::~PaneWindow() {
    m_doc.Shutdown();
}

void PaneWindow::Create(HWND parent, int childId) {
    CreateWindowExW(0, kClassName, nullptr,
                    WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_VSCROLL | WS_HSCROLL, 0, 0, 0, 0,
                    parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(childId)),
                    reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(parent, GWLP_HINSTANCE)), this);
}

void PaneWindow::ResetDocumentState() {
    m_errorText.clear();
    m_previews.clear();
    m_tiles.clear();
    m_outline.clear();
    m_zoomMode = ZoomMode::FitPage; // friendly default for a fresh document
    m_textPages.clear();
    m_textPending.clear();
    m_links.clear();
    m_linksPending.clear();
    ClearSelection();
    m_searchNeedle.clear();
    m_pendingSearch.clear();
    ++m_searchSeq;
    m_matches.clear();
    m_activeMatch = -1;
    m_searchDone = true;
    NotifySearchStatus();
    m_layout.Clear();
    m_scrollX = 0;
    m_scrollY = 0;
    m_zoom = 1.0f;
    m_currentPage = 0;
    m_wheelAccum = 0;
    // The .synctex regenerates with every build; auto-reload funnels through
    // here too, so the index lazily re-parses on the first query afterwards.
    m_synctex.Reset();
    m_syncMark = {};
    KillTimer(m_hwnd, kSyncFlashTimer);
    KillTimer(m_hwnd, kReloadTimer);
    m_reloadRetries = 0;
}

void PaneWindow::OpenDocument(std::wstring path) {
    m_hasRestoreView = false;
    m_docPath = path;
    m_state = State::Opening;
    m_doc.CancelSearch();
    ResetDocumentState();
    // Watch from Opening on (not from success): a failed open self-heals when
    // the file changes again (e.g. the next LaTeX run produces a valid PDF),
    // and an old watch must never outlive a path switch.
    m_watcher.Watch(m_hwnd, WM_PSV_FILE_CHANGED, m_docPath);
    m_openGen = m_doc.OpenAsync(std::move(path));
    UpdateScrollBars();
    Invalidate();
}

void PaneWindow::CloseDocument() {
    if (m_state == State::Empty)
        return;
    m_hasRestoreView = false;
    m_docPath.clear();
    m_state = State::Empty;
    // An Open still RUNNING in the worker posts its result regardless of the
    // queue purge; 0 never matches a real generation, so it cannot land.
    m_openGen = 0;
    m_watcher.Stop();
    m_doc.CloseAsync();
    ResetDocumentState();
    UpdateScrollBars();
    Invalidate();
    if (m_onViewChanged)
        m_onViewChanged(*this, ViewEvent::DocumentOpened, 1.0f);
}

void PaneWindow::OpenDocumentWithView(std::wstring path, float zoom, float scrollX,
                                      float scrollY, ZoomMode zoomMode) {
    OpenDocument(std::move(path));
    m_hasRestoreView = true;
    m_restoreZoom = std::clamp(zoom, kMinZoom, kMaxZoom);
    m_restoreScrollX = scrollX;
    m_restoreScrollY = scrollY;
    m_restoreZoomMode = zoomMode;
}

void PaneWindow::TryReloadDocument() {
    if (m_docPath.empty())
        return;
    // Stability probe: a deny-write open fails while the producer (a LaTeX
    // run, a download) is still writing, and a clean+rebuild cycle leaves a
    // window with no file at all. Both retry on the debounce cadence; any
    // further notification re-arms everything anyway. Reloading the current
    // view goes through the session-restore path, so position, zoom and fit
    // mode survive and sync re-anchors on DocumentOpened.
    const HANDLE probe = CreateFileW(m_docPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                     OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (probe == INVALID_HANDLE_VALUE) {
        const DWORD err = GetLastError();
        const bool transient = err == ERROR_SHARING_VIOLATION || err == ERROR_LOCK_VIOLATION ||
                               err == ERROR_FILE_NOT_FOUND || err == ERROR_ACCESS_DENIED;
        if (transient && ++m_reloadRetries <= kMaxReloadRetries)
            SetTimer(m_hwnd, kReloadTimer, kReloadDebounceMs, nullptr);
        return; // keep showing the current render; never flash an error state
    }
    CloseHandle(probe);
    m_reloadRetries = 0;
    OpenDocumentWithView(m_docPath, PersistZoom(), PersistScrollX(), PersistScrollY(),
                         PersistZoomMode());
}

void PaneWindow::SetZoomMode(ZoomMode mode) {
    if (mode == m_zoomMode)
        return;
    m_zoomMode = mode;
    if (m_state == State::Open) {
        const double pos = SyncPosition();
        RelayoutDocument(); // recomputes the fit zoom
        ScrollToSyncPosition(pos);
    }
    Invalidate();
}

void PaneWindow::SetScrollMode(ScrollMode mode) {
    if (mode == m_scrollMode)
        return;
    m_scrollMode = mode;
    m_wheelAccum = 0;
    if (m_state != State::Open || m_layout.Empty()) {
        Invalidate();
        return;
    }
    if (mode == ScrollMode::Paged) {
        // Enter on the page under the viewport center (SyncPosition's rule);
        // the re-clamp below may then move/center it within its band.
        m_currentPage =
            std::clamp(static_cast<int>(SyncPosition()), 0, m_layout.PageCount() - 1);
    }
    // Both directions re-clamp under the new mode: leaving Paged must undo a
    // possibly negative centered offset, and the drawn page set changes even
    // when the offset does not.
    ScrollTo(m_scrollX, m_scrollY);
    UpdateScrollBars();
    Invalidate();
}

void PaneWindow::GotoOutlineItem(int index) {
    if (m_state != State::Open || index < 0 || index >= static_cast<int>(m_outline.size()))
        return;
    const Document::OutlineItem& item = m_outline[static_cast<size_t>(index)];
    if (item.targetPage < 0 || item.targetPage >= m_layout.PageCount())
        return;
    AdoptPage(item.targetPage); // paged: jumps adopt their target page
    const D2D1_RECT_F pr = m_layout.PageRect(item.targetPage);
    ScrollTo(m_scrollX, pr.top + item.targetY * DesiredScale() - DipToPx(8.0f));
}

void PaneWindow::SetDarkMode(bool dark) {
    if (m_dark == dark)
        return;
    m_dark = dark;
    Invalidate();
}

void PaneWindow::OnDpiChanged(UINT dpi) {
    if (dpi == 0 || dpi == m_dpi)
        return;
    const float ratio = static_cast<float>(dpi) / static_cast<float>(m_dpi);
    m_dpi = dpi;
    if (m_hasRestoreView) { // pending session-restore offsets are device px too
        m_restoreScrollX *= ratio;
        m_restoreScrollY *= ratio;
    }
    if (m_d2dContext)
        m_d2dContext->SetDpi(static_cast<float>(dpi), static_cast<float>(dpi));
    if (m_state == State::Open && m_zoomMode != ZoomMode::Manual) {
        // Fit zooms: DesiredScale tracks the viewport, not the DPI, so the
        // raw offsets must NOT be pre-scaled (the content pixel size barely
        // changes); keep the page-unit position stable instead, like
        // OnResize/SetZoomMode do.
        const double pos = SyncPosition();
        RelayoutDocument();
        ScrollToSyncPosition(pos);
    } else {
        // Manual zoom: content px scale with DesiredScale = zoom*dpi/72.
        m_scrollX *= ratio;
        m_scrollY *= ratio;
        if (m_state == State::Open)
            RelayoutDocument();
    }
    Invalidate();
}

LRESULT CALLBACK PaneWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    PaneWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<PaneWindow*>(cs->lpCreateParams);
        self->m_hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<PaneWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (!self)
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    try {
        return self->HandleMessage(msg, wParam, lParam);
    } catch (const GraphicsError&) {
        // Drop device-dependent state; recovery repaints rebuild it from
        // scratch (WARP as last resort), with backoff if failures persist.
        self->HandleDeviceLost();
        return 0;
    } catch (const std::exception& e) {
        // Non-graphics failure: do not tear down the shared device for it.
        MessageBoxA(hwnd, e.what(), "PdfSideViewer - fatal error", MB_ICONERROR);
        PostQuitMessage(1);
        return 0;
    }
}

LRESULT PaneWindow::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        m_dpi = GetDpiForWindow(m_hwnd);
        m_doc.SetNotificationTarget(m_hwnd);
        DragAcceptFiles(m_hwnd, TRUE);
        UpdateScrollBars(); // hide the default WS_*SCROLL bars until a document needs them
        return 0;

    case WM_DROPFILES: {
        HDROP drop = reinterpret_cast<HDROP>(wParam);
        const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        wchar_t path[MAX_PATH];
        if (count >= 1 && DragQueryFileW(drop, 0, path, MAX_PATH))
            OpenDocument(path);
        if (count >= 2 && m_openSibling && DragQueryFileW(drop, 1, path, MAX_PATH))
            m_openSibling(path);
        DragFinish(drop);
        SetFocus(m_hwnd);
        return 0;
    }

    case WM_DESTROY: {
        m_doc.Shutdown();
        // The worker is gone; free any results still sitting in the queue.
        // Beware: PeekMessage returns WM_QUIT REGARDLESS of the hwnd/range
        // filter once the quit flag is set and the queue is empty, so during
        // app shutdown this drain can swallow the WM_QUIT posted by the main
        // window; re-post it or the message loop blocks forever.
        bool sawQuit = false;
        int quitCode = 0;
        MSG pending;
        while (PeekMessageW(&pending, m_hwnd, WM_PSV_FIRST, WM_PSV_LAST, PM_REMOVE)) {
            if (pending.message == WM_QUIT) {
                sawQuit = true;
                quitCode = static_cast<int>(pending.wParam);
                continue; // flag now cleared; it cannot come back in this loop
            }
            switch (pending.message) {
            case WM_PSV_DOC_OPENED:
                delete reinterpret_cast<Document::OpenResult*>(pending.lParam);
                break;
            case WM_PSV_PAGE_RENDERED:
                delete reinterpret_cast<Document::RenderResult*>(pending.lParam);
                break;
            case WM_PSV_TEXT_PAGE:
                delete reinterpret_cast<Document::TextPageResult*>(pending.lParam);
                break;
            case WM_PSV_LINKS:
                delete reinterpret_cast<Document::LinksResult*>(pending.lParam);
                break;
            case WM_PSV_SEARCH:
                delete reinterpret_cast<Document::SearchResult*>(pending.lParam);
                break;
            default:
                break;
            }
        }
        if (sawQuit)
            PostQuitMessage(quitCode);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(m_hwnd, &ps);
        EndPaint(m_hwnd, &ps);
        RECT rc;
        GetClientRect(m_hwnd, &rc);
        if (rc.right > 0 && rc.bottom > 0) {
            EnsureSwapChain();
            Render();
        }
        return 0;
    }

    case WM_SIZE:
        OnResize(LOWORD(lParam), HIWORD(lParam));
        return 0;

    case WM_TIMER:
        if (wParam == kRecoveryTimer) {
            KillTimer(m_hwnd, kRecoveryTimer);
            Invalidate(); // next WM_PAINT retries the device rebuild
            return 0;
        }
        if (wParam == kReloadTimer) {
            KillTimer(m_hwnd, kReloadTimer);
            TryReloadDocument();
            return 0;
        }
        if (wParam == kSyncFlashTimer) {
            KillTimer(m_hwnd, kSyncFlashTimer);
            m_syncMark = {};
            Invalidate();
            return 0;
        }
        break;

    case WM_PSV_FILE_CHANGED:
        // Debounce: reload only after the change burst has been quiet for a
        // while. Every notification restarts both the timer and the retry
        // budget (new burst = new attempt).
        m_reloadRetries = 0;
        SetTimer(m_hwnd, kReloadTimer, kReloadDebounceMs, nullptr);
        return 0;

    case WM_PSV_DOC_OPENED:
        OnDocOpened(std::unique_ptr<Document::OpenResult>(
            reinterpret_cast<Document::OpenResult*>(lParam)));
        return 0;

    case WM_PSV_PAGE_RENDERED:
        OnPageRendered(std::unique_ptr<Document::RenderResult>(
            reinterpret_cast<Document::RenderResult*>(lParam)));
        return 0;

    case WM_PSV_TEXT_PAGE:
        OnTextPage(std::unique_ptr<Document::TextPageResult>(
            reinterpret_cast<Document::TextPageResult*>(lParam)));
        return 0;

    case WM_PSV_LINKS:
        OnLinks(std::unique_ptr<Document::LinksResult>(
            reinterpret_cast<Document::LinksResult*>(lParam)));
        return 0;

    case WM_PSV_SEARCH:
        OnSearchResult(std::unique_ptr<Document::SearchResult>(
            reinterpret_cast<Document::SearchResult*>(lParam)));
        return 0;

    case WM_COPY:
        CopySelection();
        return 0;

    case WM_VSCROLL:
    case WM_HSCROLL:
        OnScrollMessage(msg, wParam);
        return 0;

    case WM_MOUSEWHEEL:
        OnMouseWheel(wParam, lParam, false);
        return 0;

    case WM_MOUSEHWHEEL:
        OnMouseWheel(wParam, lParam, true);
        return 0;

    case WM_KEYDOWN:
        if (OnKeyDown(wParam))
            return 0;
        break;

    case WM_LBUTTONDOWN: {
        SetFocus(m_hwnd);
        m_dragStart = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        m_suppressLinkClick = false;
        m_clickActivatedLink = false;
        const bool hadSelection = m_hasSelection;
        ClearSelection();
        if (const auto caret = CaretAt(m_dragStart, false)) {
            m_selecting = true;
            m_selAnchor = *caret;
            m_selFocus = *caret;
            SetCapture(m_hwnd);
        }
        if (hadSelection)
            Invalidate();
        return 0;
    }

    case WM_LBUTTONDBLCLK: {
        if (m_state == State::Empty || m_state == State::Error) {
            // The placeholder invites it: double-click opens a file in this
            // pane (the frame owns the dialog). A failed open counts too:
            // its natural next step is picking another file.
            if (m_onOpenRequest)
                m_onOpenRequest();
            return 0;
        }
        const POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        // The double-click's second WM_LBUTTONUP must not re-activate a link,
        // and after an internal-link scroll the same client point now hits
        // destination content: do not word-select there.
        m_suppressLinkClick = true;
        if (m_clickActivatedLink)
            return 0;
        if (const auto caret = CaretAt(pt, false)) {
            const auto it = m_textPages.find(caret->page);
            if (it != m_textPages.end() && caret->line >= 0 &&
                caret->line < static_cast<int>(it->second.size())) {
                const auto& chars = it->second[static_cast<size_t>(caret->line)].chars;
                // caret->ch is an insertion index (midpoint rule); for word
                // hit-testing pick the char whose box contains the click.
                int a = -1;
                if (const auto pp = PagePointAt(pt)) {
                    for (int k = 0; k < static_cast<int>(chars.size()); ++k) {
                        const Document::RectPt& box = chars[static_cast<size_t>(k)].box;
                        if (pp->x >= box.x0 && pp->x <= box.x1) {
                            a = k;
                            break;
                        }
                    }
                }
                if (a < 0) // gap between glyphs or past the ends
                    a = std::min(caret->ch, static_cast<int>(chars.size()) - 1);
                if (a >= 0 && IsWordChar(chars[static_cast<size_t>(a)].codepoint)) {
                    int b = a + 1;
                    while (a > 0 && IsWordChar(chars[static_cast<size_t>(a - 1)].codepoint))
                        --a;
                    while (b < static_cast<int>(chars.size()) &&
                           IsWordChar(chars[static_cast<size_t>(b)].codepoint))
                        ++b;
                    m_selAnchor = {caret->page, caret->line, a};
                    m_selFocus = {caret->page, caret->line, b};
                    m_hasSelection = true;
                    Invalidate();
                }
            }
        }
        return 0;
    }

    case WM_MOUSEMOVE:
        if (m_selecting) {
            const POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            const SIZE vp = ViewportPx();
            if (pt.y < 0)
                ScrollBy(0, static_cast<float>(pt.y)); // drag auto-scroll
            else if (pt.y > vp.cy)
                ScrollBy(0, static_cast<float>(pt.y - vp.cy));
            if (const auto caret = CaretAt(pt, true)) {
                if (*caret != m_selFocus) {
                    m_selFocus = *caret;
                    m_hasSelection = m_selFocus != m_selAnchor;
                    Invalidate();
                }
            }
            return 0;
        }
        break;

    case WM_LBUTTONUP: {
        const POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        if (m_selecting) {
            m_selecting = false;
            ReleaseCapture();
        }
        const LONG dx = pt.x - m_dragStart.x;
        const LONG dy = pt.y - m_dragStart.y;
        const bool suppress = m_suppressLinkClick;
        m_suppressLinkClick = false;
        if (!suppress && !m_hasSelection && dx * dx + dy * dy <= 9) { // click, not drag
            if (wParam & MK_CONTROL) { // Ctrl+click = SyncTeX inverse search
                InverseSearchAt(pt);
                return 0; // never also follow a link
            }
            if (const auto pp = PagePointAt(pt)) {
                if (const Document::LinkInfo* link = LinkAt(pp->page, pp->x, pp->y)) {
                    m_clickActivatedLink = true;
                    ActivateLink(*link);
                }
            }
        }
        return 0;
    }

    case WM_CAPTURECHANGED:
        m_selecting = false;
        return 0;

    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT && m_state == State::Open) {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(m_hwnd, &pt);
            if (const auto pp = PagePointAt(pt)) {
                if (LinkAt(pp->page, pp->x, pp->y)) {
                    SetCursor(LoadCursorW(nullptr, IDC_HAND));
                    return TRUE;
                }
                if (TextAt(pp->page, pp->x, pp->y)) {
                    SetCursor(LoadCursorW(nullptr, IDC_IBEAM));
                    return TRUE;
                }
            }
        }
        break;

    case WM_SETFOCUS:
        m_focused = true;
        Invalidate();
        if (m_onViewChanged)
            m_onViewChanged(*this, ViewEvent::FocusGained, 1.0f);
        return 0;

    case WM_KILLFOCUS:
        m_focused = false;
        Invalidate();
        return 0;

    default:
        break;
    }
    return DefWindowProcW(m_hwnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------- graphics --

void PaneWindow::EnsureSwapChain() {
    m_dx.EnsureCreated();

    if (m_swapChain && m_dxGeneration != m_dx.Generation()) {
        // Our swapchain/context belong to a discarded device; rebuild below.
        ReleaseTarget();
        m_d2dContext.Reset();
        m_swapChain.Reset();
        DropDeviceDependentPageBitmaps();
    }

    EnsureTextFormat();

    if (m_swapChain)
        return;

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.Scaling = DXGI_SCALING_NONE;
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    ThrowIfFailed(m_dx.DxgiFactory()->CreateSwapChainForHwnd(m_dx.D3dDevice(), m_hwnd, &desc,
                                                             nullptr, nullptr, &m_swapChain),
                  "IDXGIFactory2::CreateSwapChainForHwnd");
    m_dx.DxgiFactory()->MakeWindowAssociation(m_hwnd, DXGI_MWA_NO_ALT_ENTER);

    ThrowIfFailed(m_dx.D2dDevice()->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
                                                        &m_d2dContext),
                  "ID2D1Device::CreateDeviceContext");
    m_d2dContext->SetDpi(static_cast<float>(m_dpi), static_cast<float>(m_dpi));
    m_d2dContext->SetUnitMode(D2D1_UNIT_MODE_PIXELS);
    CreateTargetBitmap();
    m_dxGeneration = m_dx.Generation();
}

void PaneWindow::EnsureTextFormat() {
    if (m_textFormat && m_textFormatDpi == m_dpi)
        return;
    ComPtr<IDWriteTextFormat> format;
    // Sizes are in device pixels (the context runs in D2D1_UNIT_MODE_PIXELS).
    ThrowIfFailed(m_dx.DWriteFactory()->CreateTextFormat(
                      L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                      DWRITE_FONT_STRETCH_NORMAL, DipToPx(16.0f), L"en-us", &format),
                  "IDWriteFactory::CreateTextFormat");
    format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    m_textFormat = format;
    m_textFormatDpi = m_dpi;
}

void PaneWindow::CreateTargetBitmap() {
    ComPtr<IDXGISurface> surface;
    ThrowIfFailed(m_swapChain->GetBuffer(0, IID_PPV_ARGS(&surface)),
                  "IDXGISwapChain1::GetBuffer");
    const float dpi = static_cast<float>(m_dpi);
    const D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), dpi, dpi);
    ThrowIfFailed(m_d2dContext->CreateBitmapFromDxgiSurface(surface.Get(), &props, &m_targetBitmap),
                  "ID2D1DeviceContext::CreateBitmapFromDxgiSurface");
    m_d2dContext->SetTarget(m_targetBitmap.Get());
}

void PaneWindow::ReleaseTarget() {
    if (m_d2dContext)
        m_d2dContext->SetTarget(nullptr);
    m_targetBitmap.Reset();
}

void PaneWindow::DropDeviceDependentPageBitmaps() {
    // failedScale must reset too: a CreateBitmap failure on the (possibly
    // already dead) old device says nothing about the rebuilt one, and a
    // latched failedScale would suppress re-requests forever.
    for (auto& [page, entry] : m_previews) {
        entry.bitmap.Reset();
        entry.bitmapScale = 0;
        entry.failedScale = 0;
    }
    for (auto& [key, entry] : m_tiles) {
        entry.bitmap.Reset();
        entry.bitmapScale = 0;
        entry.failedScale = 0;
    }
}

void PaneWindow::HandleDeviceLost() {
    ReleaseTarget();
    m_d2dContext.Reset();
    m_swapChain.Reset();
    DropDeviceDependentPageBitmaps();
    // Discard the shared device only if our dead resources were built on the
    // current one; if the sibling already rebuilt a healthy device, keep it
    // and only rebuild our own resources. Discard() bumps the generation.
    if (m_dxGeneration == m_dx.Generation()) {
        m_dx.Discard();
        // Wake the sibling too, so it rebuilds promptly instead of freezing.
        if (m_hwnd)
            RedrawWindow(GetAncestor(m_hwnd, GA_ROOT), nullptr, nullptr,
                         RDW_INVALIDATE | RDW_ALLCHILDREN);
    }
    ScheduleRecovery();
}

void PaneWindow::ScheduleRecovery() {
    if (!m_hwnd)
        return;
    ++m_recoveryAttempts;
    if (m_recoveryAttempts <= 1) {
        Invalidate(); // ordinary device loss: retry right away
    } else if (m_recoveryAttempts <= kMaxRecoveryAttempts) {
        // Persistent failure: back off instead of spinning WM_PAINT at 100% CPU.
        SetTimer(m_hwnd, kRecoveryTimer, 250u * m_recoveryAttempts, nullptr);
    } else {
        MessageBoxW(m_hwnd, Str(StrId::DeviceLostError), L"PdfSideViewer", MB_ICONERROR);
        PostQuitMessage(1);
    }
}

void PaneWindow::OnResize(UINT width, UINT height) {
    if (width == 0 || height == 0)
        return;
    // The fit computation is bar-independent, so a scrollbar toggle's nested
    // WM_SIZE converges; this cap is pure insurance against pathological
    // metric combinations turning it into unbounded recursion.
    if (m_resizeDepth >= 4)
        return;
    ++m_resizeDepth;
    const auto depthGuard = std::unique_ptr<int, void (*)(int*)>(
        &m_resizeDepth, [](int* d) { --*d; });
    if (!m_swapChain) {
        if (m_state == State::Open && m_zoomMode != ZoomMode::Manual) {
            // Fit zooms must track the viewport even while the device is
            // down, or the recovery paint renders the stale fit layout.
            const double pos = SyncPosition();
            RelayoutDocument();
            ScrollToSyncPosition(pos);
        } else {
            ClampScroll();
            UpdateScrollBars();
        }
        Invalidate(); // swapchain is created lazily at the next paint
        return;
    }
    ReleaseTarget();
    const HRESULT hr = m_swapChain->ResizeBuffers(0, 0, 0, DXGI_FORMAT_UNKNOWN, 0);
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        HandleDeviceLost();
        return;
    }
    ThrowIfFailed(hr, "IDXGISwapChain1::ResizeBuffers");
    CreateTargetBitmap();
    if (m_state == State::Open && m_zoomMode != ZoomMode::Manual) {
        // Fit zooms track the viewport; keep the page-unit position stable.
        const double pos = SyncPosition();
        RelayoutDocument(); // may re-enter OnResize once when a scrollbar toggles
        ScrollToSyncPosition(pos);
    } else {
        ClampScroll();
        UpdateScrollBars(); // may re-enter OnResize once when a scrollbar toggles
    }
    Render();
}

void PaneWindow::Render() {
    if (!m_d2dContext || !m_targetBitmap)
        return;
    m_d2dContext->BeginDraw();
    DrawContent();
    const HRESULT hr = m_d2dContext->EndDraw();
    if (hr == static_cast<HRESULT>(D2DERR_RECREATE_TARGET)) {
        HandleDeviceLost();
        return;
    }
    DXGI_PRESENT_PARAMETERS params{};
    const HRESULT presentHr = m_swapChain->Present1(1, 0, &params);
    if (presentHr == DXGI_ERROR_DEVICE_REMOVED || presentHr == DXGI_ERROR_DEVICE_RESET) {
        HandleDeviceLost();
        return;
    }
    if (m_recoveryAttempts > 0) { // a frame reached the screen: recovery succeeded
        m_recoveryAttempts = 0;
        KillTimer(m_hwnd, kRecoveryTimer);
    }
}

// ----------------------------------------------------------------- drawing --

void PaneWindow::DrawContent() {
    const D2D1_COLOR_F background = m_dark ? D2D1::ColorF(0x202020) : D2D1::ColorF(0xF3F3F3);
    m_d2dContext->Clear(background);

    ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black), &brush)))
        return;

    if (m_state == State::Open && !m_layout.Empty())
        DrawDocument(brush.Get());
    else
        DrawPlaceholder(brush.Get());

    if (m_focused) {
        // NOT GetSize(): that returns DIPs even in D2D1_UNIT_MODE_PIXELS,
        // which shrinks the rect by the DPI factor on scaled monitors.
        const SIZE vp = ViewportPx();
        const float w = DipToPx(2.0f);
        brush->SetColor(m_dark ? D2D1::ColorF(0x4CC2FF) : D2D1::ColorF(0x0067C0));
        m_d2dContext->DrawRectangle(
            D2D1::RectF(w / 2, w / 2, static_cast<float>(vp.cx) - w / 2,
                        static_cast<float>(vp.cy) - w / 2),
            brush.Get(), w);
    }
}

void PaneWindow::DrawPlaceholder(ID2D1SolidColorBrush* brush) {
    const SIZE vp = ViewportPx(); // not GetSize(): DIPs even in pixel unit mode
    const D2D1_SIZE_F size = D2D1::SizeF(static_cast<float>(vp.cx), static_cast<float>(vp.cy));
    std::wstring text;
    switch (m_state) {
    case State::Empty:
        text = m_hint;
        break;
    case State::Opening:
        text = std::wstring(Str(StrId::PaneOpening)) + L"\n" + FileNameOf(m_docPath) + L"…";
        break;
    case State::Error:
        text = std::wstring(Str(StrId::PaneOpenFailed)) + L"\n" + FileNameOf(m_docPath) +
               L"\n\n" + m_errorText;
        break;
    case State::Open:
        text = Str(StrId::PaneEmptyDoc);
        break;
    }
    brush->SetColor(m_dark ? D2D1::ColorF(0x9D9D9D) : D2D1::ColorF(0x605E5C));
    const float pad = DipToPx(12.0f);
    const D2D1_RECT_F rect = D2D1::RectF(pad, pad, size.width - pad, size.height - pad);
    m_d2dContext->DrawText(text.c_str(), static_cast<UINT32>(text.size()), m_textFormat.Get(),
                           rect, brush);
}

int PaneWindow::TileResFor(float pageWpx, float pageHpx) {
    // SumatraPDF's heuristic: geometric mean of the per-axis oversize factors,
    // so tile area stays close to kMaxTilePx^2 even for extreme aspect ratios.
    const float factor = std::sqrt((pageWpx / kMaxTilePx) * (pageHpx / kMaxTilePx));
    if (factor <= 1.5f)
        return 0;
    const int res = static_cast<int>(std::ceil(std::log2(factor)));
    return std::min(res, 6); // 64x64 grid, far beyond what kMaxZoom can need
}

void PaneWindow::EnsureRequested(CachedBitmap& entry, int page, float scale, int res, int row,
                                 int col, bool urgent) {
    const bool upToDate = entry.bitmap && SameScale(entry.bitmapScale, scale);
    const bool requested = entry.pendingId != 0 && SameScale(entry.pendingScale, scale);
    const bool failed = SameScale(entry.failedScale, scale);
    if (upToDate || requested || failed)
        return;
    entry.pendingId = m_nextRequestId++;
    entry.pendingScale = scale;
    m_doc.RequestRender(page, scale, entry.pendingId, res, row, col, urgent);
}

void PaneWindow::DrawDocument(ID2D1SolidColorBrush* brush) {
    ++m_frame;
    const SIZE vp = ViewportPx();
    const D2D1_POINT_2F origin = ContentOrigin();
    const float desired = DesiredScale();
    const D2D1_COLOR_F pageColor = D2D1::ColorF(D2D1::ColorF::White);
    const D2D1_COLOR_F borderColor = m_dark ? D2D1::ColorF(0x000000) : D2D1::ColorF(0xB8B8B8);

    // The wanted range must be published BEFORE any render request: an idle
    // worker pops fresh jobs immediately and would judge them against the
    // previous paint's range, silently dropping them.
    int first;
    int last;
    if (PagedActive()) {
        // Strict single page: neighbors are never drawn (the background clear
        // covers where they would be), but the wanted range below still keeps
        // cur±1 warm so flips land instantly.
        first = m_currentPage;
        last = m_currentPage;
    } else {
        first = m_layout.FirstVisible(m_scrollY);
        last = first - 1;
        for (int i = first; i < m_layout.PageCount(); ++i) {
            if (m_layout.PageRect(i).top + origin.y > static_cast<float>(vp.cy))
                break;
            last = i;
        }
    }
    m_doc.SetWantedRange(first - 1, last + 1);

    // Device texture cap (16384 px at FL11 down to 2048 at FL9): previews and
    // small whole pages must stay uploadable.
    const float maxSidePx = static_cast<float>(m_d2dContext->GetMaximumBitmapSize());
    // Tiles slightly beyond the viewport render ahead of small scrolls.
    const float inflate = static_cast<float>(std::max(vp.cx, vp.cy)) * 0.25f;
    const D2D1_RECT_F view =
        D2D1::RectF(-inflate, -inflate, static_cast<float>(vp.cx) + inflate,
                    static_cast<float>(vp.cy) + inflate);

    for (int i = first; i <= last; ++i) {
        const D2D1_RECT_F pr = m_layout.PageRect(i);
        const D2D1_RECT_F dest = D2D1::RectF(pr.left + origin.x, pr.top + origin.y,
                                             pr.right + origin.x, pr.bottom + origin.y);
        const float pageW = pr.right - pr.left;
        const float pageH = pr.bottom - pr.top;

        brush->SetColor(pageColor);
        m_d2dContext->FillRectangle(dest, brush);

        const int res = TileResFor(pageW, pageH);

        // Whole-page bitmap: the final render when res == 0, otherwise a
        // capped-size preview that backs not-yet-rendered tiles (stale-scale
        // previews are stretched, so zooming never flashes an empty page).
        float previewScale = desired;
        const float cap = res == 0 ? maxSidePx : std::min(kMaxTilePx, maxSidePx);
        const float largestSide = std::max(pageW, pageH);
        if (largestSide > cap)
            previewScale = desired * (cap / largestSide) * 0.99f; // fz_round_rect margin

        CachedBitmap& preview = m_previews[i];
        preview.lastUsed = m_frame;
        if (preview.bitmap)
            m_d2dContext->DrawBitmap(preview.bitmap.Get(), dest, 1.0f,
                                     D2D1_INTERPOLATION_MODE_LINEAR);

        if (res > 0) {
            const int n = 1 << res;
            // Exact quantized size, not pageW/pageH: rect edge differences
            // lose ±1 px past 2^24 content px and would desync the tile grid
            // from the worker's, producing seams.
            const D2D1_SIZE_F pagePx = m_layout.PageSizePx(i);
            const int fullW = static_cast<int>(pagePx.width + 0.5f);
            const int fullH = static_cast<int>(pagePx.height + 0.5f);
            for (int row = 0; row < n; ++row) {
                const int y0 = fullH * row / n;
                const int y1 = fullH * (row + 1) / n;
                for (int col = 0; col < n; ++col) {
                    const int x0 = fullW * col / n;
                    const int x1 = fullW * (col + 1) / n;
                    const D2D1_RECT_F tileDest =
                        D2D1::RectF(dest.left + static_cast<float>(x0),
                                    dest.top + static_cast<float>(y0),
                                    dest.left + static_cast<float>(x1),
                                    dest.top + static_cast<float>(y1));
                    if (tileDest.right < view.left || tileDest.left > view.right ||
                        tileDest.bottom < view.top || tileDest.top > view.bottom)
                        continue;
                    CachedBitmap& tile = m_tiles[TileKey{i, res, row, col}];
                    tile.lastUsed = m_frame;
                    if (tile.bitmap && SameScale(tile.bitmapScale, desired))
                        m_d2dContext->DrawBitmap(tile.bitmap.Get(), tileDest, 1.0f,
                                                 D2D1_INTERPOLATION_MODE_LINEAR);
                    EnsureRequested(tile, i, desired, res, row, col, /*urgent=*/true);
                }
            }
        }

        // Requested AFTER the tiles: urgent jobs are LIFO (push_front), so on
        // fresh navigation this single cheap capped render pops first and
        // instantly backs the page's not-yet-rendered tiles instead of
        // arriving after every expensive exact-scale tile.
        EnsureRequested(preview, i, previewScale, 0, 0, 0,
                        /*urgent=*/res == 0 || !preview.bitmap);

        brush->SetColor(borderColor);
        m_d2dContext->DrawRectangle(dest, brush, 1.0f);

        DrawOverlays(brush, i, dest, desired);
        EnsureTextPage(i, false);
        EnsureLinks(i);
    }

    // Prefetch previews of the pages adjacent to the visible range, at the
    // exact scale the visible loop will want (a different cap here would
    // ping-pong renders on every visible-range boundary crossing).
    for (const int i : {first - 1, last + 1}) {
        if (i < 0 || i >= m_layout.PageCount())
            continue;
        const D2D1_RECT_F pr = m_layout.PageRect(i);
        const float pageW = pr.right - pr.left;
        const float pageH = pr.bottom - pr.top;
        const int res = TileResFor(pageW, pageH);
        const float cap = res == 0 ? maxSidePx : std::min(kMaxTilePx, maxSidePx);
        const float largestSide = std::max(pageW, pageH);
        float scale = desired;
        if (largestSide > cap)
            scale = desired * (cap / largestSide) * 0.99f;
        CachedBitmap& preview = m_previews[i];
        preview.lastUsed = m_frame;
        EnsureRequested(preview, i, scale, 0, 0, 0, /*urgent=*/false);
    }

    EvictStale(first - 1, last + 1);
}

// ---------------------------------------------------------- document events --

void PaneWindow::OnDocOpened(std::unique_ptr<Document::OpenResult> result) {
    // Generation, not path: re-opening the same file makes a stale result's
    // path indistinguishable, and acting on it would let renders race a
    // still-queued Open (and render the wrong document).
    if (!result || result->generation != m_openGen)
        return;
    if (!result->ok) {
        m_state = State::Error;
        m_errorText = result->error;
        m_layout.Clear();
        // Notify anyway: the outline sidebar must drop the previous
        // document's bookmarks and sync must reset the now-void anchors.
        if (m_onViewChanged)
            m_onViewChanged(*this, ViewEvent::DocumentOpened, 1.0f);
    } else {
        m_state = State::Open;
        m_layout.SetPages(std::move(result->pageSizesPt));
        m_outline = std::move(result->outline);
        if (m_hasRestoreView) {
            m_hasRestoreView = false;
            m_zoomMode = m_restoreZoomMode;
            m_zoom = m_restoreZoom;
            // Restore in the coordinate space the offsets were SAVED in (a
            // layout at the saved zoom), convert to page units, then let the
            // relayout compute the current fit zoom; computing the fit first
            // would interpret the offsets against a differently-sized layout
            // and drift the position on every save/restore cycle.
            m_layout.Update(DesiredScale(), DipToPx(8.0f), DipToPx(12.0f));
            m_scrollX = m_restoreScrollX;
            m_scrollY = m_restoreScrollY;
            // Adopt BEFORE the clamp: m_currentPage still holds page 0 from
            // the OpenDocument reset, and clamping into page 0's band here
            // would wreck every paged session restore and auto-reload.
            if (PagedActive()) {
                const SIZE vp = ViewportPx();
                AdoptPage(m_layout.FirstVisible(m_scrollY + static_cast<float>(vp.cy) / 2.0f));
            }
            ClampScroll();
            const double pos = SyncPosition();
            RelayoutDocument();
            ScrollToSyncPosition(pos);
        } else {
            RelayoutDocument();
        }
        if (m_onViewChanged)
            m_onViewChanged(*this, ViewEvent::DocumentOpened, 1.0f);
        if (!m_pendingSearch.empty()) {
            // Query typed while the document was still opening: run it now.
            const std::wstring pending = std::move(m_pendingSearch);
            m_pendingSearch.clear();
            StartSearch(pending);
        }
    }
    Invalidate();
}

void PaneWindow::OnPageRendered(std::unique_ptr<Document::RenderResult> result) {
    if (!result || m_state != State::Open)
        return;
    CachedBitmap* entry = nullptr;
    if (result->res == 0) {
        const auto it = m_previews.find(result->pageIndex);
        if (it != m_previews.end())
            entry = &it->second;
    } else {
        const auto it =
            m_tiles.find(TileKey{result->pageIndex, result->res, result->row, result->col});
        if (it != m_tiles.end())
            entry = &it->second;
    }
    if (!entry)
        return; // evicted while the render was in flight
    if (entry->pendingId == result->requestId) {
        entry->pendingId = 0;
        entry->pendingScale = 0;
    } else if (result->res != 0) {
        return; // superseded tile at an old scale is useless: the grid moved
    }
    if (!result->ok || !result->pixels)
        return; // render failed: pending cleared, the next repaint may retry
    if (!m_d2dContext) {
        Invalidate(); // device lost: the repaint re-requests what it needs
        return;
    }
    const D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
    ComPtr<ID2D1Bitmap> bitmap;
    const HRESULT hr = m_d2dContext->CreateBitmap(
        D2D1::SizeU(static_cast<UINT32>(result->width), static_cast<UINT32>(result->height)),
        result->pixels.get(), static_cast<UINT32>(result->stride), &props, &bitmap);
    if (SUCCEEDED(hr)) {
        entry->bitmap = bitmap;
        entry->bitmapScale = result->scale;
        entry->failedScale = 0;
        Invalidate();
    } else {
        // Deterministic upload failure (e.g. bitmap over the device cap):
        // remember the scale so paint stops re-issuing a doomed render.
        entry->failedScale = result->scale;
    }
}

// -------------------------------------------------------------- view state --

SIZE PaneWindow::ViewportPx() const {
    RECT rc{};
    GetClientRect(m_hwnd, &rc);
    return {rc.right, rc.bottom};
}

D2D1_POINT_2F PaneWindow::ContentOrigin() const {
    const SIZE vp = ViewportPx();
    const float ox = m_layout.TotalWidth() <= static_cast<float>(vp.cx)
                         ? (static_cast<float>(vp.cx) - m_layout.TotalWidth()) / 2.0f
                         : -m_scrollX;
    // Paged: never center the whole content; the band clamp already centers a
    // fitting page by pushing m_scrollY (possibly negative) to the one valid
    // position, so the plain translation is always correct.
    const float oy = PagedActive()
                         ? -m_scrollY
                         : (m_layout.TotalHeight() <= static_cast<float>(vp.cy)
                                ? (static_cast<float>(vp.cy) - m_layout.TotalHeight()) / 2.0f
                                : -m_scrollY);
    // Snap to whole pixels: fractional scroll/centering would put the 1:1
    // page blits on half-pixel boundaries and blur them.
    return D2D1::Point2F(std::floor(ox + 0.5f), std::floor(oy + 0.5f));
}

void PaneWindow::UpdateFitZoom() {
    if (m_zoomMode == ZoomMode::Manual || m_state != State::Open || m_layout.Empty())
        return;
    const D2D1_SIZE_F maxPt = m_layout.MaxPageSizePt();
    if (maxPt.width <= 0 || maxPt.height <= 0)
        return;
    // Bar-INDEPENDENT metrics: deriving the fit zoom from the client rect
    // feeds back into scrollbar visibility (bar toggles change the client
    // width, which changes the zoom, which changes the content height, which
    // toggles the bar again: unbounded synchronous WM_SIZE recursion). The
    // pane's window rect is client + visible bars, so it is stable, and the
    // v-bar need is predicted from the estimated content height instead.
    RECT wr{};
    GetWindowRect(m_hwnd, &wr);
    const float fullW = static_cast<float>(wr.right - wr.left);
    const float fullH = static_cast<float>(wr.bottom - wr.top);
    const float sbW = static_cast<float>(GetSystemMetricsForDpi(SM_CXVSCROLL, m_dpi));
    const float margins = 2.0f * DipToPx(12.0f);
    const float gap = DipToPx(8.0f);
    const float ptToPx = static_cast<float>(m_dpi) / 72.0f;

    const auto fitFor = [&](float w, float h) {
        float zoom = (w - margins) / (maxPt.width * ptToPx);
        if (m_zoomMode == ZoomMode::FitPage)
            zoom = std::min(zoom, (h - margins) / (maxPt.height * ptToPx));
        return std::clamp(zoom, kMinZoom, kMaxZoom);
    };
    const float zoom0 = fitFor(fullW, fullH);
    const float estTotalH = margins + m_layout.SumPageHeightsPt() * zoom0 * ptToPx +
                            gap * static_cast<float>(m_layout.PageCount() - 1);
    m_zoom = estTotalH > fullH ? fitFor(fullW - sbW, fullH) : zoom0;
}

void PaneWindow::RelayoutDocument() {
    if (m_state != State::Open)
        return;
    m_wheelAccum = 0; // band geometry changes; pending paged wheel credit is void
    const float oldZoom = m_zoom;
    UpdateFitZoom();
    m_layout.Update(DesiredScale(), DipToPx(8.0f), DipToPx(12.0f));
    ClampScroll();
    UpdateScrollBars();
    Invalidate();
    if (m_zoom != oldZoom && m_onViewChanged)
        m_onViewChanged(*this, ViewEvent::FitZoomChanged, m_zoom / oldZoom);
}

void PaneWindow::ClampScroll() {
    const SIZE vp = ViewportPx();
    m_scrollX = std::clamp(m_scrollX, 0.0f,
                           std::max(0.0f, m_layout.TotalWidth() - static_cast<float>(vp.cx)));
    if (PagedActive()) {
        // Paged invariant: no ScrollTo from ANY path can leave the current
        // page. The band excludes gap and margin (PageRect covers only the
        // page), so a neighbor can never peek in. X stays whole-content:
        // a per-page X band would yank m_scrollX on every flip through
        // mixed-width documents, breaking "X is preserved across flips".
        m_currentPage = std::clamp(m_currentPage, 0, m_layout.PageCount() - 1);
        const PagedBand band = PagedBandFor(m_currentPage);
        m_scrollY = std::clamp(m_scrollY, band.lo, band.hi);
    } else {
        m_scrollY = std::clamp(m_scrollY, 0.0f,
                               std::max(0.0f, m_layout.TotalHeight() - static_cast<float>(vp.cy)));
    }
}

PaneWindow::PagedBand PaneWindow::PagedBandFor(int page) const {
    const D2D1_RECT_F pr = m_layout.PageRect(page);
    const float vpH = static_cast<float>(ViewportPx().cy);
    const float pageH = pr.bottom - pr.top;
    PagedBand band{};
    band.fits = pageH <= vpH;
    if (band.fits) {
        // Single valid position: the page centered vertically. May be
        // negative (page 0 in a viewport taller than page + margin);
        // ContentOrigin's paged branch turns that into positive padding.
        band.lo = band.hi = pr.top - (vpH - pageH) / 2.0f;
    } else {
        band.lo = pr.top;
        band.hi = pr.bottom - vpH;
    }
    return band;
}

bool PaneWindow::AtBandEdge(const PagedBand& band, int dir) const {
    return dir > 0 ? m_scrollY >= band.hi - kPagedEdgeEps
                   : m_scrollY <= band.lo + kPagedEdgeEps;
}

void PaneWindow::AdoptPage(int page) {
    if (!PagedActive())
        return;
    m_currentPage = std::clamp(page, 0, m_layout.PageCount() - 1);
    m_wheelAccum = 0;
}

void PaneWindow::PagedFlip(int dir) {
    m_wheelAccum = 0; // drains leftover credit even when pinned at an end
    const int target = m_currentPage + dir;
    if (target < 0 || target >= m_layout.PageCount())
        return; // no wrap at document ends
    m_currentPage = target;
    const PagedBand band = PagedBandFor(target);
    // Forward lands at the top, backward at the BOTTOM of the previous page
    // (upward reading stays continuous); fits => lo == hi centers it. X is
    // deliberately preserved across flips.
    ScrollTo(m_scrollX, dir > 0 ? band.lo : band.hi);
    Invalidate(); // the drawn page changed even if the offset somehow did not
}

void PaneWindow::PagedStepY(float dy) {
    const int dir = dy > 0 ? 1 : -1;
    const PagedBand band = PagedBandFor(m_currentPage);
    if (band.fits || AtBandEdge(band, dir)) {
        PagedFlip(dir); // a previous event already parked us at the edge
        return;
    }
    m_wheelAccum = 0; // a key step invalidates pending wheel credit
    // The band clamp stops at the edge: the event that REACHES the edge never
    // flips (the mandatory stop); only the next one, already at the edge, does.
    ScrollTo(m_scrollX, m_scrollY + dy);
}

void PaneWindow::PagedWheelY(int delta, float pxPerDetent) {
    // Wheel deltas are inverted relative to scroll direction: positive delta
    // (wheel away from the user) scrolls UP, toward the PREVIOUS page.
    const int dir = delta < 0 ? 1 : -1;
    if (m_wheelAccum != 0 && (m_wheelAccum > 0) != (delta > 0))
        m_wheelAccum = 0; // direction change forfeits the credit
    const PagedBand band = PagedBandFor(m_currentPage);
    if (!band.fits && !AtBandEdge(band, dir)) {
        m_wheelAccum = 0;
        // Mid-page: scroll (the clamp stops at the edge), never flip.
        ScrollTo(m_scrollX, m_scrollY - static_cast<float>(delta) / WHEEL_DELTA * pxPerDetent);
        return;
    }
    // At the edge (or the page fits): accumulate toward one full detent so
    // precision-touchpad micro-deltas cannot flip once per event.
    m_wheelAccum += static_cast<float>(delta);
    if (std::abs(m_wheelAccum) >= static_cast<float>(WHEEL_DELTA)) {
        // Full reset, not -= WHEEL_DELTA: drains leftover credit at document
        // ends and caps coalesced multi-notch messages at one flip each.
        m_wheelAccum = 0;
        PagedFlip(dir);
    }
}

void PaneWindow::ScrollTo(float x, float y) {
    const float oldX = m_scrollX;
    const float oldY = m_scrollY;
    m_scrollX = x;
    m_scrollY = y;
    ClampScroll();
    if (m_scrollX != oldX || m_scrollY != oldY) {
        UpdateScrollBars();
        Invalidate();
        if (m_onViewChanged)
            m_onViewChanged(*this, ViewEvent::Scrolled, 1.0f);
    }
}

void PaneWindow::ScrollBy(float dx, float dy) {
    ScrollTo(m_scrollX + dx, m_scrollY + dy);
}

void PaneWindow::ZoomAt(POINT viewPt, float newZoom) {
    newZoom = std::clamp(newZoom, kMinZoom, kMaxZoom);
    // Relative epsilon: an absolute one swallows small precision-touchpad
    // steps at low zoom (each rejected event leaves m_zoom unchanged, so a
    // slow pinch would never accumulate past it). The smallest wheel step
    // (delta 1 -> factor 1.1^(1/120)) is ~8e-4 relative, safely above 1e-4.
    if (m_state != State::Open || std::abs(newZoom - m_zoom) < m_zoom * 1e-4f)
        return;

    // Anchor the nearest page point (page index + within-page position in
    // points) plus a zoom-invariant device-px residual for gap/margin areas.
    // Content y = margin + i*gap + scaled page heights: only the scale term
    // changes with zoom, so scaling raw content coordinates would drift by
    // (margin + i*gap)*(ratio-1), hundreds of px deep into a long document.
    const D2D1_POINT_2F origin = ContentOrigin();
    const float cx = static_cast<float>(viewPt.x) - origin.x;
    const float cy = static_cast<float>(viewPt.y) - origin.y;

    PageLayout::PagePoint anchor{};
    float offX = 0;
    float offY = 0;
    const bool haveAnchor = !m_layout.Empty();
    if (haveAnchor) {
        const float oldScale = DesiredScale();
        // Paged: the current page is the only one on screen; FirstVisible
        // over the background above/below it would anchor an invisible
        // neighbor and zooming would drag the view onto it.
        const int page = PagedActive()
                             ? m_currentPage
                             : std::min(m_layout.FirstVisible(cy), m_layout.PageCount() - 1);
        const D2D1_RECT_F rect = m_layout.PageRect(page);
        const float px = std::clamp(cx, rect.left, rect.right);
        const float py = std::clamp(cy, rect.top, rect.bottom);
        anchor = {page, (px - rect.left) / oldScale, (py - rect.top) / oldScale};
        offX = cx - px; // gap/margin offsets do not scale with zoom
        offY = cy - py;
    }

    const float ratioApplied = newZoom / m_zoom;
    m_zoomMode = ZoomMode::Manual; // a zoom gesture unhooks the fit modes
    m_zoom = newZoom;
    m_wheelAccum = 0; // relayout below: stale paged wheel credit must not survive it
    m_layout.Update(DesiredScale(), DipToPx(8.0f), DipToPx(12.0f));

    D2D1_POINT_2F target = D2D1::Point2F(cx, cy);
    if (haveAnchor) {
        const D2D1_POINT_2F p = m_layout.ToContent(anchor);
        target = D2D1::Point2F(p.x + offX, p.y + offY);
    }
    m_scrollX = target.x - static_cast<float>(viewPt.x);
    m_scrollY = target.y - static_cast<float>(viewPt.y);
    ClampScroll();
    UpdateScrollBars();
    Invalidate();
    if (m_onViewChanged)
        m_onViewChanged(*this, ViewEvent::Zoomed, ratioApplied);
}

double PaneWindow::SyncPosition() const {
    if (!HasDocument())
        return 0.0;
    const SIZE vp = ViewportPx();
    // Content-space y of the viewport center (works when the content is
    // smaller than the viewport and centered, too).
    const float centerY = static_cast<float>(vp.cy) / 2.0f - ContentOrigin().y;
    const int count = m_layout.PageCount();
    int page;
    if (PagedActive()) {
        // The current page is authoritative. Between a viewport change and
        // the re-clamp (resize, fullscreen, splitter drag) the stale offset
        // can put the geometric center on a neighbor, and reporting that
        // would switch pages across every resize dance.
        page = std::clamp(m_currentPage, 0, count - 1);
    } else {
        page = m_layout.FirstVisible(centerY);
        if (page >= count)
            return static_cast<double>(count);
    }
    const D2D1_RECT_F rect = m_layout.PageRect(page);
    float fraction = 0;
    if (centerY > rect.top && rect.bottom > rect.top)
        fraction = std::clamp((centerY - rect.top) / (rect.bottom - rect.top), 0.0f, 1.0f);
    if (PagedActive()) {
        // The integer part IS the current page: a stale offset can push the
        // fraction to exactly 1.0 (only possible pre-re-clamp; a band-clamped
        // center always sits strictly above the page bottom), and page + 1.0
        // would make ScrollToSyncPosition adopt the NEXT page mid-resize.
        fraction = std::min(fraction, 0.9999f);
    }
    return static_cast<double>(page) + static_cast<double>(fraction);
}

void PaneWindow::ScrollToSyncPosition(double pos) {
    if (!HasDocument())
        return;
    const int count = m_layout.PageCount();
    pos = std::clamp(pos, 0.0, static_cast<double>(count));
    const int page = std::min(static_cast<int>(pos), count - 1);
    // Paged: sync targets and the relayout dances adopt the page they aim at
    // (a programmatic jump), then the band clamp lands as close as possible.
    AdoptPage(page);
    const float fraction = static_cast<float>(std::clamp(pos - page, 0.0, 1.0));
    const D2D1_RECT_F rect = m_layout.PageRect(page);
    const float contentY = rect.top + fraction * (rect.bottom - rect.top);
    const SIZE vp = ViewportPx();
    ScrollTo(m_scrollX, contentY - static_cast<float>(vp.cy) / 2.0f);
}

void PaneWindow::ApplyZoomRatio(float ratio) {
    if (!HasDocument())
        return;
    const SIZE vp = ViewportPx();
    ZoomAt({vp.cx / 2, vp.cy / 2}, m_zoom * ratio);
}

void PaneWindow::SetManualZoom(float zoom) {
    if (!HasDocument())
        return;
    // Pin Manual even when the fit zoom already sits at the target: ZoomAt's
    // epsilon guard would otherwise skip the mode change.
    m_zoomMode = ZoomMode::Manual;
    const SIZE vp = ViewportPx();
    ZoomAt({vp.cx / 2, vp.cy / 2}, zoom);
}

void PaneWindow::UpdateScrollBars() {
    if (!m_hwnd)
        return;
    const unsigned epoch = ++m_sbEpoch;
    const SIZE vp = ViewportPx();
    const bool active = m_state == State::Open && !m_layout.Empty();

    const int vMax = active ? std::max(0, static_cast<int>(m_layout.TotalHeight()) - 1) : 0;
    const int vPage = active ? std::max(1, static_cast<int>(vp.cy)) : 0;
    const int vPos = static_cast<int>(m_scrollY);
    const int hMax = active ? std::max(0, static_cast<int>(m_layout.TotalWidth()) - 1) : 0;
    const int hPage = active ? std::max(1, static_cast<int>(vp.cx)) : 0;
    const int hPos = static_cast<int>(m_scrollX);

    SCROLLINFO si{};
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    if (m_sbV[0] != vMax || m_sbV[1] != vPage || m_sbV[2] != vPos) {
        m_sbV[0] = vMax;
        m_sbV[1] = vPage;
        m_sbV[2] = vPos;
        si.nMax = vMax;
        si.nPage = static_cast<UINT>(vPage);
        si.nPos = vPos;
        SetScrollInfo(m_hwnd, SB_VERT, &si, TRUE);
        if (epoch != m_sbEpoch)
            return; // a bar toggled: the nested WM_SIZE -> UpdateScrollBars
                    // already pushed fresh values; ours are stale now
    }
    if (m_sbH[0] != hMax || m_sbH[1] != hPage || m_sbH[2] != hPos) {
        m_sbH[0] = hMax;
        m_sbH[1] = hPage;
        m_sbH[2] = hPos;
        si.nMax = hMax;
        si.nPage = static_cast<UINT>(hPage);
        si.nPos = hPos;
        SetScrollInfo(m_hwnd, SB_HORZ, &si, TRUE);
    }
}

void PaneWindow::OnScrollMessage(UINT msg, WPARAM wParam) {
    const bool vertical = msg == WM_VSCROLL;
    const SIZE vp = ViewportPx();
    const float line = DipToPx(48.0f);
    const float page = static_cast<float>(vertical ? vp.cy : vp.cx) * 0.9f;

    if (vertical && PagedActive()) {
        switch (LOWORD(wParam)) {
        case SB_LINEUP:
            PagedStepY(-line);
            return;
        case SB_LINEDOWN:
            PagedStepY(line);
            return;
        case SB_PAGEUP:
            PagedStepY(-page);
            return;
        case SB_PAGEDOWN:
            PagedStepY(page);
            return;
        case SB_TOP:
            AdoptPage(0);
            ScrollTo(m_scrollX, 0);
            return;
        case SB_BOTTOM:
            AdoptPage(m_layout.PageCount() - 1);
            ScrollTo(m_scrollX, m_layout.TotalHeight()); // overshoot; the band clamp lands it
            return;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: {
            SCROLLINFO si{};
            si.cbSize = sizeof(si);
            si.fMask = SIF_TRACKPOS; // 32-bit position (HIWORD(wParam) truncates)
            GetScrollInfo(m_hwnd, SB_VERT, &si);
            const float track = static_cast<float>(si.nTrackPos);
            // Document-wide thumb: dragging adopts the page under the viewport
            // center (SyncPosition's rule: page containing trackPos would bias
            // one page early on the way down), then clamps into its band.
            AdoptPage(m_layout.FirstVisible(track + static_cast<float>(vp.cy) / 2.0f));
            ScrollTo(m_scrollX, track);
            return;
        }
        default:
            return;
        }
    }

    float pos = vertical ? m_scrollY : m_scrollX;

    switch (LOWORD(wParam)) {
    case SB_LINEUP:
        pos -= line;
        break;
    case SB_LINEDOWN:
        pos += line;
        break;
    case SB_PAGEUP:
        pos -= page;
        break;
    case SB_PAGEDOWN:
        pos += page;
        break;
    case SB_TOP:
        pos = 0;
        break;
    case SB_BOTTOM:
        pos = vertical ? m_layout.TotalHeight() : m_layout.TotalWidth();
        break;
    case SB_THUMBTRACK:
    case SB_THUMBPOSITION: {
        SCROLLINFO si{};
        si.cbSize = sizeof(si);
        si.fMask = SIF_TRACKPOS; // 32-bit position (HIWORD(wParam) truncates)
        GetScrollInfo(m_hwnd, vertical ? SB_VERT : SB_HORZ, &si);
        pos = static_cast<float>(si.nTrackPos);
        break;
    }
    default:
        return;
    }
    if (vertical)
        ScrollTo(m_scrollX, pos);
    else
        ScrollTo(pos, m_scrollY);
}

void PaneWindow::OnMouseWheel(WPARAM wParam, LPARAM lParam, bool horizontal) {
    if (m_state != State::Open)
        return;
    const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
    const UINT keys = GET_KEYSTATE_WPARAM(wParam);
    POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}; // screen coordinates
    ScreenToClient(m_hwnd, &pt);

    if (!horizontal && (keys & MK_CONTROL)) {
        // Continuous zoom: fractional deltas from precision touchpads work too.
        const float factor = std::pow(1.1f, static_cast<float>(delta) / WHEEL_DELTA);
        ZoomAt(pt, m_zoom * factor);
        return;
    }

    UINT scrollLines = 3;
    SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &scrollLines, 0);
    const SIZE vp = ViewportPx();

    if (PagedActive() && !horizontal && !(keys & MK_SHIFT)) {
        // Shift+wheel and WM_MOUSEHWHEEL stay horizontal below: horizontal
        // scrolling never flips pages (universal viewer behavior).
        const float pxPerDetent = scrollLines == WHEEL_PAGESCROLL
                                      ? static_cast<float>(vp.cy)
                                      : static_cast<float>(scrollLines) * DipToPx(32.0f);
        PagedWheelY(delta, pxPerDetent);
        return;
    }

    float amount;
    if (scrollLines == WHEEL_PAGESCROLL)
        amount = static_cast<float>(delta) / WHEEL_DELTA * static_cast<float>(vp.cy);
    else
        amount = static_cast<float>(delta) / WHEEL_DELTA * static_cast<float>(scrollLines) *
                 DipToPx(32.0f);

    if (horizontal)
        ScrollBy(amount, 0); // WM_MOUSEHWHEEL: positive delta scrolls right
    else if (keys & MK_SHIFT)
        ScrollBy(-amount, 0);
    else
        ScrollBy(0, -amount); // positive delta (wheel away) scrolls up
}

bool PaneWindow::OnKeyDown(WPARAM key) {
    const bool ctrl = GetKeyState(VK_CONTROL) < 0;
    // Ctrl+4 toggles the GLOBAL scroll mode, so it must work even when the
    // focused pane has no document (the state gate below would eat it).
    if (key == '4' && ctrl && m_onToggleScrollMode) {
        m_onToggleScrollMode();
        return true;
    }
    if (m_state != State::Open)
        return false;
    const SIZE vp = ViewportPx();
    const float line = DipToPx(48.0f);
    const POINT center{vp.cx / 2, vp.cy / 2};
    // Paged LEFT/RIGHT: horizontal-first. With horizontal overflow they only
    // scroll (never flip, not even at the horizontal edge); without it they
    // always flip. The couple-of-pixels tolerance covers fit-zoom rounding,
    // which can leave TotalWidth marginally past the client width and would
    // otherwise demand a pointless 2 px scroll before every FitWidth flip.
    const bool pagedFlipLR =
        PagedActive() &&
        m_layout.TotalWidth() <= static_cast<float>(vp.cx) + DipToPx(2.0f);

    switch (key) {
    case VK_UP:
        if (PagedActive())
            PagedStepY(-line);
        else
            ScrollBy(0, -line);
        return true;
    case VK_DOWN:
        if (PagedActive())
            PagedStepY(line);
        else
            ScrollBy(0, line);
        return true;
    case VK_LEFT:
        if (pagedFlipLR)
            PagedFlip(-1);
        else
            ScrollBy(-line, 0);
        return true;
    case VK_RIGHT:
        if (pagedFlipLR)
            PagedFlip(1);
        else
            ScrollBy(line, 0);
        return true;
    case VK_PRIOR:
        if (PagedActive())
            PagedStepY(-static_cast<float>(vp.cy) * 0.9f);
        else
            ScrollBy(0, -static_cast<float>(vp.cy) * 0.9f);
        return true;
    case VK_NEXT:
        if (PagedActive())
            PagedStepY(static_cast<float>(vp.cy) * 0.9f);
        else
            ScrollBy(0, static_cast<float>(vp.cy) * 0.9f);
        return true;
    case VK_HOME:
        AdoptPage(0); // no-op in continuous
        ScrollTo(m_scrollX, 0);
        return true;
    case VK_END:
        AdoptPage(m_layout.PageCount() - 1);
        ScrollTo(m_scrollX, m_layout.TotalHeight()); // overshoot; the clamp lands it
        return true;
    case VK_OEM_PLUS:
    case VK_ADD:
        if (ctrl) {
            ZoomAt(center, m_zoom * 1.25f);
            return true;
        }
        break;
    case VK_OEM_MINUS:
    case VK_SUBTRACT:
        if (ctrl) {
            ZoomAt(center, m_zoom / 1.25f);
            return true;
        }
        break;
    case '0':
    case VK_NUMPAD0:
    case '1':
    case VK_NUMPAD1:
        if (ctrl) {
            SetManualZoom(1.0f);
            return true;
        }
        break;
    case '2':
        if (ctrl) {
            SetZoomMode(ZoomMode::FitWidth);
            return true;
        }
        break;
    case '3':
        if (ctrl) {
            SetZoomMode(ZoomMode::FitPage);
            return true;
        }
        break;
    case VK_SPACE: {
        const float dy =
            (GetKeyState(VK_SHIFT) < 0 ? -0.9f : 0.9f) * static_cast<float>(vp.cy);
        if (PagedActive())
            PagedStepY(dy);
        else
            ScrollBy(0, dy);
        return true;
    }
    case 'C':
        if (ctrl && m_hasSelection) {
            CopySelection();
            return true;
        }
        break;
    case VK_ESCAPE:
        if (m_hasSelection) {
            ClearSelection();
            Invalidate();
            return true;
        }
        break;
    default:
        break;
    }
    return false;
}

// ------------------------------------------------ text, selection, search --

void PaneWindow::OnTextPage(std::unique_ptr<Document::TextPageResult> result) {
    if (!result)
        return;
    m_textPending.erase(result->pageIndex);
    if (m_state != State::Open)
        return;
    m_textPages[result->pageIndex] = std::move(result->lines);
    Invalidate(); // a pending selection/hover may resolve now
}

void PaneWindow::OnLinks(std::unique_ptr<Document::LinksResult> result) {
    if (!result)
        return;
    m_linksPending.erase(result->pageIndex);
    if (m_state != State::Open)
        return;
    m_links[result->pageIndex] = std::move(result->links);
}

void PaneWindow::OnSearchResult(std::unique_ptr<Document::SearchResult> result) {
    if (!result || result->searchId != m_searchSeq)
        return; // superseded search
    const bool hadNone = m_matches.empty();
    for (auto& match : result->matches)
        m_matches.push_back(std::move(match));
    m_searchDone = result->done;
    if (hadNone && !m_matches.empty()) {
        m_activeMatch = 0;
        ScrollToMatch(m_matches[0]);
    }
    NotifySearchStatus();
    Invalidate();
}

void PaneWindow::StartSearch(const std::wstring& needle) {
    if (needle == m_searchNeedle)
        return; // unchanged (debounce echo)
    ++m_searchSeq;
    m_matches.clear();
    m_activeMatch = -1;
    if (needle.empty() || m_state != State::Open) {
        // Never latch a query that did not run: the same-needle guard would
        // block it forever once the document opens. Park it instead and
        // re-issue on DocumentOpened.
        m_searchNeedle.clear();
        m_pendingSearch = needle;
        m_searchDone = true;
        m_doc.CancelSearch();
    } else {
        m_pendingSearch.clear();
        m_searchNeedle = needle;
        m_searchDone = false;
        m_doc.StartSearch(needle, m_searchSeq);
    }
    NotifySearchStatus();
    Invalidate();
}

void PaneWindow::ClearSearch() {
    m_searchNeedle.clear();
    m_pendingSearch.clear();
    ++m_searchSeq;
    m_matches.clear();
    m_activeMatch = -1;
    m_searchDone = true;
    m_doc.CancelSearch();
    NotifySearchStatus();
    Invalidate();
}

void PaneWindow::GotoMatch(int delta) {
    if (m_matches.empty())
        return;
    const int n = static_cast<int>(m_matches.size());
    const int base = m_activeMatch < 0 ? 0 : m_activeMatch + delta;
    m_activeMatch = ((base % n) + n) % n;
    ScrollToMatch(m_matches[static_cast<size_t>(m_activeMatch)]);
    NotifySearchStatus();
    Invalidate();
}

void PaneWindow::NotifySearchStatus() {
    if (m_onSearchStatus)
        m_onSearchStatus(*this, m_activeMatch, static_cast<int>(m_matches.size()), m_searchDone);
}

void PaneWindow::ScrollToMatch(const Document::SearchMatch& match) {
    if (match.rects.empty() || match.pageIndex < 0 || match.pageIndex >= m_layout.PageCount())
        return;
    AdoptPage(match.pageIndex); // paged: jumps adopt their target page
    const Document::RectPt& r = match.rects.front();
    const D2D1_RECT_F pr = m_layout.PageRect(match.pageIndex);
    const float scale = DesiredScale();
    const SIZE vp = ViewportPx();
    const float cx = pr.left + (r.x0 + r.x1) / 2.0f * scale;
    const float cy = pr.top + (r.y0 + r.y1) / 2.0f * scale;
    ScrollTo(cx - static_cast<float>(vp.cx) / 2.0f, cy - static_cast<float>(vp.cy) / 2.0f);
}

// ------------------------------------------------------------------ synctex --

void PaneWindow::InverseSearchAt(POINT client) {
    if (!m_onInverseSearch)
        return;
    const auto pp = PagePointAt(client);
    if (!pp)
        return; // canvas outside any page: not an error, just no target
    if (!m_synctex.EnsureLoaded(m_docPath)) {
        m_onInverseSearch(*this, nullptr, false);
        return;
    }
    const auto hit = m_synctex.SourceAt(pp->page, pp->x, pp->y);
    m_onInverseSearch(*this, hit ? &*hit : nullptr, true);
}

bool PaneWindow::ForwardSearchTo(const std::wstring& texPath, int line) {
    if (m_state != State::Open || m_layout.Empty())
        return false;
    if (!m_synctex.EnsureLoaded(m_docPath))
        return false;
    int page = -1;
    std::vector<Document::RectPt> boxes = m_synctex.ForwardBoxes(texPath, line, page);
    if (boxes.empty() || page < 0 || page >= m_layout.PageCount())
        return false;
    FlashSyncTarget(page, std::move(boxes));
    return true;
}

void PaneWindow::FlashSyncTarget(int pageIndex, std::vector<Document::RectPt> rects) {
    if (rects.empty())
        return;
    Document::RectPt u = rects.front(); // union drives the centering
    for (const Document::RectPt& r : rects) {
        u.x0 = std::min(u.x0, r.x0);
        u.y0 = std::min(u.y0, r.y0);
        u.x1 = std::max(u.x1, r.x1);
        u.y1 = std::max(u.y1, r.y1);
    }
    const D2D1_RECT_F pr = m_layout.PageRect(pageIndex);
    const float scale = DesiredScale();
    if (u.x1 - u.x0 < 2.0f) {
        // Degenerate target (kern/glue-only nodes): flash a full-width band
        // at that height instead of an invisible sliver.
        const float pageWidthPt = (pr.right - pr.left) / scale;
        for (Document::RectPt& r : rects) {
            r.x0 = 0;
            r.x1 = pageWidthPt;
        }
    }
    const SIZE vp = ViewportPx();
    const float cx = pr.left + (u.x0 + u.x1) / 2.0f * scale;
    const float cy = pr.top + (u.y0 + u.y1) / 2.0f * scale;
    AdoptPage(pageIndex); // paged: jumps adopt their target page
    ScrollTo(cx - static_cast<float>(vp.cx) / 2.0f, cy - static_cast<float>(vp.cy) / 2.0f);
    m_syncMark.pageIndex = pageIndex;
    m_syncMark.rects = std::move(rects);
    SetTimer(m_hwnd, kSyncFlashTimer, kSyncFlashMs, nullptr);
    Invalidate();
}

std::optional<PageLayout::PagePoint> PaneWindow::PagePointAt(POINT client) const {
    if (m_state != State::Open || m_layout.Empty())
        return std::nullopt;
    const D2D1_POINT_2F origin = ContentOrigin();
    const auto pp = m_layout.HitTest(static_cast<float>(client.x) - origin.x,
                                     static_cast<float>(client.y) - origin.y);
    // Paged: neighbors exist in layout space but are not drawn; hovering or
    // clicking where they would be (a centered short page leaves reachable
    // strips) must not resolve to their links/text/SyncTeX positions.
    if (pp && PagedActive() && pp->page != m_currentPage)
        return std::nullopt;
    return pp;
}

std::optional<PaneWindow::CaretPos> PaneWindow::CaretAt(POINT client, bool clampToNearest) {
    if (m_state != State::Open || m_layout.Empty())
        return std::nullopt;
    const D2D1_POINT_2F origin = ContentOrigin();
    const float cx = static_cast<float>(client.x) - origin.x;
    const float cy = static_cast<float>(client.y) - origin.y;

    int page = 0;
    float px = 0;
    float py = 0;
    auto pp = m_layout.HitTest(cx, cy);
    if (pp && PagedActive() && pp->page != m_currentPage)
        pp.reset(); // undrawn neighbor: treat as a miss, never select into it
    if (pp) {
        page = pp->page;
        px = pp->x;
        py = pp->y;
    } else if (clampToNearest) {
        page = PagedActive()
                   ? m_currentPage // pin to the only visible page
                   : std::min(m_layout.FirstVisible(cy), m_layout.PageCount() - 1);
        const D2D1_RECT_F rect = m_layout.PageRect(page);
        const float scale = DesiredScale();
        px = (std::clamp(cx, rect.left, rect.right) - rect.left) / scale;
        py = (std::clamp(cy, rect.top, rect.bottom) - rect.top) / scale;
    } else {
        return std::nullopt;
    }

    const auto it = m_textPages.find(page);
    if (it == m_textPages.end()) {
        EnsureTextPage(page, true); // selection wants it now
        return std::nullopt;
    }
    const auto& lines = it->second;
    if (lines.empty())
        return CaretPos{page, 0, 0};

    // Nearest line: vertical distance first, horizontal as tie-break, or a
    // click in the right column of a two-column page would resolve to the
    // left column's line sharing the same y-band.
    int bestLine = 0;
    float bestVert = FLT_MAX;
    float bestHorz = FLT_MAX;
    for (int li = 0; li < static_cast<int>(lines.size()); ++li) {
        const Document::RectPt& box = lines[static_cast<size_t>(li)].box;
        const float dv = py < box.y0 ? box.y0 - py : (py > box.y1 ? py - box.y1 : 0);
        const float dh = px < box.x0 ? box.x0 - px : (px > box.x1 ? px - box.x1 : 0);
        if (dv < bestVert || (dv == bestVert && dh < bestHorz)) {
            bestVert = dv;
            bestHorz = dh;
            bestLine = li;
            if (dv == 0 && dh == 0)
                break;
        }
    }
    const auto& chars = lines[static_cast<size_t>(bestLine)].chars;
    int ch = static_cast<int>(chars.size());
    for (int k = 0; k < static_cast<int>(chars.size()); ++k) {
        const Document::RectPt& box = chars[static_cast<size_t>(k)].box;
        if (px < (box.x0 + box.x1) / 2.0f) {
            ch = k;
            break;
        }
    }
    return CaretPos{page, bestLine, ch};
}

void PaneWindow::EnsureTextPage(int page, bool urgent) {
    if (m_state != State::Open || page < 0 || page >= m_layout.PageCount())
        return;
    if (m_textPages.count(page) != 0 || m_textPending.count(page) != 0)
        return;
    m_textPending.insert(page);
    m_doc.RequestTextPage(page, urgent);
}

void PaneWindow::EnsureLinks(int page) {
    if (m_state != State::Open || page < 0 || page >= m_layout.PageCount())
        return;
    if (m_links.count(page) != 0 || m_linksPending.count(page) != 0)
        return;
    m_linksPending.insert(page);
    m_doc.RequestLinks(page);
}

bool PaneWindow::TextAt(int page, float px, float py) const {
    const auto it = m_textPages.find(page);
    if (it == m_textPages.end())
        return false;
    for (const Document::TextLine& line : it->second) {
        if (py >= line.box.y0 && py <= line.box.y1 && px >= line.box.x0 - 2.0f &&
            px <= line.box.x1 + 2.0f)
            return true;
    }
    return false;
}

const Document::LinkInfo* PaneWindow::LinkAt(int page, float px, float py) const {
    const auto it = m_links.find(page);
    if (it == m_links.end())
        return nullptr;
    for (const Document::LinkInfo& link : it->second) {
        if (px >= link.box.x0 && px <= link.box.x1 && py >= link.box.y0 && py <= link.box.y1)
            return &link;
    }
    return nullptr;
}

void PaneWindow::ActivateLink(const Document::LinkInfo& link) {
    if (link.targetPage >= 0 && link.targetPage < m_layout.PageCount()) {
        // Adopt only inside the validity guard: external links carry
        // targetPage == -1 and adopting that (clamped to 0) would teleport
        // the paged view to page 0 on every URL click.
        AdoptPage(link.targetPage);
        const D2D1_RECT_F pr = m_layout.PageRect(link.targetPage);
        ScrollTo(m_scrollX, pr.top + link.targetY * DesiredScale() - DipToPx(8.0f));
    } else if (!link.uri.empty()) {
        ShellExecuteW(m_hwnd, L"open", link.uri.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
}

void PaneWindow::ClearSelection() {
    // A document swap mid-drag (Ctrl+W, auto-reload) must not leave the mouse
    // captured: WM_LBUTTONUP only calls ReleaseCapture while m_selecting is
    // still true, so a latched capture would eat every click in the app.
    if (m_selecting && GetCapture() == m_hwnd)
        ReleaseCapture();
    m_selecting = false;
    m_hasSelection = false;
}

std::wstring PaneWindow::SelectionText() const {
    if (!m_hasSelection)
        return {};
    CaretPos lo = m_selAnchor;
    CaretPos hi = m_selFocus;
    if (hi < lo)
        std::swap(lo, hi);

    std::wstring out;
    for (int page = lo.page; page <= hi.page; ++page) {
        const auto it = m_textPages.find(page);
        if (it == m_textPages.end())
            continue; // model evicted/not loaded; copy what we have
        const auto& lines = it->second;
        const int lineStart = page == lo.page ? std::max(lo.line, 0) : 0;
        const int lineEnd =
            page == hi.page ? std::min(hi.line, static_cast<int>(lines.size()) - 1)
                            : static_cast<int>(lines.size()) - 1;
        for (int li = lineStart; li <= lineEnd; ++li) {
            const auto& chars = lines[static_cast<size_t>(li)].chars;
            int a = (page == lo.page && li == lo.line) ? lo.ch : 0;
            int b = (page == hi.page && li == hi.line) ? hi.ch : static_cast<int>(chars.size());
            a = std::clamp(a, 0, static_cast<int>(chars.size()));
            b = std::clamp(b, 0, static_cast<int>(chars.size()));
            if (a >= b)
                continue;
            if (!out.empty())
                out += L"\r\n";
            for (int k = a; k < b; ++k)
                AppendCodepoint(out, chars[static_cast<size_t>(k)].codepoint);
        }
    }
    return out;
}

void PaneWindow::CopySelection() {
    const std::wstring text = SelectionText();
    if (text.empty())
        return;

    // Fill the buffer BEFORE touching the clipboard: EmptyClipboard destroys
    // the previous content, so it must only run once the data is ready.
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!mem)
        return;
    if (void* dst = GlobalLock(mem)) {
        memcpy(dst, text.c_str(), bytes);
        GlobalUnlock(mem);
    } else {
        GlobalFree(mem);
        return;
    }

    // OpenClipboard fails transiently while clipboard monitors (Win+V
    // history, RDP) read a recent update; retry briefly like mainstream apps.
    bool opened = false;
    for (int attempt = 0; attempt < 5 && !opened; ++attempt) {
        opened = OpenClipboard(m_hwnd) != FALSE;
        if (!opened)
            Sleep(10);
    }
    if (!opened) {
        GlobalFree(mem);
        return;
    }
    EmptyClipboard();
    if (!SetClipboardData(CF_UNICODETEXT, mem))
        GlobalFree(mem);
    CloseClipboard();
}

void PaneWindow::DrawOverlays(ID2D1SolidColorBrush* brush, int page, const D2D1_RECT_F& dest,
                              float scale) {
    // Selection: per-line boxes from the UI-side text model.
    if (m_hasSelection || m_selecting) {
        CaretPos lo = m_selAnchor;
        CaretPos hi = m_selFocus;
        if (hi < lo)
            std::swap(lo, hi);
        if (page >= lo.page && page <= hi.page) {
            const auto it = m_textPages.find(page);
            if (it != m_textPages.end()) {
                brush->SetColor(D2D1::ColorF(0x3390FF, 0.30f));
                const auto& lines = it->second;
                const int lineStart = page == lo.page ? std::max(lo.line, 0) : 0;
                const int lineEnd =
                    page == hi.page ? std::min(hi.line, static_cast<int>(lines.size()) - 1)
                                    : static_cast<int>(lines.size()) - 1;
                for (int li = lineStart; li <= lineEnd; ++li) {
                    const auto& line = lines[static_cast<size_t>(li)];
                    int a = (page == lo.page && li == lo.line) ? lo.ch : 0;
                    int b = (page == hi.page && li == hi.line)
                                ? hi.ch
                                : static_cast<int>(line.chars.size());
                    a = std::clamp(a, 0, static_cast<int>(line.chars.size()));
                    b = std::clamp(b, 0, static_cast<int>(line.chars.size()));
                    if (a >= b)
                        continue;
                    // Union of the selected chars' extents: chars are stored
                    // in logical order, so on RTL lines x decreases with the
                    // index and endpoints alone would build an inverted rect.
                    float x0 = line.chars[static_cast<size_t>(a)].box.x0;
                    float x1 = line.chars[static_cast<size_t>(a)].box.x1;
                    for (int k = a + 1; k < b; ++k) {
                        const Document::RectPt& cb = line.chars[static_cast<size_t>(k)].box;
                        x0 = std::min(x0, cb.x0);
                        x1 = std::max(x1, cb.x1);
                    }
                    m_d2dContext->FillRectangle(
                        D2D1::RectF(dest.left + x0 * scale, dest.top + line.box.y0 * scale,
                                    dest.left + x1 * scale, dest.top + line.box.y1 * scale),
                        brush);
                }
            } else {
                EnsureTextPage(page, true);
            }
        }
    }

    // Search highlights (active match in a distinct color).
    if (!m_matches.empty()) {
        for (size_t mi = 0; mi < m_matches.size(); ++mi) {
            const Document::SearchMatch& match = m_matches[mi];
            if (match.pageIndex != page)
                continue;
            brush->SetColor(static_cast<int>(mi) == m_activeMatch
                                ? D2D1::ColorF(0xFF8C00, 0.55f)
                                : D2D1::ColorF(0xFFD400, 0.35f));
            for (const Document::RectPt& r : match.rects) {
                m_d2dContext->FillRectangle(
                    D2D1::RectF(dest.left + r.x0 * scale, dest.top + r.y0 * scale,
                                dest.left + r.x1 * scale, dest.top + r.y1 * scale),
                    brush);
            }
        }
    }

    // SyncTeX forward-search flash: transient (kSyncFlashTimer clears it),
    // drawn last so it wins over a coincident search match. Green: distinct
    // from the blue selection and the orange/yellow matches. The ±1.5 pt
    // vertical inflation keeps thin hboxes visible.
    if (m_syncMark.pageIndex == page) {
        brush->SetColor(D2D1::ColorF(0x00A550, 0.40f));
        for (const Document::RectPt& r : m_syncMark.rects) {
            m_d2dContext->FillRectangle(
                D2D1::RectF(dest.left + r.x0 * scale, dest.top + (r.y0 - 1.5f) * scale,
                            dest.left + r.x1 * scale, dest.top + (r.y1 + 1.5f) * scale),
                brush);
        }
    }
}

void PaneWindow::EvictStale(int firstKeep, int lastKeep) {
    // Previews: keep only the visible range plus one page on each side.
    for (auto it = m_previews.begin(); it != m_previews.end();) {
        if (it->first < firstKeep || it->first > lastKeep)
            it = m_previews.erase(it);
        else
            ++it;
    }
    // Text and link models follow the same range, but pages spanned by an
    // active selection must survive (copy needs their text).
    int selLo = INT_MAX;
    int selHi = INT_MIN;
    if (m_hasSelection || m_selecting) {
        selLo = std::min(m_selAnchor.page, m_selFocus.page);
        selHi = std::max(m_selAnchor.page, m_selFocus.page);
    }
    for (auto it = m_textPages.begin(); it != m_textPages.end();) {
        const int page = it->first;
        const bool keep =
            (page >= firstKeep && page <= lastKeep) || (page >= selLo && page <= selHi);
        it = keep ? std::next(it) : m_textPages.erase(it);
    }
    for (auto it = m_links.begin(); it != m_links.end();) {
        if (it->first < firstKeep || it->first > lastKeep)
            it = m_links.erase(it);
        else
            ++it;
    }
    // Tiles: anything not touched by this paint (scrolled/zoomed away, or the
    // grid resolution changed) is dead weight; the preview backs re-entry.
    // Cancel in-flight renders too: the page-granular wanted range cannot drop
    // same-page tile jobs, and abandoned tiles would otherwise render fully
    // just to be discarded, starving previews and burning CPU.
    for (auto it = m_tiles.begin(); it != m_tiles.end();) {
        if (it->second.lastUsed != m_frame) {
            if (it->second.pendingId != 0)
                m_doc.CancelRender(it->first.page, it->first.res, it->first.row, it->first.col);
            it = m_tiles.erase(it);
        } else {
            ++it;
        }
    }
}
