#include "util/OutlineNumbering.h"

#include <cwctype>
#include <map>
#include <set>

// Grammar accepted by ParseOutlineNumbering:
//
//   title := ws* intro? key rest
//   intro := word '.'? ws*    lowercase(word) in { capitolo, cap, chapter, ch,
//                             sezione, sez, section, sec, parte, part,
//                             appendice, appendix, annex, allegato }
//          | '§' ws*
//   key   := comp ('.' comp)* '.'?
//   comp  := [0-9]{1,6} | single ASCII letter
//   rest  := end-of-string | (ws | ':' | ')' | '-' | '–' | '—') anything
//
// The intro word is tokenized as the longest run of ASCII letters and compared
// WHOLE against the list: "Sezioni 2" must not match "Sez". Digits are ASCII
// only (locale-dependent Unicode digits would make keys unstable across
// documents) and capped at six per component (rejects ISBN/date noise and can
// never overflow the int accumulator). Letter components ("Appendice A",
// "A.1 Notation") encode NEGATIVE (-1 = A) so they can never collide with
// numeric components; a LONE letter without an intro word is rejected as a
// word, not a numbering ("A day...", "E ora..."). "3 Title" is a valid
// single-level key by design; the noise it admits ("2026 Report" -> [2026])
// is harmless because a key only produces a sync point when the identical key
// parses out of the other document too. Titles that parse to no key at all
// participate in the matcher's TITLE channel instead (trimmed,
// case-insensitive equality): "Sommario", "Indice analitico", "Bibliografia"
// pair up by themselves when both documents carry them.

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
    static constexpr PCWSTR kWords[] = {L"capitolo",  L"cap",      L"chapter", L"ch",
                                        L"sezione",   L"sez",      L"section", L"sec",
                                        L"parte",     L"part",     L"appendice",
                                        L"appendix",  L"annex",    L"allegato"};
    for (PCWSTR w : kWords)
        if (lower == w)
            return true;
    return false;
}

// A letter component must be a SINGLE letter ("A", not "AB").
bool IsSingleLetterAt(const std::wstring& title, size_t i) {
    return i < title.size() && IsAsciiAlpha(title[i]) &&
           (i + 1 >= title.size() || !IsAsciiAlpha(title[i + 1]));
}

std::wstring NormalizedTitleKey(const std::wstring& title) {
    size_t b = 0;
    size_t e = title.size();
    while (b < e && IsWs(title[b]))
        ++b;
    while (e > b && IsWs(title[e - 1]))
        --e;
    std::wstring key = title.substr(b, e - b);
    if (!key.empty())
        CharLowerBuffW(key.data(), static_cast<DWORD>(key.size()));
    return key;
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

    bool hadIntro = false;
    if (i < n && title[i] == L'§') {
        ++i;
        skipWs();
        hadIntro = true;
    } else if (i < n && IsAsciiAlpha(title[i]) && !IsSingleLetterAt(title, i)) {
        // A multi-letter run can only be an intro word; a single letter falls
        // through to the component parser ("A.1 Notation").
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
        hadIntro = true;
    }

    std::vector<int> key;
    for (;;) {
        int value;
        if (i < n && IsAsciiDigit(title[i])) {
            value = 0;
            size_t digits = 0;
            while (i < n && IsAsciiDigit(title[i])) {
                if (++digits > 6)
                    return std::nullopt;
                value = value * 10 + (title[i] - L'0');
                ++i;
            }
        } else if (IsSingleLetterAt(title, i)) {
            // Encoded negative (-1 = A): letter components can never collide
            // with numeric ones ("Appendice A" vs "Capitolo 1").
            value = -(static_cast<int>(towupper(title[i])) - L'A' + 1);
            ++i;
        } else {
            return std::nullopt; // a component must follow the start or a '.'
        }
        key.push_back(value);
        if (i < n && title[i] == L'.') {
            ++i;
            if ((i < n && IsAsciiDigit(title[i])) || IsSingleLetterAt(title, i))
                continue;   // "1.2", "A.1" -> next component
            break;          // trailing dot: "1.2." ends the key
        }
        break;
    }

    if (i < n && !IsTerminator(title[i]))
        return std::nullopt; // "1.2Foo"
    // A lone letter with no intro word is a WORD, not a numbering ("A day at
    // the races", "E ora?"): letters need an intro ("Appendice A") or more
    // components ("A.1").
    if (!hadIntro && key.size() == 1 && key[0] < 0)
        return std::nullopt;
    return key;
}

std::wstring FormatOutlineNumbering(const std::vector<int>& key) {
    std::wstring text;
    for (int component : key) {
        if (!text.empty())
            text.push_back(L'.');
        if (component < 0)
            text.push_back(static_cast<wchar_t>(L'A' - component - 1)); // -1 = A
        else
            text += std::to_wstring(component);
    }
    return text;
}

std::vector<std::pair<int, int>> MatchOutlineNumberings(
    const std::vector<Document::OutlineItem>& left,
    const std::vector<Document::OutlineItem>& right) {
    // Two channels, first occurrence wins per key and per side: numbered
    // titles pair by numeric key; unnumbered ones pair by whole normalized
    // title ("Sommario", "Indice analitico") - language-mixed pairs simply
    // never title-match, and out-of-order accidental pairs die in the
    // monotonicity filter downstream.
    std::map<std::vector<int>, int> rightByKey;
    std::map<std::wstring, int> rightByTitle;
    for (size_t i = 0; i < right.size(); ++i) {
        if (auto key = ParseOutlineNumbering(right[i].title)) {
            rightByKey.emplace(std::move(*key), static_cast<int>(i));
        } else if (std::wstring t = NormalizedTitleKey(right[i].title); !t.empty()) {
            rightByTitle.emplace(std::move(t), static_cast<int>(i));
        }
    }
    std::vector<std::pair<int, int>> matches;
    std::set<std::vector<int>> leftSeen;
    std::set<std::wstring> leftSeenTitles;
    for (size_t i = 0; i < left.size(); ++i) {
        if (auto key = ParseOutlineNumbering(left[i].title)) {
            const auto it = rightByKey.find(*key);
            if (it == rightByKey.end())
                continue;
            if (!leftSeen.insert(std::move(*key)).second)
                continue; // duplicate key on the left: first occurrence wins
            matches.emplace_back(static_cast<int>(i), it->second);
        } else if (std::wstring t = NormalizedTitleKey(left[i].title); !t.empty()) {
            const auto it = rightByTitle.find(t);
            if (it == rightByTitle.end())
                continue;
            if (!leftSeenTitles.insert(std::move(t)).second)
                continue;
            matches.emplace_back(static_cast<int>(i), it->second);
        }
    }
    return matches;
}
