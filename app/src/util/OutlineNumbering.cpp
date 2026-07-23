#include "util/OutlineNumbering.h"

#include <cwctype>
#include <initializer_list>
#include <map>
#include <set>

// Grammar accepted by ParseOutlineNumbering:
//
//   title := ws* intro? key rest
//   intro := word '.'? ws*    lowercase(word) in a fixed set of section words:
//                             IT/EN capitolo/cap/chapter/ch, sezione/sez/
//                             section/sec, parte/part, appendice/appendix/
//                             annex/allegato; DE kapitel/kap, abschnitt/abschn,
//                             teil, anhang/anh, anlage; FR chapitre/chap, sect,
//                             partie, annexe (section/sec/appendice shared);
//                             HU fejezet/fej, szakasz, rész, függelék, melléklet;
//                             UK розділ/розд, глава/гл, частина, додаток/дод;
//                             RO capitol(ul), secțiune(a), partea, anexă/anexa
//                             (cedilla legacy spellings included); PT capítulo,
//                             seção/secção, apêndice, anexo (cap/parte shared);
//                             EL κεφάλαιο/κεφ, ενότητα, μέρος, παράρτημα (plus
//                             accent-less variants: Greek ALL-CAPS drops the
//                             accents, and Σ lowercases to σ, not final ς);
//                             ES sección, apéndice (capítulo/cap/parte/anexo
//                             shared with PT); PL rozdział/rozdz, część,
//                             sekcja, dodatek, załącznik, aneks; NL hoofdstuk/
//                             hfst, sectie, paragraaf, deel, bijlage,
//                             aanhangsel; CS kapitola, oddíl, část, sekce,
//                             příloha, díl (kap/dodatek shared); SV avsnitt,
//                             bilaga (kapitel/kap/appendix shared; "del" is
//                             deliberately OUT: Spanish "Del 1 al 10" would
//                             parse as key [1])
//          | '§' ws*
//   key   := comp ('.' comp)* '.'?
//   comp  := [0-9]{1,6} | single ASCII letter
//   rest  := end-of-string | (ws | ':' | ')' | '-' | '–' | '—') anything
//
// The intro word is tokenized as the longest run of letters (accents and
// Cyrillic INCLUDED, so "rész"/"függelék"/"розділ" survive) and compared WHOLE
// against the list:
// "Sezioni 2" must not match "Sez". Digits are ASCII
// only (locale-dependent Unicode digits would make keys unstable across
// documents) and capped at six per component (rejects ISBN/date noise and can
// never overflow the int accumulator). Letter components ("Appendice A",
// "A.1 Notation") encode NEGATIVE (-1 = A) so they can never collide with
// numeric components; a LONE letter without an intro word is rejected as a
// word, not a numbering ("A day...", "E ora...") unless the caller passes
// allowLoneLetter (appendix sub-items "A Foo"/"B Bar", see
// MatchOutlineNumberings). "3 Title" is a valid
// single-level key by design; the noise it admits ("2026 Report" -> [2026])
// is harmless because a key only produces a sync point when the identical key
// parses out of the other document too. Titles that parse to no key at all
// participate in the matcher's TITLE channel instead (trimmed,
// case-insensitive equality at the SAME outline depth): "Sommario", "Indice
// analitico", "Bibliografia" pair up when both documents carry them at the
// matching level. That channel also
// canonicalizes known front/back-matter section names across Italian, English,
// German, French, Hungarian, Ukrainian, Romanian, Portuguese, Greek, Spanish,
// Polish, Dutch, Czech and Swedish (CanonicalTitleKey), so a title pairs with
// its translation ("Indice"/"Contents"/"Inhaltsverzeichnis"/"Table des
// matières"/"Tartalomjegyzék"/"Зміст"/"Cuprins"/"Índice"/"Περιεχόμενα"/
// "Spis treści"/"Inhoudsopgave"/"Obsah"/"Innehåll").

