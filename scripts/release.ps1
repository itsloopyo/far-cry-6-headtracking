#!/usr/bin/env pwsh
#Requires -Version 5.1
# Fully unattended release workflow for FarCry6HeadTracking.
# Usage: pixi run release <major|minor|patch|nightly|X.Y.Z>
#
# Running this command IS the authorization. There is no second gate: the
# release runs end to end with zero prompts. The preconditions below (on main,
# clean tree, tag absent, valid semver) are the safety net in place of any
# interactive confirmation - each fails fast with a non-zero exit.

[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [string]$Version,
    [switch]$AllowDirty,
    # Ship a release even when there are no user-facing commits since the last
    # tag (writes a maintenance changelog entry instead of aborting).
    [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$modName     = 'FarCry6HeadTracking'

if (-not $Version) {
    Write-Error "Usage: pixi run release <major|minor|patch|nightly|X.Y.Z>"
    exit 1
}

if ($Version -eq 'nightly') {
    # release-nightly.ps1 never calls exit, so $LASTEXITCODE here would hold the
    # status of whatever native process Publish-NightlyBuild last spawned rather
    # than the nightly's own result. Its failures throw, so catch and report those.
    try {
        & (Join-Path $PSScriptRoot 'release-nightly.ps1') -AllowDirty:$AllowDirty
    } catch {
        # -ErrorRecord, not .Exception.Message: the record carries the script and
        # line the failure came from, and the message alone throws that away.
        Write-Error -ErrorRecord $_
        exit 1
    }
    exit 0
}

# THIRD-PARTY-NOTICES.md names the cameraunlock-core commit compiled into the
# release ZIP, and bumping the submodule does not touch it. Packaging refuses
# to ship that mismatch, so a bump with no notices edit stops the release here,
# or in CI once the tag has already been pushed. Re-sync it and let this
# release carry the correction.
& git -C $ProjectRoot diff --quiet -- THIRD-PARTY-NOTICES.md
if ($LASTEXITCODE -ne 0) {
    Write-Error "THIRD-PARTY-NOTICES.md has uncommitted edits. Commit or discard them, then re-run."
    exit 1
}
& (Join-Path $ProjectRoot 'cameraunlock-core\scripts\sync-core-notices.ps1') -Repo $ProjectRoot
if ($LASTEXITCODE -ne 0) {
    Write-Error "sync-core-notices.ps1 exited $LASTEXITCODE - fix THIRD-PARTY-NOTICES.md before releasing."
    exit 1
}
& git -C $ProjectRoot diff --quiet -- THIRD-PARTY-NOTICES.md
if ($LASTEXITCODE -ne 0) {
    & git -C $ProjectRoot commit -q -m 'chore: record the cameraunlock-core commit this build compiles' -- THIRD-PARTY-NOTICES.md
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Could not commit the re-synced THIRD-PARTY-NOTICES.md."
        exit 1
    }
    Write-Host 'THIRD-PARTY-NOTICES.md re-synced to the pinned cameraunlock-core commit.' -ForegroundColor Yellow
}

Import-Module (Join-Path $ProjectRoot 'cameraunlock-core/powershell/ReleaseWorkflow.psm1') -Force

# Mirrors New-ChangelogFromCommits' insertion so a -Force maintenance entry
# lands in the same place with the same shape.
function Add-MaintenanceChangelogEntry {
    param([string]$Path, [string]$NewVersion)
    $date = Get-Date -Format 'yyyy-MM-dd'
    $entry = "## [$NewVersion] - $date`n`n### Changed`n`n- Maintenance release (no user-facing changes).`n`n"
    $changelog = Get-Content $Path -Raw
    $changelog = $changelog -replace '(?s)(# Changelog.*?\n\n)', "`$1$entry"
    $changelog = $changelog.TrimEnd() + "`n"
    # Write-NoBom, not Set-Content: Set-Content defaults to the system ANSI
    # codepage in Windows PowerShell 5.1, and this path rewrites the WHOLE
    # changelog, so any non-ASCII already in it is mangled.
    Write-NoBom -Path $Path -Text $changelog
}

function Write-NoBom {
    param([string]$Path, [string]$Text)
    [System.IO.File]::WriteAllText($Path, $Text, (New-Object System.Text.UTF8Encoding $false))
}

# --- 1. Resolve and validate the target version ------------------------
$cmakePath = Join-Path $ProjectRoot 'CMakeLists.txt'
$cmakeText = Get-Content $cmakePath -Raw
if ($cmakeText -notmatch "project\($modName VERSION (\d+\.\d+\.\d+)") {
    Write-Error "Could not parse current version from CMakeLists.txt"
    exit 1
}
$current = $Matches[1]

try {
    $target = Resolve-ReleaseVersion -Argument $Version -CurrentVersion $current
} catch {
    Write-Error $_.Exception.Message
    exit 1
}

# --- 2. Preconditions (these stand in for interactive confirmation) ----
$branch = (git -C $ProjectRoot rev-parse --abbrev-ref HEAD).Trim()
if ($branch -ne 'main') {
    Write-Error "Releases must run on 'main' (currently on '$branch')."
    exit 1
}

if (-not $AllowDirty) {
    $status = git -C $ProjectRoot status --porcelain
    if ($status) {
        Write-Error "Working tree is not clean. Commit or stash changes before releasing."
        exit 1
    }
}

$tag = "v$target"
if (git -C $ProjectRoot tag --list $tag) {
    Write-Error "Tag $tag already exists."
    exit 1
}

Write-Host "Releasing $current -> $target" -ForegroundColor Cyan

# --- 3. Changelog from commits since the last tag ----------------------
# Runs BEFORE any version file is mutated: this is the step that aborts when
# there are no user-facing commits, and failing here leaves a clean tree
# instead of a half-applied version bump with no tag.
$changelogPath = Join-Path $ProjectRoot 'CHANGELOG.md'
Write-Host 'Generating CHANGELOG from commits...' -ForegroundColor Cyan
# Version tags only. `release nightly` moves the rolling `dev` tag to the tip
# on every publish, so an unfiltered listing reports "already released" on a
# repo that has only ever shipped dev builds, and the generator below then
# diffs an empty `dev..HEAD`.
$hasTags = git -C $ProjectRoot tag -l 'v[0-9]*' 2>$null
if (-not $hasTags) {
    if (-not (Test-Path $changelogPath)) {
        $date = Get-Date -Format 'yyyy-MM-dd'
        Write-NoBom -Path $changelogPath -Text "# Changelog`n`n## [$target] - $date`n`nFirst release.`n"
    }
} else {
    try {
        New-ChangelogFromCommits -ChangelogPath $changelogPath -Version $target -ArtifactPaths @('src/', 'cameraunlock-core', 'scripts/') | Out-Null
    } catch {
        if (-not $Force) {
            Write-Error $_.Exception.Message
            Write-Host 'No user-facing changes to release. Re-run with -Force for a maintenance release.' -ForegroundColor Yellow
            exit 1
        }
        Write-Host 'No user-facing commits since last tag - writing maintenance entry (-Force).' -ForegroundColor Yellow
        Add-MaintenanceChangelogEntry -Path $changelogPath -NewVersion $target
    }
}

# --- 4. Bump the canonical version (CMakeLists.txt) + its mirrors ------
$cmakeText = $cmakeText -replace "project\($modName VERSION \d+\.\d+\.\d+", "project($modName VERSION $target"
Write-NoBom -Path $cmakePath -Text $cmakeText

$pixiPath = Join-Path $ProjectRoot 'pixi.toml'
$pixiText = Get-Content $pixiPath -Raw
$pixiText = $pixiText -replace '(?m)^version = "\d+\.\d+\.\d+"', "version = `"$target`""
Write-NoBom -Path $pixiPath -Text $pixiText

# launcher-manifest.json is the launcher contract; keep mod_info.version in
# lockstep. mod_info.version is the only semver in the file.
$launcherManifestPath = Join-Path $ProjectRoot 'launcher-manifest.json'
$launcherManifestText = Get-Content $launcherManifestPath -Raw
$launcherManifestText = $launcherManifestText -replace '("version":\s*")\d+\.\d+\.\d+(")', "`${1}$target`$2"
Write-NoBom -Path $launcherManifestPath -Text $launcherManifestText

# MOD_VERSION in install.cmd is stamped into .headtracking-state.json at user
# install time, so it has to track the release too. Read and write the raw
# bytes: install.cmd is CRLF and a .cmd that loses that silently fails on
# Windows, which Get-Content / Set-Content would do here.
$installCmdPath = Join-Path $ProjectRoot 'scripts\install.cmd'
$installCmdText = [System.IO.File]::ReadAllText($installCmdPath)
$installCmdText = $installCmdText -replace '(set "MOD_VERSION=)\d+\.\d+\.\d+(")', "`${1}$target`$2"
[System.IO.File]::WriteAllText($installCmdPath, $installCmdText, (New-Object System.Text.UTF8Encoding $false))

# --- 5. Release-config build -------------------------------------------
# `build` is the Debug configuration in this repo; the release build is
# `build-release`, which is also what `package` depends on.
Write-Host 'Building release configuration...' -ForegroundColor Cyan
pixi run build-release
if ($LASTEXITCODE -ne 0) {
    Write-Error 'Release build failed.'
    exit 1
}

# --- 6. Commit the version bump + changelog ----------------------------
git -C $ProjectRoot add CMakeLists.txt pixi.toml CHANGELOG.md launcher-manifest.json scripts/install.cmd
git -C $ProjectRoot commit -m "Release v$target"
if ($LASTEXITCODE -ne 0) { Write-Error 'git commit failed.'; exit 1 }

# --- 7. Annotated tag --------------------------------------------------
git -C $ProjectRoot tag -a $tag -m "Release v$target"
if ($LASTEXITCODE -ne 0) { Write-Error 'git tag failed.'; exit 1 }

# --- 8. Push commits + tag (triggers .github/workflows/release.yml) ----
git -C $ProjectRoot push origin HEAD
if ($LASTEXITCODE -ne 0) { Write-Error 'git push (commits) failed.'; exit 1 }
git -C $ProjectRoot push origin $tag
if ($LASTEXITCODE -ne 0) { Write-Error 'git push (tag) failed.'; exit 1 }

Write-Host "Released $tag" -ForegroundColor Green
