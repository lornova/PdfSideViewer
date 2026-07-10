#pragma once

#include "engine/Document.h" // Document::RectPt
#include "framework.h"

#include <optional>
#include <vector>

struct synctex_scanner_t; // opaque: only SyncTex.cpp includes synctex_parser.h

// Wrapper around the vendored SyncTeX parser (thirdparty/synctex). Plain C
// file parsing: no fz_context, no Direct2D — deliberately UI-thread-callable
// (the "UI thread never calls MuPDF" rule does not apply here). Queries are
// gesture-rare and the one-off parse of a big .synctex(.gz) (~100 ms worst
// case) is accepted for now; move to a background thread posting a result
// message if it ever stutters.
//
// Every coordinate at this boundary is PDF points with a TOP-LEFT page
// origin, exactly the space PaneWindow's hit tests and overlays use (SyncTeX
// and MuPDF share the top-down convention; no flip anywhere).
class SyncTexIndex {
public:
    struct InverseHit {
        std::wstring texPath; // absolute, backslash separators
        int line = 0;         // 1-based
        int column = -1;      // -1 = unknown
    };

    SyncTexIndex() = default;
    ~SyncTexIndex() { Reset(); }
    SyncTexIndex(const SyncTexIndex&) = delete;
    SyncTexIndex& operator=(const SyncTexIndex&) = delete;

    // Lazy load of the .synctex(.gz) sitting next to pdfPath (covers both
    // -synctex=1 and -synctex=-1). Success is cached; FAILURE IS NOT: a
    // failed open costs microseconds and the file may exist after the next
    // build. Returns whether a scanner is available.
    bool EnsureLoaded(const std::wstring& pdfPath);
    void Reset(); // OpenDocument's reset list calls this (covers auto-reload)
    bool IsLoaded() const { return m_scanner != nullptr; }

    // Inverse search: pageIndex is 0-based, x/y in PDF points top-down (the
    // output space of PaneWindow::PagePointAt).
    std::optional<InverseHit> SourceAt(int pageIndex, float xPt, float yPt);

    // Forward search: line is 1-based. Returns every box on the FIRST
    // result's page (one source line can map to several hboxes);
    // outPageIndex receives the 0-based page, -1 on miss.
    std::vector<Document::RectPt> ForwardBoxes(const std::wstring& texPath, int line,
                                               int& outPageIndex);

private:
    synctex_scanner_t* m_scanner = nullptr;
    std::wstring m_pdfPath; // what the scanner was built from
};
