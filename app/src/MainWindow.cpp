#include "MainWindow.h"

#include "resource.h"
#include "util/DialogTemplate.h"
#include "util/GlyphIcons.h"
#include "util/OutlineNumbering.h"
#include "util/Settings.h"
#include "util/ShellIntegration.h"
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
constexpr GlyphSpec kToolbarGlyphs[] = {
    {0xE8A0}, // 0 open left (arrow into the left pane)
    {0xE89F}, // 1 open right
    {0xE71B}, // 2 scroll sync (link)
    {0xE895}, // 3 zoom sync (circular arrows)
    {0xE8AB}, // 4 fit width (horizontal arrows)
    {0xE9A6}, // 5 fit page
    {0xE721}, // 6 find
    {0xE8FD}, // 7 outline (bulleted list)
    // Full screen: MDL2 has no single 4-corner-arrows glyph; E740 (one
    // diagonal pair) plus its mirror composes all four.
    {0xE740, true}, // 8 full screen
    {0xEC8F},       // 9 continuous scrolling (ScrollUpDown)
    {0xE7C3},       // 10 page-by-page (Page)
    {0xE8A9},       // 11 actual size / 1:1 (ViewAll)
    {0xE718},       // 12 add sync point here (Pin)
    {0xE82D},       // 13 sync points from bookmarks (Dictionary)
    {0xE762},       // 14 sync points list (MultiSelect)
    {0xE894},       // 15 clear sync points (Clear)
    {0xE8E4},       // 16 alignment gaps (uneven rows)
    {0xE748},       // 17 swap panes (Switch)
};

constexpr UINT_PTR kPageBoxId = 2001; // rebar band 2: editable current-page box

// Rebar band ids (RBBIM_ID). Bands are addressed by id, never by index: with
// the rebar unlocked the user can reorder them freely.
constexpr UINT kBandMenu = 0;
constexpr UINT kBandToolbar = 1;
constexpr UINT kBandPageBox = 2;

// Lock-dependent band style, IE "lock the toolbars" semantics: locked = no
// grippers; unlocked = grippers always (first-in-row included) and everything
// draggable. Breaks/hidden bits are layout state preserved by the callers;
// RBBS_FIXEDSIZE is decided separately (ApplyPageBoxFixedSize): comctl32
// forces fixed bands to the end of their row, so the bit is position-bound.
UINT BandStyle(UINT id, bool locked) {
    UINT style = locked ? RBBS_NOGRIPPER : RBBS_GRIPPERALWAYS;
    if (id == kBandMenu || id == kBandToolbar)
        style |= RBBS_USECHEVRON; // both clip into an overflow popup when squeezed
    return style;
}

// Options dialog control ids (2100+ control-id space) and its modal state.
constexpr WORD kOptRestoreId = 2101;
constexpr WORD kOptScrollModeId = 2102;
constexpr WORD kOptZoomModeId = 2103;
constexpr WORD kOptScrollSyncId = 2104;
constexpr WORD kOptZoomSyncId = 2105;
constexpr WORD kOptSynctexId = 2106;
constexpr WORD kOptShellId = 2107;
constexpr WORD kOptClearMruId = 2108;
constexpr WORD kOptWheelId = 2109;
constexpr WORD kOptAnchorsId = 2110;
constexpr WORD kOptTicksId = 2111;
constexpr WORD kOptFsToolbarId = 2112;
constexpr WORD kOptFsStatusId = 2113;
constexpr WORD kOptHeaderId = 2114;
constexpr WORD kOptHeaderPathId = 2115;
constexpr WORD kSyncPtsListId = 2401;
constexpr WORD kSyncPtsRemoveId = 2402;
constexpr WORD kSyncPtsClearId = 2403;
// Posted to the (modal) sync-points dialog when the map changes underneath it:
// the modal loop still dispatches WM_PSV_* messages, so an auto-reload can
// clear/regenerate the map while the dialog shows stale rows.
constexpr UINT kMsgSyncPtsRefresh = WM_APP + 20;
struct OptionsDialogState {
    MainWindow* self = nullptr;
    bool clearRecent = false; // armed by the button, applied on OK (cancel-safe)
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
    if (m_fsBarIcons)
        ImageList_Destroy(m_fsBarIcons);
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
    SetUiLanguage(LangFromCode(session.language));
    m_toolbarVisible = session.toolbar;
    m_statusVisible = session.statusbar;
    m_outlineVisible = session.outline;
    m_synctexInverse = session.synctexInverse;
    m_restoreSession = session.restoreSession;
    m_wheelLines = session.wheelLines;
    m_outlineWidthDip = session.outlineWidth;
    m_rebarLocked = session.rebarLocked;
    m_rebarBandsSaved = session.rebarBands; // applied by BuildRebar in WM_CREATE
    m_toolbarText = std::clamp(session.toolbarText, 0, 2); // read by CreateToolbar
    m_fsShowToolbar = session.fsToolbar;
    m_fsShowStatus = session.fsStatus;
    m_defaults.scrollMode = session.defScrollMode != 0 ? PaneWindow::ScrollMode::Paged
                                                       : PaneWindow::ScrollMode::Continuous;
    m_defaults.zoomMode = static_cast<PaneWindow::ZoomMode>(session.defZoomMode);
    m_defaults.scrollSync = session.defScrollSync;
    m_defaults.zoomSync = session.defZoomSync;
    m_mruFiles = session.mruFiles;
    m_mruPairs = session.mruPairs;
    m_savedPoints = session.syncPoints;

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
    // UI preferences like the toolbar flags: applied on every launch path.
    m_showAlignmentGaps = session.showGaps;
    m_showAnchors = session.showAnchors;
    m_showTicks = session.showTicks;
    m_sync->SetAlignmentGapsEnabled(m_showAlignmentGaps);
    m_sync->SetMapChangedHandler([this] { ApplyAlignmentGaps(); });
    m_left->SetMarkerVisibility(m_showAnchors, m_showTicks);
    m_right->SetMarkerVisibility(m_showAnchors, m_showTicks);
    m_showHeader = session.showHeader;
    m_headerShowPath = session.headerShowPath;
    m_left->SetHeaderOptions(m_showHeader, m_headerShowPath);
    m_right->SetHeaderOptions(m_showHeader, m_headerShowPath);
    // m_activePane defaults to the left pane; mark it so the cue and the outline
    // association are visible before the first focus (and while the window is
    // inactive at startup).
    m_left->SetActive(true);

