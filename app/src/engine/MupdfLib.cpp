#include "engine/MupdfLib.h"

#include "framework.h"

#include <mutex>

// fz_try is setjmp-based; see the note in Document.cpp.
#pragma warning(disable : 4611)

namespace {

SRWLOCK g_locks[FZ_LOCK_MAX] = {}; // SRWLOCK_INIT is zero-init
std::once_flag g_once;
fz_context* g_base = nullptr;
std::mutex g_cloneMutex;

} // namespace

void MupdfLib::Lock(void* /*user*/, int lock) {
    AcquireSRWLockExclusive(&g_locks[lock]);
}

void MupdfLib::Unlock(void* /*user*/, int lock) {
    ReleaseSRWLockExclusive(&g_locks[lock]);
}

fz_context* MupdfLib::BaseContext() {
    std::call_once(g_once, [] {
        static fz_locks_context locks = {nullptr, Lock, Unlock};
        fz_context* ctx = fz_new_context(nullptr, &locks, FZ_STORE_DEFAULT);
        if (!ctx)
            throw std::runtime_error("fz_new_context failed");
        fz_try(ctx) {
            fz_register_document_handlers(ctx);
        }
        fz_catch(ctx) {
            fz_drop_context(ctx);
            throw std::runtime_error("fz_register_document_handlers failed");
        }
        g_base = ctx;
    });
    if (!g_base)
        throw std::runtime_error("MuPDF initialization failed");
    return g_base;
}

fz_context* MupdfLib::CloneContext() {
    fz_context* base = BaseContext();
    std::lock_guard<std::mutex> guard(g_cloneMutex);
    fz_context* clone = fz_clone_context(base);
    if (!clone)
        throw std::runtime_error("fz_clone_context failed");
    return clone;
}
