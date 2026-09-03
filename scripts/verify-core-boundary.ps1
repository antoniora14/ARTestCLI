[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$coreRoot = Join-Path $repositoryRoot 'source\ARTestEngine.Core'
$coreFiles = Get-ChildItem -LiteralPath $coreRoot -Recurse -File |
    Where-Object { $_.Extension -in '.h', '.cpp', '.vcxproj' }
$forbidden = 'ARTest.SDK|Windows.h|LoadLibrary|FakePowerSupply|FakeCanDevice|PowerOnCommand|PowerOffCommand|SendCanMessageCommand|std::cout|std::cin'
$violations = $coreFiles | Select-String -Pattern $forbidden
if ($violations) { throw "Core production boundary violation: $($violations -join [Environment]::NewLine)" }
$model = Get-Content -LiteralPath (Join-Path $coreRoot 'Model\CompiledStep.h') -Raw
if ($model -match 'ICommand|unique_ptr|shared_ptr|IInstrument') {
    throw 'CompiledStep must contain only plan data, not runtime instances.'
}
$compiler = Get-Content -LiteralPath (Join-Path $coreRoot 'Compilation\TestPlanCompiler.cpp') -Raw
if ($compiler -match 'CommandRegistry|InstrumentManager|CreateComponent|LoadLibrary') {
    throw 'Offline compilation cannot depend on runtime factories or native loading.'
}
foreach ($package in 'ARTestCmdHardware', 'ARTestCmdSample', 'ARTestDrvSimPower', 'ARTestDrvSimCAN', 'ExtensionSupport') {
    $files = Get-ChildItem -LiteralPath (Join-Path $repositoryRoot "source\$package") -Recurse -File |
        Where-Object { $_.Extension -in '.h', '.cpp', '.vcxproj' }
    if ($files | Select-String -SimpleMatch 'ARTestEngine.Core') {
        throw "$package must consume only the public extension ABI, not private Core types."
    }
}
Write-Host 'Core/extension architecture verification: PASSED'