    // The HMENU is NEVER attached to the window: it is the popup source for
    // the rebar-hosted menu band, and the band lives in the client area (the
    // rebar height comes out of Layout, not the non-client menu band).
    m_menu = BuildMenuBar();
    CreateWindowExW(0, kClassName, L"PDF Side Viewer", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
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
    // UIPI hardening: accept WM_COPYDATA (forward search, Explorer verbs)
    // even from a lower-integrity second instance.
    ChangeWindowMessageFilterEx(m_hwnd, WM_COPYDATA, MSGFLT_ALLOW, nullptr);

    const auto onViewChanged = [this](PaneWindow& p, PaneWindow::ViewEvent e, float r) {
        if (e == PaneWindow::ViewEvent::FocusGained) {
            m_activePane = &p; // the page box targets the last-focused pane
            // The active-pane cue persists through window deactivation, so it is
            // driven here from m_activePane, not from the panes' Win32 focus.
            m_left->SetActive(m_left.get() == &p);
            m_right->SetActive(m_right.get() == &p);
            if (m_outlineVisible && m_outlinePane != &p)
                UpdateOutlineSidebar(&p);
            else
                m_outlinePane = &p;
        } else {
            m_sync->OnViewChanged(p, e, r); // DocumentOpened clears the map, parks the regen
            if (e == PaneWindow::ViewEvent::DocumentOpened && m_outlineVisible &&
                m_outlinePane == &p)
                UpdateOutlineSidebar(&p);
            if (e == PaneWindow::ViewEvent::DocumentOpened) {
                // Auto-reload = the pane re-opened (or failed to re-open: the
                // path survives an error open) the SAME file. Generated points
                // are derived data: once both panes are ready again, re-derive
                // them from the fresh outlines - the LaTeX rebuild loop must
                // not lose the bookmark map on every compile, even when both
                // panes reload from one build or a broken intermediate compile
                // fails the first attempt (the parked cue rides those out). A
                // path change (open, close, swap) cancels the parked regen:
                // the map's context is gone. Manual points stay gone on
                // reload: they reference pages the rebuild may have moved.
                std::wstring& lastDoc = (&p == m_right.get()) ? m_lastDocRight : m_lastDocLeft;
                const std::wstring& cur = p.DocumentPath();
                const bool pathChanged =
                    cur.empty() || lstrcmpiW(cur.c_str(), lastDoc.c_str()) != 0;
                if (pathChanged)
                    m_sync->CancelAutoRegen();
                lastDoc = cur;
                if (m_sync->AutoRegenPending() && m_left->HasDocument() &&
                    m_right->HasDocument() && !m_left->Outline().empty() &&
                    !m_right->Outline().empty())
                    GenerateSyncPointsFromBookmarks(false);
                if (m_swapMap.pending) {
                    const std::wstring& expect =
                        (&p == m_left.get()) ? m_swapMap.expectLeft : m_swapMap.expectRight;
                    if (!p.HasDocument() ||
                        lstrcmpiW(p.DocumentPath().c_str(), expect.c_str()) != 0) {
                        // Failed reopen, close, or an interleaved open of a
                        // different file: the parked map's context is gone.
                        m_swapMap = {};
                    } else {
                        ((&p == m_left.get()) ? m_swapMap.leftSettled
                                              : m_swapMap.rightSettled) = true;
                        if (m_swapMap.leftSettled && m_swapMap.rightSettled) {
                            m_sync->RestorePoints(std::move(m_swapMap.points));
                            m_swapMap = {};
                            if (m_sync->ScrollSync())
                                m_sync->RealignFollower(*FocusedPane());
                            RememberSyncPoints(); // the mirrored pair persists too
                        }
                    }
                }
                // A NEW pair (not a reload) with no map of its own: bring
                // back its remembered sync points, if any.
                if (pathChanged)
                    TryRestoreSavedPoints();
            }
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
        if (&p == m_activePane)
            UpdatePageBox(); // cheap: change-guarded, skipped while it has focus
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
    // Session restore off = the configured DEFAULTS drive the launch state.
    m_scrollMode = (m_restoreSession ? session.scrollMode != 0
                                     : m_defaults.scrollMode == PaneWindow::ScrollMode::Paged)
                       ? PaneWindow::ScrollMode::Paged
                       : PaneWindow::ScrollMode::Continuous;
    m_left->SetScrollMode(m_scrollMode);
    m_right->SetScrollMode(m_scrollMode);
    m_left->SetDefaultZoomMode(m_defaults.zoomMode);
    m_right->SetDefaultZoomMode(m_defaults.zoomMode);
    m_left->SetWheelLinesOverride(m_wheelLines);
    m_right->SetWheelLinesOverride(m_wheelLines);
    const auto requestScrollMode = [this](PaneWindow::ScrollMode mode) { ApplyScrollMode(mode); };
    m_left->SetScrollModeRequestHandler(requestScrollMode);
    m_right->SetScrollModeRequestHandler(requestScrollMode);

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
        m_sync->SetZoomSync(m_restoreSession ? session.zoomSync : m_defaults.zoomSync);
        if (!leftFile.empty())
            m_left->OpenDocument(std::move(leftFile));
        if (!rightFile.empty())
            m_right->OpenDocument(std::move(rightFile));
        m_sync->SetScrollSync(m_restoreSession ? session.scrollSync : m_defaults.scrollSync);
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
    m_sync->SetZoomSync(m_restoreSession ? session.zoomSync : m_defaults.zoomSync);
    // Session restore off: chrome, placement and splitter still restore, but
    // the panes start empty and the sync locks come from the defaults. The
    // fallbacks stay seeded from the session either way, so a restore-less
    // launch does not wipe the stored panes at the next SaveSession.
    if (m_restoreSession) {
        // m_fallback* already hold the DPI-rescaled offsets; a later
        // WM_DPICHANGED (e.g. from the placement below) rescales the panes'
        // pending restores.
        if (!m_fallbackLeft.path.empty() &&
            GetFileAttributesW(m_fallbackLeft.path.c_str()) != INVALID_FILE_ATTRIBUTES)
            m_left->OpenDocumentWithView(
                m_fallbackLeft.path, m_fallbackLeft.zoom, m_fallbackLeft.scrollX,
                m_fallbackLeft.scrollY,
                static_cast<PaneWindow::ZoomMode>(m_fallbackLeft.zoomMode));
        if (!m_fallbackRight.path.empty() &&
            GetFileAttributesW(m_fallbackRight.path.c_str()) != INVALID_FILE_ATTRIBUTES)
            m_right->OpenDocumentWithView(
                m_fallbackRight.path, m_fallbackRight.zoom, m_fallbackRight.scrollX,
                m_fallbackRight.scrollY,
                static_cast<PaneWindow::ZoomMode>(m_fallbackRight.zoomMode));
    }
    // After the restored positions land, DocumentOpened events recapture the
    // anchor, so enabling scroll sync here preserves the saved alignment.
    m_sync->SetScrollSync(m_restoreSession ? session.scrollSync : m_defaults.scrollSync);
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
    s.showGaps = m_showAlignmentGaps;
    s.showAnchors = m_showAnchors;
    s.showTicks = m_showTicks;
    s.showHeader = m_showHeader;
    s.headerShowPath = m_headerShowPath;
    s.scrollMode = m_scrollMode == PaneWindow::ScrollMode::Paged ? 1 : 0;
    s.restoreSession = m_restoreSession;
    s.wheelLines = m_wheelLines;
    s.outlineWidth = m_outlineWidthDip;
    s.rebarLocked = m_rebarLocked;
    s.rebarBands = SerializeRebarLayout();
    s.toolbarText = m_toolbarText;
    s.fsToolbar = m_fsShowToolbar;
    s.fsStatus = m_fsShowStatus;
    s.defScrollMode = m_defaults.scrollMode == PaneWindow::ScrollMode::Paged ? 1 : 0;
    s.defZoomMode = static_cast<int>(m_defaults.zoomMode);
    s.defScrollSync = m_defaults.scrollSync;
    s.defZoomSync = m_defaults.zoomSync;
    s.dpi = m_dpi;
    s.toolbar = m_toolbarVisible;
    s.statusbar = m_statusVisible;
    s.outline = m_outlineVisible;
    s.language = LangCode(UiLanguage());
    s.synctexInverse = m_synctexInverse;
    s.mruFiles = m_mruFiles;
    s.mruPairs = m_mruPairs;
    s.syncPoints = m_savedPoints;
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
    append(file, IDC_OPTIONS, StrId::MenuOptions);
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    append(file, IDC_EXIT, StrId::MenuExit);

    HMENU lang = CreatePopupMenu();
    // Alphabetical by native name (Greek and Cyrillic after Latin), NOT id
    // order: the ids keep Lang-enum order and both the radio check and the
    // id -> Lang mapping are MF_BYCOMMAND, so menu position is free.
    append(lang, IDC_LANG_CZECH, StrId::MenuLangCzech);
    append(lang, IDC_LANG_GERMAN, StrId::MenuLangGerman);
    append(lang, IDC_LANG_ENGLISH, StrId::MenuLangEnglish);
    append(lang, IDC_LANG_SPANISH, StrId::MenuLangSpanish);
    append(lang, IDC_LANG_FRENCH, StrId::MenuLangFrench);
    append(lang, IDC_LANG_ITALIAN, StrId::MenuLangItalian);
    append(lang, IDC_LANG_HUNGARIAN, StrId::MenuLangHungarian);
    append(lang, IDC_LANG_DUTCH, StrId::MenuLangDutch);
    append(lang, IDC_LANG_POLISH, StrId::MenuLangPolish);
    append(lang, IDC_LANG_PORTUGUESE, StrId::MenuLangPortuguese);
    append(lang, IDC_LANG_ROMANIAN, StrId::MenuLangRomanian);
    append(lang, IDC_LANG_SWEDISH, StrId::MenuLangSwedish);
    append(lang, IDC_LANG_GREEK, StrId::MenuLangGreek);
    append(lang, IDC_LANG_UKRAINIAN, StrId::MenuLangUkrainian);

    HMENU view = CreatePopupMenu();
    append(view, IDC_TOGGLE_TOOLBAR, StrId::MenuToolbar);
    append(view, IDC_TOGGLE_STATUSBAR, StrId::MenuStatusBar);
    append(view, IDC_TOGGLE_OUTLINE, StrId::MenuOutline);
    append(view, IDC_LOCK_TOOLBARS, StrId::MenuLockToolbars);
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
    append(view, IDC_GOTO_PAGE, StrId::MenuGotoPage);
    AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(view, MF_POPUP, reinterpret_cast<UINT_PTR>(lang), Str(StrId::MenuLanguage));
    AppendMenuW(view, MF_SEPARATOR, 0, nullptr);
    append(view, IDC_FULLSCREEN, StrId::MenuFullScreen);

    HMENU sync = CreatePopupMenu();
    append(sync, IDC_TOGGLE_SCROLL_SYNC, StrId::MenuScrollSync);
    append(sync, IDC_TOGGLE_ZOOM_SYNC, StrId::MenuZoomSync);
    AppendMenuW(sync, MF_SEPARATOR, 0, nullptr);
    append(sync, IDC_ADD_SYNC_POINT, StrId::MenuAddSyncPoint);
    append(sync, IDC_SYNC_FROM_BOOKMARKS, StrId::MenuSyncFromBookmarks);
    append(sync, IDC_SYNC_POINTS, StrId::MenuSyncPoints);
    append(sync, IDC_CLEAR_SYNC_POINTS, StrId::MenuClearSyncPoints);
    AppendMenuW(sync, MF_SEPARATOR, 0, nullptr);
    append(sync, IDC_TOGGLE_ALIGNMENT_GAPS, StrId::MenuAlignmentGaps);
    AppendMenuW(sync, MF_SEPARATOR, 0, nullptr);
    append(sync, IDC_SWAP_PANES, StrId::MenuSwapPanes);

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
        MessageBoxW(m_hwnd, msg.c_str(), L"PDF Side Viewer", MB_OK | MB_ICONWARNING);
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
        MessageBoxW(m_hwnd, msg.c_str(), L"PDF Side Viewer", MB_OK | MB_ICONWARNING);
        return;
    }
    m_left->OpenDocument(pair.left);
    m_right->OpenDocument(pair.right);
}

void MainWindow::CreateToolbar(HINSTANCE hinst) {
    // FLAT|TRANSPARENT is REQUIRED inside a rebar band: the rebar paints the
    // band background under the transparent toolbar. (The historical "black
    // band" bug only applies to a flat toolbar parented to a window that
    // paints nothing beneath its children.)
    // IE's toolbar text options: mode 1 shows every label BELOW its icon (a
    // plain toolbar with strings); mode 2 = TBSTYLE_LIST (text beside the
    // icon) + TBSTYLE_EX_MIXEDBUTTONS, where only BTNS_SHOWTEXT buttons show
    // their label - "Selective text on right". TBSTYLE_LIST cannot be
    // toggled reliably on a live toolbar: SetToolbarTextMode recreates it.
    DWORD style = WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | TBSTYLE_FLAT |
                  TBSTYLE_TRANSPARENT | TBSTYLE_TOOLTIPS | CCS_NORESIZE | CCS_NOPARENTALIGN |
                  CCS_NODIVIDER;
    if (m_toolbarText == 2)
        style |= TBSTYLE_LIST;
    m_toolbar = CreateWindowExW(0, TOOLBARCLASSNAMEW, nullptr, style, 0, 0, 0, 0, m_rebar,
                                reinterpret_cast<HMENU>(static_cast<UINT_PTR>(102)), hinst,
                                nullptr);
    if (!m_toolbar)
        return;
    SendMessageW(m_toolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    if (m_toolbarText == 2)
        SendMessageW(m_toolbar, TB_SETEXTENDEDSTYLE, 0, TBSTYLE_EX_MIXEDBUTTONS);
    SendMessageW(m_toolbar, TB_SETMAXTEXTROWS, m_toolbarText == 0 ? 0 : 1, 0);
    RebuildToolbarIcons();

    // selective = IE's primary-action set for "Selective text on right".
    const int textMode = m_toolbarText;
    const auto button = [textMode](int image, WORD id, BYTE style, StrId label,
                                   bool selective = false) {
        TBBUTTON b{};
        b.iBitmap = image;
        b.idCommand = id;
        b.fsState = TBSTATE_ENABLED;
        b.fsStyle = style;
        if (textMode != 0) {
            b.fsStyle |= BTNS_AUTOSIZE; // width follows the label
            if (textMode == 1 || selective)
                b.fsStyle |= BTNS_SHOWTEXT; // only honored under MIXEDBUTTONS
            // Str() points into the static language tables: safe to hand the
            // pointer straight to comctl32.
            b.iString = reinterpret_cast<INT_PTR>(Str(label));
        }
        return b;
    };
    const auto separator = [] {
        TBBUTTON b{};
        b.fsStyle = BTNS_SEP;
        return b;
    };
    const TBBUTTON buttons[] = {
        button(0, IDC_OPEN_LEFT, BTNS_BUTTON, StrId::LblOpenLeft, true),
        button(1, IDC_OPEN_RIGHT, BTNS_BUTTON, StrId::LblOpenRight, true),
        separator(),
        button(2, IDC_TOGGLE_SCROLL_SYNC, BTNS_CHECK, StrId::LblScrollSync),
        button(3, IDC_TOGGLE_ZOOM_SYNC, BTNS_CHECK, StrId::LblZoomSync),
        separator(),
        button(12, IDC_ADD_SYNC_POINT, BTNS_BUTTON, StrId::LblAddSyncPoint),
        button(13, IDC_SYNC_FROM_BOOKMARKS, BTNS_BUTTON, StrId::LblSyncFromBookmarks, true),
        button(14, IDC_SYNC_POINTS, BTNS_BUTTON, StrId::LblSyncPoints),
        button(15, IDC_CLEAR_SYNC_POINTS, BTNS_BUTTON, StrId::LblClearSyncPoints),
        button(16, IDC_TOGGLE_ALIGNMENT_GAPS, BTNS_CHECK, StrId::LblAlignmentGaps),
        separator(),
        button(17, IDC_SWAP_PANES, BTNS_BUTTON, StrId::LblSwapPanes, true),
        separator(),
        button(11, IDC_ZOOM_ACTUAL, BTNS_BUTTON, StrId::LblActualSize), // momentary
        // A manual check pair, not BTNS_CHECKGROUP: Manual zoom mode means
        // NEITHER fit button is pressed, which a radio group cannot show.
        button(4, IDC_FIT_WIDTH, BTNS_CHECK, StrId::LblFitWidth),
        button(5, IDC_FIT_PAGE, BTNS_CHECK, StrId::LblFitPage),
        separator(),
        // Scroll-mode pair: exactly one is always pressed (UpdateCommandUi).
        button(9, IDC_SCROLL_CONTINUOUS, BTNS_CHECK, StrId::LblScrollContinuous),
        button(10, IDC_SCROLL_PAGED, BTNS_CHECK, StrId::LblScrollPaged),
        separator(),
        button(6, IDC_FIND_SHOW, BTNS_BUTTON, StrId::LblFind),
        button(7, IDC_TOGGLE_OUTLINE, BTNS_CHECK, StrId::LblOutline),
        separator(),
        button(8, IDC_FULLSCREEN, BTNS_BUTTON, StrId::LblFullScreen),
    };
    SendMessageW(m_toolbar, TB_ADDBUTTONSW, std::size(buttons),
                 reinterpret_cast<LPARAM>(buttons));
}

void MainWindow::RebuildToolbarInBand() {
    // TBSTYLE_LIST cannot be flipped on a live toolbar (and comctl32's string
    // pool ownership across language switches is murky): rebuild it and hand
    // the band the new child (bands are addressed by RBBIM_ID; the new window
    // replaces the old one in place).
    if (!m_rebar || !m_toolbar)
        return;
    const HINSTANCE hinst =
        reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(m_hwnd, GWLP_HINSTANCE));
    DestroyWindow(m_toolbar);
    m_toolbar = nullptr;
    if (m_toolbarIcons) {
        ImageList_Destroy(m_toolbarIcons);
        m_toolbarIcons = nullptr;
    }
    CreateToolbar(hinst);
    const LRESULT index = SendMessageW(m_rebar, RB_IDTOINDEX, kBandToolbar, 0);
    if (index != -1 && m_toolbar) {
        REBARBANDINFOW band{};
        band.cbSize = sizeof(band);
        band.fMask = RBBIM_CHILD;
        band.hwndChild = m_toolbar;
        SendMessageW(m_rebar, RB_SETBANDINFOW, static_cast<WPARAM>(index),
                     reinterpret_cast<LPARAM>(&band));
    }
    UpdateRebarBandSizes(); // new button extents: band min/ideal sizes moved
    UpdateCommandUi();      // re-press the checked states on the fresh buttons
    Layout();               // the band height likely changed
}

void MainWindow::SetToolbarTextMode(int mode) {
    mode = std::clamp(mode, 0, 2);
    if (mode == m_toolbarText)
        return;
    m_toolbarText = mode;
    RebuildToolbarInBand();
}

void MainWindow::EnsureFsBar() {
    if (m_fsBar && m_fsBarDpi == m_dpi)
        return;
    if (m_fsBar) {
        DestroyWindow(m_fsBar);
        m_fsBar = nullptr;
    }
    if (m_fsBarIcons) {
        ImageList_Destroy(m_fsBarIcons);
        m_fsBarIcons = nullptr;
    }
    const HINSTANCE hinst =
        reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(m_hwnd, GWLP_HINSTANCE));
    // NOT TBSTYLE_FLAT: this toolbar's parent is the main window, which clips
    // its children and paints nothing beneath them (the documented
    // flat-toolbar black-band trap); the standard raised look is intended.
    m_fsBar = CreateWindowExW(0, TOOLBARCLASSNAMEW, nullptr,
                              WS_CHILD | WS_CLIPSIBLINGS | TBSTYLE_TOOLTIPS | CCS_NORESIZE |
                                  CCS_NOPARENTALIGN | CCS_NODIVIDER,
                              0, 0, 0, 0, m_hwnd,
                              reinterpret_cast<HMENU>(static_cast<UINT_PTR>(105)), hinst,
                              nullptr);
    if (!m_fsBar)
        return;
    SendMessageW(m_fsBar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    const int glyphPx = MulDiv(16, m_dpi, 96);
    const GlyphSpec glyph[] = {{0xE740, true}}; // same composed 4-corner arrows
    m_fsBarIcons = CreateGlyphImageList(std::span<const GlyphSpec>(glyph), glyphPx, glyphPx,
                                        GetSysColor(COLOR_BTNTEXT));
    SendMessageW(m_fsBar, TB_SETIMAGELIST, 0, reinterpret_cast<LPARAM>(m_fsBarIcons));
    TBBUTTON b{};
    b.iBitmap = 0;
    b.idCommand = IDC_FULLSCREEN; // ordinary dispatch: toggles right back out
    b.fsState = TBSTATE_ENABLED;
    b.fsStyle = BTNS_BUTTON;
    SendMessageW(m_fsBar, TB_ADDBUTTONSW, 1, reinterpret_cast<LPARAM>(&b));
    const int btn = MulDiv(28, m_dpi, 96);
    SendMessageW(m_fsBar, TB_SETBUTTONSIZE, 0, MAKELPARAM(btn, btn));
    SendMessageW(m_fsBar, TB_AUTOSIZE, 0, 0);
    m_fsBarDpi = m_dpi;
}

void MainWindow::BuildRebar(HINSTANCE hinst) {
    // Menu band + command toolbar + page box, one row by default. Locked =
    // IE "lock the toolbars" (no grippers); unlocked the bands drag, reorder
    // and wrap to extra rows, and the layout persists in the session.
    m_rebar = CreateWindowExW(0, REBARCLASSNAMEW, nullptr,
                              WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS |
                                  RBS_VARHEIGHT | RBS_BANDBORDERS | CCS_NODIVIDER |
                                  CCS_NOPARENTALIGN,
                              0, 0, 0, 0, m_hwnd,
                              reinterpret_cast<HMENU>(static_cast<UINT_PTR>(104)), hinst,
                              nullptr);
    if (!m_rebar)
        return;
    m_menuBand.Create(m_hwnd, m_rebar, hinst, m_menu);
    CreateToolbar(hinst);
    m_pageBox = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | ES_AUTOHSCROLL |
                                    ES_CENTER,
                                0, 0, 0, 0, m_rebar, reinterpret_cast<HMENU>(kPageBoxId), hinst,
                                nullptr);
    if (m_pageBox)
        SetWindowSubclass(m_pageBox, PageBoxProc, 1, reinterpret_cast<DWORD_PTR>(this));

    const auto insertBand = [this](UINT id, HWND child, int cxMin, int cyMin, int cxIdeal,
                                   bool newRow = false) {
        REBARBANDINFOW band{};
        band.cbSize = sizeof(band);
        band.fMask = RBBIM_ID | RBBIM_CHILD | RBBIM_CHILDSIZE | RBBIM_STYLE | RBBIM_SIZE |
                     RBBIM_IDEALSIZE;
        band.wID = id;
        // The default row break survives lock toggles (SetRebarLocked
        // snapshots RBBS_BREAK per band) and is overridden by a saved layout.
        band.fStyle = BandStyle(id, m_rebarLocked) | (newRow ? RBBS_BREAK : 0u);
        band.hwndChild = child;
        band.cxMinChild = static_cast<UINT>(cxMin);
        band.cyMinChild = static_cast<UINT>(cyMin);
        band.cx = static_cast<UINT>(cxIdeal);
        band.cxIdeal = static_cast<UINT>(cxIdeal);
        SendMessageW(m_rebar, RB_INSERTBANDW, static_cast<WPARAM>(-1),
                     reinterpret_cast<LPARAM>(&band));
    };
    const SIZE menuSize = m_menuBand.IdealSize();
    const DWORD menuBtn =
        static_cast<DWORD>(SendMessageW(m_menuBand.Toolbar(), TB_GETBUTTONSIZE, 0, 0));
    // cxMinChild is one button, not the ideal width: a dragged (or squeezed)
    // menu band shrinks and overflows into its chevron instead of pinning the
    // row width.
    insertBand(kBandMenu, m_menuBand.Toolbar(), LOWORD(menuBtn), HIWORD(menuBtn), menuSize.cx);
    SIZE toolSize{};
    SendMessageW(m_toolbar, TB_GETMAXSIZE, 0, reinterpret_cast<LPARAM>(&toolSize));
    const DWORD toolBtn = static_cast<DWORD>(SendMessageW(m_toolbar, TB_GETBUTTONSIZE, 0, 0));
    // Default layout: menu row on top, toolbar + page box on their own row
    // (the labeled default toolbar would not share a row comfortably).
    insertBand(kBandToolbar, m_toolbar, MulDiv(80, m_dpi, 96), HIWORD(toolBtn), toolSize.cx,
               /*newRow=*/true);
    insertBand(kBandPageBox, m_pageBox, MulDiv(64, m_dpi, 96), MulDiv(22, m_dpi, 96),
               MulDiv(64, m_dpi, 96));
    UpdateRebarBandSizes();              // menu band cx += header (chevron math)
    ApplyRebarLayout(m_rebarBandsSaved); // saved order/widths/rows, if any
    ApplyPageBoxFixedSize();
}

void MainWindow::UpdateRebarBandSizes() {
    if (!m_rebar)
        return;
    // DPI change: button and text extents moved; refresh every band's
    // child-size metrics or the row keeps the stale height. By id: the user
    // may have reordered the bands.
    const auto setBand = [this](UINT id, int cxMin, int cyMin, int cxIdeal) {
        const LRESULT index = SendMessageW(m_rebar, RB_IDTOINDEX, id, 0);
        if (index == -1)
            return;
        REBARBANDINFOW band{};
        band.cbSize = sizeof(band);
        band.fMask = RBBIM_CHILDSIZE | RBBIM_IDEALSIZE;
        band.cxMinChild = static_cast<UINT>(cxMin);
        band.cyMinChild = static_cast<UINT>(cyMin);
        band.cxIdeal = static_cast<UINT>(cxIdeal);
        SendMessageW(m_rebar, RB_SETBANDINFOW, static_cast<WPARAM>(index),
                     reinterpret_cast<LPARAM>(&band));
    };
    SendMessageW(m_menuBand.Toolbar(), TB_AUTOSIZE, 0, 0);
    SendMessageW(m_toolbar, TB_AUTOSIZE, 0, 0);
    const SIZE menuSize = m_menuBand.IdealSize();
    const DWORD menuBtn =
        static_cast<DWORD>(SendMessageW(m_menuBand.Toolbar(), TB_GETBUTTONSIZE, 0, 0));
    setBand(kBandMenu, LOWORD(menuBtn), HIWORD(menuBtn), menuSize.cx);
    SIZE toolSize{};
    SendMessageW(m_toolbar, TB_GETMAXSIZE, 0, reinterpret_cast<LPARAM>(&toolSize));
    const DWORD toolBtn = static_cast<DWORD>(SendMessageW(m_toolbar, TB_GETBUTTONSIZE, 0, 0));
    setBand(kBandToolbar, MulDiv(80, m_dpi, 96), HIWORD(toolBtn), toolSize.cx);
    setBand(kBandPageBox, MulDiv(64, m_dpi, 96), MulDiv(22, m_dpi, 96), MulDiv(64, m_dpi, 96));
    // The menu band never stretches (the toolbar band absorbs the row slack),
    // and USECHEVRON clips the CHILD as soon as it dips under cxIdeal. The
    // child gets cx minus header minus BAND BORDERS, and the border share is
    // not exposed by any API (RBBIM_HEADERSIZE reads 0 for gripper-less
    // bands while ~4px of RBS_BANDBORDERS still apply): measure it. Oversize
    // the band so no chevron can trigger, read what the child actually got,
    // then set cx so the child lands exactly on its ideal.
    const LRESULT menuIndex = SendMessageW(m_rebar, RB_IDTOINDEX, kBandMenu, 0);
    if (menuIndex != -1) {
        REBARBANDINFOW band{};
        band.cbSize = sizeof(band);
        band.fMask = RBBIM_SIZE;
        band.cx = static_cast<UINT>(menuSize.cx) + MulDiv(64, m_dpi, 96);
        SendMessageW(m_rebar, RB_SETBANDINFOW, static_cast<WPARAM>(menuIndex),
                     reinterpret_cast<LPARAM>(&band));
        RECT child{};
        GetClientRect(m_menuBand.Toolbar(), &child); // resized synchronously above
        const int overhead =
            std::max(0, static_cast<int>(band.cx) - static_cast<int>(child.right));
        band.cx = static_cast<UINT>(menuSize.cx + overhead);
        SendMessageW(m_rebar, RB_SETBANDINFOW, static_cast<WPARAM>(menuIndex),
                     reinterpret_cast<LPARAM>(&band));
    }
}

void MainWindow::SetRebarLocked(bool locked) {
    m_rebarLocked = locked;
    if (!m_rebar)
        return;
    const int count = static_cast<int>(SendMessageW(m_rebar, RB_GETBANDCOUNT, 0, 0));
    // Snapshot order and breaks FIRST: applying RBBS_FIXEDSIZE makes the
    // rebar RELOCATE that band behind its row sibling (observed, comctl32
    // v6), which would skew an index-based loop and destroy the user's
    // arrangement. Styles are written by id, then the order is put back.
    struct BandPos {
        UINT id;
        UINT brk;
    };
    std::vector<BandPos> order;
    for (int i = 0; i < count; ++i) {
        REBARBANDINFOW band{};
        band.cbSize = sizeof(band);
        band.fMask = RBBIM_ID | RBBIM_STYLE;
        if (SendMessageW(m_rebar, RB_GETBANDINFOW, static_cast<WPARAM>(i),
                         reinterpret_cast<LPARAM>(&band)))
            order.push_back({band.wID, band.fStyle & RBBS_BREAK});
    }
    for (const BandPos& pos : order) {
        const LRESULT index = SendMessageW(m_rebar, RB_IDTOINDEX, pos.id, 0);
        if (index == -1)
            continue;
        REBARBANDINFOW band{};
        band.cbSize = sizeof(band);
        band.fMask = RBBIM_STYLE | RBBIM_SIZE | RBBIM_HEADERSIZE;
        if (!SendMessageW(m_rebar, RB_GETBANDINFOW, static_cast<WPARAM>(index),
                          reinterpret_cast<LPARAM>(&band)))
            continue;
        const int oldHeader = static_cast<int>(band.cxHeader);
        const int oldCx = static_cast<int>(band.cx);
        // Only the lock-dependent bits change: the break comes from the
        // snapshot (a relocation may have shuffled the live one) and hidden
        // state survives as-is.
        const UINT hidden = band.fStyle & RBBS_HIDDEN;
        band.fMask = RBBIM_STYLE;
        band.fStyle = BandStyle(pos.id, locked) | pos.brk | hidden;
        SendMessageW(m_rebar, RB_SETBANDINFOW, static_cast<WPARAM>(index),
                     reinterpret_cast<LPARAM>(&band));
        // The gripper lives in the band header: keep the CHILD width stable
        // across the toggle or every band shifts and chevrons pop.
        band.fMask = RBBIM_HEADERSIZE;
        if (SendMessageW(m_rebar, RB_GETBANDINFOW, static_cast<WPARAM>(index),
                         reinterpret_cast<LPARAM>(&band)) &&
            static_cast<int>(band.cxHeader) != oldHeader) {
            band.fMask = RBBIM_SIZE;
            band.cx = static_cast<UINT>(
                std::max(0, oldCx + static_cast<int>(band.cxHeader) - oldHeader));
            SendMessageW(m_rebar, RB_SETBANDINFOW, static_cast<WPARAM>(index),
                         reinterpret_cast<LPARAM>(&band));
        }
    }
    for (int target = 0; target < static_cast<int>(order.size()); ++target) {
        const LRESULT current = SendMessageW(m_rebar, RB_IDTOINDEX, order[target].id, 0);
        if (current != -1 && current != target)
            SendMessageW(m_rebar, RB_MOVEBAND, static_cast<WPARAM>(current),
                         static_cast<LPARAM>(target));
    }
    ApplyPageBoxFixedSize();
    Layout(); // gripper widths shift the row metrics
    UpdateCommandUi();
}

void MainWindow::ApplyPageBoxFixedSize() {
    if (!m_rebar)
        return;
    // RBBS_FIXEDSIZE keeps the page box from absorbing the row leftover in
    // the locked default layout, but comctl32 RELOCATES a fixed band that
    // precedes others in its row (fixed bands are forced to the row end, and
    // every re-layout re-applies it): the bit is legal ONLY while the band
    // already closes its row; anywhere else it stays a normal band.
    const LRESULT index = SendMessageW(m_rebar, RB_IDTOINDEX, kBandPageBox, 0);
    if (index == -1)
        return;
    const int count = static_cast<int>(SendMessageW(m_rebar, RB_GETBANDCOUNT, 0, 0));
    bool rowLast = index == count - 1;
    if (!rowLast) {
        REBARBANDINFOW next{};
        next.cbSize = sizeof(next);
        next.fMask = RBBIM_STYLE;
        if (SendMessageW(m_rebar, RB_GETBANDINFOW, static_cast<WPARAM>(index + 1),
                         reinterpret_cast<LPARAM>(&next)))
            rowLast = (next.fStyle & RBBS_BREAK) != 0;
    }
    REBARBANDINFOW band{};
    band.cbSize = sizeof(band);
    band.fMask = RBBIM_STYLE;
    if (!SendMessageW(m_rebar, RB_GETBANDINFOW, static_cast<WPARAM>(index),
                      reinterpret_cast<LPARAM>(&band)))
        return;
    const UINT want = (m_rebarLocked && rowLast) ? (band.fStyle | RBBS_FIXEDSIZE)
                                                 : (band.fStyle & ~RBBS_FIXEDSIZE);
    if (want != band.fStyle) {
        band.fStyle = want;
        SendMessageW(m_rebar, RB_SETBANDINFOW, static_cast<WPARAM>(index),
                     reinterpret_cast<LPARAM>(&band));
    }
}

std::wstring MainWindow::SerializeRebarLayout() const {
    if (!m_rebar)
        return m_rebarBandsSaved;
    std::wstring out;
    const int count = static_cast<int>(SendMessageW(m_rebar, RB_GETBANDCOUNT, 0, 0));
    for (int i = 0; i < count; ++i) {
        REBARBANDINFOW band{};
        band.cbSize = sizeof(band);
        band.fMask = RBBIM_STYLE | RBBIM_ID | RBBIM_SIZE;
        if (!SendMessageW(m_rebar, RB_GETBANDINFOW, static_cast<WPARAM>(i),
                          reinterpret_cast<LPARAM>(&band)))
            continue;
        wchar_t buf[48];
        swprintf_s(buf, L"%u,%u,%u;", band.wID, band.cx,
                   (band.fStyle & RBBS_BREAK) ? 1u : 0u);
        out += buf;
    }
    return out;
}

void MainWindow::ApplyRebarLayout(const std::wstring& layout) {
    if (!m_rebar || layout.empty())
        return;
    struct Entry {
        UINT id, cx, brk;
    };
    std::vector<Entry> entries;
    size_t pos = 0;
    while (pos < layout.size()) {
        size_t end = layout.find(L';', pos);
        if (end == std::wstring::npos)
            end = layout.size();
        Entry e{};
        if (swscanf_s(layout.c_str() + pos, L"%u,%u,%u", &e.id, &e.cx, &e.brk) == 3)
            entries.push_back(e);
        pos = end + 1;
    }
    // Only a complete, duplicate-free description of the existing bands is
    // applied; anything else (older builds, hand edits) keeps the defaults.
    const int count = static_cast<int>(SendMessageW(m_rebar, RB_GETBANDCOUNT, 0, 0));
    if (static_cast<int>(entries.size()) != count)
        return;
    UINT seen = 0;
    for (const Entry& e : entries) {
        if (SendMessageW(m_rebar, RB_IDTOINDEX, e.id, 0) == -1 || (seen & (1u << e.id)))
            return;
        seen |= 1u << e.id;
    }
    // Styles and widths first, positions LAST: writing RBBS_FIXEDSIZE can
    // make the rebar relocate that band (see SetRebarLocked), which would
    // undo any move already performed. Bands are found by id at write time
    // because a relocation shifts the indices mid-pass.
    for (int target = 0; target < count; ++target) {
        const Entry& e = entries[target];
        const LRESULT index = SendMessageW(m_rebar, RB_IDTOINDEX, e.id, 0);
        if (index == -1)
            continue;
        REBARBANDINFOW band{};
        band.cbSize = sizeof(band);
        band.fMask = RBBIM_STYLE;
        if (!SendMessageW(m_rebar, RB_GETBANDINFOW, static_cast<WPARAM>(index),
                          reinterpret_cast<LPARAM>(&band)))
            continue;
        // The first visual band never carries a break: a stray one would
        // waste an empty leading row.
        if (e.brk != 0 && target > 0)
            band.fStyle |= RBBS_BREAK;
        else
            band.fStyle &= ~RBBS_BREAK;
        band.fMask = RBBIM_STYLE | RBBIM_SIZE;
        band.cx = e.cx;
        SendMessageW(m_rebar, RB_SETBANDINFOW, static_cast<WPARAM>(index),
                     reinterpret_cast<LPARAM>(&band));
    }
    for (int target = 0; target < count; ++target) {
        const LRESULT current = SendMessageW(m_rebar, RB_IDTOINDEX, entries[target].id, 0);
        if (current != -1 && current != target)
            SendMessageW(m_rebar, RB_MOVEBAND, static_cast<WPARAM>(current),
                         static_cast<LPARAM>(target));
    }
}

void MainWindow::ShowRebarContextMenu(POINT screenPt) {
    // IE-style bar context menu: the toolbar toggle plus the lock. No
    // TPM_RETURNCMD: the picked item posts an ordinary WM_COMMAND.
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | (m_toolbarVisible ? MF_CHECKED : 0u), IDC_TOGGLE_TOOLBAR,
                Str(StrId::MenuToolbar));
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    // IE's toolbar text options, verbatim (its Customize dialog's "Text
    // options" values), as a radio group.
    AppendMenuW(menu, MF_STRING, IDC_TOOLBAR_TEXT_BELOW, Str(StrId::MenuToolbarTextBelow));
    AppendMenuW(menu, MF_STRING, IDC_TOOLBAR_TEXT_RIGHT, Str(StrId::MenuToolbarTextRight));
    AppendMenuW(menu, MF_STRING, IDC_TOOLBAR_TEXT_NONE, Str(StrId::MenuToolbarTextNone));
    const UINT textSel = m_toolbarText == 1   ? IDC_TOOLBAR_TEXT_BELOW
                         : m_toolbarText == 2 ? IDC_TOOLBAR_TEXT_RIGHT
                                              : IDC_TOOLBAR_TEXT_NONE;
    CheckMenuRadioItem(menu, IDC_TOOLBAR_TEXT_BELOW, IDC_TOOLBAR_TEXT_NONE, textSel,
                       MF_BYCOMMAND);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (m_rebarLocked ? MF_CHECKED : 0u), IDC_LOCK_TOOLBARS,
                Str(StrId::MenuLockToolbars));
    TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON, screenPt.x, screenPt.y,
                   0, m_hwnd, nullptr);
    DestroyMenu(menu);
}

