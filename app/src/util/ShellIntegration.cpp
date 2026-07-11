#include "util/ShellIntegration.h"

#include "util/Strings.h"

#include <shlobj.h>  // SHChangeNotify
#include <shlwapi.h> // StrStrIW

namespace {

constexpr PCWSTR kShellBase = L"Software\\Classes\\SystemFileAssociations\\.pdf\\shell";
constexpr PCWSTR kVerbLeft = L"PsvOpenLeft";
constexpr PCWSTR kVerbRight = L"PsvOpenRight";

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

bool WriteVerb(PCWSTR verb, PCWSTR label, PCWSTR flag, const std::wstring& exe) {
    const std::wstring base = std::wstring(kShellBase) + L"\\" + verb;
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, base.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr,
                        &key, nullptr) != ERROR_SUCCESS)
        return false;
    bool ok = SetValue(key, L"MUIVerb", label);
    ok = SetValue(key, L"Icon", L"\"" + exe + L"\",0") && ok;
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

} // namespace

bool ShellIntegration::Register() {
    const std::wstring exe = ExePath();
    if (exe.empty())
        return false;
    const bool ok = WriteVerb(kVerbLeft, Str(StrId::VerbOpenLeft), L"-open-left", exe) &&
                    WriteVerb(kVerbRight, Str(StrId::VerbOpenRight), L"-open-right", exe);
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return ok;
}

bool ShellIntegration::Unregister() {
    const std::wstring base = kShellBase;
    // Scoped deletes: only our two verb keys, never the .pdf association.
    const LSTATUS a = RegDeleteTreeW(HKEY_CURRENT_USER, (base + L"\\" + kVerbLeft).c_str());
    const LSTATUS b = RegDeleteTreeW(HKEY_CURRENT_USER, (base + L"\\" + kVerbRight).c_str());
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return (a == ERROR_SUCCESS || a == ERROR_FILE_NOT_FOUND) &&
           (b == ERROR_SUCCESS || b == ERROR_FILE_NOT_FOUND);
}

bool ShellIntegration::IsRegistered() {
    const std::wstring exe = ExePath();
    for (PCWSTR verb : {kVerbLeft, kVerbRight}) {
        const std::wstring cmdKey = std::wstring(kShellBase) + L"\\" + verb + L"\\command";
        wchar_t buf[1024];
        DWORD size = sizeof(buf);
        if (RegGetValueW(HKEY_CURRENT_USER, cmdKey.c_str(), nullptr, RRF_RT_REG_SZ, nullptr,
                         buf, &size) != ERROR_SUCCESS)
            return false;
        // A moved exe reads as unregistered; re-checking the Options box
        // rewrites the verbs with the new path.
        if (!exe.empty() && StrStrIW(buf, exe.c_str()) == nullptr)
            return false;
    }
    return true;
}
