#include "util/Settings.h"

#include "util/ScopedFileLock.h"

#include <shlobj.h> // SHGetKnownFolderPath, FOLDERID_*

#include <cerrno>
#include <climits>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <map>
#include <optional>
#include <vector>

namespace {

constexpr PCWSTR kWindowSection = L"window";
constexpr PCWSTR kSyncSection = L"sync";
constexpr PCWSTR kDefaultsSection = L"defaults";
constexpr PCWSTR kMruFilesSection = L"mru-files";
constexpr PCWSTR kMruPairsSection = L"mru-pairs";
constexpr PCWSTR kSyncPointsSection = L"sync-points";
constexpr PCWSTR kSynctexSection = L"synctex";

// --------------------------------------------------------------- locations

// Dynamically sized: an environment value longer than a fixed buffer must NOT
// read as absent. A silently ignored PSV_SETTINGS_DIR would send a test suite
// at the user's REAL settings.ini, which is exactly how their MRU lists once
// got wiped.
std::wstring EnvVar(PCWSTR name) {
    const DWORD size = GetEnvironmentVariableW(name, nullptr, 0);
    if (size == 0)
        return {}; // unset: the caller decides what that means
    std::wstring value(size, L'\0');
    const DWORD len = GetEnvironmentVariableW(name, value.data(), size);
    if (len == 0 || len >= size)
        return {};
    value.resize(len);
    return value;
}

bool IsDirectory(const std::wstring& path) {
    const DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

// Reports whether the directory EXISTS afterwards: a failed creation must
// surface as "no settings directory", never as a path nothing can be written
// to.
bool EnsureDirectory(const std::wstring& path) {
    return CreateDirectoryW(path.c_str(), nullptr) != FALSE || IsDirectory(path);
}

// The account's real profile folder, resolved through the shell rather than
// the APPDATA/LOCALAPPDATA environment variables: those are process-local (a
// parent process can point them anywhere) while the known folder follows the
// account's actual, possibly redirected, profile.
std::wstring KnownFolder(REFKNOWNFOLDERID id) {
    PWSTR raw = nullptr;
    if (FAILED(SHGetKnownFolderPath(id, KF_FLAG_CREATE, nullptr, &raw)))
        return {};
    std::wstring path = raw ? raw : L"";
    CoTaskMemFree(raw);
    return path;
}

std::wstring SettingsPath() {
    const std::wstring dir = SettingsDirectory();
    return dir.empty() ? std::wstring() : dir + L"\\settings.ini";
}

// The writer lock for the settings FILE lives beside the file itself: that is
// the resource it guards, and two copies pointed at different directories
// write different files and must not contend. (The HKCU shell verbs are the
// opposite case - one resource per user, wherever the settings live - and use
// UserLockDirectory() instead.)
std::wstring SettingsLockPath() {
    const std::wstring dir = SettingsDirectory();
    return dir.empty() ? std::wstring() : dir + L"\\settings.lock";
}

// -------------------------------------------------------------- INI model

using IniSection = std::map<std::wstring, std::wstring>;
using IniData = std::map<std::wstring, IniSection>;

// Case-insensitive on ASCII, like the profile APIs this replaced (every
// section and key name in this file is an ASCII literal).
std::wstring FoldKey(const std::wstring& s) {
    std::wstring out = s;
    for (wchar_t& c : out)
        if (c >= L'A' && c <= L'Z')
            c = static_cast<wchar_t>(c - L'A' + L'a');
    return out;
}

std::wstring TrimAscii(const std::wstring& s) {
    const auto blank = [](wchar_t c) { return c == L' ' || c == L'\t'; };
    size_t begin = 0;
    size_t end = s.size();
    while (begin < end && blank(s[begin]))
        ++begin;
    while (end > begin && blank(s[end - 1]))
        --end;
    return s.substr(begin, end - begin);
}

// Reads the shape GetPrivateProfileString wrote, so every settings.ini this
// app has ever produced loads unchanged: ';' comments, [sections], key=value,
// values trimmed of spaces and tabs and unwrapped from ONE matched pair of
// quotes, first occurrence of a key wins.
// It is NOT a bit-exact reimplementation of the profile API, and only files
// the app itself wrote are guaranteed identical. Known differences on
// hand-edited input: repeated [sections] merge here (the API looks in the
// first one), a UTF-16BE file is not recognized (the API did not read it
// either - it came out as mojibake), the section name ends at the FIRST ']',
// and only spaces and tabs count as surrounding whitespace.
IniData ParseIni(const std::wstring& text) {
    IniData data;
    IniSection* current = nullptr;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t end = text.find_first_of(L"\r\n", pos);
        if (end == std::wstring::npos)
            end = text.size();
        const std::wstring line = TrimAscii(text.substr(pos, end - pos));
        pos = end + 1;
        if (line.empty() || line.front() == L';')
            continue;
        if (line.front() == L'[') {
            const size_t close = line.find(L']');
            if (close != std::wstring::npos)
                current = &data[FoldKey(TrimAscii(line.substr(1, close - 1)))];
            continue;
        }
        const size_t eq = line.find(L'=');
        if (eq == std::wstring::npos || !current)
            continue;
        std::wstring key = FoldKey(TrimAscii(line.substr(0, eq)));
        std::wstring value = TrimAscii(line.substr(eq + 1));
        if (value.size() >= 2 && (value.front() == L'"' || value.front() == L'\'') &&
            value.back() == value.front())
            value = value.substr(1, value.size() - 2);
        if (!key.empty())
            current->emplace(std::move(key), std::move(value)); // emplace: first wins
    }
    return data;
}

const std::wstring* Find(const IniData& ini, PCWSTR section, const std::wstring& key) {
    const auto s = ini.find(FoldKey(section));
    if (s == ini.end())
        return nullptr;
    const auto k = s->second.find(FoldKey(key));
    return k == s->second.end() ? nullptr : &k->second;
}

std::wstring ReadString(const IniData& ini, PCWSTR section, const std::wstring& key) {
    const std::wstring* value = Find(ini, section, key);
    return value ? *value : std::wstring();
}

int ReadInt(const IniData& ini, PCWSTR section, const std::wstring& key, int fallback) {
    const std::wstring* value = Find(ini, section, key);
    if (!value || value->empty())
        return fallback;
    wchar_t* end = nullptr;
    errno = 0;
    const long parsed = std::wcstol(value->c_str(), &end, 10);
    // Digits followed by junk keep the digits (GetPrivateProfileIntW's
    // documented behaviour); a value with no digits at all falls back to the
    // default instead of reading as 0, which is what the profile API did.
    // Out of range falls back too, rather than SATURATING at LONG_MAX: a
    // saturated coordinate is a plausible-looking number that then overflows
    // the arithmetic reconstructing the window rectangle.
    if (end == value->c_str() || errno == ERANGE ||
        parsed != static_cast<long>(static_cast<int>(parsed)))
        return fallback;
    return static_cast<int>(parsed);
}

float ReadFloat(const IniData& ini, PCWSTR section, const std::wstring& key, float fallback) {
    const std::wstring* value = Find(ini, section, key);
    if (!value || value->empty())
        return fallback;
    wchar_t* end = nullptr;
    const float parsed = std::wcstof(value->c_str(), &end);
    // wcstof parses "nan"/"inf", and NaN slides through std::clamp and every
    // range guard downstream (the unclamped zoom/scroll readers would poison
    // the layout): a hand-edited value must fall back here, centrally.
    return end != value->c_str() && std::isfinite(parsed) ? parsed : fallback;
}

// Builds the WHOLE file in memory, sections in insertion order. A key that
// should not exist is simply never written - the previous in-place writer had
// to delete keys one by one, and every one of those deletes was an unchecked
// call that could half-succeed.
class IniWriter {
public:
    void Set(PCWSTR section, const std::wstring& key, const std::wstring& value) {
        Section(section).push_back({key, Sanitize(value)});
    }
    void SetInt(PCWSTR section, const std::wstring& key, int value) {
        wchar_t buffer[32];
        swprintf_s(buffer, L"%d", value);
        Set(section, key, buffer);
    }
    void SetBool(PCWSTR section, const std::wstring& key, bool value) {
        SetInt(section, key, value ? 1 : 0);
    }
    void SetFloat(PCWSTR section, const std::wstring& key, float value) {
        wchar_t buffer[64];
        swprintf_s(buffer, L"%.4f", value);
        Set(section, key, buffer);
    }
    std::wstring Text() const {
        std::wstring out;
        for (const auto& section : m_sections) {
            if (section.second.empty())
                continue; // an empty section is an absent section
            out += L"[" + section.first + L"]\r\n";
            for (const auto& entry : section.second)
                out += entry.first + L"=" + entry.second + L"\r\n";
        }
        return out;
    }

private:
    using Entries = std::vector<std::pair<std::wstring, std::wstring>>;

