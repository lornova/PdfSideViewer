#include "SyncTex.h"

#include <synctex_parser.h>

namespace {

// The parser opens files with narrow fopen/gzopen: pure-ASCII paths pass
// through, anything else falls back to the 8.3 alias (volumes without 8.3
// generation degrade gracefully to "no SyncTeX data").
std::string ToParserPath(const std::wstring& path) {
    std::wstring usable = path;
    const auto isAscii = [](const std::wstring& s) {
        return std::all_of(s.begin(), s.end(), [](wchar_t c) { return c > 0 && c < 0x80; });
    };
    if (!isAscii(usable)) {
        wchar_t shortBuf[1024];
        const DWORD n = GetShortPathNameW(path.c_str(), shortBuf, ARRAYSIZE(shortBuf));
        if (n == 0 || n >= ARRAYSIZE(shortBuf))
            return {};
        usable.assign(shortBuf, n);
        if (!isAscii(usable))
            return {};
    }
    std::string narrow(usable.size(), '\0');
    for (size_t i = 0; i < usable.size(); ++i)
        narrow[i] = static_cast<char>(usable[i]);
    return narrow;
}

std::string ToUtf8(const std::wstring& s) {
    if (s.empty())
        return {};
    const int len = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                                        nullptr, 0, nullptr, nullptr);
    if (len <= 0)
        return {};
    std::string out(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), out.data(), len,
                        nullptr, nullptr);
    return out;
}

std::wstring FromUtf8(const char* s) {
    if (!s || !*s)
        return {};
    const int len = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    if (len <= 1)
        return {};
    std::wstring out(static_cast<size_t>(len - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s, -1, out.data(), len);
    return out;
}

std::wstring DirOf(const std::wstring& path) {
    const size_t sep = path.find_last_of(L"\\/");
    return sep == std::wstring::npos ? std::wstring() : path.substr(0, sep);
}

std::wstring Canonicalize(const std::wstring& path) {
    wchar_t full[1024];
    const DWORD n = GetFullPathNameW(path.c_str(), ARRAYSIZE(full), full, nullptr);
    return (n == 0 || n >= ARRAYSIZE(full)) ? path : std::wstring(full, n);
}

// The .synctex records names as TeX saw them: MiKTeX writes the absolute
// Windows path with backslashes, TeX Live typically "./main.tex" with forward
// slashes. Try the as-given absolute form first, then the slash variant, the
// pdf-relative "./" form and finally the bare file name.
std::vector<std::string> TexNameCandidates(const std::wstring& texPath,
                                           const std::wstring& pdfDir) {
    const std::wstring full = Canonicalize(texPath);
    std::wstring slashes = full;
    std::replace(slashes.begin(), slashes.end(), L'\\', L'/');

    std::vector<std::string> candidates;
    candidates.push_back(ToUtf8(full));
    candidates.push_back(ToUtf8(slashes));

    std::wstring dirSlashes = pdfDir;
    std::replace(dirSlashes.begin(), dirSlashes.end(), L'\\', L'/');
    if (!dirSlashes.empty() && slashes.size() > dirSlashes.size() + 1 &&
        CompareStringOrdinal(slashes.c_str(), static_cast<int>(dirSlashes.size()),
                             dirSlashes.c_str(), static_cast<int>(dirSlashes.size()),
                             TRUE) == CSTR_EQUAL &&
        slashes[dirSlashes.size()] == L'/') {
        candidates.push_back(ToUtf8(L"./" + slashes.substr(dirSlashes.size() + 1)));
    }

    const size_t base = slashes.find_last_of(L'/');
    if (base != std::wstring::npos && base + 1 < slashes.size())
        candidates.push_back(ToUtf8(slashes.substr(base + 1)));
    return candidates;
}

} // namespace

bool SyncTexIndex::EnsureLoaded(const std::wstring& pdfPath) {
    if (m_scanner && lstrcmpiW(m_pdfPath.c_str(), pdfPath.c_str()) == 0)
        return true;
    Reset();
    const std::string narrow = ToParserPath(pdfPath);
    if (narrow.empty())
        return false;
    m_scanner = synctex_scanner_new_with_output_file(narrow.c_str(), nullptr, 1);
    if (m_scanner)
        m_pdfPath = pdfPath;
    return m_scanner != nullptr;
}

void SyncTexIndex::Reset() {
    if (m_scanner) {
        synctex_scanner_free(m_scanner);
        m_scanner = nullptr;
    }
    m_pdfPath.clear();
}

std::optional<SyncTexIndex::InverseHit> SyncTexIndex::SourceAt(int pageIndex, float xPt,
                                                               float yPt) {
    if (!m_scanner || pageIndex < 0)
        return std::nullopt;
    if (synctex_edit_query(m_scanner, pageIndex + 1, xPt, yPt) <= 0)
        return std::nullopt;
    synctex_node_p node = synctex_scanner_next_result(m_scanner);
    if (!node)
        return std::nullopt;

    InverseHit hit;
    hit.line = synctex_node_line(node);
    hit.column = synctex_node_column(node);
    std::wstring name = FromUtf8(synctex_scanner_get_name(m_scanner, synctex_node_tag(node)));
    if (name.empty() || hit.line < 1)
        return std::nullopt;
    std::replace(name.begin(), name.end(), L'/', L'\\');
    const bool relative = name.size() < 2 || name[1] != L':';
    if (relative && name.rfind(L"\\\\", 0) != 0)
        name = DirOf(m_pdfPath) + L"\\" + name;
    hit.texPath = Canonicalize(name);
    return hit;
}

std::vector<Document::RectPt> SyncTexIndex::ForwardBoxes(const std::wstring& texPath, int line,
                                                         int& outPageIndex) {
    outPageIndex = -1;
    std::vector<Document::RectPt> boxes;
    if (!m_scanner || line < 1)
        return boxes;

    for (const std::string& name : TexNameCandidates(texPath, DirOf(m_pdfPath))) {
        if (name.empty() ||
            synctex_display_query(m_scanner, name.c_str(), line, -1, 0) <= 0)
            continue;
        synctex_node_p node = nullptr;
        while ((node = synctex_scanner_next_result(m_scanner)) != nullptr) {
            const int page = synctex_node_page(node) - 1; // synctex pages are 1-based
            if (outPageIndex < 0)
                outPageIndex = page; // lock onto the first result's page
            if (page != outPageIndex)
                continue;
            const float h = synctex_node_visible_h(node);
            // v is the BASELINE, not the top edge; width may be negative
            // (kerns): normalize into a top-down RectPt.
            const float v = synctex_node_visible_v(node);
            const float w = synctex_node_visible_width(node);
            Document::RectPt box;
            box.x0 = std::min(h, h + w);
            box.x1 = std::max(h, h + w);
            box.y0 = v - synctex_node_visible_height(node);
            box.y1 = v + synctex_node_visible_depth(node);
            boxes.push_back(box);
        }
        if (!boxes.empty())
            return boxes;
        outPageIndex = -1;
    }
    return boxes;
}
