#include "MainWindow.h"

#include "util/Settings.h"

#include <commctrl.h> // SetWindowSubclass for the find-box keyboard handling
#include <commdlg.h>  // excluded from windows.h by WIN32_LEAN_AND_MEAN

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

namespace {

constexpr int kSplitterDip = 6;
constexpr int kMinPaneDip = 120;
constexpr int kInitialWidthDip = 1100;
constexpr int kInitialHeightDip = 760;
constexpr UINT_PTR kFindDebounceTimer = 2;
constexpr PCWSTR kFindBarClass = L"PsvFindBar";

// The find bar is a plain container: forward its children's notifications to
// the main window, which owns all command handling.
LRESULT CALLBACK FindBarProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_COMMAND)
        return SendMessageW(GetParent(hwnd), msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK FindEditProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                              UINT_PTR /*id*/, DWORD_PTR refData) {
    const HWND main = reinterpret_cast<HWND>(refData);
    if (msg == WM_KEYDOWN) {
        if (wParam == VK_RETURN) {
            PostMessageW(main, WM_COMMAND,
                         GetKeyState(VK_SHIFT) < 0 ? IDC_FIND_PREV : IDC_FIND_NEXT, 0);
            return 0;
        }
        if (wParam == VK_ESCAPE) {
            PostMessageW(main, WM_COMMAND, IDC_FIND_CLOSE, 0);
            return 0;
        }
    }
    if (msg == WM_CHAR && (wParam == VK_RETURN || wParam == VK_ESCAPE))
        return 0; // suppress the beep
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

} // namespace

void MainWindow::RegisterWindowClass(HINSTANCE hinst) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hinst;
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);

    WNDCLASSEXW fb{};
    fb.cbSize = sizeof(fb);
    fb.lpfnWndProc = FindBarProc;
    fb.hInstance = hinst;
    fb.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    fb.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    fb.lpszClassName = kFindBarClass;
    RegisterClassExW(&fb);
}

MainWindow::~MainWindow() {
    if (m_bgBrush)
        DeleteObject(m_bgBrush);
}

