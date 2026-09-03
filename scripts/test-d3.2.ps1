[CmdletBinding()]
param([ValidateSet('Debug', 'Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$testExecutable = Join-Path $repositoryRoot "artifacts\bin\x64\$Configuration\ARTestCLI.UnitTests.exe"
if (-not (Test-Path -LiteralPath $testExecutable -PathType Leaf)) {
    throw 'Build the selected configuration with scripts/build.ps1 before this focused run.'
}
# Intentionally no --gtest_output: preserve the complete baseline XML/HTML reports.
& $testExecutable '--gtest_filter=OfflineCompilationTests.*:PreparedPlanTests.*:RegistryTransactionTests.*:StageD32AbiTests.*'
if ($LASTEXITCODE -ne 0) { throw "D3.2 focused regression failed: exit $LASTEXITCODE." }
