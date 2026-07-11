#include "view/MenuBand.h"

#include <commctrl.h>
#include <windowsx.h>

MenuBand* MenuBand::s_tracking = nullptr;

namespace {

// Unhook on every exit path of the modal track loop: a leaked WH_MSGFILTER
// hook would tax every subsequent message in the thread.
struct HookGuard {
    HHOOK& hook;
    ~HookGuard() {
        if (hook) {
            UnhookWindowsHookEx(hook);
            hook = nullptr;
        }
    }
};

} // namespace

bool MenuBand::Create(HWND frame, HWND rebar, HINSTANCE hinst, HMENU source) {
    m_frame = frame;
    m_menu = source;
    // FLAT|TRANSPARENT is the documented pattern INSIDE a rebar (the band
    // paints the background); LIST makes the text-only buttons menu-thin.
    m_toolbar = CreateWindowExW(0, TOOLBARCLASSNAMEW, nullptr,
                                WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | TBSTYLE_FLAT |
                                    TBSTYLE_LIST | TBSTYLE_TRANSPARENT | CCS_NORESIZE |
                                    CCS_NOPARENTALIGN | CCS_NODIVIDER,
                                0, 0, 0, 0, rebar,
                                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kToolbarId)), hinst,
                                nullptr);
    if (!m_toolbar)
        return false;
    SendMessageW(m_toolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    SetWindowSubclass(m_toolbar, ToolbarSubclass, 1, reinterpret_cast<DWORD_PTR>(this));
    RebuildButtons();
    return true;
}

void MenuBand::SetMenu(HMENU source) {
    m_menu = source;
    RebuildButtons();
}

void MenuBand::RebuildButtons() {
    if (!m_toolbar)
        return;
    while (SendMessageW(m_toolbar, TB_BUTTONCOUNT, 0, 0) > 0)
        SendMessageW(m_toolbar, TB_DELETEBUTTON, 0, 0);
    m_count = m_menu ? GetMenuItemCount(m_menu) : 0;
    for (int i = 0; i < m_count; ++i) {
        wchar_t title[64] = L"";
        GetMenuStringW(m_menu, static_cast<UINT>(i), title, ARRAYSIZE(title), MF_BYPOSITION);
        TBBUTTON b{};
        b.iBitmap = I_IMAGENONE;
        b.idCommand = kButtonIdFirst + i;
        b.fsState = TBSTATE_ENABLED;
        b.fsStyle = BTNS_DROPDOWN | BTNS_AUTOSIZE;
        b.iString = reinterpret_cast<INT_PTR>(title); // & mnemonics render underlined
        SendMessageW(m_toolbar, TB_ADDBUTTONSW, 1, reinterpret_cast<LPARAM>(&b));
    }
    SendMessageW(m_toolbar, TB_AUTOSIZE, 0, 0);
}

SIZE MenuBand::IdealSize() const {
    SIZE size{};
    if (m_toolbar)
        SendMessageW(m_toolbar, TB_GETMAXSIZE, 0, reinterpret_cast<LPARAM>(&size));
    return size;
}