bool MainWindow::Create(HINSTANCE hinst, int nCmdShow, std::wstring leftFile,
                        std::wstring rightFile) {
    m_dx.EnsureCreated(); // fail fast in wWinMain if graphics init is impossible
    m_left = std::make_unique<PaneWindow>(m_dx, L"Left pane\nCtrl+O to open a PDF");
    m_right = std::make_unique<PaneWindow>(m_dx, L"Right pane\nCtrl+Shift+O to open a PDF");
    m_sync = std::make_unique<SyncController>(*m_left, *m_right);

    CreateWindowExW(0, kClassName, L"PdfSideViewer", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                    CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, nullptr, nullptr,
                    hinst, this);
    if (!m_hwnd)
        return false;

    // Size the window only after creation: DPI assignment (and a possible
    // WM_DPICHANGED) can happen during CreateWindowExW, so this is the first
    // moment GetDpiForWindow is authoritative.
    m_dpi = GetDpiForWindow(m_hwnd);
    SetWindowPos(m_hwnd, nullptr, 0, 0, MulDiv(kInitialWidthDip, m_dpi, 96),
                 MulDiv(kInitialHeightDip, m_dpi, 96), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

    const auto onViewChanged = [this](PaneWindow& p, PaneWindow::ViewEvent e, float r) {
        if (e == PaneWindow::ViewEvent::FocusGained) {
            if (m_outlineVisible && m_outlinePane != &p)
                UpdateOutlineSidebar(&p);
            else
                m_outlinePane = &p;
            return;
        }
        m_sync->OnViewChanged(p, e, r);
        if (e == PaneWindow::ViewEvent::DocumentOpened && m_outlineVisible &&
            m_outlinePane == &p)
            UpdateOutlineSidebar(&p);
    };
    m_left->SetViewChangedHandler(onViewChanged);
    m_right->SetViewChangedHandler(onViewChanged);
    m_left->SetOpenSiblingHandler([this](std::wstring p) { m_right->OpenDocument(std::move(p)); });
    m_right->SetOpenSiblingHandler([this](std::wstring p) { m_left->OpenDocument(std::move(p)); });

    const auto onSearchStatus = [this](PaneWindow& pane, int active, int total, bool done) {
        if (&pane != m_findTarget || !m_findCount)
            return;
        wchar_t buffer[64];
        if (total == 0)
            wcscpy_s(buffer, done ? L"0" : L"…");
        else
            swprintf_s(buffer, L"%d/%d%s", active + 1, total, done ? L"" : L"+");
        SetWindowTextW(m_findCount, buffer);
    };
    m_left->SetSearchStatusHandler(onSearchStatus);
    m_right->SetSearchStatusHandler(onSearchStatus);

    // Loaded even when the command line wins: the pane sessions seed the
    // SaveSession fallbacks so an unopened pane never wipes saved state.
    const AppSettings session = AppSettings::Load();
    const float dpiRatio = static_cast<float>(m_dpi) / static_cast<float>(session.dpi);
    m_fallbackLeft = session.left;
    m_fallbackLeft.scrollX *= dpiRatio;
    m_fallbackLeft.scrollY *= dpiRatio;
    m_fallbackRight = session.right;
    m_fallbackRight.scrollX *= dpiRatio;
    m_fallbackRight.scrollY *= dpiRatio;

    if (!leftFile.empty() || !rightFile.empty()) {
        // Explicit command line wins over the saved session.
        if (!leftFile.empty())
            m_left->OpenDocument(std::move(leftFile));
        if (!rightFile.empty())
            m_right->OpenDocument(std::move(rightFile));
    } else {
        ApplySession(session);
    }
    UpdateTitle();

    ShowWindow(m_hwnd, m_startMaximized ? SW_SHOWMAXIMIZED : nCmdShow);
    UpdateWindow(m_hwnd);
    SetFocus(m_left->Hwnd());
    return true;
}

void MainWindow::ApplySession(const AppSettings& session) {
    m_splitRatio = session.splitRatio;
    m_sync->SetZoomSync(session.zoomSync);
    // m_fallback* already hold the DPI-rescaled offsets; a later WM_DPICHANGED
    // (e.g. from the placement below) rescales the panes' pending restores.
    if (!m_fallbackLeft.path.empty() &&
        GetFileAttributesW(m_fallbackLeft.path.c_str()) != INVALID_FILE_ATTRIBUTES)
        m_left->OpenDocumentWithView(m_fallbackLeft.path, m_fallbackLeft.zoom,
                                     m_fallbackLeft.scrollX, m_fallbackLeft.scrollY,
                                     static_cast<PaneWindow::ZoomMode>(m_fallbackLeft.zoomMode));
    if (!m_fallbackRight.path.empty() &&
        GetFileAttributesW(m_fallbackRight.path.c_str()) != INVALID_FILE_ATTRIBUTES)
        m_right->OpenDocumentWithView(
            m_fallbackRight.path, m_fallbackRight.zoom, m_fallbackRight.scrollX,
            m_fallbackRight.scrollY,
            static_cast<PaneWindow::ZoomMode>(m_fallbackRight.zoomMode));
    // After the restored positions land, DocumentOpened events recapture the
    // anchor, so enabling scroll sync here preserves the saved alignment.
    m_sync->SetScrollSync(session.scrollSync);
    // Apply the placement only if the rect still touches a live monitor
    // (after a monitor disconnect the window would come up fully off-screen),
    // and without showing: Create issues the single ShowWindow, honoring the
    // maximized flag (a second ShowWindow(SW_SHOWNORMAL) would un-maximize).
    if (session.hasPlacement &&
        MonitorFromRect(&session.normalRect, MONITOR_DEFAULTTONULL) != nullptr) {
        WINDOWPLACEMENT wp{};
        wp.length = sizeof(wp);
        wp.rcNormalPosition = session.normalRect;
        wp.showCmd = SW_HIDE;
        SetWindowPlacement(m_hwnd, &wp);
        m_startMaximized = session.maximized;
    }
    Layout();
}

void MainWindow::SaveSession() const {
    AppSettings s;
    WINDOWPLACEMENT wp{};
    wp.length = sizeof(wp);
    if (GetWindowPlacement(m_hwnd, &wp)) {
        s.hasPlacement = true;
        s.normalRect = wp.rcNormalPosition;
        s.maximized = wp.showCmd == SW_SHOWMAXIMIZED;
    }
    s.splitRatio = m_splitRatio;
    s.scrollSync = m_sync->ScrollSync();
    s.zoomSync = m_sync->ZoomSync();
    s.dpi = m_dpi;
    s.left = m_left->HasPersistableDocument()
                 ? PaneSettings{m_left->DocumentPath(), m_left->PersistZoom(),
                                m_left->PersistScrollX(), m_left->PersistScrollY(),
                                static_cast<int>(m_left->PersistZoomMode())}
                 : m_fallbackLeft;
    s.right = m_right->HasPersistableDocument()
                  ? PaneSettings{m_right->DocumentPath(), m_right->PersistZoom(),
                                 m_right->PersistScrollX(), m_right->PersistScrollY(),
                                 static_cast<int>(m_right->PersistZoomMode())}
                  : m_fallbackRight;
    s.Save();
}

void MainWindow::UpdateTitle() {
    std::wstring title = L"PdfSideViewer";
    if (m_sync->ScrollSync())
        title += L"  [scroll sync]";
    if (m_sync->ZoomSync())
        title += L"  [zoom sync]";
    SetWindowTextW(m_hwnd, title.c_str());
}

LRESULT CALLBACK MainWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    MainWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<MainWindow*>(cs->lpCreateParams);
        self->m_hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self)
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    return self->HandleMessage(msg, wParam, lParam);
}

