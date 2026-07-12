#include "MainWindow.h"
#include "framework.h"
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

// Hand the forward search to the running instance. A nonzero exit code makes
// the failure visible to the caller (LaTeX Workshop) instead of silent.
bool SendForwardSearch(HWND target, const ForwardSearchRequest& req) {
    ForwardSearchBlob blob{};
    blob.line = static_cast<uint32_t>(req.line);
    blob.texLen = static_cast<uint32_t>(req.tex.size());
    blob.pdfLen = static_cast<uint32_t>(req.pdf.size());
    std::vector<BYTE> payload(sizeof(blob) +
                              (req.tex.size() + req.pdf.size()) * sizeof(wchar_t));
    memcpy(payload.data(), &blob, sizeof(blob));
    memcpy(payload.data() + sizeof(blob), req.tex.data(), req.tex.size() * sizeof(wchar_t));
    memcpy(payload.data() + sizeof(blob) + req.tex.size() * sizeof(wchar_t), req.pdf.data(),
           req.pdf.size() * sizeof(wchar_t));

    COPYDATASTRUCT cds{};
    cds.dwData = kCdForwardSearch;
    cds.cbData = static_cast<DWORD>(payload.size());
    cds.lpData = payload.data();

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

// Hand an Explorer-verb open ("-open-left/right FILE") to the running
// instance; the caller cold-starts on failure so the verb always lands.
bool SendOpenDocument(HWND target, bool rightPane, const std::wstring& path) {
    OpenDocumentBlob blob{};
    blob.side = rightPane ? 1u : 0u;
    blob.pathLen = static_cast<uint32_t>(path.size());
    std::vector<BYTE> payload(sizeof(blob) + path.size() * sizeof(wchar_t));
    memcpy(payload.data(), &blob, sizeof(blob));
    memcpy(payload.data() + sizeof(blob), path.data(), path.size() * sizeof(wchar_t));

    COPYDATASTRUCT cds{};
    cds.dwData = kCdOpenDocument;
    cds.cbData = static_cast<DWORD>(payload.size());
    cds.lpData = payload.data();

    DWORD pid = 0;
    GetWindowThreadProcessId(target, &pid);
    AllowSetForegroundWindow(pid);

    DWORD_PTR handled = 0;
    return SendMessageTimeoutW(target, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&cds),
                               SMTO_ABORTIFHUNG, 5000, &handled) != 0 &&
           handled != 0;
}

} // namespace

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int nCmdShow) {
    try {
        INITCOMMONCONTROLSEX icc{sizeof(icc),
                                 ICC_TREEVIEW_CLASSES | ICC_BAR_CLASSES | ICC_COOL_CLASSES |
                                 ICC_LISTVIEW_CLASSES};
        InitCommonControlsEx(&icc);
        MainWindow::RegisterWindowClass(hInstance);
        PaneWindow::RegisterWindowClass(hInstance);

        std::wstring leftFile;
        std::wstring rightFile;
        std::optional<ForwardSearchRequest> forward;
        int argc = 0;
        if (LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
            std::wstring first = argc > 1 ? argv[1] : L"";
            while (!first.empty() && first.front() == L'-')
                first.erase(first.begin());
            if (lstrcmpiW(first.c_str(), L"register-shell") == 0 ||
                lstrcmpiW(first.c_str(), L"unregister-shell") == 0) {
                // Headless: used by the Options dialog docs, scripts and
                // uninstall instructions. Labels are written in the default
                // (English) language on this path.
                const bool ok = lstrcmpiW(first.c_str(), L"register-shell") == 0
                                    ? ShellIntegration::Register()
                                    : ShellIntegration::Unregister();
                LocalFree(argv);
                return ok ? 0 : 1;
            }
            if (argc >= 3 && (lstrcmpiW(first.c_str(), L"open-left") == 0 ||
                              lstrcmpiW(first.c_str(), L"open-right") == 0)) {
                // Explorer context-menu verbs. Reuse a running instance via
                // WM_COPYDATA; otherwise fall through to a cold start with
                // the file on that side (a verb must always land somewhere).
                const bool right = lstrcmpiW(first.c_str(), L"open-right") == 0;
                std::wstring path = Absolutize(argv[2]);
                if (!path.empty()) {
                    if (HWND running = FindWindowW(MainWindow::kClassName, nullptr)) {
                        if (SendOpenDocument(running, right, path)) {
                            LocalFree(argv);
                            return 0;
                        }
                    }
                    (right ? rightFile : leftFile) = std::move(path);
                }
            } else if (argc >= 5 && lstrcmpiW(first.c_str(), L"forward-search") == 0) {
                // PdfSideViewer.exe -forward-search TEX LINE PDF (SumatraPDF
                // argument order, what LaTeX Workshop templates expect).
                ForwardSearchRequest req;
                req.tex = Absolutize(argv[2]);
                req.line = _wtoi(argv[3]);
                req.pdf = Absolutize(argv[4]);
                if (req.line >= 1 && !req.tex.empty() && !req.pdf.empty()) {
                    if (HWND running = FindWindowW(MainWindow::kClassName, nullptr)) {
                        const bool ok = SendForwardSearch(running, req);
                        LocalFree(argv);
                        return ok ? 0 : 1;
                    }
                    forward = std::move(req); // cold start: park it
                }
            } else {
                if (argc > 1)
                    leftFile = argv[1];
                if (argc > 2)
                    rightFile = argv[2];
            }
            LocalFree(argv);
        }

        MainWindow window;
        if (!window.Create(hInstance, nCmdShow, std::move(leftFile), std::move(rightFile),
                           std::move(forward)))
            return 1;

        const ACCEL accels[] = {
            {FCONTROL | FVIRTKEY, 'O', IDC_OPEN_LEFT},
            {FCONTROL | FSHIFT | FVIRTKEY, 'O', IDC_OPEN_RIGHT},
            {FCONTROL | FVIRTKEY, 'W', IDC_CLOSE_DOC},
            {FVIRTKEY, VK_TAB, IDC_FOCUS_NEXT_PANE},
            {FVIRTKEY, VK_F7, IDC_TOGGLE_SCROLL_SYNC},
            {FCONTROL | FVIRTKEY, VK_F7, IDC_TOGGLE_ZOOM_SYNC},
            {FSHIFT | FVIRTKEY, VK_F7, IDC_ADD_SYNC_POINT},
            {FCONTROL | FSHIFT | FVIRTKEY, VK_F7, IDC_CLEAR_SYNC_POINTS},
            {FCONTROL | FVIRTKEY, 'F', IDC_FIND_SHOW},
            {FCONTROL | FVIRTKEY, 'G', IDC_GOTO_PAGE},
            {FVIRTKEY, VK_F8, IDC_SWAP_PANES},
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
