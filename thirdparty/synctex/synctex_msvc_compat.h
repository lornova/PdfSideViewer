#pragma once

/* NOT an upstream file. Forced-included via /FI by PdfSideViewer.vcxproj so
   the upstream sources stay byte-identical.

   ATTRIBUTE_FORMAT_PRINTF: TeX Live's w2c/c-auto.h normally provides it;
   under _MSC_VER the parser references it and expects it to expand to
   nothing. */
#define ATTRIBUTE_FORMAT_PRINTF(stringIndex, firstToCheck)

/* The parser calls generic-text shlwapi functions (PathFindFileName,
   PathFindExtension) with char* buffers. Under the app's project-wide UNICODE
   defines those macros resolve to the ...W variants, which walk the narrow
   buffer as UTF-16 and silently break the .synctex name resolution (only a
   pointer-mismatch WARNING in C, and vendored files build with warnings
   off). These TUs must see the ANSI variants. */
#undef UNICODE
#undef _UNICODE
