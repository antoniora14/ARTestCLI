param(
    [Parameter(Mandatory)][string]$Python,
    [Parameter(Mandatory)][string]$SDKWheel,
    [string]$OutputRoot = "$PSScriptRoot\..\artifacts\python-c02"
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$Python = (Resolve-Path -LiteralPath $Python).Path
$SDKWheel = (Resolve-Path -LiteralPath $SDKWheel).Path
$OutputRoot = [IO.Path]::GetFullPath($OutputRoot)
$tool = "$repo\source\ARTest.Python\tools\package.py"
& $Python -I -B $tool package --source "$repo\examples\ARTestTcpHello\python" --entry-point extension:define_extension --lock "$repo\source\ARTest.Python\requirements.lock" --output "$OutputRoot\extensions\ARTestPyTcpHello"
if ($LASTEXITCODE -ne 0) { throw 'Python TCP command metadata/package generation failed.' }
& $Python -I -B $tool prepare --package "$OutputRoot\extensions\ARTestPyTcpHello" --sdk $SDKWheel --output "$OutputRoot\environment"
if ($LASTEXITCODE -ne 0) { throw 'Python TCP command environment preparation failed.' }
@{'com.artest.example.python.tcp-hello' = "$OutputRoot\environment\artest-environment.json"} |
    ConvertTo-Json | Set-Content -LiteralPath "$OutputRoot\python-environments.json" -Encoding utf8
Write-Host "Prepared C-02 Python command: $OutputRoot"
