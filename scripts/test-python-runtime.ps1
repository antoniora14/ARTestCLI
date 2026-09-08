param([ValidateSet('Debug','Release')][string]$Configuration = 'Debug')
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$exe = Join-Path $repo "artifacts\bin\x64\$Configuration\ARTestCLI.UnitTests.exe"
$results = Join-Path $repo "artifacts\test-results\x64\$Configuration"
New-Item -ItemType Directory -Force -Path $results | Out-Null
& $exe '--gtest_also_run_disabled_tests' '--gtest_filter=DISABLED_PythonIntegrationTests.*' "--gtest_output=xml:$results\ARTestPython.Integration.xml"
$testExit = $LASTEXITCODE
& "$PSScriptRoot\test-report\New-GoogleTestHtmlReport.ps1" -XmlPath "$results\ARTestPython.Integration.xml" -HtmlPath "$results\ARTestPython.Integration.html" -Configuration $Configuration -Platform x64
if ($testExit -ne 0) { throw "Python integration failed: $testExit" }
[xml]$report = Get-Content -LiteralPath "$results\ARTestPython.Integration.xml" -Raw
if ([int]$report.testsuites.tests -lt 17 -or [int]$report.testsuites.failures -ne 0 -or [int]$report.testsuites.errors -ne 0) {
    throw 'Python integration evidence is incomplete or failed.'
}
