#include "MainWindow.h"
#include "framework.h"

#include <commctrl.h>
#include <shellapi.h>

int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int nCmdShow) {
    try {
        INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_TREEVIEW_CLASSES};
        InitCommonControlsEx(&icc);
        MainWindow::RegisterWindowClass(hInstance);
        PaneWindow::RegisterWindowClass(hInstance);

        std::wstring leftFile;
        std::wstring rightFile;
        int argc = 0;
        if (LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
            if (argc > 1)
                leftFile = argv[1];
            if (argc > 2)
                rightFile = argv[2];
            LocalFree(argv);
        }

        MainWindow window;
        if (!window.Create(hInstance, nCmdShow, std::move(leftFile), std::move(rightFile)))
            return 1;

        const ACCEL accels[] = {
            {FCONTROL | FVIRTKEY, 'O', IDC_OPEN_LEFT},
            {FCONTROL | FSHIFT | FVIRTKEY, 'O', IDC_OPEN_RIGHT},
            {FVIRTKEY, VK_TAB, IDC_FOCUS_NEXT_PANE},
            {FVIRTKEY, VK_F7, IDC_TOGGLE_SCROLL_SYNC},
            {FCONTROL | FVIRTKEY, VK_F7, IDC_TOGGLE_ZOOM_SYNC},
            {FCONTROL | FVIRTKEY, 'F', IDC_FIND_SHOW},
            {FVIRTKEY, VK_F3, IDC_FIND_NEXT},
            {FSHIFT | FVIRTKEY, VK_F3, IDC_FIND_PREV},
            {FVIRTKEY, VK_F9, IDC_TOGGLE_OUTLINE},
        };
        HACCEL haccel =
            CreateAcceleratorTableW(const_cast<ACCEL*>(accels), ARRAYSIZE(accels));

        MSG msg{};
        while (GetMessageW(&msg, nullptr, 0, 0)) {
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
        MessageBoxA(nullptr, e.what(), "PdfSideViewer - fatal error", MB_ICONERROR);
        return 1;
    }
}
