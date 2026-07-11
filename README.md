# PdfSideViewer

A fast, fully native Windows 10/11 desktop viewer that shows **two PDF documents side by
side**, with **synchronized scrolling** that actually works: the pairing is a user-adjustable
anchor (pre-scroll each pane, lock, done) and positions are exchanged in *page units*, so
documents with different page formats and different zoom levels stay aligned page-for-page.

Pure Win32 + Direct2D, rendered by [MuPDF](https://mupdf.com). No .NET, no Electron, no
cross-platform toolkit: a single small executable that starts instantly.

## Features

- Two independent panes, each a full PDF viewer: continuous layout, tiled rendering crisp up
  to 800% zoom, fit-width / fit-page modes, per-monitor DPI awareness.
- **Page-by-page mode** (Ctrl+4): the view holds one page at a time. When the whole page fits,
  every key press or wheel notch flips a page; when zoomed in, input scrolls to the page edge
  first and only a further input flips (backward flips land at the bottom of the previous
  page, so reading upward stays continuous). Synced panes flip together.
- **Synchronized scrolling** (F7) with a delta anchor: align any page of one document with any
  page of the other and the offset is preserved; hold **Alt** to adjust one pane while keeping
  the lock. Optional **zoom sync** (Ctrl+F7).
- Full-document **text search** (Ctrl+F) with live highlighting and F3 navigation.
- **Text selection** with the mouse (double-click = word), Ctrl+C copies Unicode text.
- Clickable **links** (internal destinations and web URLs) and an **outline sidebar** (F9).
- **Auto-reload**: documents rewritten on disk (e.g. by a LaTeX build) reload in place,
  keeping position, zoom and sync.
- **SyncTeX** two-way sync with your TeX editor: Ctrl+click a spot in the PDF to jump to the
  source line (inverse search), and `-forward-search` jumps the viewer to a source line
  (see below).
- Menu bar, toolbar and status bar (page / zoom per pane, sync state), full screen
  (F11 / Alt+Enter), recent files and recent left+right pairs, English/Italian UI.
- Drag & drop (drop two files to fill both panes), double-click an empty pane to open a file
  there, close a document with Ctrl+W, command line (`PdfSideViewer.exe left.pdf right.pdf`),
  session restore (documents, positions, window).

## Keyboard

| Key | Action |
|---|---|
| Ctrl+O / Ctrl+Shift+O | Open document in the left / right pane |
| Ctrl+W | Close the focused pane's document |
| Tab | Switch pane |
| F7 / Ctrl+F7 | Toggle scroll sync / zoom sync |
| Alt + scroll | Adjust one pane while synced (re-anchors) |
| Ctrl+F, F3, Shift+F3 | Find, next match, previous match |
| Ctrl+C | Copy selected text |
| F9 | Outline (bookmarks) sidebar |
| Ctrl+wheel, Ctrl +/− | Zoom (anchored at the cursor) |
| Ctrl+0 / Ctrl+1 | 100% zoom |
| Ctrl+2 / Ctrl+3 | Fit width / fit page |
| Ctrl+4 | Toggle continuous / page-by-page scrolling |
| F11 / Alt+Enter | Full screen (Esc exits) |
| Ctrl+click | SyncTeX inverse search (open the .tex source at that spot) |
| Space, Shift+Space, PgUp/PgDn, Home/End, arrows | Navigate |

## SyncTeX (VS Code + LaTeX Workshop)

Compile with SyncTeX enabled (`pdflatex -synctex=1`, the LaTeX Workshop default). The viewer
reads the `.synctex(.gz)` next to each PDF.

**Inverse search** (PDF → editor): Ctrl+click a position in either pane. By default the viewer
opens VS Code at that line through the `vscode://file/...` protocol. The launch template lives
in `settings.ini` under `[synctex] inverse` (`%f` = file, `%l` = line; a value containing
`://` is treated as a URL, anything else as a command line), e.g. for another editor:
`inverse=texstudio --line %l "%f"`.

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
opened if needed) or starts the viewer if none is running. With two panes this works per
document: each pane queries its own `.synctex.gz`, so a bilingual it/en pair gets independent
forward and inverse search.

## Building

Requirements: Visual Studio 2022 with the **Desktop development with C++** workload (MSVC
v143, Windows 10/11 SDK). MuPDF (official 1.28.x source release, `thirdparty/` included) is
**not tracked in this repository**; a script downloads and unpacks it into `vendor/mupdf`.

1. Fetch and build MuPDF once per platform (x64 and/or ARM64):

   ```
   powershell scripts\get-mupdf.ps1 -Build                      # x64
   powershell scripts\get-mupdf.ps1 -Build -Platforms x64,ARM64 # both
   ```

   (equivalent to building `vendor\mupdf\platform\win32\mupdf.sln`, targets
   `libmupdf;libthirdparty;libresources`, with `-p:PlatformToolset=v143`;
   `libresources` only exists in Release and is shared by Debug builds.)

2. Build the app:

   ```
   msbuild PdfSideViewer.sln -p:Configuration=Release -p:Platform=x64 -m
   ```

The result is a single self-contained `build\<platform>\Release\PdfSideViewer.exe`.
`scripts\make-release.ps1` packages it into a portable zip.

Design notes and architecture are documented in [docs/DESIGN.md](docs/DESIGN.md).

## License

PdfSideViewer is free software, released under the **GNU General Public License v3** (see
[LICENSE](LICENSE)). It statically links [MuPDF](https://mupdf.com), Copyright © Artifex
Software, Inc., licensed under the GNU Affero General Public License v3; per GPLv3 §13 the
combined work is distributed with both licenses satisfied and full corresponding source
available in this repository.