namespace {

bool IsWs(wchar_t c) {
    return c == L' ' || c == L'\t' || c == L' '; // NBSP shows up in PDF titles
}

bool IsAsciiDigit(wchar_t c) {
    return c >= L'0' && c <= L'9';
}

bool IsAsciiAlpha(wchar_t c) {
    return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z');
}

// A letter of an intro WORD: accents/umlauts included (German "Anhang" is
// ASCII, but Hungarian "rész"/"függelék"/"melléklet" are not). GetStringTypeW
// classifies from the Unicode tables with no locale input; IsCharAlphaW is
// documented to follow the language the USER selected, and a CRT iswalpha
// follows the C locale - either would make bookmark matching depend on the
// host Windows configuration. Digits and letter COMPONENTS stay ASCII by
// design (see the grammar).
bool IsWordLetter(wchar_t c) {
    WORD type = 0;
    if (!GetStringTypeW(CT_CTYPE1, &c, 1, &type))
        return false;
    return (type & C1_ALPHA) != 0;
}

// Invariant-locale lowercasing for every comparison key in this file:
// CharLowerBuffW follows the user's language too (under a Turkish locale "I"
// drops to dotless "ı" and every ASCII-keyed table lookup silently misses);
// LCMapStringEx with the invariant locale is deterministic. LCMAP_LOWERCASE
// maps 1:1, so a length mismatch (or failure) leaves the text unchanged
// rather than truncated.
void LowerInvariant(std::wstring& text) {
    if (text.empty())
        return;
    const int n = static_cast<int>(text.size());
    std::wstring out(text.size(), L'\0');
    if (LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, text.c_str(), n, out.data(), n,
                      nullptr, nullptr, 0) == n)
        text = std::move(out);
}

bool IsTerminator(wchar_t c) {
    return IsWs(c) || c == L':' || c == L')' || c == L'-' || c == L'–' || c == L'—';
}

bool IsIntroWord(const std::wstring& lower) {
    static constexpr PCWSTR kWords[] = {
        // Italian / English
        L"capitolo", L"cap", L"chapter", L"ch", L"sezione", L"sez", L"section", L"sec",
        L"parte", L"part", L"appendice", L"appendix", L"annex", L"allegato",
        // German
        L"kapitel", L"kap", L"abschnitt", L"abschn", L"teil", L"anhang", L"anh", L"anlage",
        // French (section/sec/appendice/part already covered above)
        L"chapitre", L"chap", L"sect", L"partie", L"annexe",
        // Hungarian (accented words rely on the Unicode-aware tokenizer below)
        L"fejezet", L"fej", L"szakasz", L"rész", L"függelék", L"melléklet",
        // Ukrainian (Cyrillic: the intro branch accepts non-ASCII first letters)
        L"розділ", L"розд", L"глава", L"гл", L"частина", L"додаток", L"дод",
        // Romanian, with definite-article forms ("Capitolul 1", "Anexa A") and
        // the legacy cedilla spellings (ş/ţ) older PDFs carry instead of ș/ț
        L"capitol", L"capitolul", L"secțiune", L"secțiunea", L"secţiune", L"secţiunea",
        L"partea", L"anexă", L"anexa",
        // Portuguese (cap/parte shared above; secção pt-PT, seção pt-BR)
        L"capítulo", L"seção", L"secção", L"apêndice", L"anexo",
        // Greek, with the accent-less/σ-final forms ALL-CAPS titles lower to
        L"κεφάλαιο", L"κεφαλαιο", L"κεφ", L"ενότητα", L"ενοτητα",
        L"μέρος", L"μερος", L"μεροσ", L"παράρτημα", L"παραρτημα",
        // Spanish (capítulo/cap/parte/anexo shared with Portuguese above)
        L"sección", L"seccion", L"apéndice",
        // Polish
        L"rozdział", L"rozdz", L"część", L"sekcja", L"dodatek", L"załącznik", L"aneks",
        // Dutch (appendix shared with EN above)
        L"hoofdstuk", L"hfst", L"sectie", L"paragraaf", L"deel", L"bijlage", L"aanhangsel",
        // Czech (kap shared with DE, dodatek with PL)
        L"kapitola", L"oddíl", L"část", L"sekce", L"příloha", L"díl",
        // Swedish (kapitel/kap shared with DE, appendix with EN; "del" left
        // out on purpose: Spanish "Del 1 al 10" would parse as a numbering)
        L"avsnitt", L"bilaga"};
    for (PCWSTR w : kWords)
        if (lower == w)
            return true;
    return false;
}

