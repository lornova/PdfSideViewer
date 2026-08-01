#include "engine/Document.h"

#include "engine/MupdfLib.h"
#include "util/TextClass.h"

// fz_try is setjmp-based; its longjmp only ever jumps forward within the same
// fz_try/fz_catch region, so C++ destructors in enclosing scopes still run.
// We keep destructible objects out of the fz_try blocks themselves.
#pragma warning(disable : 4611)

namespace {

constexpr size_t kMaxCachedDisplayLists = 8;

std::string ToUtf8(const std::wstring& s) {
    if (s.empty())
        return {};
    const int len = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr,
                                        0, nullptr, nullptr);
    std::string out(static_cast<size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), out.data(), len, nullptr,
                        nullptr);
    return out;
}

std::wstring FromUtf8(const char* s) {
    if (!s || !*s)
        return {};
    const int len = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    if (len <= 1)
        return {};
    std::wstring out(static_cast<size_t>(len) - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s, -1, out.data(), len);
    return out;
}

// Transfers ownership to the window's message queue on success.
template <typename T>
void PostOrDelete(HWND hwnd, UINT msg, std::unique_ptr<T> payload) {
    if (hwnd && PostMessageW(hwnd, msg, 0, reinterpret_cast<LPARAM>(payload.get())))
        payload.release();
}

// ------------------------------------------------------ whole-word filtering
// MuPDF 1.28 has no whole-word search flag, and the regex route is unusable
// here: mujs classifies word characters with an ASCII-only iswordchar
// (thirdparty/mujs/regexp.c), so "\bperché\b" matches nothing at all - in an
// application whose UI ships in fourteen languages that is a broken feature,
// not a limitation. The boundary is therefore decided below, on the very stext
// page the matcher ran on.

struct QuadBox {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};

QuadBox BoxOf(const fz_quad& q) {
    QuadBox b;
    b.x0 = std::min({q.ul.x, q.ur.x, q.ll.x, q.lr.x});
    b.x1 = std::max({q.ul.x, q.ur.x, q.ll.x, q.lr.x});
    b.y0 = std::min({q.ul.y, q.ur.y, q.ll.y, q.lr.y});
    b.y1 = std::max({q.ul.y, q.ur.y, q.ll.y, q.lr.y});
    return b;
}

// fz_stext_char::c and fz_chartorune both hand out an int that is -1 for an
// unmappable glyph; only a real codepoint reaches the Unicode tables.
char32_t RuneOf(int c) {
    return c > 0 ? static_cast<char32_t>(c) : 0;
}

// The text line whose box contains the hit quad's centre. Structure blocks are
// walked too: a tagged PDF nests its text inside them, and missing that text
// would silently turn the boundary test into a coin toss.
fz_stext_line* LineOfQuad(fz_stext_block* first, const fz_quad& hit) {
    const QuadBox box = BoxOf(hit);
    const float hx = (box.x0 + box.x1) / 2;
    const float hy = (box.y0 + box.y1) / 2;
    for (fz_stext_block* block = first; block; block = block->next) {
        if (block->type == FZ_STEXT_BLOCK_TEXT) {
            for (fz_stext_line* line = block->u.t.first_line; line; line = line->next) {
                if (hx >= line->bbox.x0 && hx <= line->bbox.x1 && hy >= line->bbox.y0 &&
                    hy <= line->bbox.y1)
                    return line;
            }
        } else if (block->type == FZ_STEXT_BLOCK_STRUCT && block->u.s.down) {
            if (fz_stext_line* line = LineOfQuad(block->u.s.down->first_block, hit))
                return line;
        }
    }
    return nullptr;
}

// The characters flanking the run a hit quad covers. Geometry is used ONLY to
// locate the run; "before" and "after" then come from the line's character
// list, which is in reading order. That is what a word boundary asks for, and
// it keeps bidi runs and vertical writing (fz_stext_line::wmode) correct
// without a special case: there, logical and geometric order disagree.
struct RunNeighbours {
    bool located = false;
    fz_stext_char* before = nullptr; // located with null = the run starts the line
    fz_stext_char* after = nullptr;  // located with null = the run ends the line
};

