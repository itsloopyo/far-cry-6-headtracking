#!/usr/bin/env pwsh
#Requires -Version 5.1
# Packaging for Far Cry 6 Head Tracking. Produces ONE ZIP:
#   FarCry6HeadTracking-v{version}-installer.zip
#
# There is deliberately no -nexus.zip stage, and adding one back would be a bug.
# The mod has to replace tobii_gameintegration_x64.dll inside the game's own bin
# folder. A mod manager deploys into one fixed subtree and cannot replace a file
# the game ships, nor put the original back, so there is no manager route to this
# game and no Nexus page. Publish-NightlyBuild must be passed -NoNexusZip.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference    = 'SilentlyContinue'

$scriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir = Split-Path -Parent $scriptDir

Import-Module (Join-Path $projectDir 'cameraunlock-core/powershell/ReleaseWorkflow.psm1') -Force

$cmakeLists = Get-Content (Join-Path $projectDir 'CMakeLists.txt') -Raw
if ($cmakeLists -notmatch 'project\(FarCry6HeadTracking VERSION (\d+\.\d+\.\d+)') {
    throw "Could not parse version from CMakeLists.txt"
}
$version = $Matches[1]
$modName = 'FarCry6HeadTracking'
$payload = 'tobii_gameintegration_x64.dll'
$gameId  = 'far-cry-6'

# CMakeLists.txt is the canonical version, and release.ps1 stamps three mirrors from
# it. Nothing verified they agreed, so a hand-edited CMake version shipped an
# installer whose state file recorded a version the user never installed.
$pixiText = Get-Content (Join-Path $projectDir 'pixi.toml') -Raw
if ($pixiText -notmatch '(?m)^version = "(\d+\.\d+\.\d+)"') {
    throw "Could not parse version from pixi.toml"
}
if ($Matches[1] -ne $version) {
    throw "pixi.toml version $($Matches[1]) does not match CMakeLists.txt $version. Run 'pixi run release' rather than editing versions by hand."
}
$installCmdText = [System.IO.File]::ReadAllText((Join-Path $projectDir 'scripts\install.cmd'))
if ($installCmdText -notmatch 'set "MOD_VERSION=(\d+\.\d+\.\d+)"') {
    throw "Could not parse MOD_VERSION from scripts/install.cmd"
}
if ($Matches[1] -ne $version) {
    throw "install.cmd MOD_VERSION $($Matches[1]) does not match CMakeLists.txt $version. Run 'pixi run release' rather than editing versions by hand."
}

Write-Host ''
Write-Host "=== Packaging $modName v$version ===" -ForegroundColor Magenta
Write-Host ''

$releaseDir = Join-Path $projectDir 'release'
if (-not (Test-Path $releaseDir)) { New-Item -ItemType Directory -Path $releaseDir -Force | Out-Null }

$dllPath = Join-Path $projectDir "bin/Release/$payload"
if (-not (Test-Path $dllPath)) {
    throw "$payload not found at: $dllPath. Run 'pixi run build-release' first."
}

$scriptsDir = Join-Path $projectDir 'scripts'
foreach ($s in @('install.cmd', 'uninstall.cmd')) {
    if (-not (Test-Path (Join-Path $scriptsDir $s))) {
        throw "Required script not found: $s"
    }
}

$launcherManifestPath = Join-Path $projectDir 'launcher-manifest.json'
if (-not (Test-Path $launcherManifestPath)) {
    throw "launcher-manifest.json not found at: $launcherManifestPath"
}

# The ZIP is a binary distribution of MinHook (BSD-2-Clause), which requires the
# notice to accompany the binary. Throw rather than skip: a silent skip turns a
# licence violation into a green build.
$requiredDocs = @('README.md', 'LICENSE', 'CHANGELOG.md', 'THIRD-PARTY-NOTICES.md')
foreach ($doc in $requiredDocs) {
    if (-not (Test-Path (Join-Path $projectDir $doc))) {
        throw "Required document not found: $doc"
    }
}

Write-Host '--- Installer ZIP ---' -ForegroundColor Yellow

$staging = Join-Path $releaseDir 'staging-installer'
if (Test-Path $staging) { Remove-Item -Recurse -Force $staging }
New-Item -ItemType Directory -Path $staging -Force | Out-Null

foreach ($s in @('install.cmd', 'uninstall.cmd')) {
    Copy-Item (Join-Path $scriptsDir $s) -Destination $staging -Force
}

# install.cmd / uninstall.cmd are thin wrappers; their bodies, find-game.ps1 and
# games.json come from cameraunlock-core and have to travel with them or the ZIP
# cannot resolve the game.
Copy-SharedBundle -StagingDir $staging

# install.cmd and uninstall.cmd resolve the game through this file, so a build whose
# staged copy has no entry for this game produces an installer that cannot find the
# game on any machine. It is invisible locally, because the entry can exist as an
# uncommitted edit in the submodule while CI checks out the committed one.
$stagedGames = Join-Path $staging 'shared/games.json'
if (-not (Test-Path $stagedGames)) {
    throw "shared/games.json was not staged, so the installer cannot resolve the game."
}
# Parsed, not substring-matched: the file also carries the repo slug
# "itsloopyo/far-cry-6-headtracking", and a bare text search would accept that as
# the game entry.
#
# Looked up through PSObject.Properties rather than dereferenced. Set-StrictMode
# makes `$json.games.$gameId` a TERMINATING error when the key is absent, which is
# the very case these throws are written for, so the messages below would never be
# reached and the maintainer would get a bare property error instead.
$stagedGamesJson = (Get-Content $stagedGames -Raw).TrimStart([char]0xFEFF) | ConvertFrom-Json
$gameProperty = $stagedGamesJson.games.PSObject.Properties[$gameId]
if (-not $gameProperty) {
    throw "The staged shared/games.json has no '$gameId' entry, so install.cmd and uninstall.cmd would fail to find the game on every machine. Commit the entry in cameraunlock-core and bump the submodule."
}
if (-not $gameProperty.Value.PSObject.Properties['executable_relpath']) {
    throw "The staged shared/games.json entry for '$gameId' has no executable_relpath, so the installer cannot locate the game folder."
}

$pluginsDir = Join-Path $staging 'plugins'
New-Item -ItemType Directory -Path $pluginsDir -Force | Out-Null
Copy-Item $dllPath -Destination $pluginsDir -Force

foreach ($doc in $requiredDocs) {
    Copy-Item (Join-Path $projectDir $doc) -Destination $staging -Force
}

# Stamp mod_info.version from the build so the shipped manifest can never
# disagree with the built DLL.
$stagedManifest = Join-Path $staging 'launcher-manifest.json'
$manifestText = Get-Content $launcherManifestPath -Raw
$manifestText = $manifestText -replace '("version":\s*")\d+\.\d+\.\d+(")', "`${1}$version`$2"
[System.IO.File]::WriteAllText($stagedManifest, $manifestText, (New-Object System.Text.UTF8Encoding $false))
Write-Host "  launcher-manifest.json (version $version)" -ForegroundColor Green

$installerZip = Join-Path $releaseDir "$modName-v$version-installer.zip"
if (Test-Path $installerZip) { Remove-Item $installerZip -Force }
Push-Location $staging
try { Compress-Archive -Path '.\*' -DestinationPath $installerZip -Force } finally { Pop-Location }
Remove-Item -Recurse -Force $staging

$installerKb = [math]::Round((Get-Item $installerZip).Length / 1KB, 1)
Write-Host ("  $installerZip ({0:N1} KB)" -f $installerKb) -ForegroundColor Green
Write-Host ''
Write-Host 'Done.' -ForegroundColor Magenta
