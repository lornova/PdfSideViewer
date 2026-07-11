#pragma once

#include "framework.h"

// The menu bar emulated as a text-only flat toolbar so it can live inside a
// rebar band next to the command toolbar: a native HMENU bar is a non-client
// artifact of the frame and cannot be a band child (MSDN "IE-style menu bar"
// pattern). Ownership split: MainWindow owns the HMENU (and every WM_COMMAND,
// via TrackPopupMenuEx WITHOUT TPM_RETURNCMD) plus the check/radio state;
// this class owns presentation and the modal tracking loop.
class MenuBand {
public:
    // Toolbar child id inside the rebar; buttons are kButtonIdFirst + index.
    static constexpr int kToolbarId = 2300;
    static constexpr int kButtonIdFirst = 2301;

    // `source` (the never-attached menu bar HMENU) must outlive this object
    // or be replaced via SetMenu before destruction of the old handle.
    bool Create(HWND frame, HWND rebar, HINSTANCE hinst, HMENU source);
    // Language switch: repoint the popups and retitle the buttons.
    void SetMenu(HMENU source);
    HWND Toolbar() const { return m_toolbar; }
    SIZE IdealSize() const; // TB_GETMAXSIZE, for the rebar band metrics
    void SetFont(HFONT font);

    // TBN_DROPDOWN from our toolbar posts kMsgTrack (the popup must NOT open
    // inside the notification); everything else is ignored. Returns true when
    // the notify was consumed.
    bool OnNotify(const NMHDR* hdr, LRESULT* result);
    // Frame's SC_KEYMENU: lParam == 0 is the plain Alt/F10 entry (keyboard
    // mode on the band), otherwise an Alt+letter mnemonic. True = handled.
    bool OnSysKeyMenu(LPARAM lParam);
    // Rebar chevron for this band: popup listing the clipped top-level menus.
    // `screenRect` is the chevron rect in screen coordinates.
    void ShowChevron(const RECT& screenRect);
    // Menu-loop bookkeeping forwarded by the frame: whether the highlighted
    // item expands a submenu, and the popup nesting depth. Both drive the
    // Left/Right top-level navigation inside the hook.
    void OnMenuSelect(WPARAM wParam) {
        m_onSubmenuItem = (HIWORD(wParam) & MF_POPUP) != 0 && LOWORD(HIWORD(wParam)) != 0xFFFF;
    }
    void OnInitMenuPopup() { ++m_depth; }
    void OnUninitMenuPopup() {
        if (m_depth > 0)
            --m_depth;
    }
    bool IsTracking() const { return m_tracking; }

private:
    // Posted to the toolbar by TBN_DROPDOWN (wParam = button index): while
    // that notification is in flight comctl32 keeps its own "dropped button"
    // bookkeeping and PAINTS that button hot regardless of TBSTATE, so a
    // hover switch would leave the previous button lit for the whole chain.
    static constexpr UINT kMsgTrack = WM_APP + 1;

    static LRESULT CALLBACK ToolbarSubclass(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                            UINT_PTR id, DWORD_PTR data);
    static LRESULT CALLBACK MsgHook(int code, WPARAM wParam, LPARAM lParam);
    void RebuildButtons();
    void TrackFromButton(int index);
    void EnterKeyboardMode();
    void LeaveKeyboardMode();

    HWND m_frame = nullptr;
    HWND m_toolbar = nullptr;
    HMENU m_menu = nullptr;
    int m_count = 0;
    bool m_tracking = false;      // a popup chain is open (modal loop running)
    int m_active = -1;            // index of the button whose popup is open
    int m_pendingSwitch = -1;     // hover/arrow request to re-track another button
    int m_depth = 0;              // WM_INITMENUPOPUP nesting (1 = top-level popup)
    bool m_onSubmenuItem = false; // highlighted item is itself a popup
    HWND m_prevFocus = nullptr;   // focus to restore when keyboard mode ends
    HHOOK m_hook = nullptr;
    // The hook needs the instance; menus are UI-thread-modal, so a single
    // static slot (set only inside TrackFromButton) is safe.
    static MenuBand* s_tracking;
};
