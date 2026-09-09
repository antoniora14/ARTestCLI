param(
    [Parameter(Mandatory=$true)][string]$Python,
    [string]$OutputRoot = "$PSScriptRoot\..\artifacts\python",
    [switch]$IncludeFaultTests
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$Python = (Resolve-Path -LiteralPath $Python).Path
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
$tool = Join-Path $repo 'source\ARTest.Python\tools\package.py'
function Invoke-PythonTool([string[]]$ToolArguments) {
    & $Python -I -B $tool @ToolArguments
    if ($LASTEXITCODE -ne 0) { throw "Python preparation failed with exit code $LASTEXITCODE." }
}
Invoke-PythonTool @('sdk', '--protoc', "$repo\artifacts\vcpkg-process\artest-x64-windows-static-md\tools\protobuf\protoc.exe",
    '--protocol', "$repo\source\ARTestEngine.Process\protocol\artest_process.proto", '--output', "$OutputRoot\sdk")
$lock = "$repo\source\ARTest.Python\requirements.lock"
$wheel = "$OutputRoot\sdk\artest_python-0.2.0-py3-none-any.whl"
Invoke-PythonTool @('package', '--source', "$repo\source\ARTest.Python\examples\simulated",
    '--entry-point', 'extension:define_extension', '--lock', $lock, '--output', "$OutputRoot\extensions\ARTestPySimulated")
Invoke-PythonTool @('prepare', '--package', "$OutputRoot\extensions\ARTestPySimulated",
    '--sdk', $wheel, '--output', "$OutputRoot\environments\simulated")
$mapping = @{'com.artest.python.simulated' = "$OutputRoot\environments\simulated\artest-environment.json"}
if ($IncludeFaultTests) {
    Invoke-PythonTool @('package', '--source', "$repo\tests\TestSupport\PythonFaults",
        '--entry-point', 'extension:define_extension', '--lock', $lock, '--output', "$OutputRoot\test-extensions\ARTestPyFaults")
    Invoke-PythonTool @('prepare', '--package', "$OutputRoot\test-extensions\ARTestPyFaults",
        '--sdk', $wheel, '--output', "$OutputRoot\environments\faults")
    $mapping['com.artest.python.test-faults'] = "$OutputRoot\environments\faults\artest-environment.json"
}
$mapping | ConvertTo-Json | Set-Content -LiteralPath "$OutputRoot\environments\python-environments.json" -Encoding utf8
Write-Output "Prepared environment mapping: $OutputRoot\environments\python-environments.json"
