$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
Push-Location $repo
try {
    # No `| Out-Null` and an explicit exit check on each cmake call. Discarding the
    # output hid the compiler diagnostics, and $ErrorActionPreference does not apply
    # to a native exit code, so a failed configure or compile fell straight through
    # to the exe below. On any machine with a warm build-tests\ that ran the
    # PREVIOUS binary and reported green against source that no longer builds.
    cmake -B build-tests -A x64 -DFARCRY6_BUILD_TESTS=ON
    if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }
    cmake --build build-tests --config Release --target farcry6_tests farcry6_config_differential
    if ($LASTEXITCODE -ne 0) { throw "cmake build failed" }
    & "build-tests\tests\Release\farcry6_tests.exe"
    if ($LASTEXITCODE -ne 0) { throw "unit tests failed" }
    & "build-tests\tests\Release\farcry6_config_differential.exe"
    if ($LASTEXITCODE -ne 0) { throw "config differential test failed" }
    & "$repo\tests\config_differential\check-frozen.ps1"
} finally {
    Pop-Location
}
