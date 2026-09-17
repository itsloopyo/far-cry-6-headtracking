#!/usr/bin/env pwsh
#Requires -Version 5.1
# Publishes a dev build to the rolling `dev` pre-release. Unattended: the
# invocation is the authorization, and every precondition Publish-NightlyBuild
# checks fails fast rather than asking.

[CmdletBinding()]
param([switch]$AllowDirty)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

Import-Module (Join-Path $ProjectRoot 'cameraunlock-core\powershell\ReleaseWorkflow.psm1') -Force
Import-Module (Join-Path $ProjectRoot 'cameraunlock-core\powershell\NightlyRelease.psm1') -Force

# CMakeLists.txt is the canonical version source; package-release.ps1 names the
# installer ZIP from it, and Publish-NightlyBuild derives that path.
$version = Get-ProjectVersion -Source 'cmake' -Path (Join-Path $ProjectRoot 'CMakeLists.txt')

# -NoNexusZip is mandatory here, not a preference. The mod replaces a DLL the
# game ships inside its own bin folder, which no mod manager can deploy or roll
# back, so package-release.ps1 produces no Nexus ZIP and the default treatment
# of a missing one as fatal would fail every dev build.
Publish-NightlyBuild `
    -ModId 'far-cry-6' `
    -ModName 'FarCry6HeadTracking' `
    -Version $version `
    -ProjectRoot $ProjectRoot `
    -NoNexusZip `
    -AllowDirty:$AllowDirty
