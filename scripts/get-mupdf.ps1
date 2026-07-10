# Downloads and unpacks the pinned MuPDF source release into vendor/mupdf
# (the tree is not tracked in git: ~0.5 GB of sources available upstream).
# With -Build, also compiles the static libraries the app links against.
param(
    [string]$Version = '1.28.0',
    [switch]$Build,
    [ValidateSet('x64', 'ARM64')]
    [string[]]$Platforms = @('x64')
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$vendor = Join-Path $root 'vendor\mupdf'
$url = "https://mupdf.com/downloads/archive/mupdf-$Version-source.tar.gz"
$archive = Join-Path $env:TEMP "mupdf-$Version-source.tar.gz"

if (Test-Path (Join-Path $vendor 'platform\win32\mupdf.sln')) {
    Write-Host "vendor/mupdf already present, skipping download"
} else {
    Write-Host "downloading $url ..."
    curl.exe -sL -o $archive $url
    if ($LASTEXITCODE -ne 0) { Write-Error "download failed"; exit 1 }
    New-Item -ItemType Directory -Force $vendor | Out-Null
    Write-Host "extracting to $vendor ..."
    tar -xzf $archive -C $vendor --strip-components=1
    if ($LASTEXITCODE -ne 0) { Write-Error "extraction failed"; exit 1 }
    Remove-Item $archive -ErrorAction SilentlyContinue
}

if ($Build) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Workload.NativeDesktop -property installationPath
    if (-not $vs) { Write-Error "Visual Studio with the C++ workload not found"; exit 1 }
    $msbuild = Join-Path $vs 'MSBuild\Current\Bin\MSBuild.exe'
    $sln = Join-Path $vendor 'platform\win32\mupdf.sln'
    foreach ($platform in $Platforms) {
        foreach ($config in 'Release', 'Debug') {
            # libresources (pure data) only exists in Release.
            $targets = if ($config -eq 'Release') { 'libmupdf;libthirdparty;libresources' }
                       else { 'libmupdf;libthirdparty' }
            Write-Host "building MuPDF $config|$platform ..."
            & $msbuild $sln "/t:$targets" "/p:Configuration=$config" "/p:Platform=$platform" `
                /p:PlatformToolset=v143 /m /v:m /nologo
            if ($LASTEXITCODE -ne 0) { Write-Error "MuPDF build failed ($config|$platform)"; exit 1 }
        }
    }
    Write-Host "done: now build PdfSideViewer.sln"
}
