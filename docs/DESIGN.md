# PdfSideViewer — Design Document

*Status: approved design, pre-implementation. Last updated: 2026-07-26.*

A fast, fully native Windows 10/11 desktop application whose sole purpose is to display **two PDF
documents side by side** in one window (three in the optional three-pane mode), with optional
synchronized scrolling and zooming.
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
- Two independent panes (three in the optional three-pane mode), each a full PDF viewer:
  continuous vertical layout, smooth scroll, zoom-to-cursor, text search with highlights, text
  selection, clickable links.
- Synchronized scrolling with a user-controlled offset (the key differentiator), plus optional
  synchronized zoom.
- Open up to three files from the command line, drag & drop, or per-pane open buttons; restore
  last session.

### Non-goals (v1)

- Editing, annotations, form *filling*, JavaScript, printing, tabs, plugins.
- Formats other than what MuPDF opens transparently (XPS/EPUB open for free through the same API;
  they are tolerated, not targeted).

---

## 2. Technology choices

| Area | Choice | Rejected alternatives |
| --- | --- | --- |
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

```text
UI thread                     Pane worker (one per document)             Render pool (shared, 2..4)
─────────                     ──────────────────────────────             ──────────────────────────
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
  The panes never contend except on the shared store locks.
- **Render pool**: rasterizes display lists into pixmaps on cloned contexts. Every render carries a
  `fz_cookie`; scrolling or zooming past a pending request sets `cookie->abort`.
- Results are handed to the UI thread via `PostMessage` + a lock-free handoff slot; the UI thread
  uploads to `ID2D1Bitmap` and invalidates the pane.
- Display lists double as the source for text services
  (`fz_new_stext_page_from_display_list`), so workers rarely need to re-touch the document.

Start simple: the pane worker *is* the render worker (one thread per pane). The shared pool is an
optimization step once tiles land (M2), not a prerequisite.

### 4.2 Components

```text
MainWindow (frame, menu bar, toolbar, status bar, accelerators, find bar, fullscreen)
 ├── Splitters (one draggable vertical divider per pane boundary, double-click = equal shares)
 ├── PaneWindow (left)   ──┐   child HWND: input, scrollbars, D2D target
 ├── PaneWindow (centre) ──┤   optional, three-pane mode only (see pane slots below)
 ├── PaneWindow (right)  ──┤
 │     each owns:          │
 │     Document          engine wrapper: open/auth, page count/sizes, display lists, stext
 │     PageLayout        prefix-sum page offsets in virtual space, per zoom/rotation
 │     ViewState         scroll offset, zoom (incl. fit-width/fit-page), rotation
 ├── RenderCache (shared) bitmap cache + request queue, serves every pane
 ├── SyncController      mediates the active panes' ViewStates
 └── SearchController    per-pane search state, background whole-doc scan
```

**Pane slots** (`PaneSlots.h`): the frame owns a FIXED array of three pane slots in visual order
(`kSlotLeft`, `kSlotCenter`, `kSlotRight`), of which only a subset is ACTIVE: the two-pane
default is the active set {left, right} (the centre pane is never created), and the optional
three-pane mode (`[window] paneCount`, View ▸ Two/Three Panes, File ▸ Open Centre, a third command-line
file, a three-document Recent Sessions entry or a three-file drop) activates all three. Slot
indices never move when the count changes, which is what keeps everything keyed by slot
meaningful across a mode switch with no remapping or migration: the settings sections
(`[left]`/`[center]`/`[right]`, `kSlotKeys`), the sync-point tuples, the per-slot
session/fallback state, the pane child ids (`kPaneChildIdBase + slot`, so left 100, centre 101,
right 102 — the E2E scripts address panes via `GetDlgItem`) and the `-open-*` verb payloads (the
slot travels as the word `left`/`center`/`right` in the XML IPC payload, see 4.4). The ONE
exception is the command line's POSITIONAL order, which is Beyond Compare's `left right
[center]` and therefore needs `kCliSlotOrder` in main.cpp; nothing else may reuse that order.
Code walks the panes with "for each slot, if active" (visual order) and never assumes two.
`SetPaneCount` creates/destroys the centre pane, wiring it through the same `ConfigurePane` used
at startup, and its ORDER is load-bearing (a use-after-free was caught in review): clear the
sync map FIRST — emptying a non-empty map recaptures the plain anchors by walking the
controller's CURRENT pane set, so every pane in it must still be alive — then install the new
set in `SyncController`, and only THEN destroy the centre pane (`PaneWindow` tears its own HWND
down in its destructor and clears the handle on `WM_NCDESTROY`: a pane can die while the frame
lives on). Shrinking parks the centre session in its per-slot fallback and clears the centre's
last-opened path; growing REOPENS the park, and the `DocumentOpened` this fires also re-restores
the trio's remembered sync points through the path-changed flow. The park survives restarts
because the `[center]` section is always read, written while it holds a path, and deleted as a
section otherwise; File ▸ Close Session wipes the session fallbacks of ALL three slots
unconditionally (a parked or never-opened document must not resurrect) and returns the
arrangement to the configured default (`[defaults] paneCount` — deliberately distinct from
`[window] paneCount`, which is the arrangement the last session happened to have). The two-pane
and three-pane splits are stored SEPARATELY (`splitRatio` vs
`splitRatio3Left`/`splitRatio3Center`): one shared value would drag a 50/50 split into the
three-pane layout as 50/33/17 on the first switch.

**UI chrome** (all programmatic, no dialog/menu/rebar resources): everything above the panes
lives in ONE `ReBarWindow32` row with three locked bands (no grippers, fixed order): band 0 is
the MENU emulated as a text-only `TBSTYLE_FLAT|LIST|TRANSPARENT` toolbar (`view/MenuBand.*`,
the documented "IE-style menu band" pattern — a native `HMENU` bar is non-client and cannot be
a band child), band 1 the command toolbar (Segoe MDL2 glyph imagelist via `util/GlyphIcons.*`,
`RBBS_USECHEVRON` overflow popup reusing the tooltip strings), band 2 an editable page box
(Enter jumps, label-first like Ctrl+G). Bands carry `RBBIM_ID` and every access resolves the
index through `RB_IDTOINDEX`: with the rebar UNLOCKED (IE-style "Lock the Toolbars" in the
View menu and on the bar's right-click context menu) the user drags the grippers to reorder,
resize and wrap bands onto extra rows; order/width/breaks persist as `[window] rebarBands`
("id,cx,break;" per band in visual order) and the lock as `rebarLocked`. The bar context menu
also carries IE's three toolbar TEXT OPTIONS verbatim ("Show text labels" below the icons,
"Selective text on right", "No text labels"; persisted as `[window] toolbarText` 1/2/0):
below = a plain toolbar with per-button strings, selective = `TBSTYLE_LIST` +
`TBSTYLE_EX_MIXEDBUTTONS` with `BTNS_SHOWTEXT` only on the primary actions (open
left/centre/right, from bookmarks, swap) - exactly IE's mechanics. `TBSTYLE_LIST` cannot be flipped on a live
toolbar, so a mode (or language) change recreates the toolbar and re-childs the band in place
(`RebuildToolbarInBand`, by `RBBIM_ID`); the icon-only 24px button pin applies only in mode 0
(labels size their own buttons via `BTNS_AUTOSIZE` + `TB_SETMAXTEXTROWS`). Defaults: locked,
"Show text labels", and the toolbar band starts on its OWN row (`RBBS_BREAK` at the default
insert; the break survives lock toggles via SetRebarLocked's per-band snapshot and is
overridden by a saved `rebarBands` layout). "Reset the Toolbar Layout" (View menu and rebar
context menu, next to the lock) rebuilds that default through ApplyRebarLayout; invoked while
full screen it retargets the exit snapshot instead, since the forced full-screen row is not
the user's arrangement. Two toolbar icons are COMPOSED by `GlyphSpec::mirrorOverlay`, which
draws a glyph plus its horizontal mirror (GM_ADVANCED world transform; the cell rect maps onto
itself): the full-screen one, because MDL2 ships no single 4-corner-arrows glyph, and the
THREE-panes one, because it ships no three-column frame either - E90C is a frame with one
vertical divider, and its mirror adds the second, so the pane-count pair is one glyph told
apart by divider count (the pair is a manual `BTNS_CHECK` couple like the scroll modes, pressed
from `UpdateCommandUi`, mirroring the View menu's radio group). Full screen normally
hides all chrome, but two Options ([window] fsToolbar / fsStatusbar) keep the COMMAND toolbar
bands and/or the status bar on screen - never the menu band, which full screen always hides
(Alt/F10 still track the popups): the rebar therefore shows only while the kept toolbar band
is itself visible per View > Toolbar. The kept chrome uses a FORCED layout
(ApplyFullscreenBandLayout), overriding the user's lock setting and arrangement: one locked row
(no grippers, no dragging), toolbar stretched, page box right-aligned via RBBS_FIXEDSIZE at the
row end - without the override, an unlocked page box band kept its own row and stretched to the
full screen width. Because that layout is forced, the rebar CONTEXT MENU drops to the three text
options while full screen: the lock and the reset would only record intentions (SetRebarLocked
records the setting and refreshes the checkmark, ResetRebarLayout retargets the exit snapshot,
and the exit path applies both), and the toolbar toggle would be a ONE-WAY door - hiding the bar
takes the context menu with it, the menu band is gone, IDC_TOGGLE_TOOLBAR has no accelerator,
and `[window] toolbar` is persisted, so the bar would still be missing at the next launch.
Whether the toolbar shows in full screen at all is `[window] fsToolbar`, an Options checkbox.
Both commands stay reachable through the Alt menu, which still tracks. Hiding a band makes
comctl32 MERGE its row into the next
one - the following band's RBBS_BREAK is cleared for good, re-showing does not restore it - so
ToggleFullScreen snapshots the band layout at entry, restores it at exit (SetRebarLocked for
the real gripper styles FIRST, then ApplyRebarLayout: the snapshot's cx values were captured in
the user's own gripper state), and SerializeRebarLayout returns that snapshot while full screen
(a session saved there must not persist the forced row, mirroring the pre-full-screen placement
rule). A hidden band's child is also not laid out by the rebar, so UpdateRebarBandSizes skips
the menu-band width measure while it is hidden; the exit path re-measures. When the real full-screen button is not visible, a floating
one-button mini toolbar (child of the frame, top-right, NOT flat: the frame clips children
and paints nothing beneath them) provides the exit. The menu band clips
into its own chevron popup listing the hidden top-level menus. Ownership split: MainWindow owns the `HMENU` (built by
`BuildMenuBar`, NEVER attached to the window) and every `WM_COMMAND`; MenuBand owns
presentation and the modal tracking loop (`TrackPopupMenuEx` WITHOUT `TPM_RETURNCMD`, so the
popups post ordinary `WM_COMMAND`s and the dispatch is untouched). The status bar mirrors the
pane geometry: with two panes, SEVEN parts — left-pane page/zoom on the left half, right-pane
page/zoom anchored to the right half, the sync summary centered astride the midline, two
borderless fillers absorbing the slack; with three panes, page/zoom cells under each pane,
anchored at the splitter positions, and the sync summary after the right pane's cells (the
midline it used to straddle is covered by the centre pane). Page parts show the PDF
/PageLabels label when it differs from
the ordinal ("ix (9/314)"), via the single `PaneWindow::FormatPageText` formatter shared with
the scrollbar-drag tooltip and the go-to flows. `MainWindow::Layout()` reserves the rebar
height (`RB_GETBARHEIGHT`) on top and the status band at the bottom, then partitions the
remaining strip among the width-adjustable outline sidebar (persisted DIP width, own drag
divider; double-click best-fits the width to the widest expanded item so the tree loses its
horizontal scrollbar), panes and splitters. Modal dialogs (Go to Page, Options) are
built as in-memory `DLGTEMPLATE`s (`util/DialogTemplate.*`) for `DialogBoxIndirectParamW` — no
.rc resources, free modality/Tab/Esc. Every user-visible string goes through `util/Strings.*`
(an X-list keyed by `StrId` with English (en-GB), Italian, German, French, Hungarian,
Ukrainian, Romanian, Portuguese (pt-PT), Greek, Spanish (es-ES), Polish, Dutch, Czech and
Swedish tables;
English is the default, the choice persists in settings as a two-letter code and a live
switch rebuilds the `HMENU` and retitles the band).
Full screen (F11 / Alt+Enter, Esc exits) strips `WS_OVERLAPPEDWINDOW` and hides the WHOLE
rebar plus the status bar without touching their persisted visibility flags ("View > Toolbar"
only hides bands 1-2; outside full screen the menu band always stays, while full screen hides
it even when `[window] fsToolbar` keeps the toolbar bands on screen). Explorer integration (optional, Options or
`-register-shell`): three static verbs under `HKCU\...\SystemFileAssociations\.pdf\shell`
(`MUIVerb`, `MultiSelectModel=Single`, per-verb `Icon` naming the exe's own resources by
NEGATIVE resource id — the accent-filled page marks the pane the verb targets, three pages for
the centre one) — a location that by documented design never participates in default-handler
resolution. Explorer lists static verbs ALPHABETICALLY by key name (creation order is not
honoured), so the keys are numbered — `PsvOpen1Left`/`PsvOpen2Center`/`PsvOpen3Right` — to
force the visual left/centre/right order; the un-numbered names older builds wrote are never
written any more but still recognized (a rewrite drops this exe's, every removal path covers
them). `-open-left/-open-right/-open-center` reuse a
running instance through the same `WM_COPYDATA` channel as forward search (see the XML IPC
protocol under SyncTeX below; the slot travels as the word `left`/`center`/`right`, and a verb
aimed at an inactive slot switches three-pane mode on, so it always lands).

**MRU**: File carries two submenus, Recent Files (single documents, opened into the focused
pane) and Recent Sessions (the documents that were open together — two or three, keyed by the
per-slot paths, order preserved; the centre key is optional, so an entry written before the
three-pane mode still loads, and a three-document entry brings its layout with it: opening it
switches three-pane mode on, because the pane count is part of what was open together). Both cap
at 9 entries with 1..9 digit mnemonics and persist in `[mru-files]` / `[mru-pairs]` (the section
keeps its two-pane-era name). Recording happens at ONE point, the central `DocumentOpened`
handler, so every open path (dialog, MRU itself, drag & drop, command line, session restore)
feeds the lists; paths are absolutized at record time (command lines may be relative) and
deduplicated case-insensitively. A session is recorded whenever an open completes while EVERY
active pane holds a document. Clicking an entry whose file no longer exists removes it and
reports the missing path.

**SyncTeX**: `engine/SyncTex.*` wraps the reference parser vendored under `thirdparty/synctex`
(TRACKED in git, unlike `vendor/`; MIT-style license, see its README for the pinned tag). The
wrapper is plain C file parsing — no `fz_context`, no Direct2D — so it is deliberately
UI-thread-callable; queries are gesture-rare and the scanner is loaded lazily on the first
query, cached, and reset in `OpenDocument`'s clear list (which auto-reload funnels through, so
a rebuilt `.synctex.gz` re-parses on the next query). Every coordinate at the boundary is PDF
points, top-left origin — SyncTeX's own convention, so boxes drop into `Document::RectPt`
directly (`visible_v` is the BASELINE: top = v−height, bottom = v+depth). Inverse search is
Ctrl+click (`WM_LBUTTONUP` click branch, before link logic) → `PagePointAt` →
`synctex_edit_query` → a configurable launch template (`[synctex] inverse`, default the
`vscode://file/%f:%l` URI). Forward search arrives as `-forward-search TEX LINE PDF`: a second
short-lived instance finds the running window and hands the request over via `WM_COPYDATA`,
which MainWindow routes to the pane holding that PDF (focused pane wins a tie; the PDF is opened
on demand) and the pane centers and flashes the target boxes green for 1.5 s
(`kSyncFlashTimer`). Requests for documents still opening park in `m_parkedForward` and replay
on `DocumentOpened`.

