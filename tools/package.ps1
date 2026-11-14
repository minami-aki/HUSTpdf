<#
.SYNOPSIS
    Build a portable Windows release folder + zip (no model file).
    (ASCII only: Windows PowerShell 5.1 reads BOM-less scripts as ANSI.)

    Contents: HUSTpdf.exe, llama/ggml DLLs (one ggml-cpu-* per CPU variant), the Visual C++
    runtime DLLs needed by the /MD llama/ggml DLLs (app-local), data/ (fonts, domains,
    glossaries), LICENSE, THIRD_PARTY_NOTICES.md, README.md and licenses/ (license text of
    every bundled third-party component).

    GPU (M8): ggml-vulkan.dll ships in the main zip (needs only the GPU driver at run time).
    ggml-cuda.dll and the NVIDIA runtime it imports (cudart, cuBLAS) are large, so they go to a
    separate add-on zip ($name-cuda.zip) to extract into the same folder as HUSTpdf.exe.

.EXAMPLE
    .\tools\package.ps1                 # package build\release (must be built)
    .\tools\package.ps1 -Build          # build + test release first
    .\tools\package.ps1 -Tools          # also include pdfread-translate.exe / pdfread-eval.exe
#>
param(
    [string]$Preset = 'release',
    [string]$OutDir = 'dist',
    [switch]$Build,
    [switch]$Tools,
    [switch]$NoZip
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root

# MSVC environment (for dumpbin and the redistributable runtime folder).
if (-not $env:VCToolsRedistDir -or -not (Get-Command dumpbin.exe -ErrorAction SilentlyContinue)) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { throw 'vswhere.exe not found (Visual Studio / Build Tools not installed?).' }
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) { throw 'MSVC x64 toolset not found.' }
    $vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
    $vsInstallerDir = Split-Path $vswhere -Parent
    cmd /c "set `"PATH=%PATH%;$vsInstallerDir`" && `"$vcvars`" >nul && set" | ForEach-Object {
        if ($_ -match '^([^=]+)=(.*)$') { Set-Item -Path "env:$($Matches[1])" -Value $Matches[2] }
    }
}