void MainWindow::ShowChevronMenu(const NMREBARCHEVRON* nm) {
    if (!m_toolbar)
        return;
    // Overflow popup listing the command toolbar's actions (tooltip strings
    // double as labels); checked state mirrors the buttons.
    HMENU menu = CreatePopupMenu();
    const int count = static_cast<int>(SendMessageW(m_toolbar, TB_BUTTONCOUNT, 0, 0));
    for (int i = 0; i < count; ++i) {
        TBBUTTON b{};
        if (!SendMessageW(m_toolbar, TB_GETBUTTON, i, reinterpret_cast<LPARAM>(&b)))
            continue;
        if (b.fsStyle & BTNS_SEP) {
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            continue;
        }
        UINT flags = MF_STRING;
        if (SendMessageW(m_toolbar, TB_ISBUTTONCHECKED, static_cast<WPARAM>(b.idCommand), 0))
            flags |= MF_CHECKED;
        AppendMenuW(menu, flags, static_cast<UINT_PTR>(b.idCommand),
                    Str(CommandTipId(static_cast<UINT>(b.idCommand))));
    }
    RECT rc = nm->rc;
    MapWindowPoints(m_rebar, nullptr, reinterpret_cast<LPPOINT>(&rc), 2);
    TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_TOPALIGN, rc.left, rc.bottom, 0, m_hwnd, nullptr);
    DestroyMenu(menu);
}

