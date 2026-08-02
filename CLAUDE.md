# CLAUDE.md

## Project

PdfSideViewer: native Win32 + Direct2D app (Windows 10/11) showing two PDFs side by side with
synchronized scrolling. MuPDF 1.28.x statically linked, C++20, MSVC v143, no .NET/WPF/Qt. GPLv3
(MuPDF is AGPLv3: never suggest relicensing to MIT/BSD). `docs/DESIGN.md` is the authoritative
architecture document, incl. a "hard-won Win32 details" section of constraints that must not
regress.

## Build

MuPDF is NOT tracked in git. First time, and per platform:

```powershell
powershell scripts\get-mupdf.ps1 -Build                      # fetch + build x64
powershell scripts\get-mupdf.ps1 -Build -Platforms x64,ARM64
```

App (both configs must stay warning-clean at /W4):

```powershell
msbuild PdfSideViewer.sln -p:Configuration=Debug   -p:Platform=x64 -m
msbuild PdfSideViewer.sln -p:Configuration=Release -p:Platform=x64 -m
```

MSBuild is at `C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe`
(not on PATH). Output: `build\<platform>\<config>\PdfSideViewer.exe`, a single static exe.

Scripts: `make-release.ps1 -Platform x64` (portable zip); `make-installer.ps1` compiles
`scripts\PdfSideViewer.iss` (Inno Setup 6, UTF-8 BOM required by the accented `[CustomMessages]`)
into a per-user winget-ready installer, version read from resource.h; `make-test-pdfs.ps1`
regenerates `testdata\*.pdf` (hand-built PDFs with links/outline); `make-icon.ps1` regenerates
the icons in `app\res` (app.ico plus the three Explorer verb icons; deterministic
System.Drawing artwork).

- Inno gotcha: `[UninstallRun]` Check params are evaluated at SETUP time, so conditional uninstall
  logic lives in CurUninstallStepChanged (the verbs are removed only if they point into `{app}`: a
  portable/dev copy may own the same HKCU keys).
- Icon and VERSIONINFO live in `app\res\PdfSideViewer.rc`; never add an RT_MANIFEST there (the
  manifest is embedded via the vcxproj `<Manifest>` item, a second copy fails the link).
- MuPDF link: only `libmupdf.lib + libthirdparty.lib + libresources.lib` (the other libs the mupdf
  solution produces are unreferenced); `libresources` exists only in Release and is shared by Debug
  via a fallback library path; the mupdf projects declare v142 and are built with
  `/p:PlatformToolset=v143`.
- `thirdparty\synctex\` (SyncTeX reference parser) IS tracked in git, unlike `vendor\`. The app
  compiles it plus the four zlib `gz*.c` from `vendor\mupdf\thirdparty\zlib` (libthirdparty builds
  zlib WITHOUT the gzFile API: if a future MuPDF adds it, drop the app copies or face LNK2005).
  Those C files build with warnings off and a force-included `synctex_msvc_compat.h` supplying
  `ATTRIBUTE_FORMAT_PRINTF` and, CRITICAL, `#undef`ing `UNICODE/_UNICODE`: the parser calls generic
  shlwapi macros (`PathFindFileName`) with `char*` buffers, and the W variants silently break
  `.synctex` name resolution (only a pointer warning in C, suppressed by design).

## Testing

