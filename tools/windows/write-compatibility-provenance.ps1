# SPDX-License-Identifier: GPL-3.0-or-later
param(
    [Parameter(Mandatory)][ValidateSet('provisional', 'validated')][string]$Stage,
    [string]$BinDirectory = 'build/bin',
    [string]$ToolchainFile = 'build/compatibility-toolchain.json',
    [string]$TestReport = 'build/compatibility-tests.xml'
)
$ErrorActionPreference = 'Stop'
$sourceRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$sourceCommit = (& git -C $sourceRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Cannot identify source commit' }
if ($env:GITHUB_SHA -and $env:GITHUB_SHA -ne $sourceCommit) { throw 'Checkout does not match workflow source SHA' }
$manifestFile = Join-Path $BinDirectory 'COMPATIBILITY_PROVENANCE.json'
$previous = if (Test-Path -LiteralPath $manifestFile) { Get-Content -LiteralPath $manifestFile -Raw | ConvertFrom-Json } else { $null }
$tests = $null
if ($Stage -eq 'validated') {
    if (-not $previous -or $previous.validation_state -ne 'provisional' -or $previous.source_commit -ne $sourceCommit) {
        throw 'Validated package requires a provisional manifest from this source commit'
    }
    if (-not (Test-Path -LiteralPath $TestReport)) { throw 'Full CTest JUnit report is missing' }
    $created = [DateTime]$previous.created_utc
    if ((Get-Item -LiteralPath $TestReport).LastWriteTimeUtc -lt $created.ToUniversalTime()) {
        throw 'CTest report predates the provisional package'
    }
    [xml]$junit = Get-Content -LiteralPath $TestReport -Raw
    $tests = [ordered]@{total = 0; skipped = 0; failures = 0; errors = 0}
    foreach ($suite in $junit.SelectNodes("//*[local-name()='testsuite' and not(*[local-name()='testsuite'])]")) {
        foreach ($key in @('total', 'skipped', 'failures', 'errors')) {
            $attribute = if ($key -eq 'total') { 'tests' } else { $key }
            $value = $suite.GetAttribute($attribute)
            if ($value) { $tests[$key] += [int]$value }
        }
    }
    if ($tests.total - $tests.skipped -le 0 -or $tests.failures -ne 0 -or $tests.errors -ne 0) {
        throw 'Full CTest report must contain executed tests and zero failures/errors'
    }
}
$hashes = [ordered]@{}
$changed = [ordered]@{}
foreach ($name in @('lichii.exe', 'eden-cli.exe', 'shader-sweep.exe', 'spirv-val.exe', 'nxformat.dll')) {
    $path = Join-Path $BinDirectory $name
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Required runtime file missing: $name" }
    $hashes[$name] = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($Stage -eq 'validated') { $changed[$name] = $hashes[$name] -ne $previous.runtime_sha256.$name }
}
$manifest = [ordered]@{
    schema_version = 1
    validation_state = $Stage
    gameplay_state = 'unverified'
    source_commit = $sourceCommit
    source_branch = $env:GITHUB_REF_NAME
    run_id = $env:GITHUB_RUN_ID
    run_attempt = $env:GITHUB_RUN_ATTEMPT
    created_utc = (Get-Date).ToUniversalTime().ToString('o')
    toolchain = (Get-Content -LiteralPath $ToolchainFile -Raw | ConvertFrom-Json)
    runtime_sha256 = $hashes
    ctest = $tests
}
if ($Stage -eq 'validated') {
    $manifest.provisional_runtime_sha256 = $previous.runtime_sha256
    $manifest.changed_since_provisional = $changed
}
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifestFile -Encoding utf8
$sourceCommit | Set-Content -LiteralPath (Join-Path $BinDirectory 'SOURCE_COMMIT.txt') -Encoding utf8
$state = if ($Stage -eq 'provisional') { 'PROVISIONAL: full milestone checks pending' } else { 'VALIDATED: full build, import check and CTest passed' }
"$state; gameplay remains a separate verification gate." |
    Set-Content -LiteralPath (Join-Path $BinDirectory 'COMPATIBILITY_BUILD.txt') -Encoding utf8