LRESULT MainWindow::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        m_dpi = GetDpiForWindow(m_hwnd);
        m_left->Create(m_hwnd, 100);
        m_right->Create(m_hwnd, 101);
        CreateFindBar(); // after the panes: overlays must be above them
        m_outlineTree = CreateWindowExW(
            0, WC_TREEVIEWW, nullptr,
            WS_CHILD | WS_CLIPSIBLINGS | WS_BORDER | TVS_HASBUTTONS | TVS_HASLINES |
                TVS_LINESATROOT | TVS_SHOWSELALWAYS,
            0, 0, 0, 0, m_hwnd, reinterpret_cast<HMENU>(IDC_OUTLINE_TREE),
            reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(m_hwnd, GWLP_HINSTANCE)), nullptr);
        UpdateUiFont(); // find bar + tree
        UpdateTheme();  // after the pane HWNDs exist
        Layout();
        return 0;
    }

    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED)
            Layout();
        return 0;

    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        mmi->ptMinTrackSize = {MulDiv(480, m_dpi, 96), MulDiv(320, m_dpi, 96)};
        return 0;
    }

    case WM_DPICHANGED: {
        m_dpi = HIWORD(wParam);
        UpdateUiFont();
        // Panes must know the new DPI before SetWindowPos: its synchronous
        // WM_SIZE cascade rebuilds and presents their targets immediately.
        m_left->OnDpiChanged(m_dpi);
        m_right->OnDpiChanged(m_dpi);
        const RECT* suggested = reinterpret_cast<const RECT*>(lParam);
        SetWindowPos(m_hwnd, nullptr, suggested->left, suggested->top,
                     suggested->right - suggested->left, suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        Layout();
        return 0;
    }

    case WM_PAINT: { // only the splitter band is visible (WS_CLIPCHILDREN)
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(m_hwnd, &ps);
        FillRect(hdc, &ps.rcPaint, m_bgBrush);
        EndPaint(m_hwnd, &ps);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT) {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(m_hwnd, &pt);
            const RECT splitter = SplitterRect();
            if (PtInRect(&splitter, pt)) {
                SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
                return TRUE;
            }
        }
        break;

    case WM_LBUTTONDOWN: {
        const POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        const RECT splitter = SplitterRect();
        if (PtInRect(&splitter, pt)) {
            m_draggingSplitter = true;
            SetCapture(m_hwnd);
        }
        return 0;
    }

    case WM_MOUSEMOVE:
        if (m_draggingSplitter) {
            SetSplitRatioFromX(GET_X_LPARAM(lParam));
            Layout();
        }
        return 0;

    case WM_LBUTTONUP:
        if (m_draggingSplitter)
            ReleaseCapture();
        return 0;

    case WM_CAPTURECHANGED:
        m_draggingSplitter = false;
        return 0;

    case WM_LBUTTONDBLCLK: {
        const POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        const RECT splitter = SplitterRect();
        if (PtInRect(&splitter, pt)) {
            m_splitRatio = 0.5f;
            Layout();
        }
        return 0;
    }

    case WM_TIMER:
        if (wParam == kFindDebounceTimer) {
            KillTimer(m_hwnd, kFindDebounceTimer);
            if (m_findTarget && m_findBar && IsWindowVisible(m_findBar)) {
                wchar_t text[256];
                GetWindowTextW(m_findEdit, text, ARRAYSIZE(text));
                m_findTarget->StartSearch(text);
            }
            return 0;
        }
        break;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_FIND_EDIT && HIWORD(wParam) == EN_CHANGE) {
            SetTimer(m_hwnd, kFindDebounceTimer, 350, nullptr); // debounce live search
            return 0;
        }
        switch (LOWORD(wParam)) {
        case IDC_OPEN_LEFT:
            OpenDocumentDialog(false);
            return 0;
        case IDC_OPEN_RIGHT:
            OpenDocumentDialog(true);
            return 0;
        case IDC_FOCUS_NEXT_PANE:
            SetFocus(GetFocus() == m_left->Hwnd() ? m_right->Hwnd() : m_left->Hwnd());
            return 0;
        case IDC_TOGGLE_SCROLL_SYNC:
            m_sync->SetScrollSync(!m_sync->ScrollSync());
            UpdateTitle();
            return 0;
        case IDC_TOGGLE_ZOOM_SYNC:
            m_sync->SetZoomSync(!m_sync->ZoomSync());
            UpdateTitle();
            return 0;
        case IDC_FIND_SHOW:
            ShowFindBar();
            return 0;
        case IDC_FIND_NEXT:
            if (m_findTarget && m_findBar && IsWindowVisible(m_findBar))
                m_findTarget->GotoMatch(+1);
            return 0;
        case IDC_FIND_PREV:
            if (m_findTarget && m_findBar && IsWindowVisible(m_findBar))
                m_findTarget->GotoMatch(-1);
            return 0;
        case IDC_FIND_CLOSE:
            CloseFindBar();
            return 0;
        case IDC_TOGGLE_OUTLINE: {
            m_outlineVisible = !m_outlineVisible;
            if (m_outlineVisible) {
                PaneWindow* pane = GetFocus() == m_right->Hwnd() ? m_right.get()
                                                                 : m_left.get();
                UpdateOutlineSidebar(pane);
            } else if (m_outlineTree && GetFocus() == m_outlineTree) {
                // Hiding a focused window does not move keyboard focus: the
                // invisible tree would keep eating arrow keys and navigating.
                SetFocus((m_outlinePane ? m_outlinePane : m_left.get())->Hwnd());
            }
            Layout();
            return 0;
        }
        default:
            break;
        }
        break;

    case WM_NOTIFY: {
        const NMHDR* hdr = reinterpret_cast<const NMHDR*>(lParam);
        if (hdr->idFrom == IDC_OUTLINE_TREE && hdr->code == TVN_SELCHANGEDW &&
            m_outlineVisible && !m_populatingOutline && m_outlinePane) {
            const NMTREEVIEWW* tv = reinterpret_cast<const NMTREEVIEWW*>(lParam);
            if (tv->itemNew.hItem)
                m_outlinePane->GotoOutlineItem(static_cast<int>(tv->itemNew.lParam));
            return 0;
        }
        break;
    }

    case WM_ACTIVATE:
        // Snapshot the focused child before deactivation completes; GetFocus()
        // still identifies it here (WM_KILLFOCUS comes later). Any descendant
        // counts: panes, the outline tree, the find box.
        if (LOWORD(wParam) == WA_INACTIVE && m_left) {
            const HWND focus = GetFocus();
            if (focus && IsChild(m_hwnd, focus))
                m_lastPaneFocus = focus;
        }
        break;

    case WM_SETFOCUS:
        // The frame never keeps keyboard focus (DefWindowProc gives it to the
        // frame on Alt+Tab reactivation): forward it to the last-focused
        // child, if it is still alive and visible (the outline/find bar may
        // have been hidden meanwhile). Never during teardown: bouncing focus
        // onto a dying child re-enters DestroyWindow's focus handling and the
        // app never exits.
        if (!m_destroying && m_left && m_left->Hwnd() && IsWindow(m_left->Hwnd())) {
            const bool restorable = m_lastPaneFocus && IsWindow(m_lastPaneFocus) &&
                                    IsWindowVisible(m_lastPaneFocus);
            SetFocus(restorable ? m_lastPaneFocus : m_left->Hwnd());
            return 0;
        }
        break;

    case WM_SETTINGCHANGE:
        if (lParam && wcscmp(reinterpret_cast<PCWSTR>(lParam), L"ImmersiveColorSet") == 0)
            UpdateTheme();
        break;

    case WM_CLOSE:
        SaveSession();
        m_destroying = true;
        break; // DefWindowProc destroys the window

    case WM_DESTROY:
        m_destroying = true;
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }
    return DefWindowProcW(m_hwnd, msg, wParam, lParam);
}

