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
// column's "Ctrl+Più". Ukrainian and Greek mnemonics are Cyrillic/Greek
// letters (the Windows convention for localized software); their modifier key
// names stay Latin (Ctrl/Shift/Alt), only Plus/Minus localize
// (Плюс/Мінус, Συν/Πλην, ES Más/Menos, NL Plus/Min).
//
// X(id, english, italian, german, french, hungarian, ukrainian, romanian,
//   portuguese, greek, spanish, polish, dutch, czech, swedish) - English is
//   British English (en-GB: synchronised); Portuguese is European Portuguese
//   (pt-PT: Ficheiro/Ecrã); Spanish is European Spanish (es-ES: Aceptar);
//   Czech keeps the classic Windows "Storno" for Cancel.
#define PSV_STRING_LIST(X)                                                                         \
    /* menu bar */                                                                                 \
    X(MenuFile, L"&File", L"&File", L"&Datei", L"&Fichier", L"&Fájl", L"&Файл", L"&Fișier",        \
      L"&Ficheiro", L"&Αρχείο", L"&Archivo", L"&Plik", L"&Bestand", L"&Soubor", L"&Arkiv")         \
    X(MenuOpenLeft, L"Open &Left...\tCtrl+O", L"Apri a &sinistra...\tCtrl+O",                      \
      L"&Links öffnen...\tStrg+O", L"Ouvrir à &gauche...\tCtrl+O",                                 \
      L"Megnyitás &balra...\tCtrl+O", L"Відкрити &ліворуч...\tCtrl+O",                             \
      L"Deschide la &stânga...\tCtrl+O", L"Abrir à &esquerda...\tCtrl+O",                          \
      L"Άνοιγμα &αριστερά...\tCtrl+O", L"Abrir a la &izquierda...\tCtrl+O",                        \
      L"Otwórz po &lewej...\tCtrl+O", L"&Links openen...\tCtrl+O",                                 \
      L"Otevřít v&levo...\tCtrl+O", L"Öppna till &vänster...\tCtrl+O")                             \
    X(MenuOpenRight, L"Open &Right...\tCtrl+Shift+O", L"Apri a &destra...\tCtrl+Shift+O",          \
      L"&Rechts öffnen...\tStrg+Umschalt+O", L"Ouvrir à &droite...\tCtrl+Maj+O",                   \
      L"Megnyitás &jobbra...\tCtrl+Shift+O", L"Відкрити &праворуч...\tCtrl+Shift+O",               \
      L"Deschide la &dreapta...\tCtrl+Shift+O", L"Abrir à &direita...\tCtrl+Shift+O",              \
      L"Άνοιγμα &δεξιά...\tCtrl+Shift+O", L"Abrir a la &derecha...\tCtrl+Shift+O",                 \
      L"Otwórz po &prawej...\tCtrl+Shift+O", L"&Rechts openen...\tCtrl+Shift+O",                   \
      L"Otevřít v&pravo...\tCtrl+Shift+O", L"Öppna till &höger...\tCtrl+Shift+O")                  \
    X(MenuCloseDoc, L"&Close\tCtrl+W", L"C&hiudi\tCtrl+W", L"S&chließen\tStrg+W",                  \
      L"&Fermer\tCtrl+W", L"Be&zárás\tCtrl+W", L"&Закрити\tCtrl+W", L"În&chide\tCtrl+W",           \
      L"Fe&char\tCtrl+W", L"&Κλείσιμο\tCtrl+W", L"&Cerrar\tCtrl+W", L"&Zamknij\tCtrl+W",           \
      L"&Sluiten\tCtrl+W", L"&Zavřít\tCtrl+W", L"&Stäng\tCtrl+W")                                  \
    X(MenuRecentFiles, L"Recent &Files", L"File recen&ti", L"&Zuletzt verwendete Dateien",         \
      L"Fichiers &récents", L"&Legutóbbi fájlok", L"Останні &файли", L"Fișiere &recente",          \
      L"Ficheiros &recentes", L"Πρόσφατα αρ&χεία", L"Archivos &recientes",                         \
      L"Ostatnie plik&i", L"Recente &bestanden", L"&Nedávné soubory", L"Senaste &filer")           \
    X(MenuRecentPairs, L"Recent Pa&irs", L"&Coppie recenti", L"Zuletzt verwendete &Paare",         \
      L"&Paires récentes", L"Legutóbbi &párok", L"Останні па&ри", L"&Perechi recente",             \
      L"&Pares recentes", L"Πρόσφατα &ζεύγη", L"&Pares recientes", L"Ostatnie pa&ry",              \
      L"Recente &paren", L"Nedávné &dvojice", L"Senaste &par")                                     \
    X(MenuMruEmpty, L"(empty)", L"(vuoto)", L"(leer)", L"(vide)", L"(üres)", L"(порожньо)",        \
      L"(gol)", L"(vazio)", L"(κενό)", L"(vacío)", L"(pusto)", L"(leeg)", L"(prázdné)",            \
      L"(tomt)")                                                                                   \
    X(MruMissingFile, L"File not found:\n", L"File non trovato:\n",                                \
      L"Datei nicht gefunden:\n", L"Fichier introuvable :\n", L"A fájl nem található:\n",          \
      L"Файл не знайдено:\n", L"Fișierul nu a fost găsit:\n",                                      \
      L"Ficheiro não encontrado:\n", L"Το αρχείο δεν βρέθηκε:\n",                                  \
      L"Archivo no encontrado:\n", L"Nie znaleziono pliku:\n",                                     \
      L"Bestand niet gevonden:\n", L"Soubor nenalezen:\n", L"Filen hittades inte:\n")              \
    X(MenuOptions, L"&Options...", L"&Opzioni...", L"&Optionen...", L"&Options...",                \
      L"Beállí&tások...", L"П&араметри...", L"&Opțiuni...", L"&Opções...",                         \
      L"&Επιλογές...", L"&Opciones...", L"&Opcje...", L"&Opties...", L"&Možnosti...",              \
      L"Al&ternativ...")                                                                           \
    X(MenuExit, L"E&xit\tAlt+F4", L"&Esci\tAlt+F4", L"&Beenden\tAlt+F4", L"&Quitter\tAlt+F4",      \
      L"&Kilépés\tAlt+F4", L"Ви&йти\tAlt+F4", L"&Ieșire\tAlt+F4", L"&Sair\tAlt+F4",                \
      L"Έ&ξοδος\tAlt+F4", L"&Salir\tAlt+F4", L"Za&kończ\tAlt+F4", L"&Afsluiten\tAlt+F4",           \
      L"&Konec\tAlt+F4", L"&Avsluta\tAlt+F4")                                                      \
    X(MenuView, L"&View", L"&Visualizza", L"&Ansicht", L"&Affichage", L"&Nézet", L"&Вигляд",       \
      L"&Vizualizare", L"&Ver", L"&Προβολή", L"&Ver", L"&Widok", L"B&eeld", L"&Zobrazit",          \
      L"&Visa")                                                                                    \
    X(MenuToolbar, L"&Toolbar", L"Barra degli &strumenti", L"&Symbolleiste",                       \
      L"Barre d'&outils", L"&Eszköztár", L"Панель &інструментів", L"Bară de &instrumente",         \
      L"Barra de &ferramentas", L"Γραμμή εργα&λείων", L"Barra de &herramientas",                   \
      L"Pasek &narzędzi", L"&Werkbalk", L"Panel &nástrojů", L"&Verktygsfält")                      \
    X(MenuStatusBar, L"&Status Bar", L"Barra di s&tato", L"S&tatusleiste", L"Barre d'é&tat",       \
      L"Á&llapotsor", L"Рядок с&тану", L"Bară de s&tare", L"Barra de es&tado",                     \
      L"Γραμμή κατά&στασης", L"Barra de es&tado", L"Pasek s&tanu", L"&Statusbalk",                 \
      L"S&tavový řádek", L"&Statusfält")                                                           \
    X(MenuOutline, L"&Outline\tF9", L"Se&gnalibri\tF9", L"&Lesezeichen\tF9", L"&Signets\tF9",      \
      L"&Könyvjelzők\tF9", L"&Закладки\tF9", L"&Marcaje\tF9", L"&Marcadores\tF9",                  \
      L"Σελιδο&δείκτες\tF9", L"&Marcadores\tF9", L"&Zakładki\tF9", L"&Bladwijzers\tF9",            \
      L"&Záložky\tF9", L"&Bokmärken\tF9")                                                          \
    X(MenuLockToolbars, L"Loc&k the Toolbars", L"&Blocca le barre degli strumenti",                \
      L"Symbolleisten &fixieren", L"&Verrouiller les barres d'outils",                             \
      L"Eszköztárak &zárolása", L"Заблок&увати панелі інструментів",                               \
      L"&Blochează barele de instrumente", L"&Bloquear as barras de ferramentas",                  \
      L"Κλείδ&ωμα γραμμών εργαλείων", L"&Bloquear las barras de herramientas",                     \
      L"Za&blokuj paski narzędzi", L"Werkbalken ver&grendelen",                                    \
      L"Zamknout panely nástro&jů", L"&Lås verktygsfälten")                                        \
    X(MenuZoomIn, L"Zoom &In\tCtrl+Plus", L"&Ingrandisci\tCtrl+Più",                               \
      L"Ver&größern\tStrg+Plus", L"&Zoom avant\tCtrl+Plus", L"&Nagyítás\tCtrl+Plusz",              \
      L"З&більшити\tCtrl+Плюс", L"Mărir&e\tCtrl+Plus", L"&Ampliar\tCtrl+Mais",                     \
      L"&Μεγέθυνση\tCtrl+Συν", L"&Ampliar\tCtrl+Más", L"&Powiększ\tCtrl+Plus",                     \
      L"&Inzoomen\tCtrl+Plus", L"&Přiblížit\tCtrl+Plus", L"Zooma &in\tCtrl+Plus")                  \
    X(MenuZoomOut, L"Zoom O&ut\tCtrl+Minus", L"&Riduci\tCtrl+Meno",                                \
      L"Ver&kleinern\tStrg+Minus", L"Zoom arr&ière\tCtrl+Moins",                                   \
      L"K&icsinyítés\tCtrl+Mínusz", L"З&меншити\tCtrl+Мінус", L"Mi&cșorare\tCtrl+Minus",           \
      L"&Reduzir\tCtrl+Menos", L"Σμίκρ&υνση\tCtrl+Πλην", L"&Reducir\tCtrl+Menos",                  \
      L"Po&mniejsz\tCtrl+Minus", L"&Uitzoomen\tCtrl+Min", L"&Oddálit\tCtrl+Minus",                 \
      L"Zooma &ut\tCtrl+Minus")                                                                    \
    X(MenuActualSize, L"&Actual Size\tCtrl+0", L"Dimensioni &effettive\tCtrl+0",                   \
      L"&Originalgröße\tStrg+0", L"Taille &réelle\tCtrl+0", L"&Tényleges méret\tCtrl+0",           \
      L"&Фактичний розмір\tCtrl+0", L"Dimensiune &reală\tCtrl+0", L"Tamanho r&eal\tCtrl+0",        \
      L"Πραγματικό μέγε&θος\tCtrl+0", L"Tamaño r&eal\tCtrl+0",                                     \
      L"&Rzeczywisty rozmiar\tCtrl+0", L"Werkelijke gr&ootte\tCtrl+0",                             \
      L"Sk&utečná velikost\tCtrl+0", L"Ver&klig storlek\tCtrl+0")                                  \
    X(MenuFitWidth, L"Fit &Width\tCtrl+2", L"Adatta &larghezza\tCtrl+2",                           \
      L"An &Breite anpassen\tStrg+2", L"Ajuster à la &largeur\tCtrl+2",                            \
      L"Igazítás &szélességhez\tCtrl+2", L"За &шириною\tCtrl+2",                                   \
      L"Potri&vire la lățime\tCtrl+2", L"Ajustar à &largura\tCtrl+2",                              \
      L"Προσαρμογή στο &πλάτος\tCtrl+2", L"Ajustar al a&ncho\tCtrl+2",                             \
      L"Dopasuj do &szerokości\tCtrl+2", L"Aanpassen aan bree&dte\tCtrl+2",                        \
      L"Přizpůsobit &šířce\tCtrl+2", L"Anpassa till b&redd\tCtrl+2")                               \
    X(MenuFitPage, L"Fit &Page\tCtrl+3", L"Adatta &pagina\tCtrl+3",                                \
      L"An S&eite anpassen\tStrg+3", L"Ajuster à la &page\tCtrl+3",                                \
      L"Igazítás &oldalhoz\tCtrl+3", L"&Ціла сторінка\tCtrl+3",                                    \
      L"&Potrivire la pagină\tCtrl+3", L"Ajustar à &página\tCtrl+3",                               \
      L"Προσαρμογή στη σ&ελίδα\tCtrl+3", L"Ajustar a la &página\tCtrl+3",                          \
      L"Dopasuj do stron&y\tCtrl+3", L"Aanpassen aan p&agina\tCtrl+3",                             \
      L"Přizpůsobit &stránce\tCtrl+3", L"Anpassa till si&da\tCtrl+3")                              \
    X(MenuScrollContinuous, L"&Continuous Scrolling\tCtrl+4", L"Scorrimento c&ontinuo\tCtrl+4",    \
      L"Fortlaufender B&ildlauf\tStrg+4", L"Défilement &continu\tCtrl+4",                          \
      L"&Folyamatos görgetés\tCtrl+4", L"Безперервне про&кручування\tCtrl+4",                      \
      L"Derulare c&ontinuă\tCtrl+4", L"Deslocamento c&ontínuo\tCtrl+4",                            \
      L"Συνε&χής κύλιση\tCtrl+4", L"Desplazamiento c&ontinuo\tCtrl+4",                             \
      L"Przewijanie &ciągłe\tCtrl+4", L"Doorlopend s&crollen\tCtrl+4",                             \
      L"Pl&ynulé posouvání\tCtrl+4", L"Kontinuerlig rullnin&g\tCtrl+4")                            \
    X(MenuScrollPaged, L"Page-&by-Page\tCtrl+5", L"Pagina per pagi&na\tCtrl+5",                    \
      L"Seiten&weise\tStrg+5", L"Page par pa&ge\tCtrl+5",                                          \
      L"Oldalankénti gö&rgetés\tCtrl+5", L"&Посторінково\tCtrl+5",                                 \
      L"Pa&gină cu pagină\tCtrl+5", L"Página a pá&gina\tCtrl+5",                                   \
      L"Σελίδα-σελίδ&α\tCtrl+5", L"Pá&gina a página\tCtrl+5",                                      \
      L"Strona po stron&ie\tCtrl+5", L"Pagina per pagi&na\tCtrl+5",                                \
      L"Str&ánka po stránce\tCtrl+5", L"Sida &för sida\tCtrl+5")                                   \
    X(MenuGotoPage, L"&Go to Page...\tCtrl+G", L"&Vai alla pagina...\tCtrl+G",                     \
      L"Gehe &zu Seite...\tStrg+G", L"&Aller à la page...\tCtrl+G",                                \
      L"&Ugrás oldalra...\tCtrl+G", L"Пере&йти до сторінки...\tCtrl+G",                            \
      L"&Salt la pagină...\tCtrl+G", L"&Ir para a página...\tCtrl+G",                              \
      L"Μετά&βαση σε σελίδα...\tCtrl+G", L"&Ir a la página...\tCtrl+G",                            \
      L"Przej&dź do strony...\tCtrl+G", L"Ga naa&r pagina...\tCtrl+G",                             \
      L"Př&ejít na stránku...\tCtrl+G", L"Gå till sid&a...\tCtrl+G")                               \
    X(MenuLanguage, L"&Language", L"Ling&ua", L"Sp&rache", L"La&ngue", L"N&yelv", L"Мо&ва",        \
      L"&Limbă", L"I&dioma", L"&Γλώσσα", L"I&dioma", L"&Język", L"&Taal", L"Jazy&k",               \
      L"S&pråk")                                                                                   \
    X(MenuLangEnglish, L"English", L"English", L"English", L"English", L"English",                 \
      L"English", L"English", L"English", L"English", L"English", L"English", L"English",          \
      L"English", L"English")                                                                      \
    X(MenuLangItalian, L"Italiano", L"Italiano", L"Italiano", L"Italiano", L"Italiano",            \
      L"Italiano", L"Italiano", L"Italiano", L"Italiano", L"Italiano", L"Italiano",                \
      L"Italiano", L"Italiano", L"Italiano")                                                       \
    X(MenuLangGerman, L"Deutsch", L"Deutsch", L"Deutsch", L"Deutsch", L"Deutsch",                  \
      L"Deutsch", L"Deutsch", L"Deutsch", L"Deutsch", L"Deutsch", L"Deutsch", L"Deutsch",          \
      L"Deutsch", L"Deutsch")                                                                      \
    X(MenuLangFrench, L"Français", L"Français", L"Français", L"Français", L"Français",             \
      L"Français", L"Français", L"Français", L"Français", L"Français", L"Français",                \
      L"Français", L"Français", L"Français")                                                       \
    X(MenuLangHungarian, L"Magyar", L"Magyar", L"Magyar", L"Magyar", L"Magyar", L"Magyar",         \
      L"Magyar", L"Magyar", L"Magyar", L"Magyar", L"Magyar", L"Magyar", L"Magyar",                 \
      L"Magyar")                                                                                   \
    X(MenuLangUkrainian, L"Українська", L"Українська", L"Українська", L"Українська",               \
      L"Українська", L"Українська", L"Українська", L"Українська", L"Українська",                   \
      L"Українська", L"Українська", L"Українська", L"Українська", L"Українська")                   \
    X(MenuLangRomanian, L"Română", L"Română", L"Română", L"Română", L"Română", L"Română",          \
      L"Română", L"Română", L"Română", L"Română", L"Română", L"Română", L"Română",                 \
      L"Română")                                                                                   \
    X(MenuLangPortuguese, L"Português", L"Português", L"Português", L"Português",                  \
      L"Português", L"Português", L"Português", L"Português", L"Português", L"Português",          \
      L"Português", L"Português", L"Português", L"Português")                                      \
    X(MenuLangGreek, L"Ελληνικά", L"Ελληνικά", L"Ελληνικά", L"Ελληνικά", L"Ελληνικά",              \
      L"Ελληνικά", L"Ελληνικά", L"Ελληνικά", L"Ελληνικά", L"Ελληνικά", L"Ελληνικά",                \
      L"Ελληνικά", L"Ελληνικά", L"Ελληνικά")                                                       \
    X(MenuLangSpanish, L"Español", L"Español", L"Español", L"Español", L"Español",                 \
      L"Español", L"Español", L"Español", L"Español", L"Español", L"Español", L"Español",          \
      L"Español", L"Español")                                                                      \
    X(MenuLangPolish, L"Polski", L"Polski", L"Polski", L"Polski", L"Polski", L"Polski",            \
      L"Polski", L"Polski", L"Polski", L"Polski", L"Polski", L"Polski", L"Polski",                 \
      L"Polski")                                                                                   \
    X(MenuLangDutch, L"Nederlands", L"Nederlands", L"Nederlands", L"Nederlands",                   \
      L"Nederlands", L"Nederlands", L"Nederlands", L"Nederlands", L"Nederlands",                   \
      L"Nederlands", L"Nederlands", L"Nederlands", L"Nederlands", L"Nederlands")                   \
    X(MenuLangCzech, L"Čeština", L"Čeština", L"Čeština", L"Čeština", L"Čeština",                   \
      L"Čeština", L"Čeština", L"Čeština", L"Čeština", L"Čeština", L"Čeština", L"Čeština",          \
      L"Čeština", L"Čeština")                                                                      \
    X(MenuLangSwedish, L"Svenska", L"Svenska", L"Svenska", L"Svenska", L"Svenska",                 \
      L"Svenska", L"Svenska", L"Svenska", L"Svenska", L"Svenska", L"Svenska", L"Svenska",          \
      L"Svenska", L"Svenska")                                                                      \
    X(MenuFullScreen, L"&Full Screen\tF11", L"S&chermo intero\tF11", L"&Vollbild\tF11",            \
      L"Plein &écran\tF11", L"Tel&jes képernyő\tF11", L"На весь &екран\tF11",                      \
      L"Ecr&an complet\tF11", L"Ecrã i&nteiro\tF11", L"Πλήρης οθό&νη\tF11",                        \
      L"Pantalla &completa\tF11", L"Pełny &ekran\tF11", L"&Volledig scherm\tF11",                  \
      L"Celá o&brazovka\tF11", L"&Helskärm\tF11")                                                  \
    X(MenuSync, L"S&ync", L"&Sincronizzazione", L"&Synchronisierung", L"&Synchronisation",         \
      L"&Szinkronizálás", L"&Синхронізація", L"&Sincronizare", L"&Sincronização",                  \
      L"&Συγχρονισμός", L"&Sincronización", L"&Synchronizacja", L"&Synchronisatie",                \
      L"S&ynchronizace", L"&Synkronisering")                                                       \
    X(MenuScrollSync, L"&Scroll Sync\tF7", L"Sync &scorrimento\tF7", L"&Bildlauf-Sync\tF7",        \
      L"Sync du &défilement\tF7", L"&Görgetés-szinkron\tF7",                                       \
      L"Синхронізація &прокручування\tF7", L"Sincronizare &derulare\tF7",                          \
      L"Sincronização do &deslocamento\tF7", L"Συγχρονισμός &κύλισης\tF7",                         \
      L"Sincronización del &desplazamiento\tF7", L"Synchronizacja &przewijania\tF7",               \
      L"&Scrollsynchronisatie\tF7", L"Synchronizace &posouvání\tF7",                               \
      L"&Rullningssynk\tF7")                                                                       \
    X(MenuZoomSync, L"&Zoom Sync\tCtrl+F7", L"Sync &zoom\tCtrl+F7", L"&Zoom-Sync\tStrg+F7",        \
      L"Sync du &zoom\tCtrl+F7", L"&Zoom-szinkron\tCtrl+F7",                                       \
      L"Синхронізація &масштабу\tCtrl+F7", L"Sincronizare &zoom\tCtrl+F7",                         \
      L"Sincronização do &zoom\tCtrl+F7", L"Συγχρονισμός &ζουμ\tCtrl+F7",                          \
      L"Sincronización del &zoom\tCtrl+F7", L"Synchronizacja po&większenia\tCtrl+F7",              \
      L"&Zoomsynchronisatie\tCtrl+F7", L"Synchronizace při&blížení\tCtrl+F7",                      \
      L"&Zoomsynk\tCtrl+F7")                                                                       \
    X(MenuAddSyncPoint, L"&Add Sync Point Here\tShift+F7",                                         \
      L"&Aggiungi punto di sync qui\tShift+F7",                                                    \
      L"Sync-Punkt hier hinzu&fügen\tUmschalt+F7",                                                 \
      L"&Ajouter un point de sync ici\tMaj+F7",                                                    \
      L"Szinkronpont &hozzáadása itt\tShift+F7",                                                   \
      L"&Додати точку синхронізації тут\tShift+F7",                                                \
      L"&Adaugă punct de sincronizare aici\tShift+F7",                                             \
      L"&Adicionar ponto de sincronização aqui\tShift+F7",                                         \
      L"&Προσθήκη σημείου συγχρονισμού εδώ\tShift+F7",                                             \
      L"&Añadir punto de sincronización aquí\tShift+F7",                                           \
      L"&Dodaj punkt synchronizacji tutaj\tShift+F7",                                              \
      L"Synchronisatiepunt &hier toevoegen\tShift+F7",                                             \
      L"Přidat synchronizační bod &sem\tShift+F7",                                                 \
      L"Lägg till synkpunkt &här\tShift+F7")                                                       \
    X(MenuSyncFromBookmarks, L"Sync Points from &Bookmarks", L"Punti di sync dai se&gnalibri",     \
      L"Sync-Punkte aus &Lesezeichen", L"Points de sync depuis les &signets",                      \
      L"Szinkronpontok a &könyvjelzőkből", L"Точки синхронізації із &закладок",                    \
      L"Puncte de sincronizare din &marcaje", L"Pontos de sincronização dos &marcadores",          \
      L"Σημεία συγχρονισμού από σελιδο&δείκτες",                                                   \
      L"Puntos de sincronización desde &marcadores",                                               \
      L"Punkty synchronizacji z &zakładek",                                                        \
      L"Synchronisatiepunten uit &bladwijzers",                                                    \
      L"Synchronizační body ze &záložek",                                                          \
      L"Synkpunkter från &bokmärken")                                                              \
    X(MenuSyncPoints, L"Sync &Points...", L"&Punti di sync...", L"Sync-&Punkte...",                \
      L"&Points de sync...", L"Szinkron&pontok...", L"&Точки синхронізації...",                    \
      L"&Puncte de sincronizare...", L"&Pontos de sincronização...",                               \
      L"&Σημεία συγχρονισμού...", L"&Puntos de sincronización...",                                 \
      L"Punkty &synchronizacji...", L"Synchronisatie&punten...",                                   \
      L"Synchronizační bod&y...", L"Synk&punkter...")                                              \
    X(MenuClearSyncPoints, L"&Clear Sync Points\tCtrl+Shift+F7",                                   \
      L"&Rimuovi punti di sync\tCtrl+Shift+F7",                                                    \
      L"Sync-Punkte &entfernen\tStrg+Umschalt+F7",                                                 \
      L"&Effacer les points de sync\tCtrl+Maj+F7",                                                 \
      L"Szinkronpontok &törlése\tCtrl+Shift+F7",                                                   \
      L"&Очистити точки синхронізації\tCtrl+Shift+F7",                                             \
      L"Șter&ge punctele de sincronizare\tCtrl+Shift+F7",                                          \
      L"&Limpar pontos de sincronização\tCtrl+Shift+F7",                                           \
      L"Εκκα&θάριση σημείων συγχρονισμού\tCtrl+Shift+F7",                                          \
      L"&Borrar puntos de sincronización\tCtrl+Shift+F7",                                          \
      L"Wy&czyść punkty synchronizacji\tCtrl+Shift+F7",                                            \
      L"Synchronisatiepunten &wissen\tCtrl+Shift+F7",                                              \
      L"&Vymazat synchronizační body\tCtrl+Shift+F7",                                              \
      L"Re&nsa synkpunkter\tCtrl+Shift+F7")                                                        \
    X(MenuAlignmentGaps, L"Show Alignment &Gaps", L"&Mostra spazi di allineamento",                \
      L"&Ausrichtungslücken anzeigen", L"Afficher les espaces d'ali&gnement",                      \
      L"&Igazítási hézagok megjelenítése", L"Показувати проміжки &вирівнювання",                   \
      L"Afișează &spațiile de aliniere", L"Mostrar &espaços de alinhamento",                       \
      L"&Εμφάνιση κενών στοίχισης", L"Mostrar &espacios de alineación",                            \
      L"Pokaż odstępy wyrówn&ania", L"&Uitlijningsruimten tonen",                                  \
      L"Zobrazit zarovnávací &mezery", L"Visa &justeringsmellanrum")                               \
    X(MenuSwapPanes, L"S&wap Panes\tF8", L"Scam&bia pannelli\tF8", L"Bereiche &tauschen\tF8",      \
      L"Éc&hanger les panneaux\tF8", L"Panelek &felcserélése\tF8",                                 \
      L"Поміняти панелі міс&цями\tF8", L"&Inversează panourile\tF8",                               \
      L"&Trocar painéis\tF8", L"Ενα&λλαγή πλαισίων\tF8", L"&Intercambiar paneles\tF8",             \
      L"Za&mień panele\tF8", L"&Deelvensters wisselen\tF8", L"P&rohodit panely\tF8",               \
      L"&Växla paneler\tF8")                                                                       \
    X(MenuHelp, L"&Help", L"&?", L"&Hilfe", L"Aid&e", L"Sú&gó", L"&Довідка", L"&Ajutor",           \
      L"&Ajuda", L"&Βοήθεια", L"Ay&uda", L"Pomo&c", L"&Help", L"&Nápověda", L"&Hjälp")             \
    X(MenuAbout, L"&About PDF Side Viewer...", L"&Informazioni su PDF Side Viewer...",             \
      L"&Über PDF Side Viewer...", L"&À propos de PDF Side Viewer...",                             \
      L"&A PDF Side Viewer névjegye...", L"&Про PDF Side Viewer...",                               \
      L"&Despre PDF Side Viewer...", L"&Acerca do PDF Side Viewer...",                             \
      L"&Πληροφορίες για το PDF Side Viewer...", L"&Acerca de PDF Side Viewer...",                 \
      L"&Informacje o PDF Side Viewer...", L"&Info over PDF Side Viewer...",                       \
      L"&O aplikaci PDF Side Viewer...", L"&Om PDF Side Viewer...")                                \
    /* toolbar tooltips */                                                                         \
    X(TipOpenLeft, L"Open left (Ctrl+O)", L"Apri a sinistra (Ctrl+O)",                             \
      L"Links öffnen (Strg+O)", L"Ouvrir à gauche (Ctrl+O)", L"Megnyitás balra (Ctrl+O)",          \
      L"Відкрити ліворуч (Ctrl+O)", L"Deschide la stânga (Ctrl+O)",                                \
      L"Abrir à esquerda (Ctrl+O)", L"Άνοιγμα αριστερά (Ctrl+O)",                                  \
      L"Abrir a la izquierda (Ctrl+O)", L"Otwórz po lewej (Ctrl+O)",                               \
      L"Links openen (Ctrl+O)", L"Otevřít vlevo (Ctrl+O)",                                         \
      L"Öppna till vänster (Ctrl+O)")                                                              \
    X(TipOpenRight, L"Open right (Ctrl+Shift+O)", L"Apri a destra (Ctrl+Shift+O)",                 \
      L"Rechts öffnen (Strg+Umschalt+O)", L"Ouvrir à droite (Ctrl+Maj+O)",                         \
      L"Megnyitás jobbra (Ctrl+Shift+O)", L"Відкрити праворуч (Ctrl+Shift+O)",                     \
      L"Deschide la dreapta (Ctrl+Shift+O)", L"Abrir à direita (Ctrl+Shift+O)",                    \
      L"Άνοιγμα δεξιά (Ctrl+Shift+O)", L"Abrir a la derecha (Ctrl+Shift+O)",                       \
      L"Otwórz po prawej (Ctrl+Shift+O)", L"Rechts openen (Ctrl+Shift+O)",                         \
      L"Otevřít vpravo (Ctrl+Shift+O)", L"Öppna till höger (Ctrl+Shift+O)")                        \
    X(TipScrollSync, L"Scroll sync (F7)", L"Sync scorrimento (F7)", L"Bildlauf-Sync (F7)",         \
      L"Sync du défilement (F7)", L"Görgetés-szinkron (F7)",                                       \
      L"Синхронізація прокручування (F7)", L"Sincronizare derulare (F7)",                          \
      L"Sincronização do deslocamento (F7)", L"Συγχρονισμός κύλισης (F7)",                         \
      L"Sincronización del desplazamiento (F7)", L"Synchronizacja przewijania (F7)",               \
      L"Scrollsynchronisatie (F7)", L"Synchronizace posouvání (F7)",                               \
      L"Rullningssynk (F7)")                                                                       \
    X(TipZoomSync, L"Zoom sync (Ctrl+F7)", L"Sync zoom (Ctrl+F7)", L"Zoom-Sync (Strg+F7)",         \
      L"Sync du zoom (Ctrl+F7)", L"Zoom-szinkron (Ctrl+F7)",                                       \
      L"Синхронізація масштабу (Ctrl+F7)", L"Sincronizare zoom (Ctrl+F7)",                         \
      L"Sincronização do zoom (Ctrl+F7)", L"Συγχρονισμός ζουμ (Ctrl+F7)",                          \
      L"Sincronización del zoom (Ctrl+F7)", L"Synchronizacja powiększenia (Ctrl+F7)",              \
      L"Zoomsynchronisatie (Ctrl+F7)", L"Synchronizace přiblížení (Ctrl+F7)",                      \
      L"Zoomsynk (Ctrl+F7)")                                                                       \
    X(TipFitWidth, L"Fit width (Ctrl+2)", L"Adatta larghezza (Ctrl+2)",                            \
      L"An Breite anpassen (Strg+2)", L"Ajuster à la largeur (Ctrl+2)",                            \
      L"Igazítás szélességhez (Ctrl+2)", L"За шириною (Ctrl+2)",                                   \
      L"Potrivire la lățime (Ctrl+2)", L"Ajustar à largura (Ctrl+2)",                              \
      L"Προσαρμογή στο πλάτος (Ctrl+2)", L"Ajustar al ancho (Ctrl+2)",                             \
      L"Dopasuj do szerokości (Ctrl+2)", L"Aanpassen aan breedte (Ctrl+2)",                        \
      L"Přizpůsobit šířce (Ctrl+2)", L"Anpassa till bredd (Ctrl+2)")                               \
    X(TipFitPage, L"Fit page (Ctrl+3)", L"Adatta pagina (Ctrl+3)",                                 \
      L"An Seite anpassen (Strg+3)", L"Ajuster à la page (Ctrl+3)",                                \
      L"Igazítás oldalhoz (Ctrl+3)", L"Ціла сторінка (Ctrl+3)",                                    \
      L"Potrivire la pagină (Ctrl+3)", L"Ajustar à página (Ctrl+3)",                               \
      L"Προσαρμογή στη σελίδα (Ctrl+3)", L"Ajustar a la página (Ctrl+3)",                          \
      L"Dopasuj do strony (Ctrl+3)", L"Aanpassen aan pagina (Ctrl+3)",                             \
      L"Přizpůsobit stránce (Ctrl+3)", L"Anpassa till sida (Ctrl+3)")                              \
    X(TipScrollContinuous, L"Continuous scrolling (Ctrl+4)", L"Scorrimento continuo (Ctrl+4)",     \
      L"Fortlaufender Bildlauf (Strg+4)", L"Défilement continu (Ctrl+4)",                          \
      L"Folyamatos görgetés (Ctrl+4)", L"Безперервне прокручування (Ctrl+4)",                      \
      L"Derulare continuă (Ctrl+4)", L"Deslocamento contínuo (Ctrl+4)",                            \
      L"Συνεχής κύλιση (Ctrl+4)", L"Desplazamiento continuo (Ctrl+4)",                             \
      L"Przewijanie ciągłe (Ctrl+4)", L"Doorlopend scrollen (Ctrl+4)",                             \
      L"Plynulé posouvání (Ctrl+4)", L"Kontinuerlig rullning (Ctrl+4)")                            \
    X(TipScrollPaged, L"Page-by-page (Ctrl+5)", L"Pagina per pagina (Ctrl+5)",                     \
      L"Seitenweise (Strg+5)", L"Page par page (Ctrl+5)",                                          \
      L"Oldalankénti görgetés (Ctrl+5)", L"Посторінково (Ctrl+5)",                                 \
      L"Pagină cu pagină (Ctrl+5)", L"Página a página (Ctrl+5)",                                   \
      L"Σελίδα-σελίδα (Ctrl+5)", L"Página a página (Ctrl+5)",                                      \
      L"Strona po stronie (Ctrl+5)", L"Pagina per pagina (Ctrl+5)",                                \
      L"Stránka po stránce (Ctrl+5)", L"Sida för sida (Ctrl+5)")                                   \
    X(TipActualSize, L"Actual size (Ctrl+0)", L"Dimensioni effettive (Ctrl+0)",                    \
      L"Originalgröße (Strg+0)", L"Taille réelle (Ctrl+0)", L"Tényleges méret (Ctrl+0)",           \
      L"Фактичний розмір (Ctrl+0)", L"Dimensiune reală (Ctrl+0)", L"Tamanho real (Ctrl+0)",        \
      L"Πραγματικό μέγεθος (Ctrl+0)", L"Tamaño real (Ctrl+0)",                                     \
      L"Rzeczywisty rozmiar (Ctrl+0)", L"Werkelijke grootte (Ctrl+0)",                             \
      L"Skutečná velikost (Ctrl+0)", L"Verklig storlek (Ctrl+0)")                                  \
    X(TipFind, L"Find (Ctrl+F)", L"Trova (Ctrl+F)", L"Suchen (Strg+F)",                            \
      L"Rechercher (Ctrl+F)", L"Keresés (Ctrl+F)", L"Пошук (Ctrl+F)", L"Căutare (Ctrl+F)",         \
      L"Localizar (Ctrl+F)", L"Εύρεση (Ctrl+F)", L"Buscar (Ctrl+F)", L"Znajdź (Ctrl+F)",           \
      L"Zoeken (Ctrl+F)", L"Najít (Ctrl+F)", L"Sök (Ctrl+F)")                                      \
    X(TipOutline, L"Outline (F9)", L"Segnalibri (F9)", L"Lesezeichen (F9)", L"Signets (F9)",       \
      L"Könyvjelzők (F9)", L"Закладки (F9)", L"Marcaje (F9)", L"Marcadores (F9)",                  \
      L"Σελιδοδείκτες (F9)", L"Marcadores (F9)", L"Zakładki (F9)", L"Bladwijzers (F9)",            \
      L"Záložky (F9)", L"Bokmärken (F9)")                                                          \
    X(TipFullScreen, L"Full screen (F11)", L"Schermo intero (F11)", L"Vollbild (F11)",             \
      L"Plein écran (F11)", L"Teljes képernyő (F11)", L"На весь екран (F11)",                      \
      L"Ecran complet (F11)", L"Ecrã inteiro (F11)", L"Πλήρης οθόνη (F11)",                        \
      L"Pantalla completa (F11)", L"Pełny ekran (F11)", L"Volledig scherm (F11)",                  \
      L"Celá obrazovka (F11)", L"Helskärm (F11)")                                                  \
    X(TipAddSyncPoint, L"Add sync point here (Shift+F7)",                                          \
      L"Aggiungi punto di sync qui (Shift+F7)",                                                    \
      L"Sync-Punkt hier hinzufügen (Umschalt+F7)",                                                 \
      L"Ajouter un point de sync ici (Maj+F7)",                                                    \
      L"Szinkronpont hozzáadása itt (Shift+F7)",                                                   \
      L"Додати точку синхронізації тут (Shift+F7)",                                                \
      L"Adaugă punct de sincronizare aici (Shift+F7)",                                             \
      L"Adicionar ponto de sincronização aqui (Shift+F7)",                                         \
      L"Προσθήκη σημείου συγχρονισμού εδώ (Shift+F7)",                                             \
      L"Añadir punto de sincronización aquí (Shift+F7)",                                           \
      L"Dodaj punkt synchronizacji tutaj (Shift+F7)",                                              \
      L"Synchronisatiepunt hier toevoegen (Shift+F7)",                                             \
      L"Přidat synchronizační bod sem (Shift+F7)",                                                 \
      L"Lägg till synkpunkt här (Shift+F7)")                                                       \
    X(TipSyncFromBookmarks, L"Sync points from bookmarks", L"Punti di sync dai segnalibri",        \
      L"Sync-Punkte aus Lesezeichen", L"Points de sync depuis les signets",                        \
      L"Szinkronpontok a könyvjelzőkből", L"Точки синхронізації із закладок",                      \
      L"Puncte de sincronizare din marcaje", L"Pontos de sincronização dos marcadores",            \
      L"Σημεία συγχρονισμού από σελιδοδείκτες",                                                    \
      L"Puntos de sincronización desde marcadores", L"Punkty synchronizacji z zakładek",           \
      L"Synchronisatiepunten uit bladwijzers", L"Synchronizační body ze záložek",                  \
      L"Synkpunkter från bokmärken")                                                               \
    X(TipSyncPoints, L"Sync points...", L"Punti di sync...", L"Sync-Punkte...",                    \
      L"Points de sync...", L"Szinkronpontok...", L"Точки синхронізації...",                       \
      L"Puncte de sincronizare...", L"Pontos de sincronização...",                                 \
      L"Σημεία συγχρονισμού...", L"Puntos de sincronización...",                                   \
      L"Punkty synchronizacji...", L"Synchronisatiepunten...",                                     \
      L"Synchronizační body...", L"Synkpunkter...")                                                \
    X(TipClearSyncPoints, L"Clear sync points (Ctrl+Shift+F7)",                                    \
      L"Rimuovi punti di sync (Ctrl+Shift+F7)",                                                    \
      L"Sync-Punkte entfernen (Strg+Umschalt+F7)",                                                 \
      L"Effacer les points de sync (Ctrl+Maj+F7)",                                                 \
      L"Szinkronpontok törlése (Ctrl+Shift+F7)",                                                   \
      L"Очистити точки синхронізації (Ctrl+Shift+F7)",                                             \
      L"Șterge punctele de sincronizare (Ctrl+Shift+F7)",                                          \
      L"Limpar pontos de sincronização (Ctrl+Shift+F7)",                                           \
      L"Εκκαθάριση σημείων συγχρονισμού (Ctrl+Shift+F7)",                                          \
      L"Borrar puntos de sincronización (Ctrl+Shift+F7)",                                          \
      L"Wyczyść punkty synchronizacji (Ctrl+Shift+F7)",                                            \
      L"Synchronisatiepunten wissen (Ctrl+Shift+F7)",                                              \
      L"Vymazat synchronizační body (Ctrl+Shift+F7)",                                              \
      L"Rensa synkpunkter (Ctrl+Shift+F7)")                                                        \
    X(TipAlignmentGaps, L"Show alignment gaps", L"Mostra spazi di allineamento",                   \
      L"Ausrichtungslücken anzeigen", L"Afficher les espaces d'alignement",                        \
      L"Igazítási hézagok megjelenítése", L"Показувати проміжки вирівнювання",                     \
      L"Afișează spațiile de aliniere", L"Mostrar espaços de alinhamento",                         \
      L"Εμφάνιση κενών στοίχισης", L"Mostrar espacios de alineación",                              \
      L"Pokaż odstępy wyrównania", L"Uitlijningsruimten tonen",                                    \
      L"Zobrazit zarovnávací mezery", L"Visa justeringsmellanrum")                                 \
    X(TipSwapPanes, L"Swap panes (F8)", L"Scambia pannelli (F8)", L"Bereiche tauschen (F8)",       \
      L"Échanger les panneaux (F8)", L"Panelek felcserélése (F8)",                                 \
      L"Поміняти панелі місцями (F8)", L"Inversează panourile (F8)",                               \
      L"Trocar painéis (F8)", L"Εναλλαγή πλαισίων (F8)", L"Intercambiar paneles (F8)",             \
      L"Zamień panele (F8)", L"Deelvensters wisselen (F8)", L"Prohodit panely (F8)",               \
      L"Växla paneler (F8)")                                                                       \
    /* toolbar text options (Internet Explorer's exact wording) + short       */                   \
    /* per-button labels for the "below"/"selective on right" modes           */                   \
    X(MenuToolbarTextBelow, L"Sho&w text labels", L"&Mostra etichette di testo",                   \
      L"Te&xtbeschriftungen anzeigen", L"Afficher les libellés de te&xte",                         \
      L"Sz&öveges címkék megjelenítése", L"Відобра&жати текстові підписи",                         \
      L"Afișea&ză etichetele text", L"&Mostrar etiquetas de texto",                                \
      L"Εμφάνιση &ετικετών κειμένου", L"&Mostrar etiquetas de texto",                              \
      L"&Pokaż etykiety tekstowe", L"&Tekstlabels weergeven",                                      \
      L"Zobrazit &textové popisky", L"Visa te&xtetiketter")                                        \
    X(MenuToolbarTextRight, L"Selecti&ve text on right", L"Testo selettivo a &destra",             \
      L"Ausge&wählter Text rechts", L"Texte sélectif à &droite",                                   \
      L"Szelektív szöveg &jobbra", L"Вибірковий текст право&руч",                                  \
      L"Text selectiv la &dreapta", L"Te&xto seletivo à direita",                                  \
      L"Επιλεκτικό κείμενο &δεξιά", L"Te&xto selectivo a la derecha",                              \
      L"Tekst &wybiórczo po prawej", L"Selectieve tekst &rechts",                                  \
      L"Výběrový text v&pravo", L"S&elektiv text till höger")                                      \
    X(MenuToolbarTextNone, L"&No text labels", L"&Nessuna etichetta di testo",                     \
      L"&Keine Textbeschriftungen", L"Auc&un libellé de texte",                                    \
      L"&Nincs szöveges címke", L"&Без текстових підписів", L"&Fără etichete text",                \
      L"&Sem etiquetas de texto", L"&Χωρίς ετικέτες κειμένου",                                     \
      L"&Sin etiquetas de texto", L"Bez etykiet t&ekstowych",                                      \
      L"Geen tekstlabel&s", L"&Bez textových popisků", L"I&nga textetiketter")                     \
    X(LblOpenLeft, L"Left", L"Sinistra", L"Links", L"Gauche", L"Bal", L"Ліворуч", L"Stânga",       \
      L"Esquerda", L"Αριστερά", L"Izquierda", L"Lewy", L"Links", L"Vlevo", L"Vänster")             \
    X(LblOpenRight, L"Right", L"Destra", L"Rechts", L"Droite", L"Jobb", L"Праворуч",               \
      L"Dreapta", L"Direita", L"Δεξιά", L"Derecha", L"Prawy", L"Rechts", L"Vpravo",                \
      L"Höger")                                                                                    \
    X(LblScrollSync, L"Scroll sync", L"Sync scorr.", L"Bildlauf-Sync", L"Sync défil.",             \
      L"Görg.-szinkron", L"Синхр. прокруч.", L"Sinc. derulare", L"Sinc. desloc.",                  \
      L"Συγχρ. κύλισης", L"Sinc. despl.", L"Synchr. przewij.", L"Scrollsync",                      \
      L"Synchr. posuv", L"Rullningssynk")                                                          \
    X(LblZoomSync, L"Zoom sync", L"Sync zoom", L"Zoom-Sync", L"Sync zoom", L"Zoom-szinkron",       \
      L"Синхр. масшт.", L"Sinc. zoom", L"Sinc. zoom", L"Συγχρ. ζουμ", L"Sinc. zoom",               \
      L"Synchr. powiększ.", L"Zoomsync", L"Synchr. přibl.", L"Zoomsynk")                           \
    X(LblActualSize, L"100%", L"100%", L"100%", L"100%", L"100%", L"100%", L"100%", L"100%",       \
      L"100%", L"100%", L"100%", L"100%", L"100%", L"100%")                                        \
    X(LblFitWidth, L"Width", L"Larghezza", L"Breite", L"Largeur", L"Szélesség", L"Ширина",         \
      L"Lățime", L"Largura", L"Πλάτος", L"Ancho", L"Szerokość", L"Breedte", L"Šířka",              \
      L"Bredd")                                                                                    \
    X(LblFitPage, L"Page", L"Pagina", L"Seite", L"Page", L"Oldal", L"Сторінка", L"Pagină",         \
      L"Página", L"Σελίδα", L"Página", L"Strona", L"Pagina", L"Stránka", L"Sida")                  \
    X(LblScrollContinuous, L"Continuous", L"Continuo", L"Fortlaufend", L"Continu",                 \
      L"Folyamatos", L"Безперервно", L"Continuu", L"Contínuo", L"Συνεχής", L"Continuo",            \
      L"Ciągłe", L"Doorlopend", L"Plynulé", L"Kontinuerlig")                                       \
    X(LblScrollPaged, L"Paged", L"A pagine", L"Seitenweise", L"Par page", L"Oldalanként",          \
      L"Посторінково", L"Pe pagini", L"Por página", L"Ανά σελίδα", L"Por página",                  \
      L"Po stronie", L"Per pagina", L"Po stránce", L"Sidvis")                                      \
    X(LblFind, L"Find", L"Trova", L"Suchen", L"Rechercher", L"Keresés", L"Пошук", L"Căutare",      \
      L"Localizar", L"Εύρεση", L"Buscar", L"Znajdź", L"Zoeken", L"Najít", L"Sök")                  \
    X(LblOutline, L"Outline", L"Segnalibri", L"Lesezeichen", L"Signets", L"Könyvjelzők",           \
      L"Закладки", L"Marcaje", L"Marcadores", L"Σελιδοδείκτες", L"Marcadores",                     \
      L"Zakładki", L"Bladwijzers", L"Záložky", L"Bokmärken")                                       \
    X(LblFullScreen, L"Full screen", L"Schermo int.", L"Vollbild", L"Plein écran",                 \
      L"Teljes kép.", L"Весь екран", L"Ecran compl.", L"Ecrã inteiro", L"Πλήρης οθ.",              \
      L"Pant. compl.", L"Pełny ekran", L"Volledig sch.", L"Celá obraz.", L"Helskärm")              \
    X(LblAddSyncPoint, L"Add point", L"Agg. punto", L"Punkt hinzuf.", L"Ajouter point",            \
      L"Pont hozzáad.", L"Дод. точку", L"Adaugă punct", L"Adic. ponto", L"Προσθ. σημείου",         \
      L"Añadir punto", L"Dodaj punkt", L"Punt toev.", L"Přidat bod", L"Ny punkt")                  \
    X(LblSyncFromBookmarks, L"Bookmarks", L"Da segnalibri", L"Aus Lesezeichen",                    \
      L"Depuis signets", L"Könyvjelzőkből", L"Із закладок", L"Din marcaje",                        \
      L"Dos marcadores", L"Από σελιδοδ.", L"De marcadores", L"Z zakładek",                         \
      L"Bladwijzers", L"Ze záložek", L"Från bokm.")                                                \
    X(LblSyncPoints, L"Points", L"Punti", L"Punkte", L"Points", L"Pontok", L"Точки",               \
      L"Puncte", L"Pontos", L"Σημεία", L"Puntos", L"Punkty", L"Punten", L"Body",                   \
      L"Punkter")                                                                                  \
    X(LblClearSyncPoints, L"Clear", L"Rimuovi", L"Entfernen", L"Effacer", L"Törlés",               \
      L"Очистити", L"Șterge", L"Limpar", L"Εκκαθάριση", L"Borrar", L"Wyczyść", L"Wissen",          \
      L"Vymazat", L"Rensa")                                                                        \
    X(LblAlignmentGaps, L"Gaps", L"Spazi", L"Lücken", L"Espaces", L"Hézagok", L"Проміжки",         \
      L"Spații", L"Espaços", L"Κενά", L"Espacios", L"Odstępy", L"Ruimten", L"Mezery",              \
      L"Mellanrum")                                                                                \
    X(LblSwapPanes, L"Swap", L"Scambia", L"Tauschen", L"Échanger", L"Csere", L"Обміняти",          \
      L"Inversează", L"Trocar", L"Εναλλαγή", L"Intercambiar", L"Zamień", L"Wisselen",              \
      L"Prohodit", L"Växla")                                                                       \
    /* synctex feedback (status bar, never popups) */                                              \
    X(SyncTexNoData, L"SyncTeX: no .synctex file for this document",                               \
      L"SyncTeX: nessun file .synctex per questo documento",                                       \
      L"SyncTeX: keine .synctex-Datei für dieses Dokument",                                        \
      L"SyncTeX : aucun fichier .synctex pour ce document",                                        \
      L"SyncTeX: nincs .synctex fájl ehhez a dokumentumhoz",                                       \
      L"SyncTeX: немає файлу .synctex для цього документа",                                        \
      L"SyncTeX: niciun fișier .synctex pentru acest document",                                    \
      L"SyncTeX: nenhum ficheiro .synctex para este documento",                                    \
      L"SyncTeX: δεν υπάρχει αρχείο .synctex για αυτό το έγγραφο",                                 \
      L"SyncTeX: no hay archivo .synctex para este documento",                                     \
      L"SyncTeX: brak pliku .synctex dla tego dokumentu",                                          \
      L"SyncTeX: geen .synctex-bestand voor dit document",                                         \
      L"SyncTeX: pro tento dokument neexistuje soubor .synctex",                                   \
      L"SyncTeX: ingen .synctex-fil för det här dokumentet")                                       \
    X(SyncTexNoMatch, L"SyncTeX: nothing found at this position",                                  \
      L"SyncTeX: nessun risultato in questa posizione",                                            \
      L"SyncTeX: an dieser Position nichts gefunden",                                              \
      L"SyncTeX : rien trouvé à cette position",                                                   \
      L"SyncTeX: nincs találat ezen a pozíción",                                                   \
      L"SyncTeX: нічого не знайдено в цій позиції",                                                \
      L"SyncTeX: nimic găsit la această poziție",                                                  \
      L"SyncTeX: nada encontrado nesta posição",                                                   \
      L"SyncTeX: δεν βρέθηκε τίποτα σε αυτήν τη θέση",                                             \
      L"SyncTeX: no se encontró nada en esta posición",                                            \
      L"SyncTeX: nic nie znaleziono w tej pozycji",                                                \
      L"SyncTeX: niets gevonden op deze positie",                                                  \
      L"SyncTeX: na této pozici nebylo nic nalezeno",                                              \
      L"SyncTeX: inget hittades på den här positionen")                                            \
    X(SyncTexForwardMiss, L"SyncTeX: source line not found in this document",                      \
      L"SyncTeX: riga sorgente non trovata in questo documento",                                   \
      L"SyncTeX: Quellzeile in diesem Dokument nicht gefunden",                                    \
      L"SyncTeX : ligne source introuvable dans ce document",                                      \
      L"SyncTeX: a forrássor nem található ebben a dokumentumban",                                 \
      L"SyncTeX: рядок джерела не знайдено в цьому документі",                                     \
      L"SyncTeX: linia sursă nu a fost găsită în acest document",                                  \
      L"SyncTeX: linha de origem não encontrada neste documento",                                  \
      L"SyncTeX: η γραμμή προέλευσης δεν βρέθηκε σε αυτό το έγγραφο",                              \
      L"SyncTeX: línea de origen no encontrada en este documento",                                 \
      L"SyncTeX: nie znaleziono wiersza źródłowego w tym dokumencie",                              \
      L"SyncTeX: bronregel niet gevonden in dit document",                                         \
      L"SyncTeX: zdrojový řádek nebyl v tomto dokumentu nalezen",                                  \
      L"SyncTeX: källraden hittades inte i det här dokumentet")                                    \
    X(SyncTexEditorError, L"SyncTeX: could not launch the editor",                                 \
      L"SyncTeX: impossibile avviare l'editor",                                                    \
      L"SyncTeX: Editor konnte nicht gestartet werden",                                            \
      L"SyncTeX : impossible de lancer l'éditeur",                                                 \
      L"SyncTeX: nem sikerült elindítani a szerkesztőt",                                           \
      L"SyncTeX: не вдалося запустити редактор",                                                   \
      L"SyncTeX: nu s-a putut lansa editorul",                                                     \
      L"SyncTeX: não foi possível iniciar o editor",                                               \
      L"SyncTeX: δεν ήταν δυνατή η εκκίνηση του επεξεργαστή",                                      \
      L"SyncTeX: no se pudo iniciar el editor",                                                    \
      L"SyncTeX: nie można uruchomić edytora",                                                     \
      L"SyncTeX: kan de editor niet starten",                                                      \
      L"SyncTeX: editor se nepodařilo spustit",                                                    \
      L"SyncTeX: det gick inte att starta redigeraren")                                            \
    /* status bar ("Left: " + "3 / 42" is composed in code) */                                     \
    X(StatusLeftPrefix, L"Left: ", L"Sinistra: ", L"Links: ", L"Gauche : ", L"Bal: ",              \
      L"Ліворуч: ", L"Stânga: ", L"Esquerda: ", L"Αριστερά: ", L"Izquierda: ", L"Lewy: ",          \
      L"Links: ", L"Vlevo: ", L"Vänster: ")                                                        \
    X(StatusRightPrefix, L"Right: ", L"Destra: ", L"Rechts: ", L"Droite : ", L"Jobb: ",            \
      L"Праворуч: ", L"Dreapta: ", L"Direita: ", L"Δεξιά: ", L"Derecha: ", L"Prawy: ",             \
      L"Rechts: ", L"Vpravo: ", L"Höger: ")                                                        \
    X(StatusLeftNoDoc, L"Left: —", L"Sinistra: —", L"Links: —", L"Gauche : —", L"Bal: —",          \
      L"Ліворуч: —", L"Stânga: —", L"Esquerda: —", L"Αριστερά: —", L"Izquierda: —",                \
      L"Lewy: —", L"Links: —", L"Vlevo: —", L"Vänster: —")                                         \
    X(StatusRightNoDoc, L"Right: —", L"Destra: —", L"Rechts: —", L"Droite : —", L"Jobb: —",        \
      L"Праворуч: —", L"Dreapta: —", L"Direita: —", L"Δεξιά: —", L"Derecha: —",                    \
      L"Prawy: —", L"Rechts: —", L"Vpravo: —", L"Höger: —")                                        \
    X(StatusSyncBoth, L"Sync: scroll+zoom", L"Sync: scorrimento+zoom",                             \
      L"Sync: Bildlauf+Zoom", L"Sync : défilement+zoom", L"Szinkron: görgetés+zoom",               \
      L"Синхр.: прокрутка+масштаб", L"Sinc.: derulare+zoom", L"Sinc.: deslocamento+zoom",          \
      L"Συγχρ.: κύλιση+ζουμ", L"Sinc.: desplazamiento+zoom",                                       \
      L"Synchr.: przewijanie+zoom", L"Sync: scrollen+zoom", L"Synchr.: posuv+zoom",                \
      L"Synk: rullning+zoom")                                                                      \
    X(StatusSyncScroll, L"Sync: scroll", L"Sync: scorrimento", L"Sync: Bildlauf",                  \
      L"Sync : défilement", L"Szinkron: görgetés", L"Синхр.: прокрутка",                           \
      L"Sinc.: derulare", L"Sinc.: deslocamento", L"Συγχρ.: κύλιση",                               \
      L"Sinc.: desplazamiento", L"Synchr.: przewijanie", L"Sync: scrollen",                        \
      L"Synchr.: posuv", L"Synk: rullning")                                                        \
    X(StatusSyncZoom, L"Sync: zoom", L"Sync: zoom", L"Sync: Zoom", L"Sync : zoom",                 \
      L"Szinkron: zoom", L"Синхр.: масштаб", L"Sinc.: zoom", L"Sinc.: zoom",                       \
      L"Συγχρ.: ζουμ", L"Sinc.: zoom", L"Synchr.: zoom", L"Sync: zoom",                            \
      L"Synchr.: zoom", L"Synk: zoom")                                                             \
    X(StatusSyncOff, L"Sync: off", L"Sync: disattivata", L"Sync: aus", L"Sync : désactivée",       \
      L"Szinkron: ki", L"Синхр.: вимкнено", L"Sinc.: dezactivată", L"Sinc.: desativada",           \
      L"Συγχρ.: ανενεργός", L"Sinc.: desactivada", L"Synchr.: wyłączona", L"Sync: uit",            \
      L"Synchr.: vypnuto", L"Synk: av")                                                            \
    /* sync points (status cell suffix is composed in code: pre + count + post) */                 \
    X(StatusSyncPtsPre, L" · ", L" · ", L" · ", L" · ", L" · ", L" · ", L" · ", L" · ",            \
      L" · ", L" · ", L" · ", L" · ", L" · ", L" · ")                                              \
    X(StatusSyncPtsPost, L" pts", L" punti", L" Punkte", L" points", L" pont", L" точок",          \
      L" puncte", L" pontos", L" σημεία", L" puntos", L" pkt", L" punten", L" bodů",               \
      L" punkter")                                                                                 \
    X(SyncPtsGenerated, L"Sync points from bookmarks: ", L"Punti di sync dai segnalibri: ",        \
      L"Sync-Punkte aus Lesezeichen: ", L"Points de sync depuis les signets : ",                   \
      L"Szinkronpontok a könyvjelzőkből: ", L"Точки синхронізації із закладок: ",                  \
      L"Puncte de sincronizare din marcaje: ", L"Pontos de sincronização dos marcadores: ",        \
      L"Σημεία συγχρονισμού από σελιδοδείκτες: ",                                                  \
      L"Puntos de sincronización desde marcadores: ",                                              \
      L"Punkty synchronizacji z zakładek: ",                                                       \
      L"Synchronisatiepunten uit bladwijzers: ",                                                   \
      L"Synchronizační body ze záložek: ",                                                         \
      L"Synkpunkter från bokmärken: ")                                                             \
    X(SyncPtsNoMatch, L"Sync points: no matching numbered bookmarks",                              \
      L"Punti di sync: nessun segnalibro numerato in comune",                                      \
      L"Sync-Punkte: keine übereinstimmenden nummerierten Lesezeichen",                            \
      L"Points de sync : aucun signet numéroté correspondant",                                     \
      L"Szinkronpontok: nincs egyező számozott könyvjelző",                                        \
      L"Точки синхронізації: немає збіжних нумерованих закладок",                                  \
      L"Puncte de sincronizare: niciun marcaj numerotat corespondent",                             \
      L"Pontos de sincronização: sem marcadores numerados correspondentes",                        \
      L"Σημεία συγχρονισμού: κανένας κοινός αριθμημένος σελιδοδείκτης",                            \
      L"Puntos de sincronización: sin marcadores numerados coincidentes",                          \
      L"Punkty synchronizacji: brak pasujących numerowanych zakładek",                             \
      L"Synchronisatiepunten: geen overeenkomende genummerde bladwijzers",                         \
      L"Synchronizační body: žádné odpovídající číslované záložky",                                \
      L"Synkpunkter: inga matchande numrerade bokmärken")                                          \
    /* window title */                                                                             \
    X(TitleScrollSyncTag, L"  [scroll sync]", L"  [sync scorrimento]", L"  [Bildlauf-Sync]",       \
      L"  [sync défilement]", L"  [görgetés-szinkron]", L"  [синхр. прокручування]",               \
      L"  [sinc. derulare]", L"  [sinc. deslocamento]", L"  [συγχρ. κύλισης]",                     \
      L"  [sinc. desplazamiento]", L"  [synchr. przewijania]", L"  [scrollsync]",                  \
      L"  [synchr. posouvání]", L"  [rullningssynk]")                                              \
    X(TitleZoomSyncTag, L"  [zoom sync]", L"  [sync zoom]", L"  [Zoom-Sync]",                      \
      L"  [sync zoom]", L"  [zoom-szinkron]", L"  [синхр. масштабу]", L"  [sinc. zoom]",           \
      L"  [sinc. zoom]", L"  [συγχρ. ζουμ]", L"  [sinc. zoom]",                                    \
      L"  [synchr. powiększenia]", L"  [zoomsync]", L"  [synchr. přiblížení]",                     \
      L"  [zoomsynk]")                                                                             \
    /* open dialog */                                                                              \
    X(OpenDlgTitleLeft, L"Open document in left pane", L"Apri documento nel pannello sinistro",    \
      L"Dokument im linken Bereich öffnen",                                                        \
      L"Ouvrir un document dans le panneau gauche",                                                \
      L"Dokumentum megnyitása a bal panelen",                                                      \
      L"Відкрити документ у лівій панелі",                                                         \
      L"Deschide un document în panoul din stânga",                                                \
      L"Abrir documento no painel esquerdo",                                                       \
      L"Άνοιγμα εγγράφου στο αριστερό πλαίσιο",                                                    \
      L"Abrir documento en el panel izquierdo",                                                    \
      L"Otwórz dokument w lewym panelu",                                                           \
      L"Document openen in linkerdeelvenster",                                                     \
      L"Otevřít dokument v levém panelu",                                                          \
      L"Öppna dokument i vänster panel")                                                           \
    X(OpenDlgTitleRight, L"Open document in right pane",                                           \
      L"Apri documento nel pannello destro", L"Dokument im rechten Bereich öffnen",                \
      L"Ouvrir un document dans le panneau droit",                                                 \
      L"Dokumentum megnyitása a jobb panelen",                                                     \
      L"Відкрити документ у правій панелі",                                                        \
      L"Deschide un document în panoul din dreapta",                                               \
      L"Abrir documento no painel direito",                                                        \
      L"Άνοιγμα εγγράφου στο δεξιό πλαίσιο",                                                       \
      L"Abrir documento en el panel derecho",                                                      \
      L"Otwórz dokument w prawym panelu",                                                          \
      L"Document openen in rechterdeelvenster",                                                    \
      L"Otevřít dokument v pravém panelu",                                                         \
      L"Öppna dokument i höger panel")                                                             \
    X(OpenDlgFilter, L"PDF documents (*.pdf)\0*.pdf\0All files (*.*)\0*.*\0",                      \
      L"Documenti PDF (*.pdf)\0*.pdf\0Tutti i file (*.*)\0*.*\0",                                  \
      L"PDF-Dokumente (*.pdf)\0*.pdf\0Alle Dateien (*.*)\0*.*\0",                                  \
      L"Documents PDF (*.pdf)\0*.pdf\0Tous les fichiers (*.*)\0*.*\0",                             \
      L"PDF-dokumentumok (*.pdf)\0*.pdf\0Minden fájl (*.*)\0*.*\0",                                \
      L"Документи PDF (*.pdf)\0*.pdf\0Усі файли (*.*)\0*.*\0",                                     \
      L"Documente PDF (*.pdf)\0*.pdf\0Toate fișierele (*.*)\0*.*\0",                               \
      L"Documentos PDF (*.pdf)\0*.pdf\0Todos os ficheiros (*.*)\0*.*\0",                           \
      L"Έγγραφα PDF (*.pdf)\0*.pdf\0Όλα τα αρχεία (*.*)\0*.*\0",                                   \
      L"Documentos PDF (*.pdf)\0*.pdf\0Todos los archivos (*.*)\0*.*\0",                           \
      L"Dokumenty PDF (*.pdf)\0*.pdf\0Wszystkie pliki (*.*)\0*.*\0",                               \
      L"PDF-documenten (*.pdf)\0*.pdf\0Alle bestanden (*.*)\0*.*\0",                               \
      L"Dokumenty PDF (*.pdf)\0*.pdf\0Všechny soubory (*.*)\0*.*\0",                               \
      L"PDF-dokument (*.pdf)\0*.pdf\0Alla filer (*.*)\0*.*\0")                                     \
    /* pane placeholders */                                                                        \
    X(PlaceholderLeft, L"Left pane\nCtrl+O or double-click to open a PDF",                         \
      L"Pannello sinistro\nCtrl+O o doppio click per aprire un PDF",                               \
      L"Linker Bereich\nStrg+O oder Doppelklick, um ein PDF zu öffnen",                            \
      L"Panneau gauche\nCtrl+O ou double-clic pour ouvrir un PDF",                                 \
      L"Bal panel\nCtrl+O vagy dupla kattintás PDF megnyitásához",                                 \
      L"Ліва панель\nCtrl+O або подвійне клацання, щоб відкрити PDF",                              \
      L"Panoul din stânga\nCtrl+O sau dublu clic pentru a deschide un PDF",                        \
      L"Painel esquerdo\nCtrl+O ou duplo clique para abrir um PDF",                                \
      L"Αριστερό πλαίσιο\nCtrl+O ή διπλό κλικ για άνοιγμα PDF",                                    \
      L"Panel izquierdo\nCtrl+O o doble clic para abrir un PDF",                                   \
      L"Lewy panel\nCtrl+O lub dwukrotne kliknięcie, aby otworzyć PDF",                            \
      L"Linkerdeelvenster\nCtrl+O of dubbelklik om een PDF te openen",                             \
      L"Levý panel\nCtrl+O nebo dvojklik pro otevření PDF",                                        \
      L"Vänster panel\nCtrl+O eller dubbelklicka för att öppna en PDF")                            \
    X(PlaceholderRight, L"Right pane\nCtrl+Shift+O or double-click to open a PDF",                 \
      L"Pannello destro\nCtrl+Shift+O o doppio click per aprire un PDF",                           \
      L"Rechter Bereich\nStrg+Umschalt+O oder Doppelklick, um ein PDF zu öffnen",                  \
      L"Panneau droit\nCtrl+Maj+O ou double-clic pour ouvrir un PDF",                              \
      L"Jobb panel\nCtrl+Shift+O vagy dupla kattintás PDF megnyitásához",                          \
      L"Права панель\nCtrl+Shift+O або подвійне клацання, щоб відкрити PDF",                       \
      L"Panoul din dreapta\nCtrl+Shift+O sau dublu clic pentru a deschide un PDF",                 \
      L"Painel direito\nCtrl+Shift+O ou duplo clique para abrir um PDF",                           \
      L"Δεξιό πλαίσιο\nCtrl+Shift+O ή διπλό κλικ για άνοιγμα PDF",                                 \
      L"Panel derecho\nCtrl+Shift+O o doble clic para abrir un PDF",                               \
      L"Prawy panel\nCtrl+Shift+O lub dwukrotne kliknięcie, aby otworzyć PDF",                     \
      L"Rechterdeelvenster\nCtrl+Shift+O of dubbelklik om een PDF te openen",                      \
      L"Pravý panel\nCtrl+Shift+O nebo dvojklik pro otevření PDF",                                 \
      L"Höger panel\nCtrl+Shift+O eller dubbelklicka för att öppna en PDF")                        \
    X(PaneOpening, L"Opening", L"Apertura di", L"Öffnen von", L"Ouverture de",                     \
      L"Megnyitás:", L"Відкриття", L"Se deschide", L"A abrir", L"Άνοιγμα:", L"Abriendo",           \
      L"Otwieranie:", L"Openen van", L"Otevírání:", L"Öppnar")                                     \
    X(PaneOpenFailed, L"Could not open", L"Impossibile aprire", L"Fehler beim Öffnen von",         \
      L"Impossible d'ouvrir", L"Nem sikerült megnyitni:", L"Не вдалося відкрити",                  \
      L"Nu s-a putut deschide", L"Não foi possível abrir",                                         \
      L"Δεν ήταν δυνατό το άνοιγμα:", L"No se pudo abrir", L"Nie można otworzyć:",                 \
      L"Kan niet openen:", L"Nelze otevřít:", L"Det gick inte att öppna:")                         \
    X(PaneEmptyDoc, L"(empty document)", L"(documento vuoto)", L"(leeres Dokument)",               \
      L"(document vide)", L"(üres dokumentum)", L"(порожній документ)", L"(document gol)",         \
      L"(documento vazio)", L"(κενό έγγραφο)", L"(documento vacío)", L"(pusty dokument)",          \
      L"(leeg document)", L"(prázdný dokument)", L"(tomt dokument)")                               \
    X(DeviceLostError, L"The graphics device could not be restored.",                              \
      L"Impossibile ripristinare il dispositivo grafico.",                                         \
      L"Das Grafikgerät konnte nicht wiederhergestellt werden.",                                   \
      L"Le périphérique graphique n'a pas pu être restauré.",                                      \
      L"A grafikus eszközt nem sikerült helyreállítani.",                                          \
      L"Не вдалося відновити графічний пристрій.",                                                 \
      L"Dispozitivul grafic nu a putut fi restaurat.",                                             \
      L"Não foi possível restaurar o dispositivo gráfico.",                                        \
      L"Δεν ήταν δυνατή η επαναφορά της συσκευής γραφικών.",                                       \
      L"No se pudo restaurar el dispositivo gráfico.",                                             \
      L"Nie można przywrócić urządzenia graficznego.",                                             \
      L"Het grafische apparaat kon niet worden hersteld.",                                         \
      L"Grafické zařízení se nepodařilo obnovit.",                                                 \
      L"Grafikenheten kunde inte återställas.")                                                    \
    /* options dialog */                                                                           \
    X(OptTitle, L"Options", L"Opzioni", L"Optionen", L"Options", L"Beállítások",                   \
      L"Параметри", L"Opțiuni", L"Opções", L"Επιλογές", L"Opciones", L"Opcje", L"Opties",          \
      L"Možnosti", L"Alternativ")                                                                  \
    X(OptRestoreSession, L"Reopen the last session at startup",                                    \
      L"Riapri l'ultima sessione all'avvio",                                                       \
      L"Letzte Sitzung beim Start wiederherstellen",                                               \
      L"Rouvrir la dernière session au démarrage",                                                 \
      L"Az utolsó munkamenet visszaállítása induláskor",                                           \
      L"Відновлювати останній сеанс під час запуску",                                              \
      L"Redeschide ultima sesiune la pornire",                                                     \
      L"Reabrir a última sessão ao iniciar",                                                       \
      L"Επαναφορά της τελευταίας περιόδου κατά την εκκίνηση",                                      \
      L"Reabrir la última sesión al iniciar",                                                      \
      L"Otwieraj ostatnią sesję przy uruchomieniu",                                                \
      L"Laatste sessie opnieuw openen bij het starten",                                            \
      L"Při spuštění znovu otevřít poslední relaci",                                               \
      L"Öppna den senaste sessionen vid start")                                                    \
    X(OptDefaultsGroup, L"Defaults for new documents", L"Predefiniti per i nuovi documenti",       \
      L"Standardwerte für neue Dokumente",                                                         \
      L"Valeurs par défaut pour les nouveaux documents",                                           \
      L"Alapértelmezések új dokumentumokhoz",                                                      \
      L"Типові значення для нових документів",                                                     \
      L"Valori implicite pentru documente noi",                                                    \
      L"Predefinições para novos documentos",                                                      \
      L"Προεπιλογές για νέα έγγραφα",                                                              \
      L"Valores predeterminados para documentos nuevos",                                           \
      L"Ustawienia domyślne dla nowych dokumentów",                                                \
      L"Standaardwaarden voor nieuwe documenten",                                                  \
      L"Výchozí hodnoty pro nové dokumenty",                                                       \
      L"Standardvärden för nya dokument")                                                          \
    X(OptDefScrollMode, L"Scrolling:", L"Scorrimento:", L"Bildlauf:", L"Défilement :",             \
      L"Görgetés:", L"Прокручування:", L"Derulare:", L"Deslocamento:", L"Κύλιση:",                 \
      L"Desplazamiento:", L"Przewijanie:", L"Scrollen:", L"Posouvání:", L"Rullning:")              \
    X(OptDefZoomMode, L"Zoom:", L"Zoom:", L"Zoom:", L"Zoom :", L"Zoom:", L"Масштаб:",              \
      L"Zoom:", L"Zoom:", L"Ζουμ:", L"Zoom:", L"Powiększenie:", L"Zoom:", L"Přiblížení:",          \
      L"Zoom:")                                                                                    \
    X(OptScrollContinuous, L"Continuous scrolling", L"Scorrimento continuo",                       \
      L"Fortlaufender Bildlauf", L"Défilement continu", L"Folyamatos görgetés",                    \
      L"Безперервне прокручування", L"Derulare continuă", L"Deslocamento contínuo",                \
      L"Συνεχής κύλιση", L"Desplazamiento continuo", L"Przewijanie ciągłe",                        \
      L"Doorlopend scrollen", L"Plynulé posouvání", L"Kontinuerlig rullning")                      \
    X(OptScrollPaged, L"Page-by-page", L"Pagina per pagina", L"Seitenweise",                       \
      L"Page par page", L"Oldalankénti görgetés", L"Посторінково", L"Pagină cu pagină",            \
      L"Página a página", L"Σελίδα-σελίδα", L"Página a página", L"Strona po stronie",              \
      L"Pagina per pagina", L"Stránka po stránce", L"Sida för sida")                               \
    X(OptZoomActual, L"Actual size", L"Dimensioni effettive", L"Originalgröße",                    \
      L"Taille réelle", L"Tényleges méret", L"Фактичний розмір", L"Dimensiune reală",              \
      L"Tamanho real", L"Πραγματικό μέγεθος", L"Tamaño real", L"Rzeczywisty rozmiar",              \
      L"Werkelijke grootte", L"Skutečná velikost", L"Verklig storlek")                             \
    X(OptZoomFitWidth, L"Fit width", L"Adatta larghezza", L"An Breite anpassen",                   \
      L"Ajuster à la largeur", L"Igazítás szélességhez", L"За шириною",                            \
      L"Potrivire la lățime", L"Ajustar à largura", L"Προσαρμογή στο πλάτος",                      \
      L"Ajustar al ancho", L"Dopasuj do szerokości", L"Aanpassen aan breedte",                     \
      L"Přizpůsobit šířce", L"Anpassa till bredd")                                                 \
    X(OptZoomFitPage, L"Fit page", L"Adatta pagina", L"An Seite anpassen",                         \
      L"Ajuster à la page", L"Igazítás oldalhoz", L"Ціла сторінка",                                \
      L"Potrivire la pagină", L"Ajustar à página", L"Προσαρμογή στη σελίδα",                       \
      L"Ajustar a la página", L"Dopasuj do strony", L"Aanpassen aan pagina",                       \
      L"Přizpůsobit stránce", L"Anpassa till sida")                                                \
    X(OptDefScrollSync, L"Scroll sync on", L"Sync scorrimento attiva",                             \
      L"Bildlauf-Sync aktiv", L"Sync du défilement activée",                                       \
      L"Görgetés-szinkron bekapcsolva", L"Синхронізація прокручування ввімкнена",                  \
      L"Sincronizare derulare activată", L"Sincronização do deslocamento ativada",                 \
      L"Συγχρονισμός κύλισης ενεργός", L"Sincronización del desplazamiento activada",              \
      L"Synchronizacja przewijania włączona", L"Scrollsynchronisatie aan",                         \
      L"Synchronizace posouvání zapnuta", L"Rullningssynk på")                                     \
    X(OptDefZoomSync, L"Zoom sync on", L"Sync zoom attiva", L"Zoom-Sync aktiv",                    \
      L"Sync du zoom activée", L"Zoom-szinkron bekapcsolva",                                       \
      L"Синхронізація масштабу ввімкнена", L"Sincronizare zoom activată",                          \
      L"Sincronização do zoom ativada", L"Συγχρονισμός ζουμ ενεργός",                              \
      L"Sincronización del zoom activada", L"Synchronizacja powiększenia włączona",                \
      L"Zoomsynchronisatie aan", L"Synchronizace přiblížení zapnuta", L"Zoomsynk på")              \
    X(OptSynctexInverse, L"SyncTeX inverse-search command (%f = file, %l = line):",                \
      L"Comando ricerca inversa SyncTeX (%f = file, %l = riga):",                                  \
      L"SyncTeX-Rücksuche-Befehl (%f = Datei, %l = Zeile):",                                       \
      L"Commande de recherche inverse SyncTeX (%f = fichier, %l = ligne) :",                       \
      L"SyncTeX fordított keresés parancsa (%f = fájl, %l = sor):",                                \
      L"Команда зворотного пошуку SyncTeX (%f = файл, %l = рядок):",                               \
      L"Comandă de căutare inversă SyncTeX (%f = fișier, %l = linie):",                            \
      L"Comando de pesquisa inversa SyncTeX (%f = ficheiro, %l = linha):",                         \
      L"Εντολή αντίστροφης αναζήτησης SyncTeX (%f = αρχείο, %l = γραμμή):",                        \
      L"Comando de búsqueda inversa SyncTeX (%f = archivo, %l = línea):",                          \
      L"Polecenie wyszukiwania wstecznego SyncTeX (%f = plik, %l = wiersz):",                      \
      L"SyncTeX-opdracht voor omgekeerd zoeken (%f = bestand, %l = regel):",                       \
      L"Příkaz zpětného vyhledávání SyncTeX (%f = soubor, %l = řádek):",                           \
      L"SyncTeX-kommando för omvänd sökning (%f = fil, %l = rad):")                                \
    X(OptShellIntegration,                                                                         \
      L"Show \"Open left/right in PDF Side Viewer\" in the Explorer menu for PDF files",           \
      L"Mostra \"Apri a sinistra/destra in PDF Side Viewer\" nel menu di Esplora file per i PDF",  \
      L"„Links/Rechts in PDF Side Viewer öffnen“ im Explorer-Menü für PDF-Dateien anzeigen",       \
      L"Afficher « Ouvrir à gauche/droite dans PDF Side Viewer » dans le menu de "                 \
      L"l'Explorateur pour les PDF",                                                               \
      L"„Megnyitás balra/jobbra a PDF Side Viewerben” megjelenítése az Intéző menüjében "          \
      L"a PDF-ekhez",                                                                              \
      L"Показувати «Відкрити ліворуч/праворуч у PDF Side Viewer» у меню Провідника "               \
      L"для файлів PDF",                                                                           \
      L"Afișează „Deschide la stânga/dreapta în PDF Side Viewer” în meniul Explorer "              \
      L"pentru fișiere PDF",                                                                       \
      L"Mostrar «Abrir à esquerda/direita no PDF Side Viewer» no menu do Explorador "              \
      L"para ficheiros PDF",                                                                       \
      L"Εμφάνιση «Άνοιγμα αριστερά/δεξιά στο PDF Side Viewer» στο μενού της Εξερεύνησης "          \
      L"για αρχεία PDF",                                                                           \
      L"Mostrar «Abrir a la izquierda/derecha en PDF Side Viewer» en el menú del "                 \
      L"Explorador para archivos PDF",                                                             \
      L"Pokaż „Otwórz po lewej/prawej w PDF Side Viewer” w menu Eksploratora dla "                 \
      L"plików PDF",                                                                               \
      L"\"Links/rechts openen in PDF Side Viewer\" tonen in het Verkenner-menu voor "              \
      L"PDF-bestanden",                                                                            \
      L"Zobrazit „Otevřít vlevo/vpravo v PDF Side Viewer“ v nabídce Průzkumníka pro "              \
      L"soubory PDF",                                                                              \
      L"Visa \"Öppna till vänster/höger i PDF Side Viewer\" i Utforskaren-menyn "                  \
      L"för PDF-filer")                                                                            \
    X(OptFsToolbar, L"Show the toolbar in full screen",                                            \
      L"Mostra la barra degli strumenti a schermo intero",                                         \
      L"Symbolleiste im Vollbild anzeigen",                                                        \
      L"Afficher la barre d'outils en plein écran",                                                \
      L"Eszköztár megjelenítése teljes képernyőn",                                                 \
      L"Показувати панель інструментів у повноекранному режимі",                                   \
      L"Afișează bara de instrumente în ecran complet",                                            \
      L"Mostrar a barra de ferramentas em ecrã inteiro",                                           \
      L"Εμφάνιση της γραμμής εργαλείων σε πλήρη οθόνη",                                            \
      L"Mostrar la barra de herramientas en pantalla completa",                                    \
      L"Pokazuj pasek narzędzi w trybie pełnoekranowym",                                           \
      L"Werkbalk tonen in volledig scherm",                                                        \
      L"Zobrazovat panel nástrojů v celoobrazovkovém režimu",                                      \
      L"Visa verktygsfältet i helskärm")                                                           \
    X(OptFsStatus, L"Show the status bar in full screen",                                          \
      L"Mostra la barra di stato a schermo intero",                                                \
      L"Statusleiste im Vollbild anzeigen",                                                        \
      L"Afficher la barre d'état en plein écran",                                                  \
      L"Állapotsor megjelenítése teljes képernyőn",                                                \
      L"Показувати рядок стану в повноекранному режимі",                                           \
      L"Afișează bara de stare în ecran complet",                                                  \
      L"Mostrar a barra de estado em ecrã inteiro",                                                \
      L"Εμφάνιση της γραμμής κατάστασης σε πλήρη οθόνη",                                           \
      L"Mostrar la barra de estado en pantalla completa",                                          \
      L"Pokazuj pasek stanu w trybie pełnoekranowym",                                              \
      L"Statusbalk tonen in volledig scherm",                                                      \
      L"Zobrazovat stavový řádek v celoobrazovkovém režimu",                                       \
      L"Visa statusfältet i helskärm")                                                             \
    X(OptShowAnchors, L"Show sync-point anchor marks",                                             \
      L"Mostra le ancore dei punti di sync",                                                       \
      L"Ankermarken der Sync-Punkte anzeigen",                                                     \
      L"Afficher les ancres des points de sync",                                                   \
      L"Szinkronpont-horgonyok megjelenítése",                                                     \
      L"Показувати якорі точок синхронізації",                                                     \
      L"Afișează ancorele punctelor de sincronizare",                                              \
      L"Mostrar as âncoras dos pontos de sincronização",                                           \
      L"Εμφάνιση αγκυρών σημείων συγχρονισμού",                                                    \
      L"Mostrar las anclas de los puntos de sincronización",                                       \
      L"Pokazuj kotwice punktów synchronizacji",                                                   \
      L"Ankermarkeringen van synchronisatiepunten tonen",                                          \
      L"Zobrazovat kotvy synchronizačních bodů",                                                   \
      L"Visa synkpunkternas ankarmarkeringar")                                                     \
    X(OptShowTicks, L"Show sync-point ticks along the scrollbar",                                  \
      L"Mostra i tick dei punti di sync lungo la barra",                                           \
      L"Sync-Punkt-Markierungen an der Bildlaufleiste anzeigen",                                   \
      L"Afficher les repères des points de sync le long de la barre",                              \
      L"Szinkronpont-jelölések a görgetősáv mentén",                                               \
      L"Показувати позначки точок синхронізації на смузі прокручування",                           \
      L"Afișează reperele punctelor de sincronizare pe bara de derulare",                          \
      L"Mostrar as marcas dos pontos de sincronização na barra de deslocamento",                   \
      L"Εμφάνιση ενδείξεων σημείων συγχρονισμού στη γραμμή κύλισης",                               \
      L"Mostrar las marcas de los puntos de sincronización en la barra de desplazamiento",         \
      L"Pokazuj znaczniki punktów synchronizacji na pasku przewijania",                            \
      L"Markeringen van synchronisatiepunten langs de scrollbalk tonen",                           \
      L"Zobrazovat značky synchronizačních bodů na posuvníku",                                     \
      L"Visa synkpunktsmarkeringar längs rullningslisten")                                         \
    X(OptShowHeader, L"Show a header above each pane",                                             \
      L"Mostra un'intestazione sopra ogni pannello",                                               \
      L"Kopfzeile über jedem Bereich anzeigen",                                                    \
      L"Afficher un en-tête au-dessus de chaque panneau",                                          \
      L"Fejléc megjelenítése minden panel felett",                                                 \
      L"Показувати заголовок над кожною панеллю",                                                  \
      L"Afișează un antet deasupra fiecărui panou",                                                \
      L"Mostrar um cabeçalho acima de cada painel",                                                \
      L"Εμφάνιση κεφαλίδας πάνω από κάθε πλαίσιο",                                                 \
      L"Mostrar un encabezado sobre cada panel",                                                   \
      L"Pokazuj nagłówek nad każdym panelem",                                                      \
      L"Koptekst boven elk deelvenster tonen",                                                     \
      L"Zobrazovat záhlaví nad každým panelem",                                                    \
      L"Visa en rubrik ovanför varje panel")                                                       \
    X(OptHeaderShowPath, L"Show the full path instead of the file name",                           \
      L"Mostra il percorso completo invece del nome file",                                         \
      L"Vollständigen Pfad statt des Dateinamens anzeigen",                                        \
      L"Afficher le chemin complet au lieu du nom de fichier",                                     \
      L"Teljes elérési út a fájlnév helyett",                                                      \
      L"Показувати повний шлях замість імені файлу",                                               \
      L"Afișează calea completă în locul numelui fișierului",                                      \
      L"Mostrar o caminho completo em vez do nome do ficheiro",                                    \
      L"Εμφάνιση πλήρους διαδρομής αντί του ονόματος αρχείου",                                     \
      L"Mostrar la ruta completa en lugar del nombre de archivo",                                  \
      L"Pokazuj pełną ścieżkę zamiast nazwy pliku",                                                \
      L"Volledig pad tonen in plaats van de bestandsnaam",                                         \
      L"Zobrazovat úplnou cestu místo názvu souboru",                                              \
      L"Visa den fullständiga sökvägen i stället för filnamnet")                                   \
    X(OptWheelLines, L"Wheel scroll lines (0 = system):",                                          \
      L"Righe per scatto della rotellina (0 = sistema):",                                          \
      L"Zeilen pro Mausrad-Raste (0 = System):",                                                   \
      L"Lignes par cran de molette (0 = système) :",                                               \
      L"Sorok görgőkattanásonként (0 = rendszer):",                                                \
      L"Рядків за крок коліщатка (0 = системне):",                                                 \
      L"Linii per pas de rotiță (0 = sistem):",                                                    \
      L"Linhas por passo da roda (0 = sistema):",                                                  \
      L"Γραμμές ανά βήμα του τροχού (0 = σύστημα):",                                               \
      L"Líneas por paso de la rueda (0 = sistema):",                                               \
      L"Wiersze na skok kółka (0 = systemowe):",                                                   \
      L"Regels per muiswielstap (0 = systeem):",                                                   \
      L"Řádků na krok kolečka (0 = systém):",                                                      \
      L"Rader per hjulsteg (0 = system):")                                                         \
    /* button text must stay within ~130 DLU (MainWindow.cpp Options layout) */                    \
    X(OptClearRecent, L"Clear recent files and pairs", L"Svuota gli elenchi recenti",              \
      L"Zuletzt verwendete Listen leeren",                                                         \
      L"Vider les fichiers et paires récents",                                                     \
      L"Legutóbbi fájlok és párok törlése",                                                        \
      L"Очистити списки останніх",                                                                 \
      L"Golește listele recente",                                                                  \
      L"Limpar as listas recentes",                                                                \
      L"Εκκαθάριση πρόσφατων λιστών",                                                              \
      L"Borrar las listas recientes",                                                              \
      L"Wyczyść listy ostatnich",                                                                  \
      L"Recente lijsten wissen",                                                                   \
      L"Vymazat nedávné seznamy",                                                                  \
      L"Rensa senaste-listorna")                                                                   \
    /* explorer context-menu verbs (written to the registry at registration) */                    \
    X(VerbOpenLeft, L"Open left in PDF Side Viewer", L"Apri a sinistra in PDF Side Viewer",        \
      L"Links in PDF Side Viewer öffnen", L"Ouvrir à gauche dans PDF Side Viewer",                 \
      L"Megnyitás balra a PDF Side Viewerben", L"Відкрити ліворуч у PDF Side Viewer",              \
      L"Deschide la stânga în PDF Side Viewer", L"Abrir à esquerda no PDF Side Viewer",            \
      L"Άνοιγμα αριστερά στο PDF Side Viewer",                                                     \
      L"Abrir a la izquierda en PDF Side Viewer", L"Otwórz po lewej w PDF Side Viewer",            \
      L"Links openen in PDF Side Viewer", L"Otevřít vlevo v PDF Side Viewer",                      \
      L"Öppna till vänster i PDF Side Viewer")                                                     \
    X(VerbOpenRight, L"Open right in PDF Side Viewer", L"Apri a destra in PDF Side Viewer",        \
      L"Rechts in PDF Side Viewer öffnen", L"Ouvrir à droite dans PDF Side Viewer",                \
      L"Megnyitás jobbra a PDF Side Viewerben", L"Відкрити праворуч у PDF Side Viewer",            \
      L"Deschide la dreapta în PDF Side Viewer", L"Abrir à direita no PDF Side Viewer",            \
      L"Άνοιγμα δεξιά στο PDF Side Viewer",                                                        \
      L"Abrir a la derecha en PDF Side Viewer", L"Otwórz po prawej w PDF Side Viewer",             \
      L"Rechts openen in PDF Side Viewer", L"Otevřít vpravo v PDF Side Viewer",                    \
      L"Öppna till höger i PDF Side Viewer")                                                       \
    /* go-to-page dialog + shared dialog buttons */                                                \
    X(GotoTitle, L"Go to Page", L"Vai alla pagina", L"Gehe zu Seite", L"Aller à la page",          \
      L"Ugrás oldalra", L"Перейти до сторінки", L"Salt la pagină", L"Ir para a página",            \
      L"Μετάβαση σε σελίδα", L"Ir a la página", L"Przejdź do strony", L"Ga naar pagina",           \
      L"Přejít na stránku", L"Gå till sida")                                                       \
    X(GotoPrompt, L"Page number or label:", L"Numero di pagina o etichetta:",                      \
      L"Seitenzahl oder Bezeichnung:", L"Numéro de page ou étiquette :",                           \
      L"Oldalszám vagy címke:", L"Номер або мітка сторінки:",                                      \
      L"Număr de pagină sau etichetă:", L"Número de página ou etiqueta:",                          \
      L"Αριθμός ή ετικέτα σελίδας:", L"Número de página o etiqueta:",                              \
      L"Numer strony lub etykieta:", L"Paginanummer of label:",                                    \
      L"Číslo stránky nebo popisek:", L"Sidnummer eller etikett:")                                 \
    X(DlgOk, L"OK", L"OK", L"OK", L"OK", L"OK", L"OK", L"OK", L"OK", L"OK", L"Aceptar",            \
      L"OK", L"OK", L"OK", L"OK")                                                                  \
    X(DlgCancel, L"Cancel", L"Annulla", L"Abbrechen", L"Annuler", L"Mégse", L"Скасувати",          \
      L"Anulare", L"Cancelar", L"Άκυρο", L"Cancelar", L"Anuluj", L"Annuleren", L"Storno",          \
      L"Avbryt")                                                                                   \
    /* sync points dialog */                                                                       \
    X(SyncPtsDlgTitle, L"Sync Points", L"Punti di sincronizzazione", L"Sync-Punkte",               \
      L"Points de synchronisation", L"Szinkronpontok", L"Точки синхронізації",                     \
      L"Puncte de sincronizare", L"Pontos de sincronização", L"Σημεία συγχρονισμού",               \
      L"Puntos de sincronización", L"Punkty synchronizacji", L"Synchronisatiepunten",              \
      L"Synchronizační body", L"Synkpunkter")                                                      \
    X(SyncPtsColIndex, L"#", L"#", L"#", L"#", L"#", L"#", L"#", L"#", L"#", L"#", L"#",           \
      L"#", L"#", L"#")                                                                            \
    X(SyncPtsColNumbering, L"Numbering", L"Numerazione", L"Nummerierung", L"Numérotation",         \
      L"Számozás", L"Нумерація", L"Numerotare", L"Numeração", L"Αρίθμηση", L"Numeración",          \
      L"Numeracja", L"Nummering", L"Číslování", L"Numrering")                                      \
    X(SyncPtsColPages, L"Pages", L"Pagine", L"Seiten", L"Pages", L"Oldalak", L"Сторінки",          \
      L"Pagini", L"Páginas", L"Σελίδες", L"Páginas", L"Strony", L"Pagina's", L"Stránky",           \
      L"Sidor")                                                                                    \
    X(SyncPtsColOrigin, L"Origin", L"Origine", L"Ursprung", L"Origine", L"Eredet",                 \
      L"Походження", L"Origine", L"Origem", L"Προέλευση", L"Origen", L"Pochodzenie",               \
      L"Herkomst", L"Původ", L"Ursprung")                                                          \
    X(SyncPtsDlgRemove, L"&Remove", L"&Rimuovi", L"&Entfernen", L"&Supprimer",                     \
      L"&Eltávolítás", L"&Видалити", L"&Elimină", L"&Remover", L"&Κατάργηση", L"&Quitar",          \
      L"&Usuń", L"&Verwijderen", L"&Odebrat", L"&Ta bort")                                         \
    X(SyncPtsDlgClear, L"Clear &All", L"Rimuovi &tutti", L"&Alle entfernen",                       \
      L"&Tout supprimer", L"Összes &törlése", L"Видалити &всі", L"Șterge &tot",                    \
      L"Limpar &tudo", L"Κατάργηση ό&λων", L"Quitar &todo", L"Usuń &wszystkie",                    \
      L"A&lles verwijderen", L"Odebrat &vše", L"Ta bort &alla")                                    \
    X(DlgClose, L"Close", L"Chiudi", L"Schließen", L"Fermer", L"Bezárás", L"Закрити",              \
      L"Închidere", L"Fechar", L"Κλείσιμο", L"Cerrar", L"Zamknij", L"Sluiten", L"Zavřít",          \
      L"Stäng")                                                                                    \
    X(SyncPtOriginAuto, L"auto", L"auto", L"automatisch", L"auto", L"automatikus", L"авто",        \
      L"automat", L"automático", L"αυτόματο", L"automático", L"automatyczny",                      \
      L"automatisch", L"automatický", L"automatisk")                                               \
    X(SyncPtOriginManual, L"manual", L"manuale", L"manuell", L"manuel", L"kézi", L"вручну",        \
      L"manual", L"manual", L"χειροκίνητο", L"manual", L"ręczny", L"handmatig", L"ruční",          \
      L"manuell")                                                                                  \
    /* about box */                                                                                \
    X(AboutTitle, L"About PDF Side Viewer", L"Informazioni su PDF Side Viewer",                    \
      L"Über PDF Side Viewer", L"À propos de PDF Side Viewer",                                     \
      L"A PDF Side Viewer névjegye", L"Про PDF Side Viewer",                                       \
      L"Despre PDF Side Viewer", L"Acerca do PDF Side Viewer",                                     \
      L"Πληροφορίες για το PDF Side Viewer", L"Acerca de PDF Side Viewer",                         \
      L"Informacje o PDF Side Viewer", L"Info over PDF Side Viewer",                               \
      L"O aplikaci PDF Side Viewer", L"Om PDF Side Viewer")                                        \
    X(AboutBody,                                                                                   \
      L"Two PDFs side by side with synchronised scrolling.\n\n"                                    \
      L"(c) 2026 Lorenzo Novara - GPLv3\nPDF rendering: MuPDF (AGPLv3) by Artifex Software",       \
      L"Due PDF affiancati con scorrimento sincronizzato.\n\n"                                     \
      L"(c) 2026 Lorenzo Novara - GPLv3\nRendering PDF: MuPDF (AGPLv3) di Artifex Software",       \
      L"Zwei PDFs nebeneinander mit synchronisiertem Bildlauf.\n\n"                                \
      L"(c) 2026 Lorenzo Novara - GPLv3\nPDF-Rendering: MuPDF (AGPLv3) von Artifex Software",      \
      L"Deux PDF côte à côte avec défilement synchronisé.\n\n"                                     \
      L"(c) 2026 Lorenzo Novara - GPLv3\nRendu PDF : MuPDF (AGPLv3) par Artifex Software",         \
      L"Két PDF egymás mellett, szinkronizált görgetéssel.\n\n"                                    \
      L"(c) 2026 Lorenzo Novara - GPLv3\nPDF-megjelenítés: MuPDF (AGPLv3), Artifex Software",      \
      L"Два PDF поруч із синхронізованим прокручуванням.\n\n"                                      \
      L"(c) 2026 Lorenzo Novara - GPLv3\nВідтворення PDF: MuPDF (AGPLv3) від Artifex Software",    \
      L"Două PDF-uri alăturate cu derulare sincronizată.\n\n"                                      \
      L"(c) 2026 Lorenzo Novara - GPLv3\nRedare PDF: MuPDF (AGPLv3) de Artifex Software",          \
      L"Dois PDFs lado a lado com deslocamento sincronizado.\n\n"                                  \
      L"(c) 2026 Lorenzo Novara - GPLv3\nRenderização de PDF: MuPDF (AGPLv3) da Artifex Software", \
      L"Δύο PDF δίπλα-δίπλα με συγχρονισμένη κύλιση.\n\n"                                          \
      L"(c) 2026 Lorenzo Novara - GPLv3\nΑπόδοση PDF: MuPDF (AGPLv3) από την Artifex Software",    \
      L"Dos PDF lado a lado con desplazamiento sincronizado.\n\n"                                  \
      L"(c) 2026 Lorenzo Novara - GPLv3\nRepresentación de PDF: MuPDF (AGPLv3) de "                \
      L"Artifex Software",                                                                         \
      L"Dwa PDF-y obok siebie z synchronizowanym przewijaniem.\n\n"                                \
      L"(c) 2026 Lorenzo Novara - GPLv3\nRenderowanie PDF: MuPDF (AGPLv3) od Artifex Software",    \
      L"Twee PDF's naast elkaar met gesynchroniseerd scrollen.\n\n"                                \
      L"(c) 2026 Lorenzo Novara - GPLv3\nPDF-weergave: MuPDF (AGPLv3) van Artifex Software",       \
      L"Dva dokumenty PDF vedle sebe se synchronizovaným posouváním.\n\n"                          \
      L"(c) 2026 Lorenzo Novara - GPLv3\nVykreslování PDF: MuPDF (AGPLv3) od Artifex Software",    \
      L"Två PDF-filer sida vid sida med synkroniserad rullning.\n\n"                               \
      L"(c) 2026 Lorenzo Novara - GPLv3\nPDF-rendering: MuPDF (AGPLv3) av Artifex Software")