Every second-instance handoff (the Explorer verbs and forward search alike) shares ONE
`WM_COPYDATA` protocol (`util/IpcXml.*`, `dwData` 'PSVX'): a small versioned UTF-16 XML
document, `<psv v="1"><open slot="center" path="…"/></psv>` or
`<psv v="1"><forward tex="…" line="123" pdf="…"/></psv>`, built and parsed with XmlLite (an OS
component since Vista: native, no COM initialization, DTD processing prohibited, plus a depth
cap — the payload arrives from arbitrary processes). The slot is a WORD from the closed
`left`/`center`/`right` vocabulary (= `kSlotKeys`), never an integer that a renumbered enum
could reinterpret; unknown versions, commands or malformed documents are simply UNHANDLED, so
the sender's timeout check fails and it falls back to a cold start — a mixed-version handoff
degrades to a new window. `IpcXml::Parse` owns the structural checks (well-formedness, length
caps, closed vocabulary, digits-only line numbers); the receiver
(`MainWindow::HandleCopyData`) keeps the semantic ones (rooted absolute paths, line range,
inactive-slot activation). Delivery is AT-LEAST-ONCE: a handoff that times out cold-starts a
new window even though the hung receiver may still serve the original request later — the
occasional duplicate is accepted over deduplication machinery. Default UIPI is the channel's
authorization boundary (no `ChangeWindowMessageFilterEx` exception): an ELEVATED viewer simply
never receives handoffs from medium-integrity senders, which then cold-start.

With **File ▸ New Window** several viewers can be up at once, and `FindWindowW(kClassName)`
answers with exactly one of them (the first in z-order). For the Explorer verbs that is the
right convention — the window the user was last in — but a forward search RETARGETS a pane, so
handing it to a window that is not showing that pdf replaces the document it IS showing, and
because the delivery raises that window the next build finds it first again: the mistake sticks
instead of self-correcting. So forward search runs a CLAIM round first. The sender walks the
frames in z-order offering `<forwardprobe .../>`, which a receiver takes only when one of its
panes already holds that pdf (`MainWindow::PaneShowingDocument`, the same predicate
`RouteForwardSearch` picks its target with) and otherwise declines BEFORE any foregrounding, so
a refusal is invisible; if nobody claims it, the ordinary `<forward/>` goes to the topmost frame
exactly as before. It is a new ELEMENT rather than an attribute on `<forward>` on purpose:
unknown attributes are the protocol's designated extension point and are IGNORED, so an older
receiver would mistake the probe for a real request and hijack it, while an unknown element is
rejected and the sender falls through. With a single window the behaviour is unchanged either
way. The probe uses a 1 s timeout rather than the send's 5 s: it is a query, and N windows must
not multiply into N times the editor's patience.