RunNeighbours NeighboursInLine(fz_stext_line* line, const fz_quad& hit) {
    RunNeighbours out;
    const QuadBox box = BoxOf(hit);
    fz_stext_char* prev = nullptr;
    for (fz_stext_char* ch = line->first_char; ch; ch = ch->next) {
        const QuadBox cb = BoxOf(ch->quad);
        const float cx = (cb.x0 + cb.x1) / 2;
        const float cy = (cb.y0 + cb.y1) / 2;
        if (cx >= box.x0 && cx <= box.x1 && cy >= box.y0 && cy <= box.y1) {
            if (!out.located) {
                out.located = true;
                out.before = prev;
            }
            out.after = ch->next; // the last covered character wins
        }
        prev = ch;
    }
    return out;
}

// Which sides of the needle can carry a boundary at all. Searching "(a", "3.14"
// or "--" with whole word on must still find them, exactly as \b behaves: a
// side whose own edge character is not a word character stays unconstrained.
struct NeedleEdges {
    bool leading = false;
    bool trailing = false;
};

NeedleEdges EdgesOf(const std::string& needleUtf8) {
    NeedleEdges edges;
    if (needleUtf8.empty())
        return edges;
    int last = 0;
    bool first = true;
    for (const char* p = needleUtf8.c_str(); *p;) {
        int rune = 0;
        p += fz_chartorune(&rune, p);
        if (first) {
            edges.leading = IsWordChar(RuneOf(rune));
            first = false;
        }
        last = rune;
    }
    edges.trailing = IsWordChar(RuneOf(last));
    return edges;
}

// A hit survives when neither constrained side touches another word character.
// A run the geometry cannot locate is KEPT: an unclassifiable hit shown is a
// visible, correctable false positive, while one dropped is a match that
// silently does not exist.
bool HitIsWholeWord(fz_stext_page* stext, const NeedleEdges& edges, const fz_quad& firstQuad,
                    const fz_quad& lastQuad) {
    if (!stext)
        return true;
    if (edges.leading) {
        if (fz_stext_line* line = LineOfQuad(stext->first_block, firstQuad)) {
            const RunNeighbours n = NeighboursInLine(line, firstQuad);
            if (n.located && n.before && IsWordChar(RuneOf(n.before->c)))
                return false;
        }
    }
    if (edges.trailing) {
        if (fz_stext_line* line = LineOfQuad(stext->first_block, lastQuad)) {
            const RunNeighbours n = NeighboursInLine(line, lastQuad);
            if (n.located && n.after && IsWordChar(RuneOf(n.after->c)))
                return false;
        }
    }
    return true;
}

} // namespace

Document::~Document() {
    Shutdown();
}

void Document::EnsureWorker() {
    if (!m_worker.joinable())
        m_worker = std::thread(&Document::WorkerMain, this);
}

uint64_t Document::OpenAsync(std::wstring path) {
    EnsureWorker();
    uint64_t generation = 0;
    {
        std::lock_guard lock(m_mutex);
        m_jobs.clear(); // a new document supersedes everything queued
        if (m_activeCookie)
            m_activeCookie->abort = 1;
        m_wantedFirst = INT_MIN;
        m_wantedLast = INT_MAX;
        generation = ++m_openGeneration;
        Job job;
        job.type = Job::Type::Open;
        job.path = std::move(path);
        job.generation = generation;
        m_jobs.push_back(std::move(job));
    }
    m_cv.notify_one();
    return generation;
}

void Document::CloseAsync() {
    EnsureWorker();
    {
        std::lock_guard lock(m_mutex);
        m_jobs.clear(); // closing supersedes everything queued
        if (m_activeCookie)
            m_activeCookie->abort = 1;
        m_wantedFirst = INT_MIN;
        m_wantedLast = INT_MAX;
        // An Open already RUNNING still posts its result; bumping the
        // generation (pane-side m_openGen resets to 0 on close) keeps that
        // stale success from resurrecting the document just closed.
        ++m_openGeneration;
        m_currentSearchId = 0;
        Job job;
        job.type = Job::Type::Close;
        m_jobs.push_back(std::move(job));
    }
    m_cv.notify_one();
}

