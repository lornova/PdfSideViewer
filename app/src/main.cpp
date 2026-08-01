#include "MainWindow.h"
#include "framework.h"
#include "util/IpcXml.h"
#include "util/ShellIntegration.h"

#include <commctrl.h>
#include <shellapi.h>

#include <vector>

namespace {

std::wstring Absolutize(PCWSTR path) {
    wchar_t full[1024];
    const DWORD n = GetFullPathNameW(path, ARRAYSIZE(full), full, nullptr);
    return (n == 0 || n >= ARRAYSIZE(full)) ? std::wstring() : std::wstring(full, n);
}

// One send for every XML IPC payload (util/IpcXml). An empty payload (builder
// failure) reads as unhandled, so the caller's cold-start fallback kicks in.
bool SendIpcPayload(HWND target, const std::vector<BYTE>& payload) {
    if (payload.empty())
        return false;
    COPYDATASTRUCT cds{};
    cds.dwData = IpcXml::kCopyDataId;
    cds.cbData = static_cast<DWORD>(payload.size());
    cds.lpData = const_cast<BYTE*>(payload.data());

    // Grant the receiver the right to come to the foreground when it flashes
    // the target position.
    DWORD pid = 0;
    GetWindowThreadProcessId(target, &pid);
    AllowSetForegroundWindow(pid);

    DWORD_PTR handled = 0;
    return SendMessageTimeoutW(target, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&cds),
                               SMTO_ABORTIFHUNG, 5000, &handled) != 0 &&
           handled != 0;
}

// Hand the forward search to the running instance; like the open verbs, the
// caller cold-starts on failure, so the request always lands somewhere.
bool SendForwardSearch(HWND target, const ForwardSearchRequest& req) {
    return SendIpcPayload(target, IpcXml::BuildForward(req.tex, req.line, req.pdf));
}

