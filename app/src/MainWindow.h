#pragma once

#include "DxResources.h"
#include "PaneWindow.h"
#include "framework.h"
#include "util/Settings.h"
#include "util/Strings.h"
#include "view/SyncController.h"

#include <commctrl.h> // HIMAGELIST
#include <optional>

// Command IDs shared between the accelerator table and WM_COMMAND dispatch.
enum CommandId : WORD {
    IDC_OPEN_LEFT = 1001,
    IDC_OPEN_RIGHT = 1002,
    IDC_FOCUS_NEXT_PANE = 1003,
    IDC_TOGGLE_SCROLL_SYNC = 1004, // F7
    IDC_TOGGLE_ZOOM_SYNC = 1005,   // Ctrl+F7
    IDC_FIND_SHOW = 1006,          // Ctrl+F
    IDC_FIND_NEXT = 1007,          // F3 / Enter in the find box
    IDC_FIND_PREV = 1008,          // Shift+F3 / Shift+Enter
    IDC_FIND_CLOSE = 1009,         // Esc in the find box
    IDC_FIND_EDIT = 1010,
    IDC_TOGGLE_OUTLINE = 1011,     // F9
    IDC_OUTLINE_TREE = 1012,
    IDC_TOGGLE_TOOLBAR = 1013,
    IDC_TOGGLE_STATUSBAR = 1014,
    IDC_ZOOM_IN = 1015,
    IDC_ZOOM_OUT = 1016,
    // Contiguous: CheckMenuRadioItem spans IDC_ZOOM_ACTUAL..IDC_FIT_PAGE.
    IDC_ZOOM_ACTUAL = 1017, // Ctrl+0
    IDC_FIT_WIDTH = 1018,   // Ctrl+2
    IDC_FIT_PAGE = 1019,    // Ctrl+3
    IDC_FULLSCREEN = 1020,  // F11 / Alt+Enter
    IDC_ABOUT = 1021,
    IDC_EXIT = 1022,
    // Contiguous: CheckMenuRadioItem spans the language radio group.
    IDC_LANG_ENGLISH = 1023,
    IDC_LANG_ITALIAN = 1024,
    // Contiguous: CheckMenuRadioItem spans the scroll-mode radio group.
    IDC_SCROLL_CONTINUOUS = 1025,
    IDC_SCROLL_PAGED = 1026, // Ctrl+4 (pane OnKeyDown, like Ctrl+2/3; NOT an accelerator)
    IDC_CLOSE_DOC = 1027,    // Ctrl+W, closes the focused pane's document
    // MRU ranges: kMruMaxEntries slots each, dispatched as ranges (not single
    // cases) in the WM_COMMAND handler.
    IDC_MRU_FILE_FIRST = 1030,
    IDC_MRU_PAIR_FIRST = 1040,
};

// SyncTeX forward search request, exchanged between processes via
// WM_COPYDATA: the sender is a short-lived second instance spawned by the
// editor (LaTeX Workshop). Payload = ForwardSearchBlob followed by the tex
// then pdf characters, no NUL terminators. Arrives from arbitrary processes:
// the receiver validates sizes exactly.
struct ForwardSearchRequest {
    std::wstring tex;
    int line = 0; // 1-based
    std::wstring pdf;
};

constexpr ULONG_PTR kCdForwardSearch = 0x50535646; // 'PSVF'
constexpr ULONG_PTR kCdOpenDocument = 0x50535644;  // 'PSVD' (reserved: -reuse-instance)

#pragma pack(push, 1)
struct ForwardSearchBlob {
    uint32_t line;   // 1-based
    uint32_t texLen; // wchar_t count
    uint32_t pdfLen; // wchar_t count
};
#pragma pack(pop)

// Top-level frame: two PaneWindows separated by a draggable splitter.
class MainWindow {
public:
    static constexpr PCWSTR kClassName = L"PsvMainWindow";
    static void RegisterWindowClass(HINSTANCE hinst);

    ~MainWindow();

    bool Create(HINSTANCE hinst, int nCmdShow, std::wstring leftFile, std::wstring rightFile,
                std::optional<ForwardSearchRequest> forward = std::nullopt);
    HWND Hwnd() const { return m_hwnd; }

