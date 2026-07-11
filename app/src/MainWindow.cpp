#include "MainWindow.h"

#include "resource.h"
#include "util/GlyphIcons.h"
#include "util/Settings.h"
#include "util/Strings.h"

#include <commctrl.h> // SetWindowSubclass for the find-box keyboard handling
#include <commdlg.h>  // excluded from windows.h by WIN32_LEAN_AND_MEAN
#include <shellapi.h> // ShellExecuteW for the synctex inverse-search URI
#include <shlwapi.h>  // PathCompactPathExW for the MRU menu labels

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

namespace {

constexpr int kSplitterDip = 6;
constexpr int kMinPaneDip = 120;
constexpr int kInitialWidthDip = 1100;
constexpr int kInitialHeightDip = 760;
constexpr UINT_PTR kFindDebounceTimer = 2;
constexpr UINT_PTR kStatusMsgTimer = 3; // transient status-bar message expiry
constexpr PCWSTR kFindBarClass = L"PsvFindBar";

// Command lines may carry relative paths; MRU entries must survive a cwd
// change, so they are absolutized at record time.
std::wstring NormalizePath(const std::wstring& path) {
    wchar_t buffer[1024];
    const DWORD n = GetFullPathNameW(path.c_str(), ARRAYSIZE(buffer), buffer, nullptr);
    return (n == 0 || n >= ARRAYSIZE(buffer)) ? path : std::wstring(buffer, n);
}

std::wstring CompactPath(const std::wstring& path, UINT maxChars) {
    wchar_t buffer[MAX_PATH];
    if (maxChars >= ARRAYSIZE(buffer) || !PathCompactPathExW(buffer, path.c_str(), maxChars, 0))
        return path;
    return buffer;
}

// '&' in a file name would become a menu mnemonic.
std::wstring EscapeMenuText(std::wstring s) {
    size_t pos = 0;
    while ((pos = s.find(L'&', pos)) != std::wstring::npos) {
        s.insert(pos, 1, L'&');
        pos += 2;
    }
    return s;
}

std::wstring MruMenuLabel(size_t index, const std::wstring& text) {
    std::wstring label = L"&";
    label += static_cast<wchar_t>(L'1' + index); // entries cap at 9: always one digit
    label += L"  ";
    label += EscapeMenuText(text);
    return label;
}

void ReplaceAll(std::wstring& s, PCWSTR what, const std::wstring& with) {
    const size_t n = wcslen(what);
    size_t pos = 0;
    while ((pos = s.find(what, pos)) != std::wstring::npos) {
        s.replace(pos, n, with);
        pos += with.size();
    }
}

// Minimal percent-encoding for the vscode://file/ URI: keep the characters a
// path legitimately contains, escape everything else as UTF-8 bytes.
std::wstring PercentEncode(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size());
    for (const wchar_t c : s) {
        const bool safe = (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') ||
                          (c >= L'0' && c <= L'9') || c == L'-' || c == L'.' || c == L'_' ||
                          c == L'~' || c == L'/' || c == L':';
        if (safe) {
            out += c;
            continue;
        }
        char utf8[8];
        const int len = WideCharToMultiByte(CP_UTF8, 0, &c, 1, utf8, sizeof(utf8), nullptr,
                                            nullptr);
        for (int i = 0; i < len; ++i) {
            wchar_t hex[4];
            swprintf_s(hex, L"%%%02X", static_cast<unsigned char>(utf8[i]));
            out += hex;
        }
    }
    return out;
}

// Segoe MDL2 Assets codepoints; the order is the imagelist order referenced
// by the TBBUTTON iBitmap indices in CreateToolbar.
constexpr wchar_t kToolbarGlyphs[] = {
    0xE8A0, // 0 open left (arrow into the left pane)
    0xE89F, // 1 open right
    0xE71B, // 2 scroll sync (link)
    0xE895, // 3 zoom sync (circular arrows)
    0xE8AB, // 4 fit width (horizontal arrows)
    0xE9A6, // 5 fit page
    0xE721, // 6 find
    0xE8FD, // 7 outline (bulleted list)
    0xE740, // 8 full screen
    0xEC8F, // 9 continuous scrolling (ScrollUpDown)
    0xE7C3, // 10 page-by-page (Page)
};

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
    wc.hIcon = static_cast<HICON>(LoadImageW(hinst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                             GetSystemMetrics(SM_CXICON),
                                             GetSystemMetrics(SM_CYICON), LR_SHARED));
    wc.hIconSm = static_cast<HICON>(LoadImageW(hinst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                               GetSystemMetrics(SM_CXSMICON),
                                               GetSystemMetrics(SM_CYSMICON), LR_SHARED));
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
    if (m_toolbarIcons)
        ImageList_Destroy(m_toolbarIcons);
}

