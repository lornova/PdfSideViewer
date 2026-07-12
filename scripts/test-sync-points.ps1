# E2E test for WinMerge-style sync points: generation from numbered bookmarks
# (skip + monotonicity filter), waiting at segment boundaries, manual points,
# clear-restores-plain-anchor, zero-match feedback.
#
# CLAUDE.md testing rules: DPI-aware thread FIRST (the dev monitor is 175% and
# PowerShell is DPI-unaware), PSV_SETTINGS_DIR sandbox (never touch the user's
# settings.ini), abort if a foreign instance is running (posted commands would
# hit it), the exe must exit 0, retry-loop the settings deletion.
#
# NOT covered here: Alt+scroll (GetKeyState reads the real keyboard) - verify
# manually. Assertions use the ENGLISH strings (sandbox settings = default
# language) and the page box / status-cell text length (SB_GETTEXTW is not
# marshaled cross-process; SB_GETTEXTLENGTHW is pointer-free and is).
param([string]$Config = 'Debug')

$ErrorActionPreference = 'Stop'

Add-Type -Namespace Win32 -Name Native -MemberDefinition @'
[DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr ctx);
// PowerShell coerces $null to "" for string parameters, and FindWindow treats
// "" as "empty title" instead of "any": the NULL side must be an IntPtr.
[DllImport("user32.dll", CharSet=CharSet.Unicode, EntryPoint="FindWindowW")] public static extern IntPtr FindWindowByClass(string cls, IntPtr title);
[DllImport("user32.dll", CharSet=CharSet.Unicode, EntryPoint="FindWindowW")] public static extern IntPtr FindWindowByTitle(IntPtr cls, string title);
[DllImport("user32.dll", CharSet=CharSet.Unicode, EntryPoint="FindWindowExW")] public static extern IntPtr FindWindowExByClass(IntPtr parent, IntPtr after, string cls, IntPtr title);
[DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr hwnd, int id);
[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, System.Text.StringBuilder l);
[DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, string l);
[DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
[DllImport("user32.dll")] public static extern bool GetScrollInfo(IntPtr h, int bar, ref SCROLLINFO si);
[DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
[DllImport("user32.dll")] public static extern bool GetGUIThreadInfo(uint tid, ref GUITHREADINFO gui);
[DllImport("user32.dll")] public static extern bool IsWindow(IntPtr h);
[StructLayout(LayoutKind.Sequential)] public struct SCROLLINFO { public uint cbSize, fMask; public int nMin, nMax; public uint nPage; public int nPos, nTrackPos; }
[StructLayout(LayoutKind.Sequential)] public struct RECT { public int l, t, r, b; }
[StructLayout(LayoutKind.Sequential)] public struct GUITHREADINFO { public uint cbSize, flags; public IntPtr hwndActive, hwndFocus, hwndCapture, hwndMenuOwner, hwndMoveSize, hwndCaret; public RECT rcCaret; }
'@

[void][Win32.Native]::SetThreadDpiAwarenessContext([IntPtr](-4))

# --- constants (mirror MainWindow.h CommandId and the control ids) ---
$WM_COMMAND = 0x0111
$WM_SETTEXT = 0x000C
$WM_GETTEXT = 0x000D
$SB_GETTEXTLENGTHW = 0x040C
$IDC_FOCUS_NEXT_PANE = 1003
$IDC_TOGGLE_SCROLL_SYNC = 1004
$IDC_TOGGLE_ZOOM_SYNC = 1005
$IDC_GOTO_PAGE = 1028
$IDC_ADD_SYNC_POINT = 1051
$IDC_SYNC_FROM_BOOKMARKS = 1052
$IDC_CLEAR_SYNC_POINTS = 1054
$IDOK = 1
$SB_VERT = 1
$SIF_ALL = 0x17

# --- expected English status-cell texts (asserted by LENGTH, see header) ---
$mid = [string][char]0xB7 # the middle dot of StatusSyncPtsPre, ASCII-safe
$lenOff = 'Sync: off'.Length                                  # 9
$lenZoom = 'Sync: zoom'.Length                                # 10
$lenScroll = 'Sync: scroll'.Length                            # 12
$lenBoth = 'Sync: scroll+zoom'.Length                         # 17
$lenScroll3Pts = "Sync: scroll $mid 3 pts".Length             # 20
$lenScroll2Pts = "Sync: scroll $mid 2 pts".Length             # 20
$lenGenerated3 = 'Sync points from bookmarks: 3'.Length       # 29
$lenNoMatch = 'Sync points: no matching numbered bookmarks'.Length # 43

$root = Split-Path $PSScriptRoot -Parent
$exe = Join-Path $root "build\x64\$Config\PdfSideViewer.exe"
if (-not (Test-Path $exe)) { throw "missing $exe (build $Config x64 first)" }
$pdfA = Join-Path $root 'testdata\sync-a.pdf'
$pdfA2 = Join-Path $root 'testdata\sync-a2.pdf'
$pdfB = Join-Path $root 'testdata\sync-b.pdf'
$pdfC = Join-Path $root 'testdata\sync-c.pdf'
foreach ($f in $pdfA, $pdfA2, $pdfB, $pdfC) {
    if (-not (Test-Path $f)) { throw "missing $f (run scripts\make-test-pdfs.ps1)" }
}

if (Get-Process PdfSideViewer -ErrorAction SilentlyContinue) {
    throw 'a PdfSideViewer instance is already running: aborting, posted commands would hit it'
}

$scratch = Join-Path $env:TEMP ('psv-sync-test-' + [guid]::NewGuid().ToString('n'))
New-Item -ItemType Directory -Force $scratch | Out-Null
$env:PSV_SETTINGS_DIR = $scratch
# Live copy for the reload phase, in its OWN subdir: the pane's FileWatcher
# watches the document's parent directory, and settings.ini writes in the
# sandbox root must not feed it noise.
$docs = Join-Path $scratch 'docs'
New-Item -ItemType Directory -Force $docs | Out-Null
$workA = Join-Path $docs 'sync-a.pdf'
Copy-Item $pdfA $workA

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

function Get-StatusLen([IntPtr]$status) {
    [int]([Win32.Native]::SendMessageW($status, $SB_GETTEXTLENGTHW, [IntPtr]3,
                                       [IntPtr]::Zero).ToInt64() -band 0xFFFF)
}
function Get-FocusHwnd([IntPtr]$main) {
    $procId = [uint32]0
    $tid = [Win32.Native]::GetWindowThreadProcessId($main, [ref]$procId)
    $gui = New-Object Win32.Native+GUITHREADINFO
    $gui.cbSize = [Runtime.InteropServices.Marshal]::SizeOf($gui)
    if ([Win32.Native]::GetGUIThreadInfo($tid, [ref]$gui)) { return $gui.hwndFocus }
    return [IntPtr]::Zero
}
function Focus-Pane($v, [IntPtr]$pane) {
    for ($i = 0; $i -lt 4 -and (Get-FocusHwnd $v.Main) -ne $pane; $i++) {
        [void][Win32.Native]::PostMessageW($v.Main, $WM_COMMAND,
                                           [IntPtr]$IDC_FOCUS_NEXT_PANE, [IntPtr]::Zero)
        [void](Poll { (Get-FocusHwnd $v.Main) -eq $pane } 1500)
    }
    if ((Get-FocusHwnd $v.Main) -ne $pane) { throw 'could not focus the requested pane' }
}
function Invoke-GotoPage($v, [IntPtr]$pane, [int]$page1) {
    Focus-Pane $v $pane
    [void][Win32.Native]::PostMessageW($v.Main, $WM_COMMAND, [IntPtr]$IDC_GOTO_PAGE,
                                       [IntPtr]::Zero)
    if (-not (Poll { [Win32.Native]::FindWindowByTitle([IntPtr]::Zero, 'Go to Page') -ne [IntPtr]::Zero } 5000)) {
        throw 'goto dialog did not open'
    }
    $dlg = [Win32.Native]::FindWindowByTitle([IntPtr]::Zero, 'Go to Page')
    # FindWindow can catch the dialog BEFORE its children exist and BEFORE
    # WM_INITDIALOG ran (writing then gets overwritten by the prefill), so:
    # resolve the edit inside the poll, and wait for the prefill - it is
    # never empty (the pane has a document) and marks init as complete.
    $editRef = @{ h = [IntPtr]::Zero }
    $readEdit = {
        if ($editRef.h -eq [IntPtr]::Zero) {
            $editRef.h = [Win32.Native]::GetDlgItem($dlg, 2201)
        }
        if ($editRef.h -eq [IntPtr]::Zero) { return '' }
        $sb = New-Object System.Text.StringBuilder 32
        [void][Win32.Native]::SendMessageW($editRef.h, $WM_GETTEXT, [IntPtr]32, $sb)
        $sb.ToString()
    }
    if (-not (Poll { (& $readEdit).Length -gt 0 } 5000)) { throw 'goto dialog never prefilled' }
    $edit = $editRef.h
    # SEND WM_SETTEXT: cross-process SetWindowText would touch only the
    # caption cache (CLAUDE.md). Verify the write landed before confirming.
    [void][Win32.Native]::SendMessageW($edit, $WM_SETTEXT, [IntPtr]::Zero, [string]$page1)
    if ((& $readEdit) -ne [string]$page1) { throw 'goto edit did not take the page number' }
    [void][Win32.Native]::PostMessageW($dlg, $WM_COMMAND, [IntPtr]$IDOK, [IntPtr]::Zero)
    if (-not (Poll { -not [Win32.Native]::IsWindow($dlg) } 5000)) {
        throw 'goto dialog did not close'
    }
    Start-Sleep -Milliseconds 250 # let the synced sibling settle
}
function Get-PageBoxText($v) {
    $sb = New-Object System.Text.StringBuilder 64
    [void][Win32.Native]::SendMessageW($v.PageBox, $WM_GETTEXT, [IntPtr]64, $sb)
    $sb.ToString()
}
function Assert-PaneAt($v, [IntPtr]$pane, [string]$expected, [string]$what) {
    # The page box mirrors the ACTIVE pane; focusing the pane refreshes it.
    Focus-Pane $v $pane
    $ok = Poll { (Get-PageBoxText $v) -eq $expected } 5000
    Assert $ok "$what (page box '$(Get-PageBoxText $v)', expected '$expected')"
}
function Send-Command($v, [int]$id) {
    [void][Win32.Native]::PostMessageW($v.Main, $WM_COMMAND, [IntPtr]$id, [IntPtr]::Zero)
}

function Start-Viewer([string]$leftPdf, [string]$rightPdf) {
    $proc = Start-Process -FilePath $exe -ArgumentList "`"$leftPdf`"", "`"$rightPdf`"" -PassThru
    if (-not (Poll { [Win32.Native]::FindWindowByClass('PsvMainWindow', [IntPtr]::Zero) -ne [IntPtr]::Zero } 15000)) {
        throw 'main window did not appear'
    }
    $main = [Win32.Native]::FindWindowByClass('PsvMainWindow', [IntPtr]::Zero)
    $v = @{ Proc = $proc; Main = $main }
    # FindWindow can win the race against WM_CREATE: poll until the children
    # actually exist.
    $childrenReady = Poll {
        $v.Left = [Win32.Native]::GetDlgItem($main, 100)
        $v.Right = [Win32.Native]::GetDlgItem($main, 101)
        $v.Status = [Win32.Native]::FindWindowExByClass($main, [IntPtr]::Zero,
                                                        'msctls_statusbar32', [IntPtr]::Zero)
        $rebar = [Win32.Native]::FindWindowExByClass($main, [IntPtr]::Zero, 'ReBarWindow32',
                                                     [IntPtr]::Zero)
        $v.PageBox = if ($rebar -ne [IntPtr]::Zero) { [Win32.Native]::GetDlgItem($rebar, 2001) }
                     else { [IntPtr]::Zero }
        ($v.Left -ne [IntPtr]::Zero) -and ($v.Right -ne [IntPtr]::Zero) -and
            ($v.Status -ne [IntPtr]::Zero) -and ($v.PageBox -ne [IntPtr]::Zero)
    } 10000
    if (-not $childrenReady) { throw 'main window children not found' }
    $ready = Poll {
        $si = New-Object Win32.Native+SCROLLINFO
        $si.cbSize = [Runtime.InteropServices.Marshal]::SizeOf($si); $si.fMask = $SIF_ALL
        $l = [Win32.Native]::GetScrollInfo($v.Left, $SB_VERT, [ref]$si) -and $si.nMax -gt 0
        $si2 = New-Object Win32.Native+SCROLLINFO
        $si2.cbSize = [Runtime.InteropServices.Marshal]::SizeOf($si2); $si2.fMask = $SIF_ALL
        $r = [Win32.Native]::GetScrollInfo($v.Right, $SB_VERT, [ref]$si2) -and $si2.nMax -gt 0
        $l -and $r
    } 15000
    if (-not $ready) { throw 'panes did not finish opening (no scroll range)' }
    return $v
}
function Stop-Viewer($v) {
    [void]$v.Proc.CloseMainWindow()
    if (-not $v.Proc.WaitForExit(10000)) { $v.Proc.Kill(); throw 'viewer did not exit after CloseMainWindow' }
    Assert ($v.Proc.ExitCode -eq 0) "exit code 0 (got $($v.Proc.ExitCode))"
}
# Fresh-sandbox defaults switch the sync locks ON; drive both OFF so every
# phase starts from a known state. The four base texts have distinct lengths.
function Reset-SyncLocks($v) {
    if ((Get-StatusLen $v.Status) -in $lenScroll, $lenBoth) {
        Send-Command $v $IDC_TOGGLE_SCROLL_SYNC
        [void](Poll { (Get-StatusLen $v.Status) -in $lenOff, $lenZoom } 5000)
    }
    if ((Get-StatusLen $v.Status) -eq $lenZoom) {
        Send-Command $v $IDC_TOGGLE_ZOOM_SYNC
        [void](Poll { (Get-StatusLen $v.Status) -eq $lenOff } 5000)
    }
    if ((Get-StatusLen $v.Status) -ne $lenOff) { throw 'could not normalize the sync locks' }
}

try {
    # ---------------------------------------------------------------- phase 1
    # Generation: skip ([1.2] left-only), monotonicity ([3] points backward on
    # the right), waiting inside the right-side gap, both directions.
    # Expected map (0-based): (0,0) (1,1) (4,2).
    Write-Host 'phase 1: generation from numbered bookmarks (sync-a | sync-b)'
    $v = Start-Viewer $workA $pdfB
    Reset-SyncLocks $v
    Send-Command $v $IDC_SYNC_FROM_BOOKMARKS
    Assert (Poll { (Get-StatusLen $v.Status) -eq $lenGenerated3 } 5000) `
        'transient message reports 3 generated points (out-of-order candidate dropped)'
    Invoke-GotoPage $v $v.Left 5   # section "2": left p5 -> right p3 (delta -2)
    Assert-PaneAt $v $v.Right '3' 'left p5 drives right to its section 2 (p3)'
    Assert ((Get-StatusLen $v.Status) -eq $lenScroll3Pts) `
        'status cell shows "Sync: scroll" + 3 pts (sync auto-enabled by generation)'
    Invoke-GotoPage $v $v.Left 4   # left-only page: right must WAIT at its section end
    Assert-PaneAt $v $v.Right '2' 'right waits at the end of p2 while left crosses the gap'
    Invoke-GotoPage $v $v.Left 6   # after the last point: delta -2, NOT the dropped -4
    Assert-PaneAt $v $v.Right '4' 'left p6 drives right to p4 (dropped candidate has no effect)'
    Invoke-GotoPage $v $v.Right 3  # lead from the right across the same map
    Assert-PaneAt $v $v.Left '5' 'right p3 drives left to p5 (map works in both directions)'

    # --------------------------------------------------------------- phase 1b
    # Auto-reload: overwrite the live left pdf with the sync-a2 variant
    # (section "2 Conclusioni" removed) - the LaTeX rebuild scenario. The
    # re-derived map has 2 points, so left p6 now maps through segment (1,1)
    # to right p6; the stale 3-point map would keep driving right to p4.
    # Behavior is the proof: goto pairs repeat until the watcher's debounce
    # and stability probe have let the reload land.
    Write-Host 'phase 1b: auto-reload re-derives the generated points'
    Copy-Item $pdfA2 $workA -Force
    $reloaded = Poll {
        try {
            Invoke-GotoPage $v $v.Left 1
            Invoke-GotoPage $v $v.Left 6
            Focus-Pane $v $v.Right
            (Get-PageBoxText $v) -eq '6'
        } catch {
            # Mid-reload the pane has no document yet: the goto command is
            # swallowed (no dialog) or lands on an empty pane (dialog stays
            # up). Dismiss any leftover dialog and let the poll retry.
            $stray = [Win32.Native]::FindWindowByTitle([IntPtr]::Zero, 'Go to Page')
            if ($stray -ne [IntPtr]::Zero) {
                [void][Win32.Native]::PostMessageW($stray, $WM_COMMAND, [IntPtr]2, [IntPtr]::Zero) # IDCANCEL
            }
            $false
        }
    } 30000
    Assert $reloaded 'auto-reload re-derived the map from the fresh outline (left p6 -> right p6)'

    # --------------------------------------------------------------- phase 1c
    # A failed reload (broken half-written compile) must not drop the parked
    # regen: garbage fails the open, then the original sync-a lands and the
    # 3-point map must come back (left p5 -> right p3; the a2 map of phase 1b
    # would drive right to p5 instead).
    Write-Host 'phase 1c: failed reload keeps the regen parked'
    [System.IO.File]::WriteAllBytes($workA, [byte[]](1..64))
    Start-Sleep -Milliseconds 1500 # separate the two writes past the watcher debounce
    Copy-Item $pdfA $workA -Force
    $recovered = Poll {
        try {
            Invoke-GotoPage $v $v.Left 1
            Invoke-GotoPage $v $v.Left 5
            Focus-Pane $v $v.Right
            (Get-PageBoxText $v) -eq '3'
        } catch {
            $stray = [Win32.Native]::FindWindowByTitle([IntPtr]::Zero, 'Go to Page')
            if ($stray -ne [IntPtr]::Zero) {
                [void][Win32.Native]::PostMessageW($stray, $WM_COMMAND, [IntPtr]2, [IntPtr]::Zero) # IDCANCEL
            }
            $false
        }
    } 30000
    Assert $recovered 'map re-derived after a failed-then-good reload (left p5 -> right p3)'

    # ---------------------------------------------------------------- phase 2
    # Manual points placed with sync OFF, then locked; clear restores the
    # plain anchor captured at the current positions.
    Write-Host 'phase 2: manual points and clear'
    Send-Command $v $IDC_CLEAR_SYNC_POINTS
    Assert (Poll { (Get-StatusLen $v.Status) -eq $lenScroll } 5000) `
        'clear removes the pts suffix from the status cell'
    Reset-SyncLocks $v
    Invoke-GotoPage $v $v.Left 2
    Invoke-GotoPage $v $v.Right 1
    Send-Command $v $IDC_ADD_SYNC_POINT     # (1,0) 0-based
    Invoke-GotoPage $v $v.Left 5
    Invoke-GotoPage $v $v.Right 2
    Send-Command $v $IDC_ADD_SYNC_POINT     # (4,1) 0-based
    Send-Command $v $IDC_TOGGLE_SCROLL_SYNC # lock; the non-empty map must not recapture
    Assert (Poll { (Get-StatusLen $v.Status) -eq $lenScroll2Pts } 5000) `
        'status cell shows scroll sync + a pts suffix after locking (count pinned by the asserts below)'
    Invoke-GotoPage $v $v.Left 3   # segment (1,0), delta -1, clamped at the next point
    Assert-PaneAt $v $v.Right '1' 'first manual segment: right waits at the end of p1'
    Invoke-GotoPage $v $v.Left 5   # segment (4,1), delta -3
    Assert-PaneAt $v $v.Right '2' 'second manual segment drives right to p2'
    Invoke-GotoPage $v $v.Right 1  # lead right inside segment (1,0): left = right + 1
    Assert-PaneAt $v $v.Left '2' 'manual map leads from the right too'
    Send-Command $v $IDC_CLEAR_SYNC_POINTS  # anchor recaptured HERE: left 2 / right 1
    Assert (Poll { (Get-StatusLen $v.Status) -eq $lenScroll } 5000) `
        'clear removes the pts suffix again'
    Invoke-GotoPage $v $v.Right 3  # plain anchor (-1): left p4. The old map would give p6.
    Assert-PaneAt $v $v.Left '4' 'after clear the plain anchor governs (map really gone)'
    Stop-Viewer $v

    # ---------------------------------------------------------------- phase 3
    # Zero match: sync-c has no numbered bookmarks at all.
    Write-Host 'phase 3: zero-match feedback (sync-a | sync-c)'
    $v = Start-Viewer $pdfA $pdfC
    Send-Command $v $IDC_SYNC_FROM_BOOKMARKS
    Assert (Poll { (Get-StatusLen $v.Status) -eq $lenNoMatch } 5000) `
        'zero-match message shown in the status cell'
    Assert (Poll { (Get-StatusLen $v.Status) -lt 18 } 8000) `
        'message expires and no pts suffix remains (no points were created)'
    Stop-Viewer $v
} finally {
    Get-Process PdfSideViewer -ErrorAction SilentlyContinue | ForEach-Object {
        [void]$_.CloseMainWindow()
        if (-not $_.WaitForExit(5000)) { $_.Kill() }
    }
    $env:PSV_SETTINGS_DIR = $null
    # A scanner briefly holding a handle turns the delete into delete-pending:
    # retry until the directory really goes away.
    for ($i = 0; $i -lt 10; $i++) {
        try { Remove-Item -Recurse -Force $scratch -ErrorAction Stop; break }
        catch { Start-Sleep -Milliseconds 300 }
    }
}

if ($script:failures -gt 0) { Write-Host "$($script:failures) FAILURE(S)" -ForegroundColor Red; exit 1 }
Write-Host 'all sync-point E2E assertions passed' -ForegroundColor Green
exit 0