// A letter component must be a SINGLE letter ("A", not "AB").
bool IsSingleLetterAt(const std::wstring& title, size_t i) {
    return i < title.size() && IsAsciiAlpha(title[i]) &&
           (i + 1 >= title.size() || !IsAsciiAlpha(title[i + 1]));
}

std::wstring NormalizedTitleKey(const std::wstring& title) {
    size_t b = 0;
    size_t e = title.size();
    while (b < e && IsWs(title[b]))
        ++b;
    while (e > b && IsWs(title[e - 1]))
        --e;
    std::wstring key = title.substr(b, e - b);
    LowerInvariant(key);
    return key;
}

// Cross-language canonical classes for the TITLE channel: known front/back
// matter section names collapse to a shared id so a title pairs with its
// translation. Members are stored NORMALIZED (invariant-lowercase, no outer
// whitespace) so the key from NormalizedTitleKey matches verbatim; the ids are
// natural English words that self-identify their own class ("index" -> index).
// Coverage: Italian, English, German, French, Hungarian, Ukrainian, Romanian,
// Portuguese, Greek (whose common single-word headings also list the
// accent-less spelling ALL-CAPS titles lower to, "εισαγωγη" for "Εισαγωγή"),
// Spanish, Polish, Dutch, Czech, Swedish.
// Italian "Indice" (the front summary) and English/German "Index"
// (the back analytical index) are FALSE FRIENDS and live in DIFFERENT classes
// on purpose: pairing them would align a document's table of contents with the
// other's alphabetical index; Portuguese "Índice" sides with the TOC like the
// Italian convention, its back index being "Índice remissivo". A title in
// no class keeps matching by its own normalized text, so same-language exact
// pairs (and unknown sections) are unaffected. "Sommario" reads as the table
// of contents (Italian book convention) rather than as an abstract.
struct TitleClass {
    PCWSTR id;
    std::initializer_list<PCWSTR> members;
};

