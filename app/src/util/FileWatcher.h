#pragma once

#include "framework.h"

#include <thread>

// Watches ONE file for changes by monitoring its parent directory on a
// dedicated thread (ReadDirectoryChangesW) and posting `msg` to `notify`
// whenever that file is written, created or renamed into place. The receiver
// owns all policy (debounce, stability probing, the actual reload); the
// watcher only reports. The thread touches Win32 file APIs and PostMessage
// exclusively: never MuPDF, never Direct2D.
class FileWatcher {
public:
    FileWatcher() = default;
    ~FileWatcher() { Stop(); }
    FileWatcher(const FileWatcher&) = delete;
    FileWatcher& operator=(const FileWatcher&) = delete;

    // Replaces any current watch; the path is absolutized internally.
    void Watch(HWND notify, UINT msg, const std::wstring& filePath);
    void Stop();

private:
    void ThreadProc(std::wstring dir, std::wstring fileName);

    std::thread m_thread;
    HANDLE m_stopEvent = nullptr; // manual-reset; signalled by Stop()
    HWND m_notify = nullptr;
    UINT m_msg = 0;
};