void MainWindow::Layout() {
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    const int h = rc.bottom;
    const int sidebarW = m_outlineVisible ? MulDiv(260, m_dpi, 96) : 0;
    const int x0 = sidebarW;
    const int w = rc.right - x0;
    if (w <= 0 || h <= 0 || !m_left || !m_left->Hwnd())
        return;

    if (m_outlineTree) {
        if (m_outlineVisible)
            MoveWindow(m_outlineTree, 0, 0, sidebarW, h, TRUE);
        ShowWindow(m_outlineTree, m_outlineVisible ? SW_SHOW : SW_HIDE);
    }

    const int splitterW = MulDiv(kSplitterDip, m_dpi, 96);
    const int minPane = MulDiv(kMinPaneDip, m_dpi, 96);
    int leftW = static_cast<int>(static_cast<float>(w - splitterW) * m_splitRatio + 0.5f);
    if (w - splitterW >= 2 * minPane)
        leftW = std::clamp(leftW, minPane, w - splitterW - minPane);
    else
        leftW = (w - splitterW) / 2;
    m_splitterX = x0 + leftW;

    HDWP dwp = BeginDeferWindowPos(2);
    dwp = DeferWindowPos(dwp, m_left->Hwnd(), nullptr, x0, 0, leftW, h,
                         SWP_NOZORDER | SWP_NOACTIVATE);
    dwp = DeferWindowPos(dwp, m_right->Hwnd(), nullptr, x0 + leftW + splitterW, 0,
                         w - leftW - splitterW, h, SWP_NOZORDER | SWP_NOACTIVATE);
    EndDeferWindowPos(dwp);
    LayoutFindBar();
    InvalidateRect(m_hwnd, nullptr, TRUE); // repaint the splitter band
}

