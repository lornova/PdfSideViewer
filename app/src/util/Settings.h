#pragma once

#include "framework.h"

// Session persistence: %APPDATA%\PdfSideViewer\settings.ini, UTF-16 (the file
// is created with a BOM so the WritePrivateProfile* APIs store Unicode paths
// losslessly). INI over JSON: native Win32 read/write, nothing to parse.
struct PaneSettings {
    std::wstring path; // empty = pane was empty
    float zoom = 1.0f;
    float scrollX = 0;
    float scrollY = 0;
    int zoomMode = 1; // PaneWindow::ZoomMode (0 manual, 1 fit width, 2 fit page)
};

struct AppSettings {
    bool hasPlacement = false;
    RECT normalRect{};
    bool maximized = false;
    float splitRatio = 0.5f;
    bool scrollSync = false;
    bool zoomSync = false;
    UINT dpi = 96; // DPI the scroll offsets were saved at
    PaneSettings left;
    PaneSettings right;

    static AppSettings Load();
    void Save() const;
};
