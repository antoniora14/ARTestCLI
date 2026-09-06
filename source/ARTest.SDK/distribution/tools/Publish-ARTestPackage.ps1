[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$GeneratorPath,
    [Parameter(Mandatory=$true)][string]$BinaryPath,
    [Parameter(Mandatory=$true)][string]$ValidatorPath,
    [Parameter(Mandatory=$true)][string]$PackageDirectory,
    [ValidateRange(1,3600)][int]$ToolTimeoutSeconds = 120,
    [ValidateSet('', 'true', 'false')][string]$AdoptLegacyPackage = ''
)
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'ARTestPackagePublication.psm1') -Force
$arguments = @{
    GeneratorPath=$GeneratorPath; BinaryPath=$BinaryPath; ValidatorPath=$ValidatorPath
    PackageDirectory=$PackageDirectory; ToolTimeoutSeconds=$ToolTimeoutSeconds
    AdoptLegacyPackage=($AdoptLegacyPackage -eq 'true')
}
Publish-ARTestGeneratedPackage @arguments