// ------------------------------------------------------------- find bar ----

void MainWindow::CreateFindBar() {
    const HINSTANCE hinst =
        reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(m_hwnd, GWLP_HINSTANCE));
    m_findBar = CreateWindowExW(0, kFindBarClass, nullptr, WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 0,
                                0, m_hwnd, nullptr, hinst, nullptr);
    m_findEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0,
                                 m_findBar, reinterpret_cast<HMENU>(IDC_FIND_EDIT), hinst,
                                 nullptr);
    m_findCount = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_CENTER, 0, 0, 0,
                                  0, m_findBar, nullptr, hinst, nullptr);
    m_findPrev = CreateWindowExW(0, L"BUTTON", L"<", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0,
                                 0, m_findBar, reinterpret_cast<HMENU>(IDC_FIND_PREV), hinst,
                                 nullptr);
    m_findNext = CreateWindowExW(0, L"BUTTON", L">", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 0,
                                 0, m_findBar, reinterpret_cast<HMENU>(IDC_FIND_NEXT), hinst,
                                 nullptr);
    m_findClose = CreateWindowExW(0, L"BUTTON", L"X", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0,
                                  0, 0, m_findBar, reinterpret_cast<HMENU>(IDC_FIND_CLOSE),
                                  hinst, nullptr);
    SetWindowSubclass(m_findEdit, FindEditProc, 1, reinterpret_cast<DWORD_PTR>(m_hwnd));
    UpdateUiFont();
}

