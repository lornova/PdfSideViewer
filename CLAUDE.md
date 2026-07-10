# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

PdfSideViewer: a native Win32 + Direct2D Windows 10/11 app showing two PDFs side by side with
synchronized scrolling. Rendering engine is MuPDF 1.28.x, statically linked. C++20, MSVC v143,
no .NET/WPF/Qt. License: GPLv3 (MuPDF is AGPLv3; never suggest relicensing to MIT/BSD).
`docs/DESIGN.md` is the authoritative architecture document, including a "hard-won Win32
details" section of constraints that must not regress.

## Build

MuPDF is NOT tracked in git. First time (and per platform):

```
powershell scripts\get-mupdf.ps1 -Build                       # fetch + build x64
powershell scripts\get-mupdf.ps1 -Build -Platforms x64,ARM64
```

App (both must stay warning-clean at /W4):

```
msbuild PdfSideViewer.sln -p:Configuration=Debug   -p:Platform=x64 -m
msbuild PdfSideViewer.sln -p:Configuration=Release -p:Platform=x64 -m
```

MSBuild lives at `C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe`
(not on PATH). Output: `build\<platform>\<config>\PdfSideViewer.exe` (single static exe).
`scripts\make-release.ps1 -Platform x64` zips a portable release.
`scripts\make-test-pdfs.ps1` regenerates `testdata\*.pdf` (hand-built PDFs with links/outline).
`scripts\make-icon.ps1` regenerates `app\res\app.ico` (deterministic System.Drawing artwork);
the icon and VERSIONINFO live in `app\res\PdfSideViewer.rc` (never add an RT_MANIFEST there:
the manifest is embedded via the vcxproj `<Manifest>` item and a second copy fails the link).

MuPDF link facts: only `libmupdf.lib + libthirdparty.lib + libresources.lib` (the other libs the
mupdf solution produces are unreferenced); `libresources` exists only in Release and is shared by
Debug via a fallback library path; the mupdf projects declare v142 and are built with
`/p:PlatformToolset=v143`.

`thirdparty\synctex\` (SyncTeX reference parser) IS tracked in git, unlike `vendor\`. The app
compiles it plus the four zlib `gz*.c` from `vendor\mupdf\thirdparty\zlib` (libthirdparty builds
zlib WITHOUT the gzFile API — if a future MuPDF adds it, drop the app copies or face LNK2005).
The vendored C files build with warnings off and a force-included `synctex_msvc_compat.h` that
supplies `ATTRIBUTE_FORMAT_PRINTF` and — CRITICAL — `#undef`s `UNICODE/_UNICODE`: the parser
calls generic-text shlwapi macros (`PathFindFileName`) with `char*` buffers, and the W variants
silently break `.synctex` name resolution (only a pointer WARNING in C, suppressed by design).

## Testing

