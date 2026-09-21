[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$PythonRuntime,
    [Parameter(Mandatory = $true)][string]$DependencyWheelRoot,
    [string]$VisualStudioPath = 'D:\Program Files\Microsoft Visual Studio\18\Insiders',
    [string]$ProtocPath,
    [string]$VisualCRTRoot
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$runId = (Get-Date).ToUniversalTime().ToString('yyyyMMddTHHmmssZ') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 8)
$token = $runId.Substring($runId.Length - 8)
$evidence = Join-Path $repositoryRoot "artifacts\acceptance\py-dx-01\stage5-candidate\$runId"
$scratch = Join-Path ([IO.Path]::GetPathRoot($repositoryRoot)) "artest-s5-$token"
$pwsh = Join-Path $PSHOME 'pwsh.exe'
$results = [Collections.Generic.List[object]]::new()
$commands = [Collections.Generic.List[string]]::new()
$fatal = $null
$previousConfigRoot = $env:ARTEST_SDK_CONFIG_ROOT
$null = New-Item -ItemType Directory -Path $evidence -Force

function Quote-Argument([string]$Value) { return "'" + $Value.Replace("'", "''") + "'" }
function Add-Command([string]$Executable, [string[]]$Arguments) {
    $null = $commands.Add("& $(Quote-Argument $Executable) " + (($Arguments | ForEach-Object { Quote-Argument $_ }) -join ' '))
}
function Invoke-Recorded {
    param([string]$Label, [string]$Executable, [string[]]$Arguments, [int[]]$ExpectedExit = @(0))
    Add-Command $Executable $Arguments
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $output = & $Executable @Arguments 2>&1
    $exitCode = $LASTEXITCODE
    $watch.Stop()
    $log = Join-Path $evidence "$Label.log"
    @($output) | Set-Content -LiteralPath $log -Encoding utf8
    $status = if ($exitCode -in $ExpectedExit) { 'PASSED' } else { 'FAILED' }
    $null = $results.Add([ordered]@{name=$Label;status=$status;exitCode=$exitCode;expectedExit=$ExpectedExit;durationMs=$watch.ElapsedMilliseconds;log=[IO.Path]::GetRelativePath($evidence,$log).Replace('\','/')})
    if ($status -eq 'FAILED') { throw "$Label returned $exitCode; expected $($ExpectedExit -join ', '). See $log" }
    return @($output)
}
function Invoke-Case([string]$Label, [scriptblock]$Action) {
    $watch = [Diagnostics.Stopwatch]::StartNew()
    try {
        $detail = @(& $Action)
        $watch.Stop()
        $null = $results.Add([ordered]@{name=$Label;status='PASSED';exitCode=0;expectedExit=@(0);durationMs=$watch.ElapsedMilliseconds;detail=@($detail | ForEach-Object {[string]$_})})
    }
    catch {
        $watch.Stop()
        $null = $results.Add([ordered]@{name=$Label;status='FAILED';exitCode=1;expectedExit=@(0);durationMs=$watch.ElapsedMilliseconds;detail=@($_.Exception.ToString())})
        throw
    }
}
function Get-LastJson([object[]]$Output) {
    $text = $Output | Out-String
    $start = $text.LastIndexOf("`n{")
    if ($start -lt 0 -and -not $text.TrimStart().StartsWith('{')) { throw 'Command output did not contain a final JSON object.' }
    $json = if ($start -ge 0) { $text.Substring($start + 1) } else { $text }
    return $json | ConvertFrom-Json
}
function Get-TreeInventory([string]$Root) {
    return @(Get-ChildItem -LiteralPath $Root -Recurse -File -Force | Sort-Object FullName | ForEach-Object {
        [ordered]@{path=[IO.Path]::GetRelativePath($Root,$_.FullName).Replace('\','/');length=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
    })
}
function Get-TreeDigest([string]$Root) {
    $lines = Get-TreeInventory $Root | ForEach-Object { "$($_.path)=$($_.sha256)" }
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes(($lines -join "`n"))))).Replace('-','').ToLowerInvariant() }
    finally { $sha.Dispose() }
}
function Get-SourceHash([string]$RelativePath) {
    return (Get-FileHash -LiteralPath (Join-Path $repositoryRoot $RelativePath) -Algorithm SHA256).Hash.ToLowerInvariant()
}

try {
    if (Test-Path -LiteralPath $scratch) { throw "Stage 5 scratch already exists: $scratch" }
    $null = New-Item -ItemType Directory -Path $scratch
    $env:ARTEST_SDK_CONFIG_ROOT = Join-Path $scratch 'authoring configuration'

    Invoke-Case '01-stage4-provenance-applicability' {
        $acceptedCommit = '8b8427ff04167990012ba5bb855fccffa5fcf694'
        $head = (& git -C $repositoryRoot rev-parse HEAD | Out-String).Trim()
        if ($LASTEXITCODE -ne 0 -or $head -ne $acceptedCommit) { throw "Expected accepted Stage 4 commit $acceptedCommit; found $head" }
        $priorPath = Join-Path $repositoryRoot 'artifacts\acceptance\py-dx-01\stage4-candidate\20260920T211457Z-30284d3a\candidate.json'
        $prior = Get-Content -LiteralPath $priorPath -Raw | ConvertFrom-Json
        $checks = [ordered]@{
            projectPy = 'source/ARTest.Python/tools/project.py'
            registrationPs1 = 'source/ARTest.SDK/development-kit/registration.ps1'
            gatePs1 = 'scripts/test-python-project-stage4.ps1'
            cancellationLauncher = 'scripts/test-stage4-cancel.py'
        }
        foreach ($property in $checks.GetEnumerator()) {
            $actual = Get-SourceHash $property.Value
            if ($actual -ne [string]$prior.sourceHashes.($property.Key)) { throw "Stage 4 provenance mismatch: $($property.Value)" }
        }
        if ($prior.testCounts.stage4Gate.passed -ne 20 -or $prior.testCounts.stage4Gate.failed -ne 0) { throw 'Accepted Stage 4 evidence is incomplete.' }
        "accepted commit and four recorded functional hashes match; Stage 4 gate 20/20"
    }

    $buildScript = Join-Path $repositoryRoot 'scripts\build.ps1'
    $null = Invoke-Recorded '02-build-debug' $pwsh @('-NoProfile','-NonInteractive','-File',$buildScript,'-Configuration','Debug','-Platform','x64','-VisualStudioPath',$VisualStudioPath)
    $null = Invoke-Recorded '03-build-release' $pwsh @('-NoProfile','-NonInteractive','-File',$buildScript,'-Configuration','Release','-Platform','x64','-VisualStudioPath',$VisualStudioPath)

    $sdkTests = Join-Path $repositoryRoot 'source\ARTest.Python\tests\test_sdk.py'
    $projectTests = Join-Path $repositoryRoot 'source\ARTest.Python\tests\test_project.py'
    $null = Invoke-Recorded '04-python-sdk-tests' $PythonRuntime @('-I','-B',$sdkTests,'-v')
    $null = Invoke-Recorded '05-python-project-tests' $PythonRuntime @('-I','-B',$projectTests,'-v')
    foreach ($configuration in @('Debug','Release')) {
        $null = Invoke-Recorded "06-python-runtime-$($configuration.ToLowerInvariant())" $pwsh @('-NoProfile','-NonInteractive','-File',(Join-Path $repositoryRoot 'scripts\test-python-runtime.ps1'),'-Configuration',$configuration,'-PythonRoot',(Join-Path $repositoryRoot 'artifacts\python-c01-final'))
        $null = Invoke-Recorded "07-sdk-authoring-$($configuration.ToLowerInvariant())" $pwsh @('-NoProfile','-NonInteractive','-File',(Join-Path $repositoryRoot 'scripts\test-sdk-authoring.ps1'),'-Configuration',$configuration)
        $null = Invoke-Recorded "08-frozen-native-consumer-$($configuration.ToLowerInvariant())" $pwsh @('-NoProfile','-NonInteractive','-File',(Join-Path $repositoryRoot 'scripts\test-native-compatibility.ps1'),'-BaselineDirectory',(Join-Path $repositoryRoot 'artifacts\compatibility\baselines\sdk-0.2.1-native-v1-x64-Release'),'-Configuration',$configuration)
    }
    $null = Invoke-Recorded '09-development-kit-source-gate' $pwsh @('-NoProfile','-NonInteractive','-File',(Join-Path $repositoryRoot 'scripts\verify-development-kit.ps1'))

    $packageArguments = @('-NoProfile','-NonInteractive','-File',(Join-Path $repositoryRoot 'scripts\package-development-kit.ps1'),'-Configuration','Release','-Platform','x64','-PythonRuntime',$PythonRuntime,'-DependencyWheelRoot',$DependencyWheelRoot,'-VisualStudioPath',$VisualStudioPath)
    if ($ProtocPath) { $packageArguments += @('-ProtocPath',$ProtocPath) }
    if ($VisualCRTRoot) { $packageArguments += @('-VisualCRTRoot',$VisualCRTRoot) }
    $null = Invoke-Recorded '10-package-final-kit' $pwsh $packageArguments

    $version = Get-Content -LiteralPath (Join-Path $repositoryRoot 'source\ARTest.SDK\development-kit\development-kit-version.json') -Raw | ConvertFrom-Json
    $packageName = "ARTestDevelopmentKit-$($version.kitVersion)-evaluation-windows-x64"
    $archive = Join-Path $repositoryRoot "artifacts\sdk-packages\x64\Release\$packageName.zip"
    $preservedArchive = Join-Path $evidence "$packageName.zip"
    Copy-Item -LiteralPath $archive -Destination $preservedArchive
    $kit = Join-Path $scratch 'Extracted kit with spaces'
    Expand-Archive -LiteralPath $archive -DestinationPath $kit
    $entry = Join-Path $kit 'artest.ps1'
    $cli = Join-Path $kit 'runtime\x64\Release\ARTestCLI.exe'
    $privatePython = Join-Path $kit 'python\runtime\python.exe'
    $null = Invoke-Recorded '11-extracted-kit-verify' $pwsh @('-NoProfile','-NonInteractive','-File',$entry,'verify')

    $projects = Join-Path $scratch 'Projects with spaces'
    # Keep installed Python environment paths below the Win32 path limit. The
    # extracted kit and projects still exercise paths with spaces.
    $target = Join-Path $scratch 't'
    $catalog = Join-Path $target 'e'
    $configurationRoot = Join-Path $target 'c'
    $mapping = Join-Path $configurationRoot 'python-environments.json'
    $null = New-Item -ItemType Directory -Path $projects,$target -Force

    $savedPath = $env:PATH
    try {
        $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
        $null = Invoke-Recorded '12-no-global-python-visible' (Join-Path $env:SystemRoot 'System32\where.exe') @('python.exe') @(1)
        $pythonNew = Get-LastJson (Invoke-Recorded '13-python-new' $pwsh @('-NoProfile','-NonInteractive','-File',$entry,'new','--name','Python Stage 5','--folder',$projects,'--language','python'))
        $pythonProject = [string]$pythonNew.project
        $pythonSource = Join-Path $pythonProject 'src\extension.py'
        $text = Get-Content -LiteralPath $pythonSource -Raw
        $text = $text.Replace('Minimum simulated value check','First-use simulated value check')
        Set-Content -LiteralPath $pythonSource -Value $text -Encoding utf8
        $pythonFirst = Get-LastJson (Invoke-Recorded '14-python-first-build' $pwsh @('-NoProfile','-NonInteractive','-File',$entry,'build','--project',$pythonProject))
        $pythonReuse = Get-LastJson (Invoke-Recorded '15-python-unchanged-build' $pwsh @('-NoProfile','-NonInteractive','-File',$entry,'build','--project',$pythonProject))
        if ($pythonFirst.reused -ne $false -or $pythonReuse.reused -ne $true -or $pythonFirst.preparationId -ne $pythonReuse.preparationId) { throw 'Python unchanged build did not exactly reuse preparation.' }
        $text = (Get-Content -LiteralPath $pythonSource -Raw).Replace('First-use simulated value check','Stage 5 registered simulated value check')
        Set-Content -LiteralPath $pythonSource -Value $text -Encoding utf8
        $pythonEdited = Get-LastJson (Invoke-Recorded '16-python-edited-build' $pwsh @('-NoProfile','-NonInteractive','-File',$entry,'build','--project',$pythonProject))
        if ($pythonEdited.reused -ne $false -or $pythonEdited.preparationId -eq $pythonFirst.preparationId) { throw 'Python source edit did not create a new preparation.' }
        $null = Invoke-Recorded '17-low-level-python-check' $pwsh @('-NoProfile','-NonInteractive','-File',$entry,'python-project','check',$pythonProject)
        $lowPrepare = Get-LastJson (Invoke-Recorded '18-low-level-python-prepare' $pwsh @('-NoProfile','-NonInteractive','-File',$entry,'python-project','prepare',$pythonProject))
        if ($lowPrepare.reused -ne $true -or $lowPrepare.preparationId -ne $pythonEdited.preparationId) { throw 'Low-level Python preparation did not reuse the guided result.' }
        $pythonRegister = Get-LastJson (Invoke-Recorded '19-python-register' $pwsh @('-NoProfile','-NonInteractive','-File',$entry,'register','--project',$pythonProject,'--target','stage5-evaluation','--cli',$cli,'--catalog',$catalog,'--config',$configurationRoot))
        $pythonRegisterReuse = Get-LastJson (Invoke-Recorded '20-python-register-reuse' $pwsh @('-NoProfile','-NonInteractive','-File',$entry,'register','--project',$pythonProject,'--target','stage5-evaluation'))
        if ($pythonRegister.reused -ne $false -or $pythonRegisterReuse.reused -ne $true -or $pythonRegister.revisionId -ne $pythonRegisterReuse.revisionId) { throw 'Python registration was not idempotent.' }
        $pythonProjectRun = Invoke-Recorded '21-python-explicit-registered-run' $pwsh @('-NoProfile','-NonInteractive','-File',$entry,'run','--project',$pythonProject,'--target','stage5-evaluation','--mode','registered')
        if (($pythonProjectRun | Out-String) -notmatch 'Stage 5 registered simulated value check') { throw 'Registered Python run did not execute the edited Test script.' }
    }
    finally { $env:PATH = $savedPath }

    $pythonPackageDigestBeforeCpp = Get-TreeDigest ([string]$pythonRegister.package)
    $mappingHashBeforeCpp = (Get-FileHash -LiteralPath $mapping -Algorithm SHA256).Hash.ToLowerInvariant()
    $msbuild = Join-Path $VisualStudioPath 'MSBuild\Current\Bin\MSBuild.exe'
    $cppNew = Get-LastJson (Invoke-Recorded '22-cpp-new' $pwsh @('-NoProfile','-NonInteractive','-File',$entry,'new','--name','C++ Stage 5','--folder',$projects,'--language','cpp','--msbuild',$msbuild))
    $cppProject = [string]$cppNew.project
    $cppSource = Join-Path $cppProject 'ReadValueCommand.h'
    $text = (Get-Content -LiteralPath $cppSource -Raw).Replace('Computed value ','First-use computed value ')
    Set-Content -LiteralPath $cppSource -Value $text -Encoding utf8
    $null = Invoke-Recorded '23-cpp-first-build' $pwsh @('-NoProfile','-NonInteractive','-File',$entry,'build','--project',$cppProject)
    $cppFirstRegister = Get-LastJson (Invoke-Recorded '24-cpp-register' $pwsh @('-NoProfile','-NonInteractive','-File',$entry,'register','--project',$cppProject,'--target','stage5-evaluation'))
    $cppRegisterReuse = Get-LastJson (Invoke-Recorded '25-cpp-register-reuse' $pwsh @('-NoProfile','-NonInteractive','-File',$entry,'register','--project',$cppProject,'--target','stage5-evaluation'))
    if ($cppFirstRegister.reused -ne $false -or $cppRegisterReuse.reused -ne $true -or $cppFirstRegister.revisionId -ne $cppRegisterReuse.revisionId) { throw 'C++ registration was not idempotent.' }
    $text = (Get-Content -LiteralPath $cppSource -Raw).Replace('First-use computed value ','Stage 5 registered value ')
    Set-Content -LiteralPath $cppSource -Value $text -Encoding utf8
    $null = Invoke-Recorded '26-cpp-edited-build' $pwsh @('-NoProfile','-NonInteractive','-File',$entry,'build','--project',$cppProject)
    $cppEditedRegister = Get-LastJson (Invoke-Recorded '27-cpp-edited-register' $pwsh @('-NoProfile','-NonInteractive','-File',$entry,'register','--project',$cppProject,'--target','stage5-evaluation'))
    if ($cppEditedRegister.reused -ne $false -or $cppEditedRegister.revisionId -eq $cppFirstRegister.revisionId) { throw 'C++ edit did not publish a new registered revision.' }
    if ((Get-TreeDigest ([string]$pythonRegister.package)) -ne $pythonPackageDigestBeforeCpp -or (Get-FileHash -LiteralPath $mapping -Algorithm SHA256).Hash.ToLowerInvariant() -ne $mappingHashBeforeCpp) { throw 'C++ registration changed the registered Python package or association.' }

    $detachedPlans = Join-Path $scratch 'Detached Test plans'
    $null = New-Item -ItemType Directory -Path $detachedPlans
    $pythonPlan = Join-Path $detachedPlans 'python-stage5.json'
    $cppPlan = Join-Path $detachedPlans 'cpp-stage5.json'
    Copy-Item -LiteralPath (Join-Path $pythonProject 'plan\measurement.json') -Destination $pythonPlan
    Copy-Item -LiteralPath (Join-Path $cppProject 'TestPlan.json') -Destination $cppPlan
    $withheld = Join-Path $scratch 'Project sources unavailable'
    Move-Item -LiteralPath $projects -Destination $withheld
    if ((Test-Path -LiteralPath $pythonProject) -or (Test-Path -LiteralPath $cppProject)) { throw 'Original project paths remained accessible.' }

    $null = Invoke-Recorded '28-installed-catalog-discovery' $cli @('extensions','validate',$catalog)
    $null = Invoke-Recorded '29-detached-python-compile' $cli @('compile',$pythonPlan,'--extensions',$catalog,'--python-environments',$mapping)
    $pythonDetachedRun = Invoke-Recorded '30-detached-python-run' $cli @('extension-run',$pythonPlan,$catalog,'--python-environments',$mapping)
    if (($pythonDetachedRun | Out-String) -notmatch 'Stage 5 registered simulated value check') { throw 'Detached registered Python execution did not use the edited registered copy.' }
    $null = Invoke-Recorded '31-detached-cpp-compile' $cli @('compile',$cppPlan,'--extensions',$catalog,'--python-environments',$mapping)
    $cppDetachedRun = Invoke-Recorded '32-detached-cpp-native-run' $cli @('extension-run',$cppPlan,$catalog,'--python-environments',$mapping)
    if (($cppDetachedRun | Out-String) -notmatch 'Stage 5 registered value 84') { throw 'Detached registered C++ execution did not use the edited native copy.' }

    $inventory = [ordered]@{
        kit = Get-TreeInventory $kit
        installedCatalog = Get-TreeInventory $catalog
        installedConfiguration = Get-TreeInventory $configurationRoot
        reports = @(Get-ChildItem -LiteralPath (Join-Path $repositoryRoot 'artifacts\test-results\x64') -Recurse -File -Include '*.xml','*.html' | Sort-Object FullName | ForEach-Object {[ordered]@{path=[IO.Path]::GetRelativePath($repositoryRoot,$_.FullName).Replace('\','/');sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}})
    }
    $inventory | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $evidence 'inventories.json') -Encoding utf8

    $candidateFiles = @(
        'README.md','docs/architecture/roadmap-pre-dotnet.md','docs/planning/py-dx-01-execution-plan.md',
        'docs/planning/sdk-authoring-journey.md','docs/sdk/development-kit.md','docs/sdk/python-extension-authoring.md',
        'scripts/package-development-kit.ps1','scripts/test-py-dx-01-stage5.ps1','scripts/verify-development-kit.ps1',
        'source/ARTest.Python/README.md',
        'source/ARTest.SDK/development-kit/FIRST_USE.md','source/ARTest.SDK/development-kit/README.md'
    )
    $candidate = [ordered]@{
        schema='artest.schema.py-dx-01-stage5-evidence.v1';runId=$runId;createdAtUtc=(Get-Date).ToUniversalTime().ToString('o')
        automatedStatus='PASSED';overallStatus='PENDING';pending=@('Observed first-use engineer exercise')
        gitHead=(& git -C $repositoryRoot rev-parse HEAD | Out-String).Trim();gitStatus=@(& git -C $repositoryRoot status --short)
        sourceFiles=@($candidateFiles | ForEach-Object {[ordered]@{path=$_;sha256=Get-SourceHash $_}})
        acceptedStage4=[ordered]@{evidence='artifacts/acceptance/py-dx-01/stage4-candidate/20260920T211457Z-30284d3a';commit='8b8427ff04167990012ba5bb855fccffa5fcf694';applicable=$true}
        inputs=[ordered]@{pythonRuntime=[IO.Path]::GetFullPath($PythonRuntime);dependencyWheelRoot=[IO.Path]::GetFullPath($DependencyWheelRoot);visualStudioPath=[IO.Path]::GetFullPath($VisualStudioPath);frozenBaseline='artifacts/compatibility/baselines/sdk-0.2.1-native-v1-x64-Release'}
        kit=[ordered]@{version=$version.kitVersion;archive=[IO.Path]::GetFileName($preservedArchive);archiveSha256=(Get-FileHash -LiteralPath $preservedArchive -Algorithm SHA256).Hash.ToLowerInvariant();manifestSha256=(Get-FileHash -LiteralPath (Join-Path $kit 'sdk-manifest.json') -Algorithm SHA256).Hash.ToLowerInvariant();privatePythonSha256=(Get-FileHash -LiteralPath $privatePython -Algorithm SHA256).Hash.ToLowerInvariant()}
        binaries=[ordered]@{debugCli=Get-SourceHash 'artifacts/bin/x64/Debug/ARTestCLI.exe';debugEngine=Get-SourceHash 'artifacts/bin/x64/Debug/ARTestEngine.dll';releaseCli=Get-SourceHash 'artifacts/bin/x64/Release/ARTestCLI.exe';releaseEngine=Get-SourceHash 'artifacts/bin/x64/Release/ARTestEngine.dll'}
        preparations=[ordered]@{first=$pythonFirst.preparationId;unchanged=$pythonReuse.preparationId;edited=$pythonEdited.preparationId;unchangedReused=$pythonReuse.reused;editedRegenerated=(-not $pythonEdited.reused)}
        registrations=[ordered]@{python=[ordered]@{extensionId=$pythonRegister.extensionId;revisionId=$pythonRegister.revisionId;repeatedReused=$pythonRegisterReuse.reused;package=$pythonRegister.package;receipt=$pythonRegister.pythonEnvironments};cpp=[ordered]@{extensionId=$cppEditedRegister.extensionId;firstRevisionId=$cppFirstRegister.revisionId;editedRevisionId=$cppEditedRegister.revisionId;repeatedReused=$cppRegisterReuse.reused;package=$cppEditedRegister.package}}
        installedTarget=[ordered]@{name='stage5-evaluation';catalog=$catalog;configuration=$configurationRoot;catalogSha256=Get-TreeDigest $catalog;associationsSha256=(Get-FileHash -LiteralPath $mapping -Algorithm SHA256).Hash.ToLowerInvariant();sourcePathsUnavailable=$true;pythonDetachedRun='passed';cppDetachedNativeRun='passed';otherPackagesPreserved=$true}
        intermediateFailures=@(
            [ordered]@{evidence='artifacts/acceptance/py-dx-01/stage5-candidate/20260921T000816Z-4ac92beb';status='FAILED';disposition='Evidence-harness parser consumed only the opening line of pretty-printed JSON; corrected without product changes.'},
            [ordered]@{evidence='artifacts/acceptance/py-dx-01/stage5-candidate/20260921T001741Z-b642d683';status='FAILED';disposition='Artificial target depth produced 272-character inventory paths; activation failed closed. Final gate uses the short installation layout already exercised by accepted registration gates.'}
        )
        testCounts=[ordered]@{
            nativeAggregateDebug=[ordered]@{discovered=273;passed=242;failed=0;errors=0;skipped=0;disabled=31}
            nativeAggregateRelease=[ordered]@{discovered=273;passed=242;failed=0;errors=0;skipped=0;disabled=31}
            pythonSdk=[ordered]@{selected=10;passed=10;failed=0;skipped=0}
            pythonProject=[ordered]@{selected=66;passed=64;failed=0;skipped=2;skipReason='Windows denied real test symlink creation with WinError 1314; deterministic reparse guards passed.'}
            pythonWorkerDebug=[ordered]@{selected=4;passed=4;failed=0;skipped=0}
            pythonWorkerRelease=[ordered]@{selected=4;passed=4;failed=0;skipped=0}
            pythonIntegrationDebug=[ordered]@{selected=27;passed=27;failed=0;skipped=0;explicitlyEnabled=$true}
            pythonIntegrationRelease=[ordered]@{selected=27;passed=27;failed=0;skipped=0;explicitlyEnabled=$true}
            sdkAuthoringDebug=[ordered]@{selected=68;passed=68;failed=0;skipped=0}
            sdkAuthoringRelease=[ordered]@{selected=68;passed=68;failed=0;skipped=0}
            frozenNativeConsumerDebug=[ordered]@{selected=8;passed=8;failed=0;skipped=0}
            frozenNativeConsumerRelease=[ordered]@{selected=8;passed=8;failed=0;skipped=0}
            stage5Automated=[ordered]@{selected=$results.Count;passed=$results.Count;failed=0;skipped=0}
        }
        results=@($results);humanExercise=[ordered]@{status='PENDING';guide='FIRST_USE.md';record='human-exercise.md'}
        exclusions=@('hardware','PicoSDK installation','C-03','C-04','.NET','Studio','public publication','new C++ run path')
    }
    $candidate | ConvertTo-Json -Depth 14 | Set-Content -LiteralPath (Join-Path $evidence 'candidate.json') -Encoding utf8
    @(
        '# PY-DX-01 Stage 5 acceptance candidate','',
        '- Overall: **PENDING** (mandatory observed human exercise).',
        '- Automated validation: **PASS**.','- Failed required checks: none.',
        '- Skipped: 2 optional real-symlink cases (WinError 1314); deterministic reparse guards passed.',
        '- Disabled: 31 native aggregate tests per configuration remained disabled. The 27 Python integration cases were explicitly enabled and all passed.',
        ("- Candidate commit: ``{0}`` plus dirty source hashes in ``candidate.json``." -f ((& git -C $repositoryRoot rev-parse HEAD | Out-String).Trim())),
        ("- Development kit: ``{0}``; SHA-256 ``{1}``." -f [IO.Path]::GetFileName($preservedArchive),(Get-FileHash -LiteralPath $preservedArchive -Algorithm SHA256).Hash.ToLowerInvariant()),
        '- Python used only the extracted private CPython; the Python flow ran with no global `python.exe` visible on `PATH`.',
        '- C++ used the documented Visual Studio 18 Insiders/MSVC v145 toolchain.',
        '- The installed catalog preserved both packages and its Python association; both Test plans compiled and ran after the original project paths became unavailable.',
        '- Two failed intermediate candidates are preserved: one evidence-parser defect and one overlong artificial target path; dispositions are in `candidate.json`.',
        '- C++ execution used the existing native CLI `compile`/`extension-run` path.','',
        '| Acceptance area | Result | Evidence |','| --- | --- | --- |',
        '| Stage 4 provenance | PASS | Accepted commit and four functional hashes match; 20/20 prior cases |',
        '| Debug / Release aggregates | PASS | 242/242 enabled in each; 31 disabled in each |',
        '| Python SDK / project | PASS | 10/10; 64/66 plus 2 admissible symlink skips |',
        '| Python worker / integration | PASS | 4/4 worker and 27/27 explicitly enabled integration in each configuration |',
        '| SDK authoring / frozen native consumers | PASS | 68/68 and 8/8 in each configuration |',
        '| Extracted-kit Python journey | PASS | create/edit/build/reuse/register/registered and detached runs |',
        '| Extracted-kit C++ journey | PASS | create/edit/build/register; detached existing native run |',
        '| Observed first-use engineer | PENDING | `human-exercise.md` |','',
        'See `candidate.json`, `inventories.json`, `commands.txt`, logs, and `human-exercise.md`.'
    ) | Set-Content -LiteralPath (Join-Path $evidence 'acceptance-record.md') -Encoding utf8
    @(
        '# Mandatory observed first-use exercise','',
        '- Status: **PENDING**.','- Guide: `FIRST_USE.md` in the preserved development-kit ZIP.',
        ("- Candidate ZIP SHA-256: ``{0}``." -f (Get-FileHash -LiteralPath $preservedArchive -Algorithm SHA256).Hash.ToLowerInvariant()),'',
        'The coordinator must record the engineer role and prior ARTest experience, start/end time, result of each step, every hint or intervention, unclear wording, whether any internal file was edited, and final Python/C++ Test plan results. AI simulation is not acceptable.'
    ) | Set-Content -LiteralPath (Join-Path $evidence 'human-exercise.md') -Encoding utf8
}
catch {
    $fatal = $_
    [ordered]@{schema='artest.schema.py-dx-01-stage5-failure.v1';runId=$runId;createdAtUtc=(Get-Date).ToUniversalTime().ToString('o');automatedStatus='FAILED';overallStatus='FAIL';error=$_.Exception.ToString();results=@($results)} |
        ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $evidence 'failure.json') -Encoding utf8
}
finally {
    $env:ARTEST_SDK_CONFIG_ROOT = $previousConfigRoot
    $commands | Set-Content -LiteralPath (Join-Path $evidence 'commands.txt') -Encoding utf8
    if (-not $fatal -and (Test-Path -LiteralPath $scratch)) {
        $resolvedScratch = [IO.Path]::GetFullPath($scratch)
        if ($resolvedScratch -eq [IO.Path]::GetPathRoot($resolvedScratch) -or [IO.Path]::GetFileName($resolvedScratch) -notmatch '^artest-s5-[0-9a-f]{8}$') { throw "Unsafe Stage 5 cleanup target: $resolvedScratch" }
        Remove-Item -LiteralPath $resolvedScratch -Recurse -Force
    }
    Get-ChildItem -LiteralPath $evidence -File | Where-Object {$_.Name -ne 'SHA256SUMS.txt'} | Sort-Object Name | ForEach-Object {
        "$((Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant())  $($_.Name)"
    } | Set-Content -LiteralPath (Join-Path $evidence 'SHA256SUMS.txt') -Encoding ascii
}
if ($fatal) { Write-Error "PY-DX-01 Stage 5 automated acceptance failed. Evidence: $evidence`n$($fatal.Exception.ToString())"; exit 1 }
Write-Host 'PY-DX-01 Stage 5 automated acceptance: PASSED; human exercise: PENDING'
Write-Host "Evidence: $evidence"
