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
- **Synchronized scrolling** (F7) with a delta anchor: align any page of one document with any
  page of the other and the offset is preserved; hold **Alt** to adjust one pane while keeping
  the lock. Optional **zoom sync** (Ctrl+F7).
- Full-document **text search** (Ctrl+F) with live highlighting and F3 navigation.
- **Text selection** with the mouse (double-click = word), Ctrl+C copies Unicode text.
- Clickable **links** (internal destinations and web URLs) and an **outline sidebar** (F9).
- Drag & drop (drop two files to fill both panes), command line
  (`PdfSideViewer.exe left.pdf right.pdf`), session restore (documents, positions, window).

## Keyboard

| Key | Action |
|---|---|
| Ctrl+O / Ctrl+Shift+O | Open document in the left / right pane |
| Tab | Switch pane |
| F7 / Ctrl+F7 | Toggle scroll sync / zoom sync |
| Alt + scroll | Adjust one pane while synced (re-anchors) |
| Ctrl+F, F3, Shift+F3 | Find, next match, previous match |
| Ctrl+C | Copy selected text |
| F9 | Outline (bookmarks) sidebar |
| Ctrl+wheel, Ctrl +/− | Zoom (anchored at the cursor) |
| Ctrl+0 / Ctrl+1 | 100% zoom |
| Ctrl+2 / Ctrl+3 | Fit width / fit page |
| Space, Shift+Space, PgUp/PgDn, Home/End, arrows | Navigate |

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