No unit-test suite. Verification is end-to-end: launch the exe with `testdata\` PDFs, drive it by
posting Win32 messages (WM_MOUSEWHEEL with MK_CONTROL for zoom, WM_COMMAND with the `IDC_*` ids,
WM_VSCROLL, `TVM_*` to the outline tree), assert via GetScrollInfo, window titles, counter text,
clipboard and PrintWindow captures. Rules for test scripts:

- Call `SetThreadDpiAwarenessContext(-4)` FIRST: this dev machine's monitor is 175% (168 DPI) and
  PowerShell is DPI-unaware, so un-aware coordinates/captures are virtualized at 96 DPI (a past
  source of phantom "bugs").
- Set `PSV_SETTINGS_DIR` to a scratch dir before launching: it overrides where settings.ini lives,
  so the real `%APPDATA%\PdfSideViewer\settings.ini` is never touched (suites that hit the real file
  wiped the user's MRU lists whenever a user launch overlapped a test window). Deleting the TEST
  settings file needs a RETRY loop: a scanner holding a handle turns it into delete-pending and the
  file stays visible for a moment.
- One instance at a time, and CHECK first: a foreign (user) instance receives FindWindow-posted
  commands, so abort if `Get-Process PdfSideViewer` is non-empty. The exe must exit with code 0
  after a WM_CLOSE POSTED TO THE FRAME HWND - never `Process.CloseMainWindow()`: .NET picks the
  first visible unowned top-level in z-order, and a transient tooltip SysShadow (TOPMOST, visible,
  ownerless; keyboard focus parked on a toolbar keeps a tip up with no mouse) outranks the frame
  and eats the WM_CLOSE. Restore the user's clipboard if a test touches it.
- `scripts\test-sync-points.ps1` is the in-repo reference suite (sync points); it also encodes the
  PowerShell P/Invoke pitfalls: `$null` coerced to `""` for string parameters (declare NULL-able
  FindWindow arguments as IntPtr) and the startup/dialog races (poll for children after FindWindow,
  wait for the goto dialog's prefill before writing to it).

## Architecture

**Threading contract (load-bearing): the UI thread never calls MuPDF; Direct2D is only used on the
UI thread.** Each pane owns a `Document` (`engine/Document.*`) with one worker thread holding a
cloned `fz_context` (base context + shared locks in `engine/MupdfLib.*`). The worker owns the
`fz_document` exclusively, interprets pages into cached display lists and serves a job queue (Open /
Render / TextPage / Links / Search) with urgent jobs pushed to the front. Results are
heap-allocated structs posted to the pane HWND via `WM_PSV_*` (receiver takes ownership).
Selection/link hit-testing and search highlighting run UI-side on plain-C++ models extracted by the
worker, never on fz objects. Each pane also owns a `util/FileWatcher` thread (auto-reload) watching
the document's parent directory and posting `WM_PSV_FILE_CHANGED` (wParam = the watch GENERATION,
bumped per Watch: a post from the previous watch of the same HWND must not read as a change to the
new document, and joining the old thread cannot retract it; no heap payload, outside the drain
range); debounce, deny-write stability probe and the reload itself (via the view-preserving
`OpenDocumentWithView`) are UI-side.

View pipeline (PaneWindow.cpp): `PageLayout` computes a continuous vertical layout in "content px"
quantized EXACTLY like the worker's `fz_round_rect` on origin-normalized bounds, so page/tile
bitmaps blit 1:1 with no resampling (drift shows up as blur or tile seams). Pages larger than
~1.5×2048px are tiled (`2^res` grid); a capped whole-page preview backs unrendered tiles. Fit modes
recompute zoom per relayout from bar-INDEPENDENT metrics (window rect, not client rect): deriving
fit from the client width feeds back into scrollbar visibility and recurses WM_SIZE unboundedly.

Sync (`view/SyncController.*`) is the product's reason to exist (PDF Architect fails exactly here):
never degrade it to pixel offsets, and test changes with different page formats and different zoom
levels per pane. Positions are exchanged in page units (pageIndex + fraction at viewport center),
never pixels; the pairing is per-slot delta anchors captured at lock time, relative to the
reference slot (the first JOINED pane: one whose DocumentOpened the controller has processed - a
restoring pane holds a document but must not anchor, follow or lead until its view settles) and
confined to the joined subset, so an empty pane never suspends the loaded panes' sync; the
anchors are optionally generalized by a list of WinMerge-style sync points (WHOLE-page
tuples, one page per slot, strictly increasing in EVERY active coordinate; empty map
= bit-identical plain-anchor behavior; Alt and re-lock recapture ONLY while the map is empty; map
cleared on every DocumentOpened, auto points re-derived by MainWindow after a same-path reload,
permuted across Swap Panes via a parked map). With alignment gaps ON (default, `[sync] showGaps`)
the layouts gain WinMerge-style empty gap slots and sync is IDENTITY on virtual SLOT coordinates
(the follower scrolls through its gaps); gaps OFF = piecewise-constant integer delta with the
follower waiting at section ends. PageLayout is slot-based (slot = real page or gap; fit inputs and
every render/goto/counter consumer stay real-page; persisted scroll offsets are normalized to the
no-gap space). Every map mutation fires SyncController's map-changed callback; MainWindow rebuilds
gaps + markers for every pane inside ApplySilently (reentrancy); each segment contributes
max(interior) slots to ALL panes, with the gap silhouettes borrowed from the pane that supplied that
maximum. Auto-generation matches hierarchical numeric bookmark keys via `util/OutlineNumbering`.

**Pane slots** (`PaneSlots.h`): the frame owns a FIXED array of three slots in visual order
(`kSlotLeft`, `kSlotCenter`, `kSlotRight`) of which only some are ACTIVE. `m_paneCount` is 2 by
default (active set {left, right}, centre pane never created) and 3 in the optional three-pane
mode (`[window] paneCount`, View ▸ Two/Three Panes, or implicitly from File ▸ Open Centre and from a third
command-line argument). `SetPaneCount` creates/destroys the centre pane, wires it through the same
`ConfigurePane` used at startup, and CLEARS the sync map: a point tuple carrying a stale centre
coordinate would break the all-coordinates monotonicity the gap arithmetic relies on. ORDER is
load-bearing (a Codex review caught the use-after-free): clear the map FIRST (the empty-map
recapture walks the controller's CURRENT set, so every pane in it must still be alive), install
the new set in SyncController, and only THEN destroy the centre pane. Shrinking parks the centre
session in its fallback and clears `m_lastDoc[center]`; growing REOPENS the park (which also
re-restores the trio's remembered sync points via the pathChanged flow), Close Session wipes it,
and it survives restarts because [center] is always read, written while it holds a path, and
deleted as a section otherwise. Slot indices never move
when the count changes, which is what keeps the settings sections (`[left]`/`[center]`/`[right]`,
`kSlotKeys`), the sync-point tuples and the per-slot session/fallback state meaningful across a mode
switch. Walk panes with `for (slot...) if (SlotOn(slot))` (visual order) or `ForEachPane`; never
assume two. Everything that names a pane is a PaneSlot: the `-open-*` verbs (an inactive slot
switches the mode ON, so a verb always lands) and the pane child ids
(`kPaneChildIdBase + slot`, so left 100, center 101, right 102 - the E2E scripts address them by
`GetDlgItem`). Second-instance handoffs (Explorer verbs AND forward search) travel as ONE
versioned XML payload over WM_COPYDATA (`util/IpcXml.*`, dwData 'PSVX', XmlLite on both ends: OS
component, no COM init, DTD prohibited plus a depth cap on hostile input): the slot is the WORD
`left`/`center`/`right` (= `kSlotKeys`), never an integer, and an unknown version/command is
simply UNHANDLED, so the sender cold-starts and a mixed-version handoff degrades to a new window
instead of mis-slotting. Structural checks (shape, caps, closed vocabulary) live in
`IpcXml::Parse`; SEMANTIC checks (rooted paths, line range, active-set slot) stay in
`MainWindow::HandleCopyData`. The ONE exception is the command line's POSITIONAL order, which is Beyond Compare's
`left right [center]` and therefore needs `kCliSlotOrder` in main.cpp; nothing else may reuse that
order. The two-pane and three-pane splits are stored SEPARATELY (`splitRatio` vs
`splitRatio3Left`/`splitRatio3Center`): one shared value would drag a 50/50 split into the
three-pane layout as 50/33/17 on the first switch. `MatchOutlineNumberings` takes N outlines and returns rows of indices for the
keys present in ALL of them (a join on a canonical key, so more documents just means a smaller
intersection; with two it is identical to the pairwise version it replaced). The MRU (`[mru-pairs]`, still that
section name) and the sync-point memory (`[sync-points]`) are both keyed by the PER-SLOT paths
(`kSlotKeys`): the centre key is optional, so an entry written before the three-pane mode still
loads, an empty slot is deleted rather than blanked, and a session's pane count comes back with it.
Manual points serialize one page per ACTIVE slot ("l:r" or "l:c:r"), and an entry whose arity does
not match the current arrangement is dropped on restore instead of being misread. `PaneWindow` destroys its own HWND in its destructor (a pane can now die
while the frame lives on) and clears `m_hwnd` on WM_NCDESTROY.

MainWindow owns the frame, menu bar (incl. MRU submenus: recent files + recent left/right pairs,
recorded centrally on DocumentOpened, persisted in `[mru-files]`/`[mru-pairs]`), toolbar (Segoe MDL2
glyph imagelist, `util/GlyphIcons.*`), status bar (page/zoom per pane + sync state), splitter, find
bar, outline sidebar, fullscreen (F11/Alt+Enter; hides the chrome without touching the persisted
flags; SaveSession must use the pre-fullscreen placement), session persistence
(`%APPDATA%\PdfSideViewer\settings.ini`, own UTF-16 INI reader/writer, not the
`GetPrivateProfile*` APIs: Save serializes the WHOLE file in memory, writes it to a uniquely named
temp sibling with checked byte counts and swaps it in with ONE same-volume rename
(`MoveFileExW(REPLACE_EXISTING|WRITE_THROUGH)`, NOT `ReplaceFileW`: that one carries the target's
ACL/attributes onto the replacement, worthless for a file that inherits the same ACL from the
profile directory, and buys two documented PARTIAL failure states in which the canonical name is
deleted or renamed; a rename has none, so no backup and no startup recovery are needed - and if a
provider ever left the canonical name missing, the complete temp is kept for a MANUAL rename,
never adopted automatically (a crash-truncated temp is indistinguishable from a whole one) and
only until some run recreates settings.ini: forensic residue, not a recovery slot;
declared security model: the settings DIRECTORY is the boundary, so a per-file DACL or EFS state
does NOT survive a save, and since `[synctex] inverse` is trusted input that reaches
ShellExecute/CreateProcess, whoever can write the directory can run code as the user - which is
why an ELEVATED instance refuses inverse search outright and the manifest pins asInvoker),
cleanup RECONSTRUCTS the names our OWN pid can
produce (`settings.ini.<pid>.<0..63>.tmp`) and deletes those, matching nothing and dating
nothing: a foreign temp is never touched, which is the accepted cost of having no name grammar
and no staleness clock. Ranges live in `AppSettings::Normalize`, run by BOTH Load and Save
(on a copy), so no save can persist a value the next start would refuse. Load reads the whole
file through ONE handle - dozens of independent profile reads are individually coherent but can
straddle a concurrent swap, and the hybrid would be re-persisted; writers serialize best-effort
via a lock file, `util/ScopedFileLock.h`, chosen over a named mutex: profile ACL, crosses RDP
sessions, no predictable kernel name to pre-create; semantics stay last-close-wins, and a
DOWNGRADE round-trip drops keys the older build does not know), and routes pane
`ViewEvent`s to SyncController, the outline, the status bar and the
menu/toolbar checked state (`UpdateCommandUi`).

## Invariants that came from real bugs (see also docs/DESIGN.md)

- Any `PeekMessage` drain loop must re-post a swallowed `WM_QUIT` (it bypasses ALL filters).
- Publish `Document::SetWantedRange` BEFORE issuing render requests; every request/latch
  (`pendingId`, `failedScale`) must be released by a posted result or reset on device loss, or pages
  go permanently blank.
- Results are gated by generation/searchId (`m_openGen`, `m_searchSeq`), not by path or needle echo:
  re-opening the same file must not resurrect stale results. A search query is the
  `(needle, Document::SearchOptions)` PAIR, and `PaneWindow::StartSearch`'s unchanged-query
  early-out compares both: comparing the needle alone silently swallows every option toggle.
- Toolbar `BTNS_AUTOSIZE` label widths are measured ONCE, at `TB_ADDBUTTONSW` time, with the font
  of that moment, and under PMv2 comctl32 swaps its per-DPI font in AFTER `WM_DPICHANGED` returns
  (`TB_AUTOSIZE` does not re-measure): never patch a labeled toolbar in place on a DPI change -
  the command toolbar is recreated (`RebuildToolbarInBand`) and the menu band re-adds its buttons
  on every font change (`MenuBand::SetFont`).
- `ID2D1RenderTarget::GetSize()` returns DIPs even in `D2D1_UNIT_MODE_PIXELS`; use the client rect
  or `GetPixelSize()`.
- Device-loss recovery uses `DxResources::Generation()` so one pane never discards the device the
  sibling just rebuilt; recovery retries are backoff-limited.
- fz_try is setjmp-based: `fz_var` every local mutated inside and read after; keep destructible C++
  objects out of fz frames; drop fz objects in `fz_always`.
- `TBSTYLE_FLAT` only INSIDE a rebar band (there `FLAT|TRANSPARENT` is the required pattern: the
  rebar paints the band background); never on a toolbar whose parent paints nothing under children,
  where flat = a black band.
- The menu bar is a MenuBand toolbar in rebar band 0; the `HMENU` is built but NEVER attached to the
  window (fullscreen just hides the rebar), so `GetMenu` returns nothing and menu state can only be
  refreshed from `WM_INITMENUPOPUP` (which `TrackPopupMenuEx` does deliver to the frame): Edit ▸
  Copy follows the selection and Find Next/Previous follow the match list, and neither of those
  posts a view event. MainWindow owns the HMENU and every WM_COMMAND; MenuBand
  owns tracking (WH_MSGFILTER hook scoped to the track loop, EndMenu+retrack for hover/arrow
  switches; the Alt+scroll SC_KEYMENU swallow stays FIRST). Tracking never starts inside
  TBN_DROPDOWN (posted kMsgTrack: comctl32 paints the dropped button hot while the notify is in
  flight); the hook hit-tests `MSG::pt`, never `GetMessagePos()` (stale in menu loops); the subclass
  swallows WM_MOUSEMOVE while tracking (synthetic reposts would re-light the previous button).
- Rebar bands are addressed by RBBIM_ID, never by index (unlocked, the user reorders them). comctl32
  forces RBBS_FIXEDSIZE bands to the end of their row on EVERY layout: the page box holds the bit
  only while row-last (ApplyPageBoxFixedSize); RBN_HEIGHTCHANGE re-runs Layout behind the
  m_layingOut guard; the menu chevron popup must RemoveMenu its shared submenus before DestroyMenu.
  USECHEVRON clips the CHILD under cxIdeal and band borders are exposed by no API: the menu band cx
  is measured (UpdateRebarBandSizes), never ideal+constant. The measure needs the band VISIBLE (the
  rebar never lays out a hidden band's child): full screen hides the menu band unconditionally, so
  the measure is skipped there and re-run when full screen exits. Hiding a band also makes comctl32
  MERGE its row into the next one (the next band's RBBS_BREAK is cleared for good): full screen
  snapshots the band layout at entry, forces its own (ApplyFullscreenBandLayout: one locked row,
  page box right-aligned via FIXEDSIZE, lock setting overridden - SetRebarLocked only records the
  setting while full screen), restores the snapshot at exit (lock styles FIRST, then
  ApplyRebarLayout: the snapshot cx values include the user's gripper state), and
  SerializeRebarLayout returns the snapshot while full screen so a session saved there keeps the
  real layout.
- Cross-process E2E: `SetWindowText`/`GetWindowText` on another process's control DO NOT deliver
  WM_SETTEXT/WM_GETTEXT (SetWindowText even returns success, touching only the caption cache); test
  scripts must SEND `WM_SETTEXT` explicitly.
- `wWinMain` clears `ELECTRON_RUN_AS_NODE` before anything can spawn a child. Started FROM VS Code
  (its debug launcher, or an extension launching an external viewer) we inherit it, every process we
  start inherits our block in turn, and Electron reading it boots as plain Node: the `vscode://`
  inverse-search handler then rejects `--open-url` and exits, so Ctrl+click silently does nothing and
  `ShellExecuteW` cannot report it (the process DID start). Only that variable - the `VSCODE_*` ones
  are harmless. Do not "clean up" this call: launched from Explorer the bug is invisible.