StrId MainWindow::CommandTipId(UINT id) {
    // Doubles as the chevron overflow labels; keep every toolbar id covered.
    switch (id) {
    case IDC_OPEN_LEFT:
        return StrId::TipOpenLeft;
    case IDC_OPEN_RIGHT:
        return StrId::TipOpenRight;
    case IDC_TOGGLE_SCROLL_SYNC:
        return StrId::TipScrollSync;
    case IDC_TOGGLE_ZOOM_SYNC:
        return StrId::TipZoomSync;
    case IDC_ZOOM_ACTUAL:
        return StrId::TipActualSize;
    case IDC_FIT_WIDTH:
        return StrId::TipFitWidth;
    case IDC_FIT_PAGE:
        return StrId::TipFitPage;
    case IDC_SCROLL_CONTINUOUS:
        return StrId::TipScrollContinuous;
    case IDC_SCROLL_PAGED:
        return StrId::TipScrollPaged;
    case IDC_FIND_SHOW:
        return StrId::TipFind;
    case IDC_TOGGLE_OUTLINE:
        return StrId::TipOutline;
    case IDC_ADD_SYNC_POINT:
        return StrId::TipAddSyncPoint;
    case IDC_SYNC_FROM_BOOKMARKS:
        return StrId::TipSyncFromBookmarks;
    case IDC_SYNC_POINTS:
        return StrId::TipSyncPoints;
    case IDC_CLEAR_SYNC_POINTS:
        return StrId::TipClearSyncPoints;
    case IDC_TOGGLE_ALIGNMENT_GAPS:
        return StrId::TipAlignmentGaps;
    case IDC_SWAP_PANES:
        return StrId::TipSwapPanes;
    default:
        return StrId::TipFullScreen;
    }
}

void MainWindow::UpdatePageBox() {
    if (!m_pageBox || GetFocus() == m_pageBox)
        return; // never fight the user's caret
    PaneWindow* pane = m_activePane ? m_activePane : m_left.get();
    std::wstring text;
    if (pane && pane->HasDocument()) {
        const int count = pane->PageCount();
        const int page = std::clamp(static_cast<int>(pane->SyncPosition()), 0, count - 1);
        const std::wstring& label = pane->PageLabel(page);
        text = label.empty() ? std::to_wstring(page + 1) : label;
    }
    wchar_t current[64] = L"";
    GetWindowTextW(m_pageBox, current, ARRAYSIZE(current));
    if (text != current)
        SetWindowTextW(m_pageBox, text.c_str());
}

bool MainWindow::GotoFromText(const std::wstring& text) {
    PaneWindow* pane = m_activePane ? m_activePane : m_left.get();
    if (!pane || !pane->HasDocument() || text.empty())
        return false;
    // Label-first, deliberately: in a document labeled i, ii, 1, 2 the input
    // "2" goes to the page LABELED 2 (ordinal 4), not to ordinal page 2.
    int page = pane->FindPageByLabel(text);
    if (page < 0) {
        wchar_t* end = nullptr;
        const long n = wcstol(text.c_str(), &end, 10);
        if (end && *end == L'\0' && n >= 1 && n <= pane->PageCount())
            page = static_cast<int>(n) - 1;
    }
    if (page < 0)
        return false;
    pane->GotoPage(page);
    return true;
}

void MainWindow::ShowGotoPageDialog() {
    PaneWindow* pane = m_activePane ? m_activePane : m_left.get();
    if (!pane || !pane->HasDocument())
        return;
    constexpr WORD kGotoEditId = 2201;
    DialogTemplate dlg(Str(StrId::GotoTitle), 190, 62);
    dlg.AddControl(DialogTemplate::kStatic, SS_LEFT, 0, 7, 7, 176, 10, 0xFFFF,
                   Str(StrId::GotoPrompt));
    dlg.AddControl(DialogTemplate::kEdit, ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, 0, 7, 20,
                   176, 13, kGotoEditId, L"");
    dlg.AddControl(DialogTemplate::kButton, BS_DEFPUSHBUTTON | WS_TABSTOP, 0, 79, 41, 50, 14,
                   IDOK, Str(StrId::DlgOk));
    dlg.AddControl(DialogTemplate::kButton, BS_PUSHBUTTON | WS_TABSTOP, 0, 133, 41, 50, 14,
                   IDCANCEL, Str(StrId::DlgCancel));
    const HINSTANCE hinst =
        reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(m_hwnd, GWLP_HINSTANCE));
    DialogBoxIndirectParamW(hinst, dlg.Data(), m_hwnd, GotoDlgProc,
                            reinterpret_cast<LPARAM>(this));
}

INT_PTR CALLBACK MainWindow::GotoDlgProc(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    constexpr int kGotoEditId = 2201;
    auto* self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(dlg, DWLP_USER));
    switch (msg) {
    case WM_INITDIALOG: {
        SetWindowLongPtrW(dlg, DWLP_USER, lParam);
        self = reinterpret_cast<MainWindow*>(lParam);
        // Prefill with the current short form (label if any, else ordinal).
        PaneWindow* pane = self->m_activePane ? self->m_activePane : self->m_left.get();
        if (pane && pane->HasDocument()) {
            const int count = pane->PageCount();
            const int page = std::clamp(static_cast<int>(pane->SyncPosition()), 0, count - 1);
            const std::wstring& label = pane->PageLabel(page);
            const std::wstring prefill =
                label.empty() ? std::to_wstring(page + 1) : label;
            SetDlgItemTextW(dlg, kGotoEditId, prefill.c_str());
        }
        SendDlgItemMessageW(dlg, kGotoEditId, EM_SETSEL, 0, static_cast<LPARAM>(-1));
        SetFocus(GetDlgItem(dlg, kGotoEditId));
        return FALSE; // focus set manually
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDOK: {
            wchar_t text[64] = L"";
            GetDlgItemTextW(dlg, kGotoEditId, text, ARRAYSIZE(text));
            if (self && self->GotoFromText(text)) {
                EndDialog(dlg, 1);
            } else {
                MessageBeep(MB_ICONWARNING);
                SendDlgItemMessageW(dlg, kGotoEditId, EM_SETSEL, 0, static_cast<LPARAM>(-1));
                SetFocus(GetDlgItem(dlg, kGotoEditId));
            }
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(dlg, 0);
            return TRUE;
        default:
            break;
        }
        break;
    default:
        break;
    }
    return FALSE;
}

void MainWindow::ShowOptionsDialog() {
    DialogTemplate dlg(Str(StrId::OptTitle), 260, 288);
    dlg.AddControl(DialogTemplate::kButton, BS_AUTOCHECKBOX | WS_TABSTOP, 0, 7, 7, 246, 10,
                   kOptRestoreId, Str(StrId::OptRestoreSession));
    dlg.AddControl(DialogTemplate::kButton, BS_GROUPBOX, 0, 7, 22, 246, 74, 0xFFFF,
                   Str(StrId::OptDefaultsGroup));
    dlg.AddControl(DialogTemplate::kStatic, SS_LEFT, 0, 15, 37, 90, 10, 0xFFFF,
                   Str(StrId::OptDefScrollMode));
    dlg.AddControl(DialogTemplate::kComboBox, CBS_DROPDOWNLIST | WS_TABSTOP, 0, 110, 35, 138,
                   60, kOptScrollModeId, L"");
    dlg.AddControl(DialogTemplate::kStatic, SS_LEFT, 0, 15, 53, 90, 10, 0xFFFF,
                   Str(StrId::OptDefZoomMode));
    dlg.AddControl(DialogTemplate::kComboBox, CBS_DROPDOWNLIST | WS_TABSTOP, 0, 110, 51, 138,
                   60, kOptZoomModeId, L"");
    dlg.AddControl(DialogTemplate::kButton, BS_AUTOCHECKBOX | WS_TABSTOP, 0, 15, 68, 230, 10,
                   kOptScrollSyncId, Str(StrId::OptDefScrollSync));
    dlg.AddControl(DialogTemplate::kButton, BS_AUTOCHECKBOX | WS_TABSTOP, 0, 15, 80, 230, 10,
                   kOptZoomSyncId, Str(StrId::OptDefZoomSync));
    dlg.AddControl(DialogTemplate::kStatic, SS_LEFT, 0, 7, 102, 246, 10, 0xFFFF,
                   Str(StrId::OptSynctexInverse));
    dlg.AddControl(DialogTemplate::kEdit, ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, 0, 7, 113,
                   246, 13, kOptSynctexId, L"");
    dlg.AddControl(DialogTemplate::kButton, BS_AUTOCHECKBOX | BS_MULTILINE | BS_TOP | WS_TABSTOP,
                   0, 7, 131, 246, 18, kOptShellId, Str(StrId::OptShellIntegration));
    dlg.AddControl(DialogTemplate::kButton, BS_AUTOCHECKBOX | WS_TABSTOP, 0, 7, 152, 246, 10,
                   kOptAnchorsId, Str(StrId::OptShowAnchors));
    dlg.AddControl(DialogTemplate::kButton, BS_AUTOCHECKBOX | WS_TABSTOP, 0, 7, 164, 246, 10,
                   kOptTicksId, Str(StrId::OptShowTicks));
    dlg.AddControl(DialogTemplate::kButton, BS_AUTOCHECKBOX | WS_TABSTOP, 0, 7, 176, 246, 10,
                   kOptFsToolbarId, Str(StrId::OptFsToolbar));
    dlg.AddControl(DialogTemplate::kButton, BS_AUTOCHECKBOX | WS_TABSTOP, 0, 7, 188, 246, 10,
                   kOptFsStatusId, Str(StrId::OptFsStatus));
    dlg.AddControl(DialogTemplate::kButton, BS_AUTOCHECKBOX | WS_TABSTOP, 0, 7, 200, 246, 10,
                   kOptHeaderId, Str(StrId::OptShowHeader));
    dlg.AddControl(DialogTemplate::kButton, BS_AUTOCHECKBOX | WS_TABSTOP, 0, 15, 212, 238, 10,
                   kOptHeaderPathId, Str(StrId::OptHeaderShowPath));
    dlg.AddControl(DialogTemplate::kStatic, SS_LEFT, 0, 7, 232, 188, 10, 0xFFFF,
                   Str(StrId::OptWheelLines));
    dlg.AddControl(DialogTemplate::kEdit, ES_NUMBER | ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP,
                   0, 200, 230, 48, 13, kOptWheelId, L"");
    // 138 wide (up to the OK button): the German label needs more than the
    // 120 the English one suggests; keep new translations within ~130 DLU.
    dlg.AddControl(DialogTemplate::kButton, BS_PUSHBUTTON | WS_TABSTOP, 0, 7, 267, 138, 14,
                   kOptClearMruId, Str(StrId::OptClearRecent));
    dlg.AddControl(DialogTemplate::kButton, BS_DEFPUSHBUTTON | WS_TABSTOP, 0, 149, 267, 50, 14,
                   IDOK, Str(StrId::DlgOk));
    dlg.AddControl(DialogTemplate::kButton, BS_PUSHBUTTON | WS_TABSTOP, 0, 203, 267, 50, 14,
                   IDCANCEL, Str(StrId::DlgCancel));
    OptionsDialogState state{this, false};
    const HINSTANCE hinst =
        reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(m_hwnd, GWLP_HINSTANCE));
    DialogBoxIndirectParamW(hinst, dlg.Data(), m_hwnd, OptionsDlgProc,
                            reinterpret_cast<LPARAM>(&state));
}

