#include "util/DialogTemplate.h"

namespace {
// cdit lives at this offset inside DLGTEMPLATE (style + exstyle precede it).
constexpr size_t kCountOffset = sizeof(DWORD) * 2;
} // namespace

DialogTemplate::DialogTemplate(PCWSTR title, int cx, int cy) {
    DLGTEMPLATE header{};
    header.style = DS_SETFONT | DS_MODALFRAME | WS_POPUP | WS_CAPTION | WS_SYSMENU;
    header.cdit = 0; // patched in Data()
    header.x = 0;
    header.y = 0;
    header.cx = static_cast<short>(cx);
    header.cy = static_cast<short>(cy);
    const auto* raw = reinterpret_cast<const uint8_t*>(&header);
    m_bytes.assign(raw, raw + sizeof(header));
    AppendWord(0); // no menu
    AppendWord(0); // default dialog class
    AppendString(title);
    // DS_SETFONT payload: point size + face. The dialog manager scales the
    // dialog-unit grid from this font at the creation DPI.
    AppendWord(8);
    AppendString(L"MS Shell Dlg");
}

void DialogTemplate::AddControl(WORD classAtom, DWORD style, DWORD exStyle, int x, int y, int cx,
                                int cy, WORD id, PCWSTR text) {
    AlignDword(); // every DLGITEMTEMPLATE starts on a DWORD boundary
    DLGITEMTEMPLATE item{};
    item.style = style | WS_CHILD | WS_VISIBLE;
    item.dwExtendedStyle = exStyle;
    item.x = static_cast<short>(x);
    item.y = static_cast<short>(y);
    item.cx = static_cast<short>(cx);
    item.cy = static_cast<short>(cy);
    item.id = id;
    const auto* raw = reinterpret_cast<const uint8_t*>(&item);
    m_bytes.insert(m_bytes.end(), raw, raw + sizeof(item));
    AppendWord(0xFFFF); // class by atom
    AppendWord(classAtom);
    AppendString(text);
    AppendWord(0); // no creation data
    ++m_count;
}

const DLGTEMPLATE* DialogTemplate::Data() {
    auto* header = reinterpret_cast<DLGTEMPLATE*>(m_bytes.data());
    header->cdit = m_count;
    return header;
}

void DialogTemplate::AlignDword() {
    while (m_bytes.size() % sizeof(DWORD) != 0)
        m_bytes.push_back(0);
}

void DialogTemplate::AppendWord(WORD value) {
    const auto* raw = reinterpret_cast<const uint8_t*>(&value);
    m_bytes.insert(m_bytes.end(), raw, raw + sizeof(value));
}

void DialogTemplate::AppendString(PCWSTR text) {
    if (!text)
        text = L"";
    const size_t chars = wcslen(text) + 1; // include the NUL
    const auto* raw = reinterpret_cast<const uint8_t*>(text);
    m_bytes.insert(m_bytes.end(), raw, raw + chars * sizeof(wchar_t));
}
