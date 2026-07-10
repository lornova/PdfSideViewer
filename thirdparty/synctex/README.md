# SyncTeX parser (vendored)

Reference SyncTeX parser by Jérôme Laurens, used for forward/inverse search
between PDF positions and TeX sources.

- Upstream: https://github.com/jlaurens/synctex
- Tag: `2023.4(TeXLive)` — commit `c3641da45b41cd61f9e625fe722d0a8d54cedaa9` (2024-03-09)
- Parser version: 1.21
- Fetched: 2026-07-10
- Local modifications: none to upstream files. `synctex_msvc_compat.h` is OURS
  (force-included via /FI): it supplies the `ATTRIBUTE_FORMAT_PRINTF` macro
  that TeX Live's `w2c/c-auto.h` would normally provide under `_MSC_VER`.
- License: `LICENSE` in this directory (MIT-style permissive with a
  non-endorsement clause; GPLv3-compatible — the same files SumatraPDF, also
  GPLv3, vendors)

Unlike `vendor/` (untracked MuPDF tree), this directory IS tracked in git: the
files are small, license-compatible and required for every build.

Build notes: only `synctex_parser.c` and `synctex_parser_utils.c` are compiled
(by `app/PdfSideViewer.vcxproj`, with warnings disabled per-file); they need
zlib headers (taken from `vendor/mupdf/thirdparty/zlib`) and link against the
zlib objects inside MuPDF's `libthirdparty.lib`, plus the three `gz*.c` files
the app compiles itself (MuPDF's build omits them). `synctex_parser_local.h`
is unused (it requires `SYNCTEX_USE_LOCAL_HEADER`, a TeX Live-ism) and is kept
only for upstream fidelity.

To refresh: clone the tag above, copy the seven `synctex_*` files plus
`LICENSE`, update this README, and re-run the SyncTeX end-to-end tests.
