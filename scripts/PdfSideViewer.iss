; Inno Setup script for the winget / direct-download channel (the Microsoft
; Store channel is a separate MSIX, not this). Compile via
; scripts\make-installer.ps1, which passes AppVersion/FileVersion/RepoRoot;
; compiling this file directly without the defines is an error on purpose.
;
; Per-user install by design: no admin prompt, files under
; %LocalAppData%\Programs, uninstall entry and Explorer verbs in HKCU - the
; same hive the app's own Options checkbox writes, so the two stay one model.
; The AppId GUID is the upgrade identity: NEVER change it.

#ifndef AppVersion
  #error Use scripts\make-installer.ps1 (missing /DAppVersion)
#endif
#ifndef FileVersion
  #error Use scripts\make-installer.ps1 (missing /DFileVersion)
#endif
#ifndef RepoRoot
  #error Use scripts\make-installer.ps1 (missing /DRepoRoot)
#endif

[Setup]
AppId={{9D3C1A5E-4B7F-4E2A-9C61-0E8F2B7D4A31}
AppName=PDF Side Viewer
AppVersion={#AppVersion}
AppVerName=PDF Side Viewer {#AppVersion}
AppPublisher=Lorenzo Novara
AppPublisherURL=https://github.com/lornova/PdfSideViewer
AppSupportURL=https://github.com/lornova/PdfSideViewer/issues
AppUpdatesURL=https://github.com/lornova/PdfSideViewer/releases
DefaultDirName={autopf}\PDF Side Viewer
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
LicenseFile={#RepoRoot}\LICENSE
OutputDir={#RepoRoot}\build
OutputBaseFilename=PdfSideViewer-Setup-x64
SetupIconFile={#RepoRoot}\app\res\app.ico
UninstallDisplayIcon={app}\PdfSideViewer.exe
UninstallDisplayName=PDF Side Viewer
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
CloseApplications=yes
VersionInfoVersion={#FileVersion}

[Languages]
; Romanian and Greek are app UI languages (Strings.h) but have no official
; Inno Setup translation, so the wizard itself cannot offer them.
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "italian"; MessagesFile: "compiler:Languages\Italian.isl"
Name: "german"; MessagesFile: "compiler:Languages\German.isl"
Name: "french"; MessagesFile: "compiler:Languages\French.isl"
Name: "hungarian"; MessagesFile: "compiler:Languages\Hungarian.isl"
Name: "ukrainian"; MessagesFile: "compiler:Languages\Ukrainian.isl"
Name: "portuguese"; MessagesFile: "compiler:Languages\Portuguese.isl"
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "polish"; MessagesFile: "compiler:Languages\Polish.isl"
Name: "dutch"; MessagesFile: "compiler:Languages\Dutch.isl"
Name: "czech"; MessagesFile: "compiler:Languages\Czech.isl"
Name: "swedish"; MessagesFile: "compiler:Languages\Swedish.isl"

; Same wording as the app's Options checkbox (Strings.h OptShellIntegration).
[CustomMessages]
english.ShellTask=Show "Open left/centre/right in PDF Side Viewer" in the Explorer menu for PDF files
italian.ShellTask=Mostra "Apri a sinistra/al centro/a destra in PDF Side Viewer" nel menu di Esplora file per i PDF
german.ShellTask=„Links/Mitte/Rechts in PDF Side Viewer öffnen“ im Explorer-Menü für PDF-Dateien anzeigen
french.ShellTask=Afficher « Ouvrir à gauche/au centre/à droite dans PDF Side Viewer » dans le menu de l'Explorateur pour les PDF
hungarian.ShellTask=„Megnyitás balra/középre/jobbra a PDF Side Viewerben” megjelenítése az Intéző menüjében a PDF-ekhez
ukrainian.ShellTask=Показувати «Відкрити ліворуч/по центру/праворуч у PDF Side Viewer» у меню Провідника для файлів PDF
portuguese.ShellTask=Mostrar «Abrir à esquerda/ao centro/à direita no PDF Side Viewer» no menu do Explorador para ficheiros PDF
spanish.ShellTask=Mostrar «Abrir a la izquierda/en el centro/a la derecha en PDF Side Viewer» en el menú del Explorador para archivos PDF
polish.ShellTask=Pokaż „Otwórz po lewej/na środku/po prawej w PDF Side Viewer” w menu Eksploratora dla plików PDF
dutch.ShellTask="Links/midden/rechts openen in PDF Side Viewer" tonen in het Verkenner-menu voor PDF-bestanden
czech.ShellTask=Zobrazit „Otevřít vlevo/uprostřed/vpravo v PDF Side Viewer“ v nabídce Průzkumníka pro soubory PDF
swedish.ShellTask=Visa "Öppna till vänster/mitten/höger i PDF Side Viewer" i Utforskaren-menyn för PDF-filer

[Tasks]
Name: "shellintegration"; Description: "{cm:ShellTask}"
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; Flags: unchecked

[Files]
Source: "{#RepoRoot}\build\x64\Release\PdfSideViewer.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#RepoRoot}\LICENSE"; DestDir: "{app}"
Source: "{#RepoRoot}\README.md"; DestDir: "{app}"

[Icons]
Name: "{autoprograms}\PDF Side Viewer"; Filename: "{app}\PdfSideViewer.exe"
Name: "{autodesktop}\PDF Side Viewer"; Filename: "{app}\PdfSideViewer.exe"; Tasks: desktopicon

[Run]
; Headless verb registration via the app itself (single source of truth for
; the registry layout). This path writes English verb labels; re-toggling the
; checkbox in the app's Options rewrites them in the UI language.
Filename: "{app}\PdfSideViewer.exe"; Parameters: "-register-shell"; Tasks: shellintegration
Filename: "{app}\PdfSideViewer.exe"; Description: "{cm:LaunchProgram,PDF Side Viewer}"; Flags: nowait postinstall skipifsilent

[Code]
// Verb removal is uninstall-time CODE, not an [UninstallRun] entry: Check
// parameters on [UninstallRun] are evaluated during SETUP (documented Inno
// semantics) - at that point the verbs still hold their pre-install value, so
// the entry would be recorded (or dropped) from stale state. The removal
// itself is DELEGATED to the app (-unregister-shell-owned): per-verb exact
// ownership and the per-user lock live in ONE implementation instead of a
// Pascal reimplementation that would drift (its old substring match let a
// sibling "...-dev" directory pass, and it raced live copies).
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  ResultCode: Integer;
begin
  if CurUninstallStep = usUninstall then
  begin
    // The exe still exists at usUninstall: it removes the verbs it owns.
    // -unregister-shell-owned runs the SAME ownership parsing (exact first
    // quoted token) and the SAME per-user cross-process lock file as the
    // running app, so the read-check-delete sequence cannot race a live
    // copy's registration. There is deliberately NO direct-registry fallback:
    // an unlocked read-check-delete from this script could delete a verb a
    // portable copy rewrote in between, and a STALE verb left behind by a
    // corrupted install (exe that cannot launch, or a failed-closed exit
    // code, e.g. lock contention) is the safer failure - it is inert and
    // removable later with -unregister-shell from any working copy.
    if Exec(ExpandConstant('{app}\PdfSideViewer.exe'), '-unregister-shell-owned', '',
            SW_HIDE, ewWaitUntilTerminated, ResultCode) then
    begin
      // The argument array stays on THIS line: Inno reads a line whose first
      // non-blank character is '[' as a section header, so wrapping it would
      // fail the compile with "Invalid section tag".
      if ResultCode <> 0 then
        Log(Format('shell cleanup exited %d; owned verbs left behind', [ResultCode]));
    end
    else
      Log('PdfSideViewer.exe could not run; shell verbs (if any) left behind');
  end;
end;
