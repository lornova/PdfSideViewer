#pragma once

#include "PaneSlots.h"
#include "framework.h"

#include <vector>

// Both MRU lists cap at 9 so every menu entry gets a 1..9 digit mnemonic.
inline constexpr size_t kMruMaxEntries = 9;

// The documents that were open together, by slot (kSlotKeys). An empty entry
// means that slot was not in use, so a two-pane session simply leaves the
// centre empty and an old two-key settings.ini still loads.
struct MruSession {
    PerPane<std::wstring> path;
};

// Per-session sync-point memory ([sync-points], most recent first,
// kMruMaxEntries cap), keyed by the same per-slot paths. Only MANUAL points are
// stored, one 0-based page per ACTIVE slot joined by ':' ("l:r;l:r;..." with
// two panes, "l:c:r;..." with three, pure numbers: no escaping, no buffer
// worries); generated points re-derive from the bookmarks at restore time when
// hadAuto is set.
struct SavedSyncPoints {
    PerPane<std::wstring> path;
    std::wstring manual;
    bool hadAuto = false;
};

// Session persistence: %APPDATA%\PdfSideViewer\settings.ini, UTF-16LE with a
// BOM so Unicode paths round-trip. INI over JSON: the file stays hand-editable
// and the format is a dozen lines of code at each end.
struct PaneSettings {
    std::wstring path; // empty = pane was empty
    float zoom = 1.0f;
    float scrollX = 0;
    float scrollY = 0;
    int zoomMode = 2; // PaneWindow::ZoomMode (0 manual, 1 fit width, 2 fit page)
};

// SyncTeX inverse-search launch template: %f = absolute .tex path, %l = 1-based
// line. A "://" marks a URI (ShellExecute), anything else is a command line.
// The default targets VS Code's protocol handler.
inline constexpr const wchar_t* kDefaultSynctexInverse = L"vscode://file/%f:%l";

// A class, not an aggregate, for ONE reason: the ranges below are an invariant
// of the type rather than a property of the file it was read from. They used to
// hold on the load path only (Load clamped, Save wrote whatever it was handed,
// and MainWindow re-checked three of them by hand), so an out-of-range caller
// could persist a value no reader would ever have accepted. Normalize is the
// single owner and both paths run it; the data itself stays public, since there
// is nothing else here a getter would protect.
class AppSettings {
public:
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
    // Find bar toggles ([find]). Sticky across sessions like the toolbar state,
    // and global rather than per pane: the bar retargets whichever pane has the
    // focus, so a per-pane option would silently change under the user.
    bool findMatchCase = false;
    bool findWholeWord = false;
    bool findRegex = false;
    // Rebar band layout in visual order, "id,cx,break;..." per band (empty =
    // default). Parsed leniently: anything malformed keeps the default row.
    std::wstring rebarBands;
    // Defaults for fresh documents and non-restored launches ([defaults]).
    // defPaneCount is what a launch WITHOUT session restore starts with; the
    // [window] paneCount above is the last session's own arrangement.
    int defPaneCount = 2;
    int defScrollMode = 0; // PaneWindow::ScrollMode (0 continuous, 1 paged)
    int defZoomMode = 2;   // PaneWindow::ZoomMode (0 manual, 1 fit width, 2 fit page)
    bool defScrollSync = true;
    bool defZoomSync = true;
    std::wstring language = L"en"; // Strings.h kCodes ("en".."sv"); anything else falls back to en
    std::wstring synctexInverse = kDefaultSynctexInverse;
    std::vector<std::wstring> mruFiles;  // most recent first
    std::vector<MruSession> mruSessions; // most recent first
    std::vector<SavedSyncPoints> syncPoints; // most recent first
    // Number of pane slots in use: 2 (left+right, the default) or 3, which
    // activates the centre pane. Drives which slots the per-slot load/save
    // below walks, so a two-pane settings.ini never grows a [center] section.
    int paneCount = 2;
    // Three-pane shares of the pane strip, kept apart from splitRatio (the
    // two-pane one) so switching modes never reinterprets the other's value.
    float splitRatio3Left = 1.0f / 3.0f;
    float splitRatio3Center = 1.0f / 3.0f;
    // Per-slot session, stored in the [left]/[center]/[right] sections
    // (kSlotKeys). Every section is READ regardless of paneCount (a parked
    // [center] seeds the fallback of a two-pane session); a section is WRITTEN
    // when its slot is active or its path non-empty, and deleted otherwise, so
    // a pristine two-pane settings.ini never grows a [center] section.
    PerPane<PaneSettings> panes;

    // Reads the whole file through ONE handle and parses it in memory: what
    // Load returns is always a single coherent snapshot, never a mixture of
    // two versions of the file. What it returns is also always NORMALIZED, so
    // no caller has to re-check a range.
    static AppSettings Load();
    // Whole-file write to a uniquely named temp sibling, checked byte counts,
    // then ONE same-volume rename over the target: false = nothing was
    // promoted and the previous coherent file is untouched under its own name.
    // On the local volume the profile lives on, that rename has no partial
    // state, so settings.ini always resolves to one whole version of itself.
    // If a redirected or third-party provider ever broke that, the complete
    // new file is KEPT beside it under its temp name and is not adopted
    // automatically: that case degrades to a manual rename, not to silent
    // recovery of a file that a crash might have truncated. The temp is
    // protected from cleanup only WHILE settings.ini is missing; once any run
    // recreates it, the residue is stale like any other.
    // The replacement carries the DIRECTORY's inherited security, never the
    // old file's own: the settings directory is the declared boundary (see
    // Settings.cpp), so per-file DACLs and per-file EFS do not survive a save.
    // What reaches the file is a NORMALIZED copy: the caller's own object is
    // left untouched, and no value a Load would refuse can be written.
    bool Save() const;

private:
    // The declared range of every field, plus the two rules that span more than
    // one: normalRect only means something with hasPlacement, and two of the
    // three-pane shares have to leave room for the third. Idempotent, and a
    // no-op on anything Load already produced.
    void Normalize();
};

// Directory holding settings.ini (honors the PSV_SETTINGS_DIR test override,
// and fails closed when it is set but unusable); empty on failure. Also hosts
// the settings writer lock.
std::wstring SettingsDirectory();
// Canonical per-user directory for locks over resources that are NOT the
// settings file: independent of PSV_SETTINGS_DIR and of the environment
// entirely, so every copy of the app run by one user meets on the same file.
std::wstring UserLockDirectory();