    // For the message loop: Tab inside the find bar must cycle its controls
    // instead of firing the pane-switch accelerator.
    HWND FindBarHwnd() const { return m_findBar; }
    bool FindBarHasFocus() const {
        return m_findBar && IsWindowVisible(m_findBar) && IsChild(m_findBar, GetFocus());
    }
    // For the message loop: Esc exits full screen (unless the find bar owns it).
    bool IsFullScreen() const { return m_fullscreen; }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    void Layout();
    RECT SplitterRect() const;
    void SetSplitRatioFromX(int x);
    void OpenDocumentDialog(bool rightPane);
    void UpdateTheme();
    void UpdateTitle();
    PaneWindow* FocusedPane() const;
    HMENU BuildMenuBar();
    void RebuildMruMenus();
    void RecordMruFile(const std::wstring& path);
    void RecordMruPair(const std::wstring& left, const std::wstring& right);
    void OpenMruFile(size_t index);
    void OpenMruPair(size_t index);
    void CreateToolbar(HINSTANCE hinst);
    void RebuildToolbarIcons();
    void UpdateCommandUi();
    void UpdateStatusBar();
    void ShowAboutBox();
    void ToggleFullScreen();
    void SwitchLanguage(Lang lang);
    void ApplyScrollMode(PaneWindow::ScrollMode mode);
    void RouteForwardSearch(ForwardSearchRequest req);
    void LaunchInverseSearch(const SyncTexIndex::InverseHit& hit);
    void ShowStatusMessage(StrId id);
    void ApplySession(const AppSettings& session);
    void SaveSession() const;
    void CreateFindBar();
    void ShowFindBar();
    void CloseFindBar();
    void LayoutFindBar();
    void UpdateUiFont();
    void UpdateOutlineSidebar(PaneWindow* pane);
    static bool IsSystemDark();

    DxResources m_dx;
    std::unique_ptr<PaneWindow> m_left;
    std::unique_ptr<PaneWindow> m_right;
    std::unique_ptr<SyncController> m_sync;
    HWND m_hwnd = nullptr;
    HWND m_lastPaneFocus = nullptr; // pane holding focus when the frame deactivates
    HMENU m_menu = nullptr;
    // Submenus of the File popup, repopulated in place by RebuildMruMenus;
    // owned (and destroyed) by the menu bar they hang under.
    HMENU m_mruFilesMenu = nullptr;
    HMENU m_mruPairsMenu = nullptr;
    std::vector<std::wstring> m_mruFiles; // most recent first
    std::vector<MruPair> m_mruPairs;      // most recent first
    HWND m_toolbar = nullptr;
    HIMAGELIST m_toolbarIcons = nullptr;
    HWND m_status = nullptr;
    HBRUSH m_bgBrush = nullptr;
    UINT m_dpi = 96;
    bool m_toolbarVisible = true;
    bool m_statusVisible = true;
    bool m_fullscreen = false;
    // Global scroll mode (authoritative copy; the panes cache it).
    PaneWindow::ScrollMode m_scrollMode = PaneWindow::ScrollMode::Continuous;
    // Alt+scroll is the temporary sync unlock: releasing Alt afterwards must
    // not pop the menu bar open (set on Scrolled-with-Alt, consumed by the
    // SC_KEYMENU suppression).
    bool m_altScrollGesture = false;
    // Window state captured when entering full screen; also what SaveSession
    // persists while full screen (the live placement is the monitor rect).
    WINDOWPLACEMENT m_fsRestorePlacement{};
    LONG m_fsRestoreStyle = 0;
    // Vertical band left for the panes between the toolbar and the status bar.
    int m_contentTop = 0;
    int m_contentBottom = 0;
    std::wstring m_statusText[5]; // last SB_SETTEXT per part: skip no-op repaints

    // SyncTeX: inverse-search launch template and the forward request parked
    // until its document finishes opening (cold start, on-demand open, or a
    // request landing mid-reload).
    std::wstring m_synctexInverse;
    std::optional<ForwardSearchRequest> m_parkedForward;
    float m_splitRatio = 0.5f;
    int m_splitterX = 0; // left edge of the splitter band, client px
    bool m_draggingSplitter = false;
    bool m_dark = false;
    bool m_destroying = false; // suppress focus forwarding during teardown
    bool m_startMaximized = false;
    // Last known good pane sessions (DPI-rescaled at load): SaveSession falls
    // back to these for panes that never fully opened, so closing while a
    // document is opening/unreachable does not wipe it from settings.ini.
    PaneSettings m_fallbackLeft;
    PaneSettings m_fallbackRight;

    // Find bar (overlay child hosting the standard controls)
    HWND m_findBar = nullptr;
    HWND m_findEdit = nullptr;
    HWND m_findCount = nullptr;
    HWND m_findPrev = nullptr;
    HWND m_findNext = nullptr;
    HWND m_findClose = nullptr;
    HFONT m_uiFont = nullptr;
    PaneWindow* m_findTarget = nullptr;

    // Outline sidebar (bookmarks of the focused pane)
    HWND m_outlineTree = nullptr;
    bool m_outlineVisible = false;
    bool m_populatingOutline = false; // suppress TVN_SELCHANGED navigation
    PaneWindow* m_outlinePane = nullptr;
};
