# PdfSideViewer — Design Document

*Status: approved design, pre-implementation. Last updated: 2026-07-09.*

A fast, fully native Windows 10/11 desktop application whose sole purpose is to display **two PDF
documents side by side** in one window, with optional synchronized scrolling and zooming.
Open source, released under the GPLv3 (see [Licensing](#3-licensing)).

No .NET, no WPF/WinForms, no cross-platform toolkits. Pure Win32 + Direct2D, C++20, MSVC.

---

## 1. Motivation and niche

Research (July 2026) confirmed that **no existing tool does this natively on Windows**:

- **SumatraPDF** offers only tabs and separate top-level windows. In-window split view has been
  requested since ~2011 (issues #413, #1575, #2509, discussion #3281) and consistently declined by
  the maintainer; there is no synchronized scrolling across windows either.
- **PDF-XChange Editor** has no native synchronized-scrolling toggle: the corresponding forum
  request has been open since ~2017, and staff point users to the Compare Documents workflow or
  community JavaScript hacks.
- **Adobe Acrobat** has no scroll lock for two independent documents (long-standing UserVoice
  request); the popular try67 script is button-driven, not real-time.
- **PDF Architect** is the closest prior art: it does offer side-by-side synchronized scrolling,
  but (first-hand experience, 2026) it works poorly in exactly the two ways this design treats as
  hard requirements: the pairing is hardwired to fixed couples (1-1, 2-2, ...) with no way to
  re-synchronize at an offset, and even the fixed pairing drifts out of sync as soon as the two
  documents have different page formats, because it tracks raw scroll position instead of
  page-relative position.
- Diff-oriented tools (diff-pdf, ConfrontaPDF) render *differences*, are Qt/wxWidgets based, and do
  not serve the "read two documents in parallel" use case.

A native two-pane viewer with real-time, offset-aware synchronized scrolling therefore exceeds the
current state of the art, not just matches it.

### Goals

- Instant startup, minimal memory footprint, crisp rendering at any DPI and zoom.
- Two independent panes, each a full PDF viewer: continuous vertical layout, smooth scroll,
  zoom-to-cursor, text search with highlights, text selection, clickable links.
- Synchronized scrolling with a user-controlled offset (the key differentiator), plus optional
  synchronized zoom.
- Open two files from the command line, drag & drop, or per-pane open buttons; restore last session.

### Non-goals (v1)

- Editing, annotations, form *filling*, JavaScript, printing, tabs, plugins.
- Formats other than what MuPDF opens transparently (XPS/EPUB open for free through the same API;
  they are tolerated, not targeted).

---

## 2. Technology choices

| Area | Choice | Rejected alternatives |
|---|---|---|
| PDF engine | **MuPDF 1.28.x**, statically linked | PDFium (equal speed, slightly lower fidelity; permissive license not needed here); Windows.Data.Pdf (raster-only: no text layer, search, selection, links, outline) |
| UI / rendering | **Pure Win32 + Direct2D** on a DXGI flip-model swapchain | WinUI 3 (no PDF control, heavy runtime, startup/memory penalties Microsoft is still remediating; buys nothing for an app that is essentially two custom canvases) |
| Language / toolchain | C++20, MSVC v143, Visual Studio 2022, x64 (ARM64 later) | — |
| App license | GPLv3 | MIT/BSD impossible while linking AGPL MuPDF |

### Why MuPDF

- Top-tier rendering fidelity: the Alfresco/Hyland engine comparison ranked MuPDF the clear winner
  with PDFium second; rasterization speed is neck and neck (DocumentCloud benchmark).
- Compact C API designed exactly for viewers: document/page loading, display lists, structured
  text, search returning highlight quads, links, outline, selection-to-clipboard.
- Actively maintained: 1.28.0 released 2026-06-26, three releases in the preceding eight months.
- Proven embed-on-Win32 path: SumatraPDF vendors and statically links it.

### Why pure Win32 + Direct2D

- SumatraPDF demonstrates that raw Win32 is what makes a PDF viewer feel instant.
- The whole UI is two custom-drawn canvases, a splitter, and a thin toolbar; a XAML framework adds
  cost without value here.
- Direct2D (rather than SumatraPDF's GDI/GDI+) lets us re-present a cached frame under a new
  transform in under a millisecond, which is the foundation for smooth animated pan/zoom and for
  DirectManipulation integration later.

### Rendering interop (verified against MuPDF headers and Direct2D docs)

- Render into `fz_pixmap` with colorspace `fz_device_bgr(ctx)` and **`alpha = 1`**: layout is
  B,G,R,A per pixel, which maps byte-for-byte to `DXGI_FORMAT_B8G8R8A8_UNORM`.
  (BGR with `alpha = 0` yields a packed 24-bpp buffer that no D2D format accepts.)
- Clear the pixmap to opaque white first (`fz_clear_pixmap_with_value(ctx, pix, 0xFF)`); then
  either `D2D1_ALPHA_MODE_IGNORE` or `D2D1_ALPHA_MODE_PREMULTIPLIED` is correct (draw-device output
  is premultiplied).
- Upload with `ID2D1Bitmap::CopyFromMemory(nullptr, pix->samples, (UINT32)pix->stride)`; never
  assume `stride == w * 4`.
- Present via `ID2D1DeviceContext` bound to a per-pane **DXGI flip-model swapchain**
  (`DXGI_SWAP_EFFECT_FLIP_DISCARD`, BufferCount 2), per Microsoft's "use flip model" guidance.
  DirectComposition (`CreateSwapChainForComposition`) is a later refinement for artifact-free live
  resize.

---

## 3. Licensing

- MuPDF is **AGPLv3** (dual-licensed commercially by Artifex; not needed for an open-source app).
- The application code is **GPLv3**. Per GPLv3 §13 / AGPLv3 §13, GPLv3 and AGPLv3 modules may be
  combined and conveyed together, each keeping its license; the combined distribution must ship
  full corresponding source. This is the same "(A)GPLv3" arrangement SumatraPDF uses.
- Consequence: every binary release links to the exact source tag, including the vendored MuPDF
  tree with any local patches.

---

## 4. Architecture

### 4.1 Threading model (follows MuPDF's documented rules)

MuPDF's rules: one `fz_context` per thread (clones share store/glyph cache); a `fz_document` may be
used by only one thread at a time; a completed `fz_display_list` is immutable and may be rendered
from several threads simultaneously; multi-threaded use requires a `fz_locks_context` supplied at
base-context creation.

```
UI thread                     Pane worker (x2, one per document)         Render pool (shared, 2..4)
─────────                     ──────────────────────────────────         ──────────────────────────
Win32 message loop            owns fz_document exclusively               fz_clone_context each
never calls MuPDF             load page → fz_display_list                fz_run_display_list
draws cached ID2D1Bitmaps     stext page, links, outline, search         → fz_pixmap (BGRA)
posts RenderRequests          (all document-touching calls)              fz_cookie abort checks
```

- One process-wide base `fz_context` created with a `fz_locks_context` over `FZ_LOCK_MAX` (3)
  SRWLOCKs and `FZ_STORE_DEFAULT` (256 MB shared resource store);
  `fz_register_document_handlers` once at startup.
- **Per pane**: one worker thread owns the `fz_document` and everything derived from it (page
  loading, display-list recording, `fz_stext_page` extraction, link/outline loading, search).
  The two panes never contend except on the shared store locks.
- **Render pool**: rasterizes display lists into pixmaps on cloned contexts. Every render carries a
  `fz_cookie`; scrolling or zooming past a pending request sets `cookie->abort`.
- Results are handed to the UI thread via `PostMessage` + a lock-free handoff slot; the UI thread
  uploads to `ID2D1Bitmap` and invalidates the pane.
- Display lists double as the source for text services
  (`fz_new_stext_page_from_display_list`), so workers rarely need to re-touch the document.

Start simple: the pane worker *is* the render worker (one thread per pane). The shared pool is an
optimization step once tiles land (M2), not a prerequisite.

### 4.2 Components

```
MainWindow (frame, menu bar, toolbar, status bar, accelerators, find bar, fullscreen)
 ├── Splitter (draggable vertical divider, double-click = 50/50)
 ├── PaneWindow (left)  ──┐   child HWND: input, scrollbars, D2D target
 ├── PaneWindow (right) ──┤
 │     each owns:         │
 │     Document          engine wrapper: open/auth, page count/sizes, display lists, stext
 │     PageLayout        prefix-sum page offsets in virtual space, per zoom/rotation
 │     ViewState         scroll offset, zoom (incl. fit-width/fit-page), rotation
 ├── RenderCache (shared) bitmap cache + request queue, serves both panes
 ├── SyncController      mediates the two ViewStates
 └── SearchController    per-pane search state, background whole-doc scan
```

**UI chrome** (all programmatic, no dialog/menu resources): the menu bar is built with
`CreateMenu`/`AppendMenuW` and attached at `CreateWindowExW` time; the toolbar is a comctl32
`ToolbarWindow32` whose icons are Segoe MDL2 Assets glyphs rendered into a 32bpp imagelist
(`util/GlyphIcons.*`, rebuilt on `WM_DPICHANGED`); the status bar shows page/total and zoom per
pane plus the sync state, updated from the central `ViewEvent` handler with a per-part text cache.
`MainWindow::Layout()` reserves the toolbar band on top and the status band at the bottom, then
partitions the remaining strip among sidebar/panes/splitter. Every user-visible string goes
through `util/Strings.*` (an X-list keyed by `StrId` with English and Italian tables; English is
the default, the choice persists in settings and switches live by rebuilding the menu). Full
screen (F11 / Alt+Enter, Esc exits) strips `WS_OVERLAPPEDWINDOW`, detaches the menu and hides the
bars without touching their persisted visibility flags.

**MRU**: File carries two submenus, Recent Files (single documents, opened into the focused
pane) and Recent Pairs (left+right sessions, order preserved). Both cap at 9 entries with 1..9
digit mnemonics and persist in `[mru-files]` / `[mru-pairs]`. Recording happens at ONE point,
the central `DocumentOpened` handler, so every open path (dialog, MRU itself, drag & drop,
command line, session restore) feeds the lists; paths are absolutized at record time
(command lines may be relative) and deduplicated case-insensitively. A pair is recorded whenever
an open completes while the sibling pane also holds a document. Clicking an entry whose file no
longer exists removes it and reports the missing path.

**PageLayout** (continuous vertical mode): virtual canvas where page *i* sits at
`y = Σ (pageHeight[j]·zoom) + gaps`, width = max page width·zoom + padding. A prefix-sum array of
page bottoms lets a binary search find the first visible page in O(log n); hit testing subtracts
the page's on-screen origin and applies the engine's inverse page transform (handles rotation and
PDF's bottom-up coordinates). Relayout re-runs when scrollbar appearance shrinks the viewport
(loop until stable, a real subtlety copied from SumatraPDF). Fit-width/fit-page are *virtual*
zooms recomputed on every relayout.

**RenderCache** (design lifted from SumatraPDF's `RenderCache`, verified against master sources):

- Entries keyed by `(document, pageNo, rotation, zoom, tile{res,row,col})`, byte-budgeted
  (default: min(256 MB, ¼ of physical RAM)).
- Request queue: LIFO with deduplication (an identical queued request is promoted to the top);
  visible pages requested after prefetch pages so they end up highest priority; each request
  abortable.
- Prefetch: `lastVisible+1` and `firstVisible-1` after visible pages are satisfied.
- Eviction: never a visible entry; first invisible entries of the same document, then oldest
  entries of the other document.
- **Stale-bitmap substitution**: on zoom/rotation change, entries are flagged `outOfDate` but kept;
  paint stretches the old bitmap while the new render is in flight, so zooming never flashes white.
- **Tiling**: below a threshold, whole-page bitmaps (an A4 page at 100%/96 DPI ≈ 3.3 MB BGRA; at
  400% ≈ 53 MB, hence tiles). Above it, split the page into a `2^res × 2^res` grid;
  `res = ceil(log2(geometric_mean(pageSizePx / maxTileSize)))` with `maxTileSize` = primary screen
  size (SumatraPDF's heuristic). Paint walks tiles breadth-first, drawing the lower-res ancestor
  stretched as placeholder. MuPDF renders a sub-rectangle of a display list directly: create the
  pixmap with the tile's bbox (`fz_new_pixmap_with_bbox`) and pass the tile as `scissor` to
  `fz_run_display_list`, so tiles cost no re-parsing.

**SyncController** — the differentiator, so specified precisely. Two hard requirements derived
from where the closest prior art (PDF Architect) fails:

- **R1 — adjustable pairing**: the page pairing must be user-adjustable at any time; never
  hardwired to 1-1, 2-2, ... The user must be able to re-synchronize at an arbitrary offset
  without reopening anything.
- **R2 — format-independent sync**: sync must survive documents with different page formats,
  mixed page sizes within a document, and different zoom levels in the two panes.

Design:

- **Position model (satisfies R2)**: a pane's position is expressed in *page units*,
  `pos = pageIndex + fractionWithinPage`, sampled at the **viewport center**. Every page counts as
  exactly 1.0 regardless of its physical size, so an A4 document stays page-aligned with a Letter
  or A5 one, and panes at different zoom levels stay aligned too. When page heights differ, the
  two panes scroll at different pixel speeds by construction; that is what *keeps* them in sync.
  Syncing raw pixel/scrollbar offsets (PDF Architect's apparent approach, and the source of its
  drift) is explicitly rejected.
- **Default mode, delta-preserving sync (satisfies R1)**: when the user enables sync, capture
  `anchor = posB − posA` in page units. Every scroll of one pane drives the other to preserve the
  anchor. Users pre-scroll each pane to the sections they want aligned, then lock; unlocking,
  adjusting one pane, and re-locking recaptures the anchor at any moment. This is exactly what
  Acrobat/PDF-XChange users have been asking for and don't have.
- **Relative-fraction mode** (optional): `fraction = scrollY / (canvasHeight − viewportHeight)`
  mirrored to the other pane; useful for grossly different documents, known to drift locally.
- **Temporary unlock**: holding a modifier (Alt; Shift is taken by horizontal wheel scroll)
  scrolls only the focused pane while the anchor tracks each adjustment, so the alignment crafted
  under Alt is exactly what subsequent synced scrolls preserve. Re-enabling sync always
  recaptures.
- **Reentrancy guard**: an `isSyncing` flag suppresses feedback loops when programmatically
  scrolling the sibling.
- **Zoom sync**: independent toggle; applies the same zoom command (ratio-preserving) to both.
- Future (v1.x): WinMerge-style manual sync points ("align this page with that page") as a
  piecewise-linear position map; the anchor model is the degenerate single-segment case, so the
  data model already fits.

**Search** (per pane, find bar targets the focused pane):

- `fz_search_stext_page(ctx, stextPage, needle, hit_mark, quads, hit_max)` returns highlight
  **quads**, with `hit_mark` grouping the quads of one logical match (line-wrapped matches yield
  several quads). Match model: `struct Match { int page; std::vector<fz_quad> quads; }`.
- Quads live in page space and are transformed page→screen at paint time, so highlights survive
  scroll/zoom without re-searching. Active match in a distinct color, alpha-blended overlay
  (`FillGeometry` at fractional alpha over the page bitmap).
- Whole-document "m of n" count runs on the pane worker page-by-page, cancellable, posting
  incremental results; typing in the find box stays responsive.
- Normalization pass copied from SumatraPDF's `TextSearch`: Unicode case folding, whitespace
  collapse, hyphen and smart-quote variants.
- The 1.27+ `fz_match_*` / `fz_search_options` API (regex, cross-page matches) is still labeled
  experimental upstream; v1 uses the stable `fz_search_*` family and pins the MuPDF version.

**Selection & links**: mouse selection via `fz_snap_selection` (char/word/line on click count) +
`fz_highlight_selection` for quads; Ctrl+C via `fz_copy_selection(ctx, stext, a, b, crlf=1)` →
UTF-8→UTF-16 → `CF_UNICODETEXT`. Links via `fz_load_links` per page: hand cursor on hover;
internal destinations resolved with `fz_resolve_link_dest`, external URLs to `ShellExecuteW`.
Outline sidebar (`fz_load_outline`) is an M5 item.

### 4.3 Input handling

- **Wheel**: accumulate `WM_MOUSEWHEEL` deltas with remainder (never assume multiples of 120),
  consume in units of `WHEEL_DELTA / SPI_GETWHEELSCROLLLINES`, scroll in *pixels* so
  precision-touchpad two-finger scroll is naturally smooth. Handle `WM_MOUSEHWHEEL`.
- **Ctrl+wheel zoom-to-cursor**: with cursor at viewport point `c`, scroll offset `s` (device px),
  zoom `z → z'`: `s' = (s + c)·(z'/z) − c`. Continuous factor from accumulated delta.
- **Keyboard**: PgUp/PgDn/Home/End/arrows per pane; Tab switches focused pane; F3/Shift+F3 next/
  previous match; Ctrl+F find; Ctrl+O / Ctrl+Shift+O open left/right; F7 toggle scroll sync;
  Ctrl+F7 zoom sync.
- **Touch**: `WM_GESTURE` (`GID_ZOOM` centered at gesture location, `GID_PAN` with inertia) covers
  touchscreens in v1. Precision-touchpad pinch requires **DirectManipulation** (touchpads emit
  neither WM_POINTER nor WM_GESTURE); planned v1.x, one viewport per pane HWND.
- **Smooth scrolling**: exponential easing toward a target offset, driven by a compositor-friendly
  tick (`DCompositionWaitForCompositorClock` / 60+ Hz timer fallback).

### 4.4 DPI, theming, shell integration

- Per-Monitor V2 declared in the manifest (`<dpiAwareness>PerMonitorV2</dpiAwareness>`); handle
  `WM_DPICHANGED` by relayouting and re-rendering at `renderScale = zoom · monitorDPI / 96` so text
  is always rasterized for the actual device pixels, never bitmap-scaled.
- Dark title bar via `DwmSetWindowAttribute(DWMWA_USE_IMMERSIVE_DARK_MODE)`; page background aside,
  chrome colors follow the system light/dark setting. MuPDF-rendered dark mode (color inversion)
  is out of scope for v1.
- Drag & drop: one file dropped on a pane opens there; two files fill both panes.
- CLI: `PdfSideViewer.exe [left.pdf [right.pdf]]`.
- Settings + last session (files, scroll positions, zoom, sync state, window placement) in
  `%APPDATA%\PdfSideViewer\settings.ini` (UTF-16 with BOM so `WritePrivateProfile*` round-trips
  Unicode paths; INI over JSON: native Win32 read/write, nothing to parse; writes serialized
  across instances by a named mutex).
- Password-protected files: `fz_needs_password` / `fz_authenticate_password` with a retry prompt.

---

## 5. Repository layout and build

```
PdfSideViewer/
├── LICENSE                    GPLv3
├── README.md
├── docs/DESIGN.md             this document
├── PdfSideViewer.sln
├── app/
│   ├── PdfSideViewer.vcxproj
│   ├── app.manifest           PMv2 DPI, comctl32 v6
│   ├── res/                   app.ico (generated by scripts/make-icon.ps1), resource.h
│   │                          (IDI_APP + version defines), PdfSideViewer.rc (ICON+VERSIONINFO)
│   └── src/
│       ├── main.cpp           wWinMain, message loop, accelerators
│       ├── MainWindow.*       frame, menu/toolbar/status bar, splitter, find bar, fullscreen
│       ├── PaneWindow.*       canvas child HWND, input, painting
│       ├── DxResources.*      D3D11/D2D device, per-pane swapchain
│       ├── engine/
│       │   ├── MupdfLib.*     base fz_context, locks, handlers (singleton)
│       │   ├── Document.*     fz_document wrapper + pane worker thread
│       │   └── RenderTypes.h  RenderRequest, TileKey, RenderResult
│       ├── render/RenderCache.*
│       ├── view/PageLayout.*  view/ViewState.*  view/SyncController.*
│       ├── search/TextSearch.*
│       └── util/              Settings (INI), Strings (i18n en/it), GlyphIcons (MDL2)
└── vendor/
    └── mupdf/                 official 1.28.x source release (thirdparty included)
```

**Building MuPDF** (one-time per machine/config; facts verified on this repo's 1.28.0 tree):

1. Vendor the official **source release archive** (not a bare git clone: the archive ships the
   `thirdparty/` submodules pre-populated).
2. Build `platform/win32/mupdf.sln`, targets `libmupdf;libthirdparty;libresources`, for **both**
   x64 Release and x64 Debug (the app's Debug config must link a `/MDd` MuPDF; MuPDF's projects
   use `/MDd` Debug and `/MD` Release, matching the app's defaults).
   The projects declare `PlatformToolset v142`; pass `/p:PlatformToolset=v143`. Works as-is.
3. **`libresources` has no Debug configuration** (it is pure data: fonts/resources); the app's
   linker uses the Release directory as a fallback library path for it.
4. The app links exactly **`libmupdf.lib` + `libthirdparty.lib` + `libresources.lib`** and
   compiles against `vendor/mupdf/include` (`#include <mupdf/fitz.h>` inside `extern "C"`).
   The other libs the solution produces (tesseract, leptonica, zxing, harfbuzz, extract,
   mubarcode, pkcs7) are **not referenced** by the plain Release/Debug configuration and must not
   be linked.
5. No C-API DLL target exists in the solution; static linking is the path of least resistance and
   fine under GPLv3.
6. vcpkg's `libmupdf` port exists but trails upstream (1.26.10 vs 1.28.x) and has linking-issue
   history; not used.

**Hard-won Win32 details encoded in the code** (do not regress):

- `PeekMessage` returns `WM_QUIT` regardless of any hwnd/range filter once the quit flag is set
  and the queue is empty: any drain loop run during teardown must re-post a swallowed `WM_QUIT`
  (see `PaneWindow` `WM_DESTROY`) or the message loop blocks forever.
- The wanted-page-range must be published to the render worker **before** issuing render
  requests, and the (first,last) pair must be updated under the queue mutex, or fresh jobs get
  judged against a stale/torn range and silently dropped.
- `PageLayout` quantizes page rects with the same rounding as `fz_round_rect` on
  origin-normalized bounds, and `ContentOrigin` snaps to whole pixels, so the up-to-date page
  blit is exactly 1:1 (no resampling blur). The render worker normalizes the display-list bounds
  origin for the same reason. The tile grid must be derived from `PageLayout::PageSizePx` (exact
  quantized sizes), never from `PageRect` edge differences: float tops/bottoms lose ±1 px past
  2^24 content px and the grid would desynchronize from the worker's, producing seams.
- `ID2D1RenderTarget::GetSize()` returns DIPs even when the context runs in
  `D2D1_UNIT_MODE_PIXELS`; drawing chrome from it shrinks everything by the DPI factor on scaled
  monitors. Use the client rect (`ViewportPx`) or `GetPixelSize()`.
- Render results that can fail must ALWAYS reach the pane (ok flag), and every latch the pane
  keeps (`pendingId`, `failedScale`) must be reset on device loss; any silent drop or surviving
  latch turns into a permanently blank page at that zoom.
- The `.rc` must never contain an `RT_MANIFEST` entry: the manifest is already embedded via the
  vcxproj `<Manifest>` item and a second copy is a CVT1100 duplicate-resource link error.
- `SaveSession` must persist the pre-fullscreen `WINDOWPLACEMENT` while full screen: entering
  full screen rewrites the live placement with the monitor rect, so saving the live one would
  make the window come back monitor-sized and borderless-shaped after a fullscreen exit+restart.
- Toolbars must not use `TBSTYLE_FLAT` here: flat toolbars are transparent and delegate their
  background to the parent, which paints nothing under children (`WS_CLIPCHILDREN` +
  `WM_ERASEBKGND` returning 1), leaving a black band. The comctl v6 theme renders the non-flat
  style modern anyway.
- Toolbar glyphs are drawn white-on-black with `ANTIALIASED_QUALITY` (never ClearType: subpixel
  RGB fringes corrupt the coverage) and converted to premultiplied ARGB; `GdiFlush()` before
  touching DIB bits.

App project: `/std:c++20 /W4 /permissive- /utf-8`, x64 (ARM64 later; both mupdf.sln and the
installed toolchain support it). Distribution: portable zip first; installer later.

## 6. Milestones

| # | Deliverable | Contents |
|---|---|---|
| M0 | Skeleton | frame + splitter + two pane HWNDs, D2D flip-model swapchains, PMv2 DPI, dark title bar |
| M1 | Single-pane viewer | MuPDF init, open document, continuous layout, whole-page renders on pane worker, scroll + zoom-to-cursor, stale-bitmap substitution |
| M2 | Industrial rendering | RenderCache with budget/eviction/dedup/abort, tiling at high zoom, prefetch, both panes live |
| M3 | The differentiator | SyncController (delta anchor, relative mode, temporary unlock, zoom sync), CLI args, drag & drop, session persistence |
| M4 | Text services | search + highlight overlays + background count, selection + clipboard, links |
| M5 | Polish | outline sidebar, keyboard completeness, settings UI, portable zip release, ARM64 |

Each milestone leaves the tree buildable and the app usable.

## 7. Risks and mitigations

| Risk | Mitigation |
|---|---|
| MuPDF VS projects target v142 | retarget to v143 is routine and documented; pin the MuPDF tag |
| `fz_match_*` search API experimental | use stable `fz_search_*` in v1; revisit at the next MuPDF bump |
| Memory blow-up on huge documents / deep zoom | shared 256 MB `fz_store` budget + byte-budgeted RenderCache + tiling + display lists instead of pixmaps as the durable cache |
| Sync feedback loops / drift | reentrancy guard; anchor recapture on re-lock; relative mode as documented fallback |
| Touchpad pinch not delivered via WM_GESTURE | accepted v1 gap; DirectManipulation planned, design keeps one viewport per pane HWND |
| AGPL obligations | app is GPLv3; releases link exact source tags including vendored MuPDF patches |

## 8. Toolchain requirements

- Visual Studio 2022 with workload **Desktop development with C++**
  (`Microsoft.VisualStudio.Workload.NativeDesktop`): MSVC v143
  (`Microsoft.VisualStudio.Component.VC.Tools.x86.x64`), Windows 10/11 SDK.
- git. (CMake, NuGet, Windows App SDK are *not* required.)

Verified present on the development machine (2026-07-09): VS Community 2022 17.14.x with
NativeDesktop workload, MSVC v143 14.44, Windows 11 SDK 10.0.22621 + 10.0.26100, ARM64 tools,
git 2.54. Nothing additional to install.

## 9. Key references

- MuPDF C API and threading rules: https://mupdf.readthedocs.io/en/latest/reference/c/overview.html
  and `docs/examples/multi-threaded.c` in the MuPDF repo
- SumatraPDF architecture (reference design for RenderCache/DisplayModel/TextSearch, GPL-compatible):
  https://github.com/sumatrapdfreader/sumatrapdf — `src/RenderCache.cpp`, `src/DisplayModel.cpp`,
  `src/EngineMupdf.cpp`, `src/TextSearch.cpp`
- DXGI flip model: https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/for-best-performance--use-dxgi-flip-model
- Direct2D pixel formats: https://learn.microsoft.com/en-us/windows/win32/direct2d/supported-pixel-formats-and-alpha-modes
- Per-Monitor V2 DPI: https://learn.microsoft.com/en-us/windows/win32/hidpi/setting-the-default-dpi-awareness-for-a-process
- Wheel-delta handling: https://devblogs.microsoft.com/oldnewthing/20130123-00/?p=5473
