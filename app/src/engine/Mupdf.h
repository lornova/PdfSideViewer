#pragma once

// MuPDF is a C library; its headers do not provide extern "C" guards and are
// not clean under /W4 (unreferenced parameters in inline helpers, setjmp).
#pragma warning(push)
#pragma warning(disable : 4100 4611)
extern "C" {
#include <mupdf/fitz.h>
}
#pragma warning(pop)
