#requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$BaselineDirectory,
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
    [string]$EnginePath = '',
    [string]$OutputDirectory = ''
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module "$PSScriptRoot/compatibility/Compatibility.Common.psm1" -Force
$repo = Split-Path $PSScriptRoot
$root = Resolve-CompatibilityPath $BaselineDirectory
# Validation must never build or silently repair the historical consumer.
$baseline = Read-CompatibilityBaseline $root
$before = @(ConvertTo-CompatibilityEntries (Get-CompatibilityInventory $root))
if (!$EnginePath) { $EnginePath = Join-Path $repo "artifacts/bin/x64/$Configuration/ARTestEngine.dll" }
$engine = Resolve-CompatibilityPath $EnginePath
Assert-CompatibilityPlainPath $engine
$engineHash = (Get-FileHash -LiteralPath $engine -Algorithm SHA256).Hash.ToLowerInvariant()
if (!$OutputDirectory) {
    $OutputDirectory = Join-Path $repo ("artifacts/test-results/x64/$Configuration/native-compatibility/" +
        [DateTime]::UtcNow.ToString('yyyyMMddTHHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
}
$output = Resolve-CompatibilityPath $OutputDirectory
Assert-CompatibilityPlainPath $output
if (Test-Path -LiteralPath $output) { throw "COMPAT_REPORT_EXISTS: $output" }
if ($output.StartsWith($root.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'COMPAT_OUTPUT_INSIDE_BASELINE'
}
$work = New-CompatibilityWorkDirectory
$cases = @()
$guardError = ''
try {
    $bin = Join-Path $work 'bin'
    Copy-Item -LiteralPath (Join-Path $root 'bin') -Destination $bin -Recurse
    Copy-Item -LiteralPath $engine -Destination (Join-Path $bin 'ARTestEngine.dll')
    if ((Get-FileHash -LiteralPath (Join-Path $bin 'ARTestEngine.dll')).Hash.ToLowerInvariant() -cne $engineHash) {
        throw 'COMPAT_ENGINE_CHANGED_DURING_COPY'
    }
    foreach ($scenario in Get-CompatibilityScenarios) {
        $catalog = Join-Path $work $scenario
        New-CompatibilityCaseCatalog (Join-Path $root 'catalog') $catalog $scenario
        $case = Invoke-CompatibilityCase (Join-Path $bin 'ARTestCompatHost.exe') $catalog $scenario $baseline.sdkVersion
        $cases += $case
        Write-Host "$scenario : $(if ($case.passed) { 'PASSED' } else { 'FAILED' })"
        # Exercise copies must not be rewritten by the Engine/extension.
        $dll = Join-Path $catalog 'ARTestCompatExtension/ARTestCompatExtension.dll'
        $frozenHash = ($baseline.files | Where-Object path -CEQ 'catalog/ARTestCompatExtension/ARTestCompatExtension.dll').sha256
        if ((Get-FileHash -LiteralPath $dll).Hash.ToLowerInvariant() -cne $frozenHash) {
            throw 'COMPAT_EXERCISE_BINARY_CHANGED'
        }
    }
    Assert-CompatibilityInventory $root $before
    $copiedHostHash = (Get-FileHash -LiteralPath (Join-Path $bin 'ARTestCompatHost.exe')).Hash.ToLowerInvariant()
    if ($copiedHostHash -cne ($baseline.files | Where-Object path -CEQ 'bin/ARTestCompatHost.exe').sha256 -or
        (Get-FileHash -LiteralPath (Join-Path $bin 'ARTestEngine.dll')).Hash.ToLowerInvariant() -cne $engineHash -or
        (Get-FileHash -LiteralPath $engine).Hash.ToLowerInvariant() -cne $engineHash) {
        throw 'COMPAT_BINARY_CHANGED_DURING_RUN'
    }
}
catch { $guardError = $_.Exception.Message }
finally {
    try { Assert-CompatibilityInventory $root $before }
    catch { $guardError += ' ' + $_.Exception.Message }
    try { Remove-CompatibilityWorkDirectory $work }
    catch { $guardError += ' ' + $_.Exception.Message }
}
$failed = @($cases | Where-Object { !$_.passed }).Count
$passed = !$guardError -and $cases.Count -eq 8 -and $failed -eq 0
$report = [ordered]@{
    format = 'ARTest.NativeCompatibilityReport'; version = 1; runner = 'PowerShell isolated native host'
    createdUtc = [DateTime]::UtcNow.ToString('O'); passed = [bool]$passed
    status = $(if ($passed) { 'PASSED' } else { 'FAILED' })
    expectedCases = 8; executedCases = $cases.Count; failedCases = $failed; guardError = $guardError
    baseline = [ordered]@{ sdkVersion = $baseline.sdkVersion; configuration = $baseline.configuration
        compiler = $baseline.compiler; manifestSha256 = ($before | Where-Object path -CEQ 'baseline.json').sha256
        hostSha256 = ($baseline.files | Where-Object path -CEQ 'bin/ARTestCompatHost.exe').sha256
        extensionSha256 = ($baseline.files | Where-Object path -CEQ 'catalog/ARTestCompatExtension/ARTestCompatExtension.dll').sha256
        engineApi = $baseline.engineApi; nativeExtensionAbi = $baseline.nativeExtensionAbi }
    candidate = [ordered]@{ enginePath = $engine; engineSha256 = $engineHash; configuration = $Configuration
        fileVersion = [Diagnostics.FileVersionInfo]::GetVersionInfo($engine).FileVersion }
    cases = $cases
}
$null = New-Item -ItemType Directory -Path $output
$report | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath (Join-Path $output 'compatibility.json') -Encoding utf8

# Emit JUnit-compatible XML (not a claim that these scenarios ran in Google Test).
$xml = [xml]'<?xml version="1.0" encoding="utf-8"?><testsuites><testsuite name="NativeCompatibility"/></testsuites>'
$suite = $xml.DocumentElement.FirstChild
foreach ($case in $cases) {
    $node = $xml.CreateElement('testcase')
    $node.SetAttribute('name', $case.name)
    $node.SetAttribute('classname', 'NativeCompatibility')
    $node.SetAttribute('time', $case.seconds.ToString('F6', [Globalization.CultureInfo]::InvariantCulture))
    if (!$case.passed) {
        $failure = $xml.CreateElement('failure')
        $failure.SetAttribute('message', $case.error)
        $node.AppendChild($failure) | Out-Null
    }
    $suite.AppendChild($node) | Out-Null
}
if ($guardError -or $cases.Count -ne 8) {
    $guard = $xml.CreateElement('testcase')
    $guard.SetAttribute('name', 'EvidenceIntegrity')
    $failure = $xml.CreateElement('failure')
    $failure.SetAttribute('message', "Incomplete or unverifiable execution. $guardError")
    $guard.AppendChild($failure) | Out-Null
    $suite.AppendChild($guard) | Out-Null
}
foreach ($node in @($xml.DocumentElement, $suite)) {
    $node.SetAttribute('tests', [string]$suite.SelectNodes('./testcase').Count)
    $node.SetAttribute('failures', [string]$suite.SelectNodes('./testcase/failure').Count)
    $node.SetAttribute('errors', '0')
    $node.SetAttribute('skipped', '0')
}
$xml.Save((Join-Path $output 'compatibility.xml'))
function Encode([object]$Value) { [Net.WebUtility]::HtmlEncode([string]$Value) }
$rows = foreach ($case in $cases) {
    '<tr><td>' + (Encode $case.name) + '</td><td>' + $(if ($case.passed) { 'PASSED' } else { 'FAILED' }) +
        '</td><td>' + $case.seconds.ToString('F3', [Globalization.CultureInfo]::InvariantCulture) +
        '</td><td>' + (Encode $case.error) + '</td></tr>'
}
$html = '<!doctype html><html lang="en"><meta charset="utf-8"><title>ARTest native compatibility</title>' +
    '<style>body{font:16px system-ui;margin:3rem;max-width:1100px}table{border-collapse:collapse;width:100%}' +
    'td,th{padding:12px;border:1px solid #ccd;text-align:left}code{overflow-wrap:anywhere}</style>' +
    '<h1>ARTest native compatibility: ' + $report.status + '</h1><p>SDK ' + (Encode $baseline.sdkVersion) +
    ' / ' + (Encode $baseline.configuration) + ' consumer → ' + (Encode $Configuration) +
    ' Engine. ' + $cases.Count + '/8 scenarios executed; failures: ' + $failed + '.</p>' +
    '<p>Independent native-host scenarios, not Google Test. API 0.4 / ABI 0.1 remain experimental.</p>' +
    '<p>Candidate SHA-256: <code>' + $engineHash + '</code></p><p>Baseline SHA-256: <code>' +
    $report.baseline.manifestSha256 + '</code></p><p>' + (Encode $guardError) + '</p>' +
    '<table><thead><tr><th>Scenario</th><th>Verdict</th><th>Seconds</th><th>Failure</th></tr></thead><tbody>' +
    ($rows -join '') + '</tbody></table><p><a href="compatibility.json">Full provenance and per-case evidence</a> · ' +
    '<a href="compatibility.xml">JUnit XML</a></p></html>'
$html | Set-Content -LiteralPath (Join-Path $output 'compatibility.html') -Encoding utf8
# Verify persisted totals/verdicts before declaring a successful matrix cell.
$saved = Get-Content -LiteralPath (Join-Path $output 'compatibility.json') -Raw | ConvertFrom-Json
[xml]$savedXml = Get-Content -LiteralPath (Join-Path $output 'compatibility.xml') -Raw
if ($saved.passed -ne $passed -or $saved.cases.Count -ne $cases.Count -or
    [int]$savedXml.testsuites.failures -ne $suite.SelectNodes('./testcase/failure').Count) {
    throw 'COMPAT_REPORT_INCONSISTENT'
}
Write-Host "Compatibility report: $output"
if (!$passed) { throw "COMPAT_VALIDATION_FAILED: $failed failed cases. $guardError" }
