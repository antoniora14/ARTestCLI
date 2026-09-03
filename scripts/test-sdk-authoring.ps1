[CmdletBinding()]
param([ValidateSet('Debug', 'Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$testExecutable = Join-Path $repositoryRoot "artifacts\bin\x64\$Configuration\ARTestCLI.UnitTests.exe"
if (-not (Test-Path -LiteralPath $testExecutable -PathType Leaf)) {
    throw 'Build the selected configuration with scripts/build.ps1 first.'
}
# Preserve full-baseline XML/HTML evidence when running this focused subset.
& $testExecutable '--gtest_filter=Sdk*'
if ($LASTEXITCODE -ne 0) { throw "SDK authoring regression failed: exit $LASTEXITCODE." }