void Document::RequestRender(int pageIndex, float scale, uint64_t requestId, int res, int row,
                             int col, bool urgent) {
    EnsureWorker();
    {
        std::lock_guard lock(m_mutex);
        std::erase_if(m_jobs, [&](const Job& j) {
            return j.type == Job::Type::Render && j.pageIndex == pageIndex && j.res == res &&
                   j.row == row && j.col == col;
        });
        if (m_activePage == pageIndex && m_activeRes == res && m_activeRow == row &&
            m_activeCol == col && m_activeCookie)
            m_activeCookie->abort = 1; // superseded mid-render
        Job job;
        job.type = Job::Type::Render;
        job.pageIndex = pageIndex;
        job.scale = scale;
        job.res = res;
        job.row = row;
        job.col = col;
        job.requestId = requestId;
        // Visible work jumps the queue (most recent first); previews and
        // prefetch go to the back. OpenAsync clears the queue, so an urgent
        // render can never overtake a pending Open.
        if (urgent)
            m_jobs.push_front(std::move(job));
        else
            m_jobs.push_back(std::move(job));
    }
    m_cv.notify_one();
}

void Document::CancelRender(int pageIndex, int res, int row, int col) {
    std::lock_guard lock(m_mutex);
    std::erase_if(m_jobs, [&](const Job& j) {
        return j.type == Job::Type::Render && j.pageIndex == pageIndex && j.res == res &&
               j.row == row && j.col == col;
    });
    if (m_activePage == pageIndex && m_activeRes == res && m_activeRow == row &&
        m_activeCol == col && m_activeCookie)
        m_activeCookie->abort = 1;
}

void Document::SetWantedRange(int first, int last) {
    std::lock_guard lock(m_mutex);
    m_wantedFirst = first;
    m_wantedLast = last;
}

void Document::RequestTextPage(int pageIndex, bool urgent) {
    EnsureWorker();
    {
        std::lock_guard lock(m_mutex);
        std::erase_if(m_jobs, [pageIndex](const Job& j) {
            return j.type == Job::Type::TextPage && j.pageIndex == pageIndex;
        });
        Job job;
        job.type = Job::Type::TextPage;
        job.pageIndex = pageIndex;
        if (urgent)
            m_jobs.push_front(std::move(job));
        else
            m_jobs.push_back(std::move(job));
    }
    m_cv.notify_one();
}

void Document::RequestLinks(int pageIndex) {
    EnsureWorker();
    {
        std::lock_guard lock(m_mutex);
        std::erase_if(m_jobs, [pageIndex](const Job& j) {
            return j.type == Job::Type::Links && j.pageIndex == pageIndex;
        });
        Job job;
        job.type = Job::Type::Links;
        job.pageIndex = pageIndex;
        m_jobs.push_back(std::move(job));
    }
    m_cv.notify_one();
}

void Document::StartSearch(std::wstring needle, SearchOptions options, uint64_t searchId) {
    EnsureWorker();
    {
        std::lock_guard lock(m_mutex);
        std::erase_if(m_jobs, [](const Job& j) { return j.type == Job::Type::Search; });
        m_currentSearchId = searchId;
        Job job;
        job.type = Job::Type::Search;
        job.needleUtf8 = ToUtf8(needle);
        job.searchOptions = options;
        job.searchId = searchId;
        job.pageIndex = 0;
        m_jobs.push_back(std::move(job));
    }
    m_cv.notify_one();
}

void Document::CancelSearch() {
    std::lock_guard lock(m_mutex);
    m_currentSearchId = 0;
    std::erase_if(m_jobs, [](const Job& j) { return j.type == Job::Type::Search; });
}

void Document::Shutdown() {
    {
        std::lock_guard lock(m_mutex);
        m_quit = true;
        m_jobs.clear();
        if (m_activeCookie)
            m_activeCookie->abort = 1;
    }
    m_cv.notify_one();
    if (m_worker.joinable())
        m_worker.join();
}

