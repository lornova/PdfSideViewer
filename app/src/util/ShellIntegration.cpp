#include "util/ShellIntegration.h"

#include "resource.h" // IDI_VERB_*: per-verb context-menu icons
#include "util/ScopedFileLock.h"
#include "util/Settings.h" // UserLockDirectory: where the verb lock lives
#include "util/Strings.h"

#include <shlobj.h> // SHChangeNotify

#include <string>

namespace {

constexpr PCWSTR kShellBase = L"Software\\Classes\\SystemFileAssociations\\.pdf\\shell";
// Explorer lists static verbs ALPHABETICALLY by key name (key-creation order
// is not honoured), so the digit is what puts the menu in the visual
// left/centre/right order.
constexpr PCWSTR kVerbLeft = L"PsvOpen1Left";
constexpr PCWSTR kVerbCenter = L"PsvOpen2Center";
constexpr PCWSTR kVerbRight = L"PsvOpen3Right";
// The un-numbered names every build through 0.9.0 wrote (and alphabetically
// those sorted centre/left/right, the bug the digits fix). Never written any
// more; still recognized so upgrades and removals clean them up.
constexpr PCWSTR kLegacyLeft = L"PsvOpenLeft";
constexpr PCWSTR kLegacyRight = L"PsvOpenRight";
constexpr PCWSTR kLegacyCenter = L"PsvOpenCenter";

std::wstring ExePath() {
    wchar_t buf[MAX_PATH];
    const DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return len != 0 && len < MAX_PATH ? std::wstring(buf, len) : std::wstring();
}

bool SetValue(HKEY key, PCWSTR name, const std::wstring& value) {
    return RegSetValueExW(key, name, 0, REG_SZ,
                          reinterpret_cast<const BYTE*>(value.c_str()),
                          static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))) ==
           ERROR_SUCCESS;
}

// Registration mutations are read-check-write sequences over HKCU keys that
// several copies of the app share: serialized per user via a lock file
// (ScopedFileLock: profile-ACL'd, crosses RDP sessions, no predictable
// kernel-object name to pre-create). The path comes from UserLockDirectory(),
// NOT from the settings directory: the verbs are ONE resource per user and do
// not move with PSV_SETTINGS_DIR or a redirected APPDATA, so a lock keyed on
// those would let two copies with different environments walk over each other
// while both believed they held it. Copies predating the lock remain
// unserialized - that residual race is accepted; the lock removes it between
// current builds. Mutators FAIL CLOSED when it cannot be acquired.
std::wstring ShellLockPath() {
    const std::wstring dir = UserLockDirectory();
    return dir.empty() ? std::wstring() : dir + L"\\shell.lock";
}

// The exe a verb's command value launches, empty when unparsable. Register
// has always written the quoted form ("exe" -flag "%1"), every released
// version included, so the exe is the first quoted token AND the quote must
// be followed by a separator or the end - `"exe".wrapper ...` names something
// else. ORDINAL case-insensitive comparison (not lstrcmpiW's word sort): a
// path is an identifier, not text.
std::wstring CommandExe(PCWSTR command) {
    if (!command || command[0] != L'"')
        return {};
    PCWSTR end = wcschr(command + 1, L'"');
    if (!end || (end[1] != L'\0' && end[1] != L' '))
        return {};
    return std::wstring(command + 1, end);
}

bool VerbOwnedBy(PCWSTR verb, const std::wstring& exe) {
    const std::wstring cmdKey = std::wstring(kShellBase) + L"\\" + verb + L"\\command";
    wchar_t buf[1024];
    DWORD size = sizeof(buf);
    if (RegGetValueW(HKEY_CURRENT_USER, cmdKey.c_str(), nullptr, RRF_RT_REG_SZ, nullptr, buf,
                     &size) != ERROR_SUCCESS)
        return false;
    const std::wstring owner = CommandExe(buf);
    return !owner.empty() &&
           CompareStringOrdinal(owner.c_str(), -1, exe.c_str(), -1, TRUE) == CSTR_EQUAL;
}

bool WriteVerb(PCWSTR verb, PCWSTR label, PCWSTR flag, int iconId, const std::wstring& exe) {
    const std::wstring base = std::wstring(kShellBase) + L"\\" + verb;
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, base.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr,
                        &key, nullptr) != ERROR_SUCCESS)
        return false;
    bool ok = SetValue(key, L"MUIVerb", label);
    // "path,-N" picks resource id N out of the exe: the MINUS is load-bearing
    // (a bare index would track icon enumeration order, not the .rc id).
    ok = SetValue(key, L"Icon", L"\"" + exe + L"\",-" + std::to_wstring(iconId)) && ok;
    // Explicit even though inherent: the default model for command verbs is
    // Document (one process per selected file); a "left file" is a single
    // selection, so the verb hides on multi-selects instead.
    ok = SetValue(key, L"MultiSelectModel", L"Single") && ok;
    RegCloseKey(key);
    HKEY cmd = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, (base + L"\\command").c_str(), 0, nullptr, 0,
                        KEY_SET_VALUE, nullptr, &cmd, nullptr) != ERROR_SUCCESS)
        return false;
    // Both the exe and %1 quoted: unquoted paths with spaces mis-parse.
    ok = SetValue(cmd, nullptr, L"\"" + exe + L"\" " + flag + L" \"%1\"") && ok;
    RegCloseKey(cmd);
    return ok;
}

// --- primitives, NOT self-locking -----------------------------------------
// Every mutation below is a read-check-write over shared HKCU keys, so each
// entry point holds ShellLockPath() for its whole sequence. They are kept
// lock-free here for one reason: the exclusive lock is not reentrant, and the
// desired-state entry point has to run a probe AND its mutation inside a
// single acquisition.

