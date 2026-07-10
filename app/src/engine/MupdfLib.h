#pragma once

#include "engine/Mupdf.h"

// Process-wide MuPDF bootstrap. MuPDF's threading rules require a
// fz_locks_context at base-context creation time and one context per thread
// (clones share allocator, store and glyph cache). Document handlers are
// registered once on the base context; clones share the handler registry.
class MupdfLib {
public:
    // Thread-safe; creates the base context on first use.
    // Throws std::runtime_error on failure.
    static fz_context* BaseContext();

    // fz_clone_context counts as "using" the base context, so cloning is
    // serialized here (no two threads may use one context simultaneously).
    static fz_context* CloneContext();

private:
    static void Lock(void* user, int lock);
    static void Unlock(void* user, int lock);
};