void Document::WorkerMain() {
    fz_context* ctx = nullptr;
    try {
        ctx = MupdfLib::CloneContext();
    } catch (const std::exception&) {
        ctx = nullptr; // degraded: retried per job below, errors reach the pane
    }

    for (;;) {
        Job job;
        {
            std::unique_lock lock(m_mutex);
            m_cv.wait(lock, [&] { return m_quit || !m_jobs.empty(); });
            if (m_quit)
                break;
            job = std::move(m_jobs.front());
            m_jobs.pop_front();
            if (job.type == Job::Type::Render &&
                (job.pageIndex < m_wantedFirst || job.pageIndex > m_wantedLast))
                continue; // scrolled past before we got to it; the pane evicted
                          // the entry with the same range, so nothing stays latched
        }

        if (!ctx) {
            // Context creation failed (allocation pressure); retry per job so a
            // transient failure recovers, and echo the job so the pane always
            // gets a result instead of hanging in the Opening state.
            try {
                ctx = MupdfLib::CloneContext();
            } catch (const std::exception&) {
                if (job.type == Job::Type::Open) {
                    auto result = std::make_unique<OpenResult>();
                    result->path = std::move(job.path);
                    result->generation = job.generation;
                    result->error = L"MuPDF initialization failed";
                    PostOrDelete(m_notify, WM_PSV_DOC_OPENED, std::move(result));
                } else if (job.type == Job::Type::Render) {
                    auto result = std::make_unique<RenderResult>();
                    result->pageIndex = job.pageIndex;
                    result->scale = job.scale;
                    result->res = job.res;
                    result->row = job.row;
                    result->col = job.col;
                    result->requestId = job.requestId; // ok stays false
                    PostOrDelete(m_notify, WM_PSV_PAGE_RENDERED, std::move(result));
                } else if (job.type == Job::Type::TextPage) {
                    auto result = std::make_unique<TextPageResult>();
                    result->pageIndex = job.pageIndex; // empty: unlatch the pane
                    PostOrDelete(m_notify, WM_PSV_TEXT_PAGE, std::move(result));
                } else if (job.type == Job::Type::Links) {
                    auto result = std::make_unique<LinksResult>();
                    result->pageIndex = job.pageIndex;
                    PostOrDelete(m_notify, WM_PSV_LINKS, std::move(result));
                } // Search jobs are simply dropped: no context, no results
                continue;
            }
        }

        switch (job.type) {
        case Job::Type::Open:
            WorkerOpen(ctx, job);
            break;
        case Job::Type::Close:
            DropDisplayLists(ctx);
            if (m_doc) {
                fz_drop_document(ctx, m_doc);
                m_doc = nullptr;
            }
            m_pageCount = 0;
            break;
        case Job::Type::Render:
            WorkerRender(ctx, job);
            break;
        case Job::Type::TextPage:
            WorkerTextPage(ctx, job);
            break;
        case Job::Type::Links:
            WorkerLinks(ctx, job);
            break;
        case Job::Type::Search:
            WorkerSearch(ctx, job);
            break;
        }
    }

    if (ctx) {
        DropDisplayLists(ctx);
        if (m_doc) {
            fz_drop_document(ctx, m_doc);
            m_doc = nullptr;
        }
        fz_drop_context(ctx);
    }
}

