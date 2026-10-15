<#
.SYNOPSIS
    Sets up a self-hosted GitHub Actions runner for Cutthulhu's CI on a Windows
    x64 PC, and keeps WSL distributions running for the Linux runners.

.DESCRIPTION
    The runner is a Windows service running as its own hidden standard user,
    started at boot: nobody logs in as that user, and it never takes the screen,
    keyboard or mouse, so the PC stays yours to use. See BUILDING.md,
    "Self-hosted Windows and Linux (WSL) runners".

    Run from an elevated PowerShell (Run as administrator) in the checkout:

      scripts\setup-windows-runner.ps1 install <registration-token>
      scripts\setup-windows-runner.ps1 status
      scripts\setup-windows-runner.ps1 stop | start
      scripts\setup-windows-runner.ps1 remove <removal-token>
      scripts\setup-windows-runner.ps1 keep-wsl <distribution>
      scripts\setup-windows-runner.ps1 unkeep-wsl <distribution>

    The token comes from GitHub: the repository's Settings > Actions > Runners >
    New self-hosted runner (install), or the runner's page there (remove). It is
    valid for one hour.

    keep-wsl starts a WSL distribution when you log on to Windows and keeps it
    running, so the Linux runner inside it (scripts/setup-wsl-runner.sh) can
    take jobs. WSL only runs while you are logged on.
#>
param(
    [Parameter(Position = 0)][string]$Command,
    [Parameter(Position = 1)][string]$Argument,
    [string]$RunnerUser = 'ghrunner',
    [int]$Jobs = 4,
    [string]$QtVersion = '6.8.3',
    [string]$RepoUrl = 'https://github.com/open-compose/Lumenis'
)
$ErrorActionPreference = 'Stop'

$RunnerDir = 'C:\actions-runner'
$ToolsDir = 'C:\actions-runner-tools'

function Assert-Admin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($id)
    if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Run this from an elevated PowerShell (right-click > Run as administrator).'
    }
}

function Get-RunnerService {
    Get-Service -Name 'actions.runner.*' -ErrorAction SilentlyContinue | Select-Object -First 1
}

function New-RandomPassword {
    # Letters, digits and symbols; never shown or used to log on.
    $chars = [char[]]'abcdefghijkmnopqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789!#%+=?@'
    $bytes = New-Object byte[] 32
    [Security.Cryptography.RandomNumberGenerator]::Create().GetBytes($bytes)
    -join ($bytes | ForEach-Object { $chars[$_ % $chars.Length] })
}

