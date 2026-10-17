<#
.SYNOPSIS
    Install build dependencies on Windows x64 with winget, and Qt 6.8 LTS with
    aqtinstall into C:\Qt. Safe to run again: installed tools are skipped.
#>
param([string]$QtVersion = '6.8.3')
$ErrorActionPreference = 'Stop'

function Install-WingetPackage([string]$Id, [string[]]$Extra = @()) {
    Write-Host "== $Id"
    winget install --id $Id -e --accept-source-agreements --accept-package-agreements @Extra
    # winget exits non-zero when the package is already installed; that is fine.
}

# winget changes PATH in the registry, but a running PowerShell keeps the PATH
# it started with. Reload it so newly installed tools are found right away.
function Update-SessionPath {
    $machine = [Environment]::GetEnvironmentVariable('Path', 'Machine')
    $user = [Environment]::GetEnvironmentVariable('Path', 'User')
    $env:Path = "$machine;$user"
}

# Returns the command to run Python 3.12 (exe plus any arguments), or nothing.
function Find-Python {
    $py = Get-Command py -ErrorAction SilentlyContinue
    if ($py) {
        & $py.Source -3.12 --version *> $null
        if ($LASTEXITCODE -eq 0) { return $py.Source, '-3.12' }
    }
    foreach ($exe in "$env:LOCALAPPDATA\Programs\Python\Python312\python.exe",
                     "$env:ProgramFiles\Python312\python.exe") {
        if (Test-Path $exe) { return $exe }
    }
}

Install-WingetPackage Kitware.CMake
Install-WingetPackage Ninja-build.Ninja
Install-WingetPackage Python.Python.3.12
Install-WingetPackage Microsoft.VisualStudio.2022.BuildTools `
    @('--override', '--quiet --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended')

Update-SessionPath

$python = @(Find-Python)
if ($python.Count -eq 0) {
    throw 'Python 3.12 was installed but cannot be found. Open a new PowerShell window and run this script again.'
}
$pyExe = $python[0]
$pyArgs = @($python | Select-Object -Skip 1)
Write-Host "== Using Python: $($python -join ' ')"

& $pyExe @pyArgs -m pip install --upgrade aqtinstall
if ($LASTEXITCODE -ne 0) { throw 'pip install aqtinstall failed' }

if (Test-Path "C:\Qt\$QtVersion\msvc2022_64\bin\qmake.exe") {
    Write-Host "== Qt $QtVersion already installed in C:\Qt"
} else {
    & $pyExe @pyArgs -m aqt install-qt windows desktop $QtVersion win64_msvc2022_64 -O C:\Qt
    if ($LASTEXITCODE -ne 0) { throw 'Qt download failed' }
}

# WiX Toolset 3.14 builds the .msi. Use the official binaries zip (no admin
# rights, no installer) rather than winget, whose package is a different
# major version that CPack's MSI generator does not use by default.
$wixDir = Join-Path $env:USERPROFILE '.wix\wix314'
if (Test-Path (Join-Path $wixDir 'candle.exe')) {
    Write-Host "== WiX 3.14 already installed in $wixDir"
} else {
    Write-Host '== WiX Toolset 3.14'
    $zip = Join-Path $env:TEMP 'wix314-binaries.zip'
    Invoke-WebRequest -UseBasicParsing -OutFile $zip `
        'https://github.com/wixtoolset/wix3/releases/download/wix3141rtm/wix314-binaries.zip'
    $hash = (Get-FileHash -Algorithm SHA256 $zip).Hash
    if ($hash -ne '6AC824E1642D6F7277D0ED7EA09411A508F6116BA6FAE0AA5F2C7DAA2FF43D31') {
        throw "WiX download has an unexpected checksum: $hash"
    }
    New-Item -ItemType Directory -Force $wixDir | Out-Null
    Expand-Archive -Force $zip $wixDir
    Remove-Item $zip
}
[Environment]::SetEnvironmentVariable('WIX', $wixDir, 'User')
$env:WIX = $wixDir

Write-Host ''
Write-Host 'Done. Open a NEW PowerShell window (so PATH changes apply), then:'
Write-Host "  `$env:QT_ROOT_DIR = 'C:\Qt\$QtVersion\msvc2022_64'"
# FFmpeg (an LGPL shared build) for the media library: set FFMPEG_ROOT to
# the folder it prints.
& (Join-Path $PSScriptRoot 'get-ffmpeg.ps1') | Out-Host
Write-Host '  scripts\build.ps1 -Package'
