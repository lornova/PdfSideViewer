#pragma once

#include "DxResources.h"
#include "PaneWindow.h"
#include "framework.h"
#include "util/Settings.h"
#include "view/SyncController.h"

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
};

// Top-level frame: two PaneWindows separated by a draggable splitter.
class MainWindow {
public:
    static constexpr PCWSTR kClassName = L"PsvMainWindow";
    static void RegisterWindowClass(HINSTANCE hinst);

    ~MainWindow();

    bool Create(HINSTANCE hinst, int nCmdShow, std::wstring leftFile, std::wstring rightFile);
    HWND Hwnd() const { return m_hwnd; }

    // For the message loop: Tab inside the find bar must cycle its controls
    // instead of firing the pane-switch accelerator.
    HWND FindBarHwnd() const { return m_findBar; }
    bool FindBarHasFocus() const {
        return m_findBar && IsWindowVisible(m_findBar) && IsChild(m_findBar, GetFocus());
    }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    void Layout();
    RECT SplitterRect() const;
    void SetSplitRatioFromX(int x);
    void OpenDocumentDialog(bool rightPane);
    void UpdateTheme();
    void UpdateTitle();
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
    HBRUSH m_bgBrush = nullptr;
    UINT m_dpi = 96;
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