There is no unit-test suite. Verification is end-to-end: launch the exe with `testdata\` PDFs and
drive it by posting Win32 messages (WM_MOUSEWHEEL with MK_CONTROL for zoom, WM_COMMAND with the
IDC_* ids, WM_VSCROLL, TVM_* to the outline tree), then assert via GetScrollInfo, window titles,
counter text, clipboard, and PrintWindow captures. Test scripts MUST call
`SetThreadDpiAwarenessContext(-4)` first: this dev machine's monitor is 175% (168 DPI) and
PowerShell is DPI-unaware, so un-aware coordinates/captures are virtualized at 96 DPI (a past
source of phantom "bugs"). Run one instance at a time — and CHECK first: a foreign (user)
instance both receives FindWindow-posted commands and rewrites settings.ini at close, which has
already produced phantom failures; abort the test run if `Get-Process PdfSideViewer` is non-empty
and verify that a settings.ini deletion actually happened — with a RETRY loop: a scanner
briefly holding a handle turns the delete into delete-pending and the file stays visible for a
moment. The exe must always exit with code 0 after CloseMainWindow. Restore the user's
clipboard if a test touches it.

## Architecture

Threading contract (the load-bearing rule): **the UI thread never calls MuPDF; Direct2D is only
used on the UI thread.** Each pane owns a `Document` (engine/Document.\*) with one worker thread
holding a cloned `fz_context` (base context + shared locks in engine/MupdfLib.\*). The worker
owns the `fz_document` exclusively, interprets pages into cached display lists, and serves a
job queue (Open / Render / TextPage / Links / Search) with urgent jobs pushed to the front.
Results are heap-allocated structs posted to the pane HWND via `WM_PSV_*` messages (receiver
takes ownership). Selection/link hit-testing and search highlighting run UI-side on plain-C++
models extracted by the worker, never on fz objects. Each pane also owns a `util/FileWatcher`
thread (auto-reload): it watches the document's parent directory and posts
`WM_PSV_FILE_CHANGED` (no payload, outside the drain range); debounce, deny-write stability
probe and the actual reload (via the view-preserving `OpenDocumentWithView` path) are UI-side.

View pipeline (PaneWindow.cpp): `PageLayout` computes a continuous vertical layout in "content
px" quantized EXACTLY like the worker's `fz_round_rect` on origin-normalized bounds, so page/tile
bitmaps blit 1:1 with no resampling (any drift shows as blur or tile seams). Pages larger than
~1.5×2048px are tiled (`2^res` grid); a capped whole-page preview backs unrendered tiles. Fit
modes recompute zoom per relayout from bar-INDEPENDENT metrics (window rect, not client rect):
deriving fit from the client width feeds back into scrollbar visibility and recurses WM_SIZE
unboundedly.

Sync (view/SyncController.\*): positions are exchanged in page units (pageIndex + fraction at
viewport center), never pixels; the pairing is a delta anchor captured at lock time. This is the
product's reason to exist (PDF Architect fails exactly here): never degrade sync to pixel
offsets, and test sync changes with different page formats and different zoom levels per pane.

MainWindow owns the frame, menu bar (incl. MRU submenus: recent files + recent left/right
pairs, recorded centrally on DocumentOpened, persisted in [mru-files]/[mru-pairs]), toolbar
(Segoe MDL2 glyph imagelist, util/GlyphIcons.\*),
status bar (page/zoom per pane + sync state), splitter, find bar, outline sidebar, fullscreen
(F11/Alt+Enter; hides the chrome without touching the persisted flags; SaveSession must use the
pre-fullscreen placement), session persistence (`%APPDATA%\PdfSideViewer\settings.ini`, INI over
WritePrivateProfile\* with a UTF-16 BOM and a named mutex) and routes pane `ViewEvent`s to
SyncController, the outline, the status bar and the menu/toolbar checked state
(`UpdateCommandUi`).

## Invariants that came from real bugs (see also docs/DESIGN.md)

- Any `PeekMessage` drain loop must re-post a swallowed `WM_QUIT` (it bypasses ALL filters).
- Publish `Document::SetWantedRange` BEFORE issuing render requests; every request/latch
  (`pendingId`, `failedScale`) must be released by a posted result or reset on device loss,
  or pages go permanently blank.
- Results are gated by generation/searchId (`m_openGen`, `m_searchSeq`), not by path or needle
  echo: re-opening the same file must not resurrect stale results.
- `ID2D1RenderTarget::GetSize()` returns DIPs even in `D2D1_UNIT_MODE_PIXELS`; use the client
  rect or `GetPixelSize()`.
- Device-loss recovery uses `DxResources::Generation()` so one pane never discards the device
  the sibling just rebuilt; recovery retries are backoff-limited.
- fz_try is setjmp-based: `fz_var` every local mutated inside and read after; keep destructible
  C++ objects out of fz frames; drop fz objects in `fz_always`.
- No `TBSTYLE_FLAT` on the toolbar: flat = transparent background delegated to a parent that
  paints nothing under children, i.e. a black band. The v6 theme already looks modern.

## Conventions

- clang-format-ish 100 columns, 4 spaces; comments explain constraints, not what the next line
  does. The maintainer communicates in Italian.
- Every user-visible string goes through `util/Strings.h` (X-list with English and Italian
  tables; English is the default and what E2E tests assert against). Engine-level error strings
  (engine/Document.cpp) stay English: workers cache them in result structs.
- `enum CommandId` in MainWindow.h is the single registry of WM_COMMAND/accelerator ids
  (menu, toolbar and accelerators all reuse the same ids; 1017..1019 and 1023..1024 must stay
  contiguous for CheckMenuRadioItem; 1030+/1040+ are the MRU ranges, kMruMaxEntries slots each,
  dispatched as ranges in WM_COMMAND).
- Session settings are versionless: add keys with safe defaults, never repurpose existing ones.