**Auto-reload**: each pane owns a `util/FileWatcher.*` — a thread that watches the open
document's PARENT DIRECTORY (`ReadDirectoryChangesW`; a handle to the file itself would go stale
across the delete/recreate/rename cycles LaTeX toolchains perform) and posts
`WM_PSV_FILE_CHANGED` (outside the `WM_PSV_FIRST..LAST` drain range: no heap payload; wParam
carries the watch GENERATION, bumped per `Watch`, so a post from the previous watch of the same
HWND is ignored instead of reloading the new document) when the
watched name is touched. The pane debounces (500 ms after the last notification of a write
burst), then probes stability with a deny-write `CreateFileW`: while the producer still holds
the file (or a clean+rebuild window leaves no file at all) it retries on the same cadence
instead of flashing an error state. The reload itself goes through the session-restore path
(`OpenDocumentWithView`), so position, zoom and fit mode survive and sync re-anchors on
`DocumentOpened`. The watch is armed from `OpenDocument` (Opening state), not from success: a
failed open self-heals when the next build produces a valid file, and a watch never outlives a
path switch. The watcher thread touches only Win32 file APIs and `PostMessage` — never MuPDF,
never Direct2D.

**PageLayout** (continuous vertical mode): virtual canvas where page *i* sits at
`y = Σ (pageHeight[j]·zoom) + gaps` (inter-page margins; distinct from the sync feature's
*alignment gaps* below), width = max page width·zoom + padding. The geometry arrays are per
SLOT, where a slot is a real page or a sync-point *alignment gap* (an empty page-sized hole);
real-page identity survives through a slot map (`SlotToReal`/`RealToSlot`, -1 = gap), fit
inputs (`MaxPageSizePt`, `SumPageHeightsPt`, `PageCount`) see real pages only, and with an
empty gap list the layout is bit-identical to a slot-less one. A prefix-sum array of slot
bottoms lets a binary search find the first visible slot in O(log n); hit testing subtracts
the page's on-screen origin and applies the engine's inverse page transform (handles rotation and
PDF's bottom-up coordinates), returning misses inside gaps. Relayout re-runs when scrollbar
appearance shrinks the viewport (loop until stable, a real subtlety copied from SumatraPDF).
Fit-width/fit-page are *virtual* zooms recomputed on every relayout.

**Paged scroll mode** (View → Continuous Scrolling / Page-by-Page, Ctrl+4 / Ctrl+5; global for all
panes, persisted as `[window] scrollMode`): the continuous `PageLayout` stays untouched — the
mode is a per-page clamp plus quantized navigation, never a rebuilt one-page layout (that would
break `FirstVisible`, prefetch adjacency, `TotalHeight` scrollbars, fit estimation and the
page-unit sync contract). Invariants, each traced to the cross-viewer research pass
(SumatraPDF/Okular/Evince sources and bug archives):

- `m_currentPage` is the single authority. `ClampScroll` confines `m_scrollY` to the page band
  `[PageRect.top, PageRect.bottom − vp.cy]`; a page that fits degenerates to the one centered
  position, possibly NEGATIVE for page 0 (`ContentOrigin` skips whole-content centering in
  paged mode and always translates by `-m_scrollY`). The band excludes gap/margin, so neighbors
  never peek in; `DrawDocument` draws only the current page but keeps the wanted range at
  `cur±1` so flips land instantly.
- Two input regimes. Programmatic jumps (outline, internal links, search, SyncTeX, sync
  targets, thumb drag, session restore) call `AdoptPage(target)` and may rest mid-page.
  User-incremental input (wheel, arrows, PgUp/Dn, Space, SB_LINE*/SB_PAGE*) goes through
  `PagedStepY`/`PagedWheelY`: the event that REACHES the band edge never flips; only a
  subsequent event already at the edge does (the "mandatory stop" gate — Acrobat's
  immediate-flip at the edge is the design users complain about).
- A wheel flip needs one full detent of signed accumulated delta (`m_wheelAccum`), reset on
  direction change, every completed step, relayout/zoom, and flip attempts at the document
  ends (Okular bug 498038: leftover credit at the ends must drain or it eats the next opposite
  notch). Full reset, never `-= 120`: coalesced multi-notch messages cap at one flip. Wheel
  delta sign is INVERTED relative to scroll direction (delta > 0 = wheel away = scroll up =
  PREVIOUS page) — the classic sign bug, caught in plan review.
- Landing: forward flip → page top; backward flip → page BOTTOM (upward reading stays
  continuous; unanimous among viewers with edge-flip). `m_scrollX` is preserved across flips.
  LEFT/RIGHT are horizontal-first: with real horizontal overflow they only scroll (never flip,
  not even at the horizontal edge); without it they flip (a ~2 px tolerance absorbs fit-zoom
  rounding so FitWidth always flips).
- Hit-testing pins to the current page (`PagePointAt` rejects other pages, `CaretAt` clamps to
  the current one): neighbors exist in layout space and keep cached link/text models
  (`EvictStale` keeps `cur±1`), so without the pin, hovering/clicking the background would
  activate links on pages that are not on screen.
- Sync is untouched: flips emit `Scrolled` and positions stay fractional page units; the
  follower adopts the incoming target page inside `ScrollToSyncPosition`. The vertical
  scrollbar keeps document-wide semantics (Acrobat school, chosen over Sumatra's
  page-local bar): the thumb shows the true document position and dragging adopts the page
  under the viewport center.
- `OnDocOpened`'s restore path must adopt the saved page BETWEEN the offset assignment and its
  `ClampScroll`, or every paged session restore and auto-reload clamps into page 0's band and
  lands on page 0.

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
  mixed page sizes within a document, and different zoom levels per pane.

Design:

- **Position model (satisfies R2)**: a pane's position is expressed in *page units*,
  `pos = pageIndex + fractionWithinPage`, sampled at the **window center** (`SyncCenterY`):
  half the pane WINDOW's height, not the client's. The panes' windows share top and
  height, but their clients can differ by one horizontal scrollbar (only an overflowing pane
  gets it), and client-center sampling misaligned identical pages ON SCREEN by half the bar
  whenever the outline sidebar pushed exactly one pane past the overflow threshold (a real
  0.6 bug). Without a horizontal bar the centers coincide. Every page counts as
  exactly 1.0 regardless of its physical size, so an A4 document stays page-aligned with a Letter
  or A5 one, and panes at different zoom levels stay aligned too. When page heights differ, the
  panes scroll at different pixel speeds by construction; that is what *keeps* them in sync.
  Syncing raw pixel/scrollbar offsets (PDF Architect's apparent approach, and the source of its
  drift) is explicitly rejected.
- **Default mode, delta-preserving sync (satisfies R1)**: when the user enables sync, capture a
  per-slot delta anchor in page units, RELATIVE TO THE REFERENCE SLOT (the first JOINED pane): a
  leader converts its position to reference units and every follower converts back out — with
  two panes that is arithmetically the single `posB − posA` delta of the pre-slot design. Every
  scroll of one pane drives the others to preserve the anchors. Users pre-scroll each pane to
  the sections they want aligned, then lock; unlocking, adjusting one pane, and re-locking
  recaptures the anchors at any moment. This is exactly what Acrobat/PDF-XChange users have been
  asking for and don't have. Scroll and zoom sync operate on the JOINED subset of the active
  panes: a pane joins only when its `DocumentOpened` has been PROCESSED by the controller, not
  as soon as it holds a document — a (re)opening pane reports a document while its saved view is
  still being restored, and the restore's own scroll echo must not drive the established panes
  through a never-captured anchor. An empty third pane never suspends synchronization between
  the loaded ones. Point-map operations, by contrast, require every active pane to hold a
  document.
- **Relative-fraction mode** (optional): `fraction = scrollY / (canvasHeight − viewportHeight)`
  mirrored to the followers; useful for grossly different documents, known to drift locally.
- **Temporary unlock**: holding a modifier (Alt; Shift is taken by horizontal wheel scroll)
  scrolls only the focused pane. While the sync-point map is EMPTY the anchor tracks each
  adjustment, so the alignment crafted under Alt is exactly what subsequent synced scrolls
  preserve, and re-enabling sync recaptures. With a non-empty map the map is authoritative:
  Alt is a transient peek whose deviation is reabsorbed by the next locked scroll (the
  permanent fix is Alt-adjust followed by "Add Sync Point Here"), and re-enabling sync
  recaptures nothing.
- **Reentrancy guard**: an `isSyncing` flag suppresses feedback loops when programmatically
  scrolling the siblings.
- **Zoom sync**: independent toggle; applies the same zoom command to every document-ready pane
  (per-slot ratio anchors relative to the reference pane, driving absolute targets so clamping
  at the zoom bounds self-heals instead of corrupting the relationship).
- **Sync points** (WinMerge-style, implemented): an ordered list of WHOLE-page tuples
  (`SyncPoint`, one page per active SLOT) turns the plain anchors into a
  piecewise-constant INTEGER delta. Alignment is per page: one page of one document is one page
  of the others, like WinMerge's line map, never a fractional scroll offset (the within-page
  fraction transfers unchanged, an interpolated map would rubber-band the scroll speed instead).
  With alignment gaps OFF, between two points the follower is clamped just short of its next
  point, so it WAITS at the end
  of its own section while the leader crosses pages that have no counterpart and resumes
  seamlessly when the leader reaches the point; leading from the short side jumps the surplus
  pages in one block instead. The clamp epsilon is 1% of a page, NOT a token value: page
  attribution round-trips through whole-pixel scroll quantization
  (`ScrollToSyncPosition` -> `SyncPosition`), and a sub-pixel epsilon parks the viewport
  center ON the boundary, where the counter shows the next section's first page on some
  DPI/zoom combinations (bug surfaced by 96-DPI RDP metrics).
  The two directions are not exact inverses at segment boundaries: the reentrancy guard stops
  the echo, and every scroll re-drives the follower from the leader's authoritative position.
  Invariants: points strictly increase in EVERY active coordinate; a newly added manual point
  wins (conflicting points are removed); the map is cleared on every (re)open; emptying the map
  recaptures the plain anchor at the current positions; the EMPTY map degenerates to the plain
  anchor, bit-identical to the modes above. Commands: Add/Remove Sync Point Here (Shift+F7),
  Sync Points... (list/remove dialog), Clear Sync Points (Ctrl+Shift+F7). The first is a
  TOGGLE around `PointIndexHere()`, which answers with the index of the point whose tuple is
  exactly the pages in view (-1 when there is none, and whenever `AddPointHere` would refuse,
  so one call decides both the direction and the button state): on a hit the command removes
  that point, otherwise it captures every active pane's current page. Without the toggle the
  command was a no-op-looking replacement there (the conflict rule erases the duplicate and
  reinserts it as manual), and removal existed only inside the dialog. The toolbar button is
  the state made visible - a `BTNS_CHECK` pressed while the panes sit on a point - and it is
  the ONE checked state that follows the SCROLL, so it cannot ride on `UpdateCommandUi` (which
  is deliberately skipped on `Scrolled` ticks): `UpdateSyncPointButton` re-evaluates the query
  per tick against a cached 0/1 and touches comctl32 only when it flips, while UpdateCommandUi
  writes the state unconditionally and simply refreshes the cache (a fresh toolbar comes up
  unpressed, and a click auto-toggles a check button behind the code's back).
- **Bookmark generation**: "Sync Points from Bookmarks" parses a hierarchical key from each
  outline title (`util/OutlineNumbering`: multi-level prefix like "1.2.3", with an optional
  verbal intro word across fourteen languages - IT/EN "Capitolo/Cap/Chapter/Ch/Sezione/Sez/
  Section/Sec/Parte/Part/Appendice/Appendix/Annex/Allegato", DE "Kapitel/Kap/Abschnitt/
  Abschn/Teil/Anhang/Anh/Anlage", FR "Chapitre/Chap/Sect/Partie/Annexe" (Section/Sec/
  Appendice shared with IT/EN), HU "Fejezet/Fej/Szakasz/Rész/Függelék/Melléklet",
  UK "Розділ/Розд/Глава/Гл/Частина/Додаток/Дод", RO "Capitol(ul)/Secțiune(a)/Partea/
  Anexă/Anexa" (comma-below ș/ț AND their legacy cedilla spellings), PT "Capítulo/Seção/
  Secção/Apêndice/Anexo" (Cap/Parte shared), EL "Κεφάλαιο/Κεφ/Ενότητα/Μέρος/Παράρτημα"
  (plus the accent-less forms ALL-CAPS Greek titles lower to, with σ for final ς),
  ES "Sección/Apéndice" (Capítulo/Cap/Parte/Anexo shared with PT), PL "Rozdział/Rozdz/
  Część/Sekcja/Dodatek/Załącznik/Aneks", NL "Hoofdstuk/Hfst/Sectie/Paragraaf/Deel/
  Bijlage/Aanhangsel", CS "Kapitola/Oddíl/Část/Sekce/Příloha/Díl" (Kap/Dodatek shared),
  SV "Avsnitt/Bilaga" (Kapitel/Kap/Appendix shared; "Del" deliberately excluded, Spanish
  "Del 1 al 10" would parse as a numbering), plus
  "§"; the intro-word tokenizer is Unicode-aware AND locale-independent (GetStringTypeW +
  invariant-locale lowercasing, never the user-language Char* APIs, so matching cannot
  change with the host Windows language), accented Hungarian words survive, and a
  non-ASCII first letter always tokenizes as a word (components stay ASCII), so
  Cyrillic and Greek intro words reach the lookup; ASCII
  digits only; SINGLE-letter components encode negative,
  -1 = A, so "Appendice A" and "A.1" pair without colliding with numeric keys, while a lone
  letter without an intro word is rejected as a plain word EXCEPT under an appendix heading:
  the matcher passes `allowLoneLetter` for sub-items whose parent canonicalizes to the
  "appendix" class, so LaTeX-style appendix labels "A Foo"/"B Bar"/"C Baz" pair across
  translations by their language-neutral letter). Titles that parse to no key at
  all pair on the TITLE channel instead: trimmed case-insensitive whole-title equality
  at the SAME outline depth ("Sommario", "Indice analitico"; numeric keys carry their
  own hierarchy, canonical titles do not - without the depth guard a top-level
  "Introduzione" could anchor to a chapter's nested unnumbered "Introduction", and
  real translations share the outline structure, so requiring equal depth costs
  nothing), plus a cross-language canonicalization of common
  front/back-matter section names across Italian, English, German, French, Hungarian,
  Ukrainian, Romanian, Portuguese, Greek, Spanish, Polish, Dutch, Czech and Swedish
  (`CanonicalTitleKey`), so "Indice"/"Contents"/"Inhaltsverzeichnis"/"Table des matières"/
  "Tartalomjegyzék"/"Зміст"/"Cuprins"/"Índice"/"Περιεχόμενα"/"Spis treści"/
  "Inhoudsopgave"/"Obsah"/"Innehåll" collapse to
  one class.
  Italian "Indice" (front summary) and English/German "Index" (back
  analytical index) are false friends kept in DIFFERENT classes on purpose (Portuguese
  and Spanish "Índice" side with the TOC, the back index being "Índice
  remissivo"/"Índice analítico"); unrecognized
  titles fall back to their own text, so same-language exact pairs are unaffected. The matcher
  (`MatchOutlineNumberings`) takes N outlines and returns rows of indices for the keys present
  in ALL of them — a join on the canonical, document-independent key, so more documents just
  means a smaller intersection, and with two outlines it is identical to the pairwise version
  it replaced; requiring every outline to carry the key is what keeps the tuples usable as hard
  anchors (a row with a missing coordinate could satisfy neither the strictly-increasing
  invariant nor the alignment-gap arithmetic). First occurrence per key/title and per outline
  wins, and one point is emitted per key present in ALL outlines; only the bookmarks' target
  pages matter (alignment is whole-page). Candidates that violate the all-coordinates
  monotonicity (out-of-order bookmarks, and deep subsections starting on their parent section's
  PAGE - two points on one page would conflict, the first wins) are greedily dropped.
  Generation replaces previously generated points, keeps manual ones (manual wins on conflict),
  turns scroll sync on and realigns the followers once. After an auto-reload of the SAME path,
  MainWindow re-derives generated points from the fresh outline (the LaTeX rebuild loop must
  not lose the map on every compile); manual points are dropped there: they reference pages the
  rebuild may have moved arbitrarily. The regen cue is PARKED in the controller
  (`AutoRegenPending`), not derived from the map at the moment of the event: the map is already
  cleared by the first `DocumentOpened` of an all-panes reload, and a failed intermediate
  reload (broken half-written compile) fires `DocumentOpened` with no document - the parked cue
  rides both out and is cancelled only by a path change (open/close/swap) or an explicit clear.
- **Alignment gaps** (WinMerge-style rendered holes; Sync ▸ Show Alignment Gaps, checked by
  default, persisted as `[sync] showGaps`): where one document has pages with no counterpart
  inside a segment, the OTHER panes' layouts get empty gap slots just before their own point
  pages, each silhouetted like the missing counterpart page (its size in PDF points, rendered
  at the local zoom, width capped at the real pages' width so `TotalWidth` stays
  gap-invariant); the pre-first-point segment gets gaps too (different-length preambles align
  from the top), the tail after the last point diverges freely. Each segment contributes
  max(interior) slots to ALL panes, with the gap silhouettes borrowed from the pane that
  supplied that maximum, so every point's pages land on the same slot
  index - and with gaps enabled scroll sync becomes IDENTITY on virtual slot coordinates
  (`VirtualSyncPosition`/`ScrollToVirtualSyncPosition`): the follower scrolls THROUGH its gaps
  1:1 instead of waiting. Virtual sync is gated on a GAP EPOCH (a version MainWindow stamps on
  every pane with every gap push; a (re)opened pane resets to 0): the reload restore dance
  fires `Scrolled` after `SetPages` cleared the reloading pane's gaps but before
  `DocumentOpened` clears the map, and without the matching-epoch check that scroll would
  drive mismatched slot layouts; the real-page `MapTarget` fallback is layout-shape-invariant. Paged mode flips gap slots like blank pages (`m_currentSlot` is the
  flip cursor; render keys, the page counter and real `SyncPosition` derive the real page, and
  on a gap slot the counter pins to the previous real page). Persisted scroll offsets are
  normalized to the NO-GAP coordinate space (`PersistScrollY` subtracts `GapPixelsAbove`):
  every restore lands in a gapless layout (`SetPages` clears the gaps) and the later gap
  rebuild preserves the position in real-page units. The map-change reaction is a
  `SyncController` callback (`SetMapChangedHandler`) fired on EVERY map mutation including the
  implicit `DocumentOpened` clear; MainWindow recomputes every pane's gaps and marker lists,
  wrapping the pane relayouts in `ApplySilently` (the controller's reentrancy guard) so the
  gap-collapse scroll echoes never drive the siblings.
- **Sync-point markers**: an anchor glyph (U+2693, DirectWrite "Segoe UI Symbol", brush-tinted)
  beside each sync-point page's top-left corner (inside the corner over an alpha backing when
  the gutter is narrow), plus a tick strip along the right client edge (the native scrollbar
  cannot be custom-painted) with one tick per point at its page's proportional document
  position. The ticks use the focus-ring accent (manual opaque, generated 0.45 alpha); the
  anchor glyph goes darker in light mode (0x00529B, generated 0.6 alpha) because the thin
  glyph needs more weight than a 2px ring, while dark mode keeps the light accent (darker
  would sink into the background); under high contrast both become flat system colours
  (`COLOR_HIGHLIGHT` manual, `COLOR_GRAYTEXT` generated). Markers show whenever a map
  exists, gaps toggle notwithstanding; each rendering has its own Options checkbox
  ([sync] showAnchors / showTicks, default on). Hovering an anchor shows a tracking tooltip
  with the point's numbering key (or "manual"): strict monotonicity in every coordinate
  guarantees at most one point per page per pane, so the tip is always a single entry.
  The Sync Points dialog uses a report-mode ListView with column headers (#, Numbering,
  Pages, Origin; DialogTemplate gained a class-by-name AddControl overload because comctl32
  classes have no DLGITEMTEMPLATE atom).
- **Swap mirroring**: F8 is a ROTATION of the active slots — each pane adopts what its
  predecessor in visual order held, so left content moves to centre, centre to right and right
  wraps back to left; Shift+F8 rotates the other way (each pane adopts its successor, the
  inverse permutation, applied identically to documents, expectations, point tuples and
  fallbacks); with two panes both directions ARE the exchange, bit for bit. The map is
  preserved with every point's coordinates permuted the same way (the coordinates co-increase,
  so the order survives). The permuted map is parked in MainWindow BEFORE the reopen storm
  (each swapped pane fires `DocumentOpened`, clearing the live map) and reinstalled via
  `SyncController::RestorePoints` when EVERY active pane settled on its expected path; any
  `DocumentOpened` with a different path (failed open, close, interleaved open, a second swap)
  discards the park. `RestorePoints` touches neither the anchors, the sync flags nor the
  parked regen; permuted generated points re-arm reload regeneration naturally via
  `HasAutoPoints`.