INT_PTR CALLBACK MainWindow::OptionsDlgProc(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<OptionsDialogState*>(GetWindowLongPtrW(dlg, DWLP_USER));
    switch (msg) {
    case WM_INITDIALOG: {
        SetWindowLongPtrW(dlg, DWLP_USER, lParam);
        state = reinterpret_cast<OptionsDialogState*>(lParam);
        MainWindow* self = state->self;
        CheckDlgButton(dlg, kOptRestoreId,
                       self->m_restoreSession ? BST_CHECKED : BST_UNCHECKED);
        const HWND scrollCombo = GetDlgItem(dlg, kOptScrollModeId);
        SendMessageW(scrollCombo, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(Str(StrId::OptScrollContinuous)));
        SendMessageW(scrollCombo, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(Str(StrId::OptScrollPaged)));
        SendMessageW(scrollCombo, CB_SETCURSEL,
                     self->m_defaults.scrollMode == PaneWindow::ScrollMode::Paged ? 1 : 0, 0);
        const HWND zoomCombo = GetDlgItem(dlg, kOptZoomModeId);
        SendMessageW(zoomCombo, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(Str(StrId::OptZoomActual)));
        SendMessageW(zoomCombo, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(Str(StrId::OptZoomFitWidth)));
        SendMessageW(zoomCombo, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(Str(StrId::OptZoomFitPage)));
        SendMessageW(zoomCombo, CB_SETCURSEL,
                     static_cast<WPARAM>(self->m_defaults.zoomMode), 0);
        CheckDlgButton(dlg, kOptScrollSyncId,
                       self->m_defaults.scrollSync ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dlg, kOptZoomSyncId,
                       self->m_defaults.zoomSync ? BST_CHECKED : BST_UNCHECKED);
        SetDlgItemTextW(dlg, kOptSynctexId, self->m_synctexInverse.c_str());
        CheckDlgButton(dlg, kOptShellId,
                       ShellIntegration::IsRegistered() ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dlg, kOptAnchorsId, self->m_showAnchors ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dlg, kOptTicksId, self->m_showTicks ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dlg, kOptFsToolbarId,
                       self->m_fsShowToolbar ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dlg, kOptFsStatusId, self->m_fsShowStatus ? BST_CHECKED : BST_UNCHECKED);
        SetDlgItemInt(dlg, kOptWheelId, static_cast<UINT>(self->m_wheelLines), FALSE);
        CheckDlgButton(dlg, kOptHeaderId, self->m_showHeader ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(dlg, kOptHeaderPathId,
                       self->m_headerShowPath ? BST_CHECKED : BST_UNCHECKED);
        EnableWindow(GetDlgItem(dlg, kOptHeaderPathId), self->m_showHeader);
        return TRUE;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case kOptHeaderId:
            // The path toggle is subordinate: it only applies when the header is on.
            EnableWindow(GetDlgItem(dlg, kOptHeaderPathId),
                         IsDlgButtonChecked(dlg, kOptHeaderId) == BST_CHECKED);
            return TRUE;
        case kOptClearMruId:
            if (state) {
                state->clearRecent = true;
                EnableWindow(GetDlgItem(dlg, kOptClearMruId), FALSE);
            }
            return TRUE;
        case IDOK: {
            if (!state)
                return TRUE;
            MainWindow* self = state->self;
            self->m_restoreSession = IsDlgButtonChecked(dlg, kOptRestoreId) == BST_CHECKED;
            self->m_defaults.scrollMode =
                SendDlgItemMessageW(dlg, kOptScrollModeId, CB_GETCURSEL, 0, 0) == 1
                    ? PaneWindow::ScrollMode::Paged
                    : PaneWindow::ScrollMode::Continuous;
            const int zoomSel = std::clamp(
                static_cast<int>(SendDlgItemMessageW(dlg, kOptZoomModeId, CB_GETCURSEL, 0, 0)),
                0, 2);
            self->m_defaults.zoomMode = static_cast<PaneWindow::ZoomMode>(zoomSel);
            self->m_defaults.scrollSync =
                IsDlgButtonChecked(dlg, kOptScrollSyncId) == BST_CHECKED;
            self->m_defaults.zoomSync = IsDlgButtonChecked(dlg, kOptZoomSyncId) == BST_CHECKED;
            wchar_t synctex[512] = L"";
            GetDlgItemTextW(dlg, kOptSynctexId, synctex, ARRAYSIZE(synctex));
            self->m_synctexInverse = synctex;
            BOOL parsed = FALSE;
            const UINT lines = GetDlgItemInt(dlg, kOptWheelId, &parsed, FALSE);
            self->m_wheelLines = parsed ? static_cast<int>(std::min(lines, 100u)) : 0;
            // Live pushes: fresh documents and the very next wheel tick see
            // the new values; persistence happens at SaveSession like every
            // other setting.
            self->m_left->SetDefaultZoomMode(self->m_defaults.zoomMode);
            self->m_right->SetDefaultZoomMode(self->m_defaults.zoomMode);
            self->m_left->SetWheelLinesOverride(self->m_wheelLines);
            self->m_right->SetWheelLinesOverride(self->m_wheelLines);
            self->m_showAnchors = IsDlgButtonChecked(dlg, kOptAnchorsId) == BST_CHECKED;
            self->m_showTicks = IsDlgButtonChecked(dlg, kOptTicksId) == BST_CHECKED;
            self->m_left->SetMarkerVisibility(self->m_showAnchors, self->m_showTicks);
            self->m_right->SetMarkerVisibility(self->m_showAnchors, self->m_showTicks);
            self->m_showHeader = IsDlgButtonChecked(dlg, kOptHeaderId) == BST_CHECKED;
            self->m_headerShowPath = IsDlgButtonChecked(dlg, kOptHeaderPathId) == BST_CHECKED;
            self->m_left->SetHeaderOptions(self->m_showHeader, self->m_headerShowPath);
            self->m_right->SetHeaderOptions(self->m_showHeader, self->m_headerShowPath);
            self->m_fsShowToolbar = IsDlgButtonChecked(dlg, kOptFsToolbarId) == BST_CHECKED;
            self->m_fsShowStatus = IsDlgButtonChecked(dlg, kOptFsStatusId) == BST_CHECKED;
            if (self->m_fullscreen)
                self->Layout(); // apply the full-screen chrome choice live
            const bool wantShell = IsDlgButtonChecked(dlg, kOptShellId) == BST_CHECKED;
            if (wantShell != ShellIntegration::IsRegistered()) {
                const bool applied = wantShell ? ShellIntegration::Register()
                                               : ShellIntegration::Unregister();
                if (!applied)
                    MessageBeep(MB_ICONWARNING);
            }
            if (state->clearRecent) {
                self->m_mruFiles.clear();
                self->m_mruPairs.clear();
                self->RebuildMruMenus();
            }
            EndDialog(dlg, 1);
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(dlg, 0);
            return TRUE;
        default:
            break;
        }
        break;
    default:
        break;
    }
    return FALSE;
}

LRESULT CALLBACK MainWindow::PageBoxProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                         UINT_PTR /*id*/, DWORD_PTR data) {
    auto* self = reinterpret_cast<MainWindow*>(data);
    switch (msg) {
    case WM_KEYDOWN:
        if (wParam == VK_RETURN) {
            wchar_t text[64] = L"";
            GetWindowTextW(hwnd, text, ARRAYSIZE(text));
            if (self->GotoFromText(text)) {
                PaneWindow* pane = self->m_activePane ? self->m_activePane
                                                      : self->m_left.get();
                SetFocus(pane->Hwnd());
                self->UpdatePageBox();
            } else {
                MessageBeep(MB_ICONWARNING);
                SendMessageW(hwnd, EM_SETSEL, 0, static_cast<LPARAM>(-1));
            }
            return 0;
        }
        if (wParam == VK_ESCAPE) {
            PaneWindow* pane = self->m_activePane ? self->m_activePane : self->m_left.get();
            SetFocus(pane->Hwnd());
            self->UpdatePageBox(); // restore the live page text
            return 0;
        }
        break;
    case WM_CHAR:
        if (wParam == L'\r' || wParam == 0x1B)
            return 0; // the WM_KEYDOWN branch handled it: swallow the beep
        break;
    default:
        break;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

void MainWindow::RebuildToolbarIcons() {
    if (!m_toolbar)
        return;
    const int glyphPx = MulDiv(16, m_dpi, 96);
    HIMAGELIST icons =
        CreateGlyphImageList(std::span<const GlyphSpec>(kToolbarGlyphs), glyphPx, glyphPx,
                             GetSysColor(COLOR_BTNTEXT));
    if (!icons)
        return;
    SendMessageW(m_toolbar, TB_SETIMAGELIST, 0, reinterpret_cast<LPARAM>(icons));
    if (m_toolbarIcons)
        ImageList_Destroy(m_toolbarIcons);
    m_toolbarIcons = icons;
    if (m_toolbarText == 0) {
        // Icon-only: pin the compact square. With labels the toolbar sizes
        // its own buttons (BTNS_AUTOSIZE widths, text-row height).
        const int btn = MulDiv(24, m_dpi, 96);
        SendMessageW(m_toolbar, TB_SETBUTTONSIZE, 0, MAKELPARAM(btn, btn));
    }
    SendMessageW(m_toolbar, TB_AUTOSIZE, 0, 0);
}

namespace {
void PopulateSyncPointsList(HWND list, const std::vector<SyncPoint>& points) {
    ListView_DeleteAllItems(list);
    for (size_t i = 0; i < points.size(); ++i) {
        const SyncPoint& p = points[i];
        const int row = static_cast<int>(i);
        const std::wstring ordinal = std::to_wstring(i + 1);
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = row;
        item.pszText = const_cast<wchar_t*>(ordinal.c_str());
        ListView_InsertItem(list, &item);
        ListView_SetItemText(list, row, 1, const_cast<wchar_t*>(p.label.c_str()));
        const std::wstring pages = L"p" + std::to_wstring(p.left + 1) + L" \x2194 p" +
                                   std::to_wstring(p.right + 1);
        ListView_SetItemText(list, row, 2, const_cast<wchar_t*>(pages.c_str()));
        ListView_SetItemText(list, row, 3,
                             const_cast<wchar_t*>(Str(p.manual ? StrId::SyncPtOriginManual
                                                               : StrId::SyncPtOriginAuto)));
    }
}
} // namespace

void MainWindow::ShowSyncPointsDialog() {
    DialogTemplate dlg(Str(StrId::SyncPtsDlgTitle), 300, 182);
    dlg.AddControl(WC_LISTVIEWW,
                   LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_BORDER | WS_TABSTOP, 0, 7,
                   7, 286, 146, kSyncPtsListId, L"");
    // Remove starts disabled: it follows the listbox selection.
    dlg.AddControl(DialogTemplate::kButton, BS_PUSHBUTTON | WS_TABSTOP | WS_DISABLED, 0, 7, 161,
                   60, 14, kSyncPtsRemoveId, Str(StrId::SyncPtsDlgRemove));
    dlg.AddControl(DialogTemplate::kButton, BS_PUSHBUTTON | WS_TABSTOP, 0, 71, 161, 60, 14,
                   kSyncPtsClearId, Str(StrId::SyncPtsDlgClear));
    // Close = IDCANCEL (Esc and the X work): every action applies
    // immediately, there is nothing to confirm.
    dlg.AddControl(DialogTemplate::kButton, BS_DEFPUSHBUTTON | WS_TABSTOP, 0, 243, 161, 50, 14,
                   IDCANCEL, Str(StrId::DlgClose));
    const HINSTANCE hinst =
        reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(m_hwnd, GWLP_HINSTANCE));
    DialogBoxIndirectParamW(hinst, dlg.Data(), m_hwnd, SyncPointsDlgProc,
                            reinterpret_cast<LPARAM>(this));
    m_syncPtsDlg = nullptr;
    // The map may have been emptied from inside the dialog.
    UpdateCommandUi();
    UpdateStatusBar();
}

INT_PTR CALLBACK MainWindow::SyncPointsDlgProc(HWND dlg, UINT msg, WPARAM wParam,
                                               LPARAM lParam) {
    auto* self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(dlg, DWLP_USER));
    switch (msg) {
    case WM_INITDIALOG: {
        SetWindowLongPtrW(dlg, DWLP_USER, lParam);
        self = reinterpret_cast<MainWindow*>(lParam);
        const HWND list = GetDlgItem(dlg, kSyncPtsListId);
        ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT);
        const int dpi = static_cast<int>(GetDpiForWindow(dlg));
        const auto addCol = [list, dpi](int idx, int widthDip, StrId text) {
            LVCOLUMNW col{};
            col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
            col.cx = MulDiv(widthDip, dpi, 96);
            col.pszText = const_cast<wchar_t*>(Str(text));
            col.iSubItem = idx;
            ListView_InsertColumn(list, idx, &col);
        };
        addCol(0, 34, StrId::SyncPtsColIndex);
        addCol(1, 96, StrId::SyncPtsColNumbering);
        addCol(2, 120, StrId::SyncPtsColPages);
        addCol(3, 82, StrId::SyncPtsColOrigin);
        PopulateSyncPointsList(list, self->m_sync->Points());
        self->m_syncPtsDlg = dlg; // reset by ShowSyncPointsDialog on return
        return TRUE;
    }
    case kMsgSyncPtsRefresh: {
        // The map changed outside the dialog (auto-reload regen, swap settle):
        // repopulate so row indexes match the live map again. Idempotent for
        // the dialog's own mutations, which posted this after their inline
        // repopulate; keep the selection by (clamped) index.
        if (!self)
            return TRUE;
        const HWND list = GetDlgItem(dlg, kSyncPtsListId);
        const int sel = ListView_GetNextItem(list, -1, LVNI_SELECTED);
        PopulateSyncPointsList(list, self->m_sync->Points());
        const int n = static_cast<int>(self->m_sync->Points().size());
        if (n > 0 && sel >= 0) {
            const int keep = std::min(sel, n - 1);
            ListView_SetItemState(list, keep, LVIS_SELECTED | LVIS_FOCUSED,
                                  LVIS_SELECTED | LVIS_FOCUSED);
        }
        if (n == 0 && GetFocus() != GetDlgItem(dlg, IDCANCEL))
            SetFocus(GetDlgItem(dlg, IDCANCEL)); // don't strand focus on a disabling button
        EnableWindow(GetDlgItem(dlg, kSyncPtsRemoveId),
                     n > 0 && ListView_GetNextItem(list, -1, LVNI_SELECTED) != -1);
        EnableWindow(GetDlgItem(dlg, kSyncPtsClearId), n > 0);
        return TRUE;
    }
    case WM_NOTIFY: {
        const auto* hdr = reinterpret_cast<const NMHDR*>(lParam);
        if (hdr->idFrom == kSyncPtsListId && hdr->code == LVN_ITEMCHANGED) {
            const HWND list = GetDlgItem(dlg, kSyncPtsListId);
            EnableWindow(GetDlgItem(dlg, kSyncPtsRemoveId),
                         ListView_GetNextItem(list, -1, LVNI_SELECTED) != -1);
        }
        break;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case kSyncPtsRemoveId: {
            const HWND list = GetDlgItem(dlg, kSyncPtsListId);
            const int sel = ListView_GetNextItem(list, -1, LVNI_SELECTED);
            if (self && sel >= 0) {
                self->m_sync->RemovePoint(static_cast<size_t>(sel));
                const int n = static_cast<int>(self->m_sync->Points().size());
                PopulateSyncPointsList(list, self->m_sync->Points());
                if (n > 0) {
                    const int keep = std::min(sel, n - 1);
                    ListView_SetItemState(list, keep, LVIS_SELECTED | LVIS_FOCUSED,
                                          LVIS_SELECTED | LVIS_FOCUSED);
                } else {
                    // Disabling the focused button strands keyboard focus.
                    SetFocus(GetDlgItem(dlg, IDCANCEL));
                    EnableWindow(GetDlgItem(dlg, kSyncPtsRemoveId), FALSE);
                    EnableWindow(GetDlgItem(dlg, kSyncPtsClearId), FALSE);
                }
                self->RememberSyncPoints();
                self->UpdateCommandUi();
                self->UpdateStatusBar();
            }
            return TRUE;
        }
        case kSyncPtsClearId:
            if (self) {
                self->m_sync->ClearPoints();
                PopulateSyncPointsList(GetDlgItem(dlg, kSyncPtsListId), self->m_sync->Points());
                SetFocus(GetDlgItem(dlg, IDCANCEL)); // Clear had focus and gets disabled
                EnableWindow(GetDlgItem(dlg, kSyncPtsRemoveId), FALSE);
                EnableWindow(GetDlgItem(dlg, kSyncPtsClearId), FALSE);
                self->RememberSyncPoints(); // an emptied map forgets the pair
                self->UpdateCommandUi();
                self->UpdateStatusBar();
            }
            return TRUE;
        case IDCANCEL:
            EndDialog(dlg, 0);
            return TRUE;
        default:
            break;
        }
        break;
    default:
        break;
    }
    return FALSE;
}