bool MainWindow::Create(HINSTANCE hinst, int nCmdShow, std::wstring leftFile,
                        std::wstring rightFile,
                        std::optional<ForwardSearchRequest> forward) {
    m_dx.EnsureCreated(); // fail fast in wWinMain if graphics init is impossible

    // Loaded before any UI is built: the language drives the menu and the
    // pane placeholders, and the bar visibility must be in place for the
    // WM_CREATE Layout. A command line only overrides the documents; the UI
    // preferences always apply.
    const AppSettings session = AppSettings::Load();
    SetUiLanguage(session.language == L"it" ? Lang::Italian : Lang::English);
    m_toolbarVisible = session.toolbar;
    m_statusVisible = session.statusbar;
    m_outlineVisible = session.outline;
    m_synctexInverse = session.synctexInverse;
    m_mruFiles = session.mruFiles;
    m_mruPairs = session.mruPairs;

    if (forward) {
        // Cold-start forward search: prefer the saved session when it already
        // contains the pdf (the sibling document comes back too); otherwise
        // open just that pdf. The query itself replays on DocumentOpened.
        m_parkedForward = std::move(*forward);
        const bool inSession =
            lstrcmpiW(session.left.path.c_str(), m_parkedForward->pdf.c_str()) == 0 ||
            lstrcmpiW(session.right.path.c_str(), m_parkedForward->pdf.c_str()) == 0;
        if (!inSession && leftFile.empty() && rightFile.empty())
            leftFile = m_parkedForward->pdf;
    }

    m_left = std::make_unique<PaneWindow>(m_dx, Str(StrId::PlaceholderLeft));
    m_right = std::make_unique<PaneWindow>(m_dx, Str(StrId::PlaceholderRight));
    m_sync = std::make_unique<SyncController>(*m_left, *m_right);

    // Attached before creation so the very first GetClientRect already
    // excludes the menu band.
    m_menu = BuildMenuBar();
    CreateWindowExW(0, kClassName, L"PdfSideViewer", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                    CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, nullptr, m_menu,
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
        } else {
            m_sync->OnViewChanged(p, e, r);
            if (e == PaneWindow::ViewEvent::DocumentOpened && m_outlineVisible &&
                m_outlinePane == &p)
                UpdateOutlineSidebar(&p);
            // Every successful open funnels through here (dialog, MRU, drag &
            // drop, command line, session restore), so this is the one MRU
            // recording point. Error opens keep HasDocument() false.
            if (e == PaneWindow::ViewEvent::DocumentOpened && p.HasDocument()) {
                RecordMruFile(p.DocumentPath());
                if (m_left->HasDocument() && m_right->HasDocument())
                    RecordMruPair(m_left->DocumentPath(), m_right->DocumentPath());
                RebuildMruMenus();
                // Parked forward search: covers cold start, on-demand opens
                // and requests that landed while an auto-reload was underway.
                if (m_parkedForward &&
                    lstrcmpiW(p.DocumentPath().c_str(), m_parkedForward->pdf.c_str()) == 0) {
                    const ForwardSearchRequest req = *m_parkedForward;
                    m_parkedForward.reset();
                    if (!p.ForwardSearchTo(req.tex, req.line))
                        ShowStatusMessage(StrId::SyncTexForwardMiss);
                }
            }
        }
        UpdateStatusBar();
        // Scroll ticks are too frequent for menu/toolbar churn; the checked
        // state only depends on zoom mode and focus anyway.
        if (e != PaneWindow::ViewEvent::Scrolled)
            UpdateCommandUi();
        else if (GetKeyState(VK_MENU) < 0)
            m_altScrollGesture = true; // Alt is a scroll modifier here, not a menu key
    };
    m_left->SetViewChangedHandler(onViewChanged);
    m_right->SetViewChangedHandler(onViewChanged);
    m_left->SetOpenSiblingHandler([this](std::wstring p) { m_right->OpenDocument(std::move(p)); });
    m_right->SetOpenSiblingHandler([this](std::wstring p) { m_left->OpenDocument(std::move(p)); });
    m_left->SetOpenRequestHandler([this] { OpenDocumentDialog(false); });
    m_right->SetOpenRequestHandler([this] { OpenDocumentDialog(true); });

    // Applied before any document opens (panes are still closed: only the
    // flag lands); the restore path then adopts the saved page under it.
    m_scrollMode = session.scrollMode != 0 ? PaneWindow::ScrollMode::Paged
                                           : PaneWindow::ScrollMode::Continuous;
    m_left->SetScrollMode(m_scrollMode);
    m_right->SetScrollMode(m_scrollMode);
    const auto toggleScrollMode = [this] {
        ApplyScrollMode(m_scrollMode == PaneWindow::ScrollMode::Paged
                            ? PaneWindow::ScrollMode::Continuous
                            : PaneWindow::ScrollMode::Paged);
    };
    m_left->SetToggleScrollModeHandler(toggleScrollMode);
    m_right->SetToggleScrollModeHandler(toggleScrollMode);

    const auto onInverseSearch = [this](PaneWindow&, const SyncTexIndex::InverseHit* hit,
                                        bool hadData) {
        if (hit)
            LaunchInverseSearch(*hit);
        else
            ShowStatusMessage(hadData ? StrId::SyncTexNoMatch : StrId::SyncTexNoData);
    };
    m_left->SetInverseSearchHandler(onInverseSearch);
    m_right->SetInverseSearchHandler(onInverseSearch);

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

    // The pane sessions seed the SaveSession fallbacks even when the command
    // line wins, so an unopened pane never wipes saved state.
    const float dpiRatio = static_cast<float>(m_dpi) / static_cast<float>(session.dpi);
    m_fallbackLeft = session.left;
    m_fallbackLeft.scrollX *= dpiRatio;
    m_fallbackLeft.scrollY *= dpiRatio;
    m_fallbackRight = session.right;
    m_fallbackRight.scrollX *= dpiRatio;
    m_fallbackRight.scrollY *= dpiRatio;

    if (m_outlineVisible)
        UpdateOutlineSidebar(m_left.get()); // adopt a target; filled on DocumentOpened

    if (!leftFile.empty() || !rightFile.empty()) {
        // Explicit command line wins over the saved session for the DOCUMENTS
        // only; the sync preference applies to every launch path (same order
        // as ApplySession: anchors recapture when the opens complete).
        m_sync->SetZoomSync(session.zoomSync);
        if (!leftFile.empty())
            m_left->OpenDocument(std::move(leftFile));
        if (!rightFile.empty())
            m_right->OpenDocument(std::move(rightFile));
        m_sync->SetScrollSync(session.scrollSync);
    } else {
        ApplySession(session);
    }
    UpdateTitle();
    UpdateCommandUi();

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
    // While full screen the live placement is the monitor rect written by the
    // enter transition; the captured pre-fullscreen placement is the session.
    WINDOWPLACEMENT wp = m_fsRestorePlacement;
    wp.length = sizeof(wp);
    if (m_fullscreen || GetWindowPlacement(m_hwnd, &wp)) {
        s.hasPlacement = true;
        s.normalRect = wp.rcNormalPosition;
        s.maximized = wp.showCmd == SW_SHOWMAXIMIZED;
    }
    s.splitRatio = m_splitRatio;
    s.scrollSync = m_sync->ScrollSync();
    s.zoomSync = m_sync->ZoomSync();
    s.scrollMode = m_scrollMode == PaneWindow::ScrollMode::Paged ? 1 : 0;
    s.dpi = m_dpi;
    s.toolbar = m_toolbarVisible;
    s.statusbar = m_statusVisible;
    s.outline = m_outlineVisible;
    s.language = UiLanguage() == Lang::Italian ? L"it" : L"en";
    s.synctexInverse = m_synctexInverse;
    s.mruFiles = m_mruFiles;
    s.mruPairs = m_mruPairs;
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

