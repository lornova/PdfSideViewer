#pragma once

#include "engine/Mupdf.h"
#include "framework.h"

#include <climits>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

// Posted to the owning pane's HWND; lParam carries a heap-allocated result the
// receiver must take ownership of (wrap in unique_ptr immediately). Keep the
// range contiguous: the WM_DESTROY drain frees them by range.
constexpr UINT WM_PSV_DOC_OPENED = WM_APP + 1;    // lParam: Document::OpenResult*
constexpr UINT WM_PSV_PAGE_RENDERED = WM_APP + 2; // lParam: Document::RenderResult*
constexpr UINT WM_PSV_TEXT_PAGE = WM_APP + 3;     // lParam: Document::TextPageResult*
constexpr UINT WM_PSV_LINKS = WM_APP + 4;         // lParam: Document::LinksResult*
constexpr UINT WM_PSV_SEARCH = WM_APP + 5;        // lParam: Document::SearchResult*
constexpr UINT WM_PSV_FIRST = WM_PSV_DOC_OPENED;
constexpr UINT WM_PSV_LAST = WM_PSV_SEARCH;

// One PDF document with its dedicated worker thread. Per MuPDF's threading
// rules every call that touches the fz_document happens on the worker, which
// owns a cloned fz_context; the UI thread never calls into MuPDF. Pages are
// interpreted once into cached display lists, then rasterized per request
// with an abortable fz_cookie.
class Document {
public:
    struct OutlineItem {
        int depth = 0; // 0 = top level; children follow their parent
        std::wstring title;
        int targetPage = -1;
        float targetY = 0; // within-page y in points from the top
    };
    struct OpenResult {
        std::wstring path;       // echo of the request (informational)
        uint64_t generation = 0; // matches OpenAsync's return; discards stale results
        bool ok = false;
        std::wstring error;
        std::vector<D2D1_SIZE_F> pageSizesPt;  // page sizes in PDF points (1/72")
        std::vector<OutlineItem> outline;      // flattened bookmark tree (may be empty)
    };
    struct RenderResult {
        int pageIndex = 0;
        float scale = 0; // points -> device pixels scale actually rendered
        int res = 0;     // 0 = whole page; else tile of a 2^res x 2^res grid
        int row = 0;
        int col = 0;
        uint64_t requestId = 0;
        bool ok = false; // false: render failed; pixels is null, pane must unlatch
        int width = 0;
        int height = 0;
        int stride = 0;
        std::unique_ptr<uint8_t[]> pixels; // BGRA, opaque white background
    };

    struct RectPt { // axis-aligned box in page points, top-left origin
        float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    };
    struct TextChar {
        uint32_t codepoint = 0;
        RectPt box;
    };
    struct TextLine {
        std::vector<TextChar> chars;
        RectPt box;
    };
    // UI-side text model: hit-testing, selection and copy run on this plain
    // copy so the UI thread never calls into MuPDF.
    struct TextPageResult {
        int pageIndex = 0;
        std::vector<TextLine> lines; // reading order (blocks flattened)
    };
    struct LinkInfo {
        RectPt box;
        std::wstring uri;    // external URL (empty for internal links)
        int targetPage = -1; // internal destination page (-1 = external)
        float targetY = 0;   // within-page y in points (0 = page top)
    };
    struct LinksResult {
        int pageIndex = 0;
        std::vector<LinkInfo> links;
    };
    struct SearchMatch {
        int pageIndex = 0;
        std::vector<RectPt> rects; // one logical hit may span several boxes
    };
    struct SearchResult {
        uint64_t searchId = 0;
        bool done = false;    // whole document scanned
        int pagesScanned = 0; // cumulative
        std::vector<SearchMatch> matches; // this chunk's hits, page order
    };

    Document() = default;
    ~Document();

    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;

    void SetNotificationTarget(HWND hwnd) { m_notify = hwnd; }

    // Supersedes every queued job and aborts a render in flight. Returns the
    // open generation echoed by the matching OpenResult: results keyed by path
    // alone cannot distinguish a stale open of the same file from a re-open.
    uint64_t OpenAsync(std::wstring path);

    // Supersedes any queued/running render of the same (page, res, row, col).
    // urgent jobs (visible content) jump the queue; previews/prefetch do not.
    void RequestRender(int pageIndex, float scale, uint64_t requestId, int res, int row, int col,
                       bool urgent);

    // Drops a queued render of (page, res, row, col) and aborts it if running:
    // the pane evicted the tile, so the result would be discarded anyway.
    void CancelRender(int pageIndex, int res, int row, int col);

    // Text model / links extraction (always posts a result, possibly empty).
    void RequestTextPage(int pageIndex, bool urgent);
    void RequestLinks(int pageIndex);

    // Whole-document search, processed in small chunks interleaved with
    // renders; a new StartSearch supersedes the previous one, results carry
    // the searchId so stale batches are discarded pane-side.
    void StartSearch(std::wstring needle, uint64_t searchId);
    void CancelSearch();

    // Queued renders for pages outside [first,last] are dropped (fast scroll).
    void SetWantedRange(int first, int last);

    void Shutdown(); // idempotent; joins the worker

private:
    struct Job {
        enum class Type { Open, Render, TextPage, Links, Search } type = Type::Open;
        std::wstring path;
        uint64_t generation = 0; // Open jobs only
        int pageIndex = 0;
        float scale = 0;
        int res = 0;
        int row = 0;
        int col = 0;
        uint64_t requestId = 0;
        std::string needleUtf8; // Search jobs only
        uint64_t searchId = 0;
    };

    void EnsureWorker();
    void WorkerMain();
    void WorkerOpen(fz_context* ctx, const Job& job);
    void WorkerRender(fz_context* ctx, const Job& job);
    void WorkerTextPage(fz_context* ctx, const Job& job);
    void WorkerLinks(fz_context* ctx, const Job& job);
    void WorkerSearch(fz_context* ctx, const Job& job);
    fz_display_list* AcquireDisplayList(fz_context* ctx, int pageIndex); // worker only
    void DropDisplayLists(fz_context* ctx);                             // worker only

    HWND m_notify = nullptr;
    std::thread m_worker;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<Job> m_jobs;              // guarded by m_mutex
    bool m_quit = false;                 // guarded by m_mutex
    fz_cookie* m_activeCookie = nullptr; // guarded by m_mutex; points into worker stack
    int m_activePage = -1;               // guarded by m_mutex (with res/row/col below)
    int m_activeRes = -1;
    int m_activeRow = -1;
    int m_activeCol = -1;
    int m_wantedFirst = INT_MIN;         // guarded by m_mutex (pair must never tear)
    int m_wantedLast = INT_MAX;          // guarded by m_mutex
    uint64_t m_openGeneration = 0;       // guarded by m_mutex
    uint64_t m_currentSearchId = 0;      // guarded by m_mutex; 0 = no search

    // Worker-thread-only state (never touched by the UI thread):
    fz_document* m_doc = nullptr;
    std::map<int, fz_display_list*> m_lists;
    int m_pageCount = 0;
};