void MenuBand::SetFont(HFONT font) {
    if (!m_toolbar)
        return;
    SendMessageW(m_toolbar, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    SendMessageW(m_toolbar, TB_AUTOSIZE, 0, 0);
}

bool MenuBand::OnNotify(const NMHDR* hdr, LRESULT* result) {
    if (!m_toolbar || hdr->hwndFrom != m_toolbar)
        return false;
    if (hdr->code == TBN_DROPDOWN) {
        const auto* nm = reinterpret_cast<const NMTOOLBARW*>(hdr);
        const int index = nm->iItem - kButtonIdFirst;
        // Track from the posted message, after comctl32 unwinds its own
        // click handling (see kMsgTrack); TrackFromButton re-validates.
        if (!m_tracking && index >= 0 && index < m_count)
            PostMessageW(m_toolbar, kMsgTrack, static_cast<WPARAM>(index), 0);
        *result = TBDDRET_DEFAULT;
        return true;
    }
    return false;
}

bool MenuBand::OnSysKeyMenu(LPARAM lParam) {
    if (!m_toolbar || m_count == 0)
        return false;
    if (m_tracking)
        return true; // a popup chain is already open: swallow the re-entry
    if (lParam == 0) {
        // Plain Alt tap or F10: arm keyboard mode on the band (arrows move
        // the hot item, Down/Enter opens, Esc leaves).
        if (GetFocus() == m_toolbar)
            LeaveKeyboardMode();
        else
            EnterKeyboardMode();
        return true;
    }
    UINT id = 0;
    if (SendMessageW(m_toolbar, TB_MAPACCELERATOR, static_cast<WPARAM>(lParam),
                     reinterpret_cast<LPARAM>(&id)) &&
        id >= static_cast<UINT>(kButtonIdFirst)) {
        TrackFromButton(static_cast<int>(id) - kButtonIdFirst);
        return true;
    }
    return false;
}

void MenuBand::ShowChevron(const RECT& screenRect) {
    if (!m_menu || !m_toolbar || m_tracking)
        return;
    RECT client{};
    GetClientRect(m_toolbar, &client);
    // The overflow popup REUSES the bar's popups as submenus: an HMENU
    // destroy cascades into submenus, so every item must be detached with
    // RemoveMenu before DestroyMenu or the real menu bar dies with the temp.
    HMENU popup = CreatePopupMenu();
    for (int i = 0; i < m_count; ++i) {
        RECT rb{};
        SendMessageW(m_toolbar, TB_GETITEMRECT, static_cast<WPARAM>(i),
                     reinterpret_cast<LPARAM>(&rb));
        if (rb.right <= client.right)
            continue; // fully visible: reachable on the band itself
        wchar_t title[64] = L"";
        GetMenuStringW(m_menu, static_cast<UINT>(i), title, ARRAYSIZE(title), MF_BYPOSITION);
        AppendMenuW(popup, MF_POPUP, reinterpret_cast<UINT_PTR>(GetSubMenu(m_menu, i)), title);
    }
    if (GetMenuItemCount(popup) > 0) {
        // No TPM_RETURNCMD, no hook: plain nested navigation; the selection
        // posts a normal WM_COMMAND to the frame.
        TrackPopupMenuEx(popup, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_VERTICAL, screenRect.left,
                         screenRect.bottom, m_frame, nullptr);
    }
    while (GetMenuItemCount(popup) > 0)
        RemoveMenu(popup, 0, MF_BYPOSITION);
    DestroyMenu(popup);
}

void MenuBand::EnterKeyboardMode() {
    m_prevFocus = GetFocus();
    SetFocus(m_toolbar);
    SendMessageW(m_toolbar, TB_SETHOTITEM, 0, 0);
}

void MenuBand::LeaveKeyboardMode() {
    SendMessageW(m_toolbar, TB_SETHOTITEM, static_cast<WPARAM>(-1), 0);
    if (GetFocus() == m_toolbar) {
        HWND target = m_prevFocus && IsWindow(m_prevFocus) ? m_prevFocus : m_frame;
        SetFocus(target);
    }
    m_prevFocus = nullptr;
}

void MenuBand::TrackFromButton(int index) {
    if (!m_menu || index < 0 || index >= m_count || m_tracking)
        return;
    m_tracking = true;
    s_tracking = this;
    m_pendingSwitch = index;
    // Hover/arrow switches EndMenu() the current popup and loop here into the
    // adjacent one; the modal TrackPopupMenuEx makes this the only way.
    while (m_pendingSwitch >= 0) {
        const int i = m_pendingSwitch;
        m_pendingSwitch = -1;
        m_active = i;
        m_depth = 0;
        m_onSubmenuItem = false;
        SendMessageW(m_toolbar, TB_PRESSBUTTON, kButtonIdFirst + i, MAKELPARAM(TRUE, 0));
        SendMessageW(m_toolbar, TB_SETHOTITEM, i, 0);
        RECT rb{};
        SendMessageW(m_toolbar, TB_GETRECT, kButtonIdFirst + i, reinterpret_cast<LPARAM>(&rb));
        RECT exclude = rb;
        MapWindowPoints(m_toolbar, nullptr, reinterpret_cast<LPPOINT>(&exclude), 2);
        TPMPARAMS tpm{};
        tpm.cbSize = sizeof(tpm);
        tpm.rcExclude = exclude;
        {
            m_hook = SetWindowsHookExW(WH_MSGFILTER, MsgHook, nullptr, GetCurrentThreadId());
            HookGuard guard{m_hook};
            // No TPM_RETURNCMD: the selected item posts a normal WM_COMMAND
            // to the frame, so the existing dispatch stays untouched.
            TrackPopupMenuEx(GetSubMenu(m_menu, i), TPM_LEFTALIGN | TPM_TOPALIGN | TPM_VERTICAL,
                             exclude.left, exclude.bottom, m_frame, &tpm);
        }
        SendMessageW(m_toolbar, TB_PRESSBUTTON, kButtonIdFirst + i, MAKELPARAM(FALSE, 0));
    }
    SendMessageW(m_toolbar, TB_SETHOTITEM, static_cast<WPARAM>(-1), 0);
    m_active = -1;
    m_tracking = false;
    s_tracking = nullptr;
    if (GetFocus() == m_toolbar)
        LeaveKeyboardMode(); // keyboard entry: the dismissal returns focus
}

LRESULT CALLBACK MenuBand::MsgHook(int code, WPARAM wParam, LPARAM lParam) {
    MenuBand* self = s_tracking;
    if (code == MSGF_MENU && self) {
        const MSG* msg = reinterpret_cast<const MSG*>(lParam);
        switch (msg->message) {
        case WM_MOUSEMOVE: {
            // MSG::pt is the position stamped on THIS message; GetMessagePos
            // is stale inside the menu modal loop (its PM_NOREMOVE peeks skip
            // the refresh), which made switches fire one event late, not at
            // all, or on the wrong button.
            POINT pt = msg->pt;
            if (WindowFromPoint(pt) == self->m_toolbar) {
                POINT client = pt;
                ScreenToClient(self->m_toolbar, &client);
                const int hit = static_cast<int>(SendMessageW(
                    self->m_toolbar, TB_HITTEST, 0, reinterpret_cast<LPARAM>(&client)));
                if (hit >= 0 && hit < self->m_count && hit != self->m_active) {
                    self->m_pendingSwitch = hit;
                    EndMenu();
                }
            }
            break;
        }
        case WM_KEYDOWN:
            // Top-level navigation between the band's popups; deeper levels
            // keep the native submenu behavior.
            if (msg->wParam == VK_LEFT && self->m_depth <= 1) {
                self->m_pendingSwitch = (self->m_active + self->m_count - 1) % self->m_count;
                EndMenu();
                return 1;
            }
            if (msg->wParam == VK_RIGHT && self->m_depth <= 1 && !self->m_onSubmenuItem) {
                self->m_pendingSwitch = (self->m_active + 1) % self->m_count;
                EndMenu();
                return 1;
            }
            break;
        default:
            break;
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

LRESULT CALLBACK MenuBand::ToolbarSubclass(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                           UINT_PTR /*id*/, DWORD_PTR data) {
    auto* self = reinterpret_cast<MenuBand*>(data);
    switch (msg) {
    case kMsgTrack:
        self->TrackFromButton(static_cast<int>(wParam));
        return 0;
    case WM_MOUSEMOVE:
        // While a popup chain is open the hook owns hover switching; the only
        // moves that reach the toolbar are synthetic reposts with stale
        // positions, and letting it hot-track them leaves the PREVIOUS button
        // lit after a switch. The track loop sets the hot item itself.
        if (self->m_tracking)
            return 0;
        break;
    case WM_KEYDOWN:
        // Keyboard mode (band focused, no popup open yet).
        switch (wParam) {
        case VK_LEFT: {
            const int hot = static_cast<int>(SendMessageW(hwnd, TB_GETHOTITEM, 0, 0));
            SendMessageW(hwnd, TB_SETHOTITEM,
                         static_cast<WPARAM>((hot + self->m_count - 1) % self->m_count), 0);
            return 0;
        }
        case VK_RIGHT: {
            const int hot = static_cast<int>(SendMessageW(hwnd, TB_GETHOTITEM, 0, 0));
            SendMessageW(hwnd, TB_SETHOTITEM, static_cast<WPARAM>((hot + 1) % self->m_count),
                         0);
            return 0;
        }
        case VK_DOWN:
        case VK_RETURN: {
            const int hot = static_cast<int>(SendMessageW(hwnd, TB_GETHOTITEM, 0, 0));
            if (hot >= 0)
                self->TrackFromButton(hot);
            return 0;
        }
        case VK_ESCAPE:
            self->LeaveKeyboardMode();
            return 0;
        default: {
            // Mnemonic while the band is focused (no Alt needed).
            UINT cmd = 0;
            if (SendMessageW(hwnd, TB_MAPACCELERATOR, wParam, reinterpret_cast<LPARAM>(&cmd)) &&
                cmd >= static_cast<UINT>(kButtonIdFirst)) {
                self->TrackFromButton(static_cast<int>(cmd) - kButtonIdFirst);
                return 0;
            }
            break;
        }
        }
        break;
    case WM_SYSKEYDOWN:
        if (wParam == VK_MENU) { // second Alt tap leaves keyboard mode
            self->LeaveKeyboardMode();
            return 0;
        }
        break;
    case WM_KILLFOCUS:
        // Clicking elsewhere while armed: drop the hot highlight quietly.
        SendMessageW(hwnd, TB_SETHOTITEM, static_cast<WPARAM>(-1), 0);
        break;
    default:
        break;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}
