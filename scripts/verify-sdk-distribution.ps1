[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$sdkSource = Join-Path $repositoryRoot 'source\ARTest.SDK'
$version = Get-Content -LiteralPath (Join-Path $sdkSource 'sdk-version.json') -Raw |
    ConvertFrom-Json
if ($version.schema -ne 'artest.schema.sdk-version.v1' -or
    $version.sdkVersion -ne '0.1.0' -or
    $version.engineApi -ne '0.4' -or
    $version.nativeExtensionAbi -ne '0.1' -or
    $version.stability -ne 'experimental' -or
    $version.platform -ne 'windows-x64' -or
    $version.toolset -ne 'v145' -or
    $version.cppStandard -ne 'C++20') {
    throw 'The D3.3-C SDK version declaration does not match the supported baseline.'
}

$required = @(
    'distribution\ARTestSDK.props',
    'distribution\README.md',
    'distribution\THIRD_PARTY_NOTICES.md',
    'templates\ARTestExtension\ARTestExtensionStarter.vcxproj',
    'templates\ARTestExtension\Extension.cpp',
    'templates\ARTestExtension\ReadValueCommand.h',
    'templates\ARTestExtension\SimulatedValueSource.h',
    'templates\ARTestExtension\artest-extension.json',
    'templates\ARTestExtension\TestPlan.json'
)
foreach ($relativePath in $required) {
    if (-not (Test-Path -LiteralPath (Join-Path $sdkSource $relativePath) -PathType Leaf)) {
        throw "Required SDK distribution source is missing: $relativePath"
    }
}

$templateRoot = Join-Path $sdkSource 'templates\ARTestExtension'
$templateSources = Get-ChildItem -LiteralPath $templateRoot -Recurse -File |
    Where-Object { $_.Extension -in '.h', '.hpp', '.cpp', '.vcxproj' }
foreach ($file in $templateSources) {
    $text = Get-Content -LiteralPath $file.FullName -Raw
    if ($text -match 'ARTestEngine\.Core|ProjectReference|<ARTest/detail/|ExtensionSupport') {
        throw "The external template crosses an SDK boundary: $($file.FullName)"
    }
}

[xml]$project = Get-Content -LiteralPath (
    Join-Path $templateRoot 'ARTestExtensionStarter.vcxproj') -Raw
$projectXml = $project.OuterXml
if ($projectXml -notmatch 'ARTestSDKRoot' -or
    $projectXml -notmatch 'ARTestSDK\.props' -or
    $projectXml -notmatch 'TreatWarningAsError' -or
    $projectXml -notmatch 'Level4' -or
    $projectXml -match 'ProjectConfiguration Include="[^"]*\|Win32"') {
    throw 'The extension template is not an x64-only strict installed-SDK consumer.'
}

$manifest = Get-Content -LiteralPath (
    Join-Path $templateRoot 'artest-extension.json') -Raw | ConvertFrom-Json
if ($manifest.schemaVersion -ne 2 -or
    $manifest.runtime.architecture -ne 'x64' -or
    $manifest.runtime.abi.major -ne 0 -or
    $manifest.runtime.abi.minor -ne 1 -or
    @($manifest.components).Count -ne 2) {
    throw 'The extension template manifest does not match the D3.3-C contract.'
}

Write-Host 'SDK distribution source verification: PASSED'
