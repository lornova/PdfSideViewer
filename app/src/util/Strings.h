#pragma once

#include "framework.h"

// Every user-visible UI string in every supported language. The X-list keeps an
// id and all of its translations on one row, so the enum and the per-language
// tables cannot drift apart when entries are added or reordered. Engine-level
// error strings (engine/Document.cpp) stay English: they are produced on the
// worker thread and cached inside result structs, so they cannot follow a
// live language switch anyway.
//
// X(id, english, italian)
#define PSV_STRING_LIST(X)                                                                         \
    /* menu bar */                                                                                 \
    X(MenuFile, L"&File", L"&File")                                                                \
    X(MenuOpenLeft, L"Open &Left...\tCtrl+O", L"Apri a &sinistra...\tCtrl+O")                      \
    X(MenuOpenRight, L"Open &Right...\tCtrl+Shift+O", L"Apri a &destra...\tCtrl+Shift+O")          \
    X(MenuCloseDoc, L"&Close\tCtrl+W", L"C&hiudi\tCtrl+W")                                         \
    X(MenuRecentFiles, L"Recent &Files", L"File recen&ti")                                         \
    X(MenuRecentPairs, L"Recent Pa&irs", L"&Coppie recenti")                                       \
    X(MenuMruEmpty, L"(empty)", L"(vuoto)")                                                        \
    X(MruMissingFile, L"File not found:\n", L"File non trovato:\n")                                \
    X(MenuOptions, L"&Options...", L"&Opzioni...")                                                 \
    X(MenuExit, L"E&xit\tAlt+F4", L"&Esci\tAlt+F4")                                                \
    X(MenuView, L"&View", L"&Visualizza")                                                          \
    X(MenuToolbar, L"&Toolbar", L"Barra degli &strumenti")                                         \
    X(MenuStatusBar, L"&Status Bar", L"Barra di s&tato")                                           \
    X(MenuOutline, L"&Outline\tF9", L"Se&gnalibri\tF9")                                            \
    X(MenuLockToolbars, L"Loc&k the Toolbars", L"&Blocca le barre degli strumenti")                \
    X(MenuZoomIn, L"Zoom &In\tCtrl+Plus", L"&Ingrandisci\tCtrl+Più")                               \
    X(MenuZoomOut, L"Zoom O&ut\tCtrl+Minus", L"&Riduci\tCtrl+Meno")                                \
    X(MenuActualSize, L"&Actual Size\tCtrl+0", L"Dimensioni &effettive\tCtrl+0")                   \
    X(MenuFitWidth, L"Fit &Width\tCtrl+2", L"Adatta &larghezza\tCtrl+2")                           \
    X(MenuFitPage, L"Fit &Page\tCtrl+3", L"Adatta &pagina\tCtrl+3")                                \
    X(MenuScrollContinuous, L"&Continuous Scrolling\tCtrl+4", L"Scorrimento c&ontinuo\tCtrl+4")    \
    X(MenuScrollPaged, L"Page-&by-Page\tCtrl+4", L"Pagina per pagi&na\tCtrl+4")                    \
    X(MenuGotoPage, L"&Go to Page...\tCtrl+G", L"&Vai alla pagina...\tCtrl+G")                     \
    X(MenuLanguage, L"&Language", L"Ling&ua")                                                      \
    X(MenuLangEnglish, L"English", L"English")                                                     \
    X(MenuLangItalian, L"Italiano", L"Italiano")                                                   \
    X(MenuFullScreen, L"&Full Screen\tF11", L"S&chermo intero\tF11")                               \
    X(MenuSync, L"S&ync", L"&Sincronizzazione")                                                    \
    X(MenuScrollSync, L"&Scroll Sync\tF7", L"Sync &scorrimento\tF7")                               \
    X(MenuZoomSync, L"&Zoom Sync\tCtrl+F7", L"Sync &zoom\tCtrl+F7")                                \
    X(MenuSwapPanes, L"S&wap Panes\tF8", L"Scam&bia pannelli\tF8")                                 \
    X(MenuHelp, L"&Help", L"&?")                                                                   \
    X(MenuAbout, L"&About PdfSideViewer...", L"&Informazioni su PdfSideViewer...")                 \
    /* toolbar tooltips */                                                                         \
    X(TipOpenLeft, L"Open left (Ctrl+O)", L"Apri a sinistra (Ctrl+O)")                             \
    X(TipOpenRight, L"Open right (Ctrl+Shift+O)", L"Apri a destra (Ctrl+Shift+O)")                 \
    X(TipScrollSync, L"Scroll sync (F7)", L"Sync scorrimento (F7)")                                \
    X(TipZoomSync, L"Zoom sync (Ctrl+F7)", L"Sync zoom (Ctrl+F7)")                                 \
    X(TipFitWidth, L"Fit width (Ctrl+2)", L"Adatta larghezza (Ctrl+2)")                            \
    X(TipFitPage, L"Fit page (Ctrl+3)", L"Adatta pagina (Ctrl+3)")                                 \
    X(TipScrollContinuous, L"Continuous scrolling (Ctrl+4)", L"Scorrimento continuo (Ctrl+4)")     \
    X(TipScrollPaged, L"Page-by-page (Ctrl+4)", L"Pagina per pagina (Ctrl+4)")                     \
    X(TipActualSize, L"Actual size (Ctrl+0)", L"Dimensioni effettive (Ctrl+0)")                    \
    X(TipFind, L"Find (Ctrl+F)", L"Trova (Ctrl+F)")                                                \
    X(TipOutline, L"Outline (F9)", L"Segnalibri (F9)")                                             \
    X(TipFullScreen, L"Full screen (F11)", L"Schermo intero (F11)")                                \
    /* synctex feedback (status bar, never popups) */                                              \
    X(SyncTexNoData, L"SyncTeX: no .synctex file for this document",                               \
      L"SyncTeX: nessun file .synctex per questo documento")                                       \
    X(SyncTexNoMatch, L"SyncTeX: nothing found at this position",                                  \
      L"SyncTeX: nessun risultato in questa posizione")                                            \
    X(SyncTexForwardMiss, L"SyncTeX: source line not found in this document",                      \
      L"SyncTeX: riga sorgente non trovata in questo documento")                                   \
    X(SyncTexEditorError, L"SyncTeX: could not launch the editor",                                 \
      L"SyncTeX: impossibile avviare l'editor")                                                    \
    /* status bar ("Left: " + "3 / 42" is composed in code) */                                     \
    X(StatusLeftPrefix, L"Left: ", L"Sinistra: ")                                                  \
    X(StatusRightPrefix, L"Right: ", L"Destra: ")                                                  \
    X(StatusLeftNoDoc, L"Left: —", L"Sinistra: —")                                                 \
    X(StatusRightNoDoc, L"Right: —", L"Destra: —")                                                 \
    X(StatusSyncBoth, L"Sync: scroll+zoom", L"Sync: scorrimento+zoom")                             \
    X(StatusSyncScroll, L"Sync: scroll", L"Sync: scorrimento")                                     \
    X(StatusSyncZoom, L"Sync: zoom", L"Sync: zoom")                                                \
    X(StatusSyncOff, L"Sync: off", L"Sync: disattivata")                                           \
    /* window title */                                                                             \
    X(TitleScrollSyncTag, L"  [scroll sync]", L"  [sync scorrimento]")                             \
    X(TitleZoomSyncTag, L"  [zoom sync]", L"  [sync zoom]")                                        \
    /* open dialog */                                                                              \
    X(OpenDlgTitleLeft, L"Open document in left pane", L"Apri documento nel pannello sinistro")    \
    X(OpenDlgTitleRight, L"Open document in right pane", L"Apri documento nel pannello destro")    \
    X(OpenDlgFilter, L"PDF documents (*.pdf)\0*.pdf\0All files (*.*)\0*.*\0",                      \
      L"Documenti PDF (*.pdf)\0*.pdf\0Tutti i file (*.*)\0*.*\0")                                  \
    /* pane placeholders */                                                                        \
    X(PlaceholderLeft, L"Left pane\nCtrl+O or double-click to open a PDF",                         \
      L"Pannello sinistro\nCtrl+O o doppio click per aprire un PDF")                               \
    X(PlaceholderRight, L"Right pane\nCtrl+Shift+O or double-click to open a PDF",                 \
      L"Pannello destro\nCtrl+Shift+O o doppio click per aprire un PDF")                           \
    X(PaneOpening, L"Opening", L"Apertura di")                                                     \
    X(PaneOpenFailed, L"Could not open", L"Impossibile aprire")                                    \
    X(PaneEmptyDoc, L"(empty document)", L"(documento vuoto)")                                     \
    X(DeviceLostError, L"The graphics device could not be restored.",                              \
      L"Impossibile ripristinare il dispositivo grafico.")                                         \
    /* options dialog */                                                                           \
    X(OptTitle, L"Options", L"Opzioni")                                                            \
    X(OptRestoreSession, L"Reopen the last session at startup",                                    \
      L"Riapri l'ultima sessione all'avvio")                                                       \
    X(OptDefaultsGroup, L"Defaults for new documents", L"Predefiniti per i nuovi documenti")       \
    X(OptDefScrollMode, L"Scrolling:", L"Scorrimento:")                                            \
    X(OptDefZoomMode, L"Zoom:", L"Zoom:")                                                          \
    X(OptScrollContinuous, L"Continuous scrolling", L"Scorrimento continuo")                       \
    X(OptScrollPaged, L"Page-by-page", L"Pagina per pagina")                                       \
    X(OptZoomActual, L"Actual size", L"Dimensioni effettive")                                      \
    X(OptZoomFitWidth, L"Fit width", L"Adatta larghezza")                                          \
    X(OptZoomFitPage, L"Fit page", L"Adatta pagina")                                               \
    X(OptDefScrollSync, L"Scroll sync on", L"Sync scorrimento attiva")                             \
    X(OptDefZoomSync, L"Zoom sync on", L"Sync zoom attiva")                                        \
    X(OptSynctexInverse, L"SyncTeX inverse-search command (%f = file, %l = line):",                \
      L"Comando ricerca inversa SyncTeX (%f = file, %l = riga):")                                  \
    X(OptShellIntegration,                                                                         \
      L"Show \"Open left/right in PdfSideViewer\" in the Explorer menu for PDF files",             \
      L"Mostra \"Apri a sinistra/destra in PdfSideViewer\" nel menu di Esplora file per i PDF")    \
    X(OptWheelLines, L"Wheel scroll lines (0 = system):",                                          \
      L"Righe per scatto della rotellina (0 = sistema):")                                          \
    X(OptClearRecent, L"Clear recent files and pairs", L"Svuota gli elenchi recenti")              \
    /* explorer context-menu verbs (written to the registry at registration) */                    \
    X(VerbOpenLeft, L"Open left in PdfSideViewer", L"Apri a sinistra in PdfSideViewer")            \
    X(VerbOpenRight, L"Open right in PdfSideViewer", L"Apri a destra in PdfSideViewer")            \
    /* go-to-page dialog + shared dialog buttons */                                                \
    X(GotoTitle, L"Go to Page", L"Vai alla pagina")                                                \
    X(GotoPrompt, L"Page number or label:", L"Numero di pagina o etichetta:")                      \
    X(DlgOk, L"OK", L"OK")                                                                         \
    X(DlgCancel, L"Cancel", L"Annulla")                                                            \
    /* about box */                                                                                \
    X(AboutTitle, L"About PdfSideViewer", L"Informazioni su PdfSideViewer")                        \
    X(AboutBody,                                                                                   \
      L"Two PDFs side by side with synchronized scrolling.\n\n"                                    \
      L"(c) 2026 Lorenzo Novara - GPLv3\nPDF rendering: MuPDF (AGPLv3) by Artifex Software",       \
      L"Due PDF affiancati con scorrimento sincronizzato.\n\n"                                     \
      L"(c) 2026 Lorenzo Novara - GPLv3\nRendering PDF: MuPDF (AGPLv3) di Artifex Software")

enum class Lang { English = 0, Italian = 1 };

enum class StrId : size_t {
#define PSV_AS_ENUM(id, en, it) id,
    PSV_STRING_LIST(PSV_AS_ENUM)
#undef PSV_AS_ENUM
        Count,
};

// UI-thread only (like all UI state): panes and frame read strings while
// painting and building menus; workers never touch them.
void SetUiLanguage(Lang lang);
Lang UiLanguage();
PCWSTR Str(StrId id);
