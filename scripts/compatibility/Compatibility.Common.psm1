Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Resolve-CompatibilityPath([string]$Path) {
    # Set-Location does not change the process-wide .NET current directory.
    return $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Path)
}

function Assert-CompatibilityPlainPath([string]$Path) {
    $current = Resolve-CompatibilityPath $Path
    while ($current) {
        if (Test-Path -LiteralPath $current) {
            if ((Get-Item -LiteralPath $current -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "COMPAT_REPARSE_POINT: $current"
            }
        }
        $current = [IO.Path]::GetDirectoryName($current)
    }
}

function Get-CompatibilityInventory([string]$Root, [string]$Exclude = '') {
    $Root = Resolve-CompatibilityPath $Root
    Assert-CompatibilityPlainPath $Root
    if (!(Test-Path -LiteralPath $Root -PathType Container)) { throw "COMPAT_DIRECTORY_MISSING: $Root" }
    $result = [ordered]@{}
    $directories = [Collections.Generic.Stack[string]]::new()
    $directories.Push($Root)
    while ($directories.Count) {
        foreach ($item in Get-ChildItem -LiteralPath $directories.Pop() -Force) {
            Assert-CompatibilityPlainPath $item.FullName
            if ($item.PSIsContainer) { $directories.Push($item.FullName); continue }
            $relative = [IO.Path]::GetRelativePath($Root, $item.FullName).Replace('\', '/')
            if ($relative -cne $Exclude) {
                $result[$relative] = (Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
            }
        }
    }
    return $result
}

function Assert-CompatibilityInventory([string]$Root, [object[]]$Entries, [string]$Exclude = '') {
    $actual = Get-CompatibilityInventory $Root $Exclude
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($entry in $Entries) {
        if (!$seen.Add([string]$entry.path) -or !$actual.Contains([string]$entry.path) -or
            $actual[[string]$entry.path] -cne [string]$entry.sha256) {
            throw "COMPAT_INVENTORY_MISMATCH: $($entry.path)"
        }
    }
    if ($seen.Count -ne $actual.Count) { throw 'COMPAT_INVENTORY_MISMATCH: unexpected or missing files' }
}

function ConvertTo-CompatibilityEntries($Inventory) {
    foreach ($key in @($Inventory.Keys | Sort-Object)) {
        [ordered]@{ path = $key; sha256 = $Inventory[$key] }
    }
}

function Read-CompatibilityBaseline([string]$Root) {
    $Root = Resolve-CompatibilityPath $Root
    Assert-CompatibilityPlainPath (Join-Path $Root 'baseline.json')
    if (!(Test-Path -LiteralPath (Join-Path $Root 'baseline.json') -PathType Leaf)) {
        throw 'COMPAT_BASELINE_MISSING: baseline.json'
    }
    $baseline = Get-Content -LiteralPath (Join-Path $Root 'baseline.json') -Raw | ConvertFrom-Json
    if ($baseline.format -cne 'ARTest.NativeCompatibilityBaseline' -or $baseline.version -ne 1 -or
        $baseline.kitVersion -cne 'native-v1' -or $baseline.platform -cne 'x64' -or
        $baseline.configuration -cnotin @('Debug', 'Release') -or !$baseline.controlPassed) {
        throw 'COMPAT_BASELINE_UNSUPPORTED'
    }
    Assert-CompatibilityInventory $Root $baseline.files 'baseline.json'
    foreach ($file in @('bin/ARTestCompatHost.exe', 'catalog/ARTestCompatExtension/ARTestCompatExtension.dll',
                        'catalog/ARTestCompatExtension/artest-extension.json')) {
        if (!(Test-Path -LiteralPath (Join-Path $Root $file) -PathType Leaf)) { throw "COMPAT_BASELINE_MISSING: $file" }
    }
    return $baseline
}

function New-CompatibilityWorkDirectory {
    $directory = Join-Path ([IO.Path]::GetTempPath()) ('ARTestCompat-' + [guid]::NewGuid().ToString('N'))
    Assert-CompatibilityPlainPath $directory
    return (New-Item -ItemType Directory -Path $directory).FullName
}

function Remove-CompatibilityWorkDirectory([string]$Path) {
    $full = [IO.Path]::GetFullPath($Path).TrimEnd('\')
    if ([IO.Path]::GetDirectoryName($full) -ine [IO.Path]::GetTempPath().TrimEnd('\') -or
        [IO.Path]::GetFileName($full) -cnotmatch '^ARTestCompat-[a-f0-9]{32}$') {
        throw "COMPAT_UNSAFE_CLEANUP: $full"
    }
    # Audit the entire owned tree before deleting; never follow a junction.
    $null = Get-CompatibilityInventory $full
    Remove-Item -LiteralPath $full -Recurse -Force
}

function Invoke-CompatibilityCase([string]$HostPath, [string]$Catalog, [string]$Scenario,
                                  [string]$SdkVersion, [int]$TimeoutSeconds = 20) {
    $process = [Diagnostics.Process]::new()
    $process.StartInfo.FileName = $HostPath
    $process.StartInfo.WorkingDirectory = Split-Path $HostPath
    $process.StartInfo.UseShellExecute = $false
    $process.StartInfo.CreateNoWindow = $true
    $process.StartInfo.RedirectStandardOutput = $true
    $process.StartInfo.RedirectStandardError = $true
    $process.StartInfo.ArgumentList.Add($Catalog)
    $process.StartInfo.ArgumentList.Add($Scenario)
    $timer = [Diagnostics.Stopwatch]::StartNew()
    $result = [ordered]@{ name = $Scenario; passed = $false; seconds = 0; exitCode = $null
                         stdout = ''; stderr = ''; error = ''; evidence = $null }
    try {
        if (!$process.Start()) { throw 'Cannot start compatibility host' }
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        if (!$process.WaitForExit($TimeoutSeconds * 1000)) {
            $process.Kill($true)
            $process.WaitForExit()
            throw 'COMPAT_PROCESS_TIMEOUT'
        }
        $result.stdout = $stdout.GetAwaiter().GetResult()
        $result.stderr = $stderr.GetAwaiter().GetResult()
        $result.exitCode = $process.ExitCode
        if ($result.exitCode -ne 0) { throw "COMPAT_HOST_FAILED: exit $($result.exitCode): $($result.stdout) $($result.stderr)" }
        $evidence = $result.stdout | ConvertFrom-Json
        $expectedEngine = [IO.Path]::GetFullPath((Join-Path (Split-Path $HostPath) 'ARTestEngine.dll'))
        if ($evidence.format -cne 'ARTest.NativeCompatibilityCase' -or $evidence.version -ne 1 -or
            $evidence.passed -isnot [bool] -or !$evidence.passed -or $evidence.scenario -cne $Scenario -or
            $evidence.compiledSdk -cne $SdkVersion -or
            [IO.Path]::GetFullPath($evidence.enginePath) -ine $expectedEngine) {
            throw 'COMPAT_HOST_EVIDENCE_INVALID'
        }
        $result.evidence = $evidence
        $result.passed = $true
    }
    catch { $result.error = $_.Exception.Message }
    finally {
        $result.seconds = $timer.Elapsed.TotalSeconds
        $process.Dispose()
    }
    return [pscustomobject]$result
}

function New-CompatibilityCaseCatalog([string]$Source, [string]$Destination, [string]$Scenario) {
    Copy-Item -LiteralPath $Source -Destination $Destination -Recurse
    if ($Scenario -in @('IncompatibleAbi', 'IntegrityMismatch')) {
        $manifestPath = Join-Path $Destination 'ARTestCompatExtension/artest-extension.json'
        $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
        if ($Scenario -eq 'IncompatibleAbi') { $manifest.runtime.abi.major = 999 }
        else { $manifest.integrity.sha256 = '0' * 64 }
        $manifest | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath $manifestPath -Encoding utf8
    }
}

function Get-CompatibilityScenarios {
    return @('Lifecycle', 'ServiceFailure', 'CleanupFailure', 'Cancellation', 'Timeout',
             'InvalidParameters', 'IncompatibleAbi', 'IntegrityMismatch')
}

Export-ModuleMember -Function *-Compatibility*
