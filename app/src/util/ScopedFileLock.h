#pragma once

#include "framework.h"

// Serializes per-USER mutations (the HKCU shell verbs, the settings file)
// through an exclusively-opened lock FILE, instead of a named kernel mutex.
// The file wins on every axis the mutex lost on: the profile directory's ACL
// already scopes it to the user (no SID derivation, and no way for another
// local principal to pre-create a predictable global name and wedge us -
// CreateMutexW's documented hijack caveat), and a file path crosses logon
// sessions, so an RDP second session of the same account contends on the same
// lock as the console one.
//
// Acquisition = CreateFileW with no sharing, on a lock file that is created
// once and then KEPT: the handle is the lock, not the file's existence. No
// delete-on-close, which would (a) make CreateFileW's symlink following a
// deletion primitive over the link's target and (b) put every contender
// through a delete-pending window where the open reports ACCESS_DENIED. What
// remains is a single transient error - ERROR_SHARING_VIOLATION, the holder
// has it open - and it is the only one worth retrying. Everything else
// (missing directory, ACL denial, a reparse obstacle) is PERMANENT and
// returns at once instead of stalling the UI thread for the whole timeout.
// The timeout bounds the RETRY LOOP, not wall clock: CreateFileW is
// synchronous, so a redirected profile on unreachable storage can block
// inside a single attempt for as long as the redirector takes.
//
// Callers decide what a failed acquisition means: mutations of a shared
// resource must FAIL CLOSED on !Acquired(), while a writer whose correctness
// does not depend on the lock (AppSettings::Save, atomic by construction) may
// proceed unserialized.
class ScopedFileLock {
public:
    explicit ScopedFileLock(const std::wstring& lockPath, DWORD timeoutMs = 5000) {
        if (lockPath.empty())
            return;
        const ULONGLONG deadline = GetTickCount64() + timeoutMs;
        for (;;) {
            // OPEN_REPARSE_POINT: a lock file replaced by a symlink must not
            // silently redirect the exclusive open onto whatever it names.
            m_file = CreateFileW(lockPath.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                                 FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            if (m_file != INVALID_HANDLE_VALUE || GetLastError() != ERROR_SHARING_VIOLATION ||
                GetTickCount64() >= deadline)
                return;
            Sleep(50);
        }
    }
    ~ScopedFileLock() {
        if (m_file != INVALID_HANDLE_VALUE)
            CloseHandle(m_file); // the file stays; the exclusive handle was the lock
    }
    ScopedFileLock(const ScopedFileLock&) = delete;
    ScopedFileLock& operator=(const ScopedFileLock&) = delete;

    bool Acquired() const { return m_file != INVALID_HANDLE_VALUE; }

private:
    HANDLE m_file = INVALID_HANDLE_VALUE;
};