// Current names only: a set still on the legacy names must read as
// unregistered, so the repair path rewrites (and thereby renames) it.
bool AllVerbsOwnedBy(const std::wstring& exe) {
    for (PCWSTR verb : {kVerbLeft, kVerbRight, kVerbCenter})
        if (!VerbOwnedBy(verb, exe))
            return false;
    return true;
}

// A PARTIAL set (an upgrade over the two-verb release, a failed write) reads
// as unregistered above, yet its verbs are still visible in Explorer and must
// be removable; same for verbs still under the legacy names.
bool AnyVerbOwnedBy(const std::wstring& exe) {
    for (PCWSTR verb :
         {kVerbLeft, kVerbRight, kVerbCenter, kLegacyLeft, kLegacyRight, kLegacyCenter})
        if (VerbOwnedBy(verb, exe))
            return true;
    return false;
}

bool WriteAllVerbs(const std::wstring& exe) {
    // This exe's verbs under the LEGACY names go first, or an upgrade would
    // show six entries; best-effort (a leftover is visible but harmless, and
    // every removal path covers the legacy names too). Foreign legacy verbs
    // stay: their copy owns them. The centre verb is registered
    // unconditionally: like the File menu entry, it is a way INTO the
    // three-pane mode, not something that follows it.
    for (PCWSTR verb : {kLegacyLeft, kLegacyRight, kLegacyCenter})
        if (VerbOwnedBy(verb, exe))
            RegDeleteTreeW(HKEY_CURRENT_USER, (std::wstring(kShellBase) + L"\\" + verb).c_str());
    const bool ok =
        WriteVerb(kVerbLeft, Str(StrId::VerbOpenLeft), L"-open-left", IDI_VERB_LEFT, exe) &&
        WriteVerb(kVerbCenter, Str(StrId::VerbOpenCenter), L"-open-center", IDI_VERB_CENTER,
                  exe) &&
        WriteVerb(kVerbRight, Str(StrId::VerbOpenRight), L"-open-right", IDI_VERB_RIGHT, exe);
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return ok;
}

bool RemoveVerbs(const std::wstring& ownedBy, bool ownedOnly) {
    // Scoped deletes: only our own verb keys (current and legacy names),
    // never the .pdf association. With ownedOnly, ownership is checked PER
    // VERB: a mixed state (this copy owns some verbs, a portable/dev copy
    // owns others - partial writes from different exes can interleave) must
    // remove only what is genuinely ours.
    bool ok = true;
    for (PCWSTR verb :
         {kVerbLeft, kVerbRight, kVerbCenter, kLegacyLeft, kLegacyRight, kLegacyCenter}) {
        if (ownedOnly && !VerbOwnedBy(verb, ownedBy))
            continue;
        const LSTATUS s = RegDeleteTreeW(HKEY_CURRENT_USER,
                                         (std::wstring(kShellBase) + L"\\" + verb).c_str());
        ok = ok && (s == ERROR_SUCCESS || s == ERROR_FILE_NOT_FOUND);
    }
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return ok;
}

} // namespace

bool ShellIntegration::Apply(bool desired) {
    const std::wstring exe = ExePath();
    if (exe.empty())
        return false;
    const ScopedFileLock lock(ShellLockPath());
    if (!lock.Acquired())
        return false;
    // Probe and mutation inside ONE acquisition. Deciding from an unlocked
    // probe outside would lose the user's request: another copy completing a
    // registration between "nothing of ours is there" and the mutation turns
    // an unchecked box into a no-op, and the reverse ordering loses a checked
    // one. Both branches repair PARTIAL states (that is why this is not a
    // desired != IsRegistered() comparison): checked rewrites the full set,
    // unchecked removes whatever this exe still owns.
    if (desired)
        return AllVerbsOwnedBy(exe) || WriteAllVerbs(exe);
    return !AnyVerbOwnedBy(exe) || RemoveVerbs(exe, true);
}

bool ShellIntegration::Register() {
    const std::wstring exe = ExePath();
    if (exe.empty())
        return false;
    const ScopedFileLock lock(ShellLockPath());
    if (!lock.Acquired())
        return false;
    return WriteAllVerbs(exe);
}

bool ShellIntegration::Unregister() {
    const ScopedFileLock lock(ShellLockPath());
    if (!lock.Acquired())
        return false;
    return RemoveVerbs({}, false);
}

bool ShellIntegration::UnregisterOwned() {
    // Unregister() stays the wholesale product-level removal for the headless
    // -unregister-shell administration path; this one is the uninstaller's
    // and the Options uncheck's scope.
    const std::wstring exe = ExePath();
    if (exe.empty())
        return false;
    // The lock closes the read-check-delete window: without it another copy
    // could rewrite a verb between the ownership probe and the delete, and
    // this instance would remove the newly foreign entry.
    const ScopedFileLock lock(ShellLockPath());
    if (!lock.Acquired())
        return false;
    return RemoveVerbs(exe, true);
}

bool ShellIntegration::OwnsAnyVerb() {
    // Same unlocked DISPLAY role as IsRegistered(): it tells the Options
    // dialog that an uncheck still has work to do, and Apply() re-derives the
    // state under the lock before touching anything.
    const std::wstring exe = ExePath();
    return !exe.empty() && AnyVerbOwnedBy(exe);
}

bool ShellIntegration::IsRegistered() {
    // Display state for the Options checkbox, deliberately unlocked: it feeds
    // a dialog the user is about to answer, and the answer is applied through
    // Apply() - which re-derives the state under the lock rather than
    // trusting this one. A moved exe reads as unregistered; re-checking the
    // box rewrites the verbs with the new path.
    const std::wstring exe = ExePath();
    return !exe.empty() && AllVerbsOwnedBy(exe);
}
