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

MuPDF link facts: only `libmupdf.lib + libthirdparty.lib + libresources.lib` (the other libs the
mupdf solution produces are unreferenced); `libresources` exists only in Release and is shared by
Debug via a fallback library path; the mupdf projects declare v142 and are built with
`/p:PlatformToolset=v143`.

## Testing

There is no unit-test suite. Verification is end-to-end: launch the exe with `testdata\` PDFs and
drive it by posting Win32 messages (WM_MOUSEWHEEL with MK_CONTROL for zoom, WM_COMMAND with the
IDC_* ids, WM_VSCROLL, TVM_* to the outline tree), then assert via GetScrollInfo, window titles,
counter text, clipboard, and PrintWindow captures. Test scripts MUST call
`SetThreadDpiAwarenessContext(-4)` first: this dev machine's monitor is 175% (168 DPI) and
PowerShell is DPI-unaware, so un-aware coordinates/captures are virtualized at 96 DPI (a past
source of phantom "bugs"). Run one instance at a time; the exe must always exit with code 0 after
CloseMainWindow. Restore the user's clipboard if a test touches it.

## Architecture

Threading contract (the load-bearing rule): **the UI thread never calls MuPDF; Direct2D is only
used on the UI thread.** Each pane owns a `Document` (engine/Document.\*) with one worker thread
holding a cloned `fz_context` (base context + shared locks in engine/MupdfLib.\*). The worker
owns the `fz_document` exclusively, interprets pages into cached display lists, and serves a
job queue (Open / Render / TextPage / Links / Search) with urgent jobs pushed to the front.
Results are heap-allocated structs posted to the pane HWND via `WM_PSV_*` messages (receiver
takes ownership). Selection/link hit-testing and search highlighting run UI-side on plain-C++
models extracted by the worker, never on fz objects.

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

MainWindow owns the frame, splitter, find bar, outline sidebar, session persistence
(`%APPDATA%\PdfSideViewer\settings.ini`, INI over WritePrivateProfile\* with a UTF-16 BOM and a
named mutex) and routes pane `ViewEvent`s to SyncController and the outline.

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

## Conventions

- clang-format-ish 100 columns, 4 spaces; comments explain constraints, not what the next line
  does. UI strings are English; the maintainer communicates in Italian.
- `enum CommandId` in MainWindow.h is the single registry of WM_COMMAND/accelerator ids.
- Session settings are versionless: add keys with safe defaults, never repurpose existing ones.
