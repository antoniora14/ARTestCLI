[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$sdkSource = Join-Path $repositoryRoot 'source\ARTest.SDK'
$version = Get-Content -LiteralPath (Join-Path $sdkSource 'sdk-version.json') -Raw |
    ConvertFrom-Json
if ($version.schema -ne 'artest.schema.sdk-version.v1' -or
    $version.sdkVersion -ne '0.2.2' -or
    $version.engineApi -ne '0.4' -or
    $version.nativeExtensionAbi -ne '0.1' -or
    $version.stability -ne 'experimental' -or
    $version.platform -ne 'windows-x64' -or
    $version.toolset -ne 'v145' -or
    $version.cppStandard -ne 'C++20') {
    throw 'The SDK version declaration does not match the D3.4.3 compatibility baseline.'
}

$required = @(
    'distribution\ARTestSDK.props',
    'distribution\ARTestMetadata.targets',
    'distribution\tools\Publish-ARTestPackage.ps1',
    'distribution\tools\ARTestPackagePublication.psm1',
    'distribution\README.md',
    'distribution\THIRD_PARTY_NOTICES.md',
    'templates\ARTestExtension\ARTestExtensionStarter.vcxproj',
    'templates\ARTestExtension\Extension.cpp',
    'templates\ARTestExtension\ReadValueCommand.h',
    'templates\ARTestExtension\SimulatedValueSource.h',
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

if ($projectXml -notmatch 'ARTestMetadata\.targets' -or
    (Test-Path -LiteralPath (Join-Path $templateRoot 'artest-extension.json')) -or
    @(Get-ChildItem -Path (Join-Path $templateRoot 'schemas\*.json') -ErrorAction SilentlyContinue).Count -gt 0) {
    throw 'The installed starter must generate metadata rather than copy handwritten manifests/schemas.'
}

$validatorRoot = Join-Path $repositoryRoot 'source\ARTestSdkValidate'
foreach ($source in Get-ChildItem -LiteralPath $validatorRoot -File |
    Where-Object { $_.Extension -in '.cpp', '.h', '.vcxproj' }) {
    if ((Get-Content -LiteralPath $source.FullName -Raw) -match 'ARTestEngine\.Core|ARTestEngine[/\\].*\.h') {
        throw "Build validator must consume the public Engine API only: $($source.FullName)"
    }
}
Write-Host 'SDK distribution source verification: PASSED'