void Document::WorkerOpen(fz_context* ctx, const Job& job) {
    DropDisplayLists(ctx);
    if (m_doc) {
        fz_drop_document(ctx, m_doc);
        m_doc = nullptr;
    }
    m_pageCount = 0;

    auto result = std::make_unique<OpenResult>();
    result->path = job.path;
    result->generation = job.generation;
    const std::string utf8 = ToUtf8(job.path);

    fz_document* doc = nullptr;
    int pageCount = 0;
    bool needsPassword = false;
    fz_var(doc);
    fz_var(pageCount);
    fz_var(needsPassword);
    fz_try(ctx) {
        doc = fz_open_document(ctx, utf8.c_str());
        needsPassword = fz_needs_password(ctx, doc) != 0;
        if (!needsPassword)
            pageCount = fz_count_pages(ctx, doc);
    }
    fz_catch(ctx) {
        if (doc) {
            fz_drop_document(ctx, doc);
            doc = nullptr;
        }
        result->error = FromUtf8(fz_caught_message(ctx));
    }

    if (needsPassword) {
        fz_drop_document(ctx, doc);
        result->error = L"password-protected documents are not supported yet";
        PostOrDelete(m_notify, WM_PSV_DOC_OPENED, std::move(result));
        return;
    }
    if (!doc) {
        if (result->error.empty())
            result->error = L"could not open the document";
        PostOrDelete(m_notify, WM_PSV_DOC_OPENED, std::move(result));
        return;
    }

    // Preallocate outside fz_try so no C++ exception can cross a fz_try frame.
    result->pageSizesPt.assign(static_cast<size_t>(pageCount), D2D1::SizeF(612.0f, 792.0f));
    bool sizesOk = true;
    fz_page* page = nullptr; // hoisted: fz_bound_page can throw and must not leak the page
    fz_var(sizesOk);
    fz_var(page);
    fz_try(ctx) {
        for (int i = 0; i < pageCount; ++i) {
            page = fz_load_page(ctx, doc, i);
            const fz_rect box = fz_bound_page(ctx, page);
            fz_drop_page(ctx, page);
            page = nullptr;
            result->pageSizesPt[static_cast<size_t>(i)] =
                D2D1::SizeF(box.x1 - box.x0, box.y1 - box.y0);
        }
    }
    fz_always(ctx) {
        if (page) {
            fz_drop_page(ctx, page);
            page = nullptr;
        }
    }
    fz_catch(ctx) {
        sizesOk = false;
        result->error = FromUtf8(fz_caught_message(ctx));
    }

    if (!sizesOk) {
        fz_drop_document(ctx, doc);
        result->pageSizesPt.clear();
        PostOrDelete(m_notify, WM_PSV_DOC_OPENED, std::move(result));
        return;
    }

    // Outline (bookmarks): optional; a damaged tree must not fail the open.
    fz_outline* outline = nullptr;
    fz_var(outline);
    fz_try(ctx) {
        outline = fz_load_outline(ctx, doc);
    }
    fz_catch(ctx) {
        outline = nullptr;
    }
    if (outline) {
        // Iterative depth-first walk: pure pointer reads, no fz calls, so no
        // longjmp can cross the C++ containers. PDF documents are single
        // chapter, so location.page is the page index directly.
        std::vector<std::pair<fz_outline*, int>> stack;
        stack.emplace_back(outline, 0);
        while (!stack.empty()) {
            auto [node, depth] = stack.back();
            stack.pop_back();
            if (!node)
                continue;
            stack.emplace_back(node->next, depth);
            OutlineItem item;
            item.depth = depth;
            item.title = FromUtf8(node->title);
            if (node->page.chapter == 0 && node->page.page >= 0 &&
                node->page.page < pageCount)
                item.targetPage = node->page.page;
            if (node->y == node->y && node->y > 0) // NaN-safe
                item.targetY = node->y;
            result->outline.push_back(std::move(item));
            stack.emplace_back(node->down, depth + 1);
        }
        fz_drop_outline(ctx, outline);
    }

    // Page labels (/PageLabels): optional; a damaged number tree must not
    // fail the open, hence the separate fz frame. All C++ allocation stays
    // outside the frame (setjmp rule); inside there are only C byte writes.
    // pdf_page_label falls back to the decimal ordinal when the tree is
    // absent, so "no labels" is detected by comparison afterwards.
    constexpr int kLabelCap = 64;
    std::vector<char> rawLabels(static_cast<size_t>(pageCount) * kLabelCap, '\0');
    bool labelsOk = false;
    fz_var(labelsOk);
    fz_try(ctx) {
        pdf_document* pdoc = pdf_specifics(ctx, doc); // borrowed; null = not a PDF
        if (pdoc) {
            for (int i = 0; i < pageCount; ++i)
                pdf_page_label(ctx, pdoc, i,
                               rawLabels.data() + static_cast<size_t>(i) * kLabelCap,
                               kLabelCap);
            labelsOk = true;
        }
    }
    fz_catch(ctx) {
        labelsOk = false;
    }
    if (labelsOk) {
        result->pageLabels.reserve(static_cast<size_t>(pageCount));
        bool anyCustom = false;
        for (int i = 0; i < pageCount; ++i) {
            std::wstring label = FromUtf8(rawLabels.data() + static_cast<size_t>(i) * kLabelCap);
            if (label != std::to_wstring(i + 1))
                anyCustom = true;
            result->pageLabels.push_back(std::move(label));
        }
        if (!anyCustom)
            result->pageLabels.clear(); // plain ordinals: nothing worth carrying
    }

    m_doc = doc;
    m_pageCount = pageCount;
    result->ok = true;
    PostOrDelete(m_notify, WM_PSV_DOC_OPENED, std::move(result));
}

