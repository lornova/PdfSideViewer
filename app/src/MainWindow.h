#pragma once

#include "DxResources.h"
#include "PaneWindow.h"
#include "framework.h"
#include "util/Settings.h"
#include "util/Strings.h"
#include "view/MenuBand.h"
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
    // 1023..1024 held the two-language radio group; when it grew to five it
    // moved to 1059+ (1025 landlocks it here). The old slots stay retired.
    // Contiguous: CheckMenuRadioItem spans the scroll-mode radio group.
    IDC_SCROLL_CONTINUOUS = 1025,
    IDC_SCROLL_PAGED = 1026, // Ctrl+5 (pane OnKeyDown, like Ctrl+2/3; NOT an accelerator)
    IDC_CLOSE_DOC = 1027,    // Ctrl+W, closes the focused pane's document
    IDC_GOTO_PAGE = 1028,    // Ctrl+G
    IDC_SWAP_PANES = 1029,   // F8
    // MRU ranges: kMruMaxEntries slots each, dispatched as ranges (not single
    // cases) in the WM_COMMAND handler.
    IDC_MRU_FILE_FIRST = 1030,
    IDC_MRU_PAIR_FIRST = 1040,
    IDC_OPTIONS = 1049,
    IDC_LOCK_TOOLBARS = 1050,       // IE-style rebar lock (menu + rebar context menu)
    IDC_ADD_SYNC_POINT = 1051,      // Shift+F7
    IDC_SYNC_FROM_BOOKMARKS = 1052,
    IDC_SYNC_POINTS = 1053,         // the view/remove dialog
    IDC_CLEAR_SYNC_POINTS = 1054,   // Ctrl+Shift+F7
    IDC_TOGGLE_ALIGNMENT_GAPS = 1055,
    // Contiguous: CheckMenuRadioItem spans the toolbar-text radio group
    // (Internet Explorer's three text options).
    IDC_TOOLBAR_TEXT_BELOW = 1056,
    IDC_TOOLBAR_TEXT_RIGHT = 1057,
    IDC_TOOLBAR_TEXT_NONE = 1058,
    // Contiguous AND in Lang-enum order: CheckMenuRadioItem spans the group
    // and WM_COMMAND maps id - IDC_LANG_ENGLISH straight to a Lang.
    IDC_LANG_ENGLISH = 1059,
    IDC_LANG_ITALIAN = 1060,
    IDC_LANG_GERMAN = 1061,
    IDC_LANG_FRENCH = 1062,
    IDC_LANG_HUNGARIAN = 1063,
    IDC_LANG_UKRAINIAN = 1064,
    IDC_LANG_ROMANIAN = 1065,
    IDC_LANG_PORTUGUESE = 1066,
    IDC_LANG_GREEK = 1067,
    IDC_LANG_SPANISH = 1068,
    IDC_LANG_POLISH = 1069,
    IDC_LANG_DUTCH = 1070,
    IDC_LANG_CZECH = 1071,
    IDC_LANG_SWEDISH = 1072,
    // Control ids live in a separate >= 2000 space so they can never collide
    // with command dispatch: 2001 page box, 2100+ Options dialog, 2201 goto
    // dialog, 2300+ the menu-band toolbar and its buttons (MenuBand.h), 2400+
    // the sync-points dialog.
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
constexpr ULONG_PTR kCdOpenDocument = 0x50535644;  // 'PSVD' (-open-left/-open-right handoff)