PaneWindow* MainWindow::FocusedPane() const {
    return GetFocus() == m_right->Hwnd() ? m_right.get() : m_left.get();
}

HMENU MainWindow::BuildMenuBar() {
    const auto append = [](HMENU menu, UINT_PTR id, StrId text) {
        AppendMenuW(menu, MF_STRING, id, Str(text));
    };
    m_mruFilesMenu = CreatePopupMenu();
    m_mruPairsMenu = CreatePopupMenu();
    HMENU file = CreatePopupMenu();
    append(file, IDC_OPEN_LEFT, StrId::MenuOpenLeft);
    append(file, IDC_OPEN_RIGHT, StrId::MenuOpenRight);
    append(file, IDC_CLOSE_DOC, StrId::MenuCloseDoc);
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_POPUP, reinterpret_cast<UINT_PTR>(m_mruFilesMenu),
                Str(StrId::MenuRecentFiles));
    AppendMenuW(file, MF_POPUP, reinterpret_cast<UINT_PTR>(m_mruPairsMenu),
                Str(StrId::MenuRecentPairs));
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    append(file, IDC_EXIT, StrId::MenuExit);

    HMENU lang = CreatePopupMenu();
    append(lang, IDC_LANG_ENGLISH, StrId::MenuLangEnglish);
    append(lang, IDC_LANG_ITALIAN, StrId::MenuLangItalian);

    HMENU view = CreatePopupMenu();
    append(view, IDC_TOGGLE_TOOLBAR, StrId::MenuToolbar);
    append(view, IDC_TOGGLE_STATUSBAR, StrId::MenuStatusBar);
    append(view, IDC_TOGGLE_OUTLINE, StrId::MenuOutline);
    AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
    append(view, IDC_ZOOM_IN, StrId::MenuZoomIn);
    append(view, IDC_ZOOM_OUT, StrId::MenuZoomOut);
    append(view, IDC_ZOOM_ACTUAL, StrId::MenuActualSize);
    append(view, IDC_FIT_WIDTH, StrId::MenuFitWidth);
    append(view, IDC_FIT_PAGE, StrId::MenuFitPage);
    AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
    append(view, IDC_SCROLL_CONTINUOUS, StrId::MenuScrollContinuous);
    append(view, IDC_SCROLL_PAGED, StrId::MenuScrollPaged);
    AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(view, MF_POPUP, reinterpret_cast<UINT_PTR>(lang), Str(StrId::MenuLanguage));
    AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
    append(view, IDC_FULLSCREEN, StrId::MenuFullScreen);

    HMENU sync = CreatePopupMenu();
    append(sync, IDC_TOGGLE_SCROLL_SYNC, StrId::MenuScrollSync);
    append(sync, IDC_TOGGLE_ZOOM_SYNC, StrId::MenuZoomSync);

    HMENU help = CreatePopupMenu();
    append(help, IDC_ABOUT, StrId::MenuAbout);

    HMENU bar = CreateMenu();
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(file), Str(StrId::MenuFile));
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(view), Str(StrId::MenuView));
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(sync), Str(StrId::MenuSync));
    AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(help), Str(StrId::MenuHelp));
    RebuildMruMenus();
    return bar;
}

void MainWindow::RebuildMruMenus() {
    const auto reset = [](HMENU menu) {
        if (!menu)
            return false;
        while (GetMenuItemCount(menu) > 0)
            DeleteMenu(menu, 0, MF_BYPOSITION);
        return true;
    };
    if (reset(m_mruFilesMenu)) {
        for (size_t i = 0; i < m_mruFiles.size(); ++i)
            AppendMenuW(m_mruFilesMenu, MF_STRING,
                        static_cast<UINT_PTR>(IDC_MRU_FILE_FIRST) + i,
                        MruMenuLabel(i, CompactPath(m_mruFiles[i], 64)).c_str());
        if (m_mruFiles.empty())
            AppendMenuW(m_mruFilesMenu, MF_STRING | MF_GRAYED, 0, Str(StrId::MenuMruEmpty));
    }
    if (reset(m_mruPairsMenu)) {
        for (size_t i = 0; i < m_mruPairs.size(); ++i) {
            const std::wstring text =
                CompactPath(m_mruPairs[i].left, 36) + L"  +  " +
                CompactPath(m_mruPairs[i].right, 36);
            AppendMenuW(m_mruPairsMenu, MF_STRING,
                        static_cast<UINT_PTR>(IDC_MRU_PAIR_FIRST) + i,
                        MruMenuLabel(i, text).c_str());
        }
        if (m_mruPairs.empty())
            AppendMenuW(m_mruPairsMenu, MF_STRING | MF_GRAYED, 0, Str(StrId::MenuMruEmpty));
    }
}

void MainWindow::RecordMruFile(const std::wstring& path) {
    if (path.empty())
        return;
    const std::wstring normalized = NormalizePath(path);
    auto& v = m_mruFiles;
    v.erase(std::remove_if(v.begin(), v.end(),
                           [&](const std::wstring& p) {
                               return lstrcmpiW(p.c_str(), normalized.c_str()) == 0;
                           }),
            v.end());
    v.insert(v.begin(), normalized);
    if (v.size() > kMruMaxEntries)
        v.resize(kMruMaxEntries);
}

void MainWindow::RecordMruPair(const std::wstring& left, const std::wstring& right) {
    if (left.empty() || right.empty())
        return;
    const std::wstring l = NormalizePath(left);
    const std::wstring r = NormalizePath(right);
    auto& v = m_mruPairs;
    v.erase(std::remove_if(v.begin(), v.end(),
                           [&](const MruPair& p) {
                               return lstrcmpiW(p.left.c_str(), l.c_str()) == 0 &&
                                      lstrcmpiW(p.right.c_str(), r.c_str()) == 0;
                           }),
            v.end());
    v.insert(v.begin(), {l, r});
    if (v.size() > kMruMaxEntries)
        v.resize(kMruMaxEntries);
}

void MainWindow::OpenMruFile(size_t index) {
    if (index >= m_mruFiles.size())
        return;
    const std::wstring path = m_mruFiles[index]; // copy: the erase below invalidates
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        m_mruFiles.erase(m_mruFiles.begin() + static_cast<ptrdiff_t>(index));
        RebuildMruMenus();
        const std::wstring msg = Str(StrId::MruMissingFile) + path;
        MessageBoxW(m_hwnd, msg.c_str(), L"PdfSideViewer", MB_OK | MB_ICONWARNING);
        return;
    }
    FocusedPane()->OpenDocument(path);
}

