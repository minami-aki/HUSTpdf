<#
.SYNOPSIS
    Build the single-file Windows installer dist\HUSTpdf-<version>-win64-setup.exe with Inno Setup.
    (ASCII only: Windows PowerShell 5.1 reads BOM-less scripts as ANSI.)

    Contents: everything from tools\package.ps1 (HUSTpdf.exe, llama/ggml DLLs incl. every CPU
    variant and ggml-vulkan.dll, VC++ runtime, data\, licenses) plus the CUDA add-on (ggml-cuda.dll
    + cuBLAS, optional component, selected by default). The translation model is NOT included:
    the app downloads it on first run.

    Needs Inno Setup 6 (winget install JRSoftware.InnoSetup) and a Release build with Vulkan and
    CUDA (tools\build.ps1 -Preset release).

.EXAMPLE
    .\tools\make_installer.ps1            # stage build\release + compile the installer
    .\tools\make_installer.ps1 -Build     # build + test release first
#>
param(
    [string]$Preset = 'release',
    [switch]$Build
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root

$iscc = @(
    (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe'),
    (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'),
    (Join-Path $env:ProgramFiles 'Inno Setup 6\ISCC.exe')
) | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) { throw 'ISCC.exe (Inno Setup 6) not found. Install: winget install JRSoftware.InnoSetup' }

# 1. Stage files in a private folder (does not touch dist\, which may be in use by a running app).
$stageRoot = Join-Path $root 'build\installer-stage'
$pkgArgs = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $PSScriptRoot 'package.ps1'),
          '-Preset', $Preset, '-OutDir', 'build\installer-stage', '-NoZip')
if ($Build) { $pkgArgs += '-Build' }
& powershell @pkgArgs
if ($LASTEXITCODE -ne 0) { throw "package.ps1 failed ($LASTEXITCODE)" }

$version = (Get-Content (Join-Path $root 'vcpkg.json') -Raw | ConvertFrom-Json).version
$name = "HUSTpdf-$version-win64"
$main = Join-Path $stageRoot $name
$cuda = Join-Path $stageRoot "$name-cuda\$name"
if (-not (Test-Path (Join-Path $main 'HUSTpdf.exe'))) { throw "Staged app not found in $main" }
if (-not (Test-Path (Join-Path $main 'ggml-vulkan.dll'))) { Write-Warning 'ggml-vulkan.dll missing: this build has no Vulkan backend.' }

# 2. Compile the installer.
$out = Join-Path $root 'dist'
New-Item -ItemType Directory -Force $out | Out-Null
$defines = @("/DAppVersion=$version", "/DStageMain=$main", "/DOutDir=$out")
if (Test-Path (Join-Path $cuda 'ggml-cuda.dll')) { $defines += "/DStageCuda=$cuda" }
else { Write-Warning 'No CUDA add-on staged: the installer will have CPU + Vulkan only.' }
& $iscc /Q @defines (Join-Path $PSScriptRoot 'installer\hustpdf.iss')
if ($LASTEXITCODE -ne 0) { throw "ISCC failed ($LASTEXITCODE)" }

$setup = Join-Path $out "$name-setup.exe"
Write-Host ("Created {0} ({1:N1} MB)" -f $setup, ((Get-Item $setup).Length / 1MB))
