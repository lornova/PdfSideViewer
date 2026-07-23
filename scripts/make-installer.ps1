# Compiles the winget/direct-download installer (scripts\PdfSideViewer.iss)
# into build\PdfSideViewer-Setup-x64.exe. Build Release x64 first. The version
# is read from app\res\resource.h so it can never drift from VERSIONINFO.
$ErrorActionPreference = 'Stop'

$root = Split-Path $PSScriptRoot -Parent

$exe = Join-Path $root 'build\x64\Release\PdfSideViewer.exe'
if (-not (Test-Path $exe)) {
    Write-Error "Build Release x64 first: $exe not found"
    exit 1
}

# ISCC: winget installs per-user, the classic setup per-machine.
$iscc = @(
    "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe",
    "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
    "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe"
) | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) { $iscc = (Get-Command iscc -ErrorAction SilentlyContinue).Source }
if (-not $iscc) {
    Write-Error 'Inno Setup 6 not found (winget install JRSoftware.InnoSetup)'
    exit 1
}

$resource = Get-Content (Join-Path $root 'app\res\resource.h') -Raw
$ver = @{}
foreach ($part in 'MAJOR', 'MINOR', 'PATCH', 'BUILD') {
    if ($resource -notmatch "#define PSV_VERSION_$part (\d+)") {
        Write-Error "PSV_VERSION_$part not found in resource.h"
        exit 1
    }
    $ver[$part] = [int]$Matches[1]
}
$appVersion = '{0}.{1}.{2}' -f $ver.MAJOR, $ver.MINOR, $ver.PATCH
$fileVersion = "$appVersion.$($ver.BUILD)"

& $iscc /Qp (Join-Path $root 'scripts\PdfSideViewer.iss') `
    "/DAppVersion=$appVersion" "/DFileVersion=$fileVersion" "/DRepoRoot=$root"
if ($LASTEXITCODE -ne 0) {
    Write-Error "ISCC failed with exit code $LASTEXITCODE"
    exit 1
}

$setup = Join-Path $root 'build\PdfSideViewer-Setup-x64.exe'
$size = [math]::Round((Get-Item $setup).Length / 1MB, 1)
Write-Host "created $setup ($size MB, version $appVersion)"
