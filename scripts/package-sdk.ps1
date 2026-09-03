[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [ValidateSet('x64')]
    [string]$Platform = 'x64'
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$versionSource = Join-Path $repositoryRoot 'source\ARTest.SDK\sdk-version.json'
$version = Get-Content -LiteralPath $versionSource -Raw | ConvertFrom-Json
if ($version.schema -ne 'artest.schema.sdk-version.v1' -or
    $version.stability -ne 'experimental' -or
    $version.platform -ne 'windows-x64') {
    throw 'The source SDK version declaration is invalid or unsupported.'
}
$sdkVersion = $version.sdkVersion
$artifactRoot = [IO.Path]::GetFullPath(
    (Join-Path $repositoryRoot "artifacts\sdk-packages\$Platform\$Configuration"))
$packageName = "ARTestSDK-$sdkVersion-windows-$Platform"
$packageRoot = [IO.Path]::GetFullPath((Join-Path $artifactRoot $packageName))
$archivePath = [IO.Path]::GetFullPath((Join-Path $artifactRoot "$packageName.zip"))
$stagingArchive = [IO.Path]::GetFullPath(
    (Join-Path $artifactRoot (".$packageName.partial." + [guid]::NewGuid().ToString('N') + '.zip')))
$stagingRoot = [IO.Path]::GetFullPath(
    (Join-Path $artifactRoot (".$packageName.partial." + [guid]::NewGuid().ToString('N'))))

function Assert-ChildPath {
    param([string]$Candidate, [string]$Parent)
    $fullCandidate = [IO.Path]::GetFullPath($Candidate)
    $fullParent = [IO.Path]::GetFullPath($Parent).TrimEnd('\') + '\'
    if (-not $fullCandidate.StartsWith(
            $fullParent, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Generated SDK path escapes its artifact root: $fullCandidate"
    }
}

Assert-ChildPath -Candidate $packageRoot -Parent $artifactRoot
Assert-ChildPath -Candidate $archivePath -Parent $artifactRoot
Assert-ChildPath -Candidate $stagingRoot -Parent $artifactRoot
Assert-ChildPath -Candidate $stagingArchive -Parent $artifactRoot

try {
    $null = New-Item -ItemType Directory -Path $stagingRoot
    foreach ($relativeDirectory in @(
            'include', 'include\nlohmann', 'build\native', 'docs',
            'share\ARTest\schemas', 'templates', 'tools')) {
        $null = New-Item -ItemType Directory -Path (
            Join-Path $stagingRoot $relativeDirectory)
    }

    Copy-Item -LiteralPath (Join-Path $repositoryRoot 'source\ARTest.SDK\include\ARTest') -Destination (Join-Path $stagingRoot 'include') -Recurse
    Copy-Item -LiteralPath (Join-Path $repositoryRoot 'source\ARTest.SDK\include\ARTestEngineApi.h') -Destination (Join-Path $stagingRoot 'include\ARTestEngineApi.h')
    Copy-Item -LiteralPath (Join-Path $repositoryRoot 'source\ARTest.SDK\include\ARTestEngineClient.h') -Destination (Join-Path $stagingRoot 'include\ARTestEngineClient.h')
    Copy-Item -LiteralPath (Join-Path $repositoryRoot 'source\ARTest.SDK\include\ARTestExtensionAbi.h') -Destination (Join-Path $stagingRoot 'include\ARTestExtensionAbi.h')
    Copy-Item -LiteralPath (Join-Path $repositoryRoot 'source\ThirdParty\json.hpp') -Destination (Join-Path $stagingRoot 'include\nlohmann\json.hpp')
    Copy-Item -Path (Join-Path $repositoryRoot 'source\ARTest.SDK\schemas\*') -Destination (Join-Path $stagingRoot 'share\ARTest\schemas')
    Copy-Item -Path (Join-Path $repositoryRoot 'docs\sdk\*') -Destination (Join-Path $stagingRoot 'docs')
    Copy-Item -Path (Join-Path $repositoryRoot 'source\ARTest.SDK\templates\*') -Destination (Join-Path $stagingRoot 'templates') -Recurse
    Copy-Item -LiteralPath (Join-Path $repositoryRoot 'scripts\package-extension.ps1') -Destination (Join-Path $stagingRoot 'tools\package-extension.ps1')
    Copy-Item -LiteralPath (Join-Path $repositoryRoot 'source\ARTest.SDK\distribution\ARTestSDK.props') -Destination (Join-Path $stagingRoot 'build\native\ARTestSDK.props')
    Copy-Item -LiteralPath (Join-Path $repositoryRoot 'source\ARTest.SDK\distribution\README.md') -Destination (Join-Path $stagingRoot 'README.md')
    Copy-Item -LiteralPath (Join-Path $repositoryRoot 'source\ARTest.SDK\distribution\THIRD_PARTY_NOTICES.md') -Destination (Join-Path $stagingRoot 'THIRD_PARTY_NOTICES.md')
    Copy-Item -LiteralPath $versionSource -Destination (Join-Path $stagingRoot 'sdk-version.json')

    $inventory = @(
        Get-ChildItem -LiteralPath $stagingRoot -Recurse -File |
            Sort-Object FullName |
            ForEach-Object {
                [ordered]@{
                    path = [IO.Path]::GetRelativePath($stagingRoot, $_.FullName).Replace('\', '/')
                    sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
                }
            }
    )
    [ordered]@{
        schema = 'artest.schema.sdk-package.v1'
        sdkVersion = $sdkVersion
        engineApi = $version.engineApi
        nativeExtensionAbi = $version.nativeExtensionAbi
        stability = $version.stability
        platform = $Platform
        files = $inventory
    } | ConvertTo-Json -Depth 8 |
        Set-Content -LiteralPath (Join-Path $stagingRoot 'sdk-manifest.json') -Encoding utf8

    if (Test-Path -LiteralPath $packageRoot) {
        Remove-Item -LiteralPath $packageRoot -Recurse -Force
    }
    Move-Item -LiteralPath $stagingRoot -Destination $packageRoot
    Compress-Archive -Path (Join-Path $packageRoot '*') -DestinationPath $stagingArchive
    if (Test-Path -LiteralPath $archivePath) {
        Remove-Item -LiteralPath $archivePath -Force
    }
    Move-Item -LiteralPath $stagingArchive -Destination $archivePath
}
finally {
    if (Test-Path -LiteralPath $stagingRoot) {
        Assert-ChildPath -Candidate $stagingRoot -Parent $artifactRoot
        Remove-Item -LiteralPath $stagingRoot -Recurse -Force
    }
    if (Test-Path -LiteralPath $stagingArchive) {
        Assert-ChildPath -Candidate $stagingArchive -Parent $artifactRoot
        Remove-Item -LiteralPath $stagingArchive -Force
    }
}

Write-Host "SDK directory: $packageRoot"
Write-Host "SDK archive: $archivePath"
