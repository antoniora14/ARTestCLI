[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [ValidateSet('x64')]
    [string]$Platform = 'x64',
    [string]$VisualStudioPath = 'D:\Program Files\Microsoft Visual Studio\18\Insiders'
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$version = Get-Content -LiteralPath (
    Join-Path $repositoryRoot 'source\ARTest.SDK\sdk-version.json') -Raw | ConvertFrom-Json
$msbuildPath = Join-Path $VisualStudioPath 'MSBuild\Current\Bin\MSBuild.exe'
$sdkRoot = Join-Path $repositoryRoot "artifacts\sdk-packages\$Platform\$Configuration\ARTestSDK-$($version.sdkVersion)-windows-$Platform"
$archivePath = "$sdkRoot.zip"
$testArtifactRoot = [IO.Path]::GetFullPath(
    (Join-Path $repositoryRoot "artifacts\sdk-consumer-tests\$Platform\$Configuration"))

function Assert-ChildPath {
    param([string]$Candidate, [string]$Parent)
    $fullCandidate = [IO.Path]::GetFullPath($Candidate)
    $fullParent = [IO.Path]::GetFullPath($Parent).TrimEnd('\') + '\'
    if (-not $fullCandidate.StartsWith(
            $fullParent, [StringComparison]::OrdinalIgnoreCase)) {
        throw "External-consumer path escapes its artifact root: $fullCandidate"
    }
}

if (-not (Test-Path -LiteralPath $msbuildPath -PathType Leaf)) {
    throw "Visual Studio Insiders MSBuild was not found: $msbuildPath"
}

& (Join-Path $PSScriptRoot 'package-sdk.ps1') -Configuration $Configuration -Platform $Platform
if (-not (Test-Path -LiteralPath $sdkRoot -PathType Container) -or
    -not (Test-Path -LiteralPath $archivePath -PathType Leaf)) {
    throw 'The SDK directory and archive are both required.'
}

$requiredFiles = @(
    'README.md',
    'THIRD_PARTY_NOTICES.md',
    'sdk-manifest.json',
    'sdk-version.json',
    'build\native\ARTestSDK.props',
    'include\ARTest\Extension.h',
    'include\ARTest\Testing.h',
    'include\ARTestExtensionAbi.h',
    'include\nlohmann\json.hpp',
    'templates\ARTestExtension\ARTestExtensionStarter.vcxproj',
    'tools\package-extension.ps1'
)
foreach ($requiredFile in $requiredFiles) {
    if (-not (Test-Path -LiteralPath (Join-Path $sdkRoot $requiredFile) -PathType Leaf)) {
        throw "The SDK package is incomplete: $requiredFile"
    }
}

$manifest = Get-Content -LiteralPath (Join-Path $sdkRoot 'sdk-manifest.json') -Raw |
    ConvertFrom-Json
if ($manifest.schema -ne 'artest.schema.sdk-package.v1' -or
    $manifest.sdkVersion -ne $version.sdkVersion -or
    $manifest.engineApi -ne $version.engineApi -or
    $manifest.nativeExtensionAbi -ne $version.nativeExtensionAbi -or
    $manifest.stability -ne $version.stability) {
    throw 'The SDK package manifest declares unexpected contract versions.'
}

$manifestPaths = @($manifest.files | ForEach-Object { $_.path })
$actualPaths = @(
    Get-ChildItem -LiteralPath $sdkRoot -Recurse -File |
        ForEach-Object { [IO.Path]::GetRelativePath($sdkRoot, $_.FullName).Replace('\', '/') } |
        Where-Object { $_ -ne 'sdk-manifest.json' } |
        Sort-Object
)
if (Compare-Object -ReferenceObject @($manifestPaths | Sort-Object) -DifferenceObject $actualPaths) {
    throw 'The SDK manifest inventory does not match the packaged files.'
}
foreach ($entry in $manifest.files) {
    if ($entry.path.Contains('..') -or [IO.Path]::IsPathRooted($entry.path)) {
        throw "Unsafe SDK inventory path: $($entry.path)"
    }
    $actualHash = (Get-FileHash -LiteralPath (
        Join-Path $sdkRoot $entry.path) -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actualHash -ne $entry.sha256) {
        throw "SDK checksum mismatch: $($entry.path)"
    }
}

Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::OpenRead($archivePath)
try {
    foreach ($entry in $archive.Entries) {
        if ([IO.Path]::IsPathRooted($entry.FullName) -or
            $entry.FullName.Split('/') -contains '..') {
            throw "Unsafe SDK archive entry: $($entry.FullName)"
        }
    }
}
finally {
    $archive.Dispose()
}

Assert-ChildPath -Candidate $testArtifactRoot -Parent (
    Join-Path $repositoryRoot 'artifacts\sdk-consumer-tests')
if (Test-Path -LiteralPath $testArtifactRoot) {
    Remove-Item -LiteralPath $testArtifactRoot -Recurse -Force
}
$null = New-Item -ItemType Directory -Path $testArtifactRoot
$installedSdkRoot = Join-Path $testArtifactRoot 'installed SDK'
[IO.Compression.ZipFile]::ExtractToDirectory($archivePath, $installedSdkRoot)
$consumerRoot = Join-Path $testArtifactRoot 'external extension project'
$extensionsRoot = Join-Path $testArtifactRoot 'extension packages'
Copy-Item -LiteralPath (Join-Path $installedSdkRoot 'templates\ARTestExtension') -Destination $consumerRoot -Recurse

$consumerProject = Join-Path $consumerRoot 'ARTestExtensionStarter.vcxproj'
& $msbuildPath $consumerProject /m "/p:Configuration=$Configuration" "/p:Platform=$Platform" "/p:ARTestSDKRoot=$installedSdkRoot" "/p:ARTestPackageRoot=$extensionsRoot" /verbosity:minimal
if ($LASTEXITCODE -ne 0) {
    throw "The installed SDK consumer build failed with exit code $LASTEXITCODE."
}

$cli = Join-Path $repositoryRoot "artifacts\bin\$Platform\$Configuration\ARTestCLI.exe"
$plan = Join-Path $consumerRoot 'TestPlan.json'
$doctorOutput = (& $cli extensions doctor $extensionsRoot 2>&1 | Out-String)
$doctorExitCode = $LASTEXITCODE
Write-Host $doctorOutput
if ($doctorExitCode -ne 0 -or $doctorOutput -notmatch '"status"\s*:\s*"active"') {
    throw "The external extension catalog did not activate; exit $doctorExitCode."
}

$compileOutput = (& $cli compile $plan --extensions $extensionsRoot 2>&1 | Out-String)
$compileExitCode = $LASTEXITCODE
Write-Host $compileOutput
if ($compileExitCode -ne 0 -or
    $compileOutput -notmatch 'Valid script. No instruments were initialized.' -or
    $compileOutput -match '\[State\] INITIALIZING') {
    throw "The external extension did not compile offline correctly; exit $compileExitCode."
}

$runOutput = (& $cli run $plan --extensions $extensionsRoot 2>&1 | Out-String)
$runExitCode = $LASTEXITCODE
Write-Host $runOutput
if ($runExitCode -ne 0 -or
    $runOutput -notmatch 'Computed value 84\.000000\.' -or
    $runOutput -notmatch 'Execution finished with PASSED' -or
    $runOutput -notmatch 'Simulated value source shut down') {
    throw "The external extension did not execute and clean up correctly; exit $runExitCode."
}

Write-Host 'SDK package and external-consumer compatibility: PASSED'
