# Push this job's screenshots to ci-screenshots/<branch>/<name> through
# GitHub's Git Data API, for the ui-consistency job: the Windows twin of
# "scripts/ci-screenshots.py push" (the PC may have no git or Python).
#
#   scripts\ci-screenshots.ps1 -Name windows -Dir shots
#
# Needs GITHUB_TOKEN (contents: write), GITHUB_REPOSITORY, GITHUB_RUN_ID and
# GITHUB_REF_NAME, as set in a GitHub Actions job.
param(
    [Parameter(Mandatory = $true)][string]$Name,
    [Parameter(Mandatory = $true)][string]$Dir
)
$ErrorActionPreference = 'Stop'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$api = if ($env:GITHUB_API_URL) { $env:GITHUB_API_URL } else { 'https://api.github.com' }
$repo = "$api/repos/$env:GITHUB_REPOSITORY"
$headers = @{
    Authorization          = "Bearer $env:GITHUB_TOKEN"
    Accept                 = 'application/vnd.github+json'
    'X-GitHub-Api-Version' = '2022-11-28'
}
function Invoke-Api([string]$Method, [string]$Url, $Body) {
    $json = $Body | ConvertTo-Json -Depth 5 -Compress
    Invoke-RestMethod -Method $Method -Uri $Url -Headers $headers -Body $json -ContentType 'application/json'
}

$zip = Join-Path $env:RUNNER_TEMP 'shots.zip'
Remove-Item -Force $zip -ErrorAction SilentlyContinue
Compress-Archive -Path (Join-Path $Dir '*') -DestinationPath $zip
$content = [Convert]::ToBase64String([IO.File]::ReadAllBytes($zip))

$blob = Invoke-Api POST "$repo/git/blobs" @{ content = $content; encoding = 'base64' }
$tree = Invoke-Api POST "$repo/git/trees" @{ tree = @(@{ path = 'shots.zip'; mode = '100644'; type = 'blob'; sha = $blob.sha }) }
$message = "Screenshots: $Name, run $env:GITHUB_RUN_ID, $env:GITHUB_SHA"
$commit = Invoke-Api POST "$repo/git/commits" @{ message = $message; tree = $tree.sha; parents = @() }
$branch = "ci-screenshots/$env:GITHUB_REF_NAME/$Name"
try {
    Invoke-Api PATCH "$repo/git/refs/heads/$branch" @{ sha = $commit.sha; force = $true } | Out-Null
} catch {
    Invoke-Api POST "$repo/git/refs" @{ ref = "refs/heads/$branch"; sha = $commit.sha } | Out-Null
}
Write-Host ("pushed {0} ({1} KB) to {2}" -f $Dir, [int]((Get-Item $zip).Length / 1024), $branch)
