param(
    [Parameter(Mandatory)][string]$SDKRoot,
    [ValidateSet('Debug','Release')][string]$Configuration = 'Release',
    [string]$VisualStudioPath = 'D:\Program Files\Microsoft Visual Studio\18\Insiders'
)
$ErrorActionPreference = 'Stop'
$SDKRoot = (Resolve-Path -LiteralPath $SDKRoot).Path
$version = Get-Content -LiteralPath "$SDKRoot\sdk-version.json" -Raw | ConvertFrom-Json
if ($version.sdkVersion -ne '0.4.0') { throw 'This example is validated against SDK 0.4.0.' }
$build = Join-Path $VisualStudioPath 'MSBuild\Current\Bin\MSBuild.exe'
foreach ($project in 'ARTestTcpHello.vcxproj', 'simulator\ARTestTcpSimulator.vcxproj') {
    & $build (Join-Path $PSScriptRoot $project) "/p:ARTestSDKRoot=$SDKRoot" "/p:Configuration=$Configuration" /p:Platform=x64 /v:minimal /nologo
    if ($LASTEXITCODE -ne 0) { throw "Build failed: $project ($LASTEXITCODE)." }
}
