#include "Strings.h"

#include <iterator>

namespace {

#define PSV_AS_EN(id, en, it) en,
constexpr PCWSTR kEnglish[] = {PSV_STRING_LIST(PSV_AS_EN)};
#undef PSV_AS_EN

#define PSV_AS_IT(id, en, it) it,
constexpr PCWSTR kItalian[] = {PSV_STRING_LIST(PSV_AS_IT)};
#undef PSV_AS_IT

static_assert(std::size(kEnglish) == static_cast<size_t>(StrId::Count));
static_assert(std::size(kItalian) == static_cast<size_t>(StrId::Count));

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
    return (g_lang == Lang::Italian ? kItalian : kEnglish)[index];
}
