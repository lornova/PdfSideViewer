#pragma once

#include "framework.h"

#include <vector>

// In-memory DLGTEMPLATE builder for DialogBoxIndirectParamW: the repo ships
// no dialog resources (everything is programmatic), and the dialog manager
// gives modality, Tab/Enter/Esc navigation and DS_SETFONT typography for
// free, which a hand-rolled modal window would have to re-implement.
// Coordinates are DIALOG UNITS (scaled by the dialog font at creation).
class DialogTemplate {
public:
    // Predefined window-class atoms for DLGITEMTEMPLATE.
    static constexpr WORD kButton = 0x0080;
    static constexpr WORD kEdit = 0x0081;
    static constexpr WORD kStatic = 0x0082;
    static constexpr WORD kListBox = 0x0083;
    static constexpr WORD kComboBox = 0x0085;

    DialogTemplate(PCWSTR title, int cx, int cy);
    void AddControl(WORD classAtom, DWORD style, DWORD exStyle, int x, int y, int cx, int cy,
                    WORD id, PCWSTR text);
    // Class-by-name variant for comctl32 classes (SysListView32, ...), which
    // have no predefined DLGITEMTEMPLATE atom.
    void AddControl(PCWSTR className, DWORD style, DWORD exStyle, int x, int y, int cx, int cy,
                    WORD id, PCWSTR text);
    // Finalizes the item count and returns the template (valid until the
    // builder is modified or destroyed).
    const DLGTEMPLATE* Data();

private:
    void AlignDword();
    void AppendWord(WORD value);
    void AppendString(PCWSTR text);

    std::vector<uint8_t> m_bytes;
    WORD m_count = 0;
};