// Hand an Explorer-verb open ("-open-left/right/center FILE") to the running
// instance; the caller cold-starts on failure so the verb always lands.
bool SendOpenDocument(HWND target, int slot, const std::wstring& path) {
    return SendIpcPayload(target, IpcXml::BuildOpen(slot, path));
}

} // namespace

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int nCmdShow) {
    try {
        // Launched FROM VS Code (its debug launcher, or an extension such as
        // LaTeX Workshop starting an external viewer) this process inherits
        // ELECTRON_RUN_AS_NODE=1. Every process we start inherits our block in
        // turn - including the one the shell starts for a "vscode://" inverse
        // search - and Electron reading that variable boots as plain Node,
        // rejects "--open-url" and exits: Ctrl+click silently does nothing, and
        // ShellExecuteW cannot report it because the process DID start. The
        // variable means nothing to us, so drop it before anything can be
        // spawned. Only this one: the VSCODE_* variables are harmless (verified
        // by clearing this one alone and watching the handler go from exit 9,
        // "bad option: --open-url", to exit 0).
        SetEnvironmentVariableW(L"ELECTRON_RUN_AS_NODE", nullptr);
        INITCOMMONCONTROLSEX icc{sizeof(icc),
                                 ICC_TREEVIEW_CLASSES | ICC_BAR_CLASSES | ICC_COOL_CLASSES |
                                 ICC_LISTVIEW_CLASSES};
        InitCommonControlsEx(&icc);
        MainWindow::RegisterWindowClass(hInstance);
        PaneWindow::RegisterWindowClass(hInstance);

        // Indexed by PaneSlot. The POSITIONAL order is Beyond Compare's
        // (left, right, then center), which is why it needs a table: it is
        // deliberately not the visual left-to-right order.
        constexpr int kCliSlotOrder[] = {kSlotLeft, kSlotRight, kSlotCenter};
        PerPane<std::wstring> files;
        std::optional<ForwardSearchRequest> forward;
        int argc = 0;
        if (LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
            std::wstring first = argc > 1 ? argv[1] : L"";
            while (!first.empty() && first.front() == L'-')
                first.erase(first.begin());
            if (lstrcmpiW(first.c_str(), L"register-shell") == 0 ||
                lstrcmpiW(first.c_str(), L"unregister-shell") == 0 ||
                lstrcmpiW(first.c_str(), L"unregister-shell-owned") == 0) {
                // Headless: used by the Options dialog docs, scripts and the
                // uninstaller. Labels are written in the default (English)
                // language on this path. The -owned variant removes only the
                // verbs whose command launches THIS exe (the uninstaller's
                // scope), under the same per-user lock file as the app.
                const bool ok =
                    lstrcmpiW(first.c_str(), L"register-shell") == 0
                        ? ShellIntegration::Register()
                        : lstrcmpiW(first.c_str(), L"unregister-shell") == 0
                              ? ShellIntegration::Unregister()
                              : ShellIntegration::UnregisterOwned();
                LocalFree(argv);
                return ok ? 0 : 1;
            }
            // The switches are recognized by NAME, before any arity check: a
            // missing operand must degrade to a plain cold start, never fall
            // through to positional parsing where the switch itself would be
            // opened as the left "PDF".
            if (lstrcmpiW(first.c_str(), L"open-left") == 0 ||
                lstrcmpiW(first.c_str(), L"open-right") == 0 ||
                lstrcmpiW(first.c_str(), L"open-center") == 0) {
                // Explorer context-menu verbs. Reuse a running instance via
                // WM_COPYDATA; otherwise fall through to a cold start with
                // the file on that side (a verb must always land somewhere).
                const int slot = lstrcmpiW(first.c_str(), L"open-right") == 0  ? kSlotRight
                                 : lstrcmpiW(first.c_str(), L"open-center") == 0 ? kSlotCenter
                                                                                 : kSlotLeft;
                std::wstring path = argc >= 3 ? Absolutize(argv[2]) : std::wstring();
                if (!path.empty()) {
                    if (HWND running = FindWindowW(MainWindow::kClassName, nullptr)) {
                        if (SendOpenDocument(running, slot, path)) {
                            LocalFree(argv);
                            return 0;
                        }
                    }
                    files[static_cast<size_t>(slot)] = std::move(path);
                }
            } else if (lstrcmpiW(first.c_str(), L"forward-search") == 0) {
                // PdfSideViewer.exe -forward-search TEX LINE PDF (SumatraPDF
                // argument order, what LaTeX Workshop templates expect).
                if (argc >= 5) {
                    ForwardSearchRequest req;
                    req.tex = Absolutize(argv[2]);
                    req.line = _wtoi(argv[3]);
                    req.pdf = Absolutize(argv[4]);
                    if (req.line >= 1 && !req.tex.empty() && !req.pdf.empty()) {
                        if (HWND running = FindWindowW(MainWindow::kClassName, nullptr)) {
                            if (SendForwardSearch(running, req)) {
                                LocalFree(argv);
                                return 0;
                            }
                        }
                        // No receiver, or the handoff failed (hung instance,
                        // unknown protocol version across a mixed-version
                        // pair): cold-start with the request parked, so
                        // forward search degrades to a new window exactly
                        // like the open verbs instead of dying with an exit
                        // code the editor can only surface as a generic
                        // error.
                        forward = std::move(req);
                    }
                }
            } else {
                for (int i = 0; i < static_cast<int>(std::size(kCliSlotOrder)); ++i)
                    if (argc > i + 1)
                        files[static_cast<size_t>(kCliSlotOrder[i])] = argv[i + 1];
            }
            LocalFree(argv);
        }

        MainWindow window;
        if (!window.Create(hInstance, nCmdShow, std::move(files), std::move(forward)))
            return 1;

        const ACCEL accels[] = {
            {FCONTROL | FVIRTKEY, 'O', IDC_OPEN_LEFT},
            {FCONTROL | FSHIFT | FVIRTKEY, 'O', IDC_OPEN_RIGHT},
            // NEVER Ctrl+Alt+letter: AltGr IS Ctrl+Alt, and among the UI
            // languages several layouts type letters with it (Polish AltGr+O
            // = o-acute) - the accelerator would eat ordinary text input.
            {FCONTROL | FSHIFT | FVIRTKEY, 'M', IDC_OPEN_CENTER},
            {FCONTROL | FVIRTKEY, 'W', IDC_CLOSE_DOC},
            {FVIRTKEY, VK_TAB, IDC_FOCUS_NEXT_PANE},
            {FVIRTKEY, VK_F7, IDC_TOGGLE_SCROLL_SYNC},
            {FCONTROL | FVIRTKEY, VK_F7, IDC_TOGGLE_ZOOM_SYNC},
            {FSHIFT | FVIRTKEY, VK_F7, IDC_ADD_SYNC_POINT},
            {FCONTROL | FSHIFT | FVIRTKEY, VK_F7, IDC_CLEAR_SYNC_POINTS},
            {FCONTROL | FVIRTKEY, 'F', IDC_FIND_SHOW},
            {FCONTROL | FVIRTKEY, 'G', IDC_GOTO_PAGE},
            {FVIRTKEY, VK_F8, IDC_SWAP_PANES},
            {FSHIFT | FVIRTKEY, VK_F8, IDC_SWAP_PANES_BACK},
            {FVIRTKEY, VK_F3, IDC_FIND_NEXT},
            {FSHIFT | FVIRTKEY, VK_F3, IDC_FIND_PREV},
            {FVIRTKEY, VK_F9, IDC_TOGGLE_OUTLINE},
            {FVIRTKEY, VK_F11, IDC_FULLSCREEN},
            {FALT | FVIRTKEY, VK_RETURN, IDC_FULLSCREEN},
        };
        HACCEL haccel =
            CreateAcceleratorTableW(const_cast<ACCEL*>(accels), ARRAYSIZE(accels));

        MSG msg{};
        while (GetMessageW(&msg, nullptr, 0, 0)) {
            // Esc leaves full screen, but the find bar keeps its Esc (its
            // subclass closes the bar). No VK_ESCAPE accelerator: it would
            // also steal the pane-local Esc that clears text selections.
            if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE &&
                window.IsFullScreen() && !window.FindBarHasFocus()) {
                PostMessageW(window.Hwnd(), WM_COMMAND, IDC_FULLSCREEN, 0);
                continue;
            }
            // Tab inside the find bar cycles its WS_TABSTOP controls instead
            // of firing IDC_FOCUS_NEXT_PANE. VK_TAB only: a blanket
            // IsDialogMessage would eat the Enter/Esc the find box handles.
            if (msg.message == WM_KEYDOWN && msg.wParam == VK_TAB &&
                window.FindBarHasFocus() && IsDialogMessageW(window.FindBarHwnd(), &msg))
                continue;
            if (!haccel || !TranslateAcceleratorW(window.Hwnd(), haccel, &msg)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
        if (haccel)
            DestroyAcceleratorTable(haccel);
        return static_cast<int>(msg.wParam);
    } catch (const std::exception& e) {
        MessageBoxA(nullptr, e.what(), "PDF Side Viewer - fatal error", MB_ICONERROR);
        return 1;
    }
}