const std::map<std::wstring, std::wstring>& TitleClassMap() {
    static const std::map<std::wstring, std::wstring> byMember = [] {
        static const TitleClass kClasses[] = {
            {L"toc", {L"indice", L"sommario", L"indice generale", L"indice dei contenuti",
                      L"contents", L"table of contents",
                      L"inhaltsverzeichnis", L"inhalt",
                      L"table des matières", L"sommaire",
                      L"tartalomjegyzék", L"tartalom",
                      L"зміст",
                      L"cuprins",
                      L"índice", L"índice geral", L"sumário", L"conteúdo",
                      L"περιεχόμενα", L"περιεχομενα", L"πίνακας περιεχομένων",
                      L"índice general", L"contenido", L"contenidos", L"sumario",
                      L"tabla de contenidos",
                      L"spis treści",
                      L"inhoudsopgave", L"inhoud",
                      L"obsah",
                      L"innehåll", L"innehållsförteckning"}},
            {L"index", {L"indice analitico", L"indice dei nomi", L"indice dei termini",
                        L"index",
                        L"stichwortverzeichnis", L"sachregister", L"sachverzeichnis", L"register",
                        L"index alphabétique",
                        L"tárgymutató", L"névmutató", L"mutató",
                        L"покажчик", L"предметний покажчик", L"іменний покажчик",
                        L"алфавітний покажчик",
                        L"index alfabetic",
                        L"índice remissivo", L"índice alfabético", L"índice onomástico",
                        L"ευρετήριο", L"ευρετηριο", L"αλφαβητικό ευρετήριο",
                        L"ευρετήριο όρων",
                        L"índice analítico", L"índice temático",
                        L"indeks", L"skorowidz",
                        L"trefwoordenregister",
                        L"rejstřík", L"věcný rejstřík", L"jmenný rejstřík",
                        L"sakregister", L"personregister"}},
            {L"preface", {L"prefazione", L"premessa", L"presentazione",
                          L"preface", L"foreword",
                          L"vorwort", L"geleitwort",
                          L"préface", L"avant-propos",
                          L"előszó",
                          L"передмова",
                          L"prefață", L"prefaţă", L"cuvânt înainte",
                          L"prefácio",
                          L"πρόλογος", L"προλογος", L"προλογοσ",
                          L"prefacio", L"prólogo",
                          L"przedmowa",
                          L"voorwoord", L"woord vooraf",
                          L"předmluva",
                          L"förord"}},
            {L"introduction", {L"introduzione",
                               L"introduction",
                               L"einleitung", L"einführung",
                               L"bevezetés", L"bevezető",
                               L"вступ",
                               L"introducere",
                               L"introdução",
                               L"εισαγωγή", L"εισαγωγη",
                               L"introducción",
                               L"wstęp", L"wprowadzenie",
                               L"inleiding", L"introductie",
                               L"úvod",
                               L"inledning", L"introduktion"}},
            {L"acknowledgments", {L"ringraziamenti",
                                  L"acknowledgments", L"acknowledgements",
                                  L"danksagung", L"dank",
                                  L"remerciements",
                                  L"köszönetnyilvánítás", L"köszönet",
                                  L"подяки", L"подяка",
                                  L"mulțumiri", L"mulţumiri",
                                  L"agradecimentos",
                                  L"ευχαριστίες", L"ευχαριστιες", L"ευχαριστιεσ",
                                  L"agradecimientos",
                                  L"podziękowania",
                                  L"dankwoord", L"dankbetuiging",
                                  L"poděkování",
                                  L"tack"}},
            {L"abstract", {L"abstract", L"riassunto",
                           L"summary",
                           L"zusammenfassung", L"kurzfassung",
                           L"résumé",
                           L"kivonat", L"absztrakt", L"összefoglaló", L"összefoglalás",
                           L"анотація", L"реферат",
                           L"rezumat",
                           L"resumo",
                           L"περίληψη", L"περιληψη",
                           L"resumen",
                           L"streszczenie", L"abstrakt",
                           L"samenvatting",
                           L"shrnutí", L"souhrn", L"resumé",
                           L"sammanfattning", L"sammandrag"}},
            {L"conclusion", {L"conclusione", L"conclusioni",
                             L"conclusion", L"conclusions",
                             L"schluss", L"fazit", L"schlussfolgerung",
                             L"következtetés", L"következtetések", L"összegzés",
                             L"висновки", L"висновок",
                             L"concluzii", L"concluzie",
                             L"conclusão", L"conclusões",
                             L"συμπεράσματα", L"συμπερασματα", L"συμπέρασμα",
                             L"συμπερασμα",
                             L"conclusión", L"conclusiones",
                             L"podsumowanie", L"wnioski", L"zakończenie",
                             L"conclusie", L"conclusies", L"besluit",
                             L"závěr", L"závěry",
                             L"slutsats", L"slutsatser", L"avslutning"}},
            {L"afterword", {L"postfazione", L"epilogo",
                            L"afterword", L"epilogue",
                            L"nachwort", L"epilog",
                            L"postface", L"épilogue",
                            L"utószó", L"epilógus",
                            L"післямова", L"епілог",
                            L"postfață", L"postfaţă",
                            L"posfácio", L"epílogo",
                            L"επίλογος", L"επιλογος", L"επιλογοσ",
                            L"posfacio",
                            L"posłowie",
                            L"nawoord",
                            L"doslov",
                            L"efterord"}},
            {L"appendix", {L"appendice", L"appendici",
                           L"appendix", L"appendices",
                           L"anhang", L"anhänge",
                           L"annexe", L"annexes",
                           L"függelék", L"függelékek", L"melléklet", L"mellékletek",
                           L"додаток", L"додатки",
                           L"anexă", L"anexa", L"anexe", L"apendice",
                           L"apêndice", L"apêndices", L"anexo", L"anexos",
                           L"παράρτημα", L"παραρτημα", L"παραρτήματα", L"παραρτηματα",
                           L"apéndice", L"apéndices",
                           L"dodatek", L"dodatki", L"załącznik", L"załączniki",
                           L"aneks", L"aneksy",
                           L"bijlage", L"bijlagen", L"aanhangsel",
                           L"příloha", L"přílohy", L"dodatky",
                           L"bilaga", L"bilagor"}},
            {L"glossary", {L"glossario",
                           L"glossary",
                           L"glossar",
                           L"glossaire",
                           L"szójegyzék", L"fogalomtár", L"szótár",
                           L"глосарій", L"словник термінів",
                           L"glosar",
                           L"glossário",
                           L"γλωσσάρι", L"γλωσσαρι", L"γλωσσάριο",
                           L"glosario",
                           L"słownik", L"słowniczek", L"glosariusz",
                           L"woordenlijst", L"verklarende woordenlijst", L"glossarium",
                           L"slovník pojmů", L"glosář", L"slovníček",
                           L"ordlista"}},
            {L"bibliography", {L"bibliografia", L"riferimenti bibliografici", L"riferimenti",
                               L"opere citate", L"fonti",
                               L"bibliography", L"references", L"works cited", L"sources",
                               L"literaturverzeichnis", L"literatur", L"bibliografie",
                               L"bibliographie", L"quellenverzeichnis", L"quellen",
                               L"références", L"ouvrages cités",
                               L"bibliográfia", L"irodalom", L"irodalomjegyzék",
                               L"felhasznált irodalom", L"hivatkozások", L"források",
                               L"бібліографія", L"література", L"список літератури",
                               L"список використаних джерел", L"джерела",
                               L"referințe", L"referinţe", L"referințe bibliografice",
                               L"surse",
                               L"referências", L"referências bibliográficas", L"fontes",
                               L"obras citadas",
                               L"βιβλιογραφία", L"βιβλιογραφια",
                               L"αναφορές", L"αναφορες", L"αναφορεσ",
                               L"πηγές", L"πηγες", L"πηγεσ",
                               L"bibliografía", L"referencias",
                               L"referencias bibliográficas", L"fuentes",
                               L"piśmiennictwo", L"źródła",
                               L"literatuurlijst", L"bronnen", L"bronvermelding",
                               L"referenties",
                               L"literatura", L"seznam literatury",
                               L"použitá literatura", L"zdroje",
                               // Czech "reference" is left out: it collides
                               // with the English singular "Reference"
                               // (reference material, NOT a bibliography)
                               L"litteraturförteckning", L"referenser", L"källor",
                               L"källförteckning"}},
            {L"symbols", {L"indice dei simboli", L"elenco dei simboli", L"simboli",
                          L"symbol index", L"list of symbols", L"notation", L"notations",
                          L"symbolverzeichnis", L"formelzeichen",
                          L"liste des symboles", L"symboles",
                          L"jelölések", L"jelölésjegyzék",
                          L"умовні позначення", L"перелік умовних позначень",
                          L"simboluri", L"lista simbolurilor", L"notații", L"notaţii",
                          L"símbolos", L"lista de símbolos", L"notação",
                          L"σύμβολα", L"συμβολα", L"κατάλογος συμβόλων",
                          L"notación",
                          L"wykaz symboli", L"symbole", L"oznaczenia",
                          L"symbolenlijst", L"lijst van symbolen", L"notatie",
                          L"seznam symbolů", L"symboly", L"značení",
                          L"symbollista", L"symboler", L"beteckningar"}},
            {L"version-history", {L"cronologia",
                                  L"version history", L"revision history", L"changelog",
                                  L"versionsgeschichte", L"änderungsverlauf",
                                  L"historique des versions", L"journal des modifications",
                                  L"verziótörténet", L"változásnapló",
                                  L"історія версій", L"журнал змін",
                                  L"istoricul versiunilor", L"istoric versiuni",
                                  L"jurnal de modificări",
                                  L"histórico de versões", L"registo de alterações",
                                  L"ιστορικό εκδόσεων", L"ιστορικο εκδοσεων",
                                  L"historial de versiones", L"registro de cambios",
                                  L"historia wersji", L"historia zmian",
                                  L"versiegeschiedenis", L"wijzigingslogboek",
                                  L"historie verzí", L"seznam změn",
                                  L"versionshistorik", L"ändringslogg"}},
            {L"notes", {L"note",
                        L"notes",
                        L"anmerkungen",
                        L"jegyzetek", L"megjegyzések",
                        L"примітки",
                        L"notas",
                        L"σημειώσεις", L"σημειωσεις", L"σημειωσεισ",
                        L"przypisy", L"uwagi", L"notatki",
                        L"noten", L"aantekeningen", L"opmerkingen",
                        L"poznámky",
                        L"noter", L"anteckningar"}},
            {L"figures", {L"elenco delle figure", L"indice delle figure",
                          L"list of figures",
                          L"abbildungsverzeichnis",
                          L"liste des figures", L"table des figures",
                          L"ábrajegyzék", L"ábrák jegyzéke",
                          L"перелік ілюстрацій", L"список ілюстрацій", L"перелік рисунків",
                          L"lista figurilor",
                          L"lista de figuras", L"índice de figuras",
                          L"κατάλογος σχημάτων", L"κατάλογος εικόνων",
                          L"spis rysunków", L"wykaz rysunków", L"spis ilustracji",
                          L"lijst van figuren", L"figurenlijst",
                          L"lijst van afbeeldingen",
                          L"seznam obrázků",
                          L"figurförteckning", L"lista över figurer"}},
            {L"tables", {L"elenco delle tabelle", L"indice delle tabelle",
                         L"list of tables",
                         L"tabellenverzeichnis",
                         L"liste des tableaux",
                         L"táblázatjegyzék", L"táblázatok jegyzéke",
                         L"перелік таблиць", L"список таблиць",
                         L"lista tabelelor",
                         L"lista de tabelas", L"índice de tabelas",
                         L"κατάλογος πινάκων",
                         L"lista de tablas", L"índice de tablas",
                         L"spis tabel", L"wykaz tabel",
                         L"lijst van tabellen", L"tabellenlijst",
                         L"seznam tabulek",
                         L"tabellförteckning", L"lista över tabeller"}},
            {L"abbreviations", {L"abbreviazioni", L"sigle", L"elenco delle abbreviazioni",
                                L"abbreviations", L"list of abbreviations",
                                L"abkürzungsverzeichnis", L"abkürzungen",
                                L"abréviations", L"liste des abréviations", L"sigles",
                                L"rövidítések", L"rövidítésjegyzék",
                                L"перелік скорочень", L"список скорочень", L"скорочення",
                                L"abrevieri", L"lista abrevierilor",
                                L"abreviaturas", L"lista de abreviaturas", L"siglas",
                                L"συντομογραφίες", L"συντομογραφιες", L"συντομογραφιεσ",
                                L"κατάλογος συντομογραφιών",
                                L"abreviaciones",
                                L"wykaz skrótów", L"skróty", L"lista skrótów",
                                L"afkortingen", L"lijst van afkortingen",
                                L"afkortingenlijst",
                                L"seznam zkratek", L"zkratky",
                                L"förkortningar", L"förkortningslista"}},
            {L"dedication", {L"dedica",
                             L"dedication",
                             L"widmung",
                             L"dédicace",
                             L"ajánlás",
                             L"присвята",
                             L"dedicație", L"dedicaţie",
                             L"dedicatória",
                             L"αφιέρωση", L"αφιερωση",
                             L"dedicatoria",
                             L"dedykacja",
                             L"věnování",
                             L"tillägnan"}},
        };
        std::map<std::wstring, std::wstring> m;
        for (const TitleClass& c : kClasses)
            for (PCWSTR member : c.members)
                m.emplace(member, c.id); // first class wins on the odd shared spelling
        return m;
    }();
    return byMember;
}