void MainWindow::UpdateCommandUi() {
    if (!m_sync)
        return;
    const bool bothDocs = m_left->HasDocument() && m_right->HasDocument();
    const bool canGenerate =
        bothDocs && !m_left->Outline().empty() && !m_right->Outline().empty();
    const bool hasPoints = m_sync->HasPoints();
    if (m_menu) {
        const auto check = [this](UINT id, bool on) {
            CheckMenuItem(m_menu, id, MF_BYCOMMAND | (on ? MF_CHECKED : MF_UNCHECKED));
        };
        check(IDC_TOGGLE_TOOLBAR, m_toolbarVisible);
        check(IDC_TOGGLE_STATUSBAR, m_statusVisible);
        check(IDC_TOGGLE_OUTLINE, m_outlineVisible);
        check(IDC_LOCK_TOOLBARS, m_rebarLocked);
        check(IDC_FULLSCREEN, m_fullscreen);
        check(IDC_TOGGLE_SCROLL_SYNC, m_sync->ScrollSync());
        check(IDC_TOGGLE_ZOOM_SYNC, m_sync->ZoomSync());
        check(IDC_TOGGLE_ALIGNMENT_GAPS, m_showAlignmentGaps);
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
        CheckMenuRadioItem(m_menu, IDC_LANG_ENGLISH, IDC_LANG_SWEDISH,
                           IDC_LANG_ENGLISH + static_cast<UINT>(UiLanguage()), MF_BYCOMMAND);
        const auto enable = [this](UINT id, bool on) {
            EnableMenuItem(m_menu, id, MF_BYCOMMAND | (on ? MF_ENABLED : MF_GRAYED));
        };
        enable(IDC_ADD_SYNC_POINT, bothDocs);
        enable(IDC_SYNC_FROM_BOOKMARKS, canGenerate);
        enable(IDC_SYNC_POINTS, hasPoints);
        enable(IDC_CLEAR_SYNC_POINTS, hasPoints);
    }
    if (m_toolbar) {
        const auto press = [this](WORD id, bool on) {
            SendMessageW(m_toolbar, TB_CHECKBUTTON, id, MAKELPARAM(on ? TRUE : FALSE, 0));
        };
        const PaneWindow::ZoomMode mode = FocusedPane()->GetZoomMode();
        press(IDC_TOGGLE_SCROLL_SYNC, m_sync->ScrollSync());
        press(IDC_TOGGLE_ZOOM_SYNC, m_sync->ZoomSync());
        press(IDC_TOGGLE_ALIGNMENT_GAPS, m_showAlignmentGaps);
        press(IDC_TOGGLE_OUTLINE, m_outlineVisible);
        press(IDC_FIT_WIDTH, mode == PaneWindow::ZoomMode::FitWidth);
        press(IDC_FIT_PAGE, mode == PaneWindow::ZoomMode::FitPage);
        press(IDC_SCROLL_CONTINUOUS, m_scrollMode == PaneWindow::ScrollMode::Continuous);
        press(IDC_SCROLL_PAGED, m_scrollMode == PaneWindow::ScrollMode::Paged);
        const auto tbEnable = [this](WORD id, bool on) {
            SendMessageW(m_toolbar, TB_ENABLEBUTTON, id, MAKELPARAM(on ? TRUE : FALSE, 0));
        };
        tbEnable(IDC_ADD_SYNC_POINT, bothDocs);
        tbEnable(IDC_SYNC_FROM_BOOKMARKS, canGenerate);
        tbEnable(IDC_SYNC_POINTS, hasPoints);
        tbEnable(IDC_CLEAR_SYNC_POINTS, hasPoints);
    }
}

void MainWindow::UpdateStatusBar() {
    if (!m_status || !m_sync)
        return;
    const auto pageText = [](PaneWindow& pane, StrId prefix, StrId noDoc) {
        if (!pane.HasDocument())
            return std::wstring(Str(noDoc));
        const int count = pane.PageCount();
        const int page = std::clamp(static_cast<int>(pane.SyncPosition()), 0, count - 1);
        return Str(prefix) + pane.FormatPageText(page); // "ix (9/314)" when labeled
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
    std::wstring syncText = Str(sync);
    // The point count shows even with sync toggled off: a parked map must be
    // visible, or clearing vs. merely unlocking would look identical.
    if (m_sync->HasPoints())
        syncText += Str(StrId::StatusSyncPtsPre) + std::to_wstring(m_sync->Points().size()) +
                    Str(StrId::StatusSyncPtsPost);

    // Layout: [L page][L zoom][filler][sync centered][R page][R zoom][filler].
    const std::wstring texts[7] = {
        pageText(*m_left, StrId::StatusLeftPrefix, StrId::StatusLeftNoDoc),
        zoomText(*m_left),
        std::wstring(),
        std::move(syncText),
        pageText(*m_right, StrId::StatusRightPrefix, StrId::StatusRightNoDoc),
        zoomText(*m_right),
        std::wstring(),
    };
    for (int i = 0; i < 7; ++i) {
        if (m_statusText[i] == texts[i])
            continue; // SB_SETTEXT repaints the part even when nothing changed
        m_statusText[i] = texts[i];
        // The fillers are visual gaps, not cells: no sunken border on them.
        const WPARAM part = static_cast<WPARAM>(i) | ((i == 2 || i == 6) ? SBT_NOBORDERS : 0);
        SendMessageW(m_status, SB_SETTEXTW, part,
                     reinterpret_cast<LPARAM>(m_statusText[i].c_str()));
    }
}

void MainWindow::ShowAboutBox() {
    std::wstring text = L"PDF Side Viewer " PSV_VERSION_WSTR L"\n\n";
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

void MainWindow::SwapPanes() {
    struct Snapshot {
        bool has = false;
        std::wstring path;
        float zoom = 1.0f;
        float scrollX = 0;
        float scrollY = 0;
        PaneWindow::ZoomMode zoomMode = PaneWindow::ZoomMode::FitPage;
    };
    const auto snap = [](PaneWindow& pane) {
        return Snapshot{pane.HasPersistableDocument(), pane.DocumentPath(), pane.PersistZoom(),
                        pane.PersistScrollX(), pane.PersistScrollY(), pane.PersistZoomMode()};
    };
    const Snapshot left = snap(*m_left);
    const Snapshot right = snap(*m_right);
    if (!left.has && !right.has)
        return;
    // Park the map MIRRORED before the reopen storm: each side's
    // DocumentOpened clears the live map, and the path change cancels the
    // parked regen (the parked map replaces it). A swap-during-swap discards
    // the older park (its expected paths will not match the newer arrivals).
    m_swapMap = {};
    // HasDocument, not the snapshots' HasPersistableDocument: during an
    // in-flight open the pane's path is already the NEW file while the live
    // map still describes the dying document (it clears only at
    // DocumentOpened), and parking that map would reinstall alien
    // coordinates. Same guard makes a second swap issued mid-reopen simply
    // discard (the panes are Opening), as the parked-map contract promises.
    if (m_sync->HasPoints() && m_left->HasDocument() && m_right->HasDocument()) {
        m_swapMap.pending = true;
        m_swapMap.expectLeft = right.path;
        m_swapMap.expectRight = left.path;
        for (const SyncPoint& p : m_sync->Points())
            m_swapMap.points.push_back(SyncPoint{p.right, p.left, p.manual, p.label});
    }
    std::swap(m_fallbackLeft, m_fallbackRight);
    // The documents reload through their workers (a live Document cannot
    // change pane: the worker posts to its pane's HWND); the views ride the
    // session-restore path and the sync anchors recapture on DocumentOpened.
    // The swapped MRU pair that gets recorded is intended: the new
    // arrangement genuinely is reversed.
    if (right.has)
        m_left->OpenDocumentWithView(right.path, right.zoom, right.scrollX, right.scrollY,
                                     right.zoomMode);
    else
        m_left->CloseDocument();
    if (left.has)
        m_right->OpenDocumentWithView(left.path, left.zoom, left.scrollX, left.scrollY,
                                      left.zoomMode);
    else
        m_right->CloseDocument();
}

void MainWindow::SwitchLanguage(Lang lang) {
    if (lang == UiLanguage())
        return;
    SetUiLanguage(lang);
    HMENU old = m_menu;
    m_menu = BuildMenuBar();
    // Repoint the band BEFORE destroying the old handle it still references.
    m_menuBand.SetMenu(m_menu);
    UpdateRebarBandSizes(); // the new titles change the menu band's width
    if (old)
        DestroyMenu(old);
    if (m_toolbarText != 0)
        RebuildToolbarInBand(); // button labels live in the language tables
    UpdateTitle();
    m_left->SetPlaceholderHint(Str(StrId::PlaceholderLeft));
    m_right->SetPlaceholderHint(Str(StrId::PlaceholderRight));
    for (std::wstring& cached : m_statusText)
        cached.assign(1, L'\xFFFF'); // impossible text: force every part to rewrite
    UpdateStatusBar();
    UpdateCommandUi();
}

void MainWindow::ShowStatusMessage(StrId id) {
    ShowStatusMessage(std::wstring(Str(id)));
}

void MainWindow::ShowStatusMessage(std::wstring text) {
    if (!m_status)
        return;
    // Borrow the sync part (index 3) for a transient message; the timer
    // restores it.
    m_statusText[3] = std::move(text);
    SendMessageW(m_status, SB_SETTEXTW, 3, reinterpret_cast<LPARAM>(m_statusText[3].c_str()));
    SetTimer(m_hwnd, kStatusMsgTimer, 4000, nullptr);
}

void MainWindow::GenerateSyncPointsFromBookmarks(bool interactive) {
    if (!m_left->HasDocument() || !m_right->HasDocument())
        return; // accelerator/reload-proofing; the menu item is greyed
    std::vector<SyncPoint> candidates;
    for (const auto& [li, ri] : MatchOutlineNumberings(m_left->Outline(), m_right->Outline())) {
        const Document::OutlineItem& l = m_left->Outline()[static_cast<size_t>(li)];
        const Document::OutlineItem& r = m_right->Outline()[static_cast<size_t>(ri)];
        if (l.targetPage < 0 || r.targetPage < 0)
            continue;
        // Numbered match: the numbering key. Title match: the title itself.
        std::wstring label;
        if (const auto key = ParseOutlineNumbering(l.title))
            label = FormatOutlineNumbering(*key);
        else
            label = l.title;
        candidates.push_back(SyncPoint{l.targetPage, r.targetPage, false, std::move(label)});
    }
    if (candidates.empty()) {
        if (interactive)
            ShowStatusMessage(StrId::SyncPtsNoMatch);
        return;
    }
    const size_t n = m_sync->SetGeneratedPoints(std::move(candidates));
    // interactive = the menu command: generating a map means wanting it
    // applied, so it also turns scroll sync on and realigns at once. The
    // post-reload regeneration must do neither: a LaTeX rebuild must not
    // flip the lock nor yank the view the reload just preserved. Nothing
    // inserted (all candidates blocked by manual points) moves nothing.
    if (interactive && n > 0) {
        if (!m_sync->ScrollSync()) {
            m_sync->SetScrollSync(true);
            UpdateTitle(); // "[scroll sync]" tag, like the F7 handler
        }
        m_sync->RealignFollower(*FocusedPane());
    }
    // Persist only on the USER command: the reload path regenerates from a
    // map whose manual points were just cleared (they decay in-session by
    // design), and remembering that state would wipe the saved manual points
    // the entry deliberately keeps for the next launch. Auto-only flows are
    // covered: their entries already carry hadAuto=1.
    if (interactive)
        RememberSyncPoints();
    UpdateCommandUi();
    UpdateStatusBar();
    // Last: the realign/update traffic above rewrites the sync cell, which
    // would wipe the transient count the moment it was shown. The post-reload
    // path stays silent: it runs inside the view-changed handler whose
    // trailing UpdateStatusBar would eat the message anyway, and the
    // re-derived count is already visible in the sync cell.
    if (interactive)
        ShowStatusMessage(Str(StrId::SyncPtsGenerated) + std::to_wstring(n));
}

namespace {
// One pass over the sorted map. A segment spans the interior pages between
// consecutive points; prev = (-1,-1) seeds the pre-first segment so
// different-length preambles align from the top. Within a segment the
// interiors pair 1:1 from the segment start; the shorter side gets one gap
// slot per unmatched counterpart page, inserted just before its own point
// page, silhouetted like the page it mirrors (top-to-bottom order). The tail
// after the last point gets no gaps: free divergence, the virtual sync
// clamps. Each segment contributes max(a, b) slots to both sides, so by
// induction every point's two pages land on the same slot index.
void ComputeAlignmentGaps(const std::vector<SyncPoint>& points, const PaneWindow& left,
                          const PaneWindow& right, std::vector<PageLayout::AlignmentGap>& outLeft,
                          std::vector<PageLayout::AlignmentGap>& outRight) {
    int prevL = -1;
    int prevR = -1;
    for (const SyncPoint& p : points) {
        const int l = std::clamp(p.left, 0, left.PageCount() - 1);
        const int r = std::clamp(p.right, 0, right.PageCount() - 1);
        if (l <= prevL || r <= prevR)
            continue; // defensive only: the map invariant says impossible
        const int a = l - prevL - 1; // interior page counts
        const int b = r - prevR - 1;
        const int common = std::min(a, b);
        for (int i = common; i < a; ++i) // left surplus -> right-side gaps
            outRight.push_back({r, left.PageSizePt(prevL + 1 + i)});
        for (int i = common; i < b; ++i) // right surplus -> left-side gaps
            outLeft.push_back({l, right.PageSizePt(prevR + 1 + i)});
        prevL = l;
        prevR = r;
    }
}
} // namespace

namespace {
std::wstring FormatManualPoints(const std::vector<SyncPoint>& points) {
    // Capped well under ReadString's 2048-wchar buffer (~14 wchars per pair):
    // GetPrivateProfileString truncates SILENTLY, and a torn trailing pair
    // could restore a wrong anchor instead of a missing one.
    constexpr size_t kMaxSavedManualPoints = 100;
    std::wstring out;
    size_t saved = 0;
    for (const SyncPoint& p : points) {
        if (!p.manual)
            continue;
        if (++saved > kMaxSavedManualPoints)
            break;
        if (!out.empty())
            out.push_back(L';');
        out += std::to_wstring(p.left) + L":" + std::to_wstring(p.right);
    }
    return out;
}

std::vector<SyncPoint> ParseManualPoints(const std::wstring& text, int leftCount,
                                         int rightCount) {
    std::vector<SyncPoint> points;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find(L';', pos);
        if (end == std::wstring::npos)
            end = text.size();
        const std::wstring item = text.substr(pos, end - pos);
        pos = end + 1;
        const size_t colon = item.find(L':');
        if (colon == std::wstring::npos)
            continue;
        // Both halves must be FULLY numeric: wcstol returns 0 on garbage
        // (":", "abc:def"), which would otherwise restore a phantom {0,0}.
        wchar_t* endL = nullptr;
        const long l = wcstol(item.c_str(), &endL, 10);
        wchar_t* endR = nullptr;
        const long r = wcstol(item.c_str() + colon + 1, &endR, 10);
        if (endL != item.c_str() + colon || endR != item.c_str() + item.size())
            continue;
        // Out-of-range pairs mean the pagination changed since the save (or
        // the file was hand-edited): drop them, and enforce the map's double
        // monotonicity defensively for the same reason.
        if (l < 0 || r < 0 || l >= leftCount || r >= rightCount)
            continue;
        if (!points.empty() && (points.back().left >= static_cast<int>(l) ||
                                points.back().right >= static_cast<int>(r)))
            continue;
        points.push_back(SyncPoint{static_cast<int>(l), static_cast<int>(r), true, {}});
    }
    return points;
}
} // namespace

void MainWindow::RememberSyncPoints() {
    if (!m_sync || !m_left->HasDocument() || !m_right->HasDocument())
        return;
    const std::wstring& l = m_left->DocumentPath();
    const std::wstring& r = m_right->DocumentPath();
    std::erase_if(m_savedPoints, [&](const SavedSyncPoints& e) {
        return lstrcmpiW(e.left.c_str(), l.c_str()) == 0 &&
               lstrcmpiW(e.right.c_str(), r.c_str()) == 0;
    });
    SavedSyncPoints entry;
    entry.left = l;
    entry.right = r;
    entry.manual = FormatManualPoints(m_sync->Points());
    entry.hadAuto = m_sync->HasAutoPoints();
    // An emptied map FORGETS the pair: a deliberate clear must not resurrect
    // at the next launch.
    if (entry.manual.empty() && !entry.hadAuto)
        return;
    m_savedPoints.insert(m_savedPoints.begin(), std::move(entry));
    if (m_savedPoints.size() > kMruMaxEntries)
        m_savedPoints.resize(kMruMaxEntries);
}

void MainWindow::TryRestoreSavedPoints() {
    // Only when a pair OPENS with no map of its own: a live map (the swap's
    // reinstalled mirror, points the user already placed) always wins.
    if (!m_sync || m_sync->HasPoints() || !m_left->HasDocument() || !m_right->HasDocument())
        return;
    const std::wstring& l = m_left->DocumentPath();
    const std::wstring& r = m_right->DocumentPath();
    const auto it = std::find_if(
        m_savedPoints.begin(), m_savedPoints.end(), [&](const SavedSyncPoints& e) {
            return lstrcmpiW(e.left.c_str(), l.c_str()) == 0 &&
                   lstrcmpiW(e.right.c_str(), r.c_str()) == 0;
        });
    if (it == m_savedPoints.end())
        return;
    std::vector<SyncPoint> manual =
        ParseManualPoints(it->manual, m_left->PageCount(), m_right->PageCount());
    if (!manual.empty())
        m_sync->RestorePoints(std::move(manual));
    if (it->hadAuto)
        GenerateSyncPointsFromBookmarks(false); // merges from fresh outlines; manual wins
    UpdateCommandUi();
    UpdateStatusBar();
}

void MainWindow::ApplyAlignmentGaps() {
    if (!m_sync || !m_left || !m_right)
        return;
    std::vector<PageLayout::AlignmentGap> gapsLeft;
    std::vector<PageLayout::AlignmentGap> gapsRight;
    if (m_showAlignmentGaps && m_sync->HasPoints() && m_left->HasDocument() &&
        m_right->HasDocument())
        ComputeAlignmentGaps(m_sync->Points(), *m_left, *m_right, gapsLeft, gapsRight);
    // The rebuilds scroll the panes; their echoes must not drive the sibling
    // mid-operation (this runs inside the controller's own event handling on
    // the DocumentOpened clear). Both panes get the same fresh gap epoch:
    // virtual sync only runs between matching nonzero epochs.
    const uint64_t version = ++m_gapsEpoch;
    m_sync->ApplySilently([&] {
        m_left->SetAlignmentGaps(std::move(gapsLeft), version);
        m_right->SetAlignmentGaps(std::move(gapsRight), version);
    });
    // Markers are toggle-INDEPENDENT: the anchors and ticks show wherever a
    // map exists, gaps or not.
    std::vector<PaneWindow::SyncMarker> markersLeft;
    std::vector<PaneWindow::SyncMarker> markersRight;
    for (const SyncPoint& p : m_sync->Points()) {
        markersLeft.push_back({p.left, p.manual, p.label});
        markersRight.push_back({p.right, p.manual, p.label});
    }
    m_left->SetSyncMarkers(std::move(markersLeft));
    m_right->SetSyncMarkers(std::move(markersRight));
    if (m_syncPtsDlg)
        PostMessageW(m_syncPtsDlg, kMsgSyncPtsRefresh, 0, 0);
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

BOOL MainWindow::HandleOpenDocumentCopyData(const COPYDATASTRUCT& cds) {
    // Same validation discipline as the forward-search op: caps, exact size,
    // copy-now (the buffer dies at return), rooted paths only.
    if (cds.cbData < sizeof(OpenDocumentBlob))
        return FALSE;
    OpenDocumentBlob blob{};
    memcpy(&blob, cds.lpData, sizeof(blob));
    if (blob.side > 1 || blob.pathLen == 0 || blob.pathLen > 0x8000)
        return FALSE;
    const size_t expected =
        sizeof(OpenDocumentBlob) + static_cast<size_t>(blob.pathLen) * sizeof(wchar_t);
    if (cds.cbData != expected)
        return FALSE;
    const auto* chars = reinterpret_cast<const wchar_t*>(
        static_cast<const BYTE*>(cds.lpData) + sizeof(OpenDocumentBlob));
    std::wstring path(chars, blob.pathLen);
    const bool rooted = path.size() >= 3 && (path[1] == L':' || path.rfind(L"\\\\", 0) == 0);
    if (!rooted)
        return FALSE;
    // The sender granted AllowSetForegroundWindow; flash as the fallback cue.
    if (!SetForegroundWindow(m_hwnd)) {
        FLASHWINFO fi{sizeof(fi), m_hwnd, FLASHW_TRAY | FLASHW_TIMERNOFG, 3, 0};
        FlashWindowEx(&fi);
    }
    (blob.side != 0 ? m_right : m_left)->OpenDocument(std::move(path));
    return TRUE;
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
        // The menu lives in the rebar band; Layout's !m_fullscreen guard
        // hides the whole rebar, so no SetMenu dance is needed anymore.
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
        SetWindowPlacement(m_hwnd, &m_fsRestorePlacement); // normal AND maximized state
        SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER |
                         SWP_FRAMECHANGED);
    }
    UpdateCommandUi();
}

