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
# Phase 13's header-tooltip checks drive the REAL mouse pointer and need it to
# rest: do not use the machine while the suite runs, or they skip themselves.
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
# -SysColorCheck adds the assertions that prove the painted COLOURS follow the
# system palette (toolbar icons via COLOR_BTNTEXT, pane header strip via
# COLOR_BTNFACE): it changes the value for the session, samples the pixels, and
# puts the old one back (also from the failure path). OFF by default because
# those values are global to the desktop: a hard kill between the two
# SetSysColors calls leaves every application recoloured until the next theme
# change. Everything else here only ever touches this app. The strip assertion
# FLIPS with high contrast, which is the only mode in which the panes read the
# system palette at all.
param([string]$Config = 'Debug', [switch]$SysColorCheck)

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
[DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
[DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
// The exit code of a process this script did NOT start: a Process object from
// Get-Process leaves ExitCode empty, so hold a real handle (which also pins the
// pid against reuse) and ask the kernel.
[DllImport("kernel32.dll", SetLastError=true)] public static extern IntPtr OpenProcess(uint access, bool inherit, uint pid);
[DllImport("kernel32.dll", SetLastError=true)] public static extern bool GetExitCodeProcess(IntPtr h, out uint code);
[DllImport("kernel32.dll", SetLastError=true)] public static extern bool CloseHandle(IntPtr h);
// The header-tip check moves the REAL cursor: a synthetic WM_MOUSEMOVE arms
// TrackMouseEvent(TME_LEAVE), and with the physical pointer elsewhere Windows
// answers with an immediate WM_MOUSELEAVE that takes the tip straight down.
[DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
[DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
[DllImport("user32.dll")] public static extern bool GetCursorPos(out POINT p);
// Mouse messages go to whatever is on TOP at that point, so the hover checks
// have to raise the viewer above this console first and then verify it.
[DllImport("user32.dll")] public static extern IntPtr WindowFromPoint(POINT p);
[DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
// -SysColorCheck only: the icon ink lives in pixels, so proving it followed the
// palette needs a capture. SetSysColors is session-scoped (no SPIF_UPDATEINIFILE).
[DllImport("user32.dll")] public static extern uint GetSysColor(int index);
[DllImport("user32.dll")] public static extern bool SetSysColors(int count, int[] indices, uint[] colors);
[DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
[DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
// The panes take their colours from the system ONLY in high contrast, so the
// strip assertion needs to know which claim it is proving. SPI_GETHIGHCONTRAST
// is the only supported probe.
[DllImport("user32.dll", CharSet=CharSet.Unicode, EntryPoint="SystemParametersInfoW")] public static extern bool SpiHighContrast(uint action, uint param, ref HIGHCONTRAST hc, uint winIni);
[StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)] public struct HIGHCONTRAST { public uint cbSize, dwFlags; public IntPtr lpszDefaultScheme; }
// Volume capability, so the ACL characterization can be skipped on a filesystem
// that does not persist ACLs instead of failing for an environmental reason.
// The mount point comes from GetVolumePathName, not from a lexical path root:
// that one returns "\\server\share" without the trailing backslash the query
// requires, and on a mounted volume it names the wrong volume entirely.
[DllImport("kernel32.dll", CharSet=CharSet.Unicode, EntryPoint="GetVolumePathNameW", SetLastError=true)] public static extern bool GetVolumePathNameW(string fileName, System.Text.StringBuilder mountPoint, uint bufferLength);
[DllImport("kernel32.dll", CharSet=CharSet.Unicode, EntryPoint="GetVolumeInformationW", SetLastError=true)] public static extern bool GetVolumeInformationW(string root, System.Text.StringBuilder volName, uint volNameSize, out uint serial, out uint maxComponent, out uint flags, System.Text.StringBuilder fsName, uint fsNameSize);
[StructLayout(LayoutKind.Sequential)] public struct SCROLLINFO { public uint cbSize, fMask; public int nMin, nMax; public uint nPage; public int nPos, nTrackPos; }
[StructLayout(LayoutKind.Sequential)] public struct RECT { public int l, t, r, b; }
[StructLayout(LayoutKind.Sequential)] public struct POINT { public int x, y; }
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
$TB_ISBUTTONCHECKED = 0x040A
$TB_GETSTATE = 0x0412
$TB_GETIMAGELIST = 0x0431
$WM_SYSCOLORCHANGE = 0x0015
$TBSTATE_ENABLED = 0x04
$WM_LBUTTONDOWN = 0x0201
$WM_LBUTTONDBLCLK = 0x0203
$WM_LBUTTONUP = 0x0202
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
$IDC_SYNC_POINTS = 1053
$IDC_CLEAR_SYNC_POINTS = 1054
$IDC_TOGGLE_ALIGNMENT_GAPS = 1055
$IDC_PANES_TWO = 1075
$IDC_PANES_THREE = 1076
$IDC_FULLSCREEN = 1020
$IDC_NEW_WINDOW = 1083
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

Write-Host 'note: phase 13 drives the REAL mouse pointer (hover tooltips). Leave the machine' `
    -ForegroundColor DarkYellow
Write-Host '      alone while it runs; those checks skip themselves if the pointer is moved.' `
    -ForegroundColor DarkYellow

$script:failures = 0
# Declared even when -SysColorCheck is off: the finally reads it, and strict
# mode turns an unset variable into a throw.
$script:sysColor13 = $null
$script:sysFace13 = $null
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
# The command toolbar is a rebar child, not a frame child: its id (102) is the
# same number the RIGHT pane uses under the frame, which is why the parent
# matters here.
function Get-Toolbar([IntPtr]$main) {
    $rebar = [Win32.Native]::FindWindowExByClass($main, [IntPtr]::Zero, 'ReBarWindow32',
                                                 [IntPtr]::Zero)
    if ($rebar -eq [IntPtr]::Zero) { return [IntPtr]::Zero }
    [Win32.Native]::GetDlgItem($rebar, 102)
}
function Test-ButtonPressed([IntPtr]$toolbar, [int]$id) {
    [Win32.Native]::SendMessageW($toolbar, $TB_ISBUTTONCHECKED, [IntPtr]$id,
                                 [IntPtr]::Zero) -ne [IntPtr]::Zero
}
function Test-ButtonEnabled([IntPtr]$toolbar, [int]$id) {
    ([Win32.Native]::SendMessageW($toolbar, $TB_GETSTATE, [IntPtr]$id,
                                  [IntPtr]::Zero).ToInt64() -band $TBSTATE_ENABLED) -ne 0
}
# The frame of a given process, and how many frames exist at all. Both walk the
# top-level windows: with more than one instance the class name alone no longer
# identifies a window.
function Get-FrameFor([int]$procId) {
    $h = [IntPtr]::Zero
    while ($true) {
        $h = [Win32.Native]::FindWindowExByClass([IntPtr]::Zero, $h, 'PsvMainWindow',
                                                 [IntPtr]::Zero)
        if ($h -eq [IntPtr]::Zero) { return [IntPtr]::Zero }
        $owner = [uint32]0
        [void][Win32.Native]::GetWindowThreadProcessId($h, [ref]$owner)
        if ($owner -eq $procId) { return $h }
    }
}
function Get-FrameCount() {
    $n = 0
    $h = [IntPtr]::Zero
    while ($true) {
        $h = [Win32.Native]::FindWindowExByClass([IntPtr]::Zero, $h, 'PsvMainWindow',
                                                 [IntPtr]::Zero)
        if ($h -eq [IntPtr]::Zero) { return $n }
        $n++
    }
}
# Strongly red pixels in a window's own painting: the sampling the icon-colour
# check counts, and nothing else in the chrome can produce them.
function Measure-RedPixels([IntPtr]$hwnd) {
    Add-Type -AssemblyName System.Drawing
    $r = New-Object Win32.Native+RECT
    [void][Win32.Native]::GetWindowRect($hwnd, [ref]$r)
    $w = $r.r - $r.l; $h = $r.b - $r.t
    if ($w -le 0 -or $h -le 0) { return 0 }
    $bmp = New-Object System.Drawing.Bitmap([int]$w, [int]$h)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $hdc = $g.GetHdc()
    [void][Win32.Native]::PrintWindow($hwnd, $hdc, 0)
    $g.ReleaseHdc($hdc); $g.Dispose()
    $n = 0
    for ($y = 0; $y -lt $h; $y += 2) {
        for ($x = 0; $x -lt $w; $x += 2) {
            $p = $bmp.GetPixel($x, $y)
            if ($p.R -gt 180 -and $p.G -lt 80 -and $p.B -lt 80) { $n++ }
        }
    }
    $bmp.Dispose()
    $n
}
# Is a high-contrast theme active? The panes swap their whole palette for the
# system one exactly then, so the -SysColorCheck assertion below flips with it.
function Test-HighContrast() {
    $hc = New-Object Win32.Native+HIGHCONTRAST
    $hc.cbSize = [uint32][Runtime.InteropServices.Marshal]::SizeOf($hc)
    if (-not [Win32.Native]::SpiHighContrast(0x0042, $hc.cbSize, [ref]$hc, 0)) { return $false }
    ($hc.dwFlags -band 1) -ne 0 # HCF_HIGHCONTRASTON
}
# The pane header strip's BACKGROUND, sampled clear of the file name (which is
# left-aligned) and clear of the active pane's accent underline. PW_RENDERFULLCONTENT
# is what captures a DirectX swap chain; plain PrintWindow returns black.
function Get-HeaderStripColor([IntPtr]$pane) {
    Add-Type -AssemblyName System.Drawing
    $r = New-Object Win32.Native+RECT
    [void][Win32.Native]::GetClientRect($pane, [ref]$r)
    if ($r.r -le 0 -or $r.b -le 0) { return '' }
    $bmp = New-Object System.Drawing.Bitmap([int]$r.r, [int]$r.b)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $hdc = $g.GetHdc()
    [void][Win32.Native]::PrintWindow($pane, $hdc, 2)
    $g.ReleaseHdc($hdc); $g.Dispose()
    $dpi = [Win32.Native]::GetDpiForWindow($pane)
    if ($dpi -eq 0) { $dpi = 96 }
    $p = $bmp.GetPixel([int]($r.r - 5), [int](24 * $dpi / 96 / 2)) # kHeaderHeightDip / 2
    $bmp.Dispose()
    '#{0:X2}{1:X2}{2:X2}' -f $p.R, $p.G, $p.B
}
function Get-CursorPoint() {
    $p = New-Object Win32.Native+POINT
    [void][Win32.Native]::GetCursorPos([ref]$p)
    $p
}
function Get-CaptureHwnd([IntPtr]$main) {
    $procId = [uint32]0
    $tid = [Win32.Native]::GetWindowThreadProcessId($main, [ref]$procId)
    $gui = New-Object Win32.Native+GUITHREADINFO
    $gui.cbSize = [Runtime.InteropServices.Marshal]::SizeOf($gui)
    if ([Win32.Native]::GetGUIThreadInfo($tid, [ref]$gui)) { return $gui.hwndCapture }
    return [IntPtr]::Zero
}
# Client point packed the way Windows packs it into a mouse message's lParam.
function Get-MouseLParam([int]$x, [int]$y) {
    [IntPtr](($y -shl 16) -bor ($x -band 0xFFFF))
}
# Parks the physical pointer on a client point of $hwnd, nudging it by a pixel
# so a WM_MOUSEMOVE is guaranteed even if it was already there.
function Move-CursorToClient([IntPtr]$hwnd, [int]$x, [int]$y) {
    $p = New-Object Win32.Native+POINT
    $p.x = $x; $p.y = $y
    [void][Win32.Native]::ClientToScreen($hwnd, [ref]$p)
    [void][Win32.Native]::SetCursorPos($p.x, $p.y)
    Start-Sleep -Milliseconds 60
    [void][Win32.Native]::SetCursorPos($p.x + 1, $p.y)
}
# Every VISIBLE tooltip owned by the viewer process. Compared as a SET across an
# action, never as a count: a stray tip (the real mouse resting on a toolbar
# button) would otherwise turn an environmental accident into a failure.
function Get-VisibleTips([int]$procId) {
    # Emits the handles one by one: a collection returned whole would reach the
    # callers' pipelines as a SINGLE object.
    $h = [IntPtr]::Zero
    while ($true) {
        $h = [Win32.Native]::FindWindowExByClass([IntPtr]::Zero, $h, 'tooltips_class32',
                                                 [IntPtr]::Zero)
        if ($h -eq [IntPtr]::Zero) { break }
        $owner = [uint32]0
        [void][Win32.Native]::GetWindowThreadProcessId($h, [ref]$owner)
        if ($owner -eq $procId -and [Win32.Native]::IsWindowVisible($h)) { $h }
    }
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
    # NOT Process.CloseMainWindow(): .NET picks the first visible unowned
    # top-level in z-order as "main window", and a transient tooltip SysShadow
    # (TOPMOST, visible, ownerless) outranks the frame, losing the WM_CLOSE.
    # The frame HWND is already known: post WM_CLOSE to it directly.
    [void][Win32.Native]::PostMessageW($v.Main, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
    if (-not $v.Proc.WaitForExit(10000)) { $v.Proc.Kill(); throw 'viewer did not exit after WM_CLOSE' }
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

    # --------------------------------------------------------------- phase 13
    # Toolbar state that no other button has: the sync-point button reports
    # whether the pages IN VIEW already carry a point, so it follows the scroll
    # (UpdateCommandUi is skipped on scroll ticks; a dedicated change-guarded
    # updater owns this one), and pressing it there REMOVES that point instead
    # of replacing it with an identical manual one. The pane-count pair is the
    # View menu's radio group mirrored onto the toolbar. Last phase: the count
    # switch clears the live map, so nothing may depend on it afterwards.
    Write-Host 'phase 13: the sync-point toggle, the pane-count pair and the header strip'
    # Forced with no viewer running: the full-screen assertions need the toolbar
    # KEPT there (fsToolbar), and the header tooltip needs the strip in file-NAME
    # mode, where the path is never on screen (phase 7 left headerPath=1, and in
    # path mode a short fixture path is not compacted, so no tip is owed).
    $ini13 = Join-Path $scratch 'settings.ini'
    $ini13Text = (Get-Content $ini13 -Raw) -replace '(?m)^fsToolbar=0\r?$', 'fsToolbar=1' `
        -replace '(?m)^headerPath=1\r?$', 'headerPath=0'
    Set-Content $ini13 -Value $ini13Text -Encoding Unicode
    $ini13After = Get-Content $ini13 -Raw
    Assert (($ini13After -match '(?m)^fsToolbar=1\r?$') -and
            ($ini13After -match '(?m)^headerPath=0\r?$') -and
            ($ini13After -match '(?m)^header=1\r?$')) `
        'the sandbox ini forces the kept full-screen toolbar and the file-name header'
    $v = Start-Viewer $pdfA $pdfB
    Reset-SyncLocks $v
    $tb = Get-Toolbar $v.Main
    Assert ($tb -ne [IntPtr]::Zero) 'the command toolbar answers under the rebar'
    # Explicit pages, never the ones the launch happened to restore.
    Invoke-GotoPage $v $v.Left 2
    Invoke-GotoPage $v $v.Right 2
    Assert (-not (Test-ButtonPressed $tb $IDC_ADD_SYNC_POINT)) `
        'no point on these pages yet: the button is up'
    Send-Command $v $IDC_ADD_SYNC_POINT
    Assert (Poll { Test-ButtonPressed $tb $IDC_ADD_SYNC_POINT } 5000) `
        'adding a point here presses the button'
    Assert (Poll { (Get-StatusLen $v.Status 3) -eq ($lenOff + " $mid 1 pts".Length) } 5000) `
        'and the map really holds one point'
    Invoke-GotoPage $v $v.Left 5
    Assert (Poll { -not (Test-ButtonPressed $tb $IDC_ADD_SYNC_POINT) } 5000) `
        'scrolling off the tuple releases it (no command involved, only Scrolled)'
    Invoke-GotoPage $v $v.Left 2
    Assert (Poll { Test-ButtonPressed $tb $IDC_ADD_SYNC_POINT } 5000) `
        'scrolling back onto the tuple presses it again'
    # The toggle: pressed + command = removal, not a duplicate manual point.
    Send-Command $v $IDC_ADD_SYNC_POINT
    Assert (Poll { -not (Test-ButtonPressed $tb $IDC_ADD_SYNC_POINT) } 5000) `
        'the command on a pressed button releases it'
    Assert (Poll { (Get-StatusLen $v.Status 3) -eq $lenOff } 5000) `
        'and the point is GONE (the map is empty, not carrying a replacement)'
    Assert (-not (Test-ButtonEnabled $tb $IDC_SYNC_POINTS)) `
        'the points dialog button greys out with the emptied map'
    # The pane-count pair, pressed from the same UpdateCommandUi pass.
    Assert ((Test-ButtonPressed $tb $IDC_PANES_TWO) -and
            -not (Test-ButtonPressed $tb $IDC_PANES_THREE)) 'two panes: the pair reads 2'
    Send-Command $v $IDC_PANES_THREE
    Assert (Poll { [Win32.Native]::GetDlgItem($v.Main, 101) -ne [IntPtr]::Zero } 5000) `
        'the toolbar button created the centre pane'
    Assert (Poll { (Test-ButtonPressed $tb $IDC_PANES_THREE) -and
                   -not (Test-ButtonPressed $tb $IDC_PANES_TWO) } 5000) 'and the pair reads 3'
    Send-Command $v $IDC_PANES_TWO
    Assert (Poll { [Win32.Native]::GetDlgItem($v.Main, 101) -eq [IntPtr]::Zero } 5000) `
        'and back: the centre pane is destroyed again'
    Assert (Poll { (Test-ButtonPressed $tb $IDC_PANES_TWO) -and
                   -not (Test-ButtonPressed $tb $IDC_PANES_THREE) } 5000) 'and the pair reads 2'

    # A system-colour change must re-bake the glyph imagelists: their ink is
    # GetSysColor(COLOR_BTNTEXT), premultiplied into the pixels, and every cache
    # is keyed on the DPI alone. Observable from here because the rebuild
    # INSTALLS the new list before destroying the old, so the handle cannot come
    # back the same. What this does NOT prove is the colour: with high contrast
    # off the query answers the same value either way, and switching the theme
    # from a test would take over the whole desktop.
    $iml13 = [Win32.Native]::SendMessageW($tb, $TB_GETIMAGELIST, [IntPtr]::Zero, [IntPtr]::Zero)
    Assert ($iml13 -ne [IntPtr]::Zero) 'the command toolbar has a glyph imagelist'
    [void][Win32.Native]::PostMessageW($v.Main, $WM_SYSCOLORCHANGE, [IntPtr]::Zero, [IntPtr]::Zero)
    Assert (Poll {
            [Win32.Native]::SendMessageW($tb, $TB_GETIMAGELIST, [IntPtr]::Zero,
                                         [IntPtr]::Zero) -ne $iml13
        } 5000) 'and a system-colour change rebuilds it'
    if ($SysColorCheck) {
        # The rebuild above is only half the claim. This half changes the palette
        # for real and reads the ink back out of the pixels.
        $script:sysColor13 = [Win32.Native]::GetSysColor(18) # COLOR_BTNTEXT
        Assert ((Measure-RedPixels $tb) -eq 0) 'no red ink in the toolbar to begin with'
        [void][Win32.Native]::SetSysColors(1, @(18), @([uint32]0x000000FF)) # BGR
        $reddened = Poll { (Measure-RedPixels $tb) -gt 20 } 5000
        [void][Win32.Native]::SetSysColors(1, @(18), @([uint32]$script:sysColor13))
        $script:sysColor13 = $null # restored; the finally has nothing left to do
        Assert $reddened 'and the glyphs are re-baked in the NEW colour, not just rebuilt'
        Assert (Poll { (Measure-RedPixels $tb) -eq 0 } 5000) 'and follow it back again'

        # Same claim for the panes, which paint their chrome themselves - but
        # with the opposite expectation outside high contrast, where taking the
        # classic system colours would flatten the app's own dark/light palette.
        $hcOn13 = Test-HighContrast
        $strip13 = Get-HeaderStripColor $v.Right
        $script:sysFace13 = [Win32.Native]::GetSysColor(15) # COLOR_BTNFACE
        [void][Win32.Native]::SetSysColors(1, @(15), @([uint32]0x000000FF)) # BGR
        $stripFollowed = Poll { (Get-HeaderStripColor $v.Right) -eq '#FF0000' } 5000
        [void][Win32.Native]::SetSysColors(1, @(15), @([uint32]$script:sysFace13))
        $script:sysFace13 = $null # restored; the finally has nothing left to do
        if ($hcOn13) {
            Assert $stripFollowed 'high contrast: the header strip follows COLOR_BTNFACE'
        } else {
            Assert (-not $stripFollowed) `
                'outside high contrast the header strip keeps the built-in palette'
        }
        Assert (Poll { (Get-HeaderStripColor $v.Right) -eq $strip13 } 5000) `
            "and the strip is back to what it was ($strip13)"
    }

    # The full-screen button reports the STATE, like the View menu item does.
    Assert (-not (Test-ButtonPressed $tb $IDC_FULLSCREEN)) 'windowed: the full-screen button is up'
    Send-Command $v $IDC_FULLSCREEN
    Assert (Poll { Test-ButtonPressed $tb $IDC_FULLSCREEN } 5000) `
        'full screen presses it (the toolbar is kept there by fsToolbar)'
    Send-Command $v $IDC_FULLSCREEN
    Assert (Poll { -not (Test-ButtonPressed $tb $IDC_FULLSCREEN) } 5000) `
        'and leaving full screen releases it'

    # The header strip: hovering it offers the full path, which the strip does
    # not show in file-name mode. The tooltip TEXT is unreadable across
    # processes (TTM_GETTEXT takes a pointer), so this asserts the tip WINDOW,
    # identified as one that was not visible before the hover - never as a
    # count, or a stray tip elsewhere in the app would decide the result.
    # The PHYSICAL pointer has to move, for two reasons. The tip arms
    # TrackMouseEvent(TME_LEAVE), and with the real cursor outside the pane
    # Windows answers instantly with WM_MOUSELEAVE, taking the tip down before
    # it can be observed. And the show waits on WM_MOUSEHOVER (the strip is too
    # easy to cross for a tip on contact), which hover tracking only ever
    # delivers for real mouse input that then RESTS: a synthetic WM_MOUSEMOVE
    # arms the request and nothing more comes of it.
    $rc13 = New-Object Win32.Native+RECT
    [void][Win32.Native]::GetClientRect($v.Left, [ref]$rc13)
    $cursor13 = New-Object Win32.Native+POINT
    [void][Win32.Native]::GetCursorPos([ref]$cursor13) # restored at the end
    # Park the pointer in the page area FIRST. The check below wants a tip that
    # was not there before, and the real cursor may well be sitting on the strip
    # already (the window comes up wherever it comes up): the correct behaviour
    # would then raise no NEW window and the phase would fail for the one reason
    # that is not a defect.
    # And raise the viewer: mouse messages go to whatever window is on top at
    # that point, and by now this console may well be. HWND_TOP with
    # SWP_NOACTIVATE, not SetForegroundWindow, which a background process does
    # not get to call.
    [void][Win32.Native]::SetWindowPos($v.Main, [IntPtr]::Zero, 0, 0, 0, 0, 0x13)
    Move-CursorToClient $v.Left 40 ([int]($rc13.b / 2))
    Start-Sleep -Milliseconds 300
    $tipsBefore = @(Get-VisibleTips $v.Proc.Id)
    # y = 5 is inside the strip at every DPI (24 DIP is 24 px at the smallest).
    Move-CursorToClient $v.Left 40 5
    $parked13 = Get-CursorPoint
    $script:tip13 = [IntPtr]::Zero
    # Hover tracking needs the PHYSICAL pointer to rest on a pane that is on TOP
    # for SPI_GETMOUSEHOVERTIME. Someone using the machine breaks both, and the
    # result is indistinguishable from a broken tooltip - so the conditions are
    # sampled THROUGHOUT the wait (checking them only at the end misses a
    # pointer that wandered off and came back), and a disturbed run SKIPS
    # instead of reporting a defect that is not there. Same treatment as the ACL
    # characterization on a filesystem that cannot keep ACLs.
    $script:disturbed13 = $false
    [void](Poll {
        $at = Get-CursorPoint
        if ($at.x -ne $parked13.x -or $at.y -ne $parked13.y -or
            [Win32.Native]::WindowFromPoint($parked13) -ne $v.Left) {
            $script:disturbed13 = $true
            return $true # stop waiting: nothing observable can come of this
        }
        $fresh = @(Get-VisibleTips $v.Proc.Id | Where-Object { $tipsBefore -notcontains $_ })
        if ($fresh.Count -gt 0) { $script:tip13 = $fresh[0]; return $true }
        $false
    } 5000)
    if ($script:disturbed13 -and $script:tip13 -eq [IntPtr]::Zero) {
        Write-Host '  skip: the pointer was disturbed during the hover (machine in use?)'
    } else {
        Assert ($script:tip13 -ne [IntPtr]::Zero) 'hovering the header strip raises a tooltip'
        # And it comes down when the pointer moves off the strip, which is also
        # the regression guard for the latch (a tip that never hides passes above).
        Move-CursorToClient $v.Left 40 ([int]($rc13.b / 2))
        Assert ($script:tip13 -ne [IntPtr]::Zero -and
                (Poll { -not [Win32.Native]::IsWindowVisible($script:tip13) } 5000)) `
            'and it drops when the pointer leaves the strip'
    }

    # The strip is a label, not a window onto the content it covers: a press
    # inside it must not start a text selection (no capture), while the same
    # press over the page does.
    $stripPt13 = Get-MouseLParam 40 5
    [void][Win32.Native]::PostMessageW($v.Left, $WM_LBUTTONDOWN, [IntPtr]1, $stripPt13)
    Start-Sleep -Milliseconds 250
    $capStrip = Get-CaptureHwnd $v.Main
    [void][Win32.Native]::PostMessageW($v.Left, $WM_LBUTTONUP, [IntPtr]::Zero, $stripPt13)
    Assert ($capStrip -eq [IntPtr]::Zero) 'a press on the strip starts no selection (no capture)'
    $mid13 = Get-MouseLParam ([int]($rc13.r / 2)) ([int]($rc13.b / 2))
    [void][Win32.Native]::PostMessageW($v.Left, $WM_LBUTTONDOWN, [IntPtr]1, $mid13)
    $capPage = Poll { (Get-CaptureHwnd $v.Main) -eq $v.Left } 3000
    [void][Win32.Native]::PostMessageW($v.Left, $WM_LBUTTONUP, [IntPtr]::Zero, $mid13)
    Assert $capPage 'the same press over the page DOES (the check discriminates)'
    # And a DOUBLE click on the strip asks for another document, the gesture an
    # empty pane already offers on its placeholder. The dialog is MODAL - it
    # blocks the app's UI thread - so it has to be cancelled here or everything
    # after this, Stop-Viewer included, hangs.
    [void][Win32.Native]::PostMessageW($v.Left, $WM_LBUTTONDBLCLK, [IntPtr]1, $stripPt13)
    $openDlg13 = [IntPtr]::Zero
    $opened13 = Poll {
        $script:openDlg13 = [Win32.Native]::FindWindowByTitle([IntPtr]::Zero,
                                                              'Open document in left pane')
        $script:openDlg13 -ne [IntPtr]::Zero
    } 10000
    Assert $opened13 'double-clicking the strip opens the Open dialog for that pane'
    if ($opened13) {
        [void][Win32.Native]::PostMessageW($script:openDlg13, $WM_COMMAND, [IntPtr]2,
                                           [IntPtr]::Zero) # IDCANCEL
        Assert (Poll { -not [Win32.Native]::IsWindow($script:openDlg13) } 10000) `
            'and it closes again on cancel (the modal loop must not outlive the phase)'
    }
    [void][Win32.Native]::SetCursorPos($cursor13.x, $cursor13.y)
    Stop-Viewer $v

    # --------------------------------------------------------------- phase 14
    # File > New Window: a second PROCESS, started EMPTY, that does not land on
    # top of the first, does not take the session away from it, and does not
    # steal a forward search for a document it is not showing. Self-contained
    # and LAST on purpose: while it runs there are two frames of the same class,
    # which every FindWindowByClass in the suite would resolve arbitrarily.
    Write-Host 'phase 14: File > New Window'
    $ini14 = Join-Path $scratch 'settings.ini'
    Set-Content $ini14 -Encoding Unicode `
        -Value ((Get-Content $ini14 -Raw) -replace '(?m)^restoreSession=0\r?$', 'restoreSession=1')
    Assert ((Get-Content $ini14 -Raw) -match '(?m)^restoreSession=1\r?$') `
        'the sandbox restores sessions, which the survival check below reads'
    $v = Start-Viewer $pdfA $pdfB
    Assert ((Get-FrameCount) -eq 1) 'one frame to begin with'
    Send-Command $v $IDC_NEW_WINDOW
    Assert (Poll { (@(Get-Process PdfSideViewer -ErrorAction SilentlyContinue)).Count -eq 2 } 20000) `
        'the command started a second process'
    $child14 = @(Get-Process PdfSideViewer | Where-Object { $_.Id -ne $v.Proc.Id })[0]
    $parentOf14 = (Get-CimInstance Win32_Process -Filter "ProcessId=$($child14.Id)").ParentProcessId
    Assert ($parentOf14 -eq $v.Proc.Id) 'and it is OUR child, not a stray instance'
    # SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, taken while it is alive.
    $childHandle14 = [Win32.Native]::OpenProcess(0x00101000, $false, [uint32]$child14.Id)
    $childMain14 = [IntPtr]::Zero
    Assert (Poll {
            $script:childMain14 = Get-FrameFor $child14.Id
            $script:childMain14 -ne [IntPtr]::Zero -and
                [Win32.Native]::GetDlgItem($script:childMain14, 100) -ne [IntPtr]::Zero
        } 15000) 'the child put up its own frame'
    $childLeft14 = [Win32.Native]::GetDlgItem($script:childMain14, 100)
    $childRight14 = [Win32.Native]::GetDlgItem($script:childMain14, 102)
    # An empty pane has no scroll range, ever - the same predicate Start-Viewer
    # waits on to know a document arrived, read the other way round. Given a
    # moment first, so "empty" is not just "has not opened yet".
    Start-Sleep -Milliseconds 1500
    Assert ((Get-VScroll $childLeft14).nMax -eq 0 -and (Get-VScroll $childRight14).nMax -eq 0) `
        'the new window is EMPTY: it did not reopen the session'
    Assert ((Get-VScroll $v.Left).nMax -gt 0) 'while the first window keeps its documents'
    $rParent14 = New-Object Win32.Native+RECT
    $rChild14 = New-Object Win32.Native+RECT
    [void][Win32.Native]::GetWindowRect($v.Main, [ref]$rParent14)
    [void][Win32.Native]::GetWindowRect($script:childMain14, [ref]$rChild14)
    # ORIGIN only: the exact cascade step depends on DPI and theme metrics.
    Assert (($rParent14.l -ne $rChild14.l) -or ($rParent14.t -ne $rChild14.t)) `
        'and it does not come up exactly on top of the first'
    # Forward search with two windows. This is only a test if the plain z-order
    # handoff WOULD have picked the wrong one, so pin that first: FindWindowEx
    # walks top-level windows in z-order, and its first answer is exactly what
    # FindWindowW hands the sender.
    Assert (([Win32.Native]::FindWindowExByClass([IntPtr]::Zero, [IntPtr]::Zero, 'PsvMainWindow',
                                                 [IntPtr]::Zero)) -eq $script:childMain14) `
        'the new window is the topmost frame: without the claim round it would take the request'
    $tex14 = Join-Path $docs 'paper.tex'
    $fwd14 = Start-Process -FilePath $exe -ArgumentList '-forward-search', "`"$tex14`"", '1',
        "`"$pdfA`"" -PassThru
    if (-not $fwd14.WaitForExit(20000)) { $fwd14.Kill(); throw 'forward-search instance hung' }
    Assert ($fwd14.ExitCode -eq 0) "forward-search instance exit code 0 (got $($fwd14.ExitCode))"
    Start-Sleep -Milliseconds 1200
    Assert ((Get-VScroll $childLeft14).nMax -eq 0 -and (Get-VScroll $childRight14).nMax -eq 0) `
        'the forward search left the empty window alone (it went to the one holding the pdf)'
    # Parent first, then the EMPTY child: the LAST close is the one that decides
    # what settings.ini ends up holding, so the empty window has to be it or the
    # check below proves nothing (the populated window would simply rewrite its
    # own session over any damage).
    Stop-Viewer $v
    [void][Win32.Native]::PostMessageW($script:childMain14, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
    if (-not $child14.WaitForExit(15000)) { $child14.Kill(); throw 'the new window did not close' }
    $code14 = [uint32]0
    $gotCode14 = [Win32.Native]::GetExitCodeProcess($childHandle14, [ref]$code14)
    [void][Win32.Native]::CloseHandle($childHandle14)
    Assert ($gotCode14 -and $code14 -eq 0) "new-window exit code 0 (got $code14)"
    $back14 = Start-Process -FilePath $exe -PassThru # no arguments: pure restore
    try {
        Assert (Poll { (Get-FrameFor $back14.Id) -ne [IntPtr]::Zero } 15000) 'the plain relaunch came up'
        $backMain14 = Get-FrameFor $back14.Id
        $backLeft14 = [Win32.Native]::GetDlgItem($backMain14, 100)
        $backRight14 = [Win32.Native]::GetDlgItem($backMain14, 102)
        Assert (Poll { (Get-VScroll $backLeft14).nMax -gt 0 -and
                       (Get-VScroll $backRight14).nMax -gt 0 } 15000) `
            'and both documents came back: the empty window, closing LAST, wrote them back'
    } finally {
        [void][Win32.Native]::PostMessageW((Get-FrameFor $back14.Id), 0x0010, [IntPtr]::Zero,
                                           [IntPtr]::Zero)
        if (-not $back14.WaitForExit(15000)) { $back14.Kill() }
    }
} finally {
    # A throw between the two SetSysColors calls must not leave the desktop
    # recoloured (see -SysColorCheck).
    if ($null -ne $script:sysColor13) {
        [void][Win32.Native]::SetSysColors(1, @(18), @([uint32]$script:sysColor13))
        Write-Host '  (system colour restored on the way out)'
    }
    if ($null -ne $script:sysFace13) {
        [void][Win32.Native]::SetSysColors(1, @(15), @([uint32]$script:sysFace13))
        Write-Host '  (3D-objects colour restored on the way out)'
    }
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