// Normalized title mapped to its cross-language class id, or the normalized
// title itself when it belongs to no class (preserving exact-match pairing).
std::wstring CanonicalTitleKey(const std::wstring& title) {
    std::wstring norm = NormalizedTitleKey(title);
    if (norm.empty())
        return norm;
    const auto& byMember = TitleClassMap();
    const auto it = byMember.find(norm);
    return it == byMember.end() ? norm : it->second;
}

bool IsAppendixHeading(const std::wstring& title) {
    return CanonicalTitleKey(title) == L"appendix";
}

// Per item, whether its immediate parent (the nearest preceding shallower item)
// is an appendix heading. LaTeX \appendix labels sub-items "A Foo"/"B Bar" with
// no intro word, so under such a parent a lone leading letter is a numbering,
// not the article "A" - this gates ParseOutlineNumbering's allowLoneLetter so
// ordinary "A day..." titles elsewhere stay rejected. depth is 0-based from the
// root and the outline is in preorder, so lastAtDepth[d-1] is the live parent.
std::vector<bool> AppendixChildFlags(const std::vector<Document::OutlineItem>& items) {
    std::vector<bool> flags(items.size(), false);
    std::vector<int> lastAtDepth;
    for (size_t i = 0; i < items.size(); ++i) {
        int d = items[i].depth < 0 ? 0 : items[i].depth;
        if (static_cast<size_t>(d) >= lastAtDepth.size())
            lastAtDepth.resize(static_cast<size_t>(d) + 1, -1);
        if (d > 0 && lastAtDepth[static_cast<size_t>(d) - 1] >= 0)
            flags[i] = IsAppendixHeading(
                items[static_cast<size_t>(lastAtDepth[static_cast<size_t>(d) - 1])].title);
        lastAtDepth[static_cast<size_t>(d)] = static_cast<int>(i);
    }
    return flags;
}

} // namespace

