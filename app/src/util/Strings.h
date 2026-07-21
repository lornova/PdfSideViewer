#pragma once

#include "framework.h"

// Every user-visible UI string in every supported language. The X-list keeps an
// id and all of its translations on one row, so the enum and the per-language
// tables cannot drift apart when entries are added or reordered. Engine-level
// error strings (engine/Document.cpp) stay English: they are produced on the
// worker thread and cached inside result structs, so they cannot follow a
// live language switch anyway.
//
// Mnemonics (&) are chosen unique within each popup (File / View / Sync / Help
// and the rebar context menu) per language; accelerator key names follow the
// local Windows convention (DE Strg/Umschalt, FR Maj), like the Italian
// column's "Ctrl+Più".
//
// X(id, english, italian, german, french, hungarian)
#define PSV_STRING_LIST(X)                                                                         \
    /* menu bar */                                                                                 \
    X(MenuFile, L"&File", L"&File", L"&Datei", L"&Fichier", L"&Fájl")                              \
    X(MenuOpenLeft, L"Open &Left...\tCtrl+O", L"Apri a &sinistra...\tCtrl+O",                      \
      L"&Links öffnen...\tStrg+O", L"Ouvrir à &gauche...\tCtrl+O",                                 \
      L"Megnyitás &balra...\tCtrl+O")                                                              \
    X(MenuOpenRight, L"Open &Right...\tCtrl+Shift+O", L"Apri a &destra...\tCtrl+Shift+O",          \
      L"&Rechts öffnen...\tStrg+Umschalt+O", L"Ouvrir à &droite...\tCtrl+Maj+O",                   \
      L"Megnyitás &jobbra...\tCtrl+Shift+O")                                                       \
    X(MenuCloseDoc, L"&Close\tCtrl+W", L"C&hiudi\tCtrl+W", L"S&chließen\tStrg+W",                  \
      L"&Fermer\tCtrl+W", L"Be&zárás\tCtrl+W")                                                     \
    X(MenuRecentFiles, L"Recent &Files", L"File recen&ti", L"&Zuletzt verwendete Dateien",         \
      L"Fichiers &récents", L"&Legutóbbi fájlok")                                                  \
    X(MenuRecentPairs, L"Recent Pa&irs", L"&Coppie recenti", L"Zuletzt verwendete &Paare",         \
      L"&Paires récentes", L"Legutóbbi &párok")                                                    \
    X(MenuMruEmpty, L"(empty)", L"(vuoto)", L"(leer)", L"(vide)", L"(üres)")                       \
    X(MruMissingFile, L"File not found:\n", L"File non trovato:\n",                                \
      L"Datei nicht gefunden:\n", L"Fichier introuvable :\n", L"A fájl nem található:\n")          \
    X(MenuOptions, L"&Options...", L"&Opzioni...", L"&Optionen...", L"&Options...",                \
      L"Beállí&tások...")                                                                          \
    X(MenuExit, L"E&xit\tAlt+F4", L"&Esci\tAlt+F4", L"&Beenden\tAlt+F4", L"&Quitter\tAlt+F4",      \
      L"&Kilépés\tAlt+F4")                                                                         \
    X(MenuView, L"&View", L"&Visualizza", L"&Ansicht", L"&Affichage", L"&Nézet")                   \
    X(MenuToolbar, L"&Toolbar", L"Barra degli &strumenti", L"&Symbolleiste",                       \
      L"Barre d'&outils", L"&Eszköztár")                                                           \
    X(MenuStatusBar, L"&Status Bar", L"Barra di s&tato", L"S&tatusleiste", L"Barre d'é&tat",       \
      L"Á&llapotsor")                                                                              \
    X(MenuOutline, L"&Outline\tF9", L"Se&gnalibri\tF9", L"&Lesezeichen\tF9", L"&Signets\tF9",      \
      L"&Könyvjelzők\tF9")                                                                         \
    X(MenuLockToolbars, L"Loc&k the Toolbars", L"&Blocca le barre degli strumenti",                \
      L"Symbolleisten &fixieren", L"&Verrouiller les barres d'outils",                             \
      L"Eszköztárak &zárolása")                                                                    \
    X(MenuZoomIn, L"Zoom &In\tCtrl+Plus", L"&Ingrandisci\tCtrl+Più",                               \
      L"Ver&größern\tStrg+Plus", L"&Zoom avant\tCtrl+Plus", L"&Nagyítás\tCtrl+Plusz")              \
    X(MenuZoomOut, L"Zoom O&ut\tCtrl+Minus", L"&Riduci\tCtrl+Meno",                                \
      L"Ver&kleinern\tStrg+Minus", L"Zoom arr&ière\tCtrl+Moins",                                   \
      L"K&icsinyítés\tCtrl+Mínusz")                                                                \
    X(MenuActualSize, L"&Actual Size\tCtrl+0", L"Dimensioni &effettive\tCtrl+0",                   \
      L"&Originalgröße\tStrg+0", L"Taille &réelle\tCtrl+0", L"&Tényleges méret\tCtrl+0")           \
    X(MenuFitWidth, L"Fit &Width\tCtrl+2", L"Adatta &larghezza\tCtrl+2",                           \
      L"An &Breite anpassen\tStrg+2", L"Ajuster à la &largeur\tCtrl+2",                            \
      L"Igazítás &szélességhez\tCtrl+2")                                                           \
    X(MenuFitPage, L"Fit &Page\tCtrl+3", L"Adatta &pagina\tCtrl+3",                                \
      L"An S&eite anpassen\tStrg+3", L"Ajuster à la &page\tCtrl+3",                                \
      L"Igazítás &oldalhoz\tCtrl+3")                                                               \
    X(MenuScrollContinuous, L"&Continuous Scrolling\tCtrl+4", L"Scorrimento c&ontinuo\tCtrl+4",    \
      L"Fortlaufender B&ildlauf\tStrg+4", L"Défilement &continu\tCtrl+4",                          \
      L"&Folyamatos görgetés\tCtrl+4")                                                             \
    X(MenuScrollPaged, L"Page-&by-Page\tCtrl+5", L"Pagina per pagi&na\tCtrl+5",                    \
      L"Seiten&weise\tStrg+5", L"Page par pa&ge\tCtrl+5",                                          \
      L"Oldalankénti gö&rgetés\tCtrl+5")                                                           \
    X(MenuGotoPage, L"&Go to Page...\tCtrl+G", L"&Vai alla pagina...\tCtrl+G",                     \
      L"Gehe &zu Seite...\tStrg+G", L"&Aller à la page...\tCtrl+G",                                \
      L"&Ugrás oldalra...\tCtrl+G")                                                                \
    X(MenuLanguage, L"&Language", L"Ling&ua", L"Sp&rache", L"La&ngue", L"N&yelv")                  \
    X(MenuLangEnglish, L"English", L"English", L"English", L"English", L"English")                 \
    X(MenuLangItalian, L"Italiano", L"Italiano", L"Italiano", L"Italiano", L"Italiano")            \
    X(MenuLangGerman, L"Deutsch", L"Deutsch", L"Deutsch", L"Deutsch", L"Deutsch")                  \
    X(MenuLangFrench, L"Français", L"Français", L"Français", L"Français", L"Français")             \
    X(MenuLangHungarian, L"Magyar", L"Magyar", L"Magyar", L"Magyar", L"Magyar")                    \
    X(MenuFullScreen, L"&Full Screen\tF11", L"S&chermo intero\tF11", L"&Vollbild\tF11",            \
      L"Plein &écran\tF11", L"Tel&jes képernyő\tF11")                                              \
    X(MenuSync, L"S&ync", L"&Sincronizzazione", L"&Synchronisierung", L"&Synchronisation",         \
      L"&Szinkronizálás")                                                                          \
    X(MenuScrollSync, L"&Scroll Sync\tF7", L"Sync &scorrimento\tF7", L"&Bildlauf-Sync\tF7",        \
      L"Sync du &défilement\tF7", L"&Görgetés-szinkron\tF7")                                       \
    X(MenuZoomSync, L"&Zoom Sync\tCtrl+F7", L"Sync &zoom\tCtrl+F7", L"&Zoom-Sync\tStrg+F7",        \
      L"Sync du &zoom\tCtrl+F7", L"&Zoom-szinkron\tCtrl+F7")                                       \
    X(MenuAddSyncPoint, L"&Add Sync Point Here\tShift+F7",                                         \
      L"&Aggiungi punto di sync qui\tShift+F7",                                                    \
      L"Sync-Punkt hier hinzu&fügen\tUmschalt+F7",                                                 \
      L"&Ajouter un point de sync ici\tMaj+F7",                                                    \
      L"Szinkronpont &hozzáadása itt\tShift+F7")                                                   \
    X(MenuSyncFromBookmarks, L"Sync Points from &Bookmarks", L"Punti di sync dai se&gnalibri",     \
      L"Sync-Punkte aus &Lesezeichen", L"Points de sync depuis les &signets",                      \
      L"Szinkronpontok a &könyvjelzőkből")                                                         \
    X(MenuSyncPoints, L"Sync &Points...", L"&Punti di sync...", L"Sync-&Punkte...",                \
      L"&Points de sync...", L"Szinkron&pontok...")                                                \
    X(MenuClearSyncPoints, L"&Clear Sync Points\tCtrl+Shift+F7",                                   \
      L"&Rimuovi punti di sync\tCtrl+Shift+F7",                                                    \
      L"Sync-Punkte &entfernen\tStrg+Umschalt+F7",                                                 \
      L"&Effacer les points de sync\tCtrl+Maj+F7",                                                 \
      L"Szinkronpontok &törlése\tCtrl+Shift+F7")                                                   \
    X(MenuAlignmentGaps, L"Show Alignment &Gaps", L"&Mostra spazi di allineamento",                \
      L"&Ausrichtungslücken anzeigen", L"Afficher les espaces d'ali&gnement",                      \
      L"&Igazítási hézagok megjelenítése")                                                         \
    X(MenuSwapPanes, L"S&wap Panes\tF8", L"Scam&bia pannelli\tF8", L"Bereiche &tauschen\tF8",      \
      L"Éc&hanger les panneaux\tF8", L"Panelek &felcserélése\tF8")                                 \
    X(MenuHelp, L"&Help", L"&?", L"&Hilfe", L"Aid&e", L"Sú&gó")                                    \
    X(MenuAbout, L"&About PDF Side Viewer...", L"&Informazioni su PDF Side Viewer...",             \
      L"&Über PDF Side Viewer...", L"&À propos de PDF Side Viewer...",                             \
      L"&A PDF Side Viewer névjegye...")                                                           \
    /* toolbar tooltips */                                                                         \
    X(TipOpenLeft, L"Open left (Ctrl+O)", L"Apri a sinistra (Ctrl+O)",                             \
      L"Links öffnen (Strg+O)", L"Ouvrir à gauche (Ctrl+O)", L"Megnyitás balra (Ctrl+O)")          \
    X(TipOpenRight, L"Open right (Ctrl+Shift+O)", L"Apri a destra (Ctrl+Shift+O)",                 \
      L"Rechts öffnen (Strg+Umschalt+O)", L"Ouvrir à droite (Ctrl+Maj+O)",                         \
      L"Megnyitás jobbra (Ctrl+Shift+O)")                                                          \
    X(TipScrollSync, L"Scroll sync (F7)", L"Sync scorrimento (F7)", L"Bildlauf-Sync (F7)",         \
      L"Sync du défilement (F7)", L"Görgetés-szinkron (F7)")                                       \
    X(TipZoomSync, L"Zoom sync (Ctrl+F7)", L"Sync zoom (Ctrl+F7)", L"Zoom-Sync (Strg+F7)",         \
      L"Sync du zoom (Ctrl+F7)", L"Zoom-szinkron (Ctrl+F7)")                                       \
    X(TipFitWidth, L"Fit width (Ctrl+2)", L"Adatta larghezza (Ctrl+2)",                            \
      L"An Breite anpassen (Strg+2)", L"Ajuster à la largeur (Ctrl+2)",                            \
      L"Igazítás szélességhez (Ctrl+2)")                                                           \
    X(TipFitPage, L"Fit page (Ctrl+3)", L"Adatta pagina (Ctrl+3)",                                 \
      L"An Seite anpassen (Strg+3)", L"Ajuster à la page (Ctrl+3)",                                \
      L"Igazítás oldalhoz (Ctrl+3)")                                                               \
    X(TipScrollContinuous, L"Continuous scrolling (Ctrl+4)", L"Scorrimento continuo (Ctrl+4)",     \
      L"Fortlaufender Bildlauf (Strg+4)", L"Défilement continu (Ctrl+4)",                          \
      L"Folyamatos görgetés (Ctrl+4)")                                                             \
    X(TipScrollPaged, L"Page-by-page (Ctrl+5)", L"Pagina per pagina (Ctrl+5)",                     \
      L"Seitenweise (Strg+5)", L"Page par page (Ctrl+5)",                                          \
      L"Oldalankénti görgetés (Ctrl+5)")                                                           \
    X(TipActualSize, L"Actual size (Ctrl+0)", L"Dimensioni effettive (Ctrl+0)",                    \
      L"Originalgröße (Strg+0)", L"Taille réelle (Ctrl+0)", L"Tényleges méret (Ctrl+0)")           \
    X(TipFind, L"Find (Ctrl+F)", L"Trova (Ctrl+F)", L"Suchen (Strg+F)",                            \
      L"Rechercher (Ctrl+F)", L"Keresés (Ctrl+F)")                                                 \
    X(TipOutline, L"Outline (F9)", L"Segnalibri (F9)", L"Lesezeichen (F9)", L"Signets (F9)",       \
      L"Könyvjelzők (F9)")                                                                         \
    X(TipFullScreen, L"Full screen (F11)", L"Schermo intero (F11)", L"Vollbild (F11)",             \
      L"Plein écran (F11)", L"Teljes képernyő (F11)")                                              \
    X(TipAddSyncPoint, L"Add sync point here (Shift+F7)",                                          \
      L"Aggiungi punto di sync qui (Shift+F7)",                                                    \
      L"Sync-Punkt hier hinzufügen (Umschalt+F7)",                                                 \
      L"Ajouter un point de sync ici (Maj+F7)",                                                    \
      L"Szinkronpont hozzáadása itt (Shift+F7)")                                                   \
    X(TipSyncFromBookmarks, L"Sync points from bookmarks", L"Punti di sync dai segnalibri",        \
      L"Sync-Punkte aus Lesezeichen", L"Points de sync depuis les signets",                        \
      L"Szinkronpontok a könyvjelzőkből")                                                          \
    X(TipSyncPoints, L"Sync points...", L"Punti di sync...", L"Sync-Punkte...",                    \
      L"Points de sync...", L"Szinkronpontok...")                                                  \
    X(TipClearSyncPoints, L"Clear sync points (Ctrl+Shift+F7)",                                    \
      L"Rimuovi punti di sync (Ctrl+Shift+F7)",                                                    \
      L"Sync-Punkte entfernen (Strg+Umschalt+F7)",                                                 \
      L"Effacer les points de sync (Ctrl+Maj+F7)",                                                 \
      L"Szinkronpontok törlése (Ctrl+Shift+F7)")                                                   \
    X(TipAlignmentGaps, L"Show alignment gaps", L"Mostra spazi di allineamento",                   \
      L"Ausrichtungslücken anzeigen", L"Afficher les espaces d'alignement",                        \
      L"Igazítási hézagok megjelenítése")                                                          \
    X(TipSwapPanes, L"Swap panes (F8)", L"Scambia pannelli (F8)", L"Bereiche tauschen (F8)",       \
      L"Échanger les panneaux (F8)", L"Panelek felcserélése (F8)")                                 \
    /* toolbar text options (Internet Explorer's exact wording) + short       */                   \
    /* per-button labels for the "below"/"selective on right" modes           */                   \
    X(MenuToolbarTextBelow, L"Sho&w text labels", L"&Mostra etichette di testo",                   \
      L"Te&xtbeschriftungen anzeigen", L"Afficher les libellés de te&xte",                         \
      L"Sz&öveges címkék megjelenítése")                                                           \
    X(MenuToolbarTextRight, L"Selecti&ve text on right", L"Testo selettivo a &destra",             \
      L"Ausge&wählter Text rechts", L"Texte sélectif à &droite",                                   \
      L"Szelektív szöveg &jobbra")                                                                 \
    X(MenuToolbarTextNone, L"&No text labels", L"&Nessuna etichetta di testo",                     \
      L"&Keine Textbeschriftungen", L"Auc&un libellé de texte",                                    \
      L"&Nincs szöveges címke")                                                                    \
    X(LblOpenLeft, L"Left", L"Sinistra", L"Links", L"Gauche", L"Bal")                              \
    X(LblOpenRight, L"Right", L"Destra", L"Rechts", L"Droite", L"Jobb")                            \
    X(LblScrollSync, L"Scroll sync", L"Sync scorr.", L"Bildlauf-Sync", L"Sync défil.",             \
      L"Görg.-szinkron")                                                                           \
    X(LblZoomSync, L"Zoom sync", L"Sync zoom", L"Zoom-Sync", L"Sync zoom", L"Zoom-szinkron")       \
    X(LblActualSize, L"100%", L"100%", L"100%", L"100%", L"100%")                                  \
    X(LblFitWidth, L"Width", L"Larghezza", L"Breite", L"Largeur", L"Szélesség")                    \
    X(LblFitPage, L"Page", L"Pagina", L"Seite", L"Page", L"Oldal")                                 \
    X(LblScrollContinuous, L"Continuous", L"Continuo", L"Fortlaufend", L"Continu",                 \
      L"Folyamatos")                                                                               \
    X(LblScrollPaged, L"Paged", L"A pagine", L"Seitenweise", L"Par page", L"Oldalanként")          \
    X(LblFind, L"Find", L"Trova", L"Suchen", L"Rechercher", L"Keresés")                            \
    X(LblOutline, L"Outline", L"Segnalibri", L"Lesezeichen", L"Signets", L"Könyvjelzők")           \
    X(LblFullScreen, L"Full screen", L"Schermo int.", L"Vollbild", L"Plein écran",                 \
      L"Teljes kép.")                                                                              \
    X(LblAddSyncPoint, L"Add point", L"Agg. punto", L"Punkt hinzuf.", L"Ajouter point",            \
      L"Pont hozzáad.")                                                                            \
    X(LblSyncFromBookmarks, L"Bookmarks", L"Da segnalibri", L"Aus Lesezeichen",                    \
      L"Depuis signets", L"Könyvjelzőkből")                                                        \
    X(LblSyncPoints, L"Points", L"Punti", L"Punkte", L"Points", L"Pontok")                         \
    X(LblClearSyncPoints, L"Clear", L"Rimuovi", L"Entfernen", L"Effacer", L"Törlés")               \
    X(LblAlignmentGaps, L"Gaps", L"Spazi", L"Lücken", L"Espaces", L"Hézagok")                      \
    X(LblSwapPanes, L"Swap", L"Scambia", L"Tauschen", L"Échanger", L"Csere")                       \
    /* synctex feedback (status bar, never popups) */                                              \
    X(SyncTexNoData, L"SyncTeX: no .synctex file for this document",                               \
      L"SyncTeX: nessun file .synctex per questo documento",                                       \
      L"SyncTeX: keine .synctex-Datei für dieses Dokument",                                        \
      L"SyncTeX : aucun fichier .synctex pour ce document",                                        \
      L"SyncTeX: nincs .synctex fájl ehhez a dokumentumhoz")                                       \
    X(SyncTexNoMatch, L"SyncTeX: nothing found at this position",                                  \
      L"SyncTeX: nessun risultato in questa posizione",                                            \
      L"SyncTeX: an dieser Position nichts gefunden",                                              \
      L"SyncTeX : rien trouvé à cette position",                                                   \
      L"SyncTeX: nincs találat ezen a pozíción")                                                   \
    X(SyncTexForwardMiss, L"SyncTeX: source line not found in this document",                      \
      L"SyncTeX: riga sorgente non trovata in questo documento",                                   \
      L"SyncTeX: Quellzeile in diesem Dokument nicht gefunden",                                    \
      L"SyncTeX : ligne source introuvable dans ce document",                                      \
      L"SyncTeX: a forrássor nem található ebben a dokumentumban")                                 \
    X(SyncTexEditorError, L"SyncTeX: could not launch the editor",                                 \
      L"SyncTeX: impossibile avviare l'editor",                                                    \
      L"SyncTeX: Editor konnte nicht gestartet werden",                                            \
      L"SyncTeX : impossible de lancer l'éditeur",                                                 \
      L"SyncTeX: nem sikerült elindítani a szerkesztőt")                                           \
    /* status bar ("Left: " + "3 / 42" is composed in code) */                                     \
    X(StatusLeftPrefix, L"Left: ", L"Sinistra: ", L"Links: ", L"Gauche : ", L"Bal: ")              \
    X(StatusRightPrefix, L"Right: ", L"Destra: ", L"Rechts: ", L"Droite : ", L"Jobb: ")            \
    X(StatusLeftNoDoc, L"Left: —", L"Sinistra: —", L"Links: —", L"Gauche : —", L"Bal: —")          \
    X(StatusRightNoDoc, L"Right: —", L"Destra: —", L"Rechts: —", L"Droite : —", L"Jobb: —")        \
    X(StatusSyncBoth, L"Sync: scroll+zoom", L"Sync: scorrimento+zoom",                             \
      L"Sync: Bildlauf+Zoom", L"Sync : défilement+zoom", L"Szinkron: görgetés+zoom")               \
    X(StatusSyncScroll, L"Sync: scroll", L"Sync: scorrimento", L"Sync: Bildlauf",                  \
      L"Sync : défilement", L"Szinkron: görgetés")                                                 \
    X(StatusSyncZoom, L"Sync: zoom", L"Sync: zoom", L"Sync: Zoom", L"Sync : zoom",                 \
      L"Szinkron: zoom")                                                                           \
    X(StatusSyncOff, L"Sync: off", L"Sync: disattivata", L"Sync: aus", L"Sync : désactivée",       \
      L"Szinkron: ki")                                                                             \
    /* sync points (status cell suffix is composed in code: pre + count + post) */                 \
    X(StatusSyncPtsPre, L" · ", L" · ", L" · ", L" · ", L" · ")                                    \
    X(StatusSyncPtsPost, L" pts", L" punti", L" Punkte", L" points", L" pont")                     \
    X(SyncPtsGenerated, L"Sync points from bookmarks: ", L"Punti di sync dai segnalibri: ",        \
      L"Sync-Punkte aus Lesezeichen: ", L"Points de sync depuis les signets : ",                   \
      L"Szinkronpontok a könyvjelzőkből: ")                                                        \
    X(SyncPtsNoMatch, L"Sync points: no matching numbered bookmarks",                              \
      L"Punti di sync: nessun segnalibro numerato in comune",                                      \
      L"Sync-Punkte: keine übereinstimmenden nummerierten Lesezeichen",                            \
      L"Points de sync : aucun signet numéroté correspondant",                                     \
      L"Szinkronpontok: nincs egyező számozott könyvjelző")                                        \
    /* window title */                                                                             \
    X(TitleScrollSyncTag, L"  [scroll sync]", L"  [sync scorrimento]", L"  [Bildlauf-Sync]",       \
      L"  [sync défilement]", L"  [görgetés-szinkron]")                                            \
    X(TitleZoomSyncTag, L"  [zoom sync]", L"  [sync zoom]", L"  [Zoom-Sync]",                      \
      L"  [sync zoom]", L"  [zoom-szinkron]")                                                      \
    /* open dialog */                                                                              \
    X(OpenDlgTitleLeft, L"Open document in left pane", L"Apri documento nel pannello sinistro",    \
      L"Dokument im linken Bereich öffnen",                                                        \
      L"Ouvrir un document dans le panneau gauche",                                                \
      L"Dokumentum megnyitása a bal panelen")                                                      \
    X(OpenDlgTitleRight, L"Open document in right pane",                                           \
      L"Apri documento nel pannello destro", L"Dokument im rechten Bereich öffnen",                \
      L"Ouvrir un document dans le panneau droit",                                                 \
      L"Dokumentum megnyitása a jobb panelen")                                                     \
    X(OpenDlgFilter, L"PDF documents (*.pdf)\0*.pdf\0All files (*.*)\0*.*\0",                      \
      L"Documenti PDF (*.pdf)\0*.pdf\0Tutti i file (*.*)\0*.*\0",                                  \
      L"PDF-Dokumente (*.pdf)\0*.pdf\0Alle Dateien (*.*)\0*.*\0",                                  \
      L"Documents PDF (*.pdf)\0*.pdf\0Tous les fichiers (*.*)\0*.*\0",                             \
      L"PDF-dokumentumok (*.pdf)\0*.pdf\0Minden fájl (*.*)\0*.*\0")                                \
    /* pane placeholders */                                                                        \
    X(PlaceholderLeft, L"Left pane\nCtrl+O or double-click to open a PDF",                         \
      L"Pannello sinistro\nCtrl+O o doppio click per aprire un PDF",                               \
      L"Linker Bereich\nStrg+O oder Doppelklick, um ein PDF zu öffnen",                            \
      L"Panneau gauche\nCtrl+O ou double-clic pour ouvrir un PDF",                                 \
      L"Bal panel\nCtrl+O vagy dupla kattintás PDF megnyitásához")                                 \
    X(PlaceholderRight, L"Right pane\nCtrl+Shift+O or double-click to open a PDF",                 \
      L"Pannello destro\nCtrl+Shift+O o doppio click per aprire un PDF",                           \
      L"Rechter Bereich\nStrg+Umschalt+O oder Doppelklick, um ein PDF zu öffnen",                  \
      L"Panneau droit\nCtrl+Maj+O ou double-clic pour ouvrir un PDF",                              \
      L"Jobb panel\nCtrl+Shift+O vagy dupla kattintás PDF megnyitásához")                          \
    X(PaneOpening, L"Opening", L"Apertura di", L"Öffnen von", L"Ouverture de",                     \
      L"Megnyitás:")                                                                               \
    X(PaneOpenFailed, L"Could not open", L"Impossibile aprire", L"Fehler beim Öffnen von",         \
      L"Impossible d'ouvrir", L"Nem sikerült megnyitni:")                                          \
    X(PaneEmptyDoc, L"(empty document)", L"(documento vuoto)", L"(leeres Dokument)",               \
      L"(document vide)", L"(üres dokumentum)")                                                    \
    X(DeviceLostError, L"The graphics device could not be restored.",                              \
      L"Impossibile ripristinare il dispositivo grafico.",                                         \
      L"Das Grafikgerät konnte nicht wiederhergestellt werden.",                                   \
      L"Le périphérique graphique n'a pas pu être restauré.",                                      \
      L"A grafikus eszközt nem sikerült helyreállítani.")                                          \
    /* options dialog */                                                                           \
    X(OptTitle, L"Options", L"Opzioni", L"Optionen", L"Options", L"Beállítások")                   \
    X(OptRestoreSession, L"Reopen the last session at startup",                                    \
      L"Riapri l'ultima sessione all'avvio",                                                       \
      L"Letzte Sitzung beim Start wiederherstellen",                                               \
      L"Rouvrir la dernière session au démarrage",                                                 \
      L"Az utolsó munkamenet visszaállítása induláskor")                                           \
    X(OptDefaultsGroup, L"Defaults for new documents", L"Predefiniti per i nuovi documenti",       \
      L"Standardwerte für neue Dokumente",                                                         \
      L"Valeurs par défaut pour les nouveaux documents",                                           \
      L"Alapértelmezések új dokumentumokhoz")                                                      \
    X(OptDefScrollMode, L"Scrolling:", L"Scorrimento:", L"Bildlauf:", L"Défilement :",             \
      L"Görgetés:")                                                                                \
    X(OptDefZoomMode, L"Zoom:", L"Zoom:", L"Zoom:", L"Zoom :", L"Zoom:")                           \
    X(OptScrollContinuous, L"Continuous scrolling", L"Scorrimento continuo",                       \
      L"Fortlaufender Bildlauf", L"Défilement continu", L"Folyamatos görgetés")                    \
    X(OptScrollPaged, L"Page-by-page", L"Pagina per pagina", L"Seitenweise",                       \
      L"Page par page", L"Oldalankénti görgetés")                                                  \
    X(OptZoomActual, L"Actual size", L"Dimensioni effettive", L"Originalgröße",                    \
      L"Taille réelle", L"Tényleges méret")                                                        \
    X(OptZoomFitWidth, L"Fit width", L"Adatta larghezza", L"An Breite anpassen",                   \
      L"Ajuster à la largeur", L"Igazítás szélességhez")                                           \
    X(OptZoomFitPage, L"Fit page", L"Adatta pagina", L"An Seite anpassen",                         \
      L"Ajuster à la page", L"Igazítás oldalhoz")                                                  \
    X(OptDefScrollSync, L"Scroll sync on", L"Sync scorrimento attiva",                             \
      L"Bildlauf-Sync aktiv", L"Sync du défilement activée",                                       \
      L"Görgetés-szinkron bekapcsolva")                                                            \
    X(OptDefZoomSync, L"Zoom sync on", L"Sync zoom attiva", L"Zoom-Sync aktiv",                    \
      L"Sync du zoom activée", L"Zoom-szinkron bekapcsolva")                                       \
    X(OptSynctexInverse, L"SyncTeX inverse-search command (%f = file, %l = line):",                \
      L"Comando ricerca inversa SyncTeX (%f = file, %l = riga):",                                  \
      L"SyncTeX-Rücksuche-Befehl (%f = Datei, %l = Zeile):",                                       \
      L"Commande de recherche inverse SyncTeX (%f = fichier, %l = ligne) :",                       \
      L"SyncTeX fordított keresés parancsa (%f = fájl, %l = sor):")                                \
    X(OptShellIntegration,                                                                         \
      L"Show \"Open left/right in PDF Side Viewer\" in the Explorer menu for PDF files",           \
      L"Mostra \"Apri a sinistra/destra in PDF Side Viewer\" nel menu di Esplora file per i PDF",  \
      L"„Links/Rechts in PDF Side Viewer öffnen“ im Explorer-Menü für PDF-Dateien anzeigen",       \
      L"Afficher « Ouvrir à gauche/droite dans PDF Side Viewer » dans le menu de "                 \
      L"l'Explorateur pour les PDF",                                                               \
      L"„Megnyitás balra/jobbra a PDF Side Viewerben” megjelenítése az Intéző menüjében "          \
      L"a PDF-ekhez")                                                                              \
    X(OptFsToolbar, L"Show the toolbar in full screen",                                            \
      L"Mostra la barra degli strumenti a schermo intero",                                         \
      L"Symbolleiste im Vollbild anzeigen",                                                        \
      L"Afficher la barre d'outils en plein écran",                                                \
      L"Eszköztár megjelenítése teljes képernyőn")                                                 \
    X(OptFsStatus, L"Show the status bar in full screen",                                          \
      L"Mostra la barra di stato a schermo intero",                                                \
      L"Statusleiste im Vollbild anzeigen",                                                        \
      L"Afficher la barre d'état en plein écran",                                                  \
      L"Állapotsor megjelenítése teljes képernyőn")                                                \
    X(OptShowAnchors, L"Show sync-point anchor marks",                                             \
      L"Mostra le ancore dei punti di sync",                                                       \
      L"Ankermarken der Sync-Punkte anzeigen",                                                     \
      L"Afficher les ancres des points de sync",                                                   \
      L"Szinkronpont-horgonyok megjelenítése")                                                     \
    X(OptShowTicks, L"Show sync-point ticks along the scrollbar",                                  \
      L"Mostra i tick dei punti di sync lungo la barra",                                           \
      L"Sync-Punkt-Markierungen an der Bildlaufleiste anzeigen",                                   \
      L"Afficher les repères des points de sync le long de la barre",                              \
      L"Szinkronpont-jelölések a görgetősáv mentén")                                               \
    X(OptShowHeader, L"Show a header above each pane",                                             \
      L"Mostra un'intestazione sopra ogni pannello",                                               \
      L"Kopfzeile über jedem Bereich anzeigen",                                                    \
      L"Afficher un en-tête au-dessus de chaque panneau",                                          \
      L"Fejléc megjelenítése minden panel felett")                                                 \
    X(OptHeaderShowPath, L"Show the full path instead of the file name",                           \
      L"Mostra il percorso completo invece del nome file",                                         \
      L"Vollständigen Pfad statt des Dateinamens anzeigen",                                        \
      L"Afficher le chemin complet au lieu du nom de fichier",                                     \
      L"Teljes elérési út a fájlnév helyett")                                                      \
    X(OptWheelLines, L"Wheel scroll lines (0 = system):",                                          \
      L"Righe per scatto della rotellina (0 = sistema):",                                          \
      L"Zeilen pro Mausrad-Raste (0 = System):",                                                   \
      L"Lignes par cran de molette (0 = système) :",                                               \
      L"Sorok görgőkattanásonként (0 = rendszer):")                                                \
    /* button text must stay within ~130 DLU (MainWindow.cpp Options layout) */                    \
    X(OptClearRecent, L"Clear recent files and pairs", L"Svuota gli elenchi recenti",              \
      L"Zuletzt verwendete Listen leeren",                                                         \
      L"Vider les fichiers et paires récents",                                                     \
      L"Legutóbbi fájlok és párok törlése")                                                        \
    /* explorer context-menu verbs (written to the registry at registration) */                    \
    X(VerbOpenLeft, L"Open left in PDF Side Viewer", L"Apri a sinistra in PDF Side Viewer",        \
      L"Links in PDF Side Viewer öffnen", L"Ouvrir à gauche dans PDF Side Viewer",                 \
      L"Megnyitás balra a PDF Side Viewerben")                                                     \
    X(VerbOpenRight, L"Open right in PDF Side Viewer", L"Apri a destra in PDF Side Viewer",        \
      L"Rechts in PDF Side Viewer öffnen", L"Ouvrir à droite dans PDF Side Viewer",                \
      L"Megnyitás jobbra a PDF Side Viewerben")                                                    \
    /* go-to-page dialog + shared dialog buttons */                                                \
    X(GotoTitle, L"Go to Page", L"Vai alla pagina", L"Gehe zu Seite", L"Aller à la page",          \
      L"Ugrás oldalra")                                                                            \
    X(GotoPrompt, L"Page number or label:", L"Numero di pagina o etichetta:",                      \
      L"Seitenzahl oder Bezeichnung:", L"Numéro de page ou étiquette :",                           \
      L"Oldalszám vagy címke:")                                                                    \
    X(DlgOk, L"OK", L"OK", L"OK", L"OK", L"OK")                                                    \
    X(DlgCancel, L"Cancel", L"Annulla", L"Abbrechen", L"Annuler", L"Mégse")                        \
    /* sync points dialog */                                                                       \
    X(SyncPtsDlgTitle, L"Sync Points", L"Punti di sincronizzazione", L"Sync-Punkte",               \
      L"Points de synchronisation", L"Szinkronpontok")                                             \
    X(SyncPtsColIndex, L"#", L"#", L"#", L"#", L"#")                                               \
    X(SyncPtsColNumbering, L"Numbering", L"Numerazione", L"Nummerierung", L"Numérotation",         \
      L"Számozás")                                                                                 \
    X(SyncPtsColPages, L"Pages", L"Pagine", L"Seiten", L"Pages", L"Oldalak")                       \
    X(SyncPtsColOrigin, L"Origin", L"Origine", L"Ursprung", L"Origine", L"Eredet")                 \
    X(SyncPtsDlgRemove, L"&Remove", L"&Rimuovi", L"&Entfernen", L"&Supprimer",                     \
      L"&Eltávolítás")                                                                             \
    X(SyncPtsDlgClear, L"Clear &All", L"Rimuovi &tutti", L"&Alle entfernen",                       \
      L"&Tout supprimer", L"Összes &törlése")                                                      \
    X(DlgClose, L"Close", L"Chiudi", L"Schließen", L"Fermer", L"Bezárás")                          \
    X(SyncPtOriginAuto, L"auto", L"auto", L"automatisch", L"auto", L"automatikus")                 \
    X(SyncPtOriginManual, L"manual", L"manuale", L"manuell", L"manuel", L"kézi")                   \
    /* about box */                                                                                \
    X(AboutTitle, L"About PDF Side Viewer", L"Informazioni su PDF Side Viewer",                    \
      L"Über PDF Side Viewer", L"À propos de PDF Side Viewer",                                     \
      L"A PDF Side Viewer névjegye")                                                               \
    X(AboutBody,                                                                                   \
      L"Two PDFs side by side with synchronized scrolling.\n\n"                                    \
      L"(c) 2026 Lorenzo Novara - GPLv3\nPDF rendering: MuPDF (AGPLv3) by Artifex Software",       \
      L"Due PDF affiancati con scorrimento sincronizzato.\n\n"                                     \
      L"(c) 2026 Lorenzo Novara - GPLv3\nRendering PDF: MuPDF (AGPLv3) di Artifex Software",       \
      L"Zwei PDFs nebeneinander mit synchronisiertem Bildlauf.\n\n"                                \
      L"(c) 2026 Lorenzo Novara - GPLv3\nPDF-Rendering: MuPDF (AGPLv3) von Artifex Software",      \
      L"Deux PDF côte à côte avec défilement synchronisé.\n\n"                                     \
      L"(c) 2026 Lorenzo Novara - GPLv3\nRendu PDF : MuPDF (AGPLv3) par Artifex Software",         \
      L"Két PDF egymás mellett, szinkronizált görgetéssel.\n\n"                                    \
      L"(c) 2026 Lorenzo Novara - GPLv3\nPDF-megjelenítés: MuPDF (AGPLv3), Artifex Software")

// Order = the per-language table order in Strings.cpp = the persisted-code
// order ("en","it","de","fr","hu") = the language menu's radio-item order
// (IDC_LANG_ENGLISH + i, MainWindow.h). Append only.
enum class Lang { English = 0, Italian = 1, German = 2, French = 3, Hungarian = 4 };
constexpr int kLangCount = 5;

enum class StrId : size_t {
#define PSV_AS_ENUM(id, en, it, de, fr, hu) id,
    PSV_STRING_LIST(PSV_AS_ENUM)
#undef PSV_AS_ENUM
        Count,
};

// UI-thread only (like all UI state): panes and frame read strings while
// painting and building menus; workers never touch them.
void SetUiLanguage(Lang lang);
Lang UiLanguage();
PCWSTR Str(StrId id);

// Two-letter codes persisted in settings.ini ("en"/"it"/"de"/"fr"/"hu");
// anything unknown falls back to English.
Lang LangFromCode(const std::wstring& code);
PCWSTR LangCode(Lang lang);
