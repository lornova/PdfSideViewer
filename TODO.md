# PdfSideViewer TODO list

## Bugs to be fixed

- Some toolbar buttons render differently on Windows 10 and on Windows 11: the glyphs come from
  the system icon font (Segoe MDL2 Assets), which is not guaranteed to be consistent across
  Windows versions, and possibly not even across minor releases. Baking our own images (see the
  toolbar icon review below) would remove the dependency.

- A regular expression with nested quantifiers (`(a+)+b`) runs in exponential time inside mujs,
  which has neither a step budget nor any way of being aborted, so the pane freezes: its worker
  also serves renders, and nothing short of closing the application recovers it (that much works,
  because `Document::Shutdown` detaches the wedged worker after two seconds and the session is
  still saved). A real fix needs either a step budget in the matcher or a search that runs
  somewhere killable. Reported upstream on 2026-08-06, both on the Artifex Bugzilla: a work budget
  in the mujs matcher ([bug 709616](https://bugs.ghostscript.com/show_bug.cgi?id=709616)) and an
  `fz_cookie` on the MuPDF search API, which would at least make a long search abortable
  ([bug 709617](https://bugs.ghostscript.com/show_bug.cgi?id=709617)). Until one of them lands,
  any local fix means patching `vendor\mupdf\thirdparty\mujs\regexp.c`, which is not tracked in
  git and would have to be re-applied at every MuPDF bump.

## Improvements

- Now that File ▸ New Window makes two live instances ordinary, two windows closing in sequence
  roll the settings back to what the survivor loaded at its own start. The documents are safe by
  construction (`m_fallback`), but the sync-point memory and the MRU lists are not, and they are
  list-shaped, so they could be MERGED instead: inside `AppSettings::Save`, under the writer lock
  it already takes, re-`Load` the file and union the three list fields (most-recent-first,
  case-insensitive dedup, capped) before serializing. It changes the stated "the lock does not
  merge" contract in `docs\DESIGN.md`, though not `Load`'s "no lock by design" symmetry: the read
  would be the writer's own, holding the lock. Not worth doing for the scalar options, and the
  language divergence between two live windows has no cheap answer at all.
- The Explorer verbs could get the same treatment as forward search: an `openprobe` round that
  prefers, in z-order, the first window whose target slot is EMPTY. With one window it is a
  no-op, so nothing changes for the common case. Deferred because, unlike forward search, the
  verbs have no semantically right target - only a convention - and "the window you were last
  in" is already a defensible one.
- At standard DPI (100% scaling), the toolbar buttons in the find bar overlay look bold and not
  very legible. The likely cause is in `util\GlyphIcons.cpp`: both paths draw WHITE text on a
  BLACK DIB with `ANTIALIASED_QUALITY` and then use the grey value as the alpha coverage, but
  GDI's grayscale antialiasing is tuned for dark-on-light, so the inverted render is
  systematically heavier - worst at 96 DPI, where a stroke is one pixel. Cheap levers: a gamma
  LUT in `CellStrip::Finish`, and passing a `glyphPx` smaller than `cellPx` (the call sites
  pass the same value, so the em box IS the cell and nothing breathes). Supersampling would
  defeat GDI hinting, which is what keeps 16-px MDL2 sharp. Verifiable only at REAL 96 DPI
  (this machine is at 168), and any fix touches the main toolbar too, so it belongs with the
  icon review below.
- Move the search UI from the find-bar overlay into the toolbar (decided on 2026-07-26): the
  overlay exists only because the toolbar can be hidden. Feasible but the heaviest item by far,
  and it needs three decisions BEFORE any code: what replaces the overlay's POSITION as the cue
  for which pane is being searched (the counter could name the slot); what replaces
  `IsWindowVisible(m_findBar)` as the "a search is live" predicate, and when highlights are
  then cleared; and what full screen does, since it forces one locked row that a band with an
  EDIT box does not fit. Note the stated motive only half holds: the toolbar stays hideable
  afterwards, so the condition becomes "Ctrl+F must un-hide it".
- Review the toolbar icons and the application icon. Two known snags: the Windows 10 / Windows
  11 glyph inconsistency above, and "Open Centre" (E8AE, a frame with two edge bars) reading
  close to "Three Panes" (E90C plus its mirror, a frame with two dividers) when the toolbar is
  in icon-only mode.
- Clean up the status bar.

## Release tasks (1.0)

- Submit the manifest to winget-pkgs.
- Microsoft Store distribution: MSIX package, plus packaged-run detection in the app.

## New features (post-1.0)

- Touchpad support.
- Dark reading mode.
- Large-document scalability (opening is O(n) in the page count, the layout hits float precision
  around 2^24 content px, zoom is capped at 8x).
- CJK support: Traditional Chinese (zh-TW) only, never Simplified; needs the outline-numbering
  parser extended to 第N章 chapter headings plus a visual font smoke test.
