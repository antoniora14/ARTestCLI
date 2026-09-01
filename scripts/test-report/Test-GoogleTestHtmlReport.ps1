[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

function Assert-ReportCondition([bool]$condition, [string]$message) {
    if (-not $condition) {
        throw "HTML report generator regression failure: $message"
    }
}

$generatorPath = Join-Path $PSScriptRoot 'New-GoogleTestHtmlReport.ps1'
$temporaryDirectory = Join-Path ([System.IO.Path]::GetTempPath()) ('ARTestCLI.ReportTests.' + [guid]::NewGuid().ToString('N'))
[System.IO.Directory]::CreateDirectory($temporaryDirectory) | Out-Null

try {
    $mixedXmlPath = Join-Path $temporaryDirectory 'mixed-results.xml'
    $mixedHtmlPath = Join-Path $temporaryDirectory 'mixed-results.html'
    $mixedXml = @'
<?xml version="1.0" encoding="UTF-8"?>
<testsuites tests="3" failures="1" disabled="0" errors="0" time="0.006">
  <testsuite name="SyntheticSuite" tests="3" failures="1" disabled="0" errors="0" time="0.006">
    <testcase name="Passes" status="run" result="completed" time="0.001" />
    <testcase name="Fails" status="run" result="completed" time="0.002">
      <failure message="Expected value was false">Synthetic failure detail</failure>
    </testcase>
    <testcase name="IsSkipped" status="run" result="skipped" time="0.003">
      <skipped message="Not applicable" />
    </testcase>
  </testsuite>
</testsuites>
'@
    [System.IO.File]::WriteAllText(
        $mixedXmlPath,
        $mixedXml,
        [System.Text.UTF8Encoding]::new($false))

    & $generatorPath -XmlPath $mixedXmlPath -HtmlPath $mixedHtmlPath -Configuration Regression -Platform Synthetic | Out-Null

    $html = [System.IO.File]::ReadAllText($mixedHtmlPath)
    Assert-ReportCondition ($html.Contains('Overall<strong class="failed">FAILED</strong>')) 'the overall verdict must be FAILED when any test case fails.'
    Assert-ReportCondition (([regex]::Matches($html, 'class="passed">PASSED</td>')).Count -eq 1) 'exactly one PASSED row must be present.'
    Assert-ReportCondition (([regex]::Matches($html, 'class="failed">FAILED</td>')).Count -eq 1) 'exactly one FAILED row must be present.'
    Assert-ReportCondition (([regex]::Matches($html, 'class="skipped">SKIPPED</td>')).Count -eq 1) 'exactly one SKIPPED row must be present.'
    Assert-ReportCondition ($html.Contains('Synthetic failure detail')) 'the failure details must be preserved.'

    $inconsistentXmlPath = Join-Path $temporaryDirectory 'inconsistent-results.xml'
    $inconsistentHtmlPath = Join-Path $temporaryDirectory 'inconsistent-results.html'
    $inconsistentXml = $mixedXml.Replace(
        '<testsuites tests="3" failures="1"',
        '<testsuites tests="3" failures="0"')
    [System.IO.File]::WriteAllText(
        $inconsistentXmlPath,
        $inconsistentXml,
        [System.Text.UTF8Encoding]::new($false))

    $inconsistencyRejected = $false
    try {
        & $generatorPath -XmlPath $inconsistentXmlPath -HtmlPath $inconsistentHtmlPath -Configuration Regression -Platform Synthetic | Out-Null
    }
    catch {
        $inconsistencyRejected = $_.Exception.Message -like '*Inconsistent*'
    }

    Assert-ReportCondition $inconsistencyRejected 'a summary that contradicts the test cases must stop report generation.'
    Write-Host 'HTML report generator validation: PASSED (pass/fail/skip and inconsistency detection).'
}
finally {
    if ([System.IO.Directory]::Exists($temporaryDirectory)) {
        [System.IO.Directory]::Delete($temporaryDirectory, $true)
    }
}