#pragma pack(push, 1)
struct ForwardSearchBlob {
    uint32_t line;   // 1-based
    uint32_t texLen; // wchar_t count
    uint32_t pdfLen; // wchar_t count
};
// Explorer-verb handoff payload: blob followed by pathLen wchar_t, no NUL.
struct OpenDocumentBlob {
    uint32_t side;    // 0 = left pane, 1 = right
    uint32_t pathLen; // wchar_t count
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
    RECT OutlineDividerRect() const;
    void SetSplitRatioFromX(int x);
    void SetOutlineWidthFromX(int x);
    void FitOutlineToContent();
    void OpenDocumentDialog(bool rightPane);
    void UpdateTheme();
    void UpdateTitle();
    PaneWindow* FocusedPane() const;
    HMENU BuildMenuBar();
    void BuildRebar(HINSTANCE hinst);
    void UpdateRebarBandSizes();
    void SetRebarLocked(bool locked);
    void ApplyPageBoxFixedSize();
    void ApplyRebarLayout(const std::wstring& layout);
    std::wstring SerializeRebarLayout() const;
    void ShowRebarContextMenu(POINT screenPt);
    void ShowChevronMenu(const NMREBARCHEVRON* nm);
    static StrId CommandTipId(UINT id);
    void UpdatePageBox();
    bool GotoFromText(const std::wstring& text);
    static LRESULT CALLBACK PageBoxProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                        UINT_PTR id, DWORD_PTR data);
    void ShowGotoPageDialog();
    static INT_PTR CALLBACK GotoDlgProc(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam);
    void ShowOptionsDialog();
    static INT_PTR CALLBACK OptionsDlgProc(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam);
    void GenerateSyncPointsFromBookmarks(bool interactive);
    void ApplyAlignmentGaps();     // the one reaction to every sync-map change
    void RememberSyncPoints();     // upsert the current pair into m_savedPoints
    void TryRestoreSavedPoints();  // reinstall a remembered map when a pair opens
    void ShowSyncPointsDialog();
    static INT_PTR CALLBACK SyncPointsDlgProc(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam);
    BOOL HandleOpenDocumentCopyData(const COPYDATASTRUCT& cds);
    void RebuildMruMenus();
    void RecordMruFile(const std::wstring& path);
    void RecordMruPair(const std::wstring& left, const std::wstring& right);
    void OpenMruFile(size_t index);
    void OpenMruPair(size_t index);
    void CreateToolbar(HINSTANCE hinst);
    void RebuildToolbarIcons();
    void RebuildToolbarInBand();       // recreate the toolbar inside its rebar band
    void SetToolbarTextMode(int mode); // 0 none, 1 below, 2 selective right (IE)
    void EnsureFsBar();                // floating full-screen mini toolbar (lazy)
    void UpdateCommandUi();
    void UpdateStatusBar();
    void ShowAboutBox();
    void ToggleFullScreen();
    void SwitchLanguage(Lang lang);
    void ApplyScrollMode(PaneWindow::ScrollMode mode);
    void SwapPanes();
    void RouteForwardSearch(ForwardSearchRequest req);
    void LaunchInverseSearch(const SyncTexIndex::InverseHit& hit);
    void ShowStatusMessage(StrId id);
    void ShowStatusMessage(std::wstring text);
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
    // Per-pair sync-point memory ([sync-points]): manual points serialized,
    // generated ones re-derived from the bookmarks via the hadAuto flag.
    std::vector<SavedSyncPoints> m_savedPoints;
    HWND m_rebar = nullptr; // hosts the menu band, the command toolbar, the page box
    MenuBand m_menuBand;
    HWND m_toolbar = nullptr;
    HWND m_pageBox = nullptr;             // editable current-page box (rebar band 2)
    PaneWindow* m_activePane = nullptr;   // last pane that had focus (page box target;
                                          // FocusedPane() falls back to left while the
                                          // box itself owns the focus)
    HIMAGELIST m_toolbarIcons = nullptr;
    HWND m_status = nullptr;
    HBRUSH m_bgBrush = nullptr;
    UINT m_dpi = 96;
    bool m_toolbarVisible = true;
    bool m_statusVisible = true;
    bool m_rebarLocked = true;        // IE-style toolbar lock (grippers + dragging)
    int m_toolbarText = 1;            // IE text options: 0 none, 1 below, 2 selective right
    bool m_fsShowToolbar = false;     // full screen keeps the full toolbar ([window] fsToolbar)
    bool m_fsShowStatus = false;      // full screen keeps the status bar ([window] fsStatusbar)
    // Floating one-button escape hatch shown in full screen when the full
    // toolbar is hidden: top-right, just the full-screen toggle.
    HWND m_fsBar = nullptr;
    HWND m_syncPtsDlg = nullptr;      // live sync-points dialog, if open: map changes
                                      // post it a refresh (the modal loop dispatches
                                      // WM_PSV_*, so reloads mutate the map mid-dialog)
    HIMAGELIST m_fsBarIcons = nullptr;
    UINT m_fsBarDpi = 0;
    std::wstring m_rebarBandsSaved;   // band layout loaded from the session, applied
                                      // by BuildRebar after the default insert
    bool m_layingOut = false;         // Layout's own rebar MoveWindow fires
                                      // RBN_HEIGHTCHANGE; break the recursion
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
    std::wstring m_statusText[7]; // last SB_SETTEXT per part: skip no-op repaints

    // Last DocumentOpened path per pane: an unchanged path = auto-reload, the
    // cue to re-derive the bookmark sync points from the fresh outline.
    std::wstring m_lastDocLeft;
    std::wstring m_lastDocRight;
    bool m_showAlignmentGaps = true; // "Show Alignment Gaps" toggle ([sync] showGaps)
    bool m_showAnchors = true;       // Options: anchor glyphs ([sync] showAnchors)
    bool m_showTicks = true;         // Options: scrollbar tick strip ([sync] showTicks)
    bool m_showHeader = true;        // Options: per-pane header strip ([window] header)
    bool m_headerShowPath = false;   // Options: header shows the path ([window] headerPath)
    uint64_t m_gapsEpoch = 0;        // bumped per ApplyAlignmentGaps; stamps both panes
    // Sync map parked across a pane swap: mirrored (left/right exchanged per
    // point; the coordinates co-increase, so the order survives), captured
    // BEFORE the reopen storm (each swap side fires DocumentOpened, clearing
    // the live map), reinstalled when BOTH panes settled on the two expected
    // swapped paths. Consumed on install; discarded on any mismatch (failed
    // open, close, interleaved open of another file, a second swap).
    struct ParkedSwapMap {
        bool pending = false;
        bool leftSettled = false;
        bool rightSettled = false;
        std::wstring expectLeft;
        std::wstring expectRight;
        std::vector<SyncPoint> points;
    };
    ParkedSwapMap m_swapMap;

    // SyncTeX: inverse-search launch template and the forward request parked
    // until its document finishes opening (cold start, on-demand open, or a
    // request landing mid-reload).
    std::wstring m_synctexInverse;
    std::optional<ForwardSearchRequest> m_parkedForward;
    float m_splitRatio = 0.5f;
    int m_splitterX = 0; // left edge of the splitter band, client px
    // Two draggable dividers share the mouse handlers: the pane splitter and
    // the outline sidebar divider.
    enum class DragTarget { None, PaneSplitter, OutlineDivider };
    DragTarget m_drag = DragTarget::None;
    int m_outlineWidthDip = 260; // persisted; the divider drag updates it
    int m_sidebarPx = 0;         // outline width in device px, as laid out (0 = hidden)
    bool m_restoreSession = true;
    int m_wheelLines = 0; // continuous-scroll lines per wheel notch; 0 = system
    struct Defaults {
        PaneWindow::ScrollMode scrollMode = PaneWindow::ScrollMode::Continuous;
        PaneWindow::ZoomMode zoomMode = PaneWindow::ZoomMode::FitPage;
        bool scrollSync = true;
        bool zoomSync = true;
    } m_defaults;
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