    Entries& Section(PCWSTR name) {
        for (auto& section : m_sections)
            if (section.first == name)
                return section.second;
        m_sections.push_back({name, {}});
        return m_sections.back().second;
    }
    // A value can never contain a line break: it would inject a line into the
    // file. Paths cannot hold one and the SyncTeX template is a single-line
    // edit box, so this keeps the file well formed BY CONSTRUCTION rather
    // than by luck about what the inputs happen to allow.
    static std::wstring Sanitize(std::wstring value) {
        value.erase(std::remove_if(value.begin(), value.end(),
                                   [](wchar_t c) { return c < L' ' && c != L'\t'; }),
                    value.end());
        return value;
    }

    std::vector<std::pair<std::wstring, Entries>> m_sections;
};

// --------------------------------------------------------------- file I/O

// Shared by the reader and the writer, in bytes.
constexpr size_t kMaxSettingsBytes = 8u << 20;

// The whole file through ONE handle. Load must see a single coherent
// snapshot, and dozens of independent GetPrivateProfile* calls do not promise
// one: each is coherent alone, but a writer replacing the file between two of
// them hands back a HYBRID (a paneCount from the old file, its splitter ratios
// from the new one), which the next SaveSession would then make permanent.
// FILE_SHARE_DELETE so this read never blocks a concurrent atomic swap - the
// handle keeps referring to the file it opened, whatever happens to the name.
std::wstring ReadWholeFile(const std::wstring& path) {
    const HANDLE handle =
        CreateFileW(path.c_str(), GENERIC_READ,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return {};
    std::string bytes;
    LARGE_INTEGER size{};
    // A settings.ini is a few KB; whatever else may be sitting under that name
    // must not be slurped into memory wholesale. ONE ceiling shared with the
    // writer, in BYTES, so the largest file that can be written is never one
    // the reader then refuses.
    if (GetFileSizeEx(handle, &size) && size.QuadPart > 0 &&
        size.QuadPart <= static_cast<LONGLONG>(kMaxSettingsBytes)) {
        bytes.resize(static_cast<size_t>(size.QuadPart));
        size_t done = 0;
        while (done < bytes.size()) {
            DWORD read = 0;
            if (!ReadFile(handle, bytes.data() + done, static_cast<DWORD>(bytes.size() - done),
                          &read, nullptr) ||
                read == 0) {
                bytes.clear(); // a partial read is not a settings file
                break;
            }
            done += read;
        }
    }
    CloseHandle(handle);
    const auto byteAt = [&bytes](size_t i) { return static_cast<unsigned char>(bytes[i]); };
    if (bytes.size() >= 2 && byteAt(0) == 0xFF && byteAt(1) == 0xFE) {
        const size_t payload = bytes.size() - 2;
        if (payload % sizeof(wchar_t) != 0)
            return {}; // a truncated code unit: not a settings file this app wrote
        std::wstring text(payload / sizeof(wchar_t), L'\0');
        std::memcpy(text.data(), bytes.data() + 2, payload); // copied, not reinterpreted
        return text;
    }
    // BOM-less: an ANSI file from a build before the BOM seeding, or an
    // editor that re-saved it as UTF-8 (which used to turn every accented
    // path into mojibake, since the profile APIs read it as ANSI).
    UINT codePage = CP_ACP;
    size_t offset = 0;
    if (bytes.size() >= 3 && byteAt(0) == 0xEF && byteAt(1) == 0xBB && byteAt(2) == 0xBF) {
        codePage = CP_UTF8;
        offset = 3;
    }
    if (bytes.size() <= offset)
        return {};
    const int chars = MultiByteToWideChar(codePage, 0, bytes.data() + offset,
                                          static_cast<int>(bytes.size() - offset), nullptr, 0);
    if (chars <= 0)
        return {};
    std::wstring text(static_cast<size_t>(chars), L'\0');
    MultiByteToWideChar(codePage, 0, bytes.data() + offset,
                        static_cast<int>(bytes.size() - offset), text.data(), chars);
    return text;
}

// Highest counter a temp name can carry; cleanup rejects anything above it, so
// its grammar describes only names this app is able to produce.
constexpr unsigned long kMaxTempCounter = 63;

// A temp file with a name nothing else can hold: CREATE_NEW never opens an
// existing file, so a crashed save's leftover - or a REUSED process id - can
// be neither modified nor promoted over the real settings.
//
// SECURITY MODEL, stated because the promotion below depends on it: the
// settings DIRECTORY is the boundary, not the file. This temp is created with
// default security, so it carries the directory's inherited DACL, and the
// rename does not bring the old file's own descriptor across. A per-file DACL
// or per-file EFS state that somebody set BY HAND therefore does NOT survive a
// save. That is a deliberate choice, not an oversight:
//   - the app never sets either one; both are external actions on a file it
//     rewrites on every exit;
//   - the policy DELIBERATELY trusts every principal the directory lets create
//     files, and does not try to be stricter per file. (Windows does keep
//     FILE_ADD_FILE, the target's DELETE and the parent's FILE_DELETE_CHILD
//     separate, so a creator is not automatically able to replace a protected
//     file - the point is that this app does not attempt that distinction.)
//   - Microsoft's own EFS guidance is to encrypt the FOLDER precisely because
//     rename-based saves are how applications write files.
// The consequence is worth naming: settings are TRUSTED input ([synctex]
// inverse becomes a command line), so whoever can write settings.ini can run
// code as the user. Protecting the directory works and is inherited by every
// replacement; protecting only the file does not.
HANDLE CreateUniqueTemp(const std::wstring& base, std::wstring& path) {
    for (unsigned long n = 0; n <= kMaxTempCounter; ++n) {
        std::wstring candidate = base + L"." + std::to_wstring(GetCurrentProcessId()) + L"." +
                                 std::to_wstring(n) + L".tmp";
        const HANDLE handle = CreateFileW(candidate.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                          FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle != INVALID_HANDLE_VALUE) {
            path = std::move(candidate);
            return handle;
        }
        if (GetLastError() != ERROR_FILE_EXISTS)
            break; // not a name clash: creation genuinely failed
    }
    return INVALID_HANDLE_VALUE;
}

// Every step checked, exact byte counts required, flushed before the caller
// closes: a disk-full or I/O failure must never leave a truncated temp that
// the promotion below would then swap over a perfectly good settings.ini.
bool WriteWholeFile(HANDLE handle, const std::wstring& text) {
    const WORD bom = 0xFEFF; // UTF-16LE, so Unicode paths round-trip
    // Divided, not multiplied: the product could in principle wrap past the
    // comparison before the cast below narrows it.
    if (text.size() > (kMaxSettingsBytes - sizeof(bom)) / sizeof(wchar_t))
        return false; // the reader's ceiling, BOM included; also keeps the cast exact
    DWORD written = 0;
    if (!WriteFile(handle, &bom, sizeof(bom), &written, nullptr) || written != sizeof(bom))
        return false;
    const DWORD bytes = static_cast<DWORD>(text.size() * sizeof(wchar_t));
    written = 0;
    if (!WriteFile(handle, text.data(), bytes, &written, nullptr) || written != bytes)
        return false;
    return FlushFileBuffers(handle) != FALSE;
}

// The ONE artifact name a save creates, and therefore the only one cleanup may
// ever delete: <settings.ini>.<pid>.<counter>.tmp, canonical decimal spelling,
// counter inside the range CreateUniqueTemp can actually produce. A wildcard
// would also swallow a user's own settings.ini.something (and, through 8.3
// name matching, whatever else happens to alias the pattern - the API's
// trailing ".*" even matches settings.ini itself).
bool ParseDecimal(const std::wstring& s, unsigned long limit, unsigned long& out) {
    if (s.empty() || s.size() > 10 || (s.size() > 1 && s.front() == L'0'))
        return false; // no leading zeros: not a spelling this app produces
    for (wchar_t c : s)
        if (c < L'0' || c > L'9')
            return false;
    errno = 0;
    wchar_t* end = nullptr;
    const unsigned long value = std::wcstoul(s.c_str(), &end, 10);
    if (errno == ERANGE || end != s.c_str() + s.size() || value > limit)
        return false;
    out = value;
    return true;
}

std::optional<DWORD> ParseTempName(const std::wstring& name, const std::wstring& stem) {
    if (name.size() <= stem.size() + 1 || name.compare(0, stem.size(), stem) != 0 ||
        name[stem.size()] != L'.' || name.size() < 5 ||
        name.compare(name.size() - 4, 4, L".tmp") != 0)
        return std::nullopt;
    const std::wstring rest = name.substr(stem.size() + 1, name.size() - stem.size() - 5);
    const size_t dot = rest.find_last_of(L'.');
    if (dot == std::wstring::npos)
        return std::nullopt;
    unsigned long counter = 0;
    unsigned long pid = 0;
    // The full DWORD range: process ids being multiples of four is an
    // implementation detail applications are told not to depend on. Only 0 is
    // excluded, and that by contract (the idle process is never a writer).
    if (!ParseDecimal(rest.substr(dot + 1), kMaxTempCounter, counter) ||
        !ParseDecimal(rest.substr(0, dot), 0xFFFFFFFFul, pid) || pid == 0)
        return std::nullopt;
    return static_cast<DWORD>(pid);
}

// Old enough that no save can still be using it: a save takes milliseconds.
// A temp's last-write time is also its creation time for this purpose - it is
// written once and never renamed.
constexpr ULONGLONG kStaleTempHns = 36'000'000'000ULL; // 1 hour, 100 ns units

bool OlderThan(const FILETIME& stamp, ULONGLONG ageHns) {
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    const auto pack = [](const FILETIME& t) {
        return (static_cast<ULONGLONG>(t.dwHighDateTime) << 32) | t.dwLowDateTime;
    };
    const ULONGLONG then = pack(stamp);
    const ULONGLONG here = pack(now);
    return here > then && here - then > ageHns;
}

// Leftovers from a crashed save hold document and MRU paths and would
// otherwise linger forever. Only OUR OWN process's temps are removed on sight;
// a foreign one has to be an hour old first, because the settings lock is
// best-effort and another writer may be between closing its finished temp and
// renaming it, with nothing holding the file open. Worst case if that
// judgement is ever wrong: that writer's save fails and the previous file
// stays - there is no window in which settings.ini does not exist.
void SweepStaleTemps(const std::wstring& target) {
    // Nothing is swept while the canonical file is ABSENT. In that state a
    // retained temp can be the only complete copy of the settings left (see
    // PromoteFile), and litter a user can rename by hand beats deleting the
    // last snapshot on a timer. The protection lasts exactly that long: once
    // any run recreates settings.ini (from defaults, if that is all it had),
    // the old temp becomes an ordinary stale artifact again and the rules
    // below apply to it. It is short-lived forensic residue, not a durable
    // recovery slot.
    if (GetFileAttributesW(target.c_str()) == INVALID_FILE_ATTRIBUTES)
        return;
    const size_t slash = target.find_last_of(L'\\');
    if (slash == std::wstring::npos)
        return;
    const std::wstring dir = target.substr(0, slash + 1);
    const std::wstring stem = target.substr(slash + 1);
    const DWORD self = GetCurrentProcessId();
    WIN32_FIND_DATAW found{};
    const HANDLE search = FindFirstFileW((target + L".*.tmp").c_str(), &found);
    if (search == INVALID_HANDLE_VALUE)
        return;
    do {
        if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;
        const std::optional<DWORD> pid = ParseTempName(found.cFileName, stem);
        if (pid && (*pid == self || OlderThan(found.ftLastWriteTime, kStaleTempHns)))
            DeleteFileW((dir + found.cFileName).c_str());
    } while (FindNextFileW(search, &found));
    FindClose(search);
}

// Swaps the finished temp in with ONE same-volume namespace rename.
// Deliberately not ReplaceFileW: that call exists to carry the target's
// descriptor, attributes and streams onto the replacement - which the security
// model above declares out of scope - and in exchange it has two documented
// PARTIAL failure states in which the canonical name is left deleted, or
// renamed to something else. A rename has neither: on the local NTFS volume
// this file lives on, settings.ini resolves either to the old file or to the
// new one, never to nothing. (That is the proven property here, and the reason
// the source and the target are always siblings and no cross-volume copy flag
// is passed; the API does not promise rollback or power-loss semantics for
// every redirected or third-party provider.) Everything that used to exist to
// survive ReplaceFileW's partial states - a backup sibling, its recovery on
// the next start, its own cleanup rules - is gone with them.
bool PromoteFile(const std::wstring& temp, const std::wstring& target) {
    if (MoveFileExW(temp.c_str(), target.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return true;
    // The orphan is dropped only with the previous file VERIFIED in place. On
    // a local volume a failed rename cannot have removed the target, but that
    // is a property of the filesystem, not a promise of the API: on a provider
    // where it did, this temp would be the only complete copy left, so it is
    // kept (and the sweep leaves it alone while the canonical name is absent).
    // It is deliberately NOT adopted automatically on the next start: a temp
    // that a crash truncated mid-write is indistinguishable from a complete
    // one without a second file state to mark it, and silently loading half a
    // settings file is worse than starting from defaults. That case degrades
    // to manual rescue - rename it - and is documented as such.
    if (GetFileAttributesW(target.c_str()) != INVALID_FILE_ATTRIBUTES)
        DeleteFileW(temp.c_str());
    return false;
}

// ------------------------------------------------------------- pane slots

PaneSettings LoadPane(const IniData& ini, PCWSTR section) {
    PaneSettings pane;
    pane.path = ReadString(ini, section, L"path");
    pane.zoom = ReadFloat(ini, section, L"zoom", 1.0f);
    pane.scrollX = ReadFloat(ini, section, L"scrollX", 0);
    pane.scrollY = ReadFloat(ini, section, L"scrollY", 0);
    pane.zoomMode = std::clamp(ReadInt(ini, section, L"zoomMode", 2), 0, 2);
    return pane;
}

void SavePane(IniWriter& ini, PCWSTR section, const PaneSettings& pane) {
    ini.Set(section, L"path", pane.path);
    ini.SetFloat(section, L"zoom", pane.zoom);
    ini.SetFloat(section, L"scrollX", pane.scrollX);
    ini.SetFloat(section, L"scrollY", pane.scrollY);
    ini.SetInt(section, L"zoomMode", pane.zoomMode);
}

} // namespace

std::wstring SettingsDirectory() {
    // Test isolation hook: E2E suites point this at a scratch directory so
    // they can never wipe or overwrite the user's real settings.ini (which
    // has happened: a suite window plus a concurrent user launch empties the
    // MRU lists the user then persists on close). Set but unusable FAILS
    // CLOSED - falling back to the real profile would defeat the sandbox.
    const std::wstring sandbox = EnvVar(L"PSV_SETTINGS_DIR");
    if (!sandbox.empty())
        return EnsureDirectory(sandbox) ? sandbox : std::wstring();
    std::wstring roaming = KnownFolder(FOLDERID_RoamingAppData);
    if (roaming.empty())
        roaming = EnvVar(L"APPDATA"); // only if the shell cannot answer
    if (roaming.empty())
        return {};
    const std::wstring dir = roaming + L"\\PdfSideViewer";
    return EnsureDirectory(dir) ? dir : std::wstring();
}

std::wstring UserLockDirectory() {
    // Canonical, override-INDEPENDENT and local. What it guards (the HKCU
    // shell verbs) is one resource per USER wherever the settings happen to
    // live, so two copies running with different PSV_SETTINGS_DIR or APPDATA
    // values must still meet on the same lock file - a lock whose identity
    // moves with process-local environment protects nothing. Local rather
    // than roaming app data because roaming is the one that is redirected on
    // purpose; local app data can be redirected too, so this REDUCES the odds
    // of a network path under the lock, it does not remove them.
    const std::wstring local = KnownFolder(FOLDERID_LocalAppData);
    if (local.empty())
        return {};
    const std::wstring dir = local + L"\\PdfSideViewer";
    return EnsureDirectory(dir) ? dir : std::wstring();
}

AppSettings AppSettings::Load() {
    AppSettings s;
    const std::wstring path = SettingsPath();
    if (path.empty())
        return s;
    // NO lock on the read, by design: Save() builds the whole file aside and
    // swaps it in atomically, and this reads it whole through a single
    // handle, so what lands here is ALWAYS one coherent snapshot (the
    // previous file or the new one, never a mixture). A torn read would
    // otherwise be re-persisted by the next SaveSession and turn permanent -
    // which is why coherence lives in the writer plus this one read, not in a
    // reader-side lock that could time out.
    const IniData ini = ParseIni(ReadWholeFile(path));
    s.hasPlacement = ReadInt(ini, kWindowSection, L"hasPlacement", 0) != 0;
    // Clamped to a sane virtual-desktop range BEFORE the additions: extremes
    // from a corrupted or hand-edited file must not overflow the rectangle
    // arithmetic into a placement that is nonsense rather than merely wrong.
    constexpr int kCoordLimit = 1'000'000;
    const int x = std::clamp(ReadInt(ini, kWindowSection, L"x", 0), -kCoordLimit, kCoordLimit);
    const int y = std::clamp(ReadInt(ini, kWindowSection, L"y", 0), -kCoordLimit, kCoordLimit);
    const int w = std::clamp(ReadInt(ini, kWindowSection, L"w", 0), 0, kCoordLimit);
    const int h = std::clamp(ReadInt(ini, kWindowSection, L"h", 0), 0, kCoordLimit);
    s.normalRect = {x, y, x + w, y + h};
    s.maximized = ReadInt(ini, kWindowSection, L"maximized", 0) != 0;
    s.splitRatio = std::clamp(ReadFloat(ini, kWindowSection, L"splitRatio", 0.5f), 0.1f, 0.9f);
    s.paneCount = std::clamp(ReadInt(ini, kWindowSection, L"paneCount", 2), 2, kPaneSlots);
    s.splitRatio3Left =
        std::clamp(ReadFloat(ini, kWindowSection, L"splitRatio3Left", 1.0f / 3.0f), 0.1f, 0.8f);
    s.splitRatio3Center =
        std::clamp(ReadFloat(ini, kWindowSection, L"splitRatio3Center", 1.0f / 3.0f), 0.1f, 0.8f);
    // Two shares must leave room for the third; a hand-edited pair that does
    // not falls back to equal thirds rather than collapsing a pane.
    if (s.splitRatio3Left + s.splitRatio3Center > 0.9f) {
        s.splitRatio3Left = 1.0f / 3.0f;
        s.splitRatio3Center = 1.0f / 3.0f;
    }
    s.dpi = static_cast<UINT>(std::max(1, ReadInt(ini, kWindowSection, L"dpi", 96)));
    s.toolbar = ReadInt(ini, kWindowSection, L"toolbar", 1) != 0;
    s.statusbar = ReadInt(ini, kWindowSection, L"statusbar", 1) != 0;
    s.outline = ReadInt(ini, kWindowSection, L"outline", 0) != 0;
    s.language = ReadString(ini, kWindowSection, L"language");
    if (s.language.empty())
        s.language = L"en";
    s.scrollSync = ReadInt(ini, kSyncSection, L"scroll", 1) != 0;
    s.zoomSync = ReadInt(ini, kSyncSection, L"zoom", 1) != 0;
    s.showGaps = ReadInt(ini, kSyncSection, L"showGaps", 1) != 0;
    s.showAnchors = ReadInt(ini, kSyncSection, L"showAnchors", 1) != 0;
    s.showTicks = ReadInt(ini, kSyncSection, L"showTicks", 1) != 0;
    s.scrollMode = std::clamp(ReadInt(ini, kWindowSection, L"scrollMode", 0), 0, 1);
    s.restoreSession = ReadInt(ini, kWindowSection, L"restoreSession", 1) != 0;
    s.wheelLines = std::clamp(ReadInt(ini, kWindowSection, L"wheelLines", 0), 0, 100);
    s.outlineWidth = std::clamp(ReadInt(ini, kWindowSection, L"outlineWidth", 260), 120, 600);
    s.rebarLocked = ReadInt(ini, kWindowSection, L"rebarLocked", 1) != 0;
    s.rebarBands = ReadString(ini, kWindowSection, L"rebarBands");
    s.toolbarText = std::clamp(ReadInt(ini, kWindowSection, L"toolbarText", 1), 0, 2);
    s.fsToolbar = ReadInt(ini, kWindowSection, L"fsToolbar", 0) != 0;
    s.fsStatus = ReadInt(ini, kWindowSection, L"fsStatusbar", 0) != 0;
    s.showHeader = ReadInt(ini, kWindowSection, L"header", 1) != 0;
    s.headerShowPath = ReadInt(ini, kWindowSection, L"headerPath", 0) != 0;
    s.defPaneCount = std::clamp(ReadInt(ini, kDefaultsSection, L"paneCount", 2), 2, kPaneSlots);
    s.defScrollMode = std::clamp(ReadInt(ini, kDefaultsSection, L"scrollMode", 0), 0, 1);
    s.defZoomMode = std::clamp(ReadInt(ini, kDefaultsSection, L"zoomMode", 2), 0, 2);
    s.defScrollSync = ReadInt(ini, kDefaultsSection, L"scrollSync", 1) != 0;
    s.defZoomSync = ReadInt(ini, kDefaultsSection, L"zoomSync", 1) != 0;
    {
        std::wstring inverse = ReadString(ini, kSynctexSection, L"inverse");
        if (!inverse.empty())
            s.synctexInverse = std::move(inverse); // empty/missing keeps the default
    }
    for (size_t i = 0; i < kMruMaxEntries; ++i) {
        std::wstring f = ReadString(ini, kMruFilesSection, L"file" + std::to_wstring(i));
        if (f.empty())
            break;
        s.mruFiles.push_back(std::move(f));
    }
    // The outer slots are what makes an entry exist; the centre is optional, so
    // a two-pane entry written by any earlier version still loads unchanged.
    for (size_t i = 0; i < kMruMaxEntries; ++i) {
        const std::wstring n = std::to_wstring(i);
        MruSession entry;
        for (int slot = 0; slot < kPaneSlots; ++slot)
            entry.path[static_cast<size_t>(slot)] =
                ReadString(ini, kMruPairsSection, kSlotKeys[slot] + n);
        if (entry.path[kSlotLeft].empty() || entry.path[kSlotRight].empty())
            break;
        s.mruSessions.push_back(std::move(entry));
    }
    for (size_t i = 0; i < kMruMaxEntries; ++i) {
        const std::wstring n = std::to_wstring(i);
        SavedSyncPoints entry;
        for (int slot = 0; slot < kPaneSlots; ++slot)
            entry.path[static_cast<size_t>(slot)] =
                ReadString(ini, kSyncPointsSection, kSlotKeys[slot] + n);
        if (entry.path[kSlotLeft].empty() || entry.path[kSlotRight].empty())
            break;
        entry.manual = ReadString(ini, kSyncPointsSection, L"manual" + n);
        entry.hadAuto = ReadInt(ini, kSyncPointsSection, L"auto" + n, 0) != 0;
        s.syncPoints.push_back(std::move(entry));
    }
    // Every slot section is read, ACTIVE or NOT: a [center] left by a parked
    // three-pane session must seed the fallback even when the session itself
    // reopens as two panes, or the park would not survive a restart.
    for (int slot = 0; slot < kPaneSlots; ++slot)
        s.panes[static_cast<size_t>(slot)] = LoadPane(ini, kSlotKeys[slot]);
    return s;
}

bool AppSettings::Save() const {
    const std::wstring target = SettingsPath();
    if (target.empty())
        return false;
    // Best-effort serialization ONLY, and deliberately short: the whole-file
    // swap below makes even an unserialized save safe (each writer builds its
    // own complete file, so the rename wins or loses wholesale and no
    // interleaving is possible). What the lock does NOT do is merge: two
    // instances that loaded the same state and both save are last-close-wins,
    // exactly as they were before it existed. Failing to acquire it therefore
    // does not discard the save.
    const ScopedFileLock lock(SettingsLockPath(), 1000);
    // Trade-off, accepted: a whole-file rewrite drops keys this build does
    // not know, so a DOWNGRADE round-trip loses a newer version's additions
    // (they are versionless safe-default keys by contract).
    IniWriter ini;
    ini.SetBool(kWindowSection, L"hasPlacement", hasPlacement);
    ini.SetInt(kWindowSection, L"x", normalRect.left);
    ini.SetInt(kWindowSection, L"y", normalRect.top);
    ini.SetInt(kWindowSection, L"w", normalRect.right - normalRect.left);
    ini.SetInt(kWindowSection, L"h", normalRect.bottom - normalRect.top);
    ini.SetBool(kWindowSection, L"maximized", maximized);
    ini.SetFloat(kWindowSection, L"splitRatio", splitRatio);
    ini.SetInt(kWindowSection, L"paneCount", paneCount);
    ini.SetFloat(kWindowSection, L"splitRatio3Left", splitRatio3Left);
    ini.SetFloat(kWindowSection, L"splitRatio3Center", splitRatio3Center);
    ini.SetInt(kWindowSection, L"dpi", static_cast<int>(dpi));
    ini.SetBool(kWindowSection, L"toolbar", toolbar);
    ini.SetBool(kWindowSection, L"statusbar", statusbar);
    ini.SetBool(kWindowSection, L"outline", outline);
    ini.Set(kWindowSection, L"language", language);
    ini.SetInt(kWindowSection, L"scrollMode", scrollMode);
    ini.SetBool(kWindowSection, L"restoreSession", restoreSession);
    ini.SetInt(kWindowSection, L"wheelLines", wheelLines);
    ini.SetInt(kWindowSection, L"outlineWidth", outlineWidth);
    ini.SetBool(kWindowSection, L"rebarLocked", rebarLocked);
    ini.Set(kWindowSection, L"rebarBands", rebarBands);
    ini.SetInt(kWindowSection, L"toolbarText", toolbarText);
    ini.SetBool(kWindowSection, L"fsToolbar", fsToolbar);
    ini.SetBool(kWindowSection, L"fsStatusbar", fsStatus);
    ini.SetBool(kWindowSection, L"header", showHeader);
    ini.SetBool(kWindowSection, L"headerPath", headerShowPath);
    ini.SetInt(kDefaultsSection, L"paneCount", defPaneCount);
    ini.SetInt(kDefaultsSection, L"scrollMode", defScrollMode);
    ini.SetInt(kDefaultsSection, L"zoomMode", defZoomMode);
    ini.SetBool(kDefaultsSection, L"scrollSync", defScrollSync);
    ini.SetBool(kDefaultsSection, L"zoomSync", defZoomSync);
    ini.SetBool(kSyncSection, L"scroll", scrollSync);
    ini.SetBool(kSyncSection, L"zoom", zoomSync);
    ini.SetBool(kSyncSection, L"showGaps", showGaps);
    ini.SetBool(kSyncSection, L"showAnchors", showAnchors);
    ini.SetBool(kSyncSection, L"showTicks", showTicks);
    ini.Set(kSynctexSection, L"inverse", synctexInverse);
    for (size_t i = 0; i < mruFiles.size() && i < kMruMaxEntries; ++i)
        ini.Set(kMruFilesSection, L"file" + std::to_wstring(i), mruFiles[i]);
    // A slot that is empty in an entry gets no key at all, rather than a blank
    // one, so an entry that shrank from three documents to two leaves no stale
    // centre path behind. (Rebuilding the file from scratch is what makes
    // "absent" and "deleted" the same thing here.)
    const auto writeSlots = [&ini](PCWSTR section, const std::wstring& n,
                                   const PerPane<std::wstring>& paths) {
        for (int slot = 0; slot < kPaneSlots; ++slot) {
            const std::wstring& value = paths[static_cast<size_t>(slot)];
            if (!value.empty())
                ini.Set(section, kSlotKeys[slot] + n, value);
        }
    };
    for (size_t i = 0; i < mruSessions.size() && i < kMruMaxEntries; ++i)
        writeSlots(kMruPairsSection, std::to_wstring(i), mruSessions[i].path);
    for (size_t i = 0; i < syncPoints.size() && i < kMruMaxEntries; ++i) {
        const std::wstring n = std::to_wstring(i);
        writeSlots(kSyncPointsSection, n, syncPoints[i].path);
        ini.Set(kSyncPointsSection, L"manual" + n, syncPoints[i].manual);
        ini.SetBool(kSyncPointsSection, L"auto" + n, syncPoints[i].hadAuto);
    }
    for (int slot = 0; slot < kPaneSlots; ++slot) {
        // Active slots always; an inactive centre only while it PARKS a
        // document, so the park survives restarts. Anything else leaves the
        // section out entirely: a pristine two-pane settings.ini never grows a
        // [center], and a park that was wiped (Close Session, Ctrl+W) does not
        // linger in the file to resurrect.
        const PaneSettings& pane = panes[static_cast<size_t>(slot)];
        if (SlotActive(slot, paneCount) || !pane.path.empty())
            SavePane(ini, kSlotKeys[slot], pane);
    }

    SweepStaleTemps(target);
    std::wstring temp;
    const HANDLE handle = CreateUniqueTemp(target, temp);
    if (handle == INVALID_HANDLE_VALUE)
        return false;
    const bool written = WriteWholeFile(handle, ini.Text());
    const bool closed = CloseHandle(handle) != FALSE;
    if (!written || !closed) {
        DeleteFileW(temp.c_str()); // never promote a partial file
        return false;
    }
    return PromoteFile(temp, target);
}