- **Per-session persistence** (`[sync-points]`, most recent first, kMruMaxEntries cap, keyed
  by the per-slot paths with the centre key optional): only the MANUAL points are stored, as
  pure numbers - one 0-based page per ACTIVE slot joined by ':' ("l:r;l:r;..." with two panes,
  "l:c:r;..." with three; no titles in the file, no escaping, no INI buffer concerns), and an
  entry whose arity does not match the current arrangement is dropped on restore instead of
  being misread; a `hadAuto` flag records that the session also carried a generated map. The
  entry is upserted at every USER map mutation (add, remove, clear, generate, the reload
  regen, the swap-permutation install; an emptied map FORGETS the session) - never from the
  system's DocumentOpened clear, whose transient empty state must not wipe the memory.
  Restore fires when a pane's path CHANGES and every active pane is ready, only if the live
  map is empty (the swap's reinstalled park and freshly placed points always win): manual
  points are re-validated (range + strict all-coordinates monotonicity - the pagination may
  have changed, the file may be hand-edited) and reinstalled via `RestorePoints`, then
  `hadAuto` re-generates from the FRESH outlines (manual wins on conflict), which is also why
  storing the generated points themselves would be wrong. A same-path reload deliberately does
  NOT restore: in-session manual points decay on reload by design, but the saved entry keeps
  them for the next launch (the file is stable by then).

**Search** (per pane, find bar targets the focused pane):

- `fz_match_stext_page(ctx, stextPage, needle, hit_mark, quads, hit_max, options)` returns
  highlight **quads**, with `hit_mark` grouping the quads of one logical match (line-wrapped
  matches yield several quads). Match model:
  `struct Match { int page; std::vector<fz_quad> quads; }`.
  The worker builds the stext page itself with the flags MuPDF's own search path uses
  (`FZ_STEXT_DEHYPHENATE`, `source/fitz/util.c`), rather than letting `fz_match_page_number`
  build a private one: the whole-word filter below has to classify boundaries on the very page
  the matcher ran on.
- Quads live in page space and are transformed page→screen at paint time, so highlights survive
  scroll/zoom without re-searching. Active match in a distinct color, alpha-blended overlay
  (`FillGeometry` at fractional alpha over the page bitmap).
- Whole-document "m of n" count runs on the pane worker page-by-page, cancellable, posting
  incremental results; typing in the find box stays responsive.
- Results arriving NEVER select or scroll to a match: they land while the reader is still
  typing, and jumping would cost them the place they were reading. The counter therefore shows
  a bare total until something is selected, and the first F3 lands on the first match at or
  after the CURRENT page (wrapping to the top when every match is behind), not on match one.
- Normalization pass copied from SumatraPDF's `TextSearch`: Unicode case folding, whitespace
  collapse, hyphen and smart-quote variants.
- **Search options** (`Document::SearchOptions`, an application-level POD: no `fz_` type reaches a
  UI header, and the translation to `fz_search_options` happens inside the worker). They are part
  of the query IDENTITY, not a side setting: the pane latches the `(needle, options)` pair, so
  toggling one with the text unchanged re-runs the search instead of being swallowed by the
  same-needle guard. Global to the frame rather than per pane, because the find bar retargets
  whichever pane has the focus. Persisted in `[find]`.
  - **Match case**: `FZ_SEARCH_EXACT` instead of `FZ_SEARCH_IGNORE_CASE`. The `fz_search_*`
    family this replaced is itself a shim over `fz_match_*` with `FZ_SEARCH_IGNORE_CASE`
    (`source/fitz/stext-search.c`), so the engine underneath is unchanged and the
    case-insensitive branch is bit-for-bit what shipped before. This lifts the v1 decision to
    stay on `fz_search_*`: that decision was made because `fz_match_*` was labeled experimental,
    but the stable entry point was already running it.
  - **Match whole word**: MuPDF 1.28 has NO flag for it, and the obvious `FZ_SEARCH_REGEXP` route
    with `\b…\b` is unusable - mujs classifies word characters with an ASCII-only `iswordchar`
    (`thirdparty/mujs/regexp.c`), so `\bperché\b` matches nothing at all, which in a fourteen-
    language application is a broken feature rather than a limitation. The boundary is therefore
    decided worker-side: locate the run geometrically (the hit quad's centre picks the
    `fz_stext_line`, the covered characters are those whose own quad centre falls inside it),
    then take the neighbours from the line's character LIST. Geometry only finds the run; "before"
    and "after" come from reading order, which is what keeps bidi and vertical `wmode` correct.
    The predicate is `util/TextClass.h` (`GetStringTypeW`, `C1_ALPHA | C1_DIGIT`, no locale
    input). A side whose own needle edge is not a word character is left unconstrained, so "(a"
    and "3.14" still match. A run the geometry cannot locate is KEPT: a visible false positive is
    correctable, a silently missing match is not.
  - **Regex** (`FZ_SEARCH_REGEXP | FZ_SEARCH_KEEP_LINES`). The engine underneath is mujs
    (`thirdparty/mujs/regexp.c`), a backtracking matcher that recurses once per character consumed
    (a `match()` frame is ~320 bytes) and whose only brake is `REG_MAXREC = 4096` frames. It has no
    step budget and no cancellation hook of any kind, and `stext-search.c` passes it no cookie, so
    what follows are mitigations, not fixes - each load-bearing, and the last entry is what is
    left uncovered:
    - `FZ_SEARCH_KEEP_LINES` puts a real `\n` at every line end of the haystack
      (`source/fitz/util.c`, `do_flatten`) and mujs's `I_ANY` refuses to cross one
      (`regexp.c`), so `.` is bounded by the LINE instead of by the page. It also gives regex
      users the per-line `^` and `$` they expect. Cost, in regex mode only: a match no longer
      crosses an implicit line break, so a wrapped phrase needs an explicit `\s`.
    - `/STACK 8388608` in the vcxproj, applying to every thread (`std::thread` passes
      `dwStackSize` 0, so the workers inherit the PE header value). This covers the atoms
      `KEEP_LINES` does NOT bound - a negated class or `\s` happily crosses newlines - and it is
      what lets `REG_MAXREC` actually fire instead of being overrun first. Measured, not
      estimated: at the old 1 MB reserve `[^Z]*` over `testdata/find-a.pdf` page 4 (>3000
      characters) kills the process with `0xC00000FD`, STATUS_STACK_OVERFLOW, which `fz_try`
      cannot catch because it is setjmp-based and this is an SEH fault; at 8 MB the same pattern
      returns one hit per page. Both cases are `scripts/test-find-options.ps1` phase 10.
    - **No live search in regex mode**: every prefix of a pattern is itself a pattern, and the
      debounce would run `(a+`, then `(a+)`, then `(a+)+` one keystroke apart. The query is
      confirmed with Enter (or by toggling an option, which is also a confirmation); the counter
      deliberately keeps showing the previous query's result meanwhile, so nothing suggests a
      search has run. `MainWindow::m_findRegexPending` records the unexecuted query, which is why
      Enter must be able to FORCE a re-run past the pane's `(needle, options)` latch. That flag
      outlives the bar: the edit box keeps its text when the bar is closed, so REOPENING it is not
      a confirmation either, and `ShowFindBar`'s re-issue of the box's contents (there to move the
      highlights when the bar changes pane) runs only a query that has already been executed once.
    - A needle that does not compile is caught ONCE per search, worker-side, by an
      `fz_new_search`/`fz_drop_search` probe before any page work: `fz_match_stext_page` throws
      `FZ_ERROR_ARGUMENT` on the first page and the per-page `fz_catch` - which exists to skip
      DAMAGED pages - would file it as "zero hits", silently. `SearchResult::badPattern` reaches
      the counter as a localized one-word error state instead.
    - `Document::kMaxMatches` (10000) caps the hits per document, checked per page and carried
      across chunks by the continuation job. The counter marks it with `≥`, never with another
      `+`: `+` already means "the scan is still running, more may arrive", and the cap is the
      opposite statement.
    - Whole word composes with regex (the boundary filter is independent of the matcher), and
      there both needle edges are constrained unconditionally: `(a+)` starting with a parenthesis
      says nothing about what the pattern matches.
    - What stays UNFIXED, by construction: nested quantifiers (`(a+)+b`, `(\w+\s+)+end`) run in
      exponential time inside `js_regexec`, with nothing to abort them. The pane's worker also
      serves renders, so that pane freezes. The containment is `Document::Shutdown`: it waits two
      seconds for the worker and then DETACHES it, so the application exits and saves its session
      instead of hanging on `join()` until the Task Manager. The detached thread still owns every
      member of its `Document`, so `PaneWindow` leaks the object rather than destroying it
      (`m_docAbandoned`); the cloned `fz_context` is never dropped and `MupdfLib`'s base context
      is a leaked static, so nothing it touches goes away underneath it. That leak is deliberate
      and one-shot. The timed wait does not ask WHY the worker is late, so the same path also
      catches an ordinary slow job that ignores the abort cookie (a first `fz_open_document` on a
      huge file or a stalled network share, if a pane is closed right then): harmless, because the
      abandoned worker's posts fail once the window is gone and the thread dies with the process,
      but it is why the timeout is a supported state and not an assertion. Both halves are
      reported upstream (Artifex Bugzilla, 2026-08-06): [709616](https://bugs.ghostscript.com/show_bug.cgi?id=709616)
      asks mujs for a work budget in `match()` (it bounds recursion DEPTH but not work, which is
      the wrong axis: measured against the bundled 1.28.0 mujs, `(a+)+b` over N `a` characters
      doubles per character, 0.050 s at N=20 and 25.3 s at N=29, with `js_regexec` returning "no
      match" rather than the depth error), and [709617](https://bugs.ghostscript.com/show_bug.cgi?id=709617)
      asks MuPDF for an `fz_cookie` on the search API, which no entry point currently takes.

**Selection & links**: mouse selection via `fz_snap_selection` (char/word/line on click count) +
`fz_highlight_selection` for quads; Ctrl+C via `fz_copy_selection(ctx, stext, a, b, crlf=1)` →
UTF-8→UTF-16 → `CF_UNICODETEXT`. Links via `fz_load_links` per page: hand cursor on hover;
internal destinations resolved with `fz_resolve_link_dest`, external URLs to `ShellExecuteW`.
Outline sidebar (`fz_load_outline`) is an M5 item.

**Environment hygiene for spawned processes**: `wWinMain` clears `ELECTRON_RUN_AS_NODE`. Started
from VS Code (its debug launcher, or an extension launching an external viewer) the app inherits
it; a shell-spawned child inherits our block in turn, and Electron reading that variable boots as
plain Node, so the `vscode://` inverse-search handler rejects `--open-url` and exits. The symptom
is Ctrl+click doing nothing at all, with no way to report it: `ShellExecuteW` returns success
because the process really did start. Invisible when the app is launched from Explorer, which is
why it survived to 0.9.1.

### 4.3 Input handling

- **Wheel**: accumulate `WM_MOUSEWHEEL` deltas with remainder (never assume multiples of 120),
  consume in units of `WHEEL_DELTA / SPI_GETWHEELSCROLLLINES`, scroll in *pixels* so
  precision-touchpad two-finger scroll is naturally smooth. Handle `WM_MOUSEHWHEEL`.
- **Ctrl+wheel zoom-to-cursor**: with cursor at viewport point `c`, scroll offset `s` (device px),
  zoom `z → z'`: `s' = (s + c)·(z'/z) − c`. Continuous factor from accumulated delta.
- **Keyboard**: PgUp/PgDn/Home/End/arrows per pane; Tab cycles the focused pane in visual order;
  F3/Shift+F3 next/previous match; Ctrl+F find; Ctrl+O / Ctrl+Shift+O / Ctrl+Shift+M open
  left/right/centre (the centre one switches three-pane mode on, like the menu-only View ▸
  Two/Three Panes radio pair that picks the arrangement directly); F7 toggle scroll sync;
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
- comctl32 toolbars measure `BTNS_AUTOSIZE` label widths ONCE, at `TB_ADDBUTTONSW` time, with the
  font installed at that moment, and under PMv2 the control swaps its own per-DPI font in only
  AFTER the top-level `WM_DPICHANGED` handler returns (`WM_DPICHANGED_AFTERPARENT`): after a
  monitor change the text is wider than the cached rects and every label truncates to an ellipsis
  (`TB_AUTOSIZE` does not re-measure). So labeled toolbars are never patched in place on a DPI
  change: the command toolbar is recreated in its band (`RebuildToolbarInBand`, the same path the
  text-mode and language switches use, which also re-fetches per-DPI icons and band metrics), and
  the menu band re-adds its buttons whenever its font changes (`MenuBand::SetFont`). The icon-only
  bars (find bar, full-screen escape hatch) are DPI-keyed instead: explicit `TB_SETBUTTONSIZE`
  plus a rebuilt imagelist, no text to re-measure.
- Dark title bar via `DwmSetWindowAttribute(DWMWA_USE_IMMERSIVE_DARK_MODE)`; page background aside,
  chrome colors follow the system light/dark setting. MuPDF-rendered dark mode (color inversion)
  is out of scope for v1.
- Every glyph imagelist BAKES `GetSysColor(COLOR_BTNTEXT)` into premultiplied pixels, and each
  cache is keyed on the DPI alone, so a HIGH CONTRAST switch would otherwise leave the icons in
  the old ink until a DPI, language or text-mode change happened to rebuild them.
  `WM_SYSCOLORCHANGE` re-bakes all three sets (`RebuildSystemColorAssets`) and forwards the
  message to the common controls, which the message's own contract requires: it reaches top-level
  windows only, and a toolbar left unnotified keeps drawing its buttons in the old 3D Objects
  colour. `WM_THEMECHANGED` runs the same handler, as the accessibility guidance asks of anything
  that CACHES colours. Ordinary light/dark switching does NOT arrive here (the classic system
  colours do not follow it, which is also why the chrome stays light in dark mode); that one comes
  as `WM_SETTINGCHANGE`/`ImmersiveColorSet` and only retints the title bar and the panes.
- HIGH CONTRAST reaches the pane canvas too, which is drawn by hand and therefore has no theme
  part to hand to uxtheme: the documented fallback for an owner-drawn surface is `GetSysColor`,
  and `PaneWindow::Themed(sysIndex, dark, light)` returns the system metric when high contrast is
  on and the built-in palette otherwise. The condition is not optional - the classic colours do
  not follow the light/dark personalization, so using them unconditionally would flatten the dark
  theme to Windows 95 grey. Detection is `SPI_GETHIGHCONTRAST` (the only supported probe: there is
  no registry key, and guessing from `COLOR_WINDOW` is wrong for the HC-white themes); MainWindow
  owns it and pushes it into the panes (`SetHighContrast`, which repaints UNCONDITIONALLY, because
  switching between two HC themes changes the colours without changing the on/off answer).
  The mapping keeps the documented PAIRS: canvas `COLOR_WINDOW` with the placeholder text and the
  page border in `COLOR_WINDOWTEXT`, header strip `COLOR_BTNFACE` with `COLOR_BTNTEXT`,
  `COLOR_GRAYTEXT` for everything de-emphasized (inactive pane name, generated points, non-active
  search hits, alignment-gap silhouettes), `COLOR_HIGHLIGHT` for every accent (focus ring, active
  pane underline, manual points, anchor glyph, selection, active hit) and `COLOR_HOTLIGHT` for the
  SyncTeX flash, which has to stay distinguishable from both. Three consequences worth knowing:
  the PAGE stays white (it is content, and inverting it is the post-1.0 dark reading mode), so the
  three overlays that sit on it keep their alpha and only change hue - an opaque HC fill would
  erase the glyphs it is meant to emphasize; both stock HC themes give `COLOR_BTNFACE` and
  `COLOR_WINDOW` the SAME value, so the header strip and the tick strip gain a `COLOR_BTNTEXT`
  hairline (Windows separates HC regions the same way) and the page border is inset half a pixel,
  since a 1px stroke centred on an integral edge is 50% blending on two columns; and the alpha
  washes that carry meaning elsewhere (6% tick backing, 45% generated ticks) become flat colours.
- Drag & drop: one file dropped on a pane opens there; extra files land in the OTHER panes in
  visual order, and two extra files switch three-pane mode on first (three files always mean a
  three-pane arrangement).
- CLI: `PdfSideViewer.exe [left.pdf [right.pdf [center.pdf]]]` — Beyond Compare's positional
  order (left, right, THEN center: `kCliSlotOrder` in main.cpp), deliberately not the visual
  order; a third argument switches three-pane mode on.
- Settings + last session (files, scroll positions, zoom, sync state, window placement) in
  `%APPDATA%\PdfSideViewer\settings.ini` (UTF-16LE with BOM; INI over JSON so the file stays
  hand-editable). The `GetPrivateProfile*`/`WritePrivateProfile*` APIs are NOT used: `Save`
  serializes the whole file in memory, writes it to a uniquely named temp sibling
  (`CREATE_NEW`, checked byte counts, `FlushFileBuffers`, checked close — a partial file must
  never be promoted) and swaps it in atomically; `Load` reads the whole file through ONE handle
  and parses it. Each profile call is coherent on its own, but dozens of them can straddle a
  concurrent replacement and assemble a HYBRID snapshot, which the next save would then make
  permanent. The swap is ONE `MoveFileExW(MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)`
  and deliberately **not** `ReplaceFileW`: that call exists to carry the target's security
  descriptor, attributes and streams onto the replacement (see the security model below), and
  in exchange it has two documented PARTIAL failure states
  (`ERROR_UNABLE_TO_MOVE_REPLACEMENT`, `..._2`) that leave the canonical name deleted or
  renamed to something else. A same-volume rename on the local NTFS volume the profile lives
  on has no partial state: `settings.ini` resolves either to the old file or to the new one,
  never to nothing, so there is no window for a backup sibling or a startup recovery to exist
  for. (Source and target are always siblings and no cross-volume copy flag is passed; the API
  itself promises replacement, not rollback or power-loss semantics on every third-party
  provider. Should one ever leave the canonical name missing after a failed rename, the
  complete temp is KEPT — the sweep stops entirely while `settings.ini` is absent — but is not
  adopted automatically: a temp truncated by a crash is indistinguishable from a complete one
  without a second file state to mark it, and half a settings file is worse than defaults, so
  that case degrades to a manual rename. That protection lasts only while the canonical name is
  missing: the first run to recreate `settings.ini`, defaults included, turns the temp back into
  ordinary stale residue. It is a forensic artifact, not a recovery slot.) Cleanup does not
  inspect the directory at all: it RECONSTRUCTS the names this process id can produce
  (`settings.ini.<own pid>.<0..63>.tmp`) and deletes those. A live process cannot share our id,
  so every one of those names belongs to us or to a dead run and there is no writer to race;
  no pattern is handed to the filesystem, so a user's own `settings.ini.something` (and whatever
  8.3 aliasing would have matched, the API's trailing `.*` including `settings.ini` itself) is
  out of reach by construction. The earlier version matched a wildcard and re-parsed a name
  grammar to decide whether a FOREIGN temp was the app's, then dated it against a one-hour clock
  to guess whether its writer was still alive. Stated cost of dropping that: a temp left by a run
  whose id is never reused stays for good, inert, holding what `settings.ini` already holds.
  Writers serialize best-effort through
  a lock file (`util/ScopedFileLock.h`; profile-ACL'd, crosses RDP logon sessions, no predictable
  kernel object name to pre-create), and an unserialized save is still safe: whole files win or
  lose wholesale. The lock does not merge: two instances that loaded the same state are
  last-close-wins, as they always were. Accepted trade-off: a downgrade round-trip drops keys the
  older build does not know (versionless safe-default keys by contract).
  File ▸ New Window turns that from a rarity into a normal state, so what it costs is worth
  stating. Safe by construction: the per-slot DOCUMENTS (an empty window seeds `m_fallback` from
  the session it loaded and `SaveSession` writes those back untouched, so it never blanks the
  stored panes), the atomic swap, the per-pid temp sweep and the HKCU verb lock. Genuinely lost,
  rolled back to what the other window loaded at ITS start: the sync-point memory
  (`[sync-points]`, hand-placed work with no other copy), the MRU lists, and every Options value
  including the language - which also visibly DIFFERS between two live windows, `g_lang` being
  per process with no cross-process notification. Documented, not synchronized: a broadcast would
  reopen "which live state travels" for every setting.
- `AppSettings` is a class rather than an aggregate for one reason: the declared range of each
  field, and the two rules that span more than one (`normalRect` exists only with `hasPlacement`;
  two of the three-pane shares must leave room for the third), are an invariant of the TYPE, not a
  property of the file. `AppSettings::Normalize` is their single owner and BOTH paths run it, so a
  hand-edited file and an out-of-range caller are repaired by the same code: `Load` returns a
  normalized snapshot and `Save` serializes a normalized copy, leaving the caller's own object
  untouched. Before that the clamps lived on the load path only, `Save` wrote whatever it was
  handed, and `MainWindow` re-checked three of them by hand. The data members stay public: there is
  no other invariant here, so getters would add a call layer and protect nothing.
- **Security model for the settings file, declared:** the settings DIRECTORY is the boundary,
  not the file. Every save promotes a freshly created temp, which carries the directory's
  inherited DACL, so a per-file DACL or per-file EFS state set BY HAND does not survive a save.
  This is a decision, with three reasons: the app never sets either one (both are external
  actions on a file it rewrites on every exit); the policy deliberately trusts every principal
  the directory lets create files, rather than attempting a finer per-file distinction (Windows
  does keep `FILE_ADD_FILE`, the target's `DELETE` and the parent's `FILE_DELETE_CHILD`
  separate); and Microsoft's own EFS guidance is to encrypt the FOLDER, precisely because
  applications write files by renaming a temp over them. The
  consequence is explicit: settings are TRUSTED input (`[synctex] inverse` becomes a command
  line through `ShellExecuteW`/`CreateProcessW`), so whoever can write `settings.ini` can run
  code as the user. Protecting the DIRECTORY works and is inherited by every replacement;
  protecting only the file does not. Reverting to `ReplaceFileW` would restore per-file
  preservation at the cost of its two partial-failure states, and is the one change that would
  reopen that trade-off.
- Lock IDENTITY follows the guarded RESOURCE: the settings lock sits next to `settings.ini`
  (two copies pointed at different directories write different files and must not contend),
  while the shell-verb lock lives in `UserLockDirectory()` — `%LOCALAPPDATA%\PdfSideViewer` via
  the Known Folder API, immune to `PSV_SETTINGS_DIR` and to the `APPDATA` environment variable —
  because the HKCU verbs are ONE resource per user wherever the settings happen to live. Local
  rather than roaming app data because roaming is the one redirected on purpose; local app data
  can be redirected too, so this reduces the odds of a network path under the lock without
  removing them — and a synchronous `CreateFileW` on unreachable storage blocks for as long as
  the redirector takes, whatever the lock's own timeout says.
- Password-protected files: `fz_needs_password` / `fz_authenticate_password` with a retry prompt.

---

## 5. Repository layout and build

```text
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
│       ├── PaneSlots.h        fixed pane-slot registry: visual order, active subset
│       ├── DxResources.*      D3D11/D2D device, per-pane swapchain
│       ├── engine/
│       │   ├── MupdfLib.*     base fz_context, locks, handlers (singleton)
│       │   ├── Document.*     fz_document wrapper + pane worker thread
│       │   └── RenderTypes.h  RenderRequest, TileKey, RenderResult
│       ├── render/RenderCache.*
│       ├── view/PageLayout.*  view/ViewState.*  view/SyncController.*
│       ├── search/TextSearch.*
│       └── util/              Settings (INI), Strings (14-language i18n), GlyphIcons (MDL2)
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
- The optional per-pane header strip is a CONSTANT DIP-scaled band reserved at the top of the
  pane, folded into `ViewportPx` (reduced height) and `ContentOrigin` (a downward shift): scroll,
  clamp, hit test and paint all account for it through those two choke points, and it collapses to
  a bit-identical no-header layout when off (`HeaderPx()==0`). `UpdateFitZoom` subtracts the SAME
  constant from the WINDOW height, never a client-derived metric, or it reopens the bar-visibility
  `WM_SIZE` recursion. When shown, the strip replaces the focus ring as the active-pane cue
  (accent underline). Both the underline and the fallback focus ring track the ACTIVE pane
  (`m_activePane`, pushed to the panes via `SetActive`), NOT the Win32 focus, so the current-pane
  marker survives window deactivation (there is deliberately no `WM_KILLFOCUS` handler clearing it);
  the outline sidebar refers to that pane, so losing the marker when the window blurs loses that
  association. The sync-sampling center (`SyncCenterY`) subtracts the band at the four sites that
  pair it with raw scroll offsets rather than routing through `ContentOrigin`, or the sync
  round-trip drifts by the band height. The strip is drawn LAST, as an opaque band over content
  the viewport has already scrolled under it, and the hit tests know nothing about that: a client
  point inside the band maps through `ContentOrigin` to real page content hidden beneath. So every
  input entry point (`WM_LBUTTONDOWN`, the word-select `WM_LBUTTONDBLCLK` after its placeholder
  branch, `WM_LBUTTONUP`, `WM_SETCURSOR`) tests `InHeaderStrip` first and does nothing there;
  only an in-flight selection drag still reads the band, as its auto-scroll-up area. The one
  gesture the strip DOES own is the double click, which fires the pane's `m_onOpenRequest` - the
  same handler the empty pane's placeholder uses, so the strip that names the document is also
  how you replace it, and the frame's dialog opens in that document's own folder. Hovering it
  raises a tracking tooltip with the full path (`m_headerTip`, uId 3 beside the scrollbar tip's 1
  and the anchor tip's 2), shown exactly when `HeaderStripText()` - the ONE function both the
  draw and the tip go through - differs from `m_docPath`: the file name always does, a compacted
  path does, an already-short path does not. A `TTF_TRACK` tip appears the instant its owner says
  so, which is wrong for a band spanning the full pane width - the pointer crosses it on the way
  to everything above - so the show is deferred to `WM_MOUSEHOVER`: `WM_MOUSEMOVE` only arms
  `TrackMouseEvent(TME_HOVER)` and every move re-arms it, which buys the system's own
  rest-to-show delay (`SPI_GETMOUSEHOVERTIME`, 400 ms by default, the same one ordinary comctl32
  tooltips obey) without a timer of the app's own. Hover tracking answers to REAL mouse input:
  a synthetic `WM_MOUSEMOVE` arms it but never produces the hover, so a test has to move the
  physical pointer and let it rest. Placement follows the pointer in X and drops clear of both
  the band and the CURSOR in Y (`max(HeaderPx + gap, client.y + SM_CYCURSOR)`), which is the UX
  guide's two rules together ("near the object being hovered, usually at the pointer's tail or
  head" and "avoid covering the object... even if that requires separation between the pointer
  and the tip"): a tip pinned to a corner of a full-width band is nowhere near the pointer, one
  placed AT the pointer covers the truncated text it exists to reveal, and one placed just under
  the band still ends up beneath the arrow, whose hotspot is in the strip while its body hangs
  below. The strip forces the standard arrow (the `WM_SETCURSOR` guard falls through to the class
  cursor), so its hotspot is the top-left corner and one cursor box below it clears the glyph at
  any DPI and at any accessibility cursor size. Unlike the scrollbar and anchor tips this one
  omits `TTF_ABSOLUTE`, so the control still pulls a long path back inside the monitor near a
  screen edge; the offset itself has to be ours, since the control otherwise puts its own
  top-left exactly on the point given. DirectWrite's own tail ellipsis stays undetectable (no
  `IDWriteTextLayout` exists in the app), which is the one truncation the tip cannot answer for.
- Render results that can fail must ALWAYS reach the pane (ok flag), and every latch the pane
  keeps (`pendingId`, `failedScale`) must be reset on device loss; any silent drop or surviving
  latch turns into a permanently blank page at that zoom.
- The `.rc` must never contain an `RT_MANIFEST` entry: the manifest is already embedded via the
  vcxproj `<Manifest>` item and a second copy is a CVT1100 duplicate-resource link error.
- `SaveSession` must persist the pre-fullscreen `WINDOWPLACEMENT` while full screen: entering
  full screen rewrites the live placement with the monitor rect, so saving the live one would
  make the window come back monitor-sized and borderless-shaped after a fullscreen exit+restart.
- `TBSTYLE_FLAT` is forbidden on a toolbar parented to a window that paints nothing under its
  children (`WS_CLIPCHILDREN` + `WM_ERASEBKGND` returning 1): flat = transparent background
  delegated to that parent, i.e. a black band. INSIDE a rebar band the opposite holds:
  `TBSTYLE_FLAT|TBSTYLE_TRANSPARENT` is the documented REQUIRED pattern, because the rebar
  itself paints the themed band background.
- The MenuBand track loop: the `WH_MSGFILTER` hook is thread-local and scoped strictly to the
  `TrackPopupMenuEx` call (RAII unhook on every exit path); hover-switching to an adjacent
  top-level popup goes through `EndMenu()` + re-track, never a second nested track; popup
  nesting depth comes from counting the frame-forwarded `WM_INITMENUPOPUP`/`WM_UNINITMENUPOPUP`
  (reset at every track entry), and `WM_MENUSELECT` says whether Right should open a submenu
  instead of switching. The `SC_KEYMENU` handler keeps the Alt+scroll gesture swallow FIRST;
  `WM_INITMENU` (also sent by TrackPopupMenu) keeps resetting that flag.
- MenuBand mouse truths (each cost a debug session): tracking must NOT start inside
  `TBN_DROPDOWN` — while that notification is in flight comctl32 keeps its own "dropped
  button" bookkeeping and paints that button hot regardless of TBSTATE, so after a hover
  switch the previous button stays lit for the whole chain; the notify posts a private message
  and tracking starts after comctl32 unwinds. Inside the hook the hit-test position is
  `MSG::pt`, never `GetMessagePos()` (stale inside the menu modal loop, whose `PM_NOREMOVE`
  peeks skip the refresh: switches fired one event late, not at all, or on the wrong button).
  The toolbar subclass swallows `WM_MOUSEMOVE` while a chain is open: the only moves reaching
  the toolbar then are synthetic reposts with stale positions, and native hot-tracking on them
  re-lights the previous button.
- Rebar band truths: bands are addressed by `RBBIM_ID`, never by index — the unlocked rebar
  lets the user reorder them. `RBN_HEIGHTCHANGE` must re-run Layout or band drags leave the
  panes under the bar (guarded by `m_layingOut`: Layout's own rebar MoveWindow fires the same
  notification back). comctl32 forces an `RBBS_FIXEDSIZE` band to the END of its row and
  re-applies that relocation on every re-layout: the page box carries the bit only while it
  already closes its row (`ApplyPageBoxFixedSize`), `SetRebarLocked` snapshots the visual
  order and restores it after the style pass, and `ApplyRebarLayout` writes styles/widths
  BEFORE positions. `RBBS_USECHEVRON` compares the CHILD width against `cxIdeal`, and the
  child loses band borders that no API exposes (`RBBIM_HEADERSIZE` reads 0 on gripper-less
  bands while ~4px of `RBS_BANDBORDERS` still apply), so a band sized exactly to its ideal
  clips into the chevron at rest: the menu band's cx is derived by MEASURING (oversize, read
  the child's client width, land it exactly on the ideal — `UpdateRebarBandSizes`), and the
  lock toggle compensates each band's header delta so child widths stay stable. The menu
  band's chevron popup reuses the bar's popups as submenus: every item is detached with
  `RemoveMenu` before `DestroyMenu`, or the destroy cascade takes the real menu bar down
  with it.
- Cross-process E2E gotchas (they cost real debugging time): `GetWindowText`/`SetWindowText`
  on another process's CONTROL do not exchange `WM_GETTEXT`/`WM_SETTEXT` — SetWindowText even
  returns success while only touching the caption cache. Test scripts must SEND `WM_SETTEXT`
  explicitly (marshaled) and read edits the same way; `SB_GETTEXTW`/`SB_GETRECT` need a
  `VirtualAllocEx` buffer in the target process.
- Toolbar glyphs are drawn white-on-black with `ANTIALIASED_QUALITY` (never ClearType: subpixel
  RGB fringes corrupt the coverage) and converted to premultiplied ARGB; `GdiFlush()` before
  touching DIB bits.

App project: `/std:c++20 /W4 /permissive- /utf-8`, x64 (ARM64 later; both mupdf.sln and the
installed toolchain support it). Distribution: portable zip first; installer later.

## 6. Milestones

| # | Deliverable | Contents |
| --- | --- | --- |
| M0 | Skeleton | frame + splitter + two pane HWNDs, D2D flip-model swapchains, PMv2 DPI, dark title bar |
| M1 | Single-pane viewer | MuPDF init, open document, continuous layout, whole-page renders on pane worker, scroll + zoom-to-cursor, stale-bitmap substitution |
| M2 | Industrial rendering | RenderCache with budget/eviction/dedup/abort, tiling at high zoom, prefetch, both panes live |
| M3 | The differentiator | SyncController (delta anchor, relative mode, temporary unlock, zoom sync), CLI args, drag & drop, session persistence |
| M4 | Text services | search + highlight overlays + background count, selection + clipboard, links |
| M5 | Polish | outline sidebar, keyboard completeness, settings UI, portable zip release, ARM64 |

Each milestone leaves the tree buildable and the app usable.

## 7. Risks and mitigations

| Risk | Mitigation |
| --- | --- |
| MuPDF VS projects target v142 | retarget to v143 is routine and documented; pin the MuPDF tag |
| `fz_match_*` search API experimental | adopted at 1.28: the "stable" `fz_search_*` is a shim over it, so the label never described a second engine |
| Regex search runs mujs: no step budget, no abort | `FZ_SEARCH_KEEP_LINES`, an 8 MB `/STACK`, confirm-with-Enter, a compile probe and a 10000-hit cap; a worker wedged on a nested quantifier is detached (and leaked), so the app still exits. See the Search section |
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

- MuPDF C API and threading rules: <https://mupdf.readthedocs.io/en/latest/reference/c/overview.html>
  and `docs/examples/multi-threaded.c` in the MuPDF repo
- SumatraPDF architecture (reference design for RenderCache/DisplayModel/TextSearch, GPL-compatible):
  <https://github.com/sumatrapdfreader/sumatrapdf> — `src/RenderCache.cpp`, `src/DisplayModel.cpp`,
  `src/EngineMupdf.cpp`, `src/TextSearch.cpp`
- DXGI flip model: <https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/for-best-performance--use-dxgi-flip-model>
- Direct2D pixel formats: <https://learn.microsoft.com/en-us/windows/win32/direct2d/supported-pixel-formats-and-alpha-modes>
- Per-Monitor V2 DPI: <https://learn.microsoft.com/en-us/windows/win32/hidpi/setting-the-default-dpi-awareness-for-a-process>
- Wheel-delta handling: <https://devblogs.microsoft.com/oldnewthing/20130123-00/?p=5473>