void Document::WorkerRender(fz_context* ctx, const Job& job) {
    if (!m_doc || job.pageIndex < 0 || job.pageIndex >= m_pageCount) {
        // Always post (ok stays false): a silent return would leave the pane's
        // pending bookkeeping latched (e.g. renders queued across a re-open
        // that landed on a shorter document).
        auto result = std::make_unique<RenderResult>();
        result->pageIndex = job.pageIndex;
        result->scale = job.scale;
        result->res = job.res;
        result->row = job.row;
        result->col = job.col;
        result->requestId = job.requestId;
        PostOrDelete(m_notify, WM_PSV_PAGE_RENDERED, std::move(result));
        return;
    }

    fz_cookie cookie{};
    {
        std::lock_guard lock(m_mutex);
        m_activeCookie = &cookie;
        m_activePage = job.pageIndex;
        m_activeRes = job.res;
        m_activeRow = job.row;
        m_activeCol = job.col;
    }

    fz_pixmap* pix = nullptr;
    fz_device* dev = nullptr;
    fz_var(pix);
    fz_var(dev);
    fz_try(ctx) {
        fz_display_list* list = AcquireDisplayList(ctx, job.pageIndex);
        const fz_rect bounds = fz_bound_display_list(ctx, list);
        // Normalize the origin so the pixmap is exactly ceil(pageSize*scale)
        // pixels: PageLayout quantizes rects the same way, so the up-to-date
        // blit is 1:1 with no resampling blur.
        const fz_matrix ctm =
            fz_concat(fz_translate(-bounds.x0, -bounds.y0), fz_scale(job.scale, job.scale));
        fz_irect bbox;
        if (job.res <= 0) {
            bbox = fz_round_rect(fz_transform_rect(bounds, ctm));
        } else {
            // Integer tile grid over the quantized page pixel size; must
            // mirror PageLayout::Update and PaneWindow::DrawDocument exactly,
            // or tile seams appear.
            const int fullW =
                static_cast<int>(std::ceil((bounds.x1 - bounds.x0) * job.scale - 0.001f));
            const int fullH =
                static_cast<int>(std::ceil((bounds.y1 - bounds.y0) * job.scale - 0.001f));
            const int n = 1 << job.res;
            bbox.x0 = fullW * job.col / n;
            bbox.x1 = fullW * (job.col + 1) / n;
            bbox.y0 = fullH * job.row / n;
            bbox.y1 = fullH * (job.row + 1) / n;
        }
        // BGR + alpha=1 gives B,G,R,A byte order: maps 1:1 onto Direct2D's
        // DXGI_FORMAT_B8G8R8A8_UNORM without conversion.
        pix = fz_new_pixmap_with_bbox(ctx, fz_device_bgr(ctx), bbox, nullptr, 1);
        fz_clear_pixmap_with_value(ctx, pix, 0xFF); // opaque white page
        dev = fz_new_draw_device(ctx, fz_identity, pix);
        fz_run_display_list(ctx, list, dev, ctm, fz_infinite_rect, &cookie);
        fz_close_device(ctx, dev);
    }
    fz_always(ctx) {
        if (dev) {
            fz_drop_device(ctx, dev);
            dev = nullptr;
        }
    }
    fz_catch(ctx) {
        if (pix) {
            fz_drop_pixmap(ctx, pix);
            pix = nullptr;
        }
    }

    {
        std::lock_guard lock(m_mutex);
        m_activeCookie = nullptr;
        m_activePage = -1;
        m_activeRes = -1;
        m_activeRow = -1;
        m_activeCol = -1;
    }

    if (cookie.abort) {
        if (pix)
            fz_drop_pixmap(ctx, pix);
        return; // superseded: a fresher request is queued
    }

    // Always post, even on failure (ok stays false): the pane must clear its
    // pending bookkeeping or the page would never be re-requested.
    auto result = std::make_unique<RenderResult>();
    result->pageIndex = job.pageIndex;
    result->scale = job.scale;
    result->res = job.res;
    result->row = job.row;
    result->col = job.col;
    result->requestId = job.requestId;
    if (pix) {
        result->ok = true;
        result->width = fz_pixmap_width(ctx, pix);
        result->height = fz_pixmap_height(ctx, pix);
        result->stride = static_cast<int>(fz_pixmap_stride(ctx, pix));
        const size_t bytes = static_cast<size_t>(result->stride) * result->height;
        result->pixels = std::make_unique<uint8_t[]>(bytes);
        memcpy(result->pixels.get(), fz_pixmap_samples(ctx, pix), bytes);
        fz_drop_pixmap(ctx, pix);
    }

    PostOrDelete(m_notify, WM_PSV_PAGE_RENDERED, std::move(result));
}

