#include "Strings.h"

#include <iterator>

namespace {

#define PSV_AS_EN(id, en, it, de, fr, hu, uk, ro, pt, el, es, pl, nl, cs, sv) en,
constexpr PCWSTR kEnglish[] = {PSV_STRING_LIST(PSV_AS_EN)};
#undef PSV_AS_EN

#define PSV_AS_IT(id, en, it, de, fr, hu, uk, ro, pt, el, es, pl, nl, cs, sv) it,
constexpr PCWSTR kItalian[] = {PSV_STRING_LIST(PSV_AS_IT)};
#undef PSV_AS_IT

#define PSV_AS_DE(id, en, it, de, fr, hu, uk, ro, pt, el, es, pl, nl, cs, sv) de,
constexpr PCWSTR kGerman[] = {PSV_STRING_LIST(PSV_AS_DE)};
#undef PSV_AS_DE

#define PSV_AS_FR(id, en, it, de, fr, hu, uk, ro, pt, el, es, pl, nl, cs, sv) fr,
constexpr PCWSTR kFrench[] = {PSV_STRING_LIST(PSV_AS_FR)};
#undef PSV_AS_FR

#define PSV_AS_HU(id, en, it, de, fr, hu, uk, ro, pt, el, es, pl, nl, cs, sv) hu,
constexpr PCWSTR kHungarian[] = {PSV_STRING_LIST(PSV_AS_HU)};
#undef PSV_AS_HU

#define PSV_AS_UK(id, en, it, de, fr, hu, uk, ro, pt, el, es, pl, nl, cs, sv) uk,
constexpr PCWSTR kUkrainian[] = {PSV_STRING_LIST(PSV_AS_UK)};
#undef PSV_AS_UK

#define PSV_AS_RO(id, en, it, de, fr, hu, uk, ro, pt, el, es, pl, nl, cs, sv) ro,
constexpr PCWSTR kRomanian[] = {PSV_STRING_LIST(PSV_AS_RO)};
#undef PSV_AS_RO

#define PSV_AS_PT(id, en, it, de, fr, hu, uk, ro, pt, el, es, pl, nl, cs, sv) pt,
constexpr PCWSTR kPortuguese[] = {PSV_STRING_LIST(PSV_AS_PT)};
#undef PSV_AS_PT

#define PSV_AS_EL(id, en, it, de, fr, hu, uk, ro, pt, el, es, pl, nl, cs, sv) el,
constexpr PCWSTR kGreek[] = {PSV_STRING_LIST(PSV_AS_EL)};
#undef PSV_AS_EL

#define PSV_AS_ES(id, en, it, de, fr, hu, uk, ro, pt, el, es, pl, nl, cs, sv) es,
constexpr PCWSTR kSpanish[] = {PSV_STRING_LIST(PSV_AS_ES)};
#undef PSV_AS_ES

#define PSV_AS_PL(id, en, it, de, fr, hu, uk, ro, pt, el, es, pl, nl, cs, sv) pl,
constexpr PCWSTR kPolish[] = {PSV_STRING_LIST(PSV_AS_PL)};
#undef PSV_AS_PL

#define PSV_AS_NL(id, en, it, de, fr, hu, uk, ro, pt, el, es, pl, nl, cs, sv) nl,
constexpr PCWSTR kDutch[] = {PSV_STRING_LIST(PSV_AS_NL)};
#undef PSV_AS_NL

#define PSV_AS_CS(id, en, it, de, fr, hu, uk, ro, pt, el, es, pl, nl, cs, sv) cs,
constexpr PCWSTR kCzech[] = {PSV_STRING_LIST(PSV_AS_CS)};
#undef PSV_AS_CS

#define PSV_AS_SV(id, en, it, de, fr, hu, uk, ro, pt, el, es, pl, nl, cs, sv) sv,
constexpr PCWSTR kSwedish[] = {PSV_STRING_LIST(PSV_AS_SV)};
#undef PSV_AS_SV

static_assert(std::size(kEnglish) == static_cast<size_t>(StrId::Count));
static_assert(std::size(kItalian) == static_cast<size_t>(StrId::Count));
static_assert(std::size(kGerman) == static_cast<size_t>(StrId::Count));
static_assert(std::size(kFrench) == static_cast<size_t>(StrId::Count));
static_assert(std::size(kHungarian) == static_cast<size_t>(StrId::Count));
static_assert(std::size(kUkrainian) == static_cast<size_t>(StrId::Count));
static_assert(std::size(kRomanian) == static_cast<size_t>(StrId::Count));
static_assert(std::size(kPortuguese) == static_cast<size_t>(StrId::Count));
static_assert(std::size(kGreek) == static_cast<size_t>(StrId::Count));
static_assert(std::size(kSpanish) == static_cast<size_t>(StrId::Count));
static_assert(std::size(kPolish) == static_cast<size_t>(StrId::Count));
static_assert(std::size(kDutch) == static_cast<size_t>(StrId::Count));
static_assert(std::size(kCzech) == static_cast<size_t>(StrId::Count));
static_assert(std::size(kSwedish) == static_cast<size_t>(StrId::Count));

// Both indexed by Lang (see the order note in Strings.h).
constexpr const PCWSTR* kTables[] = {kEnglish,   kItalian,  kGerman,     kFrench, kHungarian,
                                     kUkrainian, kRomanian, kPortuguese, kGreek,  kSpanish,
                                     kPolish,    kDutch,    kCzech,      kSwedish};
constexpr PCWSTR kCodes[] = {L"en", L"it", L"de", L"fr", L"hu", L"uk", L"ro",
                             L"pt", L"el", L"es", L"pl", L"nl", L"cs", L"sv"};

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
