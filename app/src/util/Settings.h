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

// Per-pair sync-point memory ([sync-points], most recent first, kMruMaxEntries
// cap). Only MANUAL points are stored ("l:r;l:r;..." 0-based page pairs, pure
// numbers: no escaping, no buffer-length worries); generated points re-derive
// from the bookmarks at restore time when hadAuto is set.
struct SavedSyncPoints {
    std::wstring left;
    std::wstring right;
    std::wstring manual;
    bool hadAuto = false;
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
    bool showGaps = true;    // render WinMerge-style alignment gaps for sync points
    bool showAnchors = true; // draw the anchor glyph beside sync-point pages
    bool showTicks = true;   // draw the sync-point tick strip along the scrollbar
    int scrollMode = 0; // PaneWindow::ScrollMode (0 continuous, 1 paged); global like sync
    UINT dpi = 96; // DPI the scroll offsets were saved at
    bool toolbar = true;
    bool statusbar = true;
    bool outline = false;
    bool restoreSession = true; // startup: reopen the last session vs start empty
    int wheelLines = 0;         // continuous-scroll wheel lines per notch; 0 = system value
    int outlineWidth = 260;     // outline sidebar width, DIP
    bool rebarLocked = true;    // IE-style "lock the toolbars": no grippers, no dragging
    // IE-style toolbar text options: 0 = no text labels, 1 = show text labels
    // (below the icons, the default), 2 = selective text on right.
    int toolbarText = 1;
    bool fsToolbar = false; // full screen: keep the full toolbar visible
    bool fsStatus = false;  // full screen: keep the status bar visible
    bool showHeader = true;      // per-pane header strip with the PDF file name/path
    bool headerShowPath = false; // header shows the full path instead of the file name
    // Rebar band layout in visual order, "id,cx,break;..." per band (empty =
    // default). Parsed leniently: anything malformed keeps the default row.
    std::wstring rebarBands;
    // Defaults for fresh documents and non-restored launches ([defaults]).
    int defScrollMode = 0; // PaneWindow::ScrollMode (0 continuous, 1 paged)
    int defZoomMode = 2;   // PaneWindow::ZoomMode (0 manual, 1 fit width, 2 fit page)
    bool defScrollSync = true;
    bool defZoomSync = true;
    std::wstring language = L"en"; // "en"/"it"/"de"/"fr"/"hu"; anything else falls back to en
    // SyncTeX inverse-search launch template: %f = absolute .tex path,
    // %l = 1-based line. A "://" marks a URI (ShellExecute), anything else is
    // a command line. Default targets VS Code's protocol handler.
    std::wstring synctexInverse = L"vscode://file/%f:%l";
    std::vector<std::wstring> mruFiles; // most recent first
    std::vector<MruPair> mruPairs;      // most recent first
    std::vector<SavedSyncPoints> syncPoints; // most recent first
    PaneSettings left;
    PaneSettings right;

    static AppSettings Load();
    void Save() const;
};