void MainWindow::UpdateUiFont() {
    if (!m_findBar)
        return;
    NONCLIENTMETRICSW ncm{};
    ncm.cbSize = sizeof(ncm);
    if (!SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, m_dpi))
        return;
    HFONT font = CreateFontIndirectW(&ncm.lfMessageFont);
    if (!font)
        return;
    for (HWND child :
         {m_findEdit, m_findCount, m_findPrev, m_findNext, m_findClose, m_outlineTree}) {
        if (child)
            SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
    if (m_uiFont)
        DeleteObject(m_uiFont);
    m_uiFont = font;
}

void MainWindow::UpdateOutlineSidebar(PaneWindow* pane) {
    if (!m_outlineTree)
        return;
    if (!pane)
        pane = m_outlinePane ? m_outlinePane : m_left.get();
    m_outlinePane = pane;
    if (!m_outlineVisible)
        return;

    m_populatingOutline = true;
    SendMessageW(m_outlineTree, WM_SETREDRAW, FALSE, 0);
    TreeView_DeleteAllItems(m_outlineTree);
    const auto& outline = pane->Outline();
    std::vector<HTREEITEM> parents;  // parents[d] = last inserted item at depth d
    std::vector<HTREEITEM> parentAt; // parentAt[d] = hParent that parents[d] hangs under
    for (size_t i = 0; i < outline.size(); ++i) {
        const Document::OutlineItem& item = outline[i];
        const auto d = static_cast<size_t>(item.depth);
        TVINSERTSTRUCTW ins{};
        ins.hParent = item.depth > 0 && item.depth <= static_cast<int>(parents.size())
                          ? parents[d - 1]
                          : TVI_ROOT;
        // Appending after the cached previous sibling is O(1); TVI_LAST walks
        // the whole sibling chain (O(N^2) rebuilds for large outlines). The
        // parentAt check guards against a stale cache entry from a previous
        // subtree (depth sequences like 0,1,0,1).
        ins.hInsertAfter = (d < parents.size() && parentAt[d] == ins.hParent)
                               ? parents[d]
                               : TVI_LAST;
        ins.item.mask = TVIF_TEXT | TVIF_PARAM;
        ins.item.pszText = const_cast<wchar_t*>(item.title.c_str());
        ins.item.lParam = static_cast<LPARAM>(i);
        const HTREEITEM h = TreeView_InsertItem(m_outlineTree, &ins);
        if (parents.size() <= d) {
            parents.resize(d + 1);
            parentAt.resize(d + 1);
        }
        parents[d] = h;
        parentAt[d] = ins.hParent;
    }
    for (HTREEITEM h = TreeView_GetRoot(m_outlineTree); h;
         h = TreeView_GetNextSibling(m_outlineTree, h))
        TreeView_Expand(m_outlineTree, h, TVE_EXPAND);
    SendMessageW(m_outlineTree, WM_SETREDRAW, TRUE, 0);
    m_populatingOutline = false;
    InvalidateRect(m_outlineTree, nullptr, TRUE);
}

void MainWindow::LayoutFindBar() {
    if (!m_findBar || !m_findTarget)
        return;
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    const int barW = MulDiv(340, m_dpi, 96);
    const int barH = MulDiv(34, m_dpi, 96);
    const int pad = MulDiv(6, m_dpi, 96);
    const int paneRight = m_findTarget == m_left.get() ? m_splitterX : rc.right;
    const int x = std::max(0, paneRight - barW - pad);
    SetWindowPos(m_findBar, HWND_TOP, x, pad, barW, barH, SWP_NOACTIVATE);

    const int gap = MulDiv(4, m_dpi, 96);
    const int btn = MulDiv(26, m_dpi, 96);
    const int countW = MulDiv(64, m_dpi, 96);
    const int innerH = barH - 2 * gap;
    int right = barW - gap;
    MoveWindow(m_findClose, right - btn, gap, btn, innerH, TRUE);
    right -= btn + gap;
    MoveWindow(m_findNext, right - btn, gap, btn, innerH, TRUE);
    right -= btn + gap;
    MoveWindow(m_findPrev, right - btn, gap, btn, innerH, TRUE);
    right -= btn + gap;
    MoveWindow(m_findCount, right - countW, gap + MulDiv(4, m_dpi, 96), countW,
               innerH - MulDiv(4, m_dpi, 96), TRUE);
    right -= countW + gap;
    MoveWindow(m_findEdit, gap, gap, std::max(40, right - gap), innerH, TRUE);
}

