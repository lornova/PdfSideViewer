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

- The "Add Sync Point Here" toolbar button should appear pressed when the pages currently shown
  in the panes are all synced with each other.
- At standard DPI (100% scaling), the toolbar buttons in the find bar overlay look bold and not
  very legible.
- In full-screen mode the toolbars are always locked, so the toolbar context menu should show
  only the three text options (0 = no text labels, 1 = show text labels below the icons,
  2 = selective text on right), hiding the toolbar toggle and the lock/reset entries.
- Add two radio buttons to the toolbar to switch between the two- and three-pane modes
  (mirroring the View ▸ Two/Three Panes radio pair).
- Move the search UI from the find-bar overlay into the toolbar (decided on 2026-07-26): the
  overlay exists only because the toolbar can be hidden.
- When one of the three Open commands targets a pane that already holds a document, the Open
  dialog should start in that document's folder.
- Review the toolbar icons and the application icon.
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
