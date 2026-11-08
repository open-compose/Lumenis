<#
.SYNOPSIS
    Downloads a shared build of FFmpeg for Windows x64 (BtbN's FFmpeg-Builds,
    the newest release branch) for development and CI, and prints the folder
    to use as FFMPEG_ROOT. Run it again to reuse the download; -Update
    fetches the newest build.

.DESCRIPTION
    No administrator rights are needed. The release's asset list says which
    builds exist; the archive is checked against the release's published
    SHA-256 checksums. Inside GitHub Actions the script also sets
    FFMPEG_ROOT for the following steps, and reports failures as errors on
    the run.

    -Variant gpl (the default) is BtbN's GPL build, which has the H.264 and
    H.265 encoders (libx264, libx265) that MOV, MP4 and MXF export use; the
    LGPL build has none, so exports in those formats fail (owner's report,
    2026-11-08; BUILDING.md "Export says there is no H.264 encoder").
    -Variant lgpl gets the LGPL build. With the GPL build the script checks
    that both encoders are there and, in GitHub Actions, sets
    LUMENIS_REQUIRE_ENCODERS so that tst_movie_export fails without them.
    GPL parts are compatible with this project's licence
    (THIRD-PARTY-NOTICES.md). Official releases will use a build of our own
    with the codecs that wait for counsel left out (docs/EXTENSIONS.md);
    this one is for development.
#>
param(
    [string]$Dest = (Join-Path $env:LOCALAPPDATA 'Cutthulhu\ffmpeg'),
    [ValidateSet('gpl', 'lgpl')]
    [string]$Variant = 'gpl',
    [switch]$Update
)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$pattern = "^ffmpeg-n([\d.]+)-latest-win64-$Variant-shared-[\d.]+\.zip$"
$required = if ($Variant -eq 'gpl') { @('libx264', 'libx265') } else { @() }

function Get-Cached {
    if (-not (Test-Path $Dest)) { return $null }
    Get-ChildItem -Directory $Dest |
        Where-Object { $_.Name -match "^ffmpeg-n[\d.]+-latest-win64-$Variant-shared" -and
                       (Test-Path (Join-Path $_.FullName 'include\libavcodec\avcodec.h')) } |
        Sort-Object { [version](($_.Name -replace '^ffmpeg-n([\d.]+)-.*$', '$1') + '.0') } -Descending |
        Select-Object -First 1
}

function Get-Latest {
    $release = Invoke-RestMethod -UseBasicParsing -Headers @{ 'User-Agent' = 'cutthulhu-get-ffmpeg' } `
        'https://api.github.com/repos/BtbN/FFmpeg-Builds/releases/tags/latest'
    $zips = $release.assets | Where-Object { $_.name -match $pattern }
    if (-not $zips) {
        $names = ($release.assets | ForEach-Object name) -join ', '
        throw "the latest BtbN release has no win64 $Variant shared release-branch build (assets: $names)"
    }
    $zip = $zips | Sort-Object { [version](($_.name -replace $pattern, '$1') + '.0') } -Descending | Select-Object -First 1
    $sums = $release.assets | Where-Object { $_.name -eq 'checksums.sha256' } | Select-Object -First 1
    if (-not $sums) { throw 'the latest BtbN release has no checksums.sha256' }
    @{ Zip = $zip; Sums = $sums }
}

try {
    $cached = Get-Cached
    if ($cached -and -not $Update) {
        $root = $cached.FullName
    } else {
        $latest = Get-Latest
        $name = $latest.Zip.name -replace '\.zip$', ''
        $root = Join-Path $Dest $name
        if (-not (Test-Path (Join-Path $root 'include\libavcodec\avcodec.h'))) {
            New-Item -ItemType Directory -Force $Dest | Out-Null
            $zip = Join-Path $Dest $latest.Zip.name
            Write-Host "== Downloading $($latest.Zip.name)"
            Invoke-WebRequest -UseBasicParsing -OutFile $zip $latest.Zip.browser_download_url
            $sums = Invoke-WebRequest -UseBasicParsing $latest.Sums.browser_download_url
            $text = if ($sums.Content -is [byte[]]) { [Text.Encoding]::ASCII.GetString($sums.Content) } else { $sums.Content }
            $line = ($text -split "`n") | Where-Object { $_ -match [regex]::Escape($latest.Zip.name) } | Select-Object -First 1
            if (-not $line) { throw "checksums.sha256 has no entry for $($latest.Zip.name)" }
            $expected = ($line.Trim() -split '\s+')[0].ToLower()
            $actual = (Get-FileHash -Algorithm SHA256 $zip).Hash.ToLower()
            if ($actual -ne $expected) { Remove-Item $zip; throw "checksum mismatch for $($latest.Zip.name)" }
            if (Test-Path $root) { Remove-Item -Recurse -Force $root }
            Expand-Archive -Force $zip $Dest
            Remove-Item $zip
        }
    }
    if (-not (Test-Path (Join-Path $root 'bin'))) { throw "$root has no bin folder" }
    # The encoders the GPL build is fetched for, so that a build without them is found here and not in an export.
    if ($required.Count -gt 0) {
        $encoders = & (Join-Path $root 'bin\ffmpeg.exe') -hide_banner -encoders 2>$null | Out-String
        foreach ($e in $required) {
            if ($encoders -notmatch "\b$e\b") { throw "$root has no $e encoder (ffmpeg -encoders)" }
        }
    }
} catch {
    $message = "get-ffmpeg: $($_.Exception.Message)"
    if ($env:GITHUB_ACTIONS) { Write-Host "::error::$message" } else { Write-Error $message -ErrorAction Continue }
    exit 1
}

Write-Host "FFMPEG_ROOT=$root"
if ($env:GITHUB_ENV) {
    "FFMPEG_ROOT=$root" | Out-File -FilePath $env:GITHUB_ENV -Encoding utf8 -Append
    if ($required.Count -gt 0) {
        "LUMENIS_REQUIRE_ENCODERS=$($required -join ',')" | Out-File -FilePath $env:GITHUB_ENV -Encoding utf8 -Append
    }
}
