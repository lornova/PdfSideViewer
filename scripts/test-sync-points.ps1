# E2E test for WinMerge-style sync points: rendered alignment gaps (default
# ON, 1:1 traversal, flippable paged gaps, toggle round-trip), swap mirroring,
# generation from numbered bookmarks (skip + monotonicity filter), waiting at
# segment boundaries with gaps OFF, manual points, clear-restores-plain-anchor,
# zero-match feedback, pane header options, the three-pane mode (phase 8:
# CLI activation, three-way sync, N-way bookmark join, 3<->2 transitions with
# park/reopen and both saved-map restores, F8 rotation, subset sync around an
# empty centre), the XML WM_COPYDATA handoff (phase 9: a real second
# -open-center instance hands over to the running one), the auto-reload join
# guard (phase 10), the status-part schema of a HIDDEN status bar (phase 11)
# and the settings-persistence failure paths (phase 12: an injected save
# failure must leave the file byte-identical, and the cleanup must touch only
# the temp names THIS process id can produce, leaving every foreign one alone).
# Phase order matters: phase 0 asserts the fresh-sandbox defaults,
# phases 1..2 explicitly toggle the gaps off, phase 8 reuses the manual a2|c
# map phase 6 saved. What a phase needs from the persisted state it FORCES
# (ini edit or command) and asserts, rather than inheriting it silently.
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
# A command id that is not in the constants block below reads as $null, PowerShell
# coerces it to 0 for Send-Command's [int] and WM_COMMAND 0 does NOTHING: the
# phase then "passes" without ever running its command. Strict mode turns that
# silent hole into an error at the point of use.
Set-StrictMode -Version Latest

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
[DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
// Volume capability, so the ACL characterization can be skipped on a filesystem
// that does not persist ACLs instead of failing for an environmental reason.
// The mount point comes from GetVolumePathName, not from a lexical path root:
// that one returns "\\server\share" without the trailing backslash the query
// requires, and on a mounted volume it names the wrong volume entirely.
[DllImport("kernel32.dll", CharSet=CharSet.Unicode, EntryPoint="GetVolumePathNameW", SetLastError=true)] public static extern bool GetVolumePathNameW(string fileName, System.Text.StringBuilder mountPoint, uint bufferLength);
[DllImport("kernel32.dll", CharSet=CharSet.Unicode, EntryPoint="GetVolumeInformationW", SetLastError=true)] public static extern bool GetVolumeInformationW(string root, System.Text.StringBuilder volName, uint volNameSize, out uint serial, out uint maxComponent, out uint flags, System.Text.StringBuilder fsName, uint fsNameSize);
[StructLayout(LayoutKind.Sequential)] public struct SCROLLINFO { public uint cbSize, fMask; public int nMin, nMax; public uint nPage; public int nPos, nTrackPos; }
[StructLayout(LayoutKind.Sequential)] public struct RECT { public int l, t, r, b; }
[StructLayout(LayoutKind.Sequential)] public struct GUITHREADINFO { public uint cbSize, flags; public IntPtr hwndActive, hwndFocus, hwndCapture, hwndMenuOwner, hwndMoveSize, hwndCaret; public RECT rcCaret; }
'@

[void][Win32.Native]::SetThreadDpiAwarenessContext([IntPtr](-4))

# --- constants (mirror MainWindow.h CommandId and the control ids) ---
$WM_COMMAND = 0x0111
$WM_SETTEXT = 0x000C
$WM_GETTEXT = 0x000D
$WM_KEYDOWN = 0x0100
$VK_END = 0x23
$SB_GETTEXTLENGTHW = 0x040C
$SB_GETPARTS = 0x0406
$FILE_PERSISTENT_ACLS = 0x00000008
$IDC_FOCUS_NEXT_PANE = 1003
$IDC_TOGGLE_SCROLL_SYNC = 1004
$IDC_TOGGLE_ZOOM_SYNC = 1005
$IDC_TOGGLE_STATUSBAR = 1014
$IDC_ZOOM_ACTUAL = 1017
$IDC_FIT_PAGE = 1019
$IDC_SCROLL_CONTINUOUS = 1025
$IDC_SCROLL_PAGED = 1026
$IDC_CLOSE_DOC = 1027
$IDC_GOTO_PAGE = 1028
$IDC_SWAP_PANES = 1029
$IDC_SWAP_PANES_BACK = 1077
$IDC_OPTIONS = 1049
$IDC_ADD_SYNC_POINT = 1051
$IDC_SYNC_FROM_BOOKMARKS = 1052
$IDC_CLEAR_SYNC_POINTS = 1054
$IDC_TOGGLE_ALIGNMENT_GAPS = 1055
$IDC_PANES_TWO = 1075
$IDC_PANES_THREE = 1076
$IDOK = 1
$SB_VERT = 1
$SIF_ALL = 0x17
$WM_VSCROLL = 0x0115
$SB_PAGEDOWN = 3

# --- expected English status-cell texts (asserted by LENGTH, see header) ---
$mid = [string][char]0xB7 # the middle dot of StatusSyncPtsPre, ASCII-safe
$lenOff = 'Sync: off'.Length                                  # 9
$lenZoom = 'Sync: zoom'.Length                                # 10
$lenScroll = 'Sync: scroll'.Length                            # 12
$lenBoth = 'Sync: scroll+zoom'.Length                         # 17
$lenScroll3Pts = "Sync: scroll $mid 3 pts".Length             # 20
$lenScroll2Pts = "Sync: scroll $mid 2 pts".Length             # 20
$lenScroll1Pt = "Sync: scroll $mid 1 pts".Length              # 20
$lenGenerated3 = 'Sync points from bookmarks: 3'.Length       # 29
$lenNoMatch = 'Sync points: no matching numbered bookmarks'.Length # 43

$root = Split-Path $PSScriptRoot -Parent
$exe = Join-Path $root "build\x64\$Config\PdfSideViewer.exe"
if (-not (Test-Path $exe)) { throw "missing $exe (build $Config x64 first)" }
$pdfA = Join-Path $root 'testdata\sync-a.pdf'
$pdfA2 = Join-Path $root 'testdata\sync-a2.pdf'
$pdfB = Join-Path $root 'testdata\sync-b.pdf'
$pdfC = Join-Path $root 'testdata\sync-c.pdf'
$pdfD = Join-Path $root 'testdata\sync-d.pdf'
$pdfE = Join-Path $root 'testdata\sync-e.pdf'
$pdfTB = Join-Path $root 'testdata\test-b.pdf' # 3 Letter pages: phase 10's replacement
foreach ($f in $pdfA, $pdfA2, $pdfB, $pdfC, $pdfD, $pdfE, $pdfTB) {
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

function Get-VScroll([IntPtr]$pane) {
    $si = New-Object Win32.Native+SCROLLINFO
    $si.cbSize = [Runtime.InteropServices.Marshal]::SizeOf($si)
    $si.fMask = $SIF_ALL
    [void][Win32.Native]::GetScrollInfo($pane, $SB_VERT, [ref]$si)
    $si
}
function Get-StatusLen([IntPtr]$status, [int]$part = 3) {
    # The sync summary lives in part 3 with two panes, part 8 with three
    # (SyncStatusPart in MainWindow.cpp).
    [int]([Win32.Native]::SendMessageW($status, $SB_GETTEXTLENGTHW, [IntPtr]$part,
                                       [IntPtr]::Zero).ToInt64() -band 0xFFFF)
}
function Get-StatusParts([IntPtr]$status) {
    # 7 parts with two panes, 10 with three; 1 means the control never got a
    # schema at all (what a hidden bar used to keep).
    [int][Win32.Native]::SendMessageW($status, $SB_GETPARTS, [IntPtr]::Zero, [IntPtr]::Zero)
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
    if ((Get-FocusHwnd $v.Main) -ne $pane) {
        throw "could not focus the requested pane (focus=$(Get-FocusHwnd $v.Main), want=$pane)"
    }
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

function Start-Viewer([string]$leftPdf, [string]$rightPdf, [string]$centerPdf = '') {
    # The positional CLI order is Beyond Compare's left right [center]
    # (kCliSlotOrder in main.cpp), deliberately NOT the visual order: a third
    # file lands in the centre pane and switches the three-pane mode on.
    $argList = @("`"$leftPdf`"", "`"$rightPdf`"")
    if ($centerPdf -ne '') { $argList += "`"$centerPdf`"" }
    $proc = Start-Process -FilePath $exe -ArgumentList $argList -PassThru
    if (-not (Poll { [Win32.Native]::FindWindowByClass('PsvMainWindow', [IntPtr]::Zero) -ne [IntPtr]::Zero } 15000)) {
        throw 'main window did not appear'
    }
    $main = [Win32.Native]::FindWindowByClass('PsvMainWindow', [IntPtr]::Zero)
    $v = @{ Proc = $proc; Main = $main }
    # FindWindow can win the race against WM_CREATE: poll until the children
    # actually exist.
    $childrenReady = Poll {
        # Pane child ids are kPaneChildIdBase + PaneSlot (PaneSlots.h): the
        # slots are left, center, right in visual order, so the right pane is
        # 102 and 101 is the centre pane, present only in three-pane mode.
        $v.Left = [Win32.Native]::GetDlgItem($main, 100)
        $v.Center = [Win32.Native]::GetDlgItem($main, 101)
        $v.Right = [Win32.Native]::GetDlgItem($main, 102)
        $v.Status = [Win32.Native]::FindWindowExByClass($main, [IntPtr]::Zero,
                                                        'msctls_statusbar32', [IntPtr]::Zero)
        $rebar = [Win32.Native]::FindWindowExByClass($main, [IntPtr]::Zero, 'ReBarWindow32',
                                                     [IntPtr]::Zero)
        $v.PageBox = if ($rebar -ne [IntPtr]::Zero) { [Win32.Native]::GetDlgItem($rebar, 2001) }
                     else { [IntPtr]::Zero }
        ($v.Left -ne [IntPtr]::Zero) -and ($v.Right -ne [IntPtr]::Zero) -and
            ($centerPdf -eq '' -or $v.Center -ne [IntPtr]::Zero) -and
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
        $c = $true
        if ($centerPdf -ne '') {
            $si3 = New-Object Win32.Native+SCROLLINFO
            $si3.cbSize = [Runtime.InteropServices.Marshal]::SizeOf($si3); $si3.fMask = $SIF_ALL
            $c = [Win32.Native]::GetScrollInfo($v.Center, $SB_VERT, [ref]$si3) -and $si3.nMax -gt 0
        }
        $l -and $r -and $c
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
# phase starts from a known state. The four base texts have distinct lengths;
# with a live map the "· N pts" suffix shifts them all by the same amount
# (pass the expected point count, single-digit).
function Reset-SyncLocks($v, [int]$pts = 0, [int]$part = 3) {
    $sfx = if ($pts -gt 0) { " $mid $pts pts".Length } else { 0 }
    if ((Get-StatusLen $v.Status $part) -in ($lenScroll + $sfx), ($lenBoth + $sfx)) {
        Send-Command $v $IDC_TOGGLE_SCROLL_SYNC
        [void](Poll { (Get-StatusLen $v.Status $part) -in ($lenOff + $sfx), ($lenZoom + $sfx) } 5000)
    }
    if ((Get-StatusLen $v.Status $part) -eq ($lenZoom + $sfx)) {
        Send-Command $v $IDC_TOGGLE_ZOOM_SYNC
        [void](Poll { (Get-StatusLen $v.Status $part) -eq ($lenOff + $sfx) } 5000)
    }
    if ((Get-StatusLen $v.Status $part) -ne ($lenOff + $sfx)) {
        throw 'could not normalize the sync locks'
    }
}

try {
    # ---------------------------------------------------------------- phase 0
    # Alignment gaps (default ON, asserted on a fresh sandbox). Map for
    # sync-a|sync-b is (0,0),(1,1),(4,2): the right pane gets 2 gap slots
    # before its page 3 (1-based) mirroring left's A4 pages 3-4, so its slot
    # table is [p1,p2,G,G,p3,p4,p5,p6] and its scroll range grows; the left
    # layout is untouched. With gaps the follower scrolls THROUGH its gap
    # (nPos advances) while the page box stays pinned - the discriminator
    # against the gaps-off waiting behavior, where the follower stands still.
    Write-Host 'phase 0: alignment gaps, default ON (sync-a | sync-b)'
    $v = Start-Viewer $pdfA $pdfB
    Reset-SyncLocks $v
    $baseL = (Get-VScroll $v.Left).nMax
    $baseR = (Get-VScroll $v.Right).nMax
    Send-Command $v $IDC_SYNC_FROM_BOOKMARKS
    Assert (Poll { (Get-StatusLen $v.Status) -eq $lenGenerated3 } 5000) `
        'generated 3 points (gaps phase)'
    Assert (Poll { (Get-VScroll $v.Right).nMax -gt $baseR } 5000) `
        'right scroll range grew: gap slots rendered by default'
    Assert ((Get-VScroll $v.Left).nMax -eq $baseL) `
        'left scroll range unchanged: gaps only on the short side'
    Invoke-GotoPage $v $v.Left 3
    Assert-PaneAt $v $v.Right '2' 'left p3 puts right inside its gap run (counter pinned at 2)'
    $rp1 = (Get-VScroll $v.Right).nPos
    Invoke-GotoPage $v $v.Left 4
    Assert-PaneAt $v $v.Right '2' 'left p4: right counter still pinned at 2'
    Assert ((Get-VScroll $v.Right).nPos -gt $rp1) `
        'right nPos advanced through the gap while the counter stayed put (1:1 traversal)'
    Invoke-GotoPage $v $v.Left 5
    Assert-PaneAt $v $v.Right '3' 'left p5 lands right on its section 2 (p3) past the gaps'

    # Paged mode: gap slots are flippable empty pages.
    Reset-SyncLocks $v 3
    Send-Command $v $IDC_SCROLL_PAGED
    Invoke-GotoPage $v $v.Right 2
    Start-Sleep -Milliseconds 250
    $np0 = (Get-VScroll $v.Right).nPos
    $sawGapFlip = $false
    $reached3 = $false
    for ($i = 0; $i -lt 12 -and -not $reached3; $i++) {
        [void][Win32.Native]::PostMessageW($v.Right, $WM_VSCROLL, [IntPtr]$SB_PAGEDOWN,
                                           [IntPtr]::Zero)
        Start-Sleep -Milliseconds 200
        $box = Get-PageBoxText $v
        $np = (Get-VScroll $v.Right).nPos
        if ($box -eq '2' -and $np -gt $np0) { $sawGapFlip = $true } # parked on a gap slot
        if ($box -eq '3') { $reached3 = $true }
    }
    Assert $reached3 'paged flips reach p3 across the gap run'
    Assert $sawGapFlip 'an intermediate flip parked on a gap slot (blank page, counter pinned)'
    Send-Command $v $IDC_SCROLL_CONTINUOUS

    # Toggle round-trip: OFF restores the exact gapless range, ON re-grows it.
    Send-Command $v $IDC_TOGGLE_ALIGNMENT_GAPS
    Assert (Poll { (Get-VScroll $v.Right).nMax -eq $baseR } 5000) `
        'gaps OFF: right range back to the exact gapless value'
    Send-Command $v $IDC_TOGGLE_ALIGNMENT_GAPS
    Assert (Poll { (Get-VScroll $v.Right).nMax -gt $baseR } 5000) `
        'gaps ON again: range re-grows'

    # --------------------------------------------------------------- phase 0b
    # Swap mirroring: after F8 the map survives with left/right exchanged
    # ((0,0),(1,1),(2,4)), so the gap run moves to the LEFT pane and new-left
    # p3 (slot 4) pairs with new-right p5 (slot 4). Without mirroring the map
    # would be gone (status length 12) and the goto would land elsewhere.
    Write-Host 'phase 0b: swap mirrors the point map'
    if ((Get-StatusLen $v.Status) -ne $lenScroll3Pts) {
        Send-Command $v $IDC_TOGGLE_SCROLL_SYNC
        [void](Poll { (Get-StatusLen $v.Status) -eq $lenScroll3Pts } 5000)
    }
    Send-Command $v $IDC_SWAP_PANES
    $swapped = Poll {
        try {
            (Get-StatusLen $v.Status) -eq $lenScroll3Pts -and
                (Get-VScroll $v.Left).nMax -gt 0 -and (Get-VScroll $v.Right).nMax -gt 0
        } catch { $false }
    } 20000
    Assert $swapped 'map survived the swap (status still reports 3 pts)'
    $mirrorOk = Poll {
        try {
            Invoke-GotoPage $v $v.Left 2
            Focus-Pane $v $v.Right
            if ((Get-PageBoxText $v) -ne '2') { return $false }
            Invoke-GotoPage $v $v.Left 3
            Focus-Pane $v $v.Right
            (Get-PageBoxText $v) -eq '5'
        } catch {
            $stray = [Win32.Native]::FindWindowByTitle([IntPtr]::Zero, 'Go to Page')
            if ($stray -ne [IntPtr]::Zero) {
                [void][Win32.Native]::PostMessageW($stray, $WM_COMMAND, [IntPtr]2, [IntPtr]::Zero) # IDCANCEL
            }
            $false
        }
    } 20000
    Assert $mirrorOk 'mirrored map drives new-left p3 -> new-right p5 (slot 4 <-> slot 4)'
    Stop-Viewer $v

    # ---------------------------------------------------------------- phase 1
    # Generation: skip ([1.2] left-only), monotonicity ([3] points backward on
    # the right), waiting inside the right-side gap, both directions.
    # Expected map (0-based): (0,0) (1,1) (4,2).
    Write-Host 'phase 1: generation from numbered bookmarks (sync-a | sync-b)'
    $v = Start-Viewer $workA $pdfB
    Reset-SyncLocks $v
    # Phases 1..2 assert the gaps-OFF waiting behavior. Phase 0b left the
    # toggle ON (persisted), so one toggle turns it off; phase 0's round-trip
    # already proved the toggle itself works.
    Send-Command $v $IDC_TOGGLE_ALIGNMENT_GAPS
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

    # ---------------------------------------------------------------- phase 4
    # Options round-trip for the marker visibility checkboxes (2110 anchors,
    # 2111 ticks): uncheck both, OK, close - settings.ini must persist 0s.
    # The rendering itself is not observable through messages; this pins the
    # dialog plumbing and persistence.
    Write-Host 'phase 4: marker visibility options persist'
    Send-Command $v $IDC_OPTIONS
    if (-not (Poll { [Win32.Native]::FindWindowByTitle([IntPtr]::Zero, 'Options') -ne [IntPtr]::Zero } 5000)) {
        throw 'options dialog did not open'
    }
    $opt = [Win32.Native]::FindWindowByTitle([IntPtr]::Zero, 'Options')
    $BM_GETCHECK = 0x00F0
    $BM_SETCHECK = 0x00F1
    foreach ($id in 2110, 2111) {
        if (-not (Poll { [Win32.Native]::GetDlgItem($opt, $id) -ne [IntPtr]::Zero } 3000)) {
            throw "options checkbox $id not found"
        }
        $chk = [Win32.Native]::GetDlgItem($opt, $id)
        # WM_INITDIALOG checks the boxes (defaults are on); waiting for that
        # avoids the write-then-init race the goto dialog taught us.
        if (-not (Poll { [Win32.Native]::SendMessageW($chk, $BM_GETCHECK, [IntPtr]::Zero,
                                                      [IntPtr]::Zero).ToInt64() -eq 1 } 3000)) {
            throw "options checkbox $id never initialized"
        }
        [void][Win32.Native]::SendMessageW($chk, $BM_SETCHECK, [IntPtr]0, [IntPtr]::Zero)
    }
    [void][Win32.Native]::PostMessageW($opt, $WM_COMMAND, [IntPtr]$IDOK, [IntPtr]::Zero)
    if (-not (Poll { -not [Win32.Native]::IsWindow($opt) } 5000)) { throw 'options did not close' }
    Stop-Viewer $v
    $ini = Get-Content (Join-Path $scratch 'settings.ini') -Raw
    # Anchored per line: an unanchored key= match can also land inside a
    # LONGER key (fsStatusbar contains statusbar) and read as a false green.
    Assert ($ini -match '(?m)^showAnchors=0\r?$') 'settings.ini persisted showAnchors=0'
    Assert ($ini -match '(?m)^showTicks=0\r?$') 'settings.ini persisted showTicks=0'

    # ---------------------------------------------------------------- phase 5
    # Extended matching (sync-d | sync-e): deep keys (2.2.1) on distinct
    # pages, title-only pairs (d's 'Sommario' <-> e's ACCENTED
    # 'Tartalomjegyzek' via the "toc" canonical class), letter components
    # (d's Appendice A/B, A.1/A.2; e spells B's heading with the accented
    # intro word 'Fuggelek', so the pair also pins the locale-independent
    # tokenizer/lowercasing on the non-ASCII, UTF-16-encoded titles). All ten
    # channels matching is pinned by the TWO-DIGIT count in
    # the status cell (any missing channel drops to one digit = length 20);
    # the Appendice B goto pins the letter point behaviorally (delta +2 of
    # the last point (9,11); without it the previous segment's delta 0 would
    # land the right pane on p10/p11 instead).
    Write-Host 'phase 5: deep keys, title matches and letter components (sync-d | sync-e)'
    $v = Start-Viewer $pdfD $pdfE
    Reset-SyncLocks $v
    Send-Command $v $IDC_SYNC_FROM_BOOKMARKS
    $len10Pts = "Sync: scroll $mid 10 pts".Length
    Assert (Poll { (Get-StatusLen $v.Status) -eq $len10Pts } 5000) `
        'all 10 points generated (third level + titles + letters all matched)'
    Invoke-GotoPage $v $v.Left 10  # 0-based 9 = "Appendice B", last point (9,11)
    Assert-PaneAt $v $v.Right '12' 'letter point drives left Appendice B onto right p12'
    # Depth guard regression: d's TOP-LEVEL 'Note' (0-based p10) and e's
    # 'Notes' (NESTED under 'Materiale extra', 0-based p13) share the "notes"
    # canonical class and the candidate (10,13) would be monotonic after
    # (9,11), so only the matcher's equal-depth rule keeps it out. The count
    # assert above cannot see it ('11 pts' has the same status length), hence
    # the behavioral pin: with the false point left p11 would drag the right
    # pane to p14; the honest +2 tail segment lands it on p13.
    Invoke-GotoPage $v $v.Left 11
    Assert-PaneAt $v $v.Right '13' 'depth guard: top-level Note does not pair with nested Notes'
    Stop-Viewer $v

    # ---------------------------------------------------------------- phase 6
    # Manual sync points persist across sessions ([sync-points]): place two on
    # a never-seen pair with no numbered-bookmark overlap (sync-a2 | sync-c),
    # close, reopen, and the map must come back (locks restore as saved: off).
    Write-Host 'phase 6: manual sync points persist across sessions'
    $v = Start-Viewer $pdfA2 $pdfC
    Reset-SyncLocks $v
    Invoke-GotoPage $v $v.Left 2
    Invoke-GotoPage $v $v.Right 1
    Send-Command $v $IDC_ADD_SYNC_POINT   # (1,0) 0-based
    Invoke-GotoPage $v $v.Left 5
    Invoke-GotoPage $v $v.Right 2
    Send-Command $v $IDC_ADD_SYNC_POINT   # (4,1) 0-based
    $lenOff2Pts = "Sync: off $mid 2 pts".Length
    Assert (Poll { (Get-StatusLen $v.Status) -eq $lenOff2Pts } 5000) `
        'two manual points placed with sync off'
    Stop-Viewer $v
    $v = Start-Viewer $pdfA2 $pdfC
    Assert (Poll { (Get-StatusLen $v.Status) -eq $lenOff2Pts } 8000) `
        'reopening the pair restores its two manual points'
    Send-Command $v $IDC_TOGGLE_SCROLL_SYNC
    [void](Poll { (Get-StatusLen $v.Status) -eq $lenScroll2Pts } 5000)
    Invoke-GotoPage $v $v.Left 5   # restored point (4,1): delta -3
    Assert-PaneAt $v $v.Right '2' 'the restored manual map drives the right pane'
    Stop-Viewer $v

    # --------------------------------------------------------------- phase 6b
    # Generated points re-derive at startup: phase 5 saved sync-d | sync-e
    # with the auto flag (and scroll sync on), so reopening the pair must
    # rebuild the 10-point map from the fresh outlines by itself.
    Write-Host 'phase 6b: generated points re-derive at startup (saved auto flag)'
    $v = Start-Viewer $pdfD $pdfE
    Assert (Poll { (Get-StatusLen $v.Status) -eq $len10Pts } 8000) `
        'the remembered pair re-generates its 10 points on open'
    Invoke-GotoPage $v $v.Left 10
    Assert-PaneAt $v $v.Right '12' 'the re-derived map drives Appendice B onto right p12'
    Stop-Viewer $v

    # ---------------------------------------------------------------- phase 7
    # Pane header options (2114 show, 2115 path). The strip reserves a constant
    # band at the top of the pane, so with the header ON the vertical scroll PAGE
    # (the document viewport height) is smaller than with it OFF. Actual-size zoom
    # pins TotalHeight (Manual: height-independent), so only the viewport moves.
    # Then the two checkboxes round-trip to settings.ini both ways. The strip's
    # text and underline are Direct2D, not observable through messages.
    Write-Host 'phase 7: pane header options + viewport reserve (sync-a | sync-c)'
    $BM_GETCHECK = 0x00F0
    $BM_SETCHECK = 0x00F1
    # sync-c has no numbered bookmarks and this pair was never auto-saved, so no
    # points regenerate on open: the sync state and the left layout stay plain,
    # which keeps the nPage measurement clean. The sync locks are irrelevant here.
    $v = Start-Viewer $pdfA $pdfC
    Focus-Pane $v $v.Left
    Send-Command $v $IDC_ZOOM_ACTUAL   # 100% Manual: TotalHeight fixed, only nPage moves
    Start-Sleep -Milliseconds 400
    $pageHeaderOn = (Get-VScroll $v.Left).nPage   # header ON by default
    Send-Command $v $IDC_OPTIONS
    if (-not (Poll { [Win32.Native]::FindWindowByTitle([IntPtr]::Zero, 'Options') -ne [IntPtr]::Zero } 5000)) {
        throw 'options dialog did not open (phase 7)'
    }
    $opt = [Win32.Native]::FindWindowByTitle([IntPtr]::Zero, 'Options')
    if (-not (Poll { [Win32.Native]::GetDlgItem($opt, 2114) -ne [IntPtr]::Zero } 3000)) {
        throw 'header checkbox 2114 not found'
    }
    $hdr = [Win32.Native]::GetDlgItem($opt, 2114)
    # WM_INITDIALOG checks it (default on); wait for that before toggling (the
    # write-then-init race the goto dialog taught us).
    if (-not (Poll { [Win32.Native]::SendMessageW($hdr, $BM_GETCHECK, [IntPtr]::Zero, [IntPtr]::Zero).ToInt64() -eq 1 } 3000)) {
        throw 'header checkbox never initialized checked'
    }
    [void][Win32.Native]::SendMessageW($hdr, $BM_SETCHECK, [IntPtr]0, [IntPtr]::Zero)
    [void][Win32.Native]::PostMessageW($opt, $WM_COMMAND, [IntPtr]$IDOK, [IntPtr]::Zero)
    if (-not (Poll { -not [Win32.Native]::IsWindow($opt) } 5000)) { throw 'options did not close (phase 7)' }
    Assert (Poll { (Get-VScroll $v.Left).nPage -gt $pageHeaderOn } 5000) `
        "header OFF grows the document viewport (nPage was $pageHeaderOn with the strip)"
    Stop-Viewer $v
    $ini7 = Get-Content (Join-Path $scratch 'settings.ini') -Raw
    Assert ($ini7 -match '(?m)^header=0\r?$') 'settings.ini persisted header=0'

    # Reopen (header now off) and turn header + path ON: the reverse round-trip.
    $v = Start-Viewer $pdfA $pdfC
    Send-Command $v $IDC_OPTIONS
    if (-not (Poll { [Win32.Native]::FindWindowByTitle([IntPtr]::Zero, 'Options') -ne [IntPtr]::Zero } 5000)) {
        throw 'options dialog did not reopen (phase 7)'
    }
    $opt = [Win32.Native]::FindWindowByTitle([IntPtr]::Zero, 'Options')
    if (-not (Poll { [Win32.Native]::GetDlgItem($opt, 2114) -ne [IntPtr]::Zero } 3000)) {
        throw 'header checkbox 2114 missing on reopen'
    }
    $hdr = [Win32.Native]::GetDlgItem($opt, 2114)
    if (-not (Poll { [Win32.Native]::SendMessageW($hdr, $BM_GETCHECK, [IntPtr]::Zero, [IntPtr]::Zero).ToInt64() -eq 0 } 3000)) {
        throw 'header checkbox should reopen unchecked (header=0 persisted)'
    }
    $pth = [Win32.Native]::GetDlgItem($opt, 2115)
    Assert ($pth -ne [IntPtr]::Zero) 'header-path checkbox 2115 exists'
    [void][Win32.Native]::SendMessageW($hdr, $BM_SETCHECK, [IntPtr]1, [IntPtr]::Zero)
    [void][Win32.Native]::SendMessageW($pth, $BM_SETCHECK, [IntPtr]1, [IntPtr]::Zero)
    [void][Win32.Native]::PostMessageW($opt, $WM_COMMAND, [IntPtr]$IDOK, [IntPtr]::Zero)
    if (-not (Poll { -not [Win32.Native]::IsWindow($opt) } 5000)) { throw 'options did not close on reopen (phase 7)' }
    Stop-Viewer $v
    $ini7b = Get-Content (Join-Path $scratch 'settings.ini') -Raw
    Assert ($ini7b -match '(?m)^header=1\r?$') 'settings.ini persisted header=1 (reverse round-trip)'
    Assert ($ini7b -match '(?m)^headerPath=1\r?$') 'settings.ini persisted headerPath=1'

    # ---------------------------------------------------------------- phase 8
    # Three-pane mode end to end: CLI activation (positional left right CENTER
    # order), three-way plain-anchor sync, the N-way bookmark join (a smaller
    # intersection, never a distortion), a manual trio point, the 3->2->3
    # transitions (the shrink restores the surviving pair's remembered map -
    # phase 6's a2|c points - WITHOUT any reopen; the grow reopens the parked
    # centre and brings the trio's own map back through it, and must NOT drag
    # the loaded panes: the reopening pane joins sync only at DocumentOpened,
    # its restore echo swallowed), the F8 rotation (each pane adopts its
    # predecessor in visual order, the map survives permuted), and subset sync
    # (an EMPTY centre must not suspend the loaded panes' pairing). Depends on
    # phase 6 having saved the a2|c manual map. The sync cell is part 8 with
    # three panes, part 3 with two.
    Write-Host 'phase 8: three-pane mode (sync-a2 | sync-b centre | sync-c)'
    # The gaps toggle command is state-blind and this phase must not depend on
    # how many times earlier phases happened to flip it (phase 0 ends with the
    # gaps ON, phase 1 turns them OFF): force the persisted flag itself while
    # no viewer is running.
    $ini8 = Join-Path $scratch 'settings.ini'
    $ini8Text = (Get-Content $ini8 -Raw) -replace '(?m)^showGaps=1\r?$', 'showGaps=0'
    Set-Content $ini8 -Value $ini8Text -Encoding Unicode
    if ((Get-Content $ini8 -Raw) -notmatch '(?m)^showGaps=0\r?$') {
        throw 'could not force showGaps=0 in the sandbox ini'
    }
    $v = Start-Viewer $pdfA2 $pdfC $pdfB   # visual order: a2 | b | c
    Assert ($v.Center -ne [IntPtr]::Zero) 'centre pane child (id 101) exists'
    Reset-SyncLocks $v 0 8
    Send-Command $v $IDC_TOGGLE_SCROLL_SYNC
    if (-not (Poll { (Get-StatusLen $v.Status 8) -eq $lenScroll } 5000)) {
        throw 'could not enable scroll sync in three-pane mode'
    }
    $c0 = (Get-VScroll $v.Center).nPos
    $r0 = (Get-VScroll $v.Right).nPos
    Invoke-GotoPage $v $v.Left 3
    Assert (Poll { (Get-VScroll $v.Center).nPos -gt $c0 } 5000) 'centre follows the left leader'
    Assert (Poll { (Get-VScroll $v.Right).nPos -gt $r0 } 5000) 'right follows the left leader'

    # N-way join: a2 and c share no numbered keys with b (or each other), so
    # the trio generates nothing where a|b alone had 3 points.
    Send-Command $v $IDC_SYNC_FROM_BOOKMARKS
    Assert (Poll { (Get-StatusLen $v.Status 8) -eq $lenNoMatch } 5000) `
        'three-way bookmark join: no key present in ALL three outlines'
    if (-not (Poll { (Get-StatusLen $v.Status 8) -eq $lenScroll } 8000)) {
        throw 'the zero-match transient did not expire'
    }
    # One manual trio point with a DELIBERATELY asymmetric tuple: sync off so
    # each pane is positioned independently (left p4, centre p3, right p2 =
    # 0-based (3,2,1)), then captured; remembered under the a2|b|c key and
    # restored by the grow below. The mapped drives after the reverse
    # rotation prove the COORDINATES survive, not just the point count.
    Reset-SyncLocks $v 0 8
    Invoke-GotoPage $v $v.Left 4
    Invoke-GotoPage $v $v.Center 3
    Invoke-GotoPage $v $v.Right 2
    Send-Command $v $IDC_ADD_SYNC_POINT
    $lenOff1Pt = "Sync: off $mid 1 pts".Length
    Assert (Poll { (Get-StatusLen $v.Status 8) -eq $lenOff1Pt } 8000) `
        'manual trio point placed (asymmetric tuple 4:3:2)'
    Send-Command $v $IDC_TOGGLE_SCROLL_SYNC
    if (-not (Poll { (Get-StatusLen $v.Status 8) -eq $lenScroll1Pt } 5000)) {
        throw 'could not re-enable scroll sync after placing the tuple'
    }

    # Manual 100% zoom in every pane for the rest of the phase (the gaps are
    # already OFF via the forced flag above): gap slots and fit-zoom
    # recomputes would legitimately move nPos across the mode switches and
    # rotations below, while the assertions need positions that only an
    # (unwanted) sync drive could change.
    foreach ($pane in $v.Left, $v.Center, $v.Right) {
        Focus-Pane $v $pane
        Send-Command $v $IDC_ZOOM_ACTUAL
    }
    Start-Sleep -Milliseconds 600
    # Park the centre with a NONZERO offset: the grow below must restore it,
    # and that restore's scroll echo is exactly what the controller's join
    # guard has to swallow (the pane holds a document mid-restore but must
    # not lead until its DocumentOpened lands).
    Invoke-GotoPage $v $v.Center 2

    # Shrink: the centre (sync-b) parks, and the surviving pair a2|c gets its
    # phase-6 manual map back IMMEDIATELY - no reopen fires on a shrink.
    Send-Command $v $IDC_PANES_TWO
    Assert (Poll { [Win32.Native]::GetDlgItem($v.Main, 101) -eq [IntPtr]::Zero } 5000) `
        'two panes: centre child destroyed'
    Assert (Poll { (Get-StatusLen $v.Status) -eq $lenScroll2Pts } 5000) `
        'shrink restored the remembered a2|c manual map without a reopen'
    Invoke-GotoPage $v $v.Left 5
    Assert-PaneAt $v $v.Right '2' 'the restored pair map drives right (5 -> 2, as phase 6)'

    # Grow: the parked centre reopens (restoring its nonzero offset) and the
    # trio's remembered map (the manual point above) comes back through that
    # reopen. The loaded panes must NOT move: pre-join, the reopening pane's
    # restore echo used to drive them through a never-captured anchor.
    $lPre = (Get-VScroll $v.Left).nPos
    Send-Command $v $IDC_PANES_THREE
    $centerBack = Poll {
        $v.Center = [Win32.Native]::GetDlgItem($v.Main, 101)
        if ($v.Center -eq [IntPtr]::Zero) { return $false }
        (Get-VScroll $v.Center).nMax -gt 0
    } 10000
    Assert $centerBack 'three panes again: the parked centre document reopened'
    Assert (Poll { (Get-StatusLen $v.Status 8) -eq $lenScroll1Pt } 8000) `
        'the trio map restored through the centre reopen'
    Assert ((Get-VScroll $v.Center).nPos -gt 0) 'the centre came back at its parked offset'
    Start-Sleep -Milliseconds 400 # any (buggy) restore echo has landed by now
    # The LEADER pins the regression: pre-join, the reopening centre's restore
    # echo drove every pane toward its parked offset through a never-captured
    # anchor (left would jump ~3 pages here). The RIGHT legitimately moves:
    # the restored trio map is authoritative, so the first leader echo
    # reabsorbs the follower onto it - asserting it still would re-test the
    # map, not the join guard. Tolerance covers h-scrollbar flips (viewport
    # height) and page->pixel re-quantization through the resize echoes.
    $lDrift = [math]::Abs((Get-VScroll $v.Left).nPos - $lPre)
    Assert ($lDrift -le 16) `
        "the reopen did not drag the leader pane (join regression; drift l=$lDrift)"

    # Rotation: each pane adopts its PREDECESSOR in visual order (left content
    # moves to centre, centre to right, right wraps to left), so the scroll
    # ranges rotate with the documents (Manual zoom rides each snapshot
    # exactly; a fit zoom would recompute per pane width and break the strict
    # equality).
    $lMax = (Get-VScroll $v.Left).nMax
    $cMax = (Get-VScroll $v.Center).nMax
    $rMax = (Get-VScroll $v.Right).nMax
    Send-Command $v $IDC_SWAP_PANES
    Assert (Poll {
        (Get-VScroll $v.Center).nMax -eq $lMax -and
        (Get-VScroll $v.Right).nMax -eq $cMax -and
        (Get-VScroll $v.Left).nMax -eq $rMax
    } 10000) 'F8 rotates the documents (left -> centre -> right)'
    Assert (Poll { (Get-StatusLen $v.Status 8) -eq $lenScroll1Pt } 8000) `
        'the point map survives the rotation, permuted'
    # The FORWARD tuple must be checked before rotating back, or paired
    # permutation errors would cancel across the round trip. Forward =
    # (1,3,2) 0-based: a left leader on its point page drives centre +2
    # pages and right +1.
    Invoke-GotoPage $v $v.Left 2
    Assert-PaneAt $v $v.Center '4' 'forward tuple drives the centre by its permuted delta'
    Assert-PaneAt $v $v.Right '3' 'forward tuple drives the right by its permuted delta'
    # And back: Shift+F8 is the inverse permutation (each pane adopts its
    # successor), so the ranges return to the pre-rotation triple.
    Send-Command $v $IDC_SWAP_PANES_BACK
    Assert (Poll {
        (Get-VScroll $v.Left).nMax -eq $lMax -and
        (Get-VScroll $v.Center).nMax -eq $cMax -and
        (Get-VScroll $v.Right).nMax -eq $rMax
    } 10000) 'Shift+F8 rotates back (each pane adopts its successor)'
    Assert (Poll { (Get-StatusLen $v.Status 8) -eq $lenScroll1Pt } 8000) `
        'the map survives the reverse rotation too'
    # Coordinate check, not just the count: with the tuple back at (3,2,1)
    # 0-based, a left leader below the point extrapolates its deltas - left
    # p3 puts the centre one page back (p2) and the right two back (p1).
    Invoke-GotoPage $v $v.Left 3
    Assert-PaneAt $v $v.Center '2' 'restored tuple drives the centre by its own delta'
    Assert-PaneAt $v $v.Right '1' 'restored tuple drives the right by its own delta'

    # Subset sync: close the centre document (the map dies with it) and the
    # left leader must STILL drive the right follower around the empty centre.
    Focus-Pane $v $v.Center
    Send-Command $v $IDC_CLOSE_DOC
    Assert (Poll { (Get-VScroll $v.Center).nMax -eq 0 } 5000) 'centre document closed'
    [void](Poll { (Get-StatusLen $v.Status 8) -eq $lenScroll } 5000) # map died with the doc
    # Re-zero the pair delta first: the asymmetric tuple left the follower
    # two pages behind the leader, and a follower already clamped at its top
    # cannot show movement.
    Send-Command $v $IDC_TOGGLE_SCROLL_SYNC
    [void](Poll { (Get-StatusLen $v.Status 8) -eq $lenOff } 5000)
    Invoke-GotoPage $v $v.Left 1
    Invoke-GotoPage $v $v.Right 1
    Send-Command $v $IDC_TOGGLE_SCROLL_SYNC
    [void](Poll { (Get-StatusLen $v.Status 8) -eq $lenScroll } 5000)
    $rBase = (Get-VScroll $v.Right).nPos
    Invoke-GotoPage $v $v.Left 2
    Assert (Poll { (Get-VScroll $v.Right).nPos -gt $rBase } 5000) `
        'sync keeps driving the loaded panes around the empty centre (subset sync)'
    Stop-Viewer $v

    # ---------------------------------------------------------------- phase 9
    # XML WM_COPYDATA handoff, cross-process for real: a second instance runs
    # -open-center against the running one (util/IpcXml, XmlLite payload),
    # exits 0 without ever showing a window, and the running instance switches
    # to three panes with the document in the centre - which also covers the
    # inactive-slot activation path end to end.
    Write-Host 'phase 9: -open-center handoff to the running instance (XML IPC)'
    $v = Start-Viewer $pdfA $pdfB
    # Phase 8 persisted a three-pane arrangement; drop back to two so the
    # handoff exercises the mode switch too.
    if ([Win32.Native]::GetDlgItem($v.Main, 101) -ne [IntPtr]::Zero) {
        Send-Command $v $IDC_PANES_TWO
        if (-not (Poll { [Win32.Native]::GetDlgItem($v.Main, 101) -eq [IntPtr]::Zero } 5000)) {
            throw 'could not return to two panes before the handoff'
        }
    }
    # A COPY under an XML-hostile, non-ASCII filename: '&', an apostrophe and
    # a diacritic must round-trip through the writer's escaping and the
    # parser, or the handoff fails and the centre never opens. The source is
    # the 12-page sync-d: its page count is unique among the open documents,
    # which is what the identity check below pins.
    $nasty = Join-Path $docs "hand&off 'ü.pdf"
    Copy-Item $pdfD $nasty
    $second = Start-Process -FilePath $exe -ArgumentList '-open-center', "`"$nasty`"" -PassThru
    if (-not $second.WaitForExit(15000)) { $second.Kill(); throw 'verb instance did not exit' }
    Assert ($second.ExitCode -eq 0) "verb instance exit code 0 (got $($second.ExitCode))"
    Assert ((@(Get-Process PdfSideViewer -ErrorAction SilentlyContinue)).Count -eq 1) `
        'no second window: the handoff reused the running instance'
    $centerOpened = Poll {
        # Refresh the stored handle: the pre-handoff normalization DESTROYED
        # the startup centre, and the verb created a brand-new HWND.
        $v.Center = [Win32.Native]::GetDlgItem($v.Main, 101)
        if ($v.Center -eq [IntPtr]::Zero) { return $false }
        (Get-VScroll $v.Center).nMax -gt 0
    } 10000
    Assert $centerOpened 'the verb landed: three panes with the document open in the centre'
    # Identity, not merely "some scrollable document": END jumps to the last
    # page, and only the handed-off 12-page fixture ends on page 12 (the
    # neighbours hold 6-page documents).
    Focus-Pane $v $v.Center
    [void][Win32.Native]::PostMessageW($v.Center, $WM_KEYDOWN, [IntPtr]$VK_END, [IntPtr]::Zero)
    Assert-PaneAt $v $v.Center '12' 'identity pinned: the handed-off document is the 12-page fixture'
    Stop-Viewer $v

    # --------------------------------------------------------------- phase 10
    # Auto-reload must not move the SIBLING: a reopening pane leaves the
    # controller's joined set at DocumentOpening, so its view-restore echo
    # cannot lead through anchors captured for the previous document. Plain
    # anchor, no map, and deliberately NO goto between the reload and the
    # assertion (a goto would legitimize any displacement). The replacement is
    # the 3-page LETTER test-b over a 6-page A4 parked at page 5 in FIT PAGE:
    # the restored position must clamp (page box '3' = the OBSERVABLE
    # completion signal) AND the different page geometry forces the fit
    # relayout to change scale, so the restore's final reconstruction cannot
    # be idempotent - the pre-join echo NECESSARILY fires and would drag the
    # follower ~2 pages, making the exact-equality assert discriminating
    # (same-geometry sync-c could reconstruct the identical offsets and emit
    # nothing even pre-fix).
    Write-Host 'phase 10: auto-reload leaves the sibling parked (join guard on reopen)'
    $workR = Join-Path $docs 'reload-join.pdf'
    Copy-Item $pdfA $workR
    $v = Start-Viewer $workR $pdfB
    # Phase 9 persisted a three-pane arrangement; this phase is two-pane.
    if ([Win32.Native]::GetDlgItem($v.Main, 101) -ne [IntPtr]::Zero) {
        Send-Command $v $IDC_PANES_TWO
        if (-not (Poll { [Win32.Native]::GetDlgItem($v.Main, 101) -eq [IntPtr]::Zero } 5000)) {
            throw 'could not return to two panes before the reload phase'
        }
    }
    Reset-SyncLocks $v
    Send-Command $v $IDC_TOGGLE_SCROLL_SYNC
    if (-not (Poll { (Get-StatusLen $v.Status) -eq $lenScroll } 5000)) {
        throw 'could not enable scroll sync in the reload phase'
    }
    Focus-Pane $v $v.Left
    Send-Command $v $IDC_FIT_PAGE    # fit mode is what makes the relayout rescale
    Start-Sleep -Milliseconds 300
    Invoke-GotoPage $v $v.Left 5
    Start-Sleep -Milliseconds 300
    $rPark = (Get-VScroll $v.Right).nPos
    Focus-Pane $v $v.Left            # the page box mirrors the focused pane
    Copy-Item $pdfTB $workR -Force   # the watcher debounces, probes, reloads
    Assert (Poll { (Get-PageBoxText $v) -eq '3' } 20000) `
        'reload completed: the restored position clamped into the 3-page replacement'
    Start-Sleep -Milliseconds 400    # any (buggy) restore echo has landed by now
    Assert ((Get-VScroll $v.Right).nPos -eq $rPark) `
        'the reload restore did not drive the sibling (the leader moved ~2 pages)'
    Stop-Viewer $v

    # --------------------------------------------------------------- phase 11
    # The status-part schema must be configured while the bar is HIDDEN too:
    # starting with statusbar=0 leaves the control at its default single part
    # otherwise, every multi-part write fails while the cache records it, and
    # showing the bar later surfaces blank cells the change guard never
    # rewrites.
    Write-Host 'phase 11: status parts are configured while the bar is hidden'
    $ini11 = Join-Path $scratch 'settings.ini'
    # ANCHORED: -replace is case-insensitive and unanchored, so a plain
    # 'statusbar=1' would also rewrite fsStatusbar=1 (the full-screen option) -
    # and a missed edit would leave the bar VISIBLE, quietly turning the whole
    # phase into a no-op. Both the edit and the language the expected lengths
    # assume are asserted before the viewer starts.
    $ini11Text = (Get-Content $ini11 -Raw) -replace '(?m)^statusbar=1\r?$', 'statusbar=0'
    Set-Content $ini11 -Value $ini11Text -Encoding Unicode
    $ini11After = Get-Content $ini11 -Raw
    Assert (($ini11After -match '(?m)^statusbar=0\r?$') -and
            ($ini11After -notmatch '(?m)^statusbar=1\r?$') -and
            ($ini11After -match '(?m)^language=en\r?$')) `
        'the sandbox ini starts hidden and English (the lengths asserted below)'
    # A pair the suite never opened together (a|e): no remembered sync points,
    # so the expected cell is exactly "Sync: scroll" with no pts suffix.
    $v = Start-Viewer $pdfA $pdfE
    # Two panes explicitly: the sync cell INDEX depends on the arrangement
    # (SyncStatusPart), and phase 9 left a three-pane session behind.
    if ([Win32.Native]::GetDlgItem($v.Main, 101) -ne [IntPtr]::Zero) {
        Send-Command $v $IDC_PANES_TWO
        if (-not (Poll { [Win32.Native]::GetDlgItem($v.Main, 101) -eq [IntPtr]::Zero } 5000)) {
            throw 'could not return to two panes before the status-bar phase'
        }
    }
    Assert (-not [Win32.Native]::IsWindowVisible($v.Status)) `
        'the bar really starts hidden (the phase would be vacuous otherwise)'
    Assert ((Get-StatusParts $v.Status) -eq 7) `
        "the two-pane part schema exists while hidden (got $(Get-StatusParts $v.Status))"
    # The state change that used to be swallowed happens HERE, with the bar
    # still hidden: pre-fix the multi-part write failed while the cache
    # recorded it, so the cell stayed blank forever after.
    Reset-SyncLocks $v
    Send-Command $v $IDC_TOGGLE_SCROLL_SYNC
    Assert (Poll { (Get-StatusLen $v.Status) -eq $lenScroll } 5000) `
        'the sync cell is written while the bar is hidden'
    Send-Command $v $IDC_TOGGLE_STATUSBAR # show the bar for the first time
    Assert (Poll { [Win32.Native]::IsWindowVisible($v.Status) } 5000) `
        'the toggle command actually shows the bar'
    Assert (Poll { (Get-StatusLen $v.Status) -eq $lenScroll } 5000) `
        'the sync cell is populated on first show'
    Stop-Viewer $v

    # --------------------------------------------------------------- phase 12
    # Injected save failure. Building the file aside and swapping it in exists
    # so that a save which CANNOT be promoted leaves the previous coherent
    # settings untouched - never truncated, never half-written, and with no
    # sibling left behind. A read-only settings.ini fails the swap (the file
    # cannot be deleted or replaced) while the temp write itself still
    # succeeds, which is exactly the interesting path.
    Write-Host 'phase 12: settings persistence under failure (save, cleanup)'
    $ini12 = Join-Path $scratch 'settings.ini'
    $before12 = [Convert]::ToBase64String([IO.File]::ReadAllBytes($ini12))
    # NOT -Filter: that is FindFirstFile matching, where a trailing '.*' also
    # matches the bare name, so 'settings.ini.*' counts settings.ini itself.
    # (The app hands the filesystem no pattern at all - it deletes names it
    # builds itself - so it cannot hit the file it protects.)
    $siblings12 = { @(Get-ChildItem -LiteralPath $scratch -File |
            Where-Object { $_.Name -like 'settings.ini.*' } |
            ForEach-Object { $_.Name }) }
    Set-ItemProperty -LiteralPath $ini12 -Name IsReadOnly -Value $true
    try {
        $v = Start-Viewer $pdfA $pdfC
        Send-Command $v $IDC_TOGGLE_SCROLL_SYNC # a change worth persisting
        Start-Sleep -Milliseconds 400
        Stop-Viewer $v                          # SaveSession runs (and fails) here
        $after12 = [Convert]::ToBase64String([IO.File]::ReadAllBytes($ini12))
        Assert ($after12 -eq $before12) `
            'the unwritable settings file is byte-identical after the failed save'
        # @() again around the call: an empty result unrolls to $null in the
        # pipeline, and $null.Count is an error under strict mode.
        $left12 = @(& $siblings12)
        Assert ($left12.Count -eq 0) `
            "no temp sibling left behind by the failed save (found: $($left12 -join ', '))"
    } finally {
        Set-ItemProperty -LiteralPath $ini12 -Name IsReadOnly -Value $false
    }
    # The very same run WITHOUT the injection must rewrite the file - otherwise
    # "byte-identical" above would also pass on an app that never saves at all.
    # Cleanup, checked in the same run. The rule the app implements is: it
    # RECONSTRUCTS the names its OWN process id can produce
    # (<settings.ini>.<pid>.<0..63>.tmp) and deletes those, matching nothing.
    # So a foreign temp is never touched, whatever its age - the accepted cost
    # of having neither a name grammar nor a staleness clock - and the names
    # planted here before the run is even started are, by construction, all
    # foreign.
    $keep12 = [ordered]@{
        'settings.ini.999999.0.tmp'  = 'foreign and aged: age alone no longer removes anything'
        'settings.ini.999998.0.tmp'  = 'foreign and fresh: could be a save in flight'
        'settings.ini.manual.bak'    = 'not a temp name at all'
    }
    foreach ($name in $keep12.Keys) {
        $p = Join-Path $scratch $name
        Set-Content $p -Value 'leftover' -Encoding Unicode
        # Aged, except the one whose freshness is the point.
        if ($name -ne 'settings.ini.999998.0.tmp') {
            (Get-Item -LiteralPath $p).LastWriteTime = (Get-Date).AddHours(-2)
        }
    }
    # Declared security model: the settings DIRECTORY is the boundary. A save
    # promotes a freshly created temp by rename, so per-file protection added
    # by hand does NOT survive it. Pinned here as a CHARACTERIZATION test: the
    # day someone moves the promotion back to an ACL-preserving call, this
    # fails and the trade-off gets re-decided on purpose instead of by drift.
    # The SID, not the account NAME: translating a SID to a name can throw on
    # an unmappable identity (disconnected domain), and that would abort the
    # suite for an environmental reason instead of taking the skip path below.
    $me12 = [Security.Principal.WindowsIdentity]::GetCurrent().User
    # A sentinel the scratch DIRECTORY hands down to files created AFTER it,
    # so the assertions below can prove the saved file really inherited from
    # the directory: "no explicit ACEs left" alone would also pass on a
    # token-default DACL. The principal is the NULL SID (S-1-0-0), which no
    # token ever matches - so the ACE grants nothing to anybody - and which
    # holds no other ACE here, so it cannot merge into one (an ACE for the
    # current user would; and note inheritance also adds the Synchronize bit,
    # which is why identity, not rights, is what gets matched).
    # NOT checked as absent beforehand: writing a container's ACL propagates
    # its inheritable ACEs to EXISTING children too, so the current
    # settings.ini gets it immediately. What proves the promoted file is a new
    # object is the explicit ACE below disappearing across the save.
    $sentinelSid12 = New-Object Security.Principal.SecurityIdentifier('S-1-0-0')
    $hasSentinel12 = {
        param([string]$path)
        @((Get-Acl -LiteralPath $path).GetAccessRules($true, $true,
                [Security.Principal.SecurityIdentifier]) |
            Where-Object { $_.IsInherited -and $_.IdentityReference.Value -eq 'S-1-0-0' }).Count -gt 0
    }
    # CAPABILITY detection, kept apart from the fixture itself: the volume is
    # asked whether it persists ACLs at all (%TEMP% can sit on FAT32/exFAT or a
    # redirected location that does not), and only THAT skips the block. A
    # try/catch around the fixture would instead report a future bug in it -
    # wrong overload, malformed ACL, unexpected denial - as "no ACL support"
    # and leave the suite green.
    # THREE outcomes, kept apart: a failed query throws (it is a defect or an
    # environment nobody expected, and swallowing it would report "no ACL
    # support" and skip green), a successful query without the flag skips, and
    # only a successful query WITH the flag runs the fixture.
    $explicitBefore12 = @()
    $volFlags12 = [uint32]0
    $volSerial12 = [uint32]0
    $volMaxComp12 = [uint32]0
    $volName12 = New-Object System.Text.StringBuilder 260
    $fsName12 = New-Object System.Text.StringBuilder 260
    $mount12 = New-Object System.Text.StringBuilder 260
    if (-not [Win32.Native]::GetVolumePathNameW($scratch, $mount12, 260)) {
        throw "GetVolumePathName failed for $scratch (Win32 $([Runtime.InteropServices.Marshal]::GetLastWin32Error()))"
    }
    if (-not [Win32.Native]::GetVolumeInformationW($mount12.ToString(), $volName12, 260,
            [ref]$volSerial12, [ref]$volMaxComp12, [ref]$volFlags12, $fsName12, 260)) {
        throw "GetVolumeInformation failed for $($mount12.ToString()) (Win32 $([Runtime.InteropServices.Marshal]::GetLastWin32Error()))"
    }
    $aclOk12 = ($volFlags12 -band $FILE_PERSISTENT_ACLS) -ne 0
    if (-not $aclOk12) {
        Write-Host "  skip: ACL characterization ($($fsName12.ToString()) does not persist ACLs)" `
            -ForegroundColor Yellow
    } else {
        $dirAcl12 = Get-Acl -LiteralPath $scratch
        $dirAcl12.AddAccessRule((New-Object Security.AccessControl.FileSystemAccessRule(
                    $sentinelSid12, 'ReadAttributes', 'ObjectInherit', 'None', 'Allow')))
        Set-Acl -LiteralPath $scratch -AclObject $dirAcl12
        $acl12 = Get-Acl -LiteralPath $ini12
        $acl12.AddAccessRule((New-Object Security.AccessControl.FileSystemAccessRule(
                    $me12, 'FullControl', 'Allow')))
        Set-Acl -LiteralPath $ini12 -AclObject $acl12
        $explicitBefore12 = @((Get-Acl -LiteralPath $ini12).Access |
                Where-Object { -not $_.IsInherited })
    }
    $v = Start-Viewer $pdfA $pdfC
    # The names carrying the LIVE process id, planted only now because the id is
    # what the app reconstructs. Both ends of the counter range it can produce,
    # one aged and one fresh (age is not part of the rule any more), plus the
    # first counter OUTSIDE it: 64 is never a name a save writes, so the sweep
    # never reconstructs it and it survives.
    $ownSweep12 = [ordered]@{
        "settings.ini.$($v.Proc.Id).0.tmp"  = 'own id, first counter, aged'
        "settings.ini.$($v.Proc.Id).63.tmp" = 'own id, last counter, fresh'
    }
    $ownKeep12 = Join-Path $scratch "settings.ini.$($v.Proc.Id).64.tmp"
    foreach ($name in $ownSweep12.Keys) {
        $p = Join-Path $scratch $name
        Set-Content $p -Value 'leftover' -Encoding Unicode
        if ($name -like '*.0.tmp') { (Get-Item -LiteralPath $p).LastWriteTime = (Get-Date).AddHours(-2) }
    }
    Set-Content $ownKeep12 -Value 'leftover' -Encoding Unicode
    Send-Command $v $IDC_TOGGLE_SCROLL_SYNC
    Start-Sleep -Milliseconds 400
    Stop-Viewer $v
    Assert (([Convert]::ToBase64String([IO.File]::ReadAllBytes($ini12))) -ne $before12) `
        'the same run without the injection DOES rewrite the file (the check discriminates)'
    foreach ($name in $ownSweep12.Keys) {
        Assert (-not (Test-Path -LiteralPath (Join-Path $scratch $name))) `
            "swept: $name ($($ownSweep12[$name]))"
    }
    Assert (Test-Path -LiteralPath $ownKeep12) `
        'kept: own id but counter 64, past the range a save can write'
    foreach ($name in $keep12.Keys) {
        Assert (Test-Path -LiteralPath (Join-Path $scratch $name)) `
            "kept: $name ($($keep12[$name]))"
    }
    if ($aclOk12) {
        $explicitAfter12 = @((Get-Acl -LiteralPath $ini12).Access |
                Where-Object { -not $_.IsInherited })
        Assert ($explicitBefore12.Count -gt 0) 'the fixture really put an explicit ACE on the file'
        Assert ($explicitAfter12.Count -eq 0) `
            'a saved file keeps no explicit ACE of its own (declared security model)'
        Assert (& $hasSentinel12 $ini12) `
            "and does carry the directory's inheritable ACE, not a token-default DACL"
    }
    Assert ((Get-Content $ini12 -Raw).Length -gt 0) 'and is still readable by its owner'

    # The sweep's early return, which every check above leaves untested because
    # they all run with settings.ini PRESENT: an aged, grammatical temp (so
    # ordinarily sweepable) must survive the save that runs while the canonical
    # file is missing, since it can be the only complete copy left.
    # TWO candidates, both VALID settings files carrying an observable
    # non-default (statusbar=0), not junk: with junk, an implementation that DID
    # adopt one would still fall back to defaults and every file-existence
    # assertion would pass anyway. One is AGED and one is FRESH, because the
    # documented policy is unconditional - no temp is ever adopted - and an
    # implementation that adopted only fresh ones would pass an aged-only test.
    $rescueAged12 = Join-Path $scratch 'settings.ini.999995.0.tmp'
    $rescueFresh12 = Join-Path $scratch 'settings.ini.999994.0.tmp'
    foreach ($p in $rescueAged12, $rescueFresh12) {
        ((Get-Content $ini12 -Raw) -replace '(?m)^statusbar=1\r?$', 'statusbar=0') |
            Set-Content $p -Encoding Unicode
        if ((Get-Content $p -Raw) -notmatch '(?m)^statusbar=0\r?$') {
            throw "the rescue fixture $p did not get its observable non-default value"
        }
    }
    (Get-Item -LiteralPath $rescueAged12).LastWriteTime = (Get-Date).AddHours(-2)
    # Retried, per the suite's own rule: a scanner holding a handle turns the
    # delete into delete-pending, and the launch below would race it.
    for ($i = 0; $i -lt 10 -and (Test-Path -LiteralPath $ini12); $i++) {
        try { Remove-Item -LiteralPath $ini12 -Force -ErrorAction Stop }
        catch { Start-Sleep -Milliseconds 300 }
    }
    if (Test-Path -LiteralPath $ini12) { throw 'could not remove settings.ini for the rescue phase' }
    $v = Start-Viewer $pdfA $pdfC
    # An OWN-id temp too, since that is the only kind the sweep would otherwise
    # remove: without it the guard would be untestable, the two foreign files
    # above being safe for a different reason entirely.
    $guardTmp12 = Join-Path $scratch "settings.ini.$($v.Proc.Id).0.tmp"
    Set-Content $guardTmp12 -Value 'leftover' -Encoding Unicode
    Assert ([Win32.Native]::IsWindowVisible($v.Status)) `
        'NEITHER retained temp is adopted, fresh or aged: the window starts from defaults'
    Stop-Viewer $v                   # and this save recreates settings.ini
    Assert ((Test-Path -LiteralPath $rescueAged12) -and (Test-Path -LiteralPath $rescueFresh12)) `
        'both foreign temps survive the save that runs while settings.ini is absent'
    Assert (Test-Path -LiteralPath $guardTmp12) `
        "and so does this process's own, which the guard is what protects"
    # And the protection ends exactly there: forensic residue, not a recovery
    # slot. With the canonical file back, the same own-id name is swept again.
    $v = Start-Viewer $pdfA $pdfC
    $guardTmp12b = Join-Path $scratch "settings.ini.$($v.Proc.Id).0.tmp"
    Set-Content $guardTmp12b -Value 'leftover' -Encoding Unicode
    Stop-Viewer $v
    Assert (-not (Test-Path -LiteralPath $guardTmp12b)) `
        'an own-id temp IS swept by the next save, once the canonical file exists again'
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
