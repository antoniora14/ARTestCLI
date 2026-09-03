[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$BinaryPath,

    [Parameter(Mandatory = $true)]
    [string]$ManifestPath,

    [Parameter(Mandatory = $true)]
    [string]$PackageDirectory
)

$ErrorActionPreference = 'Stop'

$binary = Get-Item -LiteralPath $BinaryPath
$manifestSource = Get-Item -LiteralPath $ManifestPath
$null = New-Item -ItemType Directory -Force -Path $PackageDirectory
$packagedBinary = Join-Path $PackageDirectory $binary.Name
Copy-Item -LiteralPath $binary.FullName -Destination $packagedBinary -Force
$schemaDirectory = Join-Path $manifestSource.DirectoryName 'schemas'
if (Test-Path -LiteralPath $schemaDirectory -PathType Container) {
    Copy-Item -LiteralPath $schemaDirectory -Destination $PackageDirectory -Recurse -Force
}

$manifest = Get-Content -LiteralPath $manifestSource.FullName -Raw |
    ConvertFrom-Json
$stream = [System.IO.File]::OpenRead($packagedBinary)
try {
    $algorithm = [System.Security.Cryptography.SHA256]::Create()
    try {
        $hash = [System.BitConverter]::ToString(
            $algorithm.ComputeHash($stream)).Replace('-', '').ToLowerInvariant()
    }
    finally {
        $algorithm.Dispose()
    }
}
finally {
    $stream.Dispose()
}

if ($null -eq $manifest.integrity) {
    $manifest | Add-Member -MemberType NoteProperty -Name integrity -Value ([pscustomobject]@{})
}
if ($null -eq $manifest.integrity.PSObject.Properties['sha256']) {
    $manifest.integrity | Add-Member -MemberType NoteProperty -Name sha256 -Value $hash
}
else {
    $manifest.integrity.sha256 = $hash
}

$packagedManifest = Join-Path $PackageDirectory 'artest-extension.json'
$manifest | ConvertTo-Json -Depth 32 |
    Set-Content -LiteralPath $packagedManifest -Encoding utf8