// Order = the per-language table order in Strings.cpp = the persisted-code
// order ("en","it","de","fr","hu","uk","ro","pt","el","es","pl","nl","cs",
// "sv") = the language ID order (IDC_LANG_ENGLISH + i, MainWindow.h). Append
// only. The language menu DISPLAYS the items alphabetically by native name
// instead: position there is free because check and dispatch go MF_BYCOMMAND.
enum class Lang {
    English = 0,
    Italian = 1,
    German = 2,
    French = 3,
    Hungarian = 4,
    Ukrainian = 5,
    Romanian = 6,
    Portuguese = 7,
    Greek = 8,
    Spanish = 9,
    Polish = 10,
    Dutch = 11,
    Czech = 12,
    Swedish = 13,
};
constexpr int kLangCount = 14;

enum class StrId : size_t {
#define PSV_AS_ENUM(id, en, it, de, fr, hu, uk, ro, pt, el, es, pl, nl, cs, sv) id,
    PSV_STRING_LIST(PSV_AS_ENUM)
#undef PSV_AS_ENUM
        Count,
};

// UI-thread only (like all UI state): panes and frame read strings while
// painting and building menus; workers never touch them.
void SetUiLanguage(Lang lang);
Lang UiLanguage();
PCWSTR Str(StrId id);

// Two-letter codes persisted in settings.ini (kCodes in Strings.cpp, Lang
// order); anything unknown falls back to English.
Lang LangFromCode(const std::wstring& code);
PCWSTR LangCode(Lang lang);