- Any epsilon meant to attribute a PAGE from a fractional sync position must survive the
  page->pixel->page round trip (scrolls quantize to whole pixels): `MapTarget`'s wait clamp is 1% of
  a page for this reason; 0.0001 parked the center ON the boundary and the counter flipped to the
  next page under 96-DPI RDP metrics.

## Conventions

- Path splitting and plain file existence/removal go through `<filesystem>` (`fs::exists`,
  `fs::remove`, `fs::is_directory`, `path::filename`/`parent_path`), always the `error_code`
  overloads. The Win32 call stays only where the standard cannot express the requirement, and
  each such site says why: `MoveFileExW` in Settings.cpp (`std::filesystem::rename` adds
  `MOVEFILE_COPY_ALLOWED` and cannot ask for `MOVEFILE_WRITE_THROUGH`), `CreateFileW` for the
  settings read (iostreams open with `_SH_DENYNO`, so no `FILE_SHARE_DELETE` and a concurrent
  atomic swap would fail), `GetFullPathNameW` (lexical, collapses `..`, does not touch the disk:
  neither `absolute` nor `canonical` does that), and `DirOf` in SyncTex.cpp (its consumer needs
  a prefix with NO trailing separator, which `parent_path` does not give at a root).
- clang-format-ish 100 columns, 4 spaces; comments explain constraints, not what the next line does.
  The maintainer communicates in Italian.
