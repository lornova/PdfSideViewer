#include "Strings.h"

#include <iterator>

namespace {

#define PSV_AS_EN(id, en, it, de, fr, hu) en,
constexpr PCWSTR kEnglish[] = {PSV_STRING_LIST(PSV_AS_EN)};
#undef PSV_AS_EN

#define PSV_AS_IT(id, en, it, de, fr, hu) it,
constexpr PCWSTR kItalian[] = {PSV_STRING_LIST(PSV_AS_IT)};
#undef PSV_AS_IT

#define PSV_AS_DE(id, en, it, de, fr, hu) de,
constexpr PCWSTR kGerman[] = {PSV_STRING_LIST(PSV_AS_DE)};
#undef PSV_AS_DE

#define PSV_AS_FR(id, en, it, de, fr, hu) fr,
constexpr PCWSTR kFrench[] = {PSV_STRING_LIST(PSV_AS_FR)};
#undef PSV_AS_FR

#define PSV_AS_HU(id, en, it, de, fr, hu) hu,
constexpr PCWSTR kHungarian[] = {PSV_STRING_LIST(PSV_AS_HU)};
#undef PSV_AS_HU

static_assert(std::size(kEnglish) == static_cast<size_t>(StrId::Count));
static_assert(std::size(kItalian) == static_cast<size_t>(StrId::Count));
static_assert(std::size(kGerman) == static_cast<size_t>(StrId::Count));
static_assert(std::size(kFrench) == static_cast<size_t>(StrId::Count));
static_assert(std::size(kHungarian) == static_cast<size_t>(StrId::Count));

// Both indexed by Lang (see the order note in Strings.h).
constexpr const PCWSTR* kTables[] = {kEnglish, kItalian, kGerman, kFrench, kHungarian};
constexpr PCWSTR kCodes[] = {L"en", L"it", L"de", L"fr", L"hu"};

static_assert(std::size(kTables) == static_cast<size_t>(kLangCount));
static_assert(std::size(kCodes) == static_cast<size_t>(kLangCount));

Lang g_lang = Lang::English;

} // namespace

void SetUiLanguage(Lang lang) {
    g_lang = lang;
}

Lang UiLanguage() {
    return g_lang;
}

PCWSTR Str(StrId id) {
    const auto index = static_cast<size_t>(id);
    return kTables[static_cast<size_t>(g_lang)][index];
}

Lang LangFromCode(const std::wstring& code) {
    for (size_t i = 0; i < std::size(kCodes); ++i)
        if (code == kCodes[i])
            return static_cast<Lang>(i);
    return Lang::English;
}

PCWSTR LangCode(Lang lang) {
    return kCodes[static_cast<size_t>(lang)];
}
