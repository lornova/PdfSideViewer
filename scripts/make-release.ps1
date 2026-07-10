# Packages a portable release zip: PdfSideViewer-<platform>.zip with the
# statically linked exe, license and readme. Build Release first.
param(
    [ValidateSet('x64', 'ARM64')]
    [string]$Platform = 'x64'
)

$root = Split-Path $PSScriptRoot -Parent
$exe = Join-Path $root "build\$Platform\Release\PdfSideViewer.exe"
if (-not (Test-Path $exe)) {
    Write-Error "Build Release|$Platform first: $exe not found"
    exit 1
}

$staging = Join-Path $env:TEMP "PdfSideViewer-pack-$Platform"
Remove-Item $staging -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $staging | Out-Null

Copy-Item $exe $staging
Copy-Item (Join-Path $root 'LICENSE') $staging
Copy-Item (Join-Path $root 'README.md') $staging

$zip = Join-Path $root "build\PdfSideViewer-$Platform.zip"
Remove-Item $zip -Force -ErrorAction SilentlyContinue
Compress-Archive -Path "$staging\*" -DestinationPath $zip
Remove-Item $staging -Recurse -Force

$size = [math]::Round((Get-Item $zip).Length / 1MB, 1)
Write-Host "created $zip ($size MB)"
