[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$GeneratorPath,
    [Parameter(Mandatory=$true)][string]$BinaryPath,
    [Parameter(Mandatory=$true)][string]$PackageDirectory
)
$ErrorActionPreference = 'Stop'
$repositoryRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$artifactRoot = Join-Path $repositoryRoot 'artifacts'
$binary = Get-Item -LiteralPath $BinaryPath
$package = [IO.Path]::GetFullPath($PackageDirectory)
$allowedRoot = Join-Path $artifactRoot 'sdk-examples'
if (-not $package.StartsWith($allowedRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Example metadata output must remain inside artifacts/sdk-examples.'
}
$ancestor = $package
while ($ancestor.Length -ge $artifactRoot.Length) {
    if (Test-Path -LiteralPath $ancestor) {
        if ((Get-Item -LiteralPath $ancestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw "Reparse points are not supported in generated paths: $ancestor"
        }
    }
    $ancestor = Split-Path -Parent $ancestor
}
$generator = (Get-Item -LiteralPath $GeneratorPath).FullName
$text = (& $generator $binary.Name) -join [Environment]::NewLine
if ($LASTEXITCODE -ne 0) { throw "ARTESTMETA002: Generator failed with exit $LASTEXITCODE." }
$bundle = $text | ConvertFrom-Json
if ($bundle.format -ne 'ARTest.MetadataBundle' -or $bundle.version -ne 1 -or
    $bundle.manifest.runtime.entry -ne $binary.Name -or
    @($bundle.manifest.components).Count -eq 0) {
    throw 'ARTESTMETA003: Invalid generator output.'
}
$stagingParent = Join-Path $artifactRoot 'obj'
$staging = Join-Path $stagingParent ('metadata-' + [guid]::NewGuid().ToString('N'))
if (Test-Path -LiteralPath $stagingParent) {
    if ((Get-Item -LiteralPath $stagingParent -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
        throw 'Reparse points are not supported in metadata staging.'
    }
}
$utf8 = New-Object System.Text.UTF8Encoding($false)
try {
    $null = New-Item -ItemType Directory -Path (Join-Path $staging 'schemas') -Force
    # Preserve the SDK serialization rather than round-tripping numeric schema values.
    foreach ($file in $bundle.schemas.PSObject.Properties) {
        if ($file.Name -cnotmatch '^schemas/[a-z0-9]+([.-][a-z0-9]+)*\.json$' -or
            $file.Value -isnot [string]) {
            throw 'ARTESTMETA004: Invalid generated schema path or content.'
        }
        $null = $file.Value | ConvertFrom-Json
        [IO.File]::WriteAllText((Join-Path $staging $file.Name), $file.Value, $utf8)
    }
    foreach ($component in $bundle.manifest.components) {
        foreach ($schema in $component.schemas) {
            if ($schema.path -cnotmatch '^schemas/[a-z0-9]+([.-][a-z0-9]+)*\.json$' -or
                -not (Test-Path -LiteralPath (Join-Path $staging $schema.path) -PathType Leaf)) {
                throw 'ARTESTMETA005: Missing generated schema.'
            }
        }
    }
    if ($bundle.manifestText -isnot [string]) { throw 'ARTESTMETA006: Missing manifest text.' }
    [IO.File]::WriteAllText((Join-Path $staging 'artest-extension.json'), $bundle.manifestText, $utf8)
    # D3.4.2 will add transactional package publication and installed-SDK targets.
    $packaging = @{
        BinaryPath = $binary.FullName
        ManifestPath = Join-Path $staging 'artest-extension.json'
        PackageDirectory = $package
    }
    & (Join-Path $PSScriptRoot 'package-extension.ps1') @packaging
    Write-Host 'SDK example metadata generated from C++ and packaged.'
}
finally {
    $resolvedStaging = [IO.Path]::GetFullPath($staging)
    if (-not $resolvedStaging.StartsWith($stagingParent + '\', [StringComparison]::OrdinalIgnoreCase) -or
        [IO.Path]::GetFileName($resolvedStaging) -notmatch '^metadata-[a-f0-9]{32}$') {
        throw 'Refusing cleanup outside the owned metadata staging directory.'
    }
    if (Test-Path -LiteralPath $resolvedStaging) {
        Remove-Item -LiteralPath $resolvedStaging -Recurse -Force
    }
}