std::optional<std::vector<int>> ParseOutlineNumbering(const std::wstring& title,
                                                      bool allowLoneLetter) {
    const size_t n = title.size();
    size_t i = 0;
    const auto skipWs = [&] {
        while (i < n && IsWs(title[i]))
            ++i;
    };
    skipWs();

    bool hadIntro = false;
    if (i < n && title[i] == L'§') {
        ++i;
        skipWs();
        hadIntro = true;
    } else if (i < n && IsWordLetter(title[i]) &&
               (!IsAsciiAlpha(title[i]) || (i + 1 < n && IsWordLetter(title[i + 1])))) {
        // An ASCII letter starting a >= 2-letter word can only be an intro word;
        // a single ASCII letter falls through to the component parser ("A.1
        // Notation"). A non-ASCII letter ALWAYS starts a word (components stay
        // ASCII by design), so Cyrillic "Розділ 1" reaches the intro lookup.
        // The run takes accented letters so "Rész 2"/"Függelék A" tokenize whole.
        size_t j = i;
        std::wstring word;
        while (j < n && IsWordLetter(title[j]))
            word.push_back(title[j++]);
        LowerInvariant(word);
        if (!IsIntroWord(word))
            return std::nullopt;
        i = j;
        if (i < n && title[i] == L'.')
            ++i;
        skipWs();
        hadIntro = true;
    }

    std::vector<int> key;
    for (;;) {
        int value;
        if (i < n && IsAsciiDigit(title[i])) {
            value = 0;
            size_t digits = 0;
            while (i < n && IsAsciiDigit(title[i])) {
                if (++digits > 6)
                    return std::nullopt;
                value = value * 10 + (title[i] - L'0');
                ++i;
            }
        } else if (IsSingleLetterAt(title, i)) {
            // Encoded negative (-1 = A): letter components can never collide
            // with numeric ones ("Appendice A" vs "Capitolo 1").
            value = -(static_cast<int>(towupper(title[i])) - L'A' + 1);
            ++i;
        } else {
            return std::nullopt; // a component must follow the start or a '.'
        }
        key.push_back(value);
        if (i < n && title[i] == L'.') {
            ++i;
            if ((i < n && IsAsciiDigit(title[i])) || IsSingleLetterAt(title, i))
                continue;   // "1.2", "A.1" -> next component
            break;          // trailing dot: "1.2." ends the key
        }
        break;
    }

    if (i < n && !IsTerminator(title[i]))
        return std::nullopt; // "1.2Foo"
    // A lone letter with no intro word is a WORD, not a numbering ("A day at
    // the races", "E ora?"): letters need an intro ("Appendice A") or more
    // components ("A.1"), UNLESS the caller vouches for an appendix context
    // where "A Foo"/"B Bar" are lettered sub-items (allowLoneLetter).
    if (!hadIntro && !allowLoneLetter && key.size() == 1 && key[0] < 0)
        return std::nullopt;
    return key;
}