- Every user-visible string goes through `util/Strings.h`: an X-list with one column per language,
  in Lang-enum = language-id = code order: en (en-GB), it, de, fr, hu, uk, ro, pt (pt-PT), el,
  es (es-ES), pl, nl, cs, sv. The menu displays them alphabetically by native name. English is the
  default and what E2E tests assert against. Engine-level error strings (engine/Document.cpp) stay
  English: workers cache them in result structs.
- `enum CommandId` in MainWindow.h is the single registry of WM_COMMAND/accelerator ids (menu,
  toolbar and accelerators reuse the same ids; 1017..1019, 1025..1026, 1056..1058 and 1059..1072
  (the language group, in Lang-enum order; 1023..1024 retired) must stay contiguous for
  CheckMenuRadioItem; 1030+/1040+ are the MRU ranges, kMruMaxEntries slots each, dispatched as
  ranges in WM_COMMAND). Control ids live in a separate >= 2000 space (2001 page box, 2100+ Options
  dialog, 2201 goto dialog, 2300+ menu band, 2400+ sync points dialog) so they can never collide
  with command dispatch.
- Session settings are versionless: add keys with safe defaults, never repurpose existing ones.
  `[defaults]` holds the new-document defaults (pane count, scroll mode, zoom mode, sync locks),
  applied when session restore is off and to every fresh OpenDocument. `[defaults] paneCount` and
  `[window] paneCount` are NOT the same thing: the first is what a launch without session restore
  starts with (and what File ▸ Close Session returns to, in both directions), the second is the
  arrangement the last session happened to have.