fz_display_list* Document::AcquireDisplayList(fz_context* ctx, int pageIndex) {
    if (auto it = m_lists.find(pageIndex); it != m_lists.end())
        return it->second;
    fz_display_list* list = fz_new_display_list_from_page_number(ctx, m_doc, pageIndex);
    m_lists[pageIndex] = list;
    while (m_lists.size() > kMaxCachedDisplayLists) {
        auto farthest = m_lists.begin();
        int bestDistance = -1;
        for (auto it = m_lists.begin(); it != m_lists.end(); ++it) {
            const int distance = abs(it->first - pageIndex);
            if (distance > bestDistance) {
                bestDistance = distance;
                farthest = it;
            }
        }
        fz_drop_display_list(ctx, farthest->second);
        m_lists.erase(farthest);
    }
    return list;
}

void Document::DropDisplayLists(fz_context* ctx) {
    for (auto& [page, list] : m_lists)
        fz_drop_display_list(ctx, list);
    m_lists.clear();
}

void Document::WorkerTextPage(fz_context* ctx, const Job& job) {
    auto result = std::make_unique<TextPageResult>();
    result->pageIndex = job.pageIndex;

    if (m_doc && job.pageIndex >= 0 && job.pageIndex < m_pageCount) {
        fz_stext_page* stext = nullptr;
        fz_var(stext);
        fz_try(ctx) {
            stext = fz_new_stext_page_from_page_number(ctx, m_doc, job.pageIndex, nullptr);
        }
        fz_catch(ctx) {
            stext = nullptr; // damaged page: post the empty model
        }
        if (stext) {
            // Pure pointer walking: no fz calls, so no longjmp can cross the
            // C++ containers built here.
            for (fz_stext_block* block = stext->first_block; block; block = block->next) {
                if (block->type != FZ_STEXT_BLOCK_TEXT)
                    continue;
                for (fz_stext_line* line = block->u.t.first_line; line; line = line->next) {
                    TextLine tl;
                    for (fz_stext_char* ch = line->first_char; ch; ch = ch->next) {
                        TextChar tc;
                        tc.codepoint = static_cast<uint32_t>(ch->c);
                        tc.box.x0 = std::min({ch->quad.ul.x, ch->quad.ur.x, ch->quad.ll.x,
                                              ch->quad.lr.x});
                        tc.box.x1 = std::max({ch->quad.ul.x, ch->quad.ur.x, ch->quad.ll.x,
                                              ch->quad.lr.x});
                        tc.box.y0 = std::min({ch->quad.ul.y, ch->quad.ur.y, ch->quad.ll.y,
                                              ch->quad.lr.y});
                        tc.box.y1 = std::max({ch->quad.ul.y, ch->quad.ur.y, ch->quad.ll.y,
                                              ch->quad.lr.y});
                        tl.chars.push_back(tc);
                    }
                    if (tl.chars.empty())
                        continue;
                    tl.box = tl.chars.front().box;
                    for (const TextChar& tc : tl.chars) {
                        tl.box.x0 = std::min(tl.box.x0, tc.box.x0);
                        tl.box.y0 = std::min(tl.box.y0, tc.box.y0);
                        tl.box.x1 = std::max(tl.box.x1, tc.box.x1);
                        tl.box.y1 = std::max(tl.box.y1, tc.box.y1);
                    }
                    result->lines.push_back(std::move(tl));
                }
            }
            fz_drop_stext_page(ctx, stext);
        }
    }

    PostOrDelete(m_notify, WM_PSV_TEXT_PAGE, std::move(result));
}

void Document::WorkerLinks(fz_context* ctx, const Job& job) {
    auto result = std::make_unique<LinksResult>();
    result->pageIndex = job.pageIndex;

    if (m_doc && job.pageIndex >= 0 && job.pageIndex < m_pageCount) {
        fz_page* page = nullptr;
        fz_link* links = nullptr;
        fz_var(page);
        fz_var(links);
        fz_try(ctx) {
            page = fz_load_page(ctx, m_doc, job.pageIndex);
            links = fz_load_links(ctx, page);
        }
        fz_catch(ctx) {
            links = nullptr;
        }
        for (fz_link* link = links; link; link = link->next) {
            if (!link->uri)
                continue;
            LinkInfo info;
            info.box = {link->rect.x0, link->rect.y0, link->rect.x1, link->rect.y1};
            if (fz_is_external_link(ctx, link->uri)) {
                info.uri = FromUtf8(link->uri);
            } else {
                int targetPage = -1;
                float targetY = 0;
                fz_var(targetPage);
                fz_var(targetY);
                fz_try(ctx) {
                    const fz_link_dest dest = fz_resolve_link_dest(ctx, m_doc, link->uri);
                    targetPage = fz_page_number_from_location(ctx, m_doc, dest.loc);
                    if (dest.y == dest.y && dest.y > 0) // NaN-safe
                        targetY = dest.y;
                }
                fz_catch(ctx) {
                    targetPage = -1;
                }
                if (targetPage < 0)
                    continue; // unresolvable destination
                info.targetPage = targetPage;
                info.targetY = targetY;
            }
            result->links.push_back(std::move(info));
        }
        if (links)
            fz_drop_link(ctx, links);
        if (page)
            fz_drop_page(ctx, page);
    }

    PostOrDelete(m_notify, WM_PSV_LINKS, std::move(result));
}

