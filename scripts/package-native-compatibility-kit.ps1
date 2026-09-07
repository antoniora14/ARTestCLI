#requires -Version 7.0
[CmdletBinding()]
param([Parameter(Mandatory)][string]$OutputPath)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module "$PSScriptRoot/compatibility/Compatibility.Common.psm1" -Force
$repo = Split-Path $PSScriptRoot
$output = Resolve-CompatibilityPath $OutputPath
Assert-CompatibilityPlainPath $output
if (Test-Path -LiteralPath $output) { throw "COMPAT_KIT_EXISTS: $output" }
if ([IO.Path]::GetExtension($output) -ine '.zip') { throw 'Compatibility kit output must be a .zip file' }
$work = New-CompatibilityWorkDirectory
try {
    $kit = Join-Path $work 'kit'
    $null = New-Item -ItemType Directory -Path (Join-Path $kit 'compatibility/native-v1')
    $null = New-Item -ItemType Directory -Path (Join-Path $kit 'scripts/compatibility')
    $source = Join-Path $repo 'compatibility/native-v1'
    # Explicit inputs exclude local out/ trees, IDE state and developer files.
    foreach ($name in @('Extension.cpp', 'Host.cpp', 'EngineImports.def',
                         'ARTestCompatExtension.vcxproj', 'ARTestCompatHost.vcxproj')) {
        $inputPath = Join-Path $source $name
        Assert-CompatibilityPlainPath $inputPath
        Copy-Item -LiteralPath $inputPath -Destination (Join-Path $kit 'compatibility/native-v1')
    }
    foreach ($name in @('new-native-compatibility-baseline.ps1', 'test-native-compatibility.ps1',
                       'test-native-compatibility-guards.ps1', 'compatibility/Compatibility.Common.psm1')) {
        $inputPath = Join-Path $PSScriptRoot $name
        Assert-CompatibilityPlainPath $inputPath
        Copy-Item -LiteralPath $inputPath -Destination (Join-Path (Join-Path $kit 'scripts') $name)
    }
    Copy-Item -LiteralPath (Join-Path $repo 'docs/sdk/native-compatibility.md') -Destination (Join-Path $kit 'README.md')
    $manifest = [ordered]@{ format = 'ARTest.NativeCompatibilityKit'; version = 1
        files = @(ConvertTo-CompatibilityEntries (Get-CompatibilityInventory $kit)) }
    $manifest | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath (Join-Path $kit 'kit-manifest.json') -Encoding utf8
    $archive = Join-Path $work 'kit.zip'
    Compress-Archive -Path (Join-Path $kit '*') -DestinationPath $archive
    $extracted = Join-Path $work 'verified'
    Expand-Archive -LiteralPath $archive -DestinationPath $extracted
    Assert-CompatibilityInventory $extracted $manifest.files 'kit-manifest.json'
    $null = New-Item -ItemType Directory -Path (Split-Path $output) -Force
    [IO.File]::Copy($archive, $output, $false)
    Write-Host "Portable kit: $output"
    Write-Host "SHA-256: $((Get-FileHash -LiteralPath $output -Algorithm SHA256).Hash.ToLowerInvariant())"
}
finally { Remove-CompatibilityWorkDirectory $work }
