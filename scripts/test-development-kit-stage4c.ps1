[CmdletBinding()]
param(
    [ValidateSet('Release')][string]$Configuration = 'Release',
    [ValidateSet('x64')][string]$Platform = 'x64',
    [Parameter(Mandatory=$true)][string]$PythonRuntime,
    [Parameter(Mandatory=$true)][string]$DependencyWheelRoot,
    [string]$ProtocPath,
    [string]$VisualStudioPath = 'D:\Program Files\Microsoft Visual Studio\18\Insiders',
    [string]$VisualCRTRoot
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$version = Get-Content -LiteralPath (Join-Path $repositoryRoot 'source\ARTest.SDK\development-kit\development-kit-version.json') -Raw | ConvertFrom-Json
$packageName = "ARTestDevelopmentKit-$($version.kitVersion)-evaluation-windows-$Platform"
$archive = Join-Path $repositoryRoot "artifacts\sdk-packages\$Platform\$Configuration\$packageName.zip"
$runId = (Get-Date).ToUniversalTime().ToString('yyyyMMddTHHmmssZ') + '-' + [guid]::NewGuid().ToString('N').Substring(0,8)
$token = $runId.Substring($runId.Length - 8)
$testRoot = [IO.Path]::GetFullPath((Join-Path $repositoryRoot "artifacts\stage4c-test-roots\ARTest 4C $token"))
$evidence = Join-Path $repositoryRoot "artifacts\acceptance\py-dx-01\stage4c-candidate\$runId"
$results = [Collections.Generic.List[object]]::new()
$commands = [Collections.Generic.List[string]]::new()
$fatal = $null
$null = New-Item -ItemType Directory -Path $evidence -Force

function Quote-Arg([string]$Value) { return "'" + $Value.Replace("'", "''") + "'" }
function Add-Command([string]$Executable, [string[]]$Arguments) {
    $commands.Add("& $(Quote-Arg $Executable) " + (($Arguments | ForEach-Object { Quote-Arg $_ }) -join ' '))
}
function Invoke-Case([string]$Name, [scriptblock]$Action) {
    $watch = [Diagnostics.Stopwatch]::StartNew()
    try {
        $detail = @(& $Action); $watch.Stop()
        $results.Add([ordered]@{name=$Name;status='PASSED';durationMs=$watch.ElapsedMilliseconds;detail=@($detail | ForEach-Object {[string]$_})})
        Write-Host "$Name`: PASSED"
    }
    catch {
        $watch.Stop(); $results.Add([ordered]@{name=$Name;status='FAILED';durationMs=$watch.ElapsedMilliseconds;detail=@($_.Exception.ToString())})
        throw
    }
}
function ConvertFrom-LastJson([object[]]$Output) {
    $text = $Output | Out-String; $start = $text.LastIndexOf("`n{")
    if ($start -lt 0 -and $text.TrimStart().StartsWith('{')) { $start = -1 }
    $json = if ($start -ge 0) { $text.Substring($start + 1) } else { $text }
    return $json | ConvertFrom-Json
}
function Invoke-Kit([string[]]$Arguments) {
    Add-Command $script:entry $Arguments
    $output = & $script:entry @Arguments 2>&1
    if ($LASTEXITCODE -ne 0) { throw "Kit command failed ($LASTEXITCODE): $($output | Out-String)" }
    return @($output)
}
function Assert-KitFailure([string]$Expected, [string[]]$Arguments) {
    $pwsh = Join-Path $PSHOME 'pwsh.exe'; Add-Command $pwsh (@('-NoProfile','-NonInteractive','-File',$script:entry) + $Arguments)
    $output = & $pwsh -NoProfile -NonInteractive -File $script:entry @Arguments 2>&1
    if ($LASTEXITCODE -eq 0 -or ($output | Out-String) -notmatch [regex]::Escape($Expected)) {
        throw "Expected $Expected, exit $LASTEXITCODE`: $($output | Out-String)"
    }
    return "$Expected observed"
}
function Get-TreeDigest([string]$Root) {
    $lines = Get-ChildItem -LiteralPath $Root -Recurse -File -Force | Sort-Object FullName | ForEach-Object {
        "$([IO.Path]::GetRelativePath($Root,$_.FullName).Replace('\','/'))=$((Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash)"
    }
    $bytes = [Text.Encoding]::UTF8.GetBytes(($lines -join "`n")); $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-','').ToLowerInvariant() } finally { $sha.Dispose() }
}
function Assert-InstalledDiscovery {
    $mapping = Join-Path $script:installConfig 'python-environments.json'
    $plan = Join-Path $script:installConfig 'acceptance-compile.json'
    @{format='ARTest.Script';version=1;instruments=@();commands=@(@{stepId=1;name='Time.WaitMs';instrument='NoInstrument';params=@{milliseconds=0}})} |
        ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $plan -Encoding utf8
    foreach ($arguments in @(
        @('extensions','validate',$script:installCatalog),
        @('compile',$plan,'--extensions',$script:installCatalog,'--python-environments',$mapping))) {
        Add-Command $script:installedCli $arguments
        $output = & $script:installedCli @arguments 2>&1
        if ($LASTEXITCODE -ne 0) { throw "Installed discovery failed: $($output | Out-String)" }
    }
}
function New-Project([string]$Name, [string]$Language) {
    $args = @('new','--name',$Name,'--folder',$script:projects,'--language',$Language,'--variant','driver-only')
    if ($Language -eq 'cpp') { $args += @('--msbuild',$script:msbuild) }
    return ConvertFrom-LastJson (Invoke-Kit $args)
}
function Get-RegisterArguments([string]$Project, [switch]$DefineTarget) {
    $args = @('register','--project',$Project,'--target','station')
    if ($DefineTarget) { $args += @('--cli',$script:installedCli,'--catalog',$script:installCatalog,'--config',$script:installConfig) }
    if ((Get-Content -LiteralPath (Join-Path $Project 'artest-sdk-project.json') -Raw | ConvertFrom-Json).language -eq 'cpp') {
        $args += @('--msbuild',$script:msbuild)
    }
    return $args
}
function Register-Project([string]$Project, [switch]$DefineTarget) {
    $args = Get-RegisterArguments $Project -DefineTarget:$DefineTarget
    return ConvertFrom-LastJson (Invoke-Kit $args)
}

try {
    if (Test-Path -LiteralPath $testRoot) { throw "External test root already exists: $testRoot" }
    $null = New-Item -ItemType Directory -Path $testRoot
    $null = New-Item -ItemType Directory -Path $evidence -Force
    $env:ARTEST_SDK_CONFIG_ROOT = Join-Path $testRoot 'local'

    Invoke-Case 'Source guard, package, and extracted-kit verification' {
        & (Join-Path $PSScriptRoot 'verify-development-kit.ps1')
        $parameters = @{Configuration=$Configuration;Platform=$Platform;PythonRuntime=$PythonRuntime;DependencyWheelRoot=$DependencyWheelRoot;VisualStudioPath=$VisualStudioPath}
        if ($ProtocPath) {$parameters.ProtocPath=$ProtocPath}; if ($VisualCRTRoot) {$parameters.VisualCRTRoot=$VisualCRTRoot}
        & (Join-Path $PSScriptRoot 'package-development-kit.ps1') @parameters
        $script:kit = Join-Path $testRoot 'Extracted SDK'
        Expand-Archive -LiteralPath $archive -DestinationPath $script:kit
        $script:entry = Join-Path $script:kit 'artest.ps1'
        $verify = ConvertFrom-LastJson (Invoke-Kit @('verify'))
        if ($verify.kitVersion -ne '0.4.0') { throw 'Current kit version is not 0.4.0.' }
        $script:projects = Join-Path $testRoot 'projects'; $null = New-Item -ItemType Directory -Path $script:projects
        $script:installation = Join-Path $testRoot 'installation'
        $runtime = Join-Path $script:installation 'runtime'; Copy-Item -LiteralPath (Join-Path $script:kit 'runtime\x64\Release') -Destination $runtime -Recurse
        $script:installedCli = Join-Path $runtime 'ARTestCLI.exe'
        $script:installCatalog = Join-Path $script:installation 'extensions'
        $script:installConfig = Join-Path $script:installation 'config'
        $script:msbuild = Join-Path $VisualStudioPath 'MSBuild\Current\Bin\MSBuild.exe'
        "archiveSha256=$((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant())"
    }

    Invoke-Case 'Python first, idempotent, update, integrity, and final-path preparation' {
        $created = New-Project 'Python Registration' 'python'; $script:pythonProject = $created.project
        $first = Register-Project $script:pythonProject -DefineTarget
        $statePath = Join-Path $script:installConfig 'registrations.json'; $mappingPath = Join-Path $script:installConfig 'python-environments.json'
        $firstCatalog = Get-TreeDigest $script:installCatalog; $firstState = (Get-FileHash -LiteralPath $statePath -Algorithm SHA256).Hash
        $repeat = Register-Project $script:pythonProject
        if (-not $repeat.reused -or (Get-TreeDigest $script:installCatalog) -ne $firstCatalog -or (Get-FileHash -LiteralPath $statePath -Algorithm SHA256).Hash -ne $firstState) {
            throw 'Identical registration was not idempotent.'
        }
        $receipt = @((Get-Content -LiteralPath $mappingPath -Raw | ConvertFrom-Json).PSObject.Properties)[0].Value
        if (-not ([IO.Path]::GetFullPath($receipt).StartsWith($script:installConfig.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase))) {
            throw 'Python environment was not prepared at its definitive installation path.'
        }
        $extensionId = $first.extensionId
        $mapping = Get-Content -LiteralPath $mappingPath -Raw | ConvertFrom-Json
        $mapping | Add-Member -NotePropertyName 'com.example.preserved.mapping' -NotePropertyValue $receipt
        $mapping | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $mappingPath -Encoding utf8
        $mapping.PSObject.Properties.Remove($extensionId)
        $mapping | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $mappingPath -Encoding utf8
        $missing = Register-Project $script:pythonProject
        if ($missing.reused) { throw 'A missing persisted Python mapping was incorrectly reported as reused.' }
        $mapping = Get-Content -LiteralPath $mappingPath -Raw | ConvertFrom-Json
        if (-not $mapping.PSObject.Properties['com.example.preserved.mapping'] -or [string]$mapping.$extensionId -cne $receipt) {
            throw 'Repairing a missing Python mapping did not preserve other associations.'
        }
        $mapping.$extensionId = Join-Path $script:installConfig 'wrong-receipt.json'
        $mapping | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $mappingPath -Encoding utf8
        $incorrect = Register-Project $script:pythonProject
        if ($incorrect.reused) { throw 'An incorrect persisted Python mapping was incorrectly reported as reused.' }
        $mapping = Get-Content -LiteralPath $mappingPath -Raw | ConvertFrom-Json
        if (-not $mapping.PSObject.Properties['com.example.preserved.mapping'] -or [string]$mapping.$extensionId -cne $receipt) {
            throw 'Repairing an incorrect Python mapping did not preserve other associations.'
        }
        $receiptBytes = [IO.File]::ReadAllBytes($receipt)
        $beforeReceiptFailure = Get-TreeDigest $script:installCatalog
        Set-Content -LiteralPath $receipt -Value '{ corrupt receipt' -Encoding utf8
        try { $null = Assert-KitFailure 'ARTESTSDK007' @('register','--project',$script:pythonProject,'--target','station') }
        finally { [IO.File]::WriteAllBytes($receipt,$receiptBytes) }
        if ((Get-TreeDigest $script:installCatalog) -ne $beforeReceiptFailure) { throw 'A corrupt Python receipt changed the active catalog.' }
        Add-Content -LiteralPath (Join-Path $script:pythonProject 'src\extension.py') -Value "`n# stage4c update"
        $updated = Register-Project $script:pythonProject
        if ($updated.reused -or $updated.revisionId -eq $first.revisionId) { throw 'Python source update did not select a new revision.' }
        $active = $updated.package; $file = @(Get-ChildItem -LiteralPath $active -Recurse -File -Filter 'extension.py')[0].FullName
        $bytes = [IO.File]::ReadAllBytes($file); [IO.File]::WriteAllBytes($file,$bytes + [byte[]](10,35))
        $null = Assert-KitFailure 'ARTESTREG004' @('register','--project',$script:pythonProject,'--target','station')
        $state = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
        $revisionPackage = @($state.registrations.PSObject.Properties | Where-Object Name -CEQ $updated.extensionId)[0].Value.package
        Remove-Item -LiteralPath $active -Recurse -Force; Copy-Item -LiteralPath $revisionPackage -Destination $active -Recurse
        'Python receipt/environment validation, persisted mapping semantics, package integrity, update, and idempotence verified'
    }

    Invoke-Case 'Identical catalog recovery preserves the pre-publication selection' {
        $projectValue = Get-Content -LiteralPath (Join-Path $script:pythonProject 'artest-sdk-project.json') -Raw | ConvertFrom-Json
        $extensionId = [string]$projectValue.extensionId
        $statePath = Join-Path $script:installConfig 'registrations.json'
        $mappingPath = Join-Path $script:installConfig 'python-environments.json'
        $registryPath = Join-Path $env:ARTEST_SDK_CONFIG_ROOT 'installations.json'
        $journalPath = Join-Path $script:installConfig '.artest-register-transaction.json'
        $wrongReceipt = Join-Path $script:installConfig 'mapping-needs-correction.json'

        $mapping = Get-Content -LiteralPath $mappingPath -Raw | ConvertFrom-Json
        $expectedReceipt = [string]$mapping.$extensionId
        $mapping.$extensionId = $wrongReceipt
        $mapping | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $mappingPath -Encoding utf8
        $beforeCatalog = Get-TreeDigest $script:installCatalog
        $beforeState = [Convert]::ToBase64String([IO.File]::ReadAllBytes($statePath))
        $beforeMapping = [Convert]::ToBase64String([IO.File]::ReadAllBytes($mappingPath))
        $beforeRegistry = [Convert]::ToBase64String([IO.File]::ReadAllBytes($registryPath))
        $env:ARTEST_SDK_REGISTER_FAILPOINT = 'before-promotion'
        try { $null = Assert-KitFailure 'ARTESTREG009' @('register','--project',$script:pythonProject,'--target','station') }
        finally { Remove-Item Env:ARTEST_SDK_REGISTER_FAILPOINT -ErrorAction SilentlyContinue }
        if ((Get-TreeDigest $script:installCatalog) -cne $beforeCatalog -or
            [Convert]::ToBase64String([IO.File]::ReadAllBytes($statePath)) -cne $beforeState -or
            [Convert]::ToBase64String([IO.File]::ReadAllBytes($mappingPath)) -cne $beforeMapping -or
            [Convert]::ToBase64String([IO.File]::ReadAllBytes($registryPath)) -cne $beforeRegistry -or
            (Test-Path -LiteralPath $journalPath)) {
            throw 'Prepared-phase recovery changed the identical prior catalog, mapping, state, profile, or left a journal.'
        }
        $corrected = Register-Project $script:pythonProject
        if ($corrected.reused -or [string](Get-Content -LiteralPath $mappingPath -Raw | ConvertFrom-Json).$extensionId -cne $expectedReceipt) {
            throw 'A later attempt did not correct the Python association after prepared-phase recovery.'
        }

        $mapping = Get-Content -LiteralPath $mappingPath -Raw | ConvertFrom-Json
        $mapping.$extensionId = $wrongReceipt
        $mapping | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $mappingPath -Encoding utf8
        $beforeCatalog = Get-TreeDigest $script:installCatalog
        $beforeState = [Convert]::ToBase64String([IO.File]::ReadAllBytes($statePath))
        $beforeMapping = [Convert]::ToBase64String([IO.File]::ReadAllBytes($mappingPath))
        $beforeRegistry = [Convert]::ToBase64String([IO.File]::ReadAllBytes($registryPath))
        $marker = Join-Path $testRoot 'publisher-before-backup.marker'
        $registerArguments = Get-RegisterArguments $script:pythonProject
        $pwsh = Join-Path $PSHOME 'pwsh.exe'
        $childArguments = @('-NoProfile','-NonInteractive','-File',$script:entry) + $registerArguments
        Add-Command $pwsh $childArguments
        $start = [Diagnostics.ProcessStartInfo]::new()
        $start.FileName = $pwsh; $start.UseShellExecute = $false; $start.CreateNoWindow = $true
        foreach ($argument in $childArguments) { $null = $start.ArgumentList.Add($argument) }
        $start.Environment['ARTEST_SDK_CONFIG_ROOT'] = $env:ARTEST_SDK_CONFIG_ROOT
        $start.Environment['ARTEST_SDK_REGISTER_FAILPOINT'] = 'before-backup-wait'
        $start.Environment['ARTEST_SDK_REGISTER_FAILPOINT_MARKER'] = $marker
        $publisher = [Diagnostics.Process]::Start($start)
        try {
            $watch = [Diagnostics.Stopwatch]::StartNew()
            while (-not (Test-Path -LiteralPath $marker -PathType Leaf) -and -not $publisher.HasExited -and $watch.ElapsedMilliseconds -lt 60000) {
                Start-Sleep -Milliseconds 100
            }
            if (-not (Test-Path -LiteralPath $marker -PathType Leaf) -or $publisher.HasExited) { throw 'Publisher did not reach the pre-backup interruption point.' }
            $journal = Get-Content -LiteralPath $journalPath -Raw | ConvertFrom-Json
            if ($journal.phase -cne 'backingUp' -or (Test-Path -LiteralPath $journal.backup) -or
                (Get-TreeDigest $journal.catalog) -cne (Get-TreeDigest $journal.candidate)) {
                throw 'Pre-backup interruption did not expose the expected identical-catalog topology.'
            }
            $publisher.Kill($true)
            if (-not $publisher.WaitForExit(5000)) { throw 'Pre-backup publisher termination was not confirmed.' }
        }
        finally {
            if (-not $publisher.HasExited) {
                try { $publisher.Kill($true) } catch { }
                $null = $publisher.WaitForExit(5000)
            }
            $publisher.Dispose()
        }

        $env:ARTEST_SDK_REGISTER_FAILPOINT = 'before-promotion'
        try {
            Add-Command $pwsh $childArguments
            $recoveryOutput = & $pwsh @childArguments 2>&1
            if ($LASTEXITCODE -eq 0 -or ($recoveryOutput | Out-String) -notmatch 'ARTESTREG_RECOVERED' -or
                ($recoveryOutput | Out-String) -notmatch 'ARTESTREG009' -or ($recoveryOutput | Out-String) -match 'ARTESTREG007') {
                throw "Pre-backup recovery did not complete cleanly before the simulated retry failure: $($recoveryOutput | Out-String)"
            }
        }
        finally { Remove-Item Env:ARTEST_SDK_REGISTER_FAILPOINT -ErrorAction SilentlyContinue }
        if ((Get-TreeDigest $script:installCatalog) -cne $beforeCatalog -or
            [Convert]::ToBase64String([IO.File]::ReadAllBytes($statePath)) -cne $beforeState -or
            [Convert]::ToBase64String([IO.File]::ReadAllBytes($mappingPath)) -cne $beforeMapping -or
            [Convert]::ToBase64String([IO.File]::ReadAllBytes($registryPath)) -cne $beforeRegistry -or
            (Test-Path -LiteralPath $journalPath)) {
            throw 'Pre-backup interruption recovery did not restore the exact prior selection.'
        }
        $retried = Register-Project $script:pythonProject
        if ($retried.reused -or [string](Get-Content -LiteralPath $mappingPath -Raw | ConvertFrom-Json).$extensionId -cne $expectedReceipt) {
            throw 'A later attempt did not succeed after pre-backup interruption recovery.'
        }
        'prepared and backingUp recovery preserved identical prior catalogs and exact state/mapping/profile bytes; later retries succeeded'
    }

    Invoke-Case 'C++ first, repeat, update, and multiple-package preservation' {
        $a = New-Project 'Native A' 'cpp'; $script:nativeA = $a.project
        $first = Register-Project $script:nativeA; $repeat = Register-Project $script:nativeA
        if (-not $repeat.reused) { throw 'Identical native registration was not reused.' }
        $header = Join-Path $script:nativeA 'SimulatedValueSource.h'; (Get-Content -LiteralPath $header -Raw).Replace('42.0','43.0') | Set-Content -LiteralPath $header -Encoding utf8
        $updated = Register-Project $script:nativeA
        if ($updated.revisionId -eq $first.revisionId) { throw 'Native source update did not select a new revision.' }
        $b = New-Project 'Native B' 'cpp'; $script:nativeB = $b.project; $null = Register-Project $script:nativeB
        Assert-InstalledDiscovery
        $report = (& $script:installedCli extensions validate $script:installCatalog | Out-String) | ConvertFrom-Json
        if (@($report.packages).Count -ne 3) { throw 'Registration did not preserve all three packages.' }
        'Python plus two native packages discovered by the installed CLI'
    }

    Invoke-Case 'Ownership and duplicate-ID conflicts fail closed' {
        $statePath = Join-Path $script:installConfig 'registrations.json'; $stateBytes = [IO.File]::ReadAllBytes($statePath)
        $state = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
        $nativeId = (Get-Content -LiteralPath (Join-Path $script:nativeA 'artest-sdk-project.json') -Raw | ConvertFrom-Json).extensionId
        $state.registrations.PSObject.Properties.Remove($nativeId)
        $state | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $statePath -Encoding utf8
        $null = Assert-KitFailure 'ARTESTREG008' @('register','--project',$script:nativeA,'--target','station','--msbuild',$script:msbuild)
        [IO.File]::WriteAllBytes($statePath,$stateBytes)
        $nativePackage = (Register-Project $script:nativeA).package; $duplicate = Join-Path $script:installCatalog 'foreign-duplicate'; Copy-Item -LiteralPath $nativePackage -Destination $duplicate -Recurse
        $null = Assert-KitFailure 'ARTESTREG006' @('register','--project',$script:nativeB,'--target','station','--msbuild',$script:msbuild)
        Remove-Item -LiteralPath $duplicate -Recurse -Force
        Assert-InstalledDiscovery
        'foreign ownership and duplicate extension ID rejected'
    }

    Invoke-Case 'Incompatible target and denied lock preserve selection' {
        $registry = Join-Path $env:ARTEST_SDK_CONFIG_ROOT 'installations.json'; $before = [IO.File]::ReadAllBytes($registry)
        $bad = Join-Path $testRoot 'bad'; $null = New-Item -ItemType Directory -Path $bad; Copy-Item -LiteralPath $script:installedCli -Destination (Join-Path $bad 'ARTestCLI.exe')
        $null = Assert-KitFailure 'ARTESTREG003' @('register','--project',$script:nativeB,'--target','bad','--cli',(Join-Path $bad 'ARTestCLI.exe'),'--catalog',(Join-Path $bad 'e'),'--config',(Join-Path $bad 'c'),'--msbuild',$script:msbuild)
        $blocked = Join-Path $bad 'blocked-config'; Set-Content -LiteralPath $blocked -Value 'not a directory'
        $null = Assert-KitFailure 'ARTESTREG005' @('register','--project',$script:nativeB,'--target','denied','--cli',$script:installedCli,'--catalog',(Join-Path $bad 'denied-e'),'--config',$blocked,'--msbuild',$script:msbuild)
        $lockPath = Join-Path (Split-Path -Parent $script:installConfig) ('.' + [IO.Path]::GetFileName($script:installConfig) + '.artest-register.lock')
        $lock = [IO.File]::Open($lockPath,[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)
        try { $null = Assert-KitFailure 'ARTESTREG005' @('register','--project',$script:nativeB,'--target','station','--msbuild',$script:msbuild) } finally {$lock.Dispose()}
        if ([Convert]::ToBase64String($before) -cne [Convert]::ToBase64String([IO.File]::ReadAllBytes($registry))) { throw 'Failed target changed the selected installation.' }
        'no elevation, fallback target, or Engine replacement occurred'
    }

    Invoke-Case 'Failed and interrupted publication preserve the complete prior catalog' {
        $header = Join-Path $script:nativeA 'SimulatedValueSource.h'; (Get-Content -LiteralPath $header -Raw).Replace('43.0','44.0') | Set-Content -LiteralPath $header -Encoding utf8
        $before = Get-TreeDigest $script:installCatalog
        $statePath = Join-Path $script:installConfig 'registrations.json'
        $mappingPath = Join-Path $script:installConfig 'python-environments.json'
        $registryPath = Join-Path $env:ARTEST_SDK_CONFIG_ROOT 'installations.json'
        $beforeState = [Convert]::ToBase64String([IO.File]::ReadAllBytes($statePath))
        $beforeMapping = [Convert]::ToBase64String([IO.File]::ReadAllBytes($mappingPath))
        $beforeRegistry = [Convert]::ToBase64String([IO.File]::ReadAllBytes($registryPath))
        foreach ($point in @('before-promotion','after-backup','after-state','profile-save')) {
            $env:ARTEST_SDK_REGISTER_FAILPOINT=$point
            try { $null = Assert-KitFailure 'ARTESTREG009' @('register','--project',$script:nativeA,'--target','station','--msbuild',$script:msbuild) }
            finally { Remove-Item Env:ARTEST_SDK_REGISTER_FAILPOINT -ErrorAction SilentlyContinue }
            if ((Get-TreeDigest $script:installCatalog) -ne $before) { throw "$point changed the prior catalog." }
            if ([Convert]::ToBase64String([IO.File]::ReadAllBytes($statePath)) -cne $beforeState -or
                [Convert]::ToBase64String([IO.File]::ReadAllBytes($mappingPath)) -cne $beforeMapping -or
                [Convert]::ToBase64String([IO.File]::ReadAllBytes($registryPath)) -cne $beforeRegistry) {
                throw "$point changed state, Python mappings, or the selected installation."
            }
            Assert-InstalledDiscovery
        }
        'publication and profile-save failures restored the complete prior usable selection'
    }

    Invoke-Case 'Active journal serialization, real interruption recovery, and hostile journals' {
        $header = Join-Path $script:nativeA 'SimulatedValueSource.h'
        (Get-Content -LiteralPath $header -Raw).Replace('44.0','45.0') | Set-Content -LiteralPath $header -Encoding utf8
        $marker = Join-Path $testRoot 'publisher-after-backup.marker'
        $journalPath = Join-Path $script:installConfig '.artest-register-transaction.json'
        $registerArguments = Get-RegisterArguments $script:nativeA
        $pwsh = Join-Path $PSHOME 'pwsh.exe'
        $childArguments = @('-NoProfile','-NonInteractive','-File',$script:entry) + $registerArguments
        Add-Command $pwsh $childArguments
        $start = [Diagnostics.ProcessStartInfo]::new()
        $start.FileName = $pwsh; $start.UseShellExecute = $false; $start.CreateNoWindow = $true
        foreach ($argument in $childArguments) { $null = $start.ArgumentList.Add($argument) }
        $start.Environment['ARTEST_SDK_CONFIG_ROOT'] = $env:ARTEST_SDK_CONFIG_ROOT
        $start.Environment['ARTEST_SDK_REGISTER_FAILPOINT'] = 'after-backup-wait'
        $start.Environment['ARTEST_SDK_REGISTER_FAILPOINT_MARKER'] = $marker
        $publisher = [Diagnostics.Process]::Start($start)
        try {
            $watch = [Diagnostics.Stopwatch]::StartNew()
            while (-not (Test-Path -LiteralPath $marker -PathType Leaf) -and -not $publisher.HasExited -and $watch.ElapsedMilliseconds -lt 60000) {
                Start-Sleep -Milliseconds 100
            }
            if (-not (Test-Path -LiteralPath $marker -PathType Leaf) -or $publisher.HasExited) { throw 'Publisher did not reach the active-journal failpoint.' }
            $activeJournal = [IO.File]::ReadAllBytes($journalPath)
            $validJournalText = [Text.Encoding]::UTF8.GetString($activeJournal)
            $null = Assert-KitFailure 'ARTESTREG005' $registerArguments
            if ([Convert]::ToBase64String($activeJournal) -cne [Convert]::ToBase64String([IO.File]::ReadAllBytes($journalPath))) {
                throw 'Concurrent registration modified the active journal.'
            }
            $publisher.Kill($true)
            if (-not $publisher.WaitForExit(5000)) { throw 'Publisher termination was not confirmed by the test harness.' }
        }
        finally {
            if (-not $publisher.HasExited) {
                try { $publisher.Kill($true) } catch { }
                $null = $publisher.WaitForExit(5000)
            }
            $publisher.Dispose()
        }
        $recoveredOutput = Invoke-Kit $registerArguments
        if (($recoveredOutput | Out-String) -notmatch 'ARTESTREG_RECOVERED') { throw 'A new invocation did not report recovery of the interrupted publisher.' }
        $recovered = ConvertFrom-LastJson $recoveredOutput
        if ($recovered.reused) { throw 'Recovered changed source was incorrectly reported as reused.' }
        Assert-InstalledDiscovery
        $catalogAfterRecovery = Get-TreeDigest $script:installCatalog

        [IO.File]::WriteAllText($journalPath,'{ corrupt journal')
        $corruptBytes = [IO.File]::ReadAllBytes($journalPath)
        $null = Assert-KitFailure 'ARTESTREG007' $registerArguments
        if (-not (Test-Path -LiteralPath $journalPath -PathType Leaf) -or
            [Convert]::ToBase64String($corruptBytes) -cne [Convert]::ToBase64String([IO.File]::ReadAllBytes($journalPath))) {
            throw 'Corrupt journal was not preserved byte-for-byte.'
        }
        Remove-Item -LiteralPath $journalPath -Force

        $foreignRoot = Join-Path $testRoot 'foreign-owned-journal-target'
        $null = New-Item -ItemType Directory -Path $foreignRoot
        $foreignSentinel = Join-Path $foreignRoot 'must-survive.txt'; Set-Content -LiteralPath $foreignSentinel -Value 'owned elsewhere'
        $foreignJournal = $validJournalText | ConvertFrom-Json
        $foreignJournal.candidate = $foreignRoot
        $foreignJournal | ConvertTo-Json -Depth 16 | Set-Content -LiteralPath $journalPath -Encoding utf8
        $null = Assert-KitFailure 'ARTESTREG007' $registerArguments
        if (-not (Test-Path -LiteralPath $journalPath -PathType Leaf) -or -not (Test-Path -LiteralPath $foreignSentinel -PathType Leaf)) {
            throw 'Foreign journal path was mutated or its journal was removed.'
        }
        Remove-Item -LiteralPath $journalPath -Force

        $reparseToken = [guid]::NewGuid().ToString('N')
        $catalogParent = Split-Path -Parent $script:installCatalog
        $catalogLeaf = [IO.Path]::GetFileName($script:installCatalog)
        $reparseCandidate = Join-Path $catalogParent ('.' + $catalogLeaf + ".artest-candidate.$reparseToken")
        $reparseBackup = Join-Path $catalogParent ('.' + $catalogLeaf + ".artest-backup.$reparseToken")
        $null = New-Item -ItemType Junction -Path $reparseCandidate -Target $foreignRoot
        try {
            $reparseJournal = $validJournalText | ConvertFrom-Json
            $reparseJournal.token = $reparseToken; $reparseJournal.phase = 'prepared'
            $reparseJournal.candidate = $reparseCandidate; $reparseJournal.backup = $reparseBackup
            $reparseJournal | ConvertTo-Json -Depth 16 | Set-Content -LiteralPath $journalPath -Encoding utf8
            $null = Assert-KitFailure 'ARTESTREG007' $registerArguments
            if (-not (Test-Path -LiteralPath $journalPath -PathType Leaf) -or -not (Test-Path -LiteralPath $foreignSentinel -PathType Leaf)) {
                throw 'Reparse-point journal escaped ownership or was removed.'
            }
        }
        finally {
            if (Test-Path -LiteralPath $journalPath) { Remove-Item -LiteralPath $journalPath -Force }
            if (Test-Path -LiteralPath $reparseCandidate) { Remove-Item -LiteralPath $reparseCandidate -Force }
        }
        if ((Get-TreeDigest $script:installCatalog) -ne $catalogAfterRecovery) { throw 'Hostile journal checks changed the recovered catalog.' }
        Assert-InstalledDiscovery
        'active writer excluded, killed publisher recovered on a new invocation, and corrupt/foreign/reparse journals failed closed'
    }

    Invoke-Case 'Registration process timeout is bounded and reports unconfirmed termination' {
        $probe = Join-Path $testRoot 'registration-process-probe.ps1'
        @'
param([string]$Registration,[string]$Pwsh)
$ErrorActionPreference = 'Stop'
function Stop-Kit { param([string]$Code,[string]$Message) throw "$Code $Message" }
. $Registration
$watch = [Diagnostics.Stopwatch]::StartNew()
try {
    $null = Invoke-RegistrationProcess -Executable $Pwsh -Arguments @('-NoProfile','-NonInteractive','-Command','Start-Sleep -Milliseconds 800') -Operation 'Bounded probe' -TimeoutMilliseconds 20 -CleanupMilliseconds 20 -TestSkipTermination
}
catch {
    $watch.Stop()
    if ($_.Exception.Message -notmatch 'ARTESTREG010' -or $_.Exception.Message -notmatch 'termination was not confirmed' -or $watch.ElapsedMilliseconds -gt 2000) { throw }
    Write-Output $_.Exception.Message
    exit 0
}
throw 'Expected the bounded process probe to time out.'
'@ | Set-Content -LiteralPath $probe -Encoding utf8
        $arguments = @('-NoProfile','-NonInteractive','-File',$probe,(Join-Path $script:kit 'registration.ps1'),(Join-Path $PSHOME 'pwsh.exe'))
        Add-Command (Join-Path $PSHOME 'pwsh.exe') $arguments
        $output = & (Join-Path $PSHOME 'pwsh.exe') @arguments 2>&1
        if ($LASTEXITCODE -ne 0 -or ($output | Out-String) -notmatch 'ARTESTREG010') { throw "Bounded process probe failed: $($output | Out-String)" }
        'timeout returned promptly and did not claim that termination completed'
    }

    Invoke-Case 'Installed target remains discoverable without either source project' {
        Remove-Item -LiteralPath $script:pythonProject -Recurse -Force
        Remove-Item -LiteralPath $script:nativeA -Recurse -Force
        Remove-Item -LiteralPath $script:nativeB -Recurse -Force
        Assert-InstalledDiscovery
        $mapping = Get-Content -LiteralPath (Join-Path $script:installConfig 'python-environments.json') -Raw | ConvertFrom-Json
        foreach ($property in $mapping.PSObject.Properties) {
            if (-not (Test-Path -LiteralPath $property.Value -PathType Leaf)) { throw 'Installed Python receipt is unavailable.' }
        }
        'catalog and Python receipt remain installation-owned and source-independent'
    }
}
catch { $fatal=$_ }
finally {
    $preservedArchive = Join-Path $evidence ([IO.Path]::GetFileName($archive))
    if (Test-Path -LiteralPath $archive -PathType Leaf) {
        Copy-Item -LiteralPath $archive -Destination $preservedArchive
    }
    $sourcePaths = @(
        'docs/sdk/development-kit.md','scripts/package-development-kit.ps1','scripts/test-development-kit-stage4c.ps1',
        'scripts/verify-development-kit.ps1','source/ARTest.Python/tools/project.py','source/ARTest.Python/tests/test_project.py'
    ) + @(Get-ChildItem -LiteralPath (Join-Path $repositoryRoot 'source\ARTest.SDK\development-kit') -File | ForEach-Object {[IO.Path]::GetRelativePath($repositoryRoot,$_.FullName).Replace('\','/')})
    $candidate = [ordered]@{
        schema='artest.schema.py-dx-01-stage4c-evidence.v1';runId=$runId;createdAtUtc=(Get-Date).ToUniversalTime().ToString('o')
        scope='PY-DX-01 Stage 4C register only';kitVersion=$version.kitVersion;configuration=$Configuration;platform=$Platform
        gitHead=(& git -C $repositoryRoot rev-parse HEAD | Out-String).Trim();gitStatus=@(& git -C $repositoryRoot status --short)
        inputs=[ordered]@{pythonRuntime=[IO.Path]::GetFullPath($PythonRuntime);dependencyWheelRoot=[IO.Path]::GetFullPath($DependencyWheelRoot);visualStudioPath=[IO.Path]::GetFullPath($VisualStudioPath)}
        archive=$preservedArchive;archiveSha256=if(Test-Path -LiteralPath $preservedArchive){(Get-FileHash -LiteralPath $preservedArchive -Algorithm SHA256).Hash.ToLowerInvariant()}else{$null}
        sourceFiles=@($sourcePaths | Sort-Object -Unique | ForEach-Object {$p=Join-Path $repositoryRoot $_;[ordered]@{path=$_.Replace('\','/');sha256=(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant()}})
        externalTestRoot=$testRoot;externalTestRootRemoved=$false;cases=@($results)
        limitations=@('Registration affects subsequent CLI activations only; live sessions are not reloaded.','Stage 4 run, Stage 5, .NET, hardware, public publishing, C-03 and C-04 remain out of scope.')
    }
    $candidatePath=Join-Path $evidence 'candidate.json'; $candidate|ConvertTo-Json -Depth 16|Set-Content -LiteralPath $candidatePath -Encoding utf8
    $commands|Set-Content -LiteralPath (Join-Path $evidence 'commands.txt') -Encoding utf8
    if (Test-Path -LiteralPath $testRoot) {
        if ($testRoot -notmatch 'ARTest 4C [0-9a-f]{8}$') { if(-not $fatal){$fatal=[InvalidOperationException]::new("Unsafe cleanup target: $testRoot")} }
        else { Remove-Item -LiteralPath $testRoot -Recurse -Force; $candidate.externalTestRootRemoved=$true; $candidate|ConvertTo-Json -Depth 16|Set-Content -LiteralPath $candidatePath -Encoding utf8 }
    }
    Get-ChildItem -LiteralPath $evidence -File|Sort-Object Name|ForEach-Object{"$((Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant())  $($_.Name)"}|Set-Content -LiteralPath (Join-Path $evidence 'SHA256SUMS.txt') -Encoding ascii
}
if($fatal){Write-Error "Stage 4C tests failed. Evidence: $evidence`n$($fatal.Exception.ToString())";exit 1}
Write-Host 'PY-DX-01 Stage 4C development-kit tests: PASSED'; Write-Host "Evidence: $evidence"
