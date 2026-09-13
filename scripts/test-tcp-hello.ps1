param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Release',
    [switch]$IncludePython,
    [string]$PythonRoot = "$PSScriptRoot\..\artifacts\python-c02",
    [string]$OutputDirectory = ''
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
if (!$OutputDirectory) {
    $OutputDirectory = "$repo\artifacts\acceptance\c02\$Configuration-" + [guid]::NewGuid().ToString('N')
}
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Choose a new evidence directory; prior evidence is immutable.' }
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path "$OutputDirectory\servers" -Force | Out-Null
$oldEvidence = $env:ARTEST_C02_EVIDENCE_ROOT
$oldPython = $env:ARTEST_TCP_PYTHON_ROOT
try {
    $env:ARTEST_C02_EVIDENCE_ROOT = "$OutputDirectory\servers"
    $env:ARTEST_TCP_PYTHON_ROOT = [IO.Path]::GetFullPath($PythonRoot)
    $arguments = @('--gtest_filter=TcpHelloTests.*', "--gtest_output=xml:$OutputDirectory\tests.xml")
    if ($IncludePython) {
        $arguments[0] = '--gtest_filter=TcpHelloTests.*:DISABLED_TcpHelloPythonTests.*'
        $arguments += '--gtest_also_run_disabled_tests'
    }
    & "$repo\artifacts\bin\x64\$Configuration\ARTestCLI.UnitTests.exe" @arguments | Tee-Object -FilePath "$OutputDirectory\tests.log"
    $testExit = $LASTEXITCODE
}
finally {
    $env:ARTEST_C02_EVIDENCE_ROOT = $oldEvidence
    $env:ARTEST_TCP_PYTHON_ROOT = $oldPython
}
[xml]$result = Get-Content -LiteralPath "$OutputDirectory\tests.xml" -Raw
$expected = if ($IncludePython) { 20 } else { 16 }
$cases = @($result.testsuites.testsuite | ForEach-Object { $_.testcase })
if ($testExit -ne 0 -or [int]$result.testsuites.tests -ne $expected -or
    [int]$result.testsuites.failures -ne 0 -or [int]$result.testsuites.errors -ne 0 -or
    @($cases | Where-Object { $_.status -ne 'run' -or $_.result -ne 'completed' }).Count -ne 0) {
    throw "C-02 acceptance failed/incomplete: $OutputDirectory"
}
& "$PSScriptRoot\test-report\New-GoogleTestHtmlReport.ps1" -XmlPath "$OutputDirectory\tests.xml" -HtmlPath "$OutputDirectory\tests.html" -Configuration $Configuration -Platform x64
$journals = @(Get-ChildItem -LiteralPath "$OutputDirectory\servers" -Filter journal.jsonl -Recurse -File)
if ($journals.Count -lt 20) { throw 'Independent server evidence is missing.' }
Write-Host "C-02 focused gate: $expected passed. Evidence: $OutputDirectory"
