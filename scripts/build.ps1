<#
.SYNOPSIS
    Build, test and package on Windows x64.

.EXAMPLE
    scripts\build.ps1 -Qt C:\Qt\6.8.3\msvc2022_64 -Package

.DESCRIPTION
    Needs Visual Studio 2022 (or its Build Tools) with the "Desktop development
    with C++" workload, CMake 3.24+ and Ninja. The script enters the Visual
    Studio x64 developer environment itself, so it can run from any PowerShell.
    Qt is found from -Qt, then $env:QT_ROOT_DIR.
#>
param(
    [switch]$Debug,
    [string]$Qt = $env:QT_ROOT_DIR,
    [switch]$NoTests,
    [switch]$Package,
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
Set-Location (Join-Path $PSScriptRoot '..')

function Enter-VsDevEnvironment {
    if ($env:VSCMD_ARG_TGT_ARCH -eq 'x64') { return }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { throw 'Visual Studio 2022 not found (vswhere.exe missing).' }
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) { throw 'Visual Studio with the C++ x64 tools is not installed.' }
    Import-Module (Join-Path $vs 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
    Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
}

function Invoke-Checked {
    & $args[0] $args[1..($args.Count - 1)]
    if ($LASTEXITCODE -ne 0) { throw "$($args[0]) failed with exit code $LASTEXITCODE" }
}

Enter-VsDevEnvironment

$preset = if ($Debug) { 'debug' } else { 'release' }
if ($Qt) { $env:QT_ROOT_DIR = $Qt }
if ($Clean -and (Test-Path "build\$preset")) { Remove-Item -Recurse -Force "build\$preset" }

Invoke-Checked cmake --preset $preset
Invoke-Checked cmake --build --preset $preset

if (-not $NoTests) {
    # The app tests need Qt's DLLs on PATH when run from the build tree.
    if ($env:QT_ROOT_DIR) { $env:PATH = "$env:QT_ROOT_DIR\bin;$env:PATH" }
    Invoke-Checked ctest --preset $preset
}

if ($Package) {
    Push-Location "build\$preset"
    try { Invoke-Checked cpack } finally { Pop-Location }
    Write-Host "Packages are in build\$preset"
}
