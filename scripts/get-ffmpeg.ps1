<#
.SYNOPSIS
    Downloads an LGPL shared build of FFmpeg for Windows x64 (BtbN's
    FFmpeg-Builds, release branch 7.1) for development and CI, and prints
    the folder to use as FFMPEG_ROOT. Run it again to reuse the download.

.DESCRIPTION
    No administrator rights are needed. The archive is checked against the
    release's published SHA-256 checksums. Inside GitHub Actions the script
    also sets FFMPEG_ROOT for the following steps.

    Official releases will use a build of our own with the codecs that wait
    for counsel left out (docs/EXTENSIONS.md); this one is for development.
#>
param(
    [string]$Dest = (Join-Path $env:LOCALAPPDATA 'Cutthulhu\ffmpeg'),
    [string]$Branch = '7.1'
)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$name = "ffmpeg-n$Branch-latest-win64-lgpl-shared-$Branch"
$base = 'https://github.com/BtbN/FFmpeg-Builds/releases/download/latest'
$root = Join-Path $Dest $name

if (-not (Test-Path (Join-Path $root 'include\libavcodec\avcodec.h'))) {
    New-Item -ItemType Directory -Force $Dest | Out-Null
    $zip = Join-Path $Dest "$name.zip"
    Write-Host "== Downloading $name.zip"
    Invoke-WebRequest -UseBasicParsing -OutFile $zip "$base/$name.zip"
    $sums = Invoke-WebRequest -UseBasicParsing "$base/checksums.sha256"
    $text = if ($sums.Content -is [byte[]]) { [Text.Encoding]::ASCII.GetString($sums.Content) } else { $sums.Content }
    $line = ($text -split "`n") | Where-Object { $_ -match [regex]::Escape("$name.zip") } | Select-Object -First 1
    if (-not $line) { throw "checksums.sha256 has no entry for $name.zip" }
    $expected = ($line -split '\s+')[0].ToLower()
    $actual = (Get-FileHash -Algorithm SHA256 $zip).Hash.ToLower()
    if ($actual -ne $expected) { Remove-Item $zip; throw "checksum mismatch for $name.zip" }
    if (Test-Path $root) { Remove-Item -Recurse -Force $root }
    Expand-Archive -Force $zip $Dest
    Remove-Item $zip
}
if (-not (Test-Path (Join-Path $root 'bin'))) { throw "$root has no bin folder" }

Write-Host "FFMPEG_ROOT=$root"
if ($env:GITHUB_ENV) { "FFMPEG_ROOT=$root" | Out-File -FilePath $env:GITHUB_ENV -Encoding utf8 -Append }
