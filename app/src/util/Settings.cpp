#include "util/Settings.h"

namespace {

constexpr PCWSTR kWindowSection = L"window";
constexpr PCWSTR kSyncSection = L"sync";
constexpr PCWSTR kMruFilesSection = L"mru-files";
constexpr PCWSTR kMruPairsSection = L"mru-pairs";
constexpr PCWSTR kSynctexSection = L"synctex";

// The multi-key INI write is not atomic as a group; two instances closing
// together would interleave into a hybrid session. Serialize per user.
class SettingsLock {
public:
    SettingsLock() {
        m_mutex = CreateMutexW(nullptr, FALSE, L"Local\\PdfSideViewer.settings");
        if (m_mutex)
            WaitForSingleObject(m_mutex, 5000); // WAIT_ABANDONED still grants ownership
    }
    ~SettingsLock() {
        if (m_mutex) {
            ReleaseMutex(m_mutex);
            CloseHandle(m_mutex);
        }
    }
    SettingsLock(const SettingsLock&) = delete;
    SettingsLock& operator=(const SettingsLock&) = delete;

private:
    HANDLE m_mutex = nullptr;
};

std::wstring SettingsPath() {
    wchar_t appData[MAX_PATH];
    const DWORD len = GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH);
    if (len == 0 || len >= MAX_PATH)
        return {};
    std::wstring dir = std::wstring(appData) + L"\\PdfSideViewer";
    CreateDirectoryW(dir.c_str(), nullptr); // ok if it already exists
    return dir + L"\\settings.ini";
}

// WritePrivateProfileStringW writes ANSI unless the file already exists as
// UTF-16LE; seed it with a BOM so Unicode paths round-trip.
void EnsureUtf16File(const std::wstring& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return; // already exists (or not creatable; Save degrades gracefully)
    const WORD bom = 0xFEFF;
    DWORD written = 0;
    WriteFile(file, &bom, sizeof(bom), &written, nullptr);
    CloseHandle(file);
}

std::wstring ReadString(const std::wstring& file, PCWSTR section, PCWSTR key) {
    wchar_t buffer[2048];
    GetPrivateProfileStringW(section, key, L"", buffer, ARRAYSIZE(buffer), file.c_str());
    return buffer;
}

float ReadFloat(const std::wstring& file, PCWSTR section, PCWSTR key, float fallback) {
    const std::wstring s = ReadString(file, section, key);
    if (s.empty())
        return fallback;
    wchar_t* end = nullptr;
    const float value = std::wcstof(s.c_str(), &end);
    return end != s.c_str() ? value : fallback;
}

int ReadInt(const std::wstring& file, PCWSTR section, PCWSTR key, int fallback) {
    return static_cast<int>(
        GetPrivateProfileIntW(section, key, fallback, file.c_str()));
}

void WriteString(const std::wstring& file, PCWSTR section, PCWSTR key, const std::wstring& v) {
    WritePrivateProfileStringW(section, key, v.c_str(), file.c_str());
}

void WriteFloat(const std::wstring& file, PCWSTR section, PCWSTR key, float v) {
    wchar_t buffer[64];
    swprintf_s(buffer, L"%.4f", v);
    WriteString(file, section, key, buffer);
}

void WriteInt(const std::wstring& file, PCWSTR section, PCWSTR key, int v) {
    wchar_t buffer[32];
    swprintf_s(buffer, L"%d", v);
    WriteString(file, section, key, buffer);
}

PaneSettings LoadPane(const std::wstring& file, PCWSTR section) {
    PaneSettings pane;
    pane.path = ReadString(file, section, L"path");
    pane.zoom = ReadFloat(file, section, L"zoom", 1.0f);
    pane.scrollX = ReadFloat(file, section, L"scrollX", 0);
    pane.scrollY = ReadFloat(file, section, L"scrollY", 0);
    pane.zoomMode = std::clamp(ReadInt(file, section, L"zoomMode", 2), 0, 2);
    return pane;
}

void SavePane(const std::wstring& file, PCWSTR section, const PaneSettings& pane) {
    WriteString(file, section, L"path", pane.path);
    WriteFloat(file, section, L"zoom", pane.zoom);
    WriteFloat(file, section, L"scrollX", pane.scrollX);
    WriteFloat(file, section, L"scrollY", pane.scrollY);
    WriteInt(file, section, L"zoomMode", pane.zoomMode);
}

} // namespace

