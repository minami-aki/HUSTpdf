<#
.SYNOPSIS
    Configure, build and optionally test the project.
    (ASCII only: Windows PowerShell 5.1 reads BOM-less scripts as ANSI.)

.EXAMPLE
    .\tools\build.ps1                  # debug
    .\tools\build.ps1 -Preset release -Test
#>
param(
    [ValidateSet('debug', 'release')]
    [string]$Preset = 'debug',
    [switch]$Test
)

$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot -Parent)

# Import the MSVC x64 environment when cl.exe is not on PATH.
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
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

if (-not $env:VCPKG_ROOT) { throw 'VCPKG_ROOT environment variable is not set.' }

cmake --preset $Preset
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

cmake --build --preset $Preset
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if ($Test) {
    ctest --preset $Preset
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
