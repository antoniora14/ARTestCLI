param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Debug',
    [string]$PythonRoot = "$PSScriptRoot\..\artifacts\python"
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$workerPython = Join-Path $PythonRoot 'environments\simulated\Scripts\python.exe'
if (-not (Test-Path -LiteralPath $workerPython)) { throw 'Prepare the isolated Python example first.' }
& $workerPython -I -B "$repo\source\ARTest.Python\tests\test_worker.py" -v
if ($LASTEXITCODE -ne 0) { throw 'Installed Python worker contract tests failed.' }
$exe = Join-Path $repo "artifacts\bin\x64\$Configuration\ARTestCLI.UnitTests.exe"
$results = Join-Path $repo "artifacts\test-results\x64\$Configuration"
New-Item -ItemType Directory -Force -Path $results | Out-Null
$previousRoot = $env:ARTEST_PYTHON_ROOT
try {
    $env:ARTEST_PYTHON_ROOT = (Resolve-Path -LiteralPath $PythonRoot).Path
    & $exe '--gtest_also_run_disabled_tests' '--gtest_filter=DISABLED_PythonIntegrationTests.*' "--gtest_output=xml:$results\ARTestPython.Integration.xml"
    $testExit = $LASTEXITCODE
}
finally { $env:ARTEST_PYTHON_ROOT = $previousRoot }
& "$PSScriptRoot\test-report\New-GoogleTestHtmlReport.ps1" -XmlPath "$results\ARTestPython.Integration.xml" -HtmlPath "$results\ARTestPython.Integration.html" -Configuration $Configuration -Platform x64
if ($testExit -ne 0) { throw "Python integration failed: $testExit" }
[xml]$report = Get-Content -LiteralPath "$results\ARTestPython.Integration.xml" -Raw
if ([int]$report.testsuites.tests -lt 27 -or [int]$report.testsuites.failures -ne 0 -or [int]$report.testsuites.errors -ne 0) {
    throw 'Python integration evidence is incomplete or failed.'
}
