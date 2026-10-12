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
Install-WingetPackage WiXToolset.WiXToolset
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

Write-Host ''
Write-Host 'Done. Open a NEW PowerShell window (so PATH changes apply), then:'
Write-Host "  `$env:QT_ROOT_DIR = 'C:\Qt\$QtVersion\msvc2022_64'"
Write-Host '  scripts\build.ps1 -Package'
