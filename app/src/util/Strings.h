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
    X(MenuScrollPaged, L"Page-&by-Page\tCtrl+5", L"Pagina per pagi&na\tCtrl+5")                    \
    X(MenuGotoPage, L"&Go to Page...\tCtrl+G", L"&Vai alla pagina...\tCtrl+G")                     \
    X(MenuLanguage, L"&Language", L"Ling&ua")                                                      \
    X(MenuLangEnglish, L"English", L"English")                                                     \
    X(MenuLangItalian, L"Italiano", L"Italiano")                                                   \
    X(MenuFullScreen, L"&Full Screen\tF11", L"S&chermo intero\tF11")                               \
    X(MenuSync, L"S&ync", L"&Sincronizzazione")                                                    \
    X(MenuScrollSync, L"&Scroll Sync\tF7", L"Sync &scorrimento\tF7")                               \
    X(MenuZoomSync, L"&Zoom Sync\tCtrl+F7", L"Sync &zoom\tCtrl+F7")                                \
    X(MenuAddSyncPoint, L"&Add Sync Point Here\tShift+F7",                                         \
      L"&Aggiungi punto di sync qui\tShift+F7")                                                    \
    X(MenuSyncFromBookmarks, L"Sync Points from &Bookmarks", L"Punti di sync dai se&gnalibri")     \
    X(MenuSyncPoints, L"Sync &Points...", L"&Punti di sync...")                                    \
    X(MenuClearSyncPoints, L"&Clear Sync Points\tCtrl+Shift+F7",                                   \
      L"&Rimuovi punti di sync\tCtrl+Shift+F7")                                                    \
    X(MenuAlignmentGaps, L"Show Alignment &Gaps", L"&Mostra spazi di allineamento")                \
    X(MenuSwapPanes, L"S&wap Panes\tF8", L"Scam&bia pannelli\tF8")                                 \
    X(MenuHelp, L"&Help", L"&?")                                                                   \
    X(MenuAbout, L"&About PDF Side Viewer...", L"&Informazioni su PDF Side Viewer...")             \
    /* toolbar tooltips */                                                                         \
    X(TipOpenLeft, L"Open left (Ctrl+O)", L"Apri a sinistra (Ctrl+O)")                             \
    X(TipOpenRight, L"Open right (Ctrl+Shift+O)", L"Apri a destra (Ctrl+Shift+O)")                 \
    X(TipScrollSync, L"Scroll sync (F7)", L"Sync scorrimento (F7)")                                \
    X(TipZoomSync, L"Zoom sync (Ctrl+F7)", L"Sync zoom (Ctrl+F7)")                                 \
    X(TipFitWidth, L"Fit width (Ctrl+2)", L"Adatta larghezza (Ctrl+2)")                            \
    X(TipFitPage, L"Fit page (Ctrl+3)", L"Adatta pagina (Ctrl+3)")                                 \
    X(TipScrollContinuous, L"Continuous scrolling (Ctrl+4)", L"Scorrimento continuo (Ctrl+4)")     \
    X(TipScrollPaged, L"Page-by-page (Ctrl+5)", L"Pagina per pagina (Ctrl+5)")                     \
    X(TipActualSize, L"Actual size (Ctrl+0)", L"Dimensioni effettive (Ctrl+0)")                    \
    X(TipFind, L"Find (Ctrl+F)", L"Trova (Ctrl+F)")                                                \
    X(TipOutline, L"Outline (F9)", L"Segnalibri (F9)")                                             \
    X(TipFullScreen, L"Full screen (F11)", L"Schermo intero (F11)")                                \
    X(TipAddSyncPoint, L"Add sync point here (Shift+F7)",                                          \
      L"Aggiungi punto di sync qui (Shift+F7)")                                                    \
    X(TipSyncFromBookmarks, L"Sync points from bookmarks", L"Punti di sync dai segnalibri")        \
    X(TipSyncPoints, L"Sync points...", L"Punti di sync...")                                       \
    X(TipClearSyncPoints, L"Clear sync points (Ctrl+Shift+F7)",                                    \
      L"Rimuovi punti di sync (Ctrl+Shift+F7)")                                                    \
    X(TipAlignmentGaps, L"Show alignment gaps", L"Mostra spazi di allineamento")                   \
    X(TipSwapPanes, L"Swap panes (F8)", L"Scambia pannelli (F8)")                                  \
    /* toolbar text options (Internet Explorer's exact wording) + short       */                   \
    /* per-button labels for the "below"/"selective on right" modes           */                   \
    X(MenuToolbarTextBelow, L"Sho&w text labels", L"&Mostra etichette di testo")                   \
    X(MenuToolbarTextRight, L"Selecti&ve text on right", L"Testo selettivo a &destra")             \
    X(MenuToolbarTextNone, L"&No text labels", L"&Nessuna etichetta di testo")                     \
    X(LblOpenLeft, L"Left", L"Sinistra")                                                           \
    X(LblOpenRight, L"Right", L"Destra")                                                           \
    X(LblScrollSync, L"Scroll sync", L"Sync scorr.")                                               \
    X(LblZoomSync, L"Zoom sync", L"Sync zoom")                                                     \
    X(LblActualSize, L"100%", L"100%")                                                             \
    X(LblFitWidth, L"Width", L"Larghezza")                                                         \
    X(LblFitPage, L"Page", L"Pagina")                                                              \
    X(LblScrollContinuous, L"Continuous", L"Continuo")                                             \
    X(LblScrollPaged, L"Paged", L"A pagine")                                                       \
    X(LblFind, L"Find", L"Trova")                                                                  \
    X(LblOutline, L"Outline", L"Segnalibri")                                                       \
    X(LblFullScreen, L"Full screen", L"Schermo int.")                                              \
    X(LblAddSyncPoint, L"Add point", L"Agg. punto")                                                \
    X(LblSyncFromBookmarks, L"Bookmarks", L"Da segnalibri")                                        \
    X(LblSyncPoints, L"Points", L"Punti")                                                          \
    X(LblClearSyncPoints, L"Clear", L"Rimuovi")                                                    \
    X(LblAlignmentGaps, L"Gaps", L"Spazi")                                                         \
    X(LblSwapPanes, L"Swap", L"Scambia")                                                           \
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
    /* sync points (status cell suffix is composed in code: pre + count + post) */                 \
    X(StatusSyncPtsPre, L" · ", L" · ")                                                            \
    X(StatusSyncPtsPost, L" pts", L" punti")                                                       \
    X(SyncPtsGenerated, L"Sync points from bookmarks: ", L"Punti di sync dai segnalibri: ")        \
    X(SyncPtsNoMatch, L"Sync points: no matching numbered bookmarks",                              \
      L"Punti di sync: nessun segnalibro numerato in comune")                                      \
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
      L"Show \"Open left/right in PDF Side Viewer\" in the Explorer menu for PDF files",           \
      L"Mostra \"Apri a sinistra/destra in PDF Side Viewer\" nel menu di Esplora file per i PDF")  \
    X(OptFsToolbar, L"Show the toolbar in full screen",                                            \
      L"Mostra la barra degli strumenti a schermo intero")                                         \
    X(OptFsStatus, L"Show the status bar in full screen",                                          \
      L"Mostra la barra di stato a schermo intero")                                                \
    X(OptShowAnchors, L"Show sync-point anchor marks", L"Mostra le ancore dei punti di sync")      \
    X(OptShowTicks, L"Show sync-point ticks along the scrollbar",                                  \
      L"Mostra i tick dei punti di sync lungo la barra")                                           \
    X(OptShowHeader, L"Show a header above each pane",                                             \
      L"Mostra un'intestazione sopra ogni pannello")                                               \
    X(OptHeaderShowPath, L"Show the full path instead of the file name",                           \
      L"Mostra il percorso completo invece del nome file")                                         \
    X(OptWheelLines, L"Wheel scroll lines (0 = system):",                                          \
      L"Righe per scatto della rotellina (0 = sistema):")                                          \
    X(OptClearRecent, L"Clear recent files and pairs", L"Svuota gli elenchi recenti")              \
    /* explorer context-menu verbs (written to the registry at registration) */                    \
    X(VerbOpenLeft, L"Open left in PDF Side Viewer", L"Apri a sinistra in PDF Side Viewer")        \
    X(VerbOpenRight, L"Open right in PDF Side Viewer", L"Apri a destra in PDF Side Viewer")        \
    /* go-to-page dialog + shared dialog buttons */                                                \
    X(GotoTitle, L"Go to Page", L"Vai alla pagina")                                                \
    X(GotoPrompt, L"Page number or label:", L"Numero di pagina o etichetta:")                      \
    X(DlgOk, L"OK", L"OK")                                                                         \
    X(DlgCancel, L"Cancel", L"Annulla")                                                            \
    /* sync points dialog */                                                                       \
    X(SyncPtsDlgTitle, L"Sync Points", L"Punti di sincronizzazione")                               \
    X(SyncPtsColIndex, L"#", L"#")                                                                 \
    X(SyncPtsColNumbering, L"Numbering", L"Numerazione")                                           \
    X(SyncPtsColPages, L"Pages", L"Pagine")                                                        \
    X(SyncPtsColOrigin, L"Origin", L"Origine")                                                     \
    X(SyncPtsDlgRemove, L"&Remove", L"&Rimuovi")                                                   \
    X(SyncPtsDlgClear, L"Clear &All", L"Rimuovi &tutti")                                           \
    X(DlgClose, L"Close", L"Chiudi")                                                               \
    X(SyncPtOriginAuto, L"auto", L"auto")                                                          \
    X(SyncPtOriginManual, L"manual", L"manuale")                                                   \
    /* about box */                                                                                \
    X(AboutTitle, L"About PDF Side Viewer", L"Informazioni su PDF Side Viewer")                    \
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