if ($Build) {
    & (Join-Path $PSScriptRoot 'build.ps1') -Preset $Preset -Test
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

$bin = Join-Path $root "build\$Preset\bin"
if (-not (Test-Path (Join-Path $bin 'HUSTpdf.exe'))) { throw "HUSTpdf.exe not found in $bin (build first or pass -Build)." }

$version = (Get-Content (Join-Path $root 'vcpkg.json') -Raw | ConvertFrom-Json).version
$name = "HUSTpdf-$version-win64"
$out = Join-Path $root $OutDir
$stage = Join-Path $out $name
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force (Join-Path $stage 'licenses') | Out-Null

# 1. Executables and llama/ggml DLLs.
$exes = @('HUSTpdf.exe')
if ($Tools) { $exes += @('pdfread-translate.exe', 'pdfread-eval.exe') }
foreach ($e in $exes) { Copy-Item (Join-Path $bin $e) $stage }
Get-ChildItem $bin -File -Filter '*.dll' |
    Where-Object { ($_.Name -eq 'llama.dll' -or $_.Name -like 'ggml*.dll') -and $_.Name -ne 'ggml-cuda.dll' } |
    ForEach-Object { Copy-Item $_.FullName $stage }

# 2. Data (fonts, domains.json, glossaries) - from the source tree, not the build copy.
Copy-Item (Join-Path $root 'data') (Join-Path $stage 'data') -Recurse

# 3. Visual C++ runtime DLLs actually imported by the packaged binaries (app-local deployment).
$imports = @{}
Get-ChildItem $stage -File | Where-Object { $_.Extension -in '.exe', '.dll' } | ForEach-Object {
    (dumpbin /nologo /dependents $_.FullName) | ForEach-Object {
        if ($_ -match '^\s+(\S+\.dll)\s*$') { $imports[$Matches[1].ToLower()] = $true }
    }
}
$crtDir = Get-ChildItem (Join-Path $env:VCToolsRedistDir 'x64') -Directory -Filter 'Microsoft.VC*.CRT' |
    Sort-Object Name -Descending | Select-Object -First 1
if (-not $crtDir) { throw "Visual C++ redistributable folder not found under $env:VCToolsRedistDir" }
$runtime = @()
foreach ($dll in $imports.Keys) {
    if ($dll -match '^(vcruntime|msvcp|concrt|vccorlib)') {
        $src = Join-Path $crtDir.FullName $dll
        if (-not (Test-Path $src)) { throw "Runtime DLL $dll not found in $($crtDir.FullName)" }
        Copy-Item $src $stage
        $runtime += $dll
    }
}

# 4. Licenses: project, notices, third-party license texts.
Copy-Item (Join-Path $root 'LICENSE') (Join-Path $stage 'LICENSE.txt')
Copy-Item (Join-Path $root 'THIRD_PARTY_NOTICES.md') $stage
Copy-Item (Join-Path $root 'README.md') $stage
$lic = Join-Path $stage 'licenses'
Copy-Item (Join-Path $root 'third_party\llama.cpp\LICENSE') (Join-Path $lic 'llama.cpp.txt')
Copy-Item (Join-Path $root 'data\fonts\OFL.txt') (Join-Path $lic 'NotoSans-OFL.txt')
$share = Join-Path $root 'build\vcpkg_installed\x64-windows-static-pdfread\share'
$testOnly = @('catch2')
Get-ChildItem $share -Directory | ForEach-Object {
    $c = Join-Path $_.FullName 'copyright'
    if ((Test-Path $c) -and ($testOnly -notcontains $_.Name)) { Copy-Item $c (Join-Path $lic "$($_.Name).txt") }
}
@"
Microsoft Visual C++ runtime ($($runtime -join ', ')) is redistributed app-local from
$($crtDir.Name) under the Microsoft Visual Studio redistributable license terms
(see https://visualstudio.microsoft.com/license-terms/ - "Distributable Code").
"@ | Out-File -Encoding utf8 (Join-Path $lic 'MSVC-runtime.txt')

# 5. Zip.
function New-Zip([string]$Folder, [string]$Zip) {
    if (Test-Path $Zip) { Remove-Item -Force $Zip }
    Compress-Archive -Path $Folder -DestinationPath $Zip -CompressionLevel Optimal
    Write-Host ("Created {0} ({1:N1} MB)" -f $Zip, ((Get-Item $Zip).Length / 1MB))
}
$size = (Get-ChildItem $stage -Recurse -File | Measure-Object Length -Sum).Sum
Write-Host ("Staged {0}: {1:N1} MB, runtime: {2}" -f $stage, ($size / 1MB), ($runtime -join ', '))
if (-not $NoZip) { New-Zip $stage (Join-Path $out "$name.zip") }

# 6. CUDA add-on: ggml-cuda.dll + the NVIDIA runtime DLLs it imports. The folder has the same
#    name as the main one, so extracting both zips into one place merges them.
$cudaDll = Join-Path $bin 'ggml-cuda.dll'
if (Test-Path $cudaDll) {
    $cudaPath = $env:CUDA_PATH
    if (-not $cudaPath) { $cudaPath = [Environment]::GetEnvironmentVariable('CUDA_PATH', 'Machine') }
    if (-not $cudaPath) { throw 'ggml-cuda.dll was built but CUDA_PATH is not set (CUDA Toolkit missing?).' }
    $addonRoot = Join-Path $out "$name-cuda"
    $addon = Join-Path $addonRoot $name
    if (Test-Path $addonRoot) { Remove-Item -Recurse -Force $addonRoot }
    New-Item -ItemType Directory -Force (Join-Path $addon 'licenses') | Out-Null
    Copy-Item $cudaDll $addon
    # Follow imports transitively (cublas64 -> cublasLt64 ...), only DLLs that ship in the toolkit.
    $cudaBins = @((Join-Path $cudaPath 'bin\x64'), (Join-Path $cudaPath 'bin')) | Where-Object { Test-Path $_ }
    $queue = New-Object System.Collections.Queue
    $queue.Enqueue($cudaDll)
    $copied = @{}
    while ($queue.Count -gt 0) {
        $file = $queue.Dequeue()
        (dumpbin /nologo /dependents $file) | ForEach-Object {
            if ($_ -match '^\s+(\S+\.dll)\s*$') {
                $dll = $Matches[1]
                if ($copied.ContainsKey($dll.ToLower())) { return }
                foreach ($dir in $cudaBins) {
                    $src = Join-Path $dir $dll
                    if (Test-Path $src) {
                        Copy-Item $src $addon
                        $copied[$dll.ToLower()] = $true
                        $queue.Enqueue($src)
                        break
                    }
                }
            }
        }
    }
    $eula = Get-ChildItem $cudaPath -File -Filter 'EULA*.txt' -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($eula) { Copy-Item $eula.FullName (Join-Path $addon 'licenses\NVIDIA-CUDA-EULA.txt') }
    @"
HUSTpdf - CUDA add-on (GPU NVIDIA)

Extract this zip into the folder that contains HUSTpdf.exe (the folder names match, so the
files land next to HUSTpdf.exe), then restart HUSTpdf and pick CUDA in Settings > Translation
model > Device. Without this add-on, NVIDIA GPUs still work through the Vulkan backend.

Contents: ggml-cuda.dll (llama.cpp, MIT) and NVIDIA CUDA runtime libraries
($(@($copied.Keys) -join ', ')), redistributed under the NVIDIA CUDA Toolkit EULA
(licenses\NVIDIA-CUDA-EULA.txt). Needs an NVIDIA driver recent enough for CUDA $((Split-Path $cudaPath -Leaf)).
"@ | Out-File -Encoding utf8 (Join-Path $addon 'README-CUDA.txt')
    $addonSize = (Get-ChildItem $addon -Recurse -File | Measure-Object Length -Sum).Sum
    Write-Host ("Staged CUDA add-on {0}: {1:N1} MB ({2})" -f $addon, ($addonSize / 1MB), (@($copied.Keys) -join ', '))
    if (-not $NoZip) { New-Zip $addon (Join-Path $out "$name-cuda.zip") }
}