function Install-Runner([string]$Token) {
    if (-not $Token) { throw 'usage: scripts\setup-windows-runner.ps1 install <registration-token>' }

    # Build tools for all users: Visual Studio Build Tools (MSVC, CMake,
    # Ninja), Python and Qt in C:\Qt, and WiX for the .msi.
    $bootstrap = Join-Path $PSScriptRoot 'bootstrap-windows.ps1'
    & $bootstrap -QtVersion $QtVersion
    $qt = "C:\Qt\$QtVersion\msvc2022_64"
    if (-not (Test-Path "$qt\bin\qmake.exe")) { throw "Qt $QtVersion did not install into $qt" }

    # WiX: bootstrap-windows.ps1 puts it in your profile; the runner's user
    # needs its own copy.
    $wix = Join-Path $ToolsDir 'wix314'
    if (-not (Test-Path (Join-Path $wix 'candle.exe'))) {
        New-Item -ItemType Directory -Force $wix | Out-Null
        Copy-Item -Recurse -Force (Join-Path $env:USERPROFILE '.wix\wix314\*') $wix
    }

    # A hidden standard (not administrator) user with a random password.
    $password = New-RandomPassword
    $secure = ConvertTo-SecureString $password -AsPlainText -Force
    if (-not (Get-LocalUser -Name $RunnerUser -ErrorAction SilentlyContinue)) {
        Write-Host "== Creating the hidden standard user $RunnerUser"
        New-LocalUser -Name $RunnerUser -Password $secure -FullName 'GitHub Actions runner' `
            -Description 'Runs Cutthulhu CI jobs' -PasswordNeverExpires -AccountNeverExpires | Out-Null
    } else {
        Set-LocalUser -Name $RunnerUser -Password $secure
    }
    $hide = 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Winlogon\SpecialAccounts\UserList'
    New-Item -Force $hide | Out-Null
    New-ItemProperty -Force -Path $hide -Name $RunnerUser -Value 0 -PropertyType DWord | Out-Null

    # The runner itself, the latest release.
    if (-not (Test-Path (Join-Path $RunnerDir 'config.cmd'))) {
        $release = Invoke-RestMethod -UseBasicParsing 'https://api.github.com/repos/actions/runner/releases/latest'
        $asset = $release.assets | Where-Object { $_.name -match '^actions-runner-win-x64-[\d.]+\.zip$' } | Select-Object -First 1
        if (-not $asset) { throw 'Could not find the latest runner release.' }
        Write-Host "== Downloading $($asset.browser_download_url)"
        New-Item -ItemType Directory -Force $RunnerDir | Out-Null
        $zip = Join-Path $env:TEMP $asset.name
        Invoke-WebRequest -UseBasicParsing -OutFile $zip $asset.browser_download_url
        Expand-Archive -Force $zip $RunnerDir
        Remove-Item $zip
    }
    foreach ($dir in $RunnerDir, $ToolsDir) {
        icacls $dir /grant "${RunnerUser}:(OI)(CI)M" /T /Q | Out-Null
    }

    # The runner's environment: the Qt and WiX it uses, and a limited number
    # of parallel compile jobs so the PC stays responsive.
    @(
        "QT_ROOT_DIR=$qt",
        "WIX=$wix",
        "CMAKE_BUILD_PARALLEL_LEVEL=$Jobs"
    ) | Set-Content -Encoding ascii (Join-Path $RunnerDir '.env')

    # Register as a service running as that user. config.cmd grants it the
    # right to log on as a service.
    Push-Location $RunnerDir
    try {
        & .\config.cmd --unattended --replace --url $RepoUrl --token $Token `
            --name "$($env:COMPUTERNAME)-cutthulhu-windows" --labels cutthulhu --work _work `
            --runasservice --windowslogonaccount ".\$RunnerUser" --windowslogonpassword $password
        if ($LASTEXITCODE -ne 0) { throw 'config.cmd failed' }
    } finally {
        Pop-Location
    }
    Write-Host ''
    Write-Host 'The runner is installed and started. Next:'
    Write-Host '  1. Check it shows as Idle under the repository''s Settings > Actions > Runners.'
    Write-Host '  2. Add the repository variable WINDOWS_RUNNER = on (Settings > Secrets and variables >'
    Write-Host '     Actions > Variables) so CI sends Windows jobs to it.'
}

function Remove-Runner([string]$Token) {
    if (-not $Token) { throw 'usage: scripts\setup-windows-runner.ps1 remove <removal-token>' }
    Push-Location $RunnerDir
    try {
        & .\config.cmd remove --token $Token
    } finally {
        Pop-Location
    }
    Write-Host "Removed. To delete the user too: Remove-LocalUser $RunnerUser"
}

function Set-KeepWsl([string]$Distribution) {
    if (-not $Distribution) { throw 'usage: scripts\setup-windows-runner.ps1 keep-wsl <distribution> (see: wsl --list)' }
    # At logon, start the distribution and keep it running; hidden, low
    # priority. WSL needs a logged-on user.
    $name = "Keep WSL $Distribution running"
    $action = New-ScheduledTaskAction -Execute 'C:\Windows\System32\wsl.exe' `
        -Argument "-d $Distribution --exec sleep infinity"
    $trigger = New-ScheduledTaskTrigger -AtLogOn -User $env:USERNAME
    $settings = New-ScheduledTaskSettingsSet -Hidden -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
        -ExecutionTimeLimit ([TimeSpan]::Zero) -Priority 7
    Register-ScheduledTask -Force -TaskName $name -Action $action -Trigger $trigger -Settings $settings `
        -Description 'Keeps the WSL distribution running for its GitHub Actions runner (Cutthulhu CI).' | Out-Null
    Start-ScheduledTask -TaskName $name
    Write-Host "'$name' registered and started; it starts again each time you log on."
}

switch ($Command) {
    'install' { Assert-Admin; Install-Runner $Argument }
    'status' {
        $service = Get-RunnerService
        if ($service) { $service | Format-Table -AutoSize Name, Status, StartType } else { Write-Host 'No runner service.' }
        Get-ScheduledTask -TaskName 'Keep WSL * running' -ErrorAction SilentlyContinue | Format-Table -AutoSize TaskName, State
    }
    'stop' {
        Assert-Admin
        $service = Get-RunnerService
        if ($service) { Stop-Service $service.Name }
        Write-Host 'Stopped. While it is stopped, set WINDOWS_RUNNER to off so CI does not wait for it.'
    }
    'start' {
        Assert-Admin
        $service = Get-RunnerService
        if ($service) { Start-Service $service.Name; Write-Host 'Started.' } else { throw 'No runner service: run install first.' }
    }
    'remove' { Assert-Admin; Remove-Runner $Argument }
    'keep-wsl' { Assert-Admin; Set-KeepWsl $Argument }
    'unkeep-wsl' {
        Assert-Admin
        Unregister-ScheduledTask -Confirm:$false -TaskName "Keep WSL $Argument running"
        Write-Host 'Removed. The distribution stops when nothing else uses it.'
    }
    default { Get-Help $PSCommandPath -Detailed; exit 1 }
}
