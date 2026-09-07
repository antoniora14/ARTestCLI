#requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$BaselineDirectory,
    [Parameter(Mandatory)][string]$EnginePath
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module "$PSScriptRoot/compatibility/Compatibility.Common.psm1" -Force
$root = Resolve-CompatibilityPath $BaselineDirectory
$EnginePath = Resolve-CompatibilityPath $EnginePath
$baseline = Read-CompatibilityBaseline $root
$before = @(ConvertTo-CompatibilityEntries (Get-CompatibilityInventory $root))
$work = New-CompatibilityWorkDirectory
$checks = [Collections.Generic.List[string]]::new()
function Expect-Rejection([string]$Name, [scriptblock]$Action, [string]$Diagnostic) {
    $caught = ''
    try { & $Action | Out-Null } catch { $caught = $_.Exception.Message }
    if (!$caught.Contains($Diagnostic)) { throw "Guard failed: $Name. Expected $Diagnostic; got: $caught" }
    $checks.Add($Name)
}
try {
    $previousLocation = Get-Location
    try {
        Copy-Item -LiteralPath $root -Destination (Join-Path $work 'relative-baseline') -Recurse
        Set-Location -LiteralPath $work
        if ((Resolve-CompatibilityPath './relative-baseline') -ine (Join-Path $work 'relative-baseline')) {
            throw 'PowerShell relative path resolution failed'
        }
        $null = Read-CompatibilityBaseline './relative-baseline'
        $checks.Add('PowerShellWorkingDirectory')
    }
    finally { Set-Location -LiteralPath $previousLocation.Path }
    foreach ($mutation in @('ChangedBinary', 'MissingFile', 'UnexpectedFile', 'DuplicateEntry', 'WrongVersion')) {
        $copy = Join-Path $work $mutation
        Copy-Item -LiteralPath $root -Destination $copy -Recurse
        $hostPath = Join-Path $copy 'bin/ARTestCompatHost.exe'
        if ($mutation -eq 'ChangedBinary') { Add-Content -LiteralPath $hostPath -Value 'changed' }
        if ($mutation -eq 'MissingFile') { Remove-Item -LiteralPath $hostPath }
        if ($mutation -eq 'UnexpectedFile') { Set-Content -LiteralPath (Join-Path $copy 'unexpected.txt') -Value 'unknown' }
        if ($mutation -in @('DuplicateEntry', 'WrongVersion')) {
            $manifestPath = Join-Path $copy 'baseline.json'
            $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
            if ($mutation -eq 'WrongVersion') { $manifest.version = 999 }
            else { $manifest.files += $manifest.files[0] }
            $manifest | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath $manifestPath -Encoding utf8
        }
        $diagnostic = if ($mutation -eq 'WrongVersion') { 'COMPAT_BASELINE_UNSUPPORTED' } else { 'COMPAT_INVENTORY_MISMATCH' }
        Expect-Rejection $mutation { Read-CompatibilityBaseline $copy } $diagnostic
    }
    Expect-Rejection 'MissingBaseline' { Read-CompatibilityBaseline (Join-Path $work 'missing') } 'COMPAT_BASELINE_MISSING'
    Expect-Rejection 'ImmutableBaseline' {
        & "$PSScriptRoot/new-native-compatibility-baseline.ps1" -SdkRoot (Join-Path $work 'missing-sdk') -OutputDirectory $root
    } 'COMPAT_IMMUTABLE_BASELINE'
    $runtime = Join-Path $work 'runtime'
    Copy-Item -LiteralPath (Join-Path $root 'bin') -Destination $runtime -Recurse
    Copy-Item -LiteralPath $EnginePath -Destination (Join-Path $runtime 'ARTestEngine.dll')
    $catalog = Join-Path $work 'catalog'
    Copy-Item -LiteralPath (Join-Path $root 'catalog') -Destination $catalog -Recurse
    # Exit 0 is insufficient: reject a successful host with the wrong provenance.
    $case = Invoke-CompatibilityCase (Join-Path $runtime 'ARTestCompatHost.exe') $catalog 'Lifecycle' 'wrong-sdk'
    if ($case.passed -or $case.error -cne 'COMPAT_HOST_EVIDENCE_INVALID') { throw 'Evidence provenance guard failed' }
    $checks.Add('WrongProvenance')
    $case = Invoke-CompatibilityCase (Join-Path $runtime 'ARTestCompatHost.exe') $catalog 'UnknownScenario' $baseline.sdkVersion
    if ($case.passed -or $case.exitCode -ne 1) { throw 'Nonzero host exit guard failed' }
    $checks.Add('NonzeroHostExit')
    # A known negative scenario with an unmodified valid package must fail, not pass by name.
    $case = Invoke-CompatibilityCase (Join-Path $runtime 'ARTestCompatHost.exe') $catalog 'IncompatibleAbi' $baseline.sdkVersion
    if ($case.passed -or $case.exitCode -ne 1) { throw 'Negative scenario oracle guard failed' }
    $checks.Add('NegativeScenarioOracle')

    # End-to-end failure propagation must agree in process exit, JSON, XML and HTML.
    # Change only copied provenance: loading broken PE files can trigger Windows loader dialogs.
    $wrongBaseline = Join-Path $work 'wrong-provenance'
    Copy-Item -LiteralPath $root -Destination $wrongBaseline -Recurse
    $wrongManifestPath = Join-Path $wrongBaseline 'baseline.json'
    $wrongManifest = Get-Content -LiteralPath $wrongManifestPath -Raw | ConvertFrom-Json
    $wrongManifest.sdkVersion = 'wrong-sdk'
    $wrongManifest | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath $wrongManifestPath -Encoding utf8
    $failureReport = Join-Path $work 'failure-report'
    Expect-Rejection 'FailedReportExit' {
        & "$PSScriptRoot/test-native-compatibility.ps1" -BaselineDirectory $wrongBaseline -EnginePath $EnginePath -OutputDirectory $failureReport
    } 'COMPAT_VALIDATION_FAILED'
    $json = Get-Content -LiteralPath (Join-Path $failureReport 'compatibility.json') -Raw | ConvertFrom-Json
    [xml]$xml = Get-Content -LiteralPath (Join-Path $failureReport 'compatibility.xml') -Raw
    $html = Get-Content -LiteralPath (Join-Path $failureReport 'compatibility.html') -Raw
    if ($json.passed -or $json.executedCases -ne 8 -or $json.failedCases -ne 8 -or
        [int]$xml.testsuites.failures -ne 8 -or !$html.Contains('ARTest native compatibility: FAILED')) {
        throw 'Failure report verdict mismatch'
    }
    $checks.Add('FailedReportConsistency')
    Assert-CompatibilityInventory $root $before
    Write-Host "Compatibility harness guards: $($checks.Count)/$($checks.Count) PASSED ($($checks -join ', '))"
}
finally { Remove-CompatibilityWorkDirectory $work }
