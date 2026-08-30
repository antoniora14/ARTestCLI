[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$XmlPath,

    [Parameter(Mandatory)]
    [string]$HtmlPath,

    [string]$Configuration = '',

    [string]$Platform = ''
)

$ErrorActionPreference = 'Stop'

$resolvedXmlPath = (Resolve-Path -LiteralPath $XmlPath).Path
[xml]$document = [System.IO.File]::ReadAllText($resolvedXmlPath)
$root = $document.testsuites
if ($null -eq $root) {
    throw "El archivo no contiene un reporte Google Test valido: $resolvedXmlPath"
}

function Encode-Html([object]$value) {
    return [System.Net.WebUtility]::HtmlEncode([string]$value)
}

$testResults = foreach ($suite in @($root.testsuite)) {
    foreach ($testCase in @($suite.testcase)) {
        $failureNodes = @($testCase.SelectNodes('./failure'))
        $errorNodes = @($testCase.SelectNodes('./error'))
        $failedNodes = @($failureNodes + $errorNodes)
        $skippedNodes = @($testCase.SelectNodes('./skipped'))
        $caseStatus = [string]$testCase.status
        $caseResult = [string]$testCase.result

        $status = if ($failedNodes.Count -gt 0) {
            'FAILED'
        }
        elseif (
            $skippedNodes.Count -gt 0 -or
            $caseStatus -eq 'notrun' -or
            $caseResult -in @('skipped', 'suppressed')) {
            'SKIPPED'
        }
        elseif ($caseStatus -eq 'run' -and $caseResult -in @('', 'completed')) {
            'PASSED'
        }
        else {
            throw "Veredicto Google Test desconocido para '$($suite.name).$($testCase.name)': status='$caseStatus', result='$caseResult'."
        }

        $details = if ($failedNodes.Count -gt 0) {
            ($failedNodes | ForEach-Object {
                $message = [string]$_.message
                $body = [string]$_.InnerText
                (($message, $body) | Where-Object { $_ }) -join [Environment]::NewLine
            }) -join [Environment]::NewLine
        }
        elseif ($skippedNodes.Count -gt 0) {
            ($skippedNodes | ForEach-Object { [string]$_.message }) -join [Environment]::NewLine
        }
        else {
            ''
        }

        [pscustomobject]@{
            Suite = [string]$suite.name
            Name = [string]$testCase.name
            Status = $status
            Time = [string]$testCase.time
            Details = $details
        }
    }
}

$reportedTests = [int]$root.tests
$reportedFailures = [int]$root.failures
$reportedErrors = if ($root.errors) { [int]$root.errors } else { 0 }
$tests = @($testResults).Count
$failures = @($testResults | Where-Object Status -eq 'FAILED').Count
$skipped = @($testResults | Where-Object Status -eq 'SKIPPED').Count
$passed = @($testResults | Where-Object Status -eq 'PASSED').Count
$reportedFailedTotal = $reportedFailures + $reportedErrors

if ($tests -ne $reportedTests) {
    throw "Reporte Google Test inconsistente: el XML declara $reportedTests casos, pero contiene $tests."
}
if ($failures -ne $reportedFailedTotal) {
    throw "Reporte Google Test inconsistente: el XML declara $reportedFailedTotal fallos/errores, pero los casos contienen $failures."
}
if (($passed + $failures + $skipped) -ne $tests) {
    throw "Reporte Google Test inconsistente: no fue posible asignar un veredicto unico a cada caso."
}

$rows = foreach ($testResult in $testResults) {
    $statusClass = $testResult.Status.ToLowerInvariant()
    '<tr>' +
        '<td>' + (Encode-Html $testResult.Suite) + '</td>' +
        '<td>' + (Encode-Html $testResult.Name) + '</td>' +
        '<td class="' + $statusClass + '">' + $testResult.Status + '</td>' +
        '<td>' + (Encode-Html $testResult.Time) + ' s</td>' +
        '<td><pre>' + (Encode-Html $testResult.Details) + '</pre></td>' +
    '</tr>'
}

$overallStatus = if ($failures -eq 0) { 'PASSED' } else { 'FAILED' }
$overallClass = $overallStatus.ToLowerInvariant()
$generatedAt = Get-Date -Format 'yyyy-MM-dd HH:mm:ss K'
$titleContext = (($Configuration, $Platform) | Where-Object { $_ }) -join ' / '
if (-not $titleContext) {
    $titleContext = 'Local'
}

$html = @"
<!doctype html>
<html lang="es">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ARTestCLI - Google Test Report</title>
  <style>
    :root { color-scheme: light; font-family: "Segoe UI", Arial, sans-serif; }
    body { margin: 0; background: #f4f6f8; color: #1d2733; }
    main { max-width: 1180px; margin: 32px auto; padding: 0 24px 40px; }
    h1 { margin-bottom: 4px; }
    .meta { color: #5d6b79; margin-top: 0; }
    .summary { display: grid; grid-template-columns: repeat(6, minmax(110px, 1fr)); gap: 12px; margin: 24px 0; }
    .card { background: white; border-radius: 8px; padding: 16px; box-shadow: 0 1px 4px #00000018; }
    .card strong { display: block; font-size: 1.6rem; margin-top: 6px; }
    table { width: 100%; border-collapse: collapse; background: white; box-shadow: 0 1px 4px #00000018; }
    th, td { padding: 11px 12px; border-bottom: 1px solid #dce2e8; text-align: left; vertical-align: top; }
    th { background: #26384a; color: white; position: sticky; top: 0; }
    tr:hover td { background: #f7f9fb; }
    .passed { color: #087a37; font-weight: 700; }
    .failed { color: #b42318; font-weight: 700; }
    .skipped { color: #9a6700; font-weight: 700; }
    pre { white-space: pre-wrap; margin: 0; font: inherit; }
    @media (max-width: 760px) {
      .summary { grid-template-columns: repeat(2, 1fr); }
      table { display: block; overflow-x: auto; }
    }
  </style>
</head>
<body>
<main>
  <h1>ARTestCLI - Automated Test Report</h1>
  <p class="meta">Google Test | $titleContext | Generated: $generatedAt</p>
  <section class="summary">
    <div class="card">Overall<strong class="$overallClass">$overallStatus</strong></div>
    <div class="card">Total<strong>$tests</strong></div>
    <div class="card">Passed<strong>$passed</strong></div>
    <div class="card">Failed<strong>$failures</strong></div>
    <div class="card">Skipped<strong>$skipped</strong></div>
    <div class="card">Duration<strong>$($root.time) s</strong></div>
  </section>
  <table>
    <thead>
      <tr><th>Suite</th><th>Test case</th><th>Status</th><th>Duration</th><th>Details</th></tr>
    </thead>
    <tbody>
      $($rows -join [Environment]::NewLine)
    </tbody>
  </table>
</main>
</body>
</html>
"@

$outputDirectory = Split-Path -Parent $HtmlPath
if ($outputDirectory) {
    [System.IO.Directory]::CreateDirectory($outputDirectory) | Out-Null
}
[System.IO.File]::WriteAllText(
    [System.IO.Path]::GetFullPath($HtmlPath),
    $html,
    [System.Text.UTF8Encoding]::new($false))

Write-Host "Reporte HTML generado: $([System.IO.Path]::GetFullPath($HtmlPath))"
Write-Host "Consistencia validada: $tests casos, $passed aprobados, $failures fallidos, $skipped omitidos."
