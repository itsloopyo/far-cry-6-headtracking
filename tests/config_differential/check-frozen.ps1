# Fails when a file frozen.tsv records has changed: the frozen legacy reader, or a core
# source it compiles. Each one is what an old FarCry6HeadTracking.ini is read through, so a
# change moves what a player's old file converts to.
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$record = Join-Path $PSScriptRoot 'frozen.tsv'
$changed = @()
foreach ($line in [System.IO.File]::ReadAllLines($record)) {
    if ($line -eq '' -or $line.StartsWith('#')) { continue }
    $path, $sha = $line.Split("`t")[0, 1]
    if ($path.Contains(':')) { continue }
    $actual = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $repo $path)).Hash.ToLowerInvariant()
    if ($actual -ne $sha) { $changed += "$path is $actual, frozen.tsv records $sha" }
}
if ($changed.Count -gt 0) {
    throw "Frozen config reader sources changed:`n  $($changed -join "`n  ")"
}
Write-Host 'frozen config reader: unchanged'
