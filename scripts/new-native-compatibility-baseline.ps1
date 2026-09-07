#requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$SdkRoot,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
    [string]$VisualStudioPath = 'D:\Program Files\Microsoft Visual Studio\18\Insiders'
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module "$PSScriptRoot/compatibility/Compatibility.Common.psm1" -Force
$repo = Split-Path $PSScriptRoot
$sdk = Resolve-CompatibilityPath $SdkRoot
$destination = Resolve-CompatibilityPath $OutputDirectory
Assert-CompatibilityPlainPath $destination
if (Test-Path -LiteralPath $destination) { throw "COMPAT_IMMUTABLE_BASELINE: destination already exists: $destination" }
if ($destination.StartsWith($sdk.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase) -or
    $destination.StartsWith((Join-Path $repo 'compatibility').TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'COMPAT_OUTPUT_INSIDE_INPUT'
}
Assert-CompatibilityPlainPath (Join-Path $sdk 'sdk-manifest.json')
$manifestPath = Join-Path $sdk 'sdk-manifest.json'
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
Assert-CompatibilityInventory $sdk $manifest.files 'sdk-manifest.json'
$version = Get-Content -LiteralPath (Join-Path $sdk 'sdk-version.json') -Raw | ConvertFrom-Json
if ($manifest.schema -cne 'artest.schema.sdk-package.v1' -or $version.sdkVersion -cne $manifest.sdkVersion -or
    $version.toolset -cne 'v145' -or $version.platform -cne 'windows-x64' -or
    $version.engineApi -cne '0.4' -or $version.nativeExtensionAbi -cne '0.1') {
    throw 'COMPAT_SDK_UNSUPPORTED: this fixture targets API 0.4 / native ABI 0.1 / v145 / x64'
}
$msbuild = Join-Path $VisualStudioPath 'MSBuild/Current/Bin/MSBuild.exe'
if (!(Test-Path -LiteralPath $msbuild -PathType Leaf)) { throw "MSBuild not found: $msbuild" }
$work = New-CompatibilityWorkDirectory
try {
    # Compiling in a separate tree prevents implicit repository Directory.Build.props/includes.
    $installedSdk = Join-Path $work 'installed SDK'
    Copy-Item -LiteralPath $sdk -Destination $installedSdk -Recurse
    Assert-CompatibilityInventory $installedSdk $manifest.files 'sdk-manifest.json'
    $consumer = Join-Path $work 'consumer'
    $null = New-Item -ItemType Directory -Path $consumer
    # Local VS outputs/settings are not fixture inputs, even if an author built in place.
    foreach ($name in @('Extension.cpp', 'Host.cpp', 'EngineImports.def',
                         'ARTestCompatExtension.vcxproj', 'ARTestCompatHost.vcxproj')) {
        $inputPath = Join-Path (Join-Path $repo 'compatibility/native-v1') $name
        Assert-CompatibilityPlainPath $inputPath
        Copy-Item -LiteralPath $inputPath -Destination $consumer
    }
    $sourceFiles = Get-CompatibilityInventory $consumer
    $packages = Join-Path $work 'packages'
    foreach ($project in @('ARTestCompatExtension', 'ARTestCompatHost')) {
        $buildArguments = @((Join-Path $consumer "$project.vcxproj"), '/nologo', '/m',
            "/p:Configuration=$Configuration", '/p:Platform=x64', "/p:ARTestSDKRoot=$installedSdk",
            "/p:ARTestCompatSdkVersion=$($version.sdkVersion)", "/p:ARTestPackageRoot=$packages")
        & $msbuild @buildArguments
        if ($LASTEXITCODE -ne 0) { throw "Independent consumer build failed: $project" }
    }
    $hostDirectory = Join-Path $consumer "out/ARTestCompatHost/$Configuration"
    Copy-Item -LiteralPath (Join-Path $installedSdk 'tools/ARTestEngine.dll') -Destination $hostDirectory
    $control = @()
    foreach ($scenario in Get-CompatibilityScenarios) {
        $catalog = Join-Path $work "control-$scenario"
        New-CompatibilityCaseCatalog $packages $catalog $scenario
        $case = Invoke-CompatibilityCase (Join-Path $hostDirectory 'ARTestCompatHost.exe') $catalog $scenario $version.sdkVersion
        $control += $case
        if (!$case.passed) { throw "Baseline control failed: $scenario : $($case.error)" }
    }
    $null = New-Item -ItemType Directory -Path (Split-Path $destination) -Force
    $null = New-Item -ItemType Directory -Path $destination
    foreach ($directory in @('bin', 'catalog', 'source')) {
        $null = New-Item -ItemType Directory -Path (Join-Path $destination $directory)
    }
    Copy-Item -LiteralPath (Join-Path $hostDirectory 'ARTestCompatHost.exe') -Destination (Join-Path $destination 'bin')
    Copy-Item -LiteralPath (Join-Path $packages 'ARTestCompatExtension') -Destination (Join-Path $destination 'catalog') -Recurse
    foreach ($relative in $sourceFiles.Keys) {
        $target = Join-Path (Join-Path $destination 'source') $relative
        $null = New-Item -ItemType Directory -Path (Split-Path $target) -Force
        Copy-Item -LiteralPath (Join-Path $consumer $relative) -Destination $target
    }
    $control | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath (Join-Path $destination 'control-results.json') -Encoding utf8
    $baseline = [ordered]@{
        format = 'ARTest.NativeCompatibilityBaseline'; version = 1; kitVersion = 'native-v1'
        createdUtc = [DateTime]::UtcNow.ToString('O'); platform = 'x64'; configuration = $Configuration
        sdkVersion = $version.sdkVersion; engineApi = $version.engineApi; nativeExtensionAbi = $version.nativeExtensionAbi
        sdkManifestSha256 = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToLowerInvariant()
        controlEngineSha256 = (Get-FileHash -LiteralPath (Join-Path $hostDirectory 'ARTestEngine.dll') -Algorithm SHA256).Hash.ToLowerInvariant()
        compiler = $control[0].evidence.compiler; msbuildVersion = (& $msbuild -nologo -version | Select-Object -Last 1)
        controlPassed = $true; files = @(ConvertTo-CompatibilityEntries (Get-CompatibilityInventory $destination))
    }
    $baseline | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath (Join-Path $destination 'baseline.json') -Encoding utf8
    $null = Read-CompatibilityBaseline $destination
    Write-Host "Frozen compatibility baseline: $destination (8/8 control scenarios passed)"
}
finally { Remove-CompatibilityWorkDirectory $work }