std::wstring FormatOutlineNumbering(const std::vector<int>& key) {
    std::wstring text;
    for (int component : key) {
        if (!text.empty())
            text.push_back(L'.');
        if (component < 0)
            text.push_back(static_cast<wchar_t>(L'A' - component - 1)); // -1 = A
        else
            text += std::to_wstring(component);
    }
    return text;
}

std::vector<std::pair<int, int>> MatchOutlineNumberings(
    const std::vector<Document::OutlineItem>& left,
    const std::vector<Document::OutlineItem>& right) {
    // Two channels, first occurrence wins per key and per side: numbered
    // titles pair by numeric key; unnumbered ones pair by canonical title key
    // ("Sommario", "Indice analitico", or a cross-language class id such as
    // "Indice"/"Contents" -> "toc") AT THE SAME OUTLINE DEPTH. Numeric keys
    // carry their own hierarchy ("1.2" can only equal "1.2"), but canonical
    // titles do not: without the depth guard a top-level "Introduzione" can
    // pair with a chapter's nested unnumbered "Introduction" and, if the pair
    // happens to stay monotonic, become an authoritative anchor that drags a
    // whole segment to unrelated pages. Real translations share the outline
    // structure, so requiring equal depth costs nothing. Appendix sub-items
    // ("A Foo"/"B Bar" under an "Appendici"/"Appendices"/"Függelékek"
    // heading) join the numeric channel via their language-neutral letter.
    // Out-of-order accidental pairs die in the monotonicity filter downstream.
    const std::vector<bool> rightAppendix = AppendixChildFlags(right);
    const std::vector<bool> leftAppendix = AppendixChildFlags(left);
    const auto depthOf = [](const Document::OutlineItem& item) {
        return item.depth < 0 ? 0 : item.depth; // same clamp as AppendixChildFlags
    };
    std::map<std::vector<int>, int> rightByKey;
    std::map<std::pair<int, std::wstring>, int> rightByTitle;
    for (size_t i = 0; i < right.size(); ++i) {
        if (auto key = ParseOutlineNumbering(right[i].title, rightAppendix[i])) {
            rightByKey.emplace(std::move(*key), static_cast<int>(i));
        } else if (std::wstring t = CanonicalTitleKey(right[i].title); !t.empty()) {
            rightByTitle.emplace(std::pair{depthOf(right[i]), std::move(t)},
                                 static_cast<int>(i));
        }
    }
    std::vector<std::pair<int, int>> matches;
    std::set<std::vector<int>> leftSeen;
    std::set<std::pair<int, std::wstring>> leftSeenTitles;
    for (size_t i = 0; i < left.size(); ++i) {
        if (auto key = ParseOutlineNumbering(left[i].title, leftAppendix[i])) {
            const auto it = rightByKey.find(*key);
            if (it == rightByKey.end())
                continue;
            if (!leftSeen.insert(std::move(*key)).second)
                continue; // duplicate key on the left: first occurrence wins
            matches.emplace_back(static_cast<int>(i), it->second);
        } else if (std::wstring t = CanonicalTitleKey(left[i].title); !t.empty()) {
            std::pair<int, std::wstring> dt{depthOf(left[i]), std::move(t)};
            const auto it = rightByTitle.find(dt);
            if (it == rightByTitle.end())
                continue;
            if (!leftSeenTitles.insert(std::move(dt)).second)
                continue;
            matches.emplace_back(static_cast<int>(i), it->second);
        }
    }
    return matches;
}