AppSettings AppSettings::Load() {
    AppSettings s;
    const std::wstring file = SettingsPath();
    if (file.empty() || GetFileAttributesW(file.c_str()) == INVALID_FILE_ATTRIBUTES)
        return s;

    SettingsLock lock;
    s.hasPlacement = ReadInt(file, kWindowSection, L"hasPlacement", 0) != 0;
    s.normalRect.left = ReadInt(file, kWindowSection, L"x", 0);
    s.normalRect.top = ReadInt(file, kWindowSection, L"y", 0);
    s.normalRect.right = s.normalRect.left + ReadInt(file, kWindowSection, L"w", 0);
    s.normalRect.bottom = s.normalRect.top + ReadInt(file, kWindowSection, L"h", 0);
    s.maximized = ReadInt(file, kWindowSection, L"maximized", 0) != 0;
    s.splitRatio = std::clamp(ReadFloat(file, kWindowSection, L"splitRatio", 0.5f), 0.1f, 0.9f);
    s.dpi = static_cast<UINT>(std::max(1, ReadInt(file, kWindowSection, L"dpi", 96)));
    s.toolbar = ReadInt(file, kWindowSection, L"toolbar", 1) != 0;
    s.statusbar = ReadInt(file, kWindowSection, L"statusbar", 1) != 0;
    s.outline = ReadInt(file, kWindowSection, L"outline", 0) != 0;
    s.language = ReadString(file, kWindowSection, L"language");
    if (s.language.empty())
        s.language = L"en";
    s.scrollSync = ReadInt(file, kSyncSection, L"scroll", 1) != 0;
    s.zoomSync = ReadInt(file, kSyncSection, L"zoom", 1) != 0;
    s.scrollMode = std::clamp(ReadInt(file, kWindowSection, L"scrollMode", 0), 0, 1);
    {
        std::wstring inverse = ReadString(file, kSynctexSection, L"inverse");
        if (!inverse.empty())
            s.synctexInverse = std::move(inverse); // empty/missing keeps the default
    }
    for (size_t i = 0; i < kMruMaxEntries; ++i) {
        std::wstring f =
            ReadString(file, kMruFilesSection, (L"file" + std::to_wstring(i)).c_str());
        if (f.empty())
            break;
        s.mruFiles.push_back(std::move(f));
    }
    for (size_t i = 0; i < kMruMaxEntries; ++i) {
        std::wstring l =
            ReadString(file, kMruPairsSection, (L"left" + std::to_wstring(i)).c_str());
        std::wstring r =
            ReadString(file, kMruPairsSection, (L"right" + std::to_wstring(i)).c_str());
        if (l.empty() || r.empty())
            break;
        s.mruPairs.push_back({std::move(l), std::move(r)});
    }
    s.left = LoadPane(file, L"left");
    s.right = LoadPane(file, L"right");
    return s;
}

void AppSettings::Save() const {
    const std::wstring file = SettingsPath();
    if (file.empty())
        return;
    SettingsLock lock;
    EnsureUtf16File(file);

    WriteInt(file, kWindowSection, L"hasPlacement", hasPlacement ? 1 : 0);
    WriteInt(file, kWindowSection, L"x", normalRect.left);
    WriteInt(file, kWindowSection, L"y", normalRect.top);
    WriteInt(file, kWindowSection, L"w", normalRect.right - normalRect.left);
    WriteInt(file, kWindowSection, L"h", normalRect.bottom - normalRect.top);
    WriteInt(file, kWindowSection, L"maximized", maximized ? 1 : 0);
    WriteFloat(file, kWindowSection, L"splitRatio", splitRatio);
    WriteInt(file, kWindowSection, L"dpi", static_cast<int>(dpi));
    WriteInt(file, kWindowSection, L"toolbar", toolbar ? 1 : 0);
    WriteInt(file, kWindowSection, L"statusbar", statusbar ? 1 : 0);
    WriteInt(file, kWindowSection, L"outline", outline ? 1 : 0);
    WriteString(file, kWindowSection, L"language", language);
    WriteInt(file, kWindowSection, L"scrollMode", scrollMode);
    WriteInt(file, kSyncSection, L"scroll", scrollSync ? 1 : 0);
    WriteInt(file, kSyncSection, L"zoom", zoomSync ? 1 : 0);
    WriteString(file, kSynctexSection, L"inverse", synctexInverse);
    // Slots past the current size are deleted (nullptr value removes the key)
    // so a shrunken list leaves no stale tail behind.
    for (size_t i = 0; i < kMruMaxEntries; ++i) {
        const std::wstring key = L"file" + std::to_wstring(i);
        if (i < mruFiles.size())
            WriteString(file, kMruFilesSection, key.c_str(), mruFiles[i]);
        else
            WritePrivateProfileStringW(kMruFilesSection, key.c_str(), nullptr, file.c_str());
    }
    for (size_t i = 0; i < kMruMaxEntries; ++i) {
        const std::wstring leftKey = L"left" + std::to_wstring(i);
        const std::wstring rightKey = L"right" + std::to_wstring(i);
        if (i < mruPairs.size()) {
            WriteString(file, kMruPairsSection, leftKey.c_str(), mruPairs[i].left);
            WriteString(file, kMruPairsSection, rightKey.c_str(), mruPairs[i].right);
        } else {
            WritePrivateProfileStringW(kMruPairsSection, leftKey.c_str(), nullptr, file.c_str());
            WritePrivateProfileStringW(kMruPairsSection, rightKey.c_str(), nullptr,
                                       file.c_str());
        }
    }
    SavePane(file, L"left", left);
    SavePane(file, L"right", right);
}
