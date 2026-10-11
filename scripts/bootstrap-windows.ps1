<#
.SYNOPSIS
    Install build dependencies on Windows x64 with winget, and Qt 6.8 LTS with
    aqtinstall into C:\Qt.
#>
param([string]$QtVersion = '6.8.3')
$ErrorActionPreference = 'Stop'

winget install --id Kitware.CMake -e --accept-source-agreements --accept-package-agreements
winget install --id Ninja-build.Ninja -e --accept-source-agreements --accept-package-agreements
winget install --id Python.Python.3.12 -e --accept-source-agreements --accept-package-agreements
winget install --id WiXToolset.WiXToolset -e --accept-source-agreements --accept-package-agreements
winget install --id Microsoft.VisualStudio.2022.BuildTools -e --accept-source-agreements --accept-package-agreements `
    --override '--quiet --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended'

py -3.12 -m pip install --upgrade aqtinstall
py -3.12 -m aqt install-qt windows desktop $QtVersion win64_msvc2022_64 -O C:\Qt

Write-Host ''
Write-Host 'Open a new PowerShell window (so PATH updates apply), then:'
Write-Host "  `$env:QT_ROOT_DIR = 'C:\Qt\$QtVersion\msvc2022_64'"
Write-Host '  scripts\build.ps1 -Package'
