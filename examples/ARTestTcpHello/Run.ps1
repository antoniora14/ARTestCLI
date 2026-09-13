param(
    [Parameter(Mandatory)][string]$CLI,
    [ValidateSet('Debug','Release')][string]$Configuration = 'Release',
    [string]$PythonPackage = '',
    [string]$PythonEnvironment = ''
)
$ErrorActionPreference = 'Stop'
$CLI = (Resolve-Path -LiteralPath $CLI).Path
$simulator = Join-Path $PSScriptRoot "out\bin\x64\$Configuration\ARTestTcpSimulator.exe"
$package = Join-Path $PSScriptRoot "out\extensions\x64\$Configuration\ARTestTcpHello"
if (!(Test-Path -LiteralPath $simulator) -or !(Test-Path -LiteralPath "$package\artest-extension.json")) {
    throw 'Build the example before running.'
}
if ([bool]$PythonPackage -ne [bool]$PythonEnvironment) { throw 'Provide both PythonPackage and PythonEnvironment.' }
$token = 'ARTestTcpHello-' + [guid]::NewGuid().ToString('N')
$runRoot = Join-Path $PSScriptRoot "out\runs\$token"
New-Item -ItemType Directory -Path "$runRoot\extensions" -Force | Out-Null
Copy-Item -LiteralPath $package -Destination "$runRoot\extensions\ARTestTcpHello" -Recurse
$plan = Get-Content -LiteralPath "$PSScriptRoot\ExamplePlan.json" -Raw | ConvertFrom-Json
$mappingArgs = @()
if ($PythonPackage) {
    Copy-Item -LiteralPath (Resolve-Path -LiteralPath $PythonPackage).Path -Destination "$runRoot\extensions\ARTestPyTcpHello" -Recurse
    $mapping = @{'com.artest.example.python.tcp-hello' = (Resolve-Path -LiteralPath $PythonEnvironment).Path}
    $mapping | ConvertTo-Json | Set-Content -LiteralPath "$runRoot\python-environments.json" -Encoding utf8
    $mappingArgs = @('--python-environments', "$runRoot\python-environments.json")
    foreach ($step in $plan.commands) { $step.name = 'com.artest.example.python.command.tcp-set-and-measure' }
}
$readyName = "Local\$token-ready"
$stopName = "Local\$token-stop"
$ready = [Threading.EventWaitHandle]::new($false, [Threading.EventResetMode]::ManualReset, $readyName)
$stop = [Threading.EventWaitHandle]::new($false, [Threading.EventResetMode]::ManualReset, $stopName)
$process = $null
try {
    $arguments = '--ready-file "{0}\ready.json" --journal "{0}\server.jsonl" --ready-event {1} --stop-event {2} --parent-pid {3}' -f $runRoot, $readyName, $stopName, $PID
    $process = Start-Process -FilePath $simulator -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if (!$ready.WaitOne(5000)) { throw 'Simulator did not signal readiness within 5 seconds.' }
    $state = Get-Content -LiteralPath "$runRoot\ready.json" -Raw | ConvertFrom-Json
    if ($state.pid -ne $process.Id -or $state.port -lt 1 -or $state.port -gt 65535 -or $process.HasExited) {
        throw 'Simulator readiness validation failed.'
    }
    foreach ($instrument in $plan.instruments) { $instrument.config.port = $state.port }
    $plan | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath "$runRoot\plan.json" -Encoding utf8
    & $CLI extension-run "$runRoot\plan.json" "$runRoot\extensions" @mappingArgs | Tee-Object -FilePath "$runRoot\cli.txt"
    if ($LASTEXITCODE -ne 0) { throw "Execution failed: exit $LASTEXITCODE. Evidence: $runRoot" }
}
finally {
    $stop.Set() | Out-Null
    if ($process) {
        if (!$process.WaitForExit(3000)) { $process.Kill(); $process.WaitForExit(3000) | Out-Null }
        $process.Dispose()
    }
    $stop.Dispose()
    $ready.Dispose()
    Write-Host "Run evidence: $runRoot"
}
