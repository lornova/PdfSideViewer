# E2E test for the find bar's search options and its icon toolbars: Match Case,
# Match Whole Word (including the ACCENTED case the regex route could never have
# handled), the options-are-part-of-the-query guard, the latched button state,
# persistence in [find], the tooltip ids, the width-shedding order and the
# keyboard tab order.
#
# CLAUDE.md testing rules: DPI-aware thread FIRST (the dev monitor is 175% and
# PowerShell is DPI-unaware), PSV_SETTINGS_DIR sandbox (never touch the user's
# settings.ini), abort if a foreign instance is running (posted commands would
# hit it), the exe must exit 0, retry-loop the settings deletion.
#
# Counts come from the find bar's own counter STATIC (control id 2503), which
# formats "<active+1>/<total>" and appends "+" while the scan is still running.
param([string]$Config = 'Debug', [switch]$Capture)

$ErrorActionPreference = 'Stop'
# A command id that is not in the constants block below reads as $null, which
# PowerShell coerces to 0, and WM_COMMAND 0 does NOTHING: the phase then
# "passes" without ever running its command.
Set-StrictMode -Version Latest

Add-Type -Namespace Win32 -Name Find -MemberDefinition @'
[DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr ctx);
// PowerShell coerces $null to "" for string parameters, and FindWindow treats
// "" as "empty title" instead of "any": the NULL side must be an IntPtr.
[DllImport("user32.dll", CharSet=CharSet.Unicode, EntryPoint="FindWindowW")] public static extern IntPtr FindWindowByClass(string cls, IntPtr title);
[DllImport("user32.dll", CharSet=CharSet.Unicode, EntryPoint="FindWindowExW")] public static extern IntPtr FindWindowExByClass(IntPtr parent, IntPtr after, string cls, IntPtr title);
[DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr hwnd, int id);
[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, System.Text.StringBuilder l);
[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, string l);
[DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
[DllImport("user32.dll")] public static extern bool GetScrollInfo(IntPtr h, int bar, ref SCROLLINFO si);
[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
[DllImport("user32.dll")] public static extern bool GetGUIThreadInfo(uint tid, ref GUITHREADINFO gui);
[DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
[DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
[DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
// PrintWindow, not CopyFromScreen: the capture must not depend on the window
// being unobscured, nor on the session having an interactive desktop at all.
[DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
[StructLayout(LayoutKind.Sequential)] public struct SCROLLINFO { public uint cbSize, fMask; public int nMin, nMax; public uint nPage; public int nPos, nTrackPos; }
[StructLayout(LayoutKind.Sequential)] public struct RECT { public int l, t, r, b; }
[StructLayout(LayoutKind.Sequential)] public struct GUITHREADINFO { public uint cbSize, flags; public IntPtr hwndActive, hwndFocus, hwndCapture, hwndMenuOwner, hwndMoveSize, hwndCaret; public RECT rcCaret; }
'@

[void][Win32.Find]::SetThreadDpiAwarenessContext([IntPtr](-4))

# --- constants (mirror MainWindow.h CommandId and the control ids) ---
$WM_COMMAND = 0x0111
$WM_SETTEXT = 0x000C
$WM_GETTEXT = 0x000D
$WM_KEYDOWN = 0x0100
$VK_TAB = 0x09
$TB_ISBUTTONCHECKED = 0x040A
$TB_GETTOOLTIPS = 0x0423
$SWP_NOMOVE = 0x0002
$SWP_NOZORDER = 0x0004
$SB_VERT = 1
$SIF_ALL = 0x17
$IDC_FIND_SHOW = 1006
$IDC_FIND_NEXT = 1007
$IDC_FIND_CLOSE = 1009
$IDC_FIND_EDIT = 1010
$IDC_FIND_MATCH_CASE = 1078
$IDC_FIND_WHOLE_WORD = 1079
$kFindOptsBarId = 2501
$kFindNavBarId = 2502
$kFindCountId = 2503
$kFindCloseBarId = 2504
$kMenuBandId = 2300   # MenuBand.h, the menu-bar toolbar inside the rebar
$TB_BUTTONCOUNT = 0x0418

$root = Split-Path $PSScriptRoot -Parent
$exe = Join-Path $root "build\x64\$Config\PdfSideViewer.exe"
if (-not (Test-Path $exe)) { throw "missing $exe (build $Config x64 first)" }
$pdfF = Join-Path $root 'testdata\find-a.pdf'
$pdfB = Join-Path $root 'testdata\test-b.pdf'
foreach ($f in $pdfF, $pdfB) {
    if (-not (Test-Path $f)) { throw "missing $f (run scripts\make-test-pdfs.ps1)" }
}

if (Get-Process PdfSideViewer -ErrorAction SilentlyContinue) {
    throw 'a PdfSideViewer instance is already running: aborting, posted commands would hit it'
}

$scratch = Join-Path $env:TEMP ('psv-find-test-' + [guid]::NewGuid().ToString('n'))
New-Item -ItemType Directory -Force $scratch | Out-Null
$env:PSV_SETTINGS_DIR = $scratch

$script:failures = 0
function Assert([bool]$cond, [string]$what) {
    if ($cond) { Write-Host "  ok:   $what" }
    else { Write-Host "  FAIL: $what" -ForegroundColor Red; $script:failures++ }
}
function Poll([scriptblock]$probe, [int]$timeoutMs = 10000) {
    $sw = [Diagnostics.Stopwatch]::StartNew()
    while ($sw.ElapsedMilliseconds -lt $timeoutMs) {
        if (& $probe) { return $true }
        Start-Sleep -Milliseconds 100
    }
    return [bool](& $probe)
}
function Get-CtrlText([IntPtr]$h) {
    if ($h -eq [IntPtr]::Zero) { return '' }
    $sb = New-Object System.Text.StringBuilder 128
    [void][Win32.Find]::SendMessageW($h, $WM_GETTEXT, [IntPtr]128, $sb)
    $sb.ToString()
}
function Get-FocusHwnd([IntPtr]$main) {
    $procId = [uint32]0
    $tid = [Win32.Find]::GetWindowThreadProcessId($main, [ref]$procId)
    $gui = New-Object Win32.Find+GUITHREADINFO
    $gui.cbSize = [Runtime.InteropServices.Marshal]::SizeOf($gui)
    if ([Win32.Find]::GetGUIThreadInfo($tid, [ref]$gui)) { return $gui.hwndFocus }
    return [IntPtr]::Zero
}

function Start-Viewer {
    $proc = Start-Process -FilePath $exe -ArgumentList @("`"$pdfF`"", "`"$pdfB`"") -PassThru
    if (-not (Poll { [Win32.Find]::FindWindowByClass('PsvMainWindow', [IntPtr]::Zero) -ne [IntPtr]::Zero } 15000)) {
        throw 'main window did not appear'
    }
    $main = [Win32.Find]::FindWindowByClass('PsvMainWindow', [IntPtr]::Zero)
    $v = @{ Proc = $proc; Main = $main }
    $ready = Poll {
        $v.Left = [Win32.Find]::GetDlgItem($main, 100)
        $v.Bar = [Win32.Find]::FindWindowExByClass($main, [IntPtr]::Zero, 'PsvFindBar', [IntPtr]::Zero)
        if ($v.Bar -eq [IntPtr]::Zero -or $v.Left -eq [IntPtr]::Zero) { return $false }
        $v.Edit = [Win32.Find]::GetDlgItem($v.Bar, $IDC_FIND_EDIT)
        $v.Count = [Win32.Find]::GetDlgItem($v.Bar, $kFindCountId)
        $v.Opts = [Win32.Find]::GetDlgItem($v.Bar, $kFindOptsBarId)
        $v.Nav = [Win32.Find]::GetDlgItem($v.Bar, $kFindNavBarId)
        $v.CloseBar = [Win32.Find]::GetDlgItem($v.Bar, $kFindCloseBarId)
        $rebar = [Win32.Find]::FindWindowExByClass($main, [IntPtr]::Zero, 'ReBarWindow32', [IntPtr]::Zero)
        $v.PageBox = if ($rebar -ne [IntPtr]::Zero) { [Win32.Find]::GetDlgItem($rebar, 2001) }
                     else { [IntPtr]::Zero }
        $v.MenuBand = if ($rebar -ne [IntPtr]::Zero) { [Win32.Find]::GetDlgItem($rebar, $kMenuBandId) }
                      else { [IntPtr]::Zero }
        $si = New-Object Win32.Find+SCROLLINFO
        $si.cbSize = [Runtime.InteropServices.Marshal]::SizeOf($si); $si.fMask = $SIF_ALL
        ($v.PageBox -ne [IntPtr]::Zero) -and
            ($v.Edit -ne [IntPtr]::Zero) -and ($v.Count -ne [IntPtr]::Zero) -and
            ($v.Opts -ne [IntPtr]::Zero) -and ($v.Nav -ne [IntPtr]::Zero) -and
            ($v.CloseBar -ne [IntPtr]::Zero) -and
            ([Win32.Find]::GetScrollInfo($v.Left, $SB_VERT, [ref]$si)) -and ($si.nMax -gt 0)
    } 20000
    if (-not $ready) { throw 'find bar children not found (or the document never opened)' }
    return $v
}
function Stop-Viewer($v) {
    [void]$v.Proc.CloseMainWindow()
    if (-not $v.Proc.WaitForExit(10000)) { $v.Proc.Kill(); throw 'viewer did not exit after CloseMainWindow' }
    Assert ($v.Proc.ExitCode -eq 0) "exit code 0 (got $($v.Proc.ExitCode))"
}
function Show-FindBar($v) {
    [void][Win32.Find]::PostMessageW($v.Main, $WM_COMMAND, [IntPtr]$IDC_FIND_SHOW, [IntPtr]::Zero)
    if (-not (Poll { [Win32.Find]::IsWindowVisible($v.Bar) } 5000)) { throw 'find bar did not show' }
}
# Types the needle and waits for a SETTLED count: the counter carries a trailing
# "+" while the background scan is still walking the document.
function Get-MatchCount($v, [string]$needle) {
    # Clear FIRST and wait for the reset. Typing is debounced by 350 ms, so
    # right after WM_SETTEXT the counter still shows the PREVIOUS query's
    # settled value - indistinguishable from this one's unless the reset is
    # observed in between. ("0" = an empty query parked; "" = never searched.)
    [void][Win32.Find]::SendMessageW($v.Edit, $WM_SETTEXT, [IntPtr]::Zero, '')
    if (-not (Poll { (Get-CtrlText $v.Count) -in '0', '' } 5000)) {
        throw "the counter did not reset (reads '$(Get-CtrlText $v.Count)')"
    }
    # SEND WM_SETTEXT: cross-process SetWindowText only touches the caption
    # cache (CLAUDE.md), and EN_CHANGE is what arms the search.
    [void][Win32.Find]::SendMessageW($v.Edit, $WM_SETTEXT, [IntPtr]::Zero, $needle)
    if ((Get-CtrlText $v.Edit) -ne $needle) { throw "find edit did not take '$needle'" }
    Wait-Count $v '0'
}
# Settled = not the pre-action value, not the scanning ellipsis, no "+" tail.
# $was is what the counter read BEFORE the action, so a value that merely has
# not been recomputed yet can never be mistaken for the answer.
function Wait-Count($v, [string]$was = '') {
    [void](Poll {
        $text = Get-CtrlText $v.Count
        $text -ne '' -and $text -ne $was -and $text -ne [string][char]0x2026 -and
            -not $text.EndsWith('+')
    } 8000)
    $text = Get-CtrlText $v.Count
    # "N" = N matches with none selected yet (a fresh query must not move the
    # view, so nothing is active); "M/N" once the reader has stepped to one.
    if ($text -match '^\d+/(\d+)$') { return [int]$Matches[1] }
    if ($text -match '^(\d+)$') { return [int]$Matches[1] }
    return -1
}
function Get-ActiveIndex($v) {
    $text = Get-CtrlText $v.Count
    if ($text -match '^(\d+)/\d+') { return [int]$Matches[1] }
    return 0 # no active match
}
function Get-VPos([IntPtr]$pane) {
    $si = New-Object Win32.Find+SCROLLINFO
    $si.cbSize = [Runtime.InteropServices.Marshal]::SizeOf($si)
    $si.fMask = $SIF_ALL
    [void][Win32.Find]::GetScrollInfo($pane, $SB_VERT, [ref]$si)
    $si.nPos
}
# The rebar's page box is the cheapest deterministic way to park the reader on
# a given page: WM_SETTEXT then Enter runs the frame's own goto path.
function Set-Page($v, [int]$page1) {
    [void][Win32.Find]::SendMessageW($v.PageBox, $WM_SETTEXT, [IntPtr]::Zero, [string]$page1)
    [void][Win32.Find]::PostMessageW($v.PageBox, $WM_KEYDOWN, [IntPtr]0x0D, [IntPtr]::Zero)
    if (-not (Poll { (Get-CtrlText $v.PageBox) -eq [string]$page1 } 5000)) {
        throw "could not park on page $page1 (page box reads '$(Get-CtrlText $v.PageBox)')"
    }
    Start-Sleep -Milliseconds 250
}
# Returns the counter text from BEFORE the toggle, to be handed to Wait-Count:
# a toggle restarts the search synchronously, but the results still arrive on
# the worker's own schedule.
function Toggle-Option($v, [int]$id) {
    $was = Get-CtrlText $v.Count
    [void][Win32.Find]::PostMessageW($v.Main, $WM_COMMAND, [IntPtr]$id, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 150
    $was
}
# Waits for the step to select a match: only then does the counter carry "m/n".
function Wait-Active($v) {
    [void](Poll { (Get-CtrlText $v.Count) -match '^\d+/\d+$' } 5000)
    Get-ActiveIndex $v
}
function Get-Checked($v, [int]$id) {
    [Win32.Find]::SendMessageW($v.Opts, $TB_ISBUTTONCHECKED, [IntPtr]$id, [IntPtr]::Zero) -ne [IntPtr]::Zero
}

Write-Host "find-bar options E2E ($Config)"
$v = Start-Viewer
try {
    Write-Host '--- phase 0: fresh defaults, plain search'
    Show-FindBar $v
    Assert (-not (Get-Checked $v $IDC_FIND_MATCH_CASE)) 'match case starts off'
    Assert (-not (Get-Checked $v $IDC_FIND_WHOLE_WORD)) 'whole word starts off'
    Assert ((Get-MatchCount $v 'teorema') -eq 3) 'plain "teorema" finds 3 (Teorema/teorema/TEOREMA)'

    Write-Host '--- phase 1: match case (and the options-are-the-query guard)'
    # The needle does NOT change here: if StartSearch still compared the text
    # alone, this toggle would be swallowed and the count would stay at 3.
    $was = Toggle-Option $v $IDC_FIND_MATCH_CASE
    Assert (Get-Checked $v $IDC_FIND_MATCH_CASE) 'match case latched on'
    Assert ((Wait-Count $v $was) -eq 1) 'match case on: only the lowercase "teorema"'
    $was = Toggle-Option $v $IDC_FIND_MATCH_CASE
    Assert ((Wait-Count $v $was) -eq 3) 'match case off again: back to 3'

    Write-Host '--- phase 2: whole word'
    Assert ((Get-MatchCount $v 'arco') -eq 3) 'plain "arco": arco + b(arco)llare + arcobaleno'
    $was = Toggle-Option $v $IDC_FIND_WHOLE_WORD
    Assert (Get-Checked $v $IDC_FIND_WHOLE_WORD) 'whole word latched on'
    Assert ((Wait-Count $v $was) -eq 1) 'whole word on: only the standalone "arco"'

    Write-Host '--- phase 3: whole word on an ACCENTED word'
    # The reason this is not built on the regex \b: mujs classifies word
    # characters ASCII-only, so "\bperche<acute>\b" would match nothing at all.
    $perche = "perch$([char]0xE9)"
    Assert ((Get-MatchCount $v $perche) -eq 1) "whole word on: only the standalone '$perche'"
    $was = Toggle-Option $v $IDC_FIND_WHOLE_WORD
    Assert ((Wait-Count $v $was) -eq 3) "whole word off: '$perche' also inside sperche/perches"
    Assert (-not (Get-Checked $v $IDC_FIND_WHOLE_WORD)) 'whole word latched off'

    # The menu bar is a MenuBand toolbar whose buttons are built from the HMENU
    # (the HMENU itself is never attached to the window, so GetMenu sees
    # nothing): the count is the only cross-process evidence that Edit exists.
    Assert ([int][Win32.Find]::SendMessageW($v.MenuBand, $TB_BUTTONCOUNT, [IntPtr]::Zero,
                                            [IntPtr]::Zero) -eq 5) 'menu bar has File/Edit/View/Sync/Help'

    Write-Host '--- phase 4: a fresh search must NOT move the view'
    Show-FindBar $v
    $before = Get-VPos $v.Left
    Assert ((Get-MatchCount $v 'arco') -eq 3) 'search armed (3 hits, all on page 2)'
    Assert ((Get-VPos $v.Left) -eq $before) 'the view stayed put while the results arrived'
    Assert ((Get-ActiveIndex $v) -eq 0) 'no match is selected yet (the counter shows just the total)'
    [void][Win32.Find]::PostMessageW($v.Main, $WM_COMMAND, [IntPtr]$IDC_FIND_NEXT, [IntPtr]::Zero)
    Assert ((Wait-Active $v) -eq 1) "F3 selects the first match (counter '$(Get-CtrlText $v.Count)')"
    Assert ((Get-VPos $v.Left) -ne $before) 'and only then does the view move'

    Write-Host '--- phase 5: the first step starts from the CURRENT page'
    # "Pagina" is in every page label, so there is one match per page: parked on
    # page 3, the first step must land on page 3 rather than drag the reader
    # back to page 1.
    Set-Page $v 3
    Show-FindBar $v
    Assert ((Get-MatchCount $v 'Pagina') -eq 4) "`"Pagina`" found once per page (counter '$(Get-CtrlText $v.Count)')"
    Assert ((Get-ActiveIndex $v) -eq 0) 'still nothing selected after retyping'
    [void][Win32.Find]::PostMessageW($v.Main, $WM_COMMAND, [IntPtr]$IDC_FIND_NEXT, [IntPtr]::Zero)
    Assert ((Wait-Active $v) -eq 3) "the first step landed on page 3, not page 1 (counter '$(Get-CtrlText $v.Count)')"
    Set-Page $v 1
    Show-FindBar $v

    # The tooltip TEXT cannot be read across processes (TTM_GETTEXT takes a
    # pointer), so this asserts the infrastructure: TBSTYLE_TOOLTIPS created a
    # tooltip control on every bar. The text itself travels the same
    # TTN_GETDISPINFOW handler the main toolbar has always used - which reaches
    # the frame only because FindBarProc forwards WM_NOTIFY. Hover once by hand
    # after changing either of those.
    foreach ($bar in @{n='opts';h=$v.Opts}, @{n='nav';h=$v.Nav}, @{n='close';h=$v.CloseBar}) {
        $tip = [Win32.Find]::SendMessageW($bar.h, $TB_GETTOOLTIPS, [IntPtr]::Zero, [IntPtr]::Zero)
        Assert ($tip -ne [IntPtr]::Zero) "$($bar.n) bar has a tooltip control"
    }

    Write-Host '--- phase 6: tab order inside the find bar'
    Show-FindBar $v # returns the focus to the edit and reselects
    Assert ((Get-FocusHwnd $v.Main) -eq $v.Edit) 'focus starts in the find edit'
    $order = @()
    for ($i = 0; $i -lt 3; $i++) {
        [void][Win32.Find]::PostMessageW((Get-FocusHwnd $v.Main), $WM_KEYDOWN, [IntPtr]$VK_TAB, [IntPtr]::Zero)
        Start-Sleep -Milliseconds 200
        $h = Get-FocusHwnd $v.Main
        $order += switch ($h) {
            $v.Edit { 'edit' }
            $v.Opts { 'opts' }
            $v.Nav { 'nav' }
            $v.CloseBar { 'close' }
            default { "other($h)" }
        }
    }
    Write-Host "        tab order after the edit: $($order -join ' -> ')"
    Assert ($order -notcontains "other($(Get-FocusHwnd $v.Main))") 'tab stays inside the find bar'

    Write-Host '--- phase 7: shedding order (toggles go before the counter)'
    $rc = New-Object Win32.Find+RECT
    [void][Win32.Find]::GetWindowRect($v.Main, [ref]$rc)
    $h = $rc.b - $rc.t
    $violations = 0
    $sawOptsGone = $false
    foreach ($w in 1100, 900, 800, 700, 620, 560, 500, 460, 420) {
        [void][Win32.Find]::SetWindowPos($v.Main, [IntPtr]::Zero, 0, 0, $w, $h,
                                         $SWP_NOMOVE -bor $SWP_NOZORDER)
        Start-Sleep -Milliseconds 250
        $optsOn = [Win32.Find]::IsWindowVisible($v.Opts)
        $countOn = [Win32.Find]::IsWindowVisible($v.Count)
        if ($optsOn -and -not $countOn) { $violations++ }
        if (-not $optsOn) { $sawOptsGone = $true }
    }
    Assert ($violations -eq 0) 'the counter never disappears while the toggles are still shown'
    Assert $sawOptsGone 'the toggles do shed at a narrow enough pane'
    [void][Win32.Find]::SetWindowPos($v.Main, [IntPtr]::Zero, 0, 0, $rc.r - $rc.l, $h,
                                     $SWP_NOMOVE -bor $SWP_NOZORDER)
    Start-Sleep -Milliseconds 250

    Write-Host '--- phase 8: persistence'
    [void](Toggle-Option $v $IDC_FIND_MATCH_CASE)
    Assert (Get-Checked $v $IDC_FIND_MATCH_CASE) 'match case on before the restart'
    [void][Win32.Find]::PostMessageW($v.Main, $WM_COMMAND, [IntPtr]$IDC_FIND_CLOSE, [IntPtr]::Zero)
    if ($Capture) {
        Show-FindBar $v
        Start-Sleep -Milliseconds 400
        Add-Type -AssemblyName System.Drawing
        [void][Win32.Find]::GetWindowRect($v.Bar, [ref]$rc)
        $bmp = New-Object System.Drawing.Bitmap ($rc.r - $rc.l), ($rc.b - $rc.t)
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $hdc = $g.GetHdc()
        [void][Win32.Find]::PrintWindow($v.Bar, $hdc, 2) # PW_RENDERFULLCONTENT
        $g.ReleaseHdc($hdc)
        $shot = Join-Path $env:TEMP 'psv-findbar.png'
        $bmp.Save($shot, [System.Drawing.Imaging.ImageFormat]::Png)
        $g.Dispose(); $bmp.Dispose()
        Write-Host "        find bar captured: $shot"
    }
} finally {
    Stop-Viewer $v
}

$ini = Join-Path $scratch 'settings.ini'
$iniText = if (Test-Path $ini) { Get-Content -LiteralPath $ini -Raw } else { '' }
Assert ($iniText -match '(?m)^\[find\]') '[find] section written'
Assert ($iniText -match '(?m)^matchCase=1') '[find] matchCase=1 persisted'
Assert ($iniText -match '(?m)^wholeWord=0') '[find] wholeWord=0 persisted'

$v = Start-Viewer
try {
    Show-FindBar $v
    Assert (Get-Checked $v $IDC_FIND_MATCH_CASE) 'match case restored from settings.ini'
    Assert (-not (Get-Checked $v $IDC_FIND_WHOLE_WORD)) 'whole word restored off'
    Assert ((Get-MatchCount $v 'teorema') -eq 1) 'the restored option really applies to the search'
} finally {
    Stop-Viewer $v
}

# A scanner holding a handle turns the delete into delete-pending and the file
# stays visible for a moment: retry rather than fail on it (CLAUDE.md).
for ($i = 0; $i -lt 20 -and (Test-Path $scratch); $i++) {
    try { Remove-Item -Recurse -Force $scratch -ErrorAction Stop } catch { Start-Sleep -Milliseconds 200 }
}

if ($script:failures -gt 0) {
    Write-Host "$($script:failures) FAILURES" -ForegroundColor Red
    exit 1
}
Write-Host 'all find-bar option checks passed' -ForegroundColor Green