void MainWindow::OpenMruPair(size_t index) {
    if (index >= m_mruPairs.size())
        return;
    const MruPair pair = m_mruPairs[index]; // copy: the erase below invalidates
    const bool leftMissing = GetFileAttributesW(pair.left.c_str()) == INVALID_FILE_ATTRIBUTES;
    const bool rightMissing = GetFileAttributesW(pair.right.c_str()) == INVALID_FILE_ATTRIBUTES;
    if (leftMissing || rightMissing) {
        m_mruPairs.erase(m_mruPairs.begin() + static_cast<ptrdiff_t>(index));
        RebuildMruMenus();
        const std::wstring msg =
            Str(StrId::MruMissingFile) + (leftMissing ? pair.left : pair.right);
        MessageBoxW(m_hwnd, msg.c_str(), L"PdfSideViewer", MB_OK | MB_ICONWARNING);
        return;
    }
    m_left->OpenDocument(pair.left);
    m_right->OpenDocument(pair.right);
}

void MainWindow::CreateToolbar(HINSTANCE hinst) {
    // No TBSTYLE_FLAT: flat toolbars are transparent and delegate their
    // background to the parent, which paints nothing under children
    // (WS_CLIPCHILDREN + WM_ERASEBKGND returning 1), leaving a black band.
    // The comctl v6 theme already renders the non-flat style modern.
    m_toolbar = CreateWindowExW(0, TOOLBARCLASSNAMEW, nullptr,
                                WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | TBSTYLE_TOOLTIPS |
                                    CCS_TOP | CCS_NODIVIDER,
                                0, 0, 0, 0, m_hwnd,
                                reinterpret_cast<HMENU>(static_cast<UINT_PTR>(102)), hinst,
                                nullptr);
    if (!m_toolbar)
        return;
    SendMessageW(m_toolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    RebuildToolbarIcons();

    const auto button = [](int image, WORD id, BYTE style) {
        TBBUTTON b{};
        b.iBitmap = image;
        b.idCommand = id;
        b.fsState = TBSTATE_ENABLED;
        b.fsStyle = style;
        return b;
    };
    const auto separator = [] {
        TBBUTTON b{};
        b.fsStyle = BTNS_SEP;
        return b;
    };
    const TBBUTTON buttons[] = {
        button(0, IDC_OPEN_LEFT, BTNS_BUTTON),
        button(1, IDC_OPEN_RIGHT, BTNS_BUTTON),
        separator(),
        button(2, IDC_TOGGLE_SCROLL_SYNC, BTNS_CHECK),
        button(3, IDC_TOGGLE_ZOOM_SYNC, BTNS_CHECK),
        separator(),
        // A manual check pair, not BTNS_CHECKGROUP: Manual zoom mode means
        // NEITHER fit button is pressed, which a radio group cannot show.
        button(4, IDC_FIT_WIDTH, BTNS_CHECK),
        button(5, IDC_FIT_PAGE, BTNS_CHECK),
        separator(),
        // Scroll-mode pair: exactly one is always pressed (UpdateCommandUi).
        button(9, IDC_SCROLL_CONTINUOUS, BTNS_CHECK),
        button(10, IDC_SCROLL_PAGED, BTNS_CHECK),
        separator(),
        button(6, IDC_FIND_SHOW, BTNS_BUTTON),
        button(7, IDC_TOGGLE_OUTLINE, BTNS_CHECK),
        separator(),
        button(8, IDC_FULLSCREEN, BTNS_BUTTON),
    };
    SendMessageW(m_toolbar, TB_ADDBUTTONSW, std::size(buttons),
                 reinterpret_cast<LPARAM>(buttons));
}

void MainWindow::RebuildToolbarIcons() {
    if (!m_toolbar)
        return;
    const int glyphPx = MulDiv(16, m_dpi, 96);
    HIMAGELIST icons =
        CreateGlyphImageList(std::span<const wchar_t>(kToolbarGlyphs), glyphPx, glyphPx,
                             GetSysColor(COLOR_BTNTEXT));
    if (!icons)
        return;
    SendMessageW(m_toolbar, TB_SETIMAGELIST, 0, reinterpret_cast<LPARAM>(icons));
    if (m_toolbarIcons)
        ImageList_Destroy(m_toolbarIcons);
    m_toolbarIcons = icons;
    const int btn = MulDiv(24, m_dpi, 96);
    SendMessageW(m_toolbar, TB_SETBUTTONSIZE, 0, MAKELPARAM(btn, btn));
    SendMessageW(m_toolbar, TB_AUTOSIZE, 0, 0);
}

void MainWindow::UpdateCommandUi() {
    if (!m_sync)
        return;
    if (m_menu) {
        const auto check = [this](UINT id, bool on) {
            CheckMenuItem(m_menu, id, MF_BYCOMMAND | (on ? MF_CHECKED : MF_UNCHECKED));
        };
        check(IDC_TOGGLE_TOOLBAR, m_toolbarVisible);
        check(IDC_TOGGLE_STATUSBAR, m_statusVisible);
        check(IDC_TOGGLE_OUTLINE, m_outlineVisible);
        check(IDC_FULLSCREEN, m_fullscreen);
        check(IDC_TOGGLE_SCROLL_SYNC, m_sync->ScrollSync());
        check(IDC_TOGGLE_ZOOM_SYNC, m_sync->ZoomSync());
        UINT fitId = IDC_ZOOM_ACTUAL;
        switch (FocusedPane()->GetZoomMode()) {
        case PaneWindow::ZoomMode::FitWidth:
            fitId = IDC_FIT_WIDTH;
            break;
        case PaneWindow::ZoomMode::FitPage:
            fitId = IDC_FIT_PAGE;
            break;
        default:
            break;
        }
        CheckMenuRadioItem(m_menu, IDC_ZOOM_ACTUAL, IDC_FIT_PAGE, fitId, MF_BYCOMMAND);
        CheckMenuRadioItem(m_menu, IDC_SCROLL_CONTINUOUS, IDC_SCROLL_PAGED,
                           m_scrollMode == PaneWindow::ScrollMode::Paged
                               ? IDC_SCROLL_PAGED
                               : IDC_SCROLL_CONTINUOUS,
                           MF_BYCOMMAND);
        CheckMenuRadioItem(m_menu, IDC_LANG_ENGLISH, IDC_LANG_ITALIAN,
                           UiLanguage() == Lang::Italian ? IDC_LANG_ITALIAN : IDC_LANG_ENGLISH,
                           MF_BYCOMMAND);
    }
    if (m_toolbar) {
        const auto press = [this](WORD id, bool on) {
            SendMessageW(m_toolbar, TB_CHECKBUTTON, id, MAKELPARAM(on ? TRUE : FALSE, 0));
        };
        const PaneWindow::ZoomMode mode = FocusedPane()->GetZoomMode();
        press(IDC_TOGGLE_SCROLL_SYNC, m_sync->ScrollSync());
        press(IDC_TOGGLE_ZOOM_SYNC, m_sync->ZoomSync());
        press(IDC_TOGGLE_OUTLINE, m_outlineVisible);
        press(IDC_FIT_WIDTH, mode == PaneWindow::ZoomMode::FitWidth);
        press(IDC_FIT_PAGE, mode == PaneWindow::ZoomMode::FitPage);
        press(IDC_SCROLL_CONTINUOUS, m_scrollMode == PaneWindow::ScrollMode::Continuous);
        press(IDC_SCROLL_PAGED, m_scrollMode == PaneWindow::ScrollMode::Paged);
    }
}

void MainWindow::UpdateStatusBar() {
    if (!m_status || !m_sync)
        return;
    const auto pageText = [](PaneWindow& pane, StrId prefix, StrId noDoc) {
        if (!pane.HasDocument())
            return std::wstring(Str(noDoc));
        const int count = pane.PageCount();
        const int page = std::clamp(static_cast<int>(pane.SyncPosition()), 0, count - 1) + 1;
        return Str(prefix) + std::to_wstring(page) + L" / " + std::to_wstring(count);
    };
    const auto zoomText = [](PaneWindow& pane) {
        if (!pane.HasDocument())
            return std::wstring();
        return std::to_wstring(static_cast<int>(pane.Zoom() * 100.0f + 0.5f)) + L"%";
    };
    StrId sync = StrId::StatusSyncOff;
    if (m_sync->ScrollSync() && m_sync->ZoomSync())
        sync = StrId::StatusSyncBoth;
    else if (m_sync->ScrollSync())
        sync = StrId::StatusSyncScroll;
    else if (m_sync->ZoomSync())
        sync = StrId::StatusSyncZoom;

    const std::wstring texts[5] = {
        pageText(*m_left, StrId::StatusLeftPrefix, StrId::StatusLeftNoDoc),
        zoomText(*m_left),
        pageText(*m_right, StrId::StatusRightPrefix, StrId::StatusRightNoDoc),
        zoomText(*m_right),
        Str(sync),
    };
    for (int i = 0; i < 5; ++i) {
        if (m_statusText[i] == texts[i])
            continue; // SB_SETTEXT repaints the part even when nothing changed
        m_statusText[i] = texts[i];
        SendMessageW(m_status, SB_SETTEXTW, static_cast<WPARAM>(i),
                     reinterpret_cast<LPARAM>(m_statusText[i].c_str()));
    }
}

void MainWindow::ShowAboutBox() {
    std::wstring text = L"PdfSideViewer " PSV_VERSION_WSTR L"\n\n";
    text += Str(StrId::AboutBody);
    MessageBoxW(m_hwnd, text.c_str(), Str(StrId::AboutTitle), MB_OK | MB_ICONINFORMATION);
}

void MainWindow::ApplyScrollMode(PaneWindow::ScrollMode mode) {
    // Global by design: one mental switch, and sync-locked panes flip pages
    // together instead of mixing paged and continuous behavior.
    m_scrollMode = mode;
    m_left->SetScrollMode(mode);
    m_right->SetScrollMode(mode);
    UpdateCommandUi();
}

void MainWindow::SwitchLanguage(Lang lang) {
    if (lang == UiLanguage())
        return;
    SetUiLanguage(lang);
    HMENU old = m_menu;
    m_menu = BuildMenuBar();
    if (!m_fullscreen) { // while full screen the new handle just stays parked
        SetMenu(m_hwnd, m_menu);
        DrawMenuBar(m_hwnd);
    }
    if (old)
        DestroyMenu(old);
    UpdateTitle();
    m_left->SetPlaceholderHint(Str(StrId::PlaceholderLeft));
    m_right->SetPlaceholderHint(Str(StrId::PlaceholderRight));
    for (std::wstring& cached : m_statusText)
        cached.assign(1, L'\xFFFF'); // impossible text: force every part to rewrite
    UpdateStatusBar();
    UpdateCommandUi();
}

void MainWindow::ShowStatusMessage(StrId id) {
    if (!m_status)
        return;
    // Borrow the sync part for a transient message; the timer restores it.
    m_statusText[4] = Str(id);
    SendMessageW(m_status, SB_SETTEXTW, 4, reinterpret_cast<LPARAM>(m_statusText[4].c_str()));
    SetTimer(m_hwnd, kStatusMsgTimer, 4000, nullptr);
}

void MainWindow::LaunchInverseSearch(const SyncTexIndex::InverseHit& hit) {
    std::wstring cmd = m_synctexInverse.empty() ? L"vscode://file/%f:%l" : m_synctexInverse;
    const bool uri = cmd.find(L"://") != std::wstring::npos;
    std::wstring file = hit.texPath;
    if (uri) {
        std::replace(file.begin(), file.end(), L'\\', L'/');
        file = PercentEncode(file);
    }
    ReplaceAll(cmd, L"%f", file);
    ReplaceAll(cmd, L"%l", std::to_wstring(hit.line));

    if (uri) {
        const auto result = reinterpret_cast<INT_PTR>(
            ShellExecuteW(m_hwnd, L"open", cmd.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
        if (result <= 32) // per ShellExecute contract
            ShowStatusMessage(StrId::SyncTexEditorError);
        return;
    }
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    // CreateProcessW may rewrite the command-line buffer: pass writable data.
    if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                       nullptr, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    } else {
        ShowStatusMessage(StrId::SyncTexEditorError);
    }
}

void MainWindow::RouteForwardSearch(ForwardSearchRequest req) {
    req.pdf = NormalizePath(req.pdf);
    // Attention without stealing: the short-lived sender granted
    // AllowSetForegroundWindow, so this usually succeeds; the green flash is
    // the primary cue either way.
    if (!SetForegroundWindow(m_hwnd)) {
        FLASHWINFO fi{sizeof(fi), m_hwnd, FLASHW_TRAY | FLASHW_TIMERNOFG, 3, 0};
        FlashWindowEx(&fi);
    }
    const auto matches = [&](PaneWindow& pane) {
        return pane.HasPersistableDocument() &&
               lstrcmpiW(pane.DocumentPath().c_str(), req.pdf.c_str()) == 0;
    };
    const bool leftMatch = matches(*m_left);
    const bool rightMatch = matches(*m_right);
    PaneWindow* target = nullptr;
    if (leftMatch && rightMatch)
        target = FocusedPane(); // same pdf in both panes: follow user attention
    else if (leftMatch)
        target = m_left.get();
    else if (rightMatch)
        target = m_right.get();

    if (!target) {
        target = FocusedPane();
        target->OpenDocument(req.pdf);
        m_parkedForward = std::move(req); // replayed on DocumentOpened
        return;
    }
    if (!target->HasDocument()) {
        m_parkedForward = std::move(req); // pane still opening (e.g. mid-reload)
        return;
    }
    if (!target->ForwardSearchTo(req.tex, req.line))
        ShowStatusMessage(StrId::SyncTexForwardMiss);
}

void MainWindow::ToggleFullScreen() {
    if (!m_fullscreen) {
        m_fsRestorePlacement.length = sizeof(m_fsRestorePlacement);
        if (!GetWindowPlacement(m_hwnd, &m_fsRestorePlacement))
            return;
        m_fsRestoreStyle = static_cast<LONG>(GetWindowLongW(m_hwnd, GWL_STYLE));
        SetMenu(m_hwnd, nullptr); // the handle stays parked in m_menu
        SetWindowLongW(m_hwnd, GWL_STYLE, m_fsRestoreStyle & ~WS_OVERLAPPEDWINDOW);
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        GetMonitorInfoW(MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST), &mi);
        // Flag first: SetWindowPos delivers a synchronous WM_SIZE whose
        // Layout must already hide the bars.
        m_fullscreen = true;
        SetWindowPos(m_hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
    } else {
        m_fullscreen = false;
        SetWindowLongW(m_hwnd, GWL_STYLE, m_fsRestoreStyle);
        SetMenu(m_hwnd, m_menu);
        SetWindowPlacement(m_hwnd, &m_fsRestorePlacement); // normal AND maximized state
        SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER |
                         SWP_FRAMECHANGED);
    }
    UpdateCommandUi();
}

void MainWindow::UpdateTitle() {
    std::wstring title = L"PdfSideViewer";
    if (m_sync->ScrollSync())
        title += Str(StrId::TitleScrollSyncTag);
    if (m_sync->ZoomSync())
        title += Str(StrId::TitleZoomSyncTag);
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
        const HINSTANCE hinst =
            reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(m_hwnd, GWLP_HINSTANCE));
        m_left->Create(m_hwnd, 100);
        m_right->Create(m_hwnd, 101);
        CreateToolbar(hinst);
        CreateFindBar(); // after the panes: overlays must be above them
        m_outlineTree = CreateWindowExW(
            0, WC_TREEVIEWW, nullptr,
            WS_CHILD | WS_CLIPSIBLINGS | WS_BORDER | TVS_HASBUTTONS | TVS_HASLINES |
                TVS_LINESATROOT | TVS_SHOWSELALWAYS,
            0, 0, 0, 0, m_hwnd, reinterpret_cast<HMENU>(IDC_OUTLINE_TREE), hinst, nullptr);
        m_status = CreateWindowExW(0, STATUSCLASSNAMEW, nullptr,
                                   WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | SBARS_SIZEGRIP, 0,
                                   0, 0, 0, m_hwnd,
                                   reinterpret_cast<HMENU>(static_cast<UINT_PTR>(103)), hinst,
                                   nullptr);
        UpdateUiFont(); // find bar + tree + status bar
        UpdateTheme();  // after the pane HWNDs exist
        Layout();
        return 0;
    }

    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED)
            Layout();
        return 0;

    case WM_GETMINMAXINFO: {
        if (m_fullscreen)
            break; // borderless monitor-sized window: no track clamps
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        // Two 120-DIP panes + splitter must stay usable under menu + toolbar
        // + status bar.
        mmi->ptMinTrackSize = {MulDiv(520, m_dpi, 96), MulDiv(380, m_dpi, 96)};
        return 0;
    }

    case WM_DPICHANGED: {
        m_dpi = HIWORD(wParam);
        UpdateUiFont();
        RebuildToolbarIcons();
        // Panes must know the new DPI before SetWindowPos: its synchronous
        // WM_SIZE cascade rebuilds and presents their targets immediately.
        m_left->OnDpiChanged(m_dpi);
        m_right->OnDpiChanged(m_dpi);
        RECT target = *reinterpret_cast<const RECT*>(lParam);
        if (m_fullscreen) {
            // The suggested rect is for the framed window; stay glued to the
            // monitor instead.
            MONITORINFO mi{};
            mi.cbSize = sizeof(mi);
            GetMonitorInfoW(MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST), &mi);
            target = mi.rcMonitor;
        }
        SetWindowPos(m_hwnd, nullptr, target.left, target.top, target.right - target.left,
                     target.bottom - target.top, SWP_NOZORDER | SWP_NOACTIVATE);
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
        if (wParam == kStatusMsgTimer) {
            KillTimer(m_hwnd, kStatusMsgTimer);
            m_statusText[4].assign(1, L'\xFFFF'); // force the part to rewrite
            UpdateStatusBar();
            return 0;
        }
        break;

    case WM_COPYDATA: {
        // SyncTeX forward search from a short-lived second instance. The
        // message crosses process boundaries: validate the payload exactly.
        const auto* cds = reinterpret_cast<const COPYDATASTRUCT*>(lParam);
        if (!cds || cds->dwData != kCdForwardSearch || !cds->lpData ||
            cds->cbData < sizeof(ForwardSearchBlob))
            return FALSE;
        ForwardSearchBlob blob;
        memcpy(&blob, cds->lpData, sizeof(blob));
        if (blob.line < 1 || blob.line > 10'000'000 || blob.texLen == 0 || blob.pdfLen == 0 ||
            blob.texLen > 0x8000 || blob.pdfLen > 0x8000)
            return FALSE;
        const size_t expected =
            sizeof(ForwardSearchBlob) +
            (static_cast<size_t>(blob.texLen) + blob.pdfLen) * sizeof(wchar_t);
        if (cds->cbData != expected)
            return FALSE;
        const auto* chars = reinterpret_cast<const wchar_t*>(
            static_cast<const BYTE*>(cds->lpData) + sizeof(ForwardSearchBlob));
        ForwardSearchRequest req;
        req.tex.assign(chars, blob.texLen); // copy NOW: the buffer dies at return
        req.pdf.assign(chars + blob.texLen, blob.pdfLen);
        req.line = static_cast<int>(blob.line);
        const bool pdfRooted =
            req.pdf.size() >= 3 && (req.pdf[1] == L':' || req.pdf.rfind(L"\\\\", 0) == 0);
        if (!pdfRooted)
            return FALSE;
        RouteForwardSearch(std::move(req));
        return TRUE;
    }

    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_FIND_EDIT && HIWORD(wParam) == EN_CHANGE) {
            SetTimer(m_hwnd, kFindDebounceTimer, 350, nullptr); // debounce live search
            return 0;
        }
        // MRU entries are ranges, not enum cases.
        if (LOWORD(wParam) >= IDC_MRU_FILE_FIRST &&
            LOWORD(wParam) < IDC_MRU_FILE_FIRST + kMruMaxEntries) {
            OpenMruFile(LOWORD(wParam) - IDC_MRU_FILE_FIRST);
            return 0;
        }
        if (LOWORD(wParam) >= IDC_MRU_PAIR_FIRST &&
            LOWORD(wParam) < IDC_MRU_PAIR_FIRST + kMruMaxEntries) {
            OpenMruPair(LOWORD(wParam) - IDC_MRU_PAIR_FIRST);
            return 0;
        }
        switch (LOWORD(wParam)) {
        case IDC_OPEN_LEFT:
            OpenDocumentDialog(false);
            return 0;
        case IDC_OPEN_RIGHT:
            OpenDocumentDialog(true);
            return 0;
        case IDC_CLOSE_DOC: {
            // Only when a pane really has focus: FocusedPane() falls back to
            // the LEFT pane for any other focus (find box, outline tree), and
            // a reflexive Ctrl+W typed in the find bar must not close - and
            // wipe the saved session of - a document nobody aimed at.
            const HWND focus = GetFocus();
            if (focus != m_left->Hwnd() && focus != m_right->Hwnd())
                return 0;
            PaneWindow* pane = FocusedPane();
            // A deliberate close must not resurrect on the next launch: the
            // SaveSession fallback protects panes that never finished
            // opening, not ones the user closed. A no-op close (already
            // empty pane) must NOT wipe it, though: it may still hold a
            // session document that never got to open.
            if (pane->HasPersistableDocument())
                (pane == m_right.get() ? m_fallbackRight : m_fallbackLeft) = PaneSettings{};
            pane->CloseDocument(); // DocumentOpened refreshes status/outline/UI
            return 0;
        }
        case IDC_FOCUS_NEXT_PANE:
            SetFocus(GetFocus() == m_left->Hwnd() ? m_right->Hwnd() : m_left->Hwnd());
            return 0;
        case IDC_TOGGLE_SCROLL_SYNC:
            m_sync->SetScrollSync(!m_sync->ScrollSync());
            UpdateTitle();
            UpdateCommandUi();
            UpdateStatusBar();
            return 0;
        case IDC_TOGGLE_ZOOM_SYNC:
            m_sync->SetZoomSync(!m_sync->ZoomSync());
            UpdateTitle();
            UpdateCommandUi();
            UpdateStatusBar();
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
                UpdateOutlineSidebar(FocusedPane());
            } else if (m_outlineTree && GetFocus() == m_outlineTree) {
                // Hiding a focused window does not move keyboard focus: the
                // invisible tree would keep eating arrow keys and navigating.
                SetFocus((m_outlinePane ? m_outlinePane : m_left.get())->Hwnd());
            }
            Layout();
            UpdateCommandUi();
            return 0;
        }
        case IDC_TOGGLE_TOOLBAR:
            m_toolbarVisible = !m_toolbarVisible;
            Layout();
            UpdateCommandUi();
            return 0;
        case IDC_TOGGLE_STATUSBAR:
            m_statusVisible = !m_statusVisible;
            Layout();
            UpdateCommandUi();
            return 0;
        case IDC_ZOOM_IN:
            FocusedPane()->ApplyZoomRatio(1.25f);
            UpdateCommandUi();
            return 0;
        case IDC_ZOOM_OUT:
            FocusedPane()->ApplyZoomRatio(1.0f / 1.25f);
            UpdateCommandUi();
            return 0;
        case IDC_ZOOM_ACTUAL:
            FocusedPane()->SetManualZoom(1.0f);
            UpdateCommandUi();
            return 0;
        case IDC_FIT_WIDTH:
            FocusedPane()->SetZoomMode(PaneWindow::ZoomMode::FitWidth);
            UpdateCommandUi();
            return 0;
        case IDC_FIT_PAGE:
            FocusedPane()->SetZoomMode(PaneWindow::ZoomMode::FitPage);
            UpdateCommandUi();
            return 0;
        case IDC_SCROLL_CONTINUOUS:
            ApplyScrollMode(PaneWindow::ScrollMode::Continuous);
            return 0;
        case IDC_SCROLL_PAGED:
            ApplyScrollMode(PaneWindow::ScrollMode::Paged);
            return 0;
        case IDC_FULLSCREEN:
            ToggleFullScreen();
            return 0;
        case IDC_LANG_ENGLISH:
            SwitchLanguage(Lang::English);
            return 0;
        case IDC_LANG_ITALIAN:
            SwitchLanguage(Lang::Italian);
            return 0;
        case IDC_ABOUT:
            ShowAboutBox();
            return 0;
        case IDC_EXIT:
            PostMessageW(m_hwnd, WM_CLOSE, 0, 0);
            return 0;
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
        if (hdr->code == TTN_GETDISPINFOW) { // toolbar tooltips; idFrom = command id
            auto* info = reinterpret_cast<NMTTDISPINFOW*>(lParam);
            StrId tip;
            switch (hdr->idFrom) {
            case IDC_OPEN_LEFT:
                tip = StrId::TipOpenLeft;
                break;
            case IDC_OPEN_RIGHT:
                tip = StrId::TipOpenRight;
                break;
            case IDC_TOGGLE_SCROLL_SYNC:
                tip = StrId::TipScrollSync;
                break;
            case IDC_TOGGLE_ZOOM_SYNC:
                tip = StrId::TipZoomSync;
                break;
            case IDC_FIT_WIDTH:
                tip = StrId::TipFitWidth;
                break;
            case IDC_FIT_PAGE:
                tip = StrId::TipFitPage;
                break;
            case IDC_SCROLL_CONTINUOUS:
                tip = StrId::TipScrollContinuous;
                break;
            case IDC_SCROLL_PAGED:
                tip = StrId::TipScrollPaged;
                break;
            case IDC_FIND_SHOW:
                tip = StrId::TipFind;
                break;
            case IDC_TOGGLE_OUTLINE:
                tip = StrId::TipOutline;
                break;
            case IDC_FULLSCREEN:
                tip = StrId::TipFullScreen;
                break;
            default:
                return 0;
            }
            info->lpszText = const_cast<wchar_t*>(Str(tip));
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

    case WM_SYSCOMMAND:
        // A plain Alt press-release generates SC_KEYMENU (lParam == 0) and
        // DefWindowProc would focus the menu bar. When that Alt modified a
        // scroll (the temporary sync unlock), swallow it; Alt+letter
        // mnemonics carry the character in lParam and pass through.
        if ((wParam & 0xFFF0) == SC_KEYMENU && lParam == 0 && m_altScrollGesture) {
            m_altScrollGesture = false;
            return 0;
        }
        // NOT worth also hiding the accelerator underlines while Alt stays
        // held after the scroll: the system repaints them from the physical
        // key state (pushing UISF_HIDEACCEL onto frame and panes plus a
        // menu-bar redraw was verified to have no visual effect), and they
        // are truthful anyway - Alt+letter mnemonics still work mid-gesture.
        break;

    case WM_INITMENU:
        // The menu opened some other way (click, mnemonic): a stale gesture
        // flag must not swallow the NEXT genuine Alt tap.
        m_altScrollGesture = false;
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
        // While full screen the menu is detached: window destruction only
        // frees an attached menu, so this one must be freed by hand.
        if (m_menu && !GetMenu(m_hwnd)) {
            DestroyMenu(m_menu);
            m_menu = nullptr;
        }
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

    // Full screen hides the bars without touching the persisted visibility
    // flags. The bars self-position (CCS_TOP / SBARS grip) and never resize
    // the frame, so no WM_SIZE recursion: only the panes shrink, and their
    // fit zoom is derived from their own window rect.
    const bool toolbarOn = m_toolbar && m_toolbarVisible && !m_fullscreen;
    const bool statusOn = m_status && m_statusVisible && !m_fullscreen;
    if (m_toolbar)
        ShowWindow(m_toolbar, toolbarOn ? SW_SHOW : SW_HIDE);
    if (m_status)
        ShowWindow(m_status, statusOn ? SW_SHOW : SW_HIDE);
    int top = 0;
    int bottom = rc.bottom;
    if (toolbarOn) {
        SendMessageW(m_toolbar, TB_AUTOSIZE, 0, 0);
        RECT bar;
        GetWindowRect(m_toolbar, &bar);
        top = bar.bottom - bar.top;
    }
    if (statusOn) {
        SendMessageW(m_status, WM_SIZE, 0, 0);
        RECT bar;
        GetWindowRect(m_status, &bar);
        bottom -= bar.bottom - bar.top;
        // Right edges of the five parts; the sync summary takes the rest.
        constexpr int kPartDip[] = {130, 60, 130, 60};
        int parts[5];
        int edge = 0;
        for (size_t i = 0; i < std::size(kPartDip); ++i) {
            edge += MulDiv(kPartDip[i], m_dpi, 96);
            parts[i] = edge;
        }
        parts[4] = -1;
        SendMessageW(m_status, SB_SETPARTS, std::size(parts), reinterpret_cast<LPARAM>(parts));
    }
    m_contentTop = top;
    m_contentBottom = bottom;

    const int h = bottom - top;
    const int sidebarW = m_outlineVisible ? MulDiv(260, m_dpi, 96) : 0;
    const int x0 = sidebarW;
    const int w = rc.right - x0;
    if (w <= 0 || h <= 0 || !m_left || !m_left->Hwnd())
        return;

    if (m_outlineTree) {
        if (m_outlineVisible)
            MoveWindow(m_outlineTree, 0, top, sidebarW, h, TRUE);
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
    dwp = DeferWindowPos(dwp, m_left->Hwnd(), nullptr, x0, top, leftW, h,
                         SWP_NOZORDER | SWP_NOACTIVATE);
    dwp = DeferWindowPos(dwp, m_right->Hwnd(), nullptr, x0 + leftW + splitterW, top,
                         w - leftW - splitterW, h, SWP_NOZORDER | SWP_NOACTIVATE);
    EndDeferWindowPos(dwp);
    LayoutFindBar();
    UpdateStatusBar();
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
    for (HWND child : {m_findEdit, m_findCount, m_findPrev, m_findNext, m_findClose,
                       m_outlineTree, m_status}) {
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
    SetWindowPos(m_findBar, HWND_TOP, x, m_contentTop + pad, barW, barH, SWP_NOACTIVATE);

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
    const int splitterW = MulDiv(kSplitterDip, m_dpi, 96);
    return {m_splitterX, m_contentTop, m_splitterX + splitterW, m_contentBottom};
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
    ofn.lpstrFilter = Str(StrId::OpenDlgFilter);
    ofn.lpstrFile = buffer;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = Str(rightPane ? StrId::OpenDlgTitleRight : StrId::OpenDlgTitleLeft);
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
