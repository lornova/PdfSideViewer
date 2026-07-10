#pragma once

#include "framework.h"

#include <vector>

// Both MRU lists cap at 9 so every menu entry gets a 1..9 digit mnemonic.
inline constexpr size_t kMruMaxEntries = 9;

// A left+right document pair that was open together (order matters).
struct MruPair {
    std::wstring left;
    std::wstring right;
};

// Session persistence: %APPDATA%\PdfSideViewer\settings.ini, UTF-16 (the file
// is created with a BOM so the WritePrivateProfile* APIs store Unicode paths
// losslessly). INI over JSON: native Win32 read/write, nothing to parse.
struct PaneSettings {
    std::wstring path; // empty = pane was empty
    float zoom = 1.0f;
    float scrollX = 0;
    float scrollY = 0;
    int zoomMode = 2; // PaneWindow::ZoomMode (0 manual, 1 fit width, 2 fit page)
};

struct AppSettings {
    bool hasPlacement = false;
    RECT normalRect{};
    bool maximized = false;
    float splitRatio = 0.5f;
    bool scrollSync = true; // sync is the product: both locks default on
    bool zoomSync = true;
    UINT dpi = 96; // DPI the scroll offsets were saved at
    bool toolbar = true;
    bool statusbar = true;
    bool outline = false;
    std::wstring language = L"en"; // "en" / "it"; anything else falls back to en
    std::vector<std::wstring> mruFiles; // most recent first
    std::vector<MruPair> mruPairs;      // most recent first
    PaneSettings left;
    PaneSettings right;

    static AppSettings Load();
    void Save() const;
};