void MainWindow::UpdateTitle() {
    std::wstring title = L"PDF Side Viewer";
    // The toolbar (checked buttons) and the status bar (sync part) already
    // show the lock state: the title tags are the fallback cue when neither
    // is on screen.
    if (!m_toolbarVisible && !m_statusVisible) {
        if (m_sync->ScrollSync())
            title += Str(StrId::TitleScrollSyncTag);
        if (m_sync->ZoomSync())
            title += Str(StrId::TitleZoomSyncTag);
    }
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
        m_activePane = m_left.get();
        BuildRebar(hinst); // menu band + command toolbar + page box, one row
        CreateFindBar();   // after the panes: overlays must be above them
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
        UpdateRebarBandSizes();
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
            const RECT divider = OutlineDividerRect();
            if (PtInRect(&splitter, pt) || PtInRect(&divider, pt)) {
                SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
                return TRUE;
            }
        }
        break;

    case WM_LBUTTONDOWN: {
        const POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        const RECT splitter = SplitterRect();
        const RECT divider = OutlineDividerRect();
        if (PtInRect(&splitter, pt))
            m_drag = DragTarget::PaneSplitter;
        else if (PtInRect(&divider, pt))
            m_drag = DragTarget::OutlineDivider;
        if (m_drag != DragTarget::None)
            SetCapture(m_hwnd);
        return 0;
    }

    case WM_MOUSEMOVE:
        if (m_drag == DragTarget::PaneSplitter) {
            SetSplitRatioFromX(GET_X_LPARAM(lParam));
            Layout();
        } else if (m_drag == DragTarget::OutlineDivider) {
            SetOutlineWidthFromX(GET_X_LPARAM(lParam));
            Layout();
        }
        return 0;

    case WM_LBUTTONUP:
        if (m_drag != DragTarget::None)
            ReleaseCapture();
        return 0;

    case WM_CAPTURECHANGED:
        m_drag = DragTarget::None;
        return 0;

    case WM_LBUTTONDBLCLK: {
        const POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        const RECT splitter = SplitterRect();
        const RECT divider = OutlineDividerRect();
        if (PtInRect(&splitter, pt)) {
            m_splitRatio = 0.5f;
            Layout();
        } else if (PtInRect(&divider, pt)) {
            FitOutlineToContent(); // best fit: no horizontal scroll in the tree
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
            m_statusText[3].assign(1, L'\xFFFF'); // force the sync part to rewrite
            UpdateStatusBar();
            return 0;
        }
        break;

    case WM_COPYDATA: {
        // Requests from short-lived second instances (SyncTeX forward search,
        // Explorer-verb opens). The message crosses process boundaries:
        // validate every payload exactly.
        const auto* cds = reinterpret_cast<const COPYDATASTRUCT*>(lParam);
        if (!cds || !cds->lpData)
            return FALSE;
        if (cds->dwData == kCdOpenDocument)
            return HandleOpenDocumentCopyData(*cds);
        if (cds->dwData != kCdForwardSearch || cds->cbData < sizeof(ForwardSearchBlob))
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
        // The sync-point handlers re-check their preconditions: accelerators
        // fire regardless of the menu items' greyed state.
        case IDC_ADD_SYNC_POINT:
            if (m_sync->AddPointHere()) {
                RememberSyncPoints();
                UpdateCommandUi();
                UpdateStatusBar();
            }
            return 0;
        case IDC_SYNC_FROM_BOOKMARKS:
            GenerateSyncPointsFromBookmarks(true);
            return 0;
        case IDC_SYNC_POINTS:
            if (m_sync->HasPoints())
                ShowSyncPointsDialog();
            return 0;
        case IDC_CLEAR_SYNC_POINTS:
            m_sync->ClearPoints();
            RememberSyncPoints(); // an emptied map forgets the pair
            UpdateCommandUi();
            UpdateStatusBar();
            return 0;
        case IDC_TOGGLE_ALIGNMENT_GAPS:
            m_showAlignmentGaps = !m_showAlignmentGaps;
            m_sync->SetAlignmentGapsEnabled(m_showAlignmentGaps);
            ApplyAlignmentGaps();
            if (m_showAlignmentGaps && m_sync->ScrollSync() && m_sync->HasPoints())
                m_sync->RealignFollower(*FocusedPane()); // snap onto the 1:1 alignment
            UpdateCommandUi();
            return 0;
        case IDC_TOOLBAR_TEXT_BELOW:
            SetToolbarTextMode(1);
            return 0;
        case IDC_TOOLBAR_TEXT_RIGHT:
            SetToolbarTextMode(2);
            return 0;
        case IDC_TOOLBAR_TEXT_NONE:
            SetToolbarTextMode(0);
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
            UpdateTitle();
            return 0;
        case IDC_LOCK_TOOLBARS:
            SetRebarLocked(!m_rebarLocked);
            return 0;
        case IDC_TOGGLE_STATUSBAR:
            m_statusVisible = !m_statusVisible;
            Layout();
            UpdateCommandUi();
            UpdateTitle();
            return 0;
        case IDC_ZOOM_IN:
            FocusedPane()->ApplyZoomRatio(1.25f);
            UpdateCommandUi();
            return 0;
        case IDC_ZOOM_OUT:
            FocusedPane()->ApplyZoomRatio(1.0f / 1.25f);
            UpdateCommandUi();
            return 0;
        // With zoom sync on, the three preset commands drive BOTH panes: the
        // ratio routing would otherwise land the sibling at target*anchor
        // (100% only on one side) or leave its fit mode untouched. Silent
        // scope: the paired absolute writes must not echo through the ratio
        // path mid-flight. Fits re-capture the ratio anchor by themselves
        // (FitZoomChanged is handled even inside the silent scope); the
        // manual 100% pair needs the explicit resync.
        case IDC_ZOOM_ACTUAL:
            if (m_sync->ZoomSync()) {
                m_sync->ApplySilently([this] {
                    m_left->SetManualZoom(1.0f);
                    m_right->SetManualZoom(1.0f);
                });
                m_sync->ResyncZoomAnchor();
            } else {
                FocusedPane()->SetManualZoom(1.0f);
            }
            UpdateCommandUi();
            return 0;
        case IDC_FIT_WIDTH:
            if (m_sync->ZoomSync()) {
                m_sync->ApplySilently([this] {
                    m_left->SetZoomMode(PaneWindow::ZoomMode::FitWidth);
                    m_right->SetZoomMode(PaneWindow::ZoomMode::FitWidth);
                });
            } else {
                FocusedPane()->SetZoomMode(PaneWindow::ZoomMode::FitWidth);
            }
            UpdateCommandUi();
            return 0;
        case IDC_FIT_PAGE:
            if (m_sync->ZoomSync()) {
                m_sync->ApplySilently([this] {
                    m_left->SetZoomMode(PaneWindow::ZoomMode::FitPage);
                    m_right->SetZoomMode(PaneWindow::ZoomMode::FitPage);
                });
            } else {
                FocusedPane()->SetZoomMode(PaneWindow::ZoomMode::FitPage);
            }
            UpdateCommandUi();
            return 0;
        case IDC_SCROLL_CONTINUOUS:
            ApplyScrollMode(PaneWindow::ScrollMode::Continuous);
            return 0;
        case IDC_SCROLL_PAGED:
            ApplyScrollMode(PaneWindow::ScrollMode::Paged);
            return 0;
        case IDC_GOTO_PAGE:
            ShowGotoPageDialog();
            return 0;
        case IDC_OPTIONS:
            ShowOptionsDialog();
            return 0;
        case IDC_SWAP_PANES:
            SwapPanes();
            return 0;
        case IDC_FULLSCREEN:
            ToggleFullScreen();
            return 0;
        case IDC_LANG_ENGLISH:
        case IDC_LANG_ITALIAN:
        case IDC_LANG_GERMAN:
        case IDC_LANG_FRENCH:
        case IDC_LANG_HUNGARIAN:
        case IDC_LANG_UKRAINIAN:
        case IDC_LANG_ROMANIAN:
        case IDC_LANG_PORTUGUESE:
        case IDC_LANG_GREEK:
        case IDC_LANG_SPANISH:
        case IDC_LANG_POLISH:
        case IDC_LANG_DUTCH:
        case IDC_LANG_CZECH:
        case IDC_LANG_SWEDISH:
            SwitchLanguage(static_cast<Lang>(LOWORD(wParam) - IDC_LANG_ENGLISH));
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
        LRESULT bandResult = 0;
        if (m_menuBand.OnNotify(hdr, &bandResult))
            return bandResult;
        if (hdr->hwndFrom == m_rebar && hdr->code == RBN_CHEVRONPUSHED) {
            const auto* nm = reinterpret_cast<const NMREBARCHEVRON*>(lParam);
            if (nm->wID == kBandMenu) {
                RECT rc = nm->rc;
                MapWindowPoints(m_rebar, nullptr, reinterpret_cast<LPPOINT>(&rc), 2);
                m_menuBand.ShowChevron(rc);
            } else {
                ShowChevronMenu(nm);
            }
            return 0;
        }
        if (hdr->hwndFrom == m_rebar && hdr->code == RBN_HEIGHTCHANGE) {
            // Band drags rewrap the rows: the panes must follow the new bar
            // height (guarded against Layout's own rebar MoveWindow).
            if (!m_layingOut)
                Layout();
            return 0;
        }
        if (hdr->idFrom == IDC_OUTLINE_TREE && hdr->code == TVN_SELCHANGEDW &&
            m_outlineVisible && !m_populatingOutline && m_outlinePane) {
            const NMTREEVIEWW* tv = reinterpret_cast<const NMTREEVIEWW*>(lParam);
            if (tv->itemNew.hItem)
                m_outlinePane->GotoOutlineItem(static_cast<int>(tv->itemNew.lParam));
            return 0;
        }
        if (hdr->code == TTN_GETDISPINFOW) { // toolbar tooltips; idFrom = command id
            auto* info = reinterpret_cast<NMTTDISPINFOW*>(lParam);
            info->lpszText = const_cast<wchar_t*>(Str(CommandTipId(static_cast<UINT>(hdr->idFrom))));
            return 0;
        }
        break;
    }

    case WM_CONTEXTMENU: {
        // Right-click anywhere on the bar area (rebar background or a band
        // child): the IE-style toolbar context menu.
        const HWND from = reinterpret_cast<HWND>(wParam);
        // Gate on actual bar visibility, not on the fullscreen flag: the
        // "toolbar in full screen" option keeps the rebar live there, and its
        // context menu (lock, text options) must stay reachable.
        if (m_rebar && IsWindowVisible(m_rebar) && (from == m_rebar || IsChild(m_rebar, from))) {
            POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (pt.x == -1 && pt.y == -1) { // keyboard invocation: anchor to the bar
                RECT rc;
                GetWindowRect(m_rebar, &rc);
                pt = {rc.left, rc.bottom};
            }
            ShowRebarContextMenu(pt);
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
        if ((wParam & 0xFFF0) == SC_KEYMENU) {
            if (lParam == 0 && m_altScrollGesture) {
                m_altScrollGesture = false;
                return 0;
            }
            // The rebar band owns menu-bar keyboarding now: plain Alt/F10
            // arms the band, Alt+letter tracks the matching popup.
            if (m_menuBand.OnSysKeyMenu(lParam))
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
        // flag must not swallow the NEXT genuine Alt tap. TrackPopupMenuEx
        // sends this too, so the band's popups keep the reset behavior.
        m_altScrollGesture = false;
        break;

    // Menu-loop bookkeeping for the band's Left/Right top-level navigation.
    case WM_MENUSELECT:
        m_menuBand.OnMenuSelect(wParam);
        break;
    case WM_INITMENUPOPUP:
        m_menuBand.OnInitMenuPopup();
        break;
    case WM_UNINITMENUPOPUP:
        m_menuBand.OnUninitMenuPopup();
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
        // The menu is never attached (it feeds the rebar band), so window
        // destruction never frees it: always free it by hand.
        if (m_menu) {
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
    // flags: the whole rebar (menu included) disappears, matching the old
    // SetMenu(nullptr) behavior - unless the Options ask to keep the toolbar
    // and/or the status bar on screen. "View > Toolbar" only hides the
    // command toolbar and page box BANDS; the menu band always stays.
    const bool rebarOn = m_rebar && (!m_fullscreen || m_fsShowToolbar);
    const bool statusOn = m_status && m_statusVisible && (!m_fullscreen || m_fsShowStatus);
    if (m_rebar) {
        // Bands by id, not index: the user can reorder them when unlocked.
        const auto showBand = [this](UINT id, BOOL show) {
            const LRESULT index = SendMessageW(m_rebar, RB_IDTOINDEX, id, 0);
            if (index != -1)
                SendMessageW(m_rebar, RB_SHOWBAND, static_cast<WPARAM>(index), show);
        };
        showBand(kBandToolbar, m_toolbarVisible ? TRUE : FALSE);
        showBand(kBandPageBox, m_toolbarVisible ? TRUE : FALSE);
        ShowWindow(m_rebar, rebarOn ? SW_SHOW : SW_HIDE);
    }
    if (m_status)
        ShowWindow(m_status, statusOn ? SW_SHOW : SW_HIDE);
    // Floating full-screen escape hatch: only when the real full-screen
    // button is NOT on screen (the kept toolbar must also be visible per
    // View > Toolbar, or its band is hidden inside the shown rebar).
    if (m_fullscreen && !(m_fsShowToolbar && m_toolbarVisible)) {
        EnsureFsBar();
        if (m_fsBar) {
            SIZE sz{};
            SendMessageW(m_fsBar, TB_GETMAXSIZE, 0, reinterpret_cast<LPARAM>(&sz));
            const int margin = MulDiv(8, m_dpi, 96);
            SetWindowPos(m_fsBar, HWND_TOP, static_cast<int>(rc.right) - sz.cx - margin, margin,
                         sz.cx, sz.cy, SWP_SHOWWINDOW);
        }
    } else if (m_fsBar) {
        ShowWindow(m_fsBar, SW_HIDE);
    }
    int top = 0;
    int bottom = rc.bottom;
    if (rebarOn) {
        // Width first, then measure: RB_GETBARHEIGHT needs the row layout.
        // The guard breaks the cycle with RBN_HEIGHTCHANGE, whose handler
        // calls Layout right back.
        m_layingOut = true;
        MoveWindow(m_rebar, 0, 0, rc.right, 0, TRUE);
        top = static_cast<int>(SendMessageW(m_rebar, RB_GETBARHEIGHT, 0, 0));
        MoveWindow(m_rebar, 0, 0, rc.right, top, TRUE);
        m_layingOut = false;
    }
    if (statusOn) {
        SendMessageW(m_status, WM_SIZE, 0, 0);
        RECT bar;
        GetWindowRect(m_status, &bar);
        bottom -= bar.bottom - bar.top;
        // Seven parts mirroring the pane geometry: left half = left pane
        // (page, zoom), sync summary CENTERED straddling the midline, right
        // half = right pane; two borderless fillers absorb the slack (the
        // last one also hosts the size grip).
        const int pageW = MulDiv(170, m_dpi, 96);
        const int zoomW = MulDiv(60, m_dpi, 96);
        const int syncW = MulDiv(190, m_dpi, 96); // fits "Sync: scroll+zoom · 12 pts"
        int parts[7];
        parts[0] = pageW;
        parts[1] = parts[0] + zoomW;
        parts[2] = std::max(parts[1], static_cast<int>(rc.right) / 2 - syncW / 2); // filler
        parts[3] = parts[2] + syncW; // sync, astride the midline
        parts[4] = parts[3] + pageW;
        parts[5] = parts[4] + zoomW;
        parts[6] = -1; // filler under the grip
        SendMessageW(m_status, SB_SETPARTS, std::size(parts), reinterpret_cast<LPARAM>(parts));
    }
    m_contentTop = top;
    m_contentBottom = bottom;

    const int h = bottom - top;
    const int splitterW = MulDiv(kSplitterDip, m_dpi, 96);
    const int minPane = MulDiv(kMinPaneDip, m_dpi, 96);
    if (m_outlineVisible) {
        // User-resizable width, clamped so both panes keep their minimum.
        const int minSidebar = MulDiv(120, m_dpi, 96);
        const int maxSidebar =
            std::max(minSidebar, static_cast<int>(rc.right) - 2 * minPane - 2 * splitterW);
        m_sidebarPx = std::clamp(MulDiv(m_outlineWidthDip, m_dpi, 96), minSidebar, maxSidebar);
    } else {
        m_sidebarPx = 0;
    }
    const int x0 = m_sidebarPx + (m_outlineVisible ? splitterW : 0);
    const int w = rc.right - x0;
    if (w <= 0 || h <= 0 || !m_left || !m_left->Hwnd())
        return;

    if (m_outlineTree) {
        if (m_outlineVisible)
            MoveWindow(m_outlineTree, 0, top, m_sidebarPx, h, TRUE);
        ShowWindow(m_outlineTree, m_outlineVisible ? SW_SHOW : SW_HIDE);
    }

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
                       m_outlineTree, m_status, m_pageBox}) {
        if (child)
            SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
    m_menuBand.SetFont(font); // text buttons; the icon toolbar keeps its own
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
    int paneRight = m_findTarget == m_left.get() ? m_splitterX : rc.right;
    // In full screen the floating exit button owns the top-right corner, right
    // where the right pane's find bar lands: shift the bar left so the one
    // mouse affordance for leaving full screen stays reachable.
    if (m_findTarget != m_left.get() && m_fsBar && IsWindowVisible(m_fsBar)) {
        RECT fs;
        GetWindowRect(m_fsBar, &fs);
        paneRight -= (fs.right - fs.left) + MulDiv(8, m_dpi, 96);
    }
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

RECT MainWindow::OutlineDividerRect() const {
    if (!m_outlineVisible || m_sidebarPx <= 0)
        return {0, 0, 0, 0}; // PtInRect is always false on an empty rect
    const int splitterW = MulDiv(kSplitterDip, m_dpi, 96);
    return {m_sidebarPx, m_contentTop, m_sidebarPx + splitterW, m_contentBottom};
}

void MainWindow::SetSplitRatioFromX(int x) {
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    const int splitterW = MulDiv(kSplitterDip, m_dpi, 96);
    // Live sidebar geometry (the outline divider shifts the pane strip).
    const int x0 = m_sidebarPx + (m_outlineVisible ? splitterW : 0);
    const int usable = rc.right - x0 - splitterW;
    if (usable <= 0)
        return;
    const float ratio = static_cast<float>(x - x0 - splitterW / 2) / static_cast<float>(usable);
    m_splitRatio = std::clamp(ratio, 0.1f, 0.9f);
}

void MainWindow::SetOutlineWidthFromX(int x) {
    // The drag position is the divider's left edge = the sidebar width;
    // Layout re-clamps against the live client width.
    m_outlineWidthDip = std::clamp(MulDiv(x, 96, m_dpi), 120, 600);
}

void MainWindow::FitOutlineToContent() {
    if (!m_outlineTree || !m_outlineVisible)
        return;
    // The width that makes the tree's horizontal scrollbar vanish: the
    // widest EXPANDED item's text right edge (collapsed branches do not feed
    // the horizontal extent). Item rects are client-relative, so a tree that
    // is currently h-scrolled needs the offset added back.
    int maxRight = 0;
    HTREEITEM item = TreeView_GetRoot(m_outlineTree);
    while (item) {
        RECT rc{};
        if (TreeView_GetItemRect(m_outlineTree, item, &rc, TRUE))
            maxRight = std::max(maxRight, static_cast<int>(rc.right));
        item = TreeView_GetNextVisible(m_outlineTree, item);
    }
    if (maxRight <= 0)
        return; // empty outline: keep the current width
    const int scrollX = GetScrollPos(m_outlineTree, SB_HORZ);
    const LONG style = GetWindowLongW(m_outlineTree, GWL_STYLE);
    const int vsb =
        (style & WS_VSCROLL) != 0 ? GetSystemMetricsForDpi(SM_CXVSCROLL, m_dpi) : 0;
    const int border = GetSystemMetricsForDpi(SM_CXBORDER, m_dpi); // WS_BORDER edges
    const int pad = MulDiv(8, m_dpi, 96); // breathing room past the longest title
    const int desired = maxRight + scrollX + pad + vsb + 2 * border;
    // Same bounds as the drag: panes must stay usable (Layout re-clamps too).
    m_outlineWidthDip = std::clamp(MulDiv(desired, 96, m_dpi), 120, 600);
    Layout();
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