void Document::WorkerSearch(fz_context* ctx, const Job& job) {
    constexpr int kChunkPages = 8;   // interleave with renders queued in front
    constexpr int kMaxHitQuads = 512; // per page

    if (!m_doc)
        return;
    {
        std::lock_guard lock(m_mutex);
        if (job.searchId != m_currentSearchId)
            return; // superseded
    }

    auto result = std::make_unique<SearchResult>();
    result->searchId = job.searchId;
    const int end = std::min(job.pageIndex + kChunkPages, m_pageCount);
    std::vector<int> marks(kMaxHitQuads);
    std::vector<fz_quad> quads(kMaxHitQuads);

    // FZ_SEARCH_EXACT is case SENSITIVE; the historic fz_search_* family is
    // itself a shim over this one with FZ_SEARCH_IGNORE_CASE, so the
    // case-insensitive branch reproduces the previous behaviour bit for bit.
    const fz_search_options options =
        job.searchOptions.matchCase ? FZ_SEARCH_EXACT : FZ_SEARCH_IGNORE_CASE;
    // The stext model fz_match_page builds internally (source/fitz/util.c).
    // Built here instead so the whole-word filter looks at the SAME page the
    // matcher ran on: a second extraction with different flags would classify
    // boundaries against text the hits do not come from.
    fz_stext_options stextOpts{};
    stextOpts.flags = FZ_STEXT_DEHYPHENATE;
    const NeedleEdges edges = EdgesOf(job.needleUtf8);

    for (int page = job.pageIndex; page < end; ++page) {
        {
            std::lock_guard lock(m_mutex);
            if (m_quit || job.searchId != m_currentSearchId)
                return;
        }
        fz_stext_page* stext = nullptr;
        int count = 0;
        fz_var(stext);
        fz_var(count);
        fz_try(ctx) {
            stext = fz_new_stext_page_from_page_number(ctx, m_doc, page, &stextOpts);
            count = fz_match_stext_page(ctx, stext, job.needleUtf8.c_str(), marks.data(),
                                        quads.data(), kMaxHitQuads, options);
        }
        fz_catch(ctx) {
            count = 0; // damaged page: skip
        }
        // hit_mark groups the quads belonging to one logical match (a match
        // wrapping across lines yields several quads).
        SearchMatch current;
        current.pageIndex = page;
        int currentMark = -1;
        fz_quad firstQuad{};
        fz_quad lastQuad{};
        const auto flush = [&] {
            if (current.rects.empty())
                return;
            if (!job.searchOptions.wholeWord ||
                HitIsWholeWord(stext, edges, firstQuad, lastQuad))
                result->matches.push_back(std::move(current));
            current = SearchMatch{};
            current.pageIndex = page;
        };
        for (int i = 0; i < count; ++i) {
            if (marks[i] != currentMark)
                flush();
            currentMark = marks[i];
            const fz_quad& q = quads[static_cast<size_t>(i)];
            if (current.rects.empty())
                firstQuad = q;
            lastQuad = q;
            const QuadBox b = BoxOf(q);
            RectPt r;
            r.x0 = b.x0;
            r.x1 = b.x1;
            r.y0 = b.y0;
            r.y1 = b.y1;
            current.rects.push_back(r);
        }
        flush();
        if (stext)
            fz_drop_stext_page(ctx, stext);
    }

    result->pagesScanned = end;
    result->done = end >= m_pageCount;
    const bool done = result->done;
    PostOrDelete(m_notify, WM_PSV_SEARCH, std::move(result));

    if (!done) {
        std::lock_guard lock(m_mutex);
        if (!m_quit && job.searchId == m_currentSearchId) {
            Job cont = job; // copies the needle
            cont.pageIndex = end;
            m_jobs.push_back(std::move(cont)); // back: renders keep priority
        }
    }
}
