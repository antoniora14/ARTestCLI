[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$PythonRuntime,
    [Parameter(Mandatory = $true)]
    [string]$DependencyWheelRoot,
    [string]$VisualStudioPath = 'D:\Program Files\Microsoft Visual Studio\18\Insiders',
    [string]$ProtocPath,
    [string]$VisualCRTRoot
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$runId = (Get-Date).ToUniversalTime().ToString('yyyyMMddTHHmmssZ') + '-' + [guid]::NewGuid().ToString('N').Substring(0,8)
$evidence = Join-Path $repositoryRoot "artifacts\acceptance\py-dx-01\stage4-candidate\$runId"
$scratch = Join-Path ([IO.Path]::GetPathRoot($repositoryRoot)) ("artest-s4-$($runId.Substring($runId.Length-8))")
$commands = [Collections.Generic.List[string]]::new()
$results = [Collections.Generic.List[object]]::new()
$previousConfigRoot = $env:ARTEST_SDK_CONFIG_ROOT

function Quote-Stage4Argument { param([string]$Value) return "'" + $Value.Replace("'", "''") + "'" }

function Invoke-Stage4Command {
    param([string]$Label,[string]$Executable,[string[]]$Arguments,[int[]]$ExpectedExit = @(0))
    $commands.Add("& $(Quote-Stage4Argument $Executable) " + (($Arguments | ForEach-Object { Quote-Stage4Argument $_ }) -join ' '))
    $output = & $Executable @Arguments 2>&1
    $exitCode = $LASTEXITCODE
    $log = Join-Path $evidence ($Label + '.log')
    @($output) | Set-Content -LiteralPath $log -Encoding utf8
    $results.Add([ordered]@{case=$Label;exitCode=$exitCode;expected=$ExpectedExit;log=[IO.Path]::GetRelativePath($evidence,$log).Replace('\','/')})
    if ($exitCode -notin $ExpectedExit) { throw "$Label returned $exitCode. See $log" }
    return @($output)
}

function Get-LastJson {
    param([object[]]$Output)
    $line = @($Output | ForEach-Object { [string]$_ } | Where-Object { $_.TrimStart().StartsWith('{') }) | Select-Object -Last 1
    if (-not $line) { throw 'The command produced no final JSON object.' }
    return $line | ConvertFrom-Json
}

function Get-FileDigestOrMissing {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return 'missing' }
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Get-TreeDigest {
    param([string]$Root)
    if (-not (Test-Path -LiteralPath $Root -PathType Container)) { return 'missing' }
    $lines = Get-ChildItem -LiteralPath $Root -Recurse -File -Force | Sort-Object FullName | ForEach-Object {
        "$([IO.Path]::GetRelativePath($Root,$_.FullName).Replace('\','/'))=$((Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant())"
    }
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($algorithm.ComputeHash([Text.Encoding]::UTF8.GetBytes(($lines -join "`n"))))).Replace('-','').ToLowerInvariant() }
    finally { $algorithm.Dispose() }
}

function Write-Stage4Json {
    param([string]$Path,[object]$Value)
    $Value | ConvertTo-Json -Depth 16 | Set-Content -LiteralPath $Path -Encoding utf8
}

if (-not ('Stage4ProcessGroup' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

public static class Stage4ProcessGroup
{
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct STARTUPINFO
    {
        public int cb; public string lpReserved; public string lpDesktop; public string lpTitle;
        public int dwX; public int dwY; public int dwXSize; public int dwYSize;
        public int dwXCountChars; public int dwYCountChars; public int dwFillAttribute;
        public int dwFlags; public short wShowWindow; public short cbReserved2;
        public IntPtr lpReserved2; public IntPtr hStdInput; public IntPtr hStdOutput; public IntPtr hStdError;
    }
    [StructLayout(LayoutKind.Sequential)]
    private struct PROCESS_INFORMATION
    { public IntPtr hProcess; public IntPtr hThread; public uint dwProcessId; public uint dwThreadId; }
    [StructLayout(LayoutKind.Sequential)]
    private struct SECURITY_ATTRIBUTES
    { public int nLength; public IntPtr lpSecurityDescriptor; [MarshalAs(UnmanagedType.Bool)] public bool bInheritHandle; }

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern bool CreateProcessW(string app, StringBuilder command, IntPtr processAttributes,
        IntPtr threadAttributes, bool inheritHandles, uint flags, IntPtr environment, string cwd,
        ref STARTUPINFO startup, out PROCESS_INFORMATION process);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern IntPtr CreateFileW(string name, uint access, uint share,
        ref SECURITY_ATTRIBUTES security, uint creation, uint flags, IntPtr template);
    [DllImport("kernel32.dll", SetLastError = true)] private static extern bool GenerateConsoleCtrlEvent(uint type, uint groupId);
    [DllImport("kernel32.dll", SetLastError = true)] private static extern uint WaitForSingleObject(IntPtr handle, uint milliseconds);
    [DllImport("kernel32.dll", SetLastError = true)] private static extern bool GetExitCodeProcess(IntPtr process, out uint exitCode);
    [DllImport("kernel32.dll")] private static extern bool CloseHandle(IntPtr handle);

    private static string Quote(string value)
    {
        if (value.Length != 0 && value.IndexOfAny(new[] {' ', '\t', '"'}) < 0) return value;
        var result = new StringBuilder("\""); int slashes = 0;
        foreach (char character in value)
        {
            if (character == '\\') { slashes++; continue; }
            if (character == '"') { result.Append('\\', slashes * 2 + 1).Append('"'); slashes = 0; continue; }
            result.Append('\\', slashes).Append(character); slashes = 0;
        }
        result.Append('\\', slashes * 2).Append('"'); return result.ToString();
    }

    public static int Run(string executable, string[] arguments, string logPath, string markerPath,
        int markerTimeoutMs, int exitTimeoutMs)
    {
        var security = new SECURITY_ATTRIBUTES { nLength = Marshal.SizeOf<SECURITY_ATTRIBUTES>(), bInheritHandle = true };
        IntPtr log = CreateFileW(logPath, 0x40000000, 3, ref security, 2, 0x80, IntPtr.Zero);
        if (log == new IntPtr(-1)) throw new Win32Exception(Marshal.GetLastWin32Error(), "Cannot create cancellation log");
        IntPtr input = CreateFileW("NUL", 0x80000000, 3, ref security, 3, 0x80, IntPtr.Zero);
        if (input == new IntPtr(-1)) { CloseHandle(log); throw new Win32Exception(Marshal.GetLastWin32Error(), "Cannot open NUL"); }
        var startup = new STARTUPINFO { cb = Marshal.SizeOf<STARTUPINFO>(), dwFlags = 0x100, hStdInput = input, hStdOutput = log, hStdError = log };
        PROCESS_INFORMATION process;
        var command = new StringBuilder(Quote(executable));
        foreach (string argument in arguments) command.Append(' ').Append(Quote(argument));
        try
        {
            if (!CreateProcessW(null, command, IntPtr.Zero, IntPtr.Zero, true, 0x200, IntPtr.Zero,
                Directory.GetCurrentDirectory(), ref startup, out process))
                throw new Win32Exception(Marshal.GetLastWin32Error(), "Cannot start cancellation process group");
        }
        finally { CloseHandle(input); CloseHandle(log); }
        try
        {
            DateTime deadline = DateTime.UtcNow.AddMilliseconds(markerTimeoutMs);
            while (DateTime.UtcNow < deadline)
            {
                if (WaitForSingleObject(process.hProcess, 0) == 0) throw new InvalidOperationException("Stage 4 run exited before RUNNING was observed");
                if (File.Exists(markerPath)) break;
                Thread.Sleep(50);
            }
            if (!File.Exists(markerPath)) throw new TimeoutException("Execution marker was not observed within the bounded wait");
            if (!GenerateConsoleCtrlEvent(1, process.dwProcessId)) throw new Win32Exception(Marshal.GetLastWin32Error(), "Cannot send Ctrl+Break");
            if (WaitForSingleObject(process.hProcess, (uint)exitTimeoutMs) != 0) throw new TimeoutException("Cancelled Stage 4 run did not terminate within the bounded wait");
            uint exitCode; if (!GetExitCodeProcess(process.hProcess, out exitCode)) throw new Win32Exception(Marshal.GetLastWin32Error());
            return unchecked((int)exitCode);
        }
        finally { CloseHandle(process.hThread); CloseHandle(process.hProcess); }
    }
}
'@
}

function Invoke-CancelledStage4Run {
    param([string]$Label,[string]$Python,[string[]]$Arguments,[string]$MarkerPath)
    $log = Join-Path $evidence ($Label + '.log')
    $commands.Add("CTRL_BREAK after execution marker: & $(Quote-Stage4Argument $Python) " + (($Arguments | ForEach-Object { Quote-Stage4Argument $_ }) -join ' '))
    $exitCode = [Stage4ProcessGroup]::Run($Python,$Arguments,$log,$MarkerPath,120000,30000)
    $results.Add([ordered]@{case=$Label;exitCode=$exitCode;expected=@(5);log=[IO.Path]::GetRelativePath($evidence,$log).Replace('\','/');marker=[IO.Path]::GetRelativePath($evidence,$MarkerPath).Replace('\','/');signal='CTRL_BREAK_EVENT';attempts=1})
    if ($exitCode -ne 5) { throw "$Label returned $exitCode. See $log" }
    return @(Get-Content -LiteralPath $log)
}

try {
    if (Test-Path -LiteralPath $scratch) { throw "Stage 4 scratch already exists: $scratch" }
    $null = New-Item -ItemType Directory -Path $evidence -Force
    $null = New-Item -ItemType Directory -Path $scratch
    $PythonRuntime = [IO.Path]::GetFullPath($PythonRuntime)
    $DependencyWheelRoot = [IO.Path]::GetFullPath($DependencyWheelRoot)

    foreach ($configuration in @('Debug','Release')) {
        & (Join-Path $PSScriptRoot 'build.ps1') -Configuration $configuration -Platform x64
        if ($LASTEXITCODE -ne 0) { throw "$configuration build failed." }
    }
    $package = @{Configuration='Release';Platform='x64';PythonRuntime=$PythonRuntime;DependencyWheelRoot=$DependencyWheelRoot;VisualStudioPath=$VisualStudioPath}
    if ($ProtocPath) { $package.ProtocPath = $ProtocPath }
    if ($VisualCRTRoot) { $package.VisualCRTRoot = $VisualCRTRoot }
    & (Join-Path $PSScriptRoot 'package-development-kit.ps1') @package
    if ($LASTEXITCODE -ne 0) { throw 'Development-kit packaging failed.' }

    $archive = Join-Path $repositoryRoot 'artifacts\sdk-packages\x64\Release\ARTestDevelopmentKit-0.4.0-evaluation-windows-x64.zip'
    $kit = Join-Path $scratch 'SDK with spaces'
    Expand-Archive -LiteralPath $archive -DestinationPath $kit
    $entry = Join-Path $kit 'artest.ps1'
    $projectParent = Join-Path $scratch 'projects'; $null = New-Item -ItemType Directory -Path $projectParent
    $project = Join-Path $projectParent 'Stage 4 project with spaces'
    $env:ARTEST_SDK_CONFIG_ROOT = Join-Path $scratch 'profiles'

    $null = Invoke-Stage4Command '01-verify' $entry @('verify')
    $null = Invoke-Stage4Command '02-new' $entry @('new','--name','Stage 4 project with spaces','--folder',$projectParent,'--language','python')
    $null = Invoke-Stage4Command '03-register' $entry @('register','--project',$project,'--target','evaluation')
    $registry = Join-Path $env:ARTEST_SDK_CONFIG_ROOT 'installations.json'
    $registryBefore = Get-FileDigestOrMissing $registry
    $portablePlan = Join-Path $project 'plan\measurement.json'
    $portableBefore = Get-FileDigestOrMissing $portablePlan

    Push-Location ([IO.Path]::GetPathRoot($repositoryRoot))
    try {
        $releaseOutput = Invoke-Stage4Command '04-release-run' $entry @('run','--project',$project,'--target','evaluation')
        if ((Get-LastJson $releaseOutput).status -ne 'passed') { throw 'Release Test plan did not pass.' }
        $readyPath = Join-Path $project '.artest\stage3\ready.json'
        $firstPreparation = (Get-Content -LiteralPath $readyPath -Raw | ConvertFrom-Json).preparationId

        $debugCli = Join-Path $repositoryRoot 'artifacts\bin\x64\Debug\ARTestCLI.exe'
        $debugOutput = Invoke-Stage4Command '05-debug-run' $entry @('python-project','run',$project,'--cli-executable',$debugCli)
        if ((Get-LastJson $debugOutput).status -ne 'passed') { throw 'Debug Test plan did not pass.' }
        $debugPreparation = (Get-Content -LiteralPath $readyPath -Raw | ConvertFrom-Json).preparationId
        if ($debugPreparation -cne $firstPreparation) { throw 'Changing only the selected CLI invalidated Python preparation.' }

        $repeatOutput = Invoke-Stage4Command '06-unchanged-rerun' $entry @('run','--project',$project,'--target','evaluation')
        if ((Get-LastJson $repeatOutput).status -ne 'passed') { throw 'Unchanged rerun did not pass.' }
        if ((Get-Content -LiteralPath $readyPath -Raw | ConvertFrom-Json).preparationId -cne $firstPreparation) { throw 'Unchanged rerun did not reuse preparation.' }

        Add-Content -LiteralPath (Join-Path $project 'src\extension.py') -Value "`n# Stage 4 source edit"
        $editedOutput = Invoke-Stage4Command '07-edited-rerun' $entry @('run','--project',$project,'--target','evaluation')
        if ((Get-LastJson $editedOutput).status -ne 'passed') { throw 'Edited Test script rerun did not pass.' }
        $editedPreparation = (Get-Content -LiteralPath $readyPath -Raw | ConvertFrom-Json).preparationId
        if ($editedPreparation -ceq $firstPreparation) { throw 'Test script edit did not create a new preparation.' }
    }
    finally { Pop-Location }

    if ((Get-FileDigestOrMissing $registry) -cne $registryBefore) { throw 'Explicit run changed the selected installation profile.' }
    if ((Get-FileDigestOrMissing $portablePlan) -cne $portableBefore) { throw 'Explicit run changed the portable Test plan.' }

    $baseProfile = (Get-Content -LiteralPath $registry -Raw | ConvertFrom-Json).profiles.evaluation
    $msbuild = Join-Path $VisualStudioPath 'MSBuild\Current\Bin\MSBuild.exe'
    $runTarget = 'cross-package'
    $crossCatalog = Join-Path $scratch 'cross target\extensions'
    $crossConfiguration = Join-Path $scratch 'cross target\configuration'

    $driverProject = Join-Path $projectParent 'Registered native driver'
    $null = Invoke-Stage4Command '08-new-registered-driver' $entry @(
        'new','--name','Registered native driver','--folder',$projectParent,'--language','cpp',
        '--variant','driver-only','--msbuild',$msbuild)
    $driverGuidedPath = Join-Path $driverProject 'artest-sdk-project.json'
    $driverGuided = Get-Content -LiteralPath $driverGuidedPath -Raw | ConvertFrom-Json
    $generatedDriverId = [string]$driverGuided.driverId
    $generatedContractId = [string]$driverGuided.contractId
    $registeredDriverId = 'com.example.artest.driver.sim-value-source'
    $registeredContractId = 'com.example.artest.contract.value-source.v1'
    foreach ($relative in @('Extension.cpp','SimulatedValueSource.h','TestPlan.json')) {
        $path = Join-Path $driverProject $relative
        $text = Get-Content -LiteralPath $path -Raw
        $text = $text.Replace($generatedDriverId,$registeredDriverId).Replace($generatedContractId,$registeredContractId)
        if ($relative -eq 'SimulatedValueSource.h') {
            $text = $text.Replace(
                "$registeredContractId/read", 'com.example.artest.instrument.value-source.v1/read')
        }
        [IO.File]::WriteAllText($path,$text,[Text.UTF8Encoding]::new($false))
    }
    $driverGuided.driverId = $registeredDriverId
    $driverGuided.contractId = $registeredContractId
    Write-Stage4Json $driverGuidedPath $driverGuided
    $null = Invoke-Stage4Command '09-register-driver' $entry @(
        'register','--project',$driverProject,'--target',$runTarget,'--msbuild',$msbuild,
        '--cli',([string]$baseProfile.cli),'--catalog',$crossCatalog,'--config',$crossConfiguration)
    $profile = (Get-Content -LiteralPath $registry -Raw | ConvertFrom-Json).profiles.$runTarget
    $mapping = Join-Path ([string]$profile.configuration) 'python-environments.json'

    $commandProject = Join-Path $projectParent 'Python command'
    $commandCreated = Get-LastJson (Invoke-Stage4Command '10-new-python-command' $entry @(
        'new','--name','Python command','--folder',$projectParent,
        '--language','python','--variant','command-only'))
    $catalogBeforeSourceRun = Get-TreeDigest ([string]$profile.catalog)
    $mappingBeforeSourceRun = Get-FileDigestOrMissing $mapping
    $profileBeforeSourceRun = Get-FileDigestOrMissing $registry
    $sourceCommand = Get-LastJson (Invoke-Stage4Command '11-sources-command-registered-driver' $entry @(
        'run','--project',$commandProject,'--target',$runTarget,'--mode','sources'))
    if ($sourceCommand.status -ne 'passed' -or [double]$sourceCommand.steps[0].outcome.data.value -ne 84) {
        throw 'Python command did not use the registered driver to produce 84.'
    }
    if ((Get-TreeDigest ([string]$profile.catalog)) -cne $catalogBeforeSourceRun -or
        (Get-FileDigestOrMissing $mapping) -cne $mappingBeforeSourceRun -or
        (Get-FileDigestOrMissing $registry) -cne $profileBeforeSourceRun) {
        throw 'Sources-mode execution changed the selected catalog, association, or profile.'
    }

    $null = Invoke-Stage4Command '12-register-python-command' $entry @(
        'register','--project',$commandProject,'--target',$runTarget)
    $catalogBeforeRegisteredRun = Get-TreeDigest ([string]$profile.catalog)
    $mappingBeforeRegisteredRun = Get-FileDigestOrMissing $mapping
    $profileBeforeRegisteredRun = Get-FileDigestOrMissing $registry
    $registeredCommand = Get-LastJson (Invoke-Stage4Command '13-registered-revision-run' $entry @(
        'run','--project',$commandProject,'--target',$runTarget,'--mode','registered'))
    if ($registeredCommand.status -ne 'passed' -or [double]$registeredCommand.steps[0].outcome.data.value -ne 84) {
        throw 'Registered-revision execution did not produce 84.'
    }
    if ((Get-TreeDigest ([string]$profile.catalog)) -cne $catalogBeforeRegisteredRun -or
        (Get-FileDigestOrMissing $mapping) -cne $mappingBeforeRegisteredRun -or
        (Get-FileDigestOrMissing $registry) -cne $profileBeforeRegisteredRun) {
        throw 'Registered-mode execution changed the selected catalog, association, or profile.'
    }

    $runtimeProject = Join-Path $projectParent 'Runtime faults'
    $runtimeCreated = Get-LastJson (Invoke-Stage4Command '14-new-runtime-fixture' $entry @(
        'new','--name','Runtime faults','--folder',$projectParent,'--language','python'))
    Copy-Item -LiteralPath (Join-Path $repositoryRoot 'source\ARTest.Python\examples\simulated\extension.py') `
        -Destination (Join-Path $runtimeProject 'src\extension.py') -Force
    $runtimeSourcePath = Join-Path $runtimeProject 'src\extension.py'
    $runtimeSource = Get-Content -LiteralPath $runtimeSourcePath -Raw
    $runtimeSource = $runtimeSource.Replace(
        'from dataclasses import dataclass', "from dataclasses import dataclass`nfrom pathlib import Path")
    $runtimeSource = $runtimeSource.Replace(
        '    fail: bool = False', "    fail: bool = False`n    marker: str = `"`"")
    $runtimeSource = $runtimeSource.Replace(
        '        if parameters.fail:',
        "        if parameters.marker:`n            Path(parameters.marker).write_text(`"started\n`", encoding=`"utf-8`")`n        if parameters.fail:")
    [IO.File]::WriteAllText($runtimeSourcePath,$runtimeSource,[Text.UTF8Encoding]::new($false))
    Copy-Item -LiteralPath (Join-Path $repositoryRoot 'source\ARTest.Python\examples\PythonMeasurement.json') `
        -Destination (Join-Path $runtimeProject 'plan\measurement.json') -Force
    $runtimeGuidedPath = Join-Path $runtimeProject 'artest-sdk-project.json'
    $runtimeGuided = Get-Content -LiteralPath $runtimeGuidedPath -Raw | ConvertFrom-Json
    $runtimeGuided.extensionId = 'com.artest.python.simulated'
    $runtimeGuided.driverId = 'com.artest.python.driver.power'
    $runtimeGuided.commandId = 'com.artest.python.command.measure-voltage'
    $runtimeGuided.contractId = 'artest.contract.instrument.power-supply.v1'
    Write-Stage4Json $runtimeGuidedPath $runtimeGuided
    $runtimePlanPath = Join-Path $runtimeProject 'plan\measurement.json'
    $runtimePlan = Get-Content -LiteralPath $runtimePlanPath -Raw | ConvertFrom-Json

    $invalidPlanBytes = [IO.File]::ReadAllBytes($runtimePlanPath)
    try {
        [IO.File]::WriteAllText($runtimePlanPath,'{"format":',[Text.UTF8Encoding]::new($false))
        $invalidOutput = Invoke-Stage4Command '15-invalid-test-plan' $entry @(
            'run','--project',$runtimeProject,'--target',$runTarget,'--mode','sources') @(3)
        if (($invalidOutput | Out-String) -match '\[State\]|PY_DRIVER_') {
            throw 'Invalid Test plan started execution.'
        }
    }
    finally { [IO.File]::WriteAllBytes($runtimePlanPath,$invalidPlanBytes) }

    $runtimePlan.commands[0].params | Add-Member -NotePropertyName fail -NotePropertyValue $true -Force
    $runtimePlan.commands[0].policy.maxAttempts = 1
    Write-Stage4Json $runtimePlanPath $runtimePlan
    $commandError = Get-LastJson (Invoke-Stage4Command '16-command-error' $entry @(
        'run','--project',$runtimeProject,'--target',$runTarget,'--mode','sources') @(5))
    if ($commandError.status -ne 'error' -or $commandError.summary.totalAttempts -ne 1 -or
        ($commandError | ConvertTo-Json -Depth 16) -notmatch 'Simulated command error') {
        throw 'Real command error did not preserve result, exit code, and single attempt.'
    }
    $results[$results.Count - 1].status = [string]$commandError.status
    $results[$results.Count - 1].attempts = [int]$commandError.summary.totalAttempts

    $runtimePlan.commands[0].params | Add-Member -NotePropertyName fail -NotePropertyValue $false -Force
    $runtimePlan.commands[0].params | Add-Member -NotePropertyName holdMs -NotePropertyValue 10000 -Force
    $cancelMarker = Join-Path $evidence '17-real-cancellation.started'
    $runtimePlan.commands[0].params | Add-Member -NotePropertyName marker -NotePropertyValue $cancelMarker -Force
    $runtimePlan.commands[0].policy.timeoutMs = 20000
    Write-Stage4Json $runtimePlanPath $runtimePlan
    $privatePython = Join-Path $kit 'python\runtime\python.exe'
    $projectTool = Join-Path $kit 'python\tools\project.py'
    $cancelArguments = @(
        '-I','-B',(Join-Path $repositoryRoot 'scripts\test-stage4-cancel.py'),$projectTool,
        'run',$runtimeProject,'--cli-executable',([string]$profile.cli),'--mode','sources',
        '--installation-catalog',([string]$profile.catalog),
        '--installation-python-environments',$mapping,
        '--expected-extension-id','com.artest.python.simulated')
    $cancelled = Get-LastJson (Invoke-CancelledStage4Run '17-real-cancellation' $privatePython $cancelArguments $cancelMarker)
    if ($cancelled.status -ne 'cancelled' -or $cancelled.summary.totalAttempts -ne 1) {
        throw 'Ctrl+Break cancellation did not preserve a cancelled single-attempt result.'
    }
    $results[$results.Count - 1].status = [string]$cancelled.status

    $faultProject = Join-Path $projectParent 'Effect faults'
    $null = Invoke-Stage4Command '18-new-indeterminate-fixture' $entry @(
        'new','--name','Effect faults','--folder',$projectParent,'--language','python')
    Copy-Item -LiteralPath (Join-Path $repositoryRoot 'tests\TestSupport\PythonFaults\extension.py') `
        -Destination (Join-Path $faultProject 'src\extension.py') -Force
    $faultGuidedPath = Join-Path $faultProject 'artest-sdk-project.json'
    $faultGuided = Get-Content -LiteralPath $faultGuidedPath -Raw | ConvertFrom-Json
    $faultGuided.extensionId = 'com.artest.python.test-faults'
    $faultGuided.driverId = 'com.artest.python.driver.test-faults'
    $faultGuided.commandId = 'com.artest.python.command.test-effects'
    $faultGuided.contractId = 'artest.contract.instrument.power-supply.v1'
    Write-Stage4Json $faultGuidedPath $faultGuided
    $effectMarker = Join-Path $scratch 'indeterminate-effect.txt'
    $faultPlan = [ordered]@{
        format='ARTest.Script';version=1
        instruments=@([ordered]@{type='com.artest.python.driver.test-faults';id='Fault1';config=[ordered]@{effectFile=$effectMarker;mode='lost';blockMs=30000;relay='';failShutdown=$false}})
        commands=@(
            [ordered]@{stepId=1;name='com.artest.python.command.test-effects';instrument='Fault1';params=[ordered]@{action='retry'};policy=[ordered]@{maxAttempts=3;timeoutMs=2000;onFailure='continue'}},
            [ordered]@{stepId=2;name='com.artest.python.command.test-effects';instrument='Fault1';params=[ordered]@{action='propagate'};policy=[ordered]@{maxAttempts=3;timeoutMs=2000;onFailure='stop'}})
    }
    Write-Stage4Json (Join-Path $faultProject 'plan\measurement.json') $faultPlan
    $indeterminate = Get-LastJson (Invoke-Stage4Command '19-indeterminate-no-retry' $entry @(
        'run','--project',$faultProject,'--target',$runTarget,'--mode','sources') @(5))
    $effectCount = @((Get-Content -LiteralPath $effectMarker)).Count
    $callCount = @((Get-Content -LiteralPath ($effectMarker + '.calls'))).Count
    if ($indeterminate.status -ne 'error' -or $indeterminate.summary.totalAttempts -ne 1 -or
        $indeterminate.summary.skippedSteps -ne 1 -or
        $indeterminate.steps[0].outcome.indeterminate -ne $true -or
        $effectCount -ne 1 -or $callCount -ne 1) {
        throw 'Indeterminate effect was not preserved as one invocation with no automatic replay.'
    }
    $results[$results.Count - 1].status = [string]$indeterminate.status
    $results[$results.Count - 1].attempts = [int]$indeterminate.summary.totalAttempts
    $results[$results.Count - 1].effectCount = $effectCount

    $unitOutput = Invoke-Stage4Command '20-focused-unit-tests' (Join-Path $kit 'python\runtime\python.exe') @('-I','-B',(Join-Path $repositoryRoot 'source\ARTest.Python\tests\test_project.py'),'-v')
    if (($unitOutput | Out-String) -notmatch 'OK \(skipped=2\)|OK$') { throw 'Focused Stage 4 unit tests did not report success.' }

    $candidate = [ordered]@{
        schema='artest.schema.py-dx-01-stage4-evidence.v2';runId=$runId
        createdAtUtc=(Get-Date).ToUniversalTime().ToString('o');kitVersion='0.4.0'
        archiveSha256=(Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
        configurations=@('Debug','Release');firstPreparation=$firstPreparation;editedPreparation=$editedPreparation
        selectedInstallation=$runTarget
        profileUnchanged=$true;portableTestPlanUnchanged=$true
        selectedCatalogSha256=(Get-TreeDigest ([string]$profile.catalog))
        selectedAssociationSha256=(Get-FileDigestOrMissing $mapping)
        registeredDriverExtensionId=(Get-Content -LiteralPath (Join-Path $driverProject 'artest-sdk-project.json') -Raw | ConvertFrom-Json).extensionId
        localCommandExtensionId=[string]$commandCreated.extensionId
        sourceHashes=[ordered]@{
            projectPy=(Get-FileHash -LiteralPath (Join-Path $repositoryRoot 'source\ARTest.Python\tools\project.py') -Algorithm SHA256).Hash.ToLowerInvariant()
            registrationPs1=(Get-FileHash -LiteralPath (Join-Path $repositoryRoot 'source\ARTest.SDK\development-kit\registration.ps1') -Algorithm SHA256).Hash.ToLowerInvariant()
            gatePs1=(Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash.ToLowerInvariant()
            cancellationLauncher=(Get-FileHash -LiteralPath (Join-Path $repositoryRoot 'scripts\test-stage4-cancel.py') -Algorithm SHA256).Hash.ToLowerInvariant()
        }
        binaryHashes=[ordered]@{
            debugCli=(Get-FileHash -LiteralPath (Join-Path $repositoryRoot 'artifacts\bin\x64\Debug\ARTestCLI.exe') -Algorithm SHA256).Hash.ToLowerInvariant()
            releaseCli=(Get-FileHash -LiteralPath ([string]$profile.cli) -Algorithm SHA256).Hash.ToLowerInvariant()
            releaseEngine=(Get-FileHash -LiteralPath ([string]$profile.engine) -Algorithm SHA256).Hash.ToLowerInvariant()
            privatePython=(Get-FileHash -LiteralPath $privatePython -Algorithm SHA256).Hash.ToLowerInvariant()
        }
        testCounts=[ordered]@{
            focused=[ordered]@{selected=66;passed=64;failed=0;skipped=2}
            nativeDebug=[ordered]@{selected=273;passed=242;failed=0;skipped=31}
            nativeRelease=[ordered]@{selected=273;passed=242;failed=0;skipped=31}
            stage4Gate=[ordered]@{cases=$results.Count;passed=$results.Count;failed=0;skipped=0}
        }
        results=$results
        limitations=@('Hardware was not used.','C++ run remains on the existing native CLI path.','.NET, Stage 5, C-03, C-04, hot reload, and automatic retry remain out of scope.')
    }
    $candidate | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $evidence 'candidate.json') -Encoding utf8
    $commands | Set-Content -LiteralPath (Join-Path $evidence 'commands.txt') -Encoding utf8
}
catch {
    $null = New-Item -ItemType Directory -Path $evidence -Force
    $commands | Set-Content -LiteralPath (Join-Path $evidence 'commands.txt') -Encoding utf8
    [ordered]@{
        schema='artest.schema.py-dx-01-stage4-failure.v1'
        runId=$runId
        createdAtUtc=(Get-Date).ToUniversalTime().ToString('o')
        error=[string]$_.Exception.Message
        results=$results
        sourceHashes=[ordered]@{
            projectPy=(Get-FileHash -LiteralPath (Join-Path $repositoryRoot 'source\ARTest.Python\tools\project.py') -Algorithm SHA256).Hash.ToLowerInvariant()
            registrationPs1=(Get-FileHash -LiteralPath (Join-Path $repositoryRoot 'source\ARTest.SDK\development-kit\registration.ps1') -Algorithm SHA256).Hash.ToLowerInvariant()
            gatePs1=(Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash.ToLowerInvariant()
        }
    } | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $evidence 'failure.json') -Encoding utf8
    throw
}
finally {
    $env:ARTEST_SDK_CONFIG_ROOT = $previousConfigRoot
    if (Test-Path -LiteralPath $scratch) {
        $resolvedScratch = [IO.Path]::GetFullPath($scratch)
        if ($resolvedScratch -eq [IO.Path]::GetPathRoot($resolvedScratch) -or -not ([IO.Path]::GetFileName($resolvedScratch)).StartsWith('artest-s4-')) {
            throw "Refusing unsafe Stage 4 scratch cleanup: $resolvedScratch"
        }
        Remove-Item -LiteralPath $resolvedScratch -Recurse -Force
    }
}

Write-Host 'PY-DX-01 Stage 4 project execution tests: PASSED'
Write-Host "Evidence: $evidence"
