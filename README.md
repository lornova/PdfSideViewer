# PdfSideViewer

A fast, fully native Windows 10/11 desktop viewer that shows **two PDF documents side by
side** (three, in the optional three-pane mode), with **synchronized scrolling** that actually
works: the pairing is a user-adjustable anchor (pre-scroll each pane, lock, done) and positions
are exchanged in *page units*, so documents with different page formats and different zoom
levels stay aligned page-for-page.

Pure Win32 + Direct2D, rendered by [MuPDF](https://mupdf.com): a single self-contained
executable that starts instantly.

## Features

- Two independent panes (three in the optional three-pane mode), each a full PDF viewer:
  continuous layout, tiled rendering crisp up to 800% zoom, fit-width / fit-page modes,
  per-monitor DPI awareness.
- **Page-by-page mode** (Ctrl+4): the view holds one page at a time. When the whole page fits,
  every key press or wheel notch flips a page; when zoomed in, input scrolls to the page edge
  first and only a further input flips (backward flips land at the bottom of the previous
  page, so reading upward stays continuous). Synced panes flip together.
- **Synchronized scrolling** (F7) with a delta anchor: align any page of one document with any
  page of the other and the offset is preserved; hold **Alt** to adjust one pane while keeping
  the lock. Optional **zoom sync** (Ctrl+F7).
- **Sync points** (WinMerge-style, Sync menu): pin whole-page pairs ("page 5 left = page 4
  right"). Between points the panes scroll page-for-page; where one document has extra pages
  the other shows empty **alignment gaps**, so both panes scroll (and page-flip) together 1:1
  (Sync ▸ Show Alignment Gaps, on by default; toggled off, the shorter side waits at the end
  of its section instead). Sync-point pages are flagged with an **anchor mark** next to the
  page and **ticks** along the right edge (solid = manual, faded = generated). Add a point at
  the current alignment (Shift+F7), review and remove them in a dialog, clear them all
  (Ctrl+Shift+F7), or **generate them from bookmarks**: numberings present in both documents
  become sync points ("1.2", "2.2.1", "A.1", or with a verbal prefix in Italian, English,
  German, French, Hungarian, Ukrainian, Romanian, Portuguese, Greek, Spanish, Polish,
  Dutch, Czech or Swedish
  such as "Chapter 3", "Kapitel 3", "Chapitre 3", "Fejezet 3", "Розділ 3", "Capitolul 3",
  "Capítulo 3", "Κεφάλαιο 3", "Rozdział 3", "Hoofdstuk 3", "Kapitola 3", "Avsnitt 3",
  "Appendice A"),
  lettered appendix sub-items
  ("A ...", "B ...") pair by their letter, and
  unnumbered bookmarks pair by title at the same outline depth, matching common section
  names across the same fourteen languages ("Indice" = "Contents" = "Inhaltsverzeichnis" =
  "Table des matières" = "Tartalomjegyzék" = "Зміст" = "Cuprins" = "Índice" =
  "Περιεχόμενα" = "Spis treści" = "Inhoudsopgave" = "Obsah" = "Innehåll"), the rest is
  skipped.
  Generated points survive auto-reload (re-derived from the fresh outline), and each pair's
  points are remembered across sessions: reopening the same two documents brings its manual
  points back and re-derives the generated ones.
- Full-document **text search** (Ctrl+F) with live highlighting and F3 navigation, plus
  **Match case**, **Match whole word** and **Regular expression** toggles in the find bar
  (remembered across sessions). Whole-word boundaries are Unicode-aware, so "perché" is a whole
  word too, and whole word can be combined with a pattern. Typing never moves the view: the
  highlights appear where you are, and F3 steps from the page you are on.
  In **regex** mode the pattern is run when you press Enter, not while you type, so a half-typed
  pattern is never executed; `^` and `$` match per line, a match does not cross a line break
  (write `\s` for one), a pattern that does not compile says so instead of reporting no matches,
  and results are capped at 10000 per document (shown as `≥10000`). Patterns with nested
  quantifiers such as `(a+)+b` can still take unbounded time on the pane that runs them.
- **Text selection** with the mouse (double-click = word), Ctrl+C copies Unicode text. Copy and
  the whole find family also live in the **Edit menu**.
- Clickable **links** (internal destinations and web URLs) and an **outline sidebar** (F9).
- **Auto-reload**: documents rewritten on disk (e.g. by a LaTeX build) reload in place,
  keeping position, zoom and sync.
- **SyncTeX** two-way sync with your TeX editor: Ctrl+click a spot in the PDF to jump to the
  source line (inverse search), and `-forward-search` jumps the viewer to a source line
  (see below).
- **PDF page labels**: the status bar, the go-to box and the scrollbar tooltip show the
  document's own page labels when they differ from the ordinal, e.g. `ix (9/314)` for
  roman-numbered front matter.
- **Go to page** (Ctrl+G) by number or by label ("ix", "A-3" - labels win on ambiguity), an
  editable page box in the toolbar row, and a page tooltip while dragging the scrollbar.
- Compact chrome: menu bar and toolbar share ONE row in a rebar (with a chevron overflow when
  narrow), plus the status bar (page and zoom per pane, with the sync state between the two
  panes in the two-pane layout and after the rightmost pane's cells in the three-pane one),
  full screen (F11 / Alt+Enter), a width-adjustable outline sidebar (drag the
  divider; double-click fits it to the widest bookmark, removing the horizontal scroll),
  recent files and recent sessions (two or three documents), UI in English, Italian, German,
  French, Hungarian, Ukrainian, Romanian, Portuguese, Greek, Spanish, Polish, Dutch, Czech and
  Swedish.
- **Pane headers**: an optional strip above each pane shows the open PDF's file name (or full
  path) and doubles as the active-pane cue - the focused pane's header carries an accent underline
  along the bottom edge, the other stays plain. Toggle the header and the name/path choice in the
  Options dialog; with the header off, the active pane is marked by a border instead.
- **Movable toolbars**, Internet Explorer style: untick "Lock the Toolbars" (View menu, or
  right-click the bar) and grippers appear on the menu, toolbar and page-box bands: drag to
  reorder or resize them, or wrap them onto extra rows. The right-click menu also carries
  IE's **text options** for the command toolbar: "Show text labels" (below the icons, the
  default), "Selective text on right" (labels beside the primary buttons only) or "No text
  labels". By default the bars are locked with the menu on its own row and the labeled
  toolbar plus page box on a second row; "Reset the Toolbar Layout" (next to the lock in
  both menus) returns to that arrangement. The toolbar covers the whole Sync menu too (add
  point, from bookmarks, points list, clear, alignment gaps, swap). The arrangement, the
  lock state and the text option are remembered.
- **Options dialog**: reopen the last session or start empty, defaults for new documents
  (scroll mode, zoom, sync locks), sync-point anchor marks and scrollbar ticks on/off,
  the pane header (off, or showing the file name or the full path),
  keeping the toolbar and/or the status bar visible in full screen, wheel-scroll lines
  override, the SyncTeX inverse-search command, and the Explorer context-menu integration.
  When full screen hides the toolbar, a small floating button in the top-right corner exits
  full screen.
- **Explorer context menu** (optional, off by default): "Open left/centre/right in PDF Side
  Viewer" on .pdf files, in that order, each with an icon highlighting the pane it targets,
  registered per-user under `SystemFileAssociations` so it can NEVER become
  the default PDF handler. They reuse the running window, and "Open centre" switches it to three
  panes. Moving the exe requires re-registering (re-tick the Options checkbox, or run
  `-register-shell`/`-unregister-shell` from a script).
- **Swap panes** (F8) exchanges the two documents including their view states; with three panes
  it rotates them instead (left moves to centre, centre to right, right wraps around to left)
  and Shift+F8 rotates the other way. Sync points survive either way, permuted along with the
  documents.
- **Three-pane mode** (optional; the app stays two-pane by default): View ▸ Three Panes
  adds a third document between the other two, and File ▸ Open Centre (Ctrl+Shift+M) switches to it
  on the spot. Each pane keeps its own splitter, page/zoom cell in the status bar and view state;
  going back to two panes closes the centre document and remembers it for the next switch. The
  app stays two-pane unless you ask otherwise: a launch that does not reopen the last session
  starts from the pane count chosen in Options ▸ Defaults, which is two. Sync
  points work across all three: "Sync Points from Bookmarks" keeps the headings every open
  document shares, so a third document can only narrow the map, never distort it. Made for
  comparing three editions or translations of the same text side by side.
- Drag & drop (drop two or three files at once to fill the panes; three of them turn the
  three-pane mode on), double-click an empty pane to open a file there, close a document with
  Ctrl+W (or the whole session with File ▸ Close Session, which also returns to the default pane
  count), command line
  (`PdfSideViewer.exe left.pdf right.pdf [centre.pdf]` — Beyond Compare's argument order, so the
  THIRD file is the centre one and a third argument turns the mode on), session restore
  (documents, positions, pane count, window).
- **Recent Sessions** (File menu) remembers the documents that were open together, two or three of
  them, and reopening one brings its layout back with it. Each remembered session also keeps its
  manual sync points.

## Keyboard

| Key | Action |
| --- | --- |
| Ctrl+O / Ctrl+Shift+O | Open document in the left / right pane |
| Ctrl+Shift+M | Open document in the centre pane (switches to three panes) |
| Ctrl+W | Close the focused pane's document |
| Tab | Switch pane (cycles through all of them) |
| F7 / Ctrl+F7 | Toggle scroll sync / zoom sync |
| Shift+F7 / Ctrl+Shift+F7 | Add a sync point at the current alignment / clear sync points |
| Alt + scroll | Adjust one pane while synced (re-anchors; with sync points the tweak is transient) |
| Ctrl+F, F3, Shift+F3 | Find, next match, previous match (Esc closes the find bar) |
| Enter, F3 (regex mode) | The first one runs the pattern, the next one steps through the matches |
| Ctrl+C | Copy selected text |
| F9 | Outline (bookmarks) sidebar |
| Ctrl+wheel, Ctrl +/− | Zoom (anchored at the cursor) |
| Ctrl+0 / Ctrl+1 | 100% zoom |
| Ctrl+2 / Ctrl+3 | Fit width / fit page |
| Ctrl+4 / Ctrl+5 | Continuous / page-by-page scrolling |
| Ctrl+G | Go to page (number or label) |
| F8 / Shift+F8 | Swap the two panes (with three, rotate: left → centre → right; Shift+F8 rotates back) |
| F11 / Alt+Enter | Full screen (Esc exits) |
| Ctrl+click | SyncTeX inverse search (open the .tex source at that spot) |
| Space, Shift+Space, PgUp/PgDn, Home/End, arrows | Navigate |

## SyncTeX (VS Code + LaTeX Workshop)

Compile with SyncTeX enabled (`pdflatex -synctex=1`, the LaTeX Workshop default). The viewer
reads the `.synctex(.gz)` next to each PDF.

**Inverse search** (PDF → editor): Ctrl+click a position in any pane. By default the viewer
opens VS Code at that line through the `vscode://file/...` protocol. The launch template lives
in `settings.ini` under `[synctex] inverse` (`%f` = file, `%l` = line; a value containing
`://` is treated as a URL, anything else as a command line), e.g. for another editor:
`inverse=texstudio --line %l "%f"`.

Inverse search **requires the viewer to run without administrator privileges**, and says so
instead of launching anything: that template comes from a file the same user's ordinary
processes can write, so honouring it from an elevated process would run their command at high
integrity. The refusal also applies when the app cannot determine its own elevation, which is
the safe answer to an unknown. PDF Side Viewer is a per-user application and never needs
elevation.

**Forward search** (editor → PDF): configure LaTeX Workshop to use PdfSideViewer as the
external viewer in VS Code's `settings.json`:

```jsonc
"latex-workshop.view.pdf.viewer": "external",
"latex-workshop.view.pdf.external.viewer.command":
    "C:\\path\\to\\PdfSideViewer.exe",
"latex-workshop.view.pdf.external.viewer.args": ["%PDF%"],
"latex-workshop.view.pdf.external.synctex.command":
    "C:\\path\\to\\PdfSideViewer.exe",
"latex-workshop.view.pdf.external.synctex.args":
    ["-forward-search", "%TEX%", "%LINE%", "%PDF%"]
```

`PdfSideViewer.exe -forward-search file.tex 123 file.pdf` hands the request to the running
instance (the pane holding that PDF scrolls there and flashes the target green; the PDF is
opened if needed) or starts the viewer if none is running. This works per pane: each pane
queries its own `.synctex.gz`, so a bilingual it/en pair (or trio) gets independent forward
and inverse search.

## Building

Requirements: Visual Studio 2022 with the **Desktop development with C++** workload (MSVC
v143, Windows 10/11 SDK). MuPDF (official 1.28.x source release, `thirdparty/` included) is
**not tracked in this repository**; a script downloads and unpacks it into `vendor/mupdf`.

1. Fetch and build MuPDF once per platform (x64 and/or ARM64):

   ```powershell
   powershell scripts\get-mupdf.ps1 -Build                      # x64
   powershell scripts\get-mupdf.ps1 -Build -Platforms x64,ARM64 # both
   ```

   (equivalent to building `vendor\mupdf\platform\win32\mupdf.sln`, targets
   `libmupdf;libthirdparty;libresources`, with `-p:PlatformToolset=v143`;
   `libresources` only exists in Release and is shared by Debug builds.)

2. Build the app:

   ```powershell
   msbuild PdfSideViewer.sln -p:Configuration=Release -p:Platform=x64 -m
   ```

The result is a single self-contained `build\<platform>\Release\PdfSideViewer.exe`.
`scripts\make-release.ps1` packages it into a portable zip; `scripts\make-installer.ps1`
(requires [Inno Setup 6](https://jrsoftware.org/isinfo.php), e.g.
`winget install JRSoftware.InnoSetup`) builds `PdfSideViewer-Setup-x64.exe`, a per-user
installer (no admin, silent-install capable) with optional Explorer-menu integration.

Design notes and architecture are documented in [docs/DESIGN.md](docs/DESIGN.md).

## License

PdfSideViewer is free software, released under the **GNU General Public License v3** (see
[LICENSE](LICENSE)). It statically links [MuPDF](https://mupdf.com), Copyright © Artifex
Software, Inc., licensed under the GNU Affero General Public License v3; per GPLv3 §13 the
combined work is distributed with both licenses satisfied and full corresponding source
available in this repository.
