#include "FileWatcher.h"

namespace {

// FILE_NOTIFY_INFORMATION names are counted, not null-terminated.
bool SameName(const std::wstring& name, const wchar_t* other, DWORD otherBytes) {
    return CompareStringOrdinal(name.c_str(), static_cast<int>(name.size()), other,
                                static_cast<int>(otherBytes / sizeof(wchar_t)),
                                TRUE) == CSTR_EQUAL;
}

} // namespace

void FileWatcher::Watch(HWND notify, UINT msg, const std::wstring& filePath) {
    Stop();

    wchar_t full[1024];
    const DWORD n = GetFullPathNameW(filePath.c_str(), ARRAYSIZE(full), full, nullptr);
    if (n == 0 || n >= ARRAYSIZE(full))
        return;
    const std::wstring absolute(full, n);
    const size_t sep = absolute.find_last_of(L"\\/");
    if (sep == std::wstring::npos)
        return;

    m_notify = notify;
    m_msg = msg;
    m_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!m_stopEvent)
        return;
    m_thread = std::thread(&FileWatcher::ThreadProc, this, absolute.substr(0, sep),
                           absolute.substr(sep + 1));
}

void FileWatcher::Stop() {
    if (m_stopEvent)
        SetEvent(m_stopEvent);
    if (m_thread.joinable())
        m_thread.join();
    if (m_stopEvent) {
        CloseHandle(m_stopEvent);
        m_stopEvent = nullptr;
    }
}

void FileWatcher::ThreadProc(std::wstring dir, std::wstring fileName) {
    // Watching the DIRECTORY, not the file: LaTeX toolchains delete, recreate
    // and rename their output, and a handle to the file itself would go stale
    // on the first such cycle.
    const HANDLE dirHandle =
        CreateFileW(dir.c_str(), FILE_LIST_DIRECTORY,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                    OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
    if (dirHandle == INVALID_HANDLE_VALUE)
        return; // e.g. detached network share: degrade to no auto-reload

    const HANDLE ioEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ioEvent) {
        CloseHandle(dirHandle);
        return;
    }

    alignas(DWORD) BYTE buffer[16 * 1024];
    for (;;) {
        OVERLAPPED ov{};
        ov.hEvent = ioEvent;
        ResetEvent(ioEvent);
        if (!ReadDirectoryChangesW(dirHandle, buffer, sizeof(buffer), FALSE,
                                   FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE |
                                       FILE_NOTIFY_CHANGE_FILE_NAME |
                                       FILE_NOTIFY_CHANGE_CREATION,
                                   nullptr, &ov, nullptr))
            break;

        const HANDLE waits[2] = {m_stopEvent, ioEvent};
        const DWORD which = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
        if (which != WAIT_OBJECT_0 + 1) { // stop requested (or wait failure)
            CancelIoEx(dirHandle, &ov);
            DWORD ignored = 0;
            GetOverlappedResult(dirHandle, &ov, &ignored, TRUE);
            break;
        }

        DWORD bytes = 0;
        if (!GetOverlappedResult(dirHandle, &ov, &bytes, FALSE))
            break;

        bool changed = false;
        if (bytes == 0) {
            // Notification buffer overflowed: assume the file changed rather
            // than silently missing a rebuild.
            changed = true;
        } else {
            const BYTE* p = buffer;
            for (;;) {
                const auto* info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(p);
                if (SameName(fileName, info->FileName, info->FileNameLength)) {
                    changed = true;
                    break;
                }
                if (info->NextEntryOffset == 0)
                    break;
                p += info->NextEntryOffset;
            }
        }
        if (changed)
            PostMessageW(m_notify, m_msg, 0, 0);
    }

    CloseHandle(ioEvent);
    CloseHandle(dirHandle);
}
