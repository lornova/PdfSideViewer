#include "util/OutlineNumbering.h"

#include <cwctype>
#include <map>
#include <set>

// Grammar accepted by ParseOutlineNumbering:
//
//   title := ws* intro? key rest
//   intro := word '.'? ws*    lowercase(word) in { capitolo, cap, chapter, ch,
//                             sezione, sez, section, sec, parte, part }
//          | '§' ws*
//   key   := num ('.' num)* '.'?     num := [0-9]{1,6}
//   rest  := end-of-string | (ws | ':' | ')' | '-' | '–' | '—') anything
//
// The intro word is tokenized as the longest run of ASCII letters and compared
// WHOLE against the list: "Sezioni 2" must not match "Sez". Digits are ASCII
// only (locale-dependent Unicode digits would make keys unstable across
// documents) and capped at six per component (rejects ISBN/date noise and can
// never overflow the int accumulator). "3 Title" is a valid single-level key
// by design; the noise it admits ("2026 Report" -> [2026]) is harmless because
// a key only produces a sync point when the identical key parses out of the
// other document too.

namespace {

bool IsWs(wchar_t c) {
    return c == L' ' || c == L'\t' || c == L' '; // NBSP shows up in PDF titles
}

bool IsAsciiDigit(wchar_t c) {
    return c >= L'0' && c <= L'9';
}

bool IsAsciiAlpha(wchar_t c) {
    return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z');
}

bool IsTerminator(wchar_t c) {
    return IsWs(c) || c == L':' || c == L')' || c == L'-' || c == L'–' || c == L'—';
}

bool IsIntroWord(const std::wstring& lower) {
    static constexpr PCWSTR kWords[] = {L"capitolo", L"cap", L"chapter", L"ch", L"sezione",
                                        L"sez",      L"section", L"sec", L"parte", L"part"};
    for (PCWSTR w : kWords)
        if (lower == w)
            return true;
    return false;
}

} // namespace

std::optional<std::vector<int>> ParseOutlineNumbering(const std::wstring& title) {
    const size_t n = title.size();
    size_t i = 0;
    const auto skipWs = [&] {
        while (i < n && IsWs(title[i]))
            ++i;
    };
    skipWs();

    if (i < n && title[i] == L'§') {
        ++i;
        skipWs();
    } else if (i < n && IsAsciiAlpha(title[i])) {
        size_t j = i;
        std::wstring word;
        while (j < n && IsAsciiAlpha(title[j])) {
            word.push_back(static_cast<wchar_t>(towlower(title[j])));
            ++j;
        }
        if (!IsIntroWord(word))
            return std::nullopt;
        i = j;
        if (i < n && title[i] == L'.')
            ++i;
        skipWs();
    }

    std::vector<int> key;
    for (;;) {
        if (i >= n || !IsAsciiDigit(title[i]))
            return std::nullopt; // a component must follow the start or a '.'
        int value = 0;
        size_t digits = 0;
        while (i < n && IsAsciiDigit(title[i])) {
            if (++digits > 6)
                return std::nullopt;
            value = value * 10 + (title[i] - L'0');
            ++i;
        }
        key.push_back(value);
        if (i < n && title[i] == L'.') {
            ++i;
            if (i < n && IsAsciiDigit(title[i]))
                continue;   // "1.2" -> next component
            break;          // trailing dot: "1.2." ends the key
        }
        break;
    }

    if (i < n && !IsTerminator(title[i]))
        return std::nullopt; // "1.2Foo"
    return key;
}

std::wstring FormatOutlineNumbering(const std::vector<int>& key) {
    std::wstring text;
    for (int component : key) {
        if (!text.empty())
            text.push_back(L'.');
        text += std::to_wstring(component);
    }
    return text;
}

std::vector<std::pair<int, int>> MatchOutlineNumberings(
    const std::vector<Document::OutlineItem>& left,
    const std::vector<Document::OutlineItem>& right) {
    std::map<std::vector<int>, int> rightByKey;
    for (size_t i = 0; i < right.size(); ++i) {
        if (auto key = ParseOutlineNumbering(right[i].title))
            rightByKey.emplace(std::move(*key), static_cast<int>(i)); // first occurrence wins
    }
    std::vector<std::pair<int, int>> matches;
    std::set<std::vector<int>> leftSeen;
    for (size_t i = 0; i < left.size(); ++i) {
        auto key = ParseOutlineNumbering(left[i].title);
        if (!key)
            continue;
        const auto it = rightByKey.find(*key);
        if (it == rightByKey.end())
            continue;
        if (!leftSeen.insert(std::move(*key)).second)
            continue; // duplicate key on the left: first occurrence wins
        matches.emplace_back(static_cast<int>(i), it->second);
    }
    return matches;
}
