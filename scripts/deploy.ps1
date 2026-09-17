#!/usr/bin/env pwsh
#Requires -Version 5.1
# Deploys the built mod into the game, keeping the shipped head tracking DLL beside
# it so uninstall can put it back.
#
# Usage: deploy.ps1 [GAME_PATH] [-Configuration Debug|Release]
# Game detection order matches install.cmd: explicit path -> FAR_CRY_6_PATH env
# var -> store lookups -> games.json, all of it through core's Find-GamePath.
#
# The game ships two copies of its binaries, bin\ and bin_plus\, and which one the
# launcher starts depends on the edition and installed content. Both are deployed so
# the mod is live whichever one runs. The shipped installer deploys only to the
# folder holding the executable games.json names; this is the developer path.
#
# The backup name matches the one install.cmd uses, so a dev deploy and a real
# install cannot leave two different backups of the same file.
param(
    [Parameter(Position = 0)]
    [string]$GamePath,
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repo = Split-Path -Parent $PSScriptRoot
$built = Join-Path $repo "bin\$Configuration\tobii_gameintegration_x64.dll"
if (-not (Test-Path $built)) {
    Write-Error "Built DLL not found at $built. Run 'pixi run build-release' first."
    exit 1
}

if (-not $GamePath) {
    Import-Module (Join-Path $repo 'cameraunlock-core/powershell/GamePathDetection.psm1') -Force
    $GamePath = Find-GamePath -GameId 'far-cry-6'
}

if (-not $GamePath -or -not (Test-Path $GamePath)) {
    Write-Error "Could not locate Far Cry 6. Set FAR_CRY_6_PATH or pass the install path as the first argument."
    exit 1
}

# The @() wraps the RESULT, not the source list. Under Set-StrictMode a pipeline
# that yields nothing gives $null and one that yields a single item gives a bare
# string, and .Count throws a property error on BOTH - so an ordinary install with
# a bin\ and no bin_plus\ threw instead of deploying. Do not simplify this out.
$targets = @(@("bin", "bin_plus") | ForEach-Object { Join-Path $GamePath $_ } |
    Where-Object { Test-Path $_ })
if ($targets.Count -eq 0) {
    Write-Error "No bin or bin_plus directory under $GamePath."
    exit 1
}

foreach ($dir in $targets) {
    $live = Join-Path $dir "tobii_gameintegration_x64.dll"
    $backup = Join-Path $dir "tobii_gameintegration_x64.dll.backup"

    # Back up once, and only the genuine shipped DLL. "No backup yet" is not enough
    # on its own: a deploy that ran under an older backup name leaves the MOD sitting
    # at $live, and backing that up records the mod as the game's original and leaves
    # nothing to restore.
    #
    # The test is "is this file ours", not "is this file the payload". Comparing
    # bytes against the DLL being deployed only recognises the IDENTICAL build, so
    # an EARLIER build of the mod sitting at $live compares as different and gets
    # filed away as the game's original, which is the exact loss this guard exists
    # to prevent.
    #
    # The tracker URL the mod answers the game with is compiled into every build of
    # it and cannot appear in Tobii's own library, so it identifies our DLL at any
    # version. Read as bytes rather than through the version resource: this project
    # ships no .rc, so VersionInfo is empty and would never match.
    #
    # This is deliberately STRICTER than the shipped installer, which byte-compares
    # the live file against the payload (cameraunlock-core's install-body-shim.cmd).
    # That test only recognises the identical build, which is enough there because
    # this game always ships the DLL so a backup exists after the first install.
    # The dev path has no such guarantee: it runs against a tree that is rebuilt
    # between deploys.
    if ((Test-Path $live) -and -not (Test-Path $backup)) {
        $marker = 'opentrack://far-cry-6-headtracking'
        $liveText = [System.Text.Encoding]::ASCII.GetString(
            [System.IO.File]::ReadAllBytes($live))
        $isOurs = $liveText.Contains($marker)
        if ($isOurs) {
            Write-Host "$live is already this mod, so there is no original to back up." -ForegroundColor Yellow
        } else {
            Move-Item $live $backup
            Write-Host "Backed up shipped DLL to $backup" -ForegroundColor Yellow
        }
    }

    Copy-Item $built $live -Force
    Write-Host "Deployed to $live" -ForegroundColor Green
}