void MainWindow::ShowFindBar() {
    if (!m_findBar)
        return;
    // Retarget only when a pane actually holds focus: a repeated Ctrl+F from
    // inside the find box must reselect the query, not silently move the
    // search (and wipe its highlights) to the default left pane.
    const HWND focus = GetFocus();
    PaneWindow* target = m_findTarget ? m_findTarget : m_left.get();
    if (focus == m_left->Hwnd())
        target = m_left.get();
    else if (focus == m_right->Hwnd())
        target = m_right.get();
    if (m_findTarget && m_findTarget != target)
        m_findTarget->ClearSearch(); // highlights move with the bar
    m_findTarget = target;
    LayoutFindBar();
    ShowWindow(m_findBar, SW_SHOW);
    SetWindowPos(m_findBar, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SendMessageW(m_findEdit, EM_SETSEL, 0, -1);
    SetFocus(m_findEdit);
    wchar_t text[256];
    GetWindowTextW(m_findEdit, text, ARRAYSIZE(text));
    if (text[0] != L'\0')
        m_findTarget->StartSearch(text); // retarget with the previous needle
}

void MainWindow::CloseFindBar() {
    if (m_findBar)
        ShowWindow(m_findBar, SW_HIDE);
    KillTimer(m_hwnd, kFindDebounceTimer);
    if (m_findTarget) {
        m_findTarget->ClearSearch();
        SetFocus(m_findTarget->Hwnd());
    }
}

RECT MainWindow::SplitterRect() const {
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    const int splitterW = MulDiv(kSplitterDip, m_dpi, 96);
    return {m_splitterX, 0, m_splitterX + splitterW, rc.bottom};
}

void MainWindow::SetSplitRatioFromX(int x) {
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    const int sidebarW = m_outlineVisible ? MulDiv(260, m_dpi, 96) : 0;
    const int splitterW = MulDiv(kSplitterDip, m_dpi, 96);
    const int usable = rc.right - sidebarW - splitterW;
    if (usable <= 0)
        return;
    const float ratio =
        static_cast<float>(x - sidebarW - splitterW / 2) / static_cast<float>(usable);
    m_splitRatio = std::clamp(ratio, 0.1f, 0.9f);
}

void MainWindow::OpenDocumentDialog(bool rightPane) {
    wchar_t buffer[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = m_hwnd;
    ofn.lpstrFilter = L"PDF documents (*.pdf)\0*.pdf\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = buffer;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = rightPane ? L"Open document in right pane" : L"Open document in left pane";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (GetOpenFileNameW(&ofn))
        (rightPane ? m_right : m_left)->OpenDocument(buffer);
}

bool MainWindow::IsSystemDark() {
    DWORD value = 1;
    DWORD size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER,
                     L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value,
                     &size) != ERROR_SUCCESS)
        return false;
    return value == 0;
}

void MainWindow::UpdateTheme() {
    m_dark = IsSystemDark();
    const BOOL dark = m_dark ? TRUE : FALSE;
    DwmSetWindowAttribute(m_hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    if (m_bgBrush)
        DeleteObject(m_bgBrush);
    m_bgBrush = CreateSolidBrush(m_dark ? RGB(43, 43, 43) : RGB(229, 229, 229));
    if (m_left) {
        m_left->SetDarkMode(m_dark);
        m_right->SetDarkMode(m_dark);
    }
    InvalidateRect(m_hwnd, nullptr, TRUE);
}
