param(
    [Parameter(Mandatory)][string]$Executable,
    [Parameter(Mandatory)][string]$QtRoot,
    [Parameter(Mandatory)][string]$Destination,
    [Parameter(Mandatory)][string]$Wheelhouse,
    [Parameter(Mandatory)][ValidateSet('Debug','Release')][string]$Configuration
)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$destinationRoot = [IO.Path]::GetFullPath($Destination)
$descriptorName = 'artestdev-staging.json'
function Assert-Ordinary([string]$Path) {
    $current = [IO.Path]::GetFullPath($Path)
    while ($current) {
        if (Test-Path -LiteralPath $current) {
            if ((Get-Item -Force -LiteralPath $current).Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Reparse point is not allowed: $current"
            }
        }
        $current = [IO.Path]::GetDirectoryName($current)
    }
}
function Get-OrdinaryFiles([string]$Directory) {
    Assert-Ordinary $Directory
    foreach ($entry in Get-ChildItem -LiteralPath $Directory -Force) {
        Assert-Ordinary $entry.FullName
        if ($entry.PSIsContainer) { Get-OrdinaryFiles $entry.FullName } else { $entry }
    }
}
function Assert-Relative([string]$Path) {
    if (!$Path -or $Path -match '[\\:<>"|?*\x00-\x1f]' -or $Path.StartsWith('/') -or
        @($Path.Split('/') | Where-Object { !$_ -or $_ -in '.', '..' -or $_.EndsWith('.') -or $_.EndsWith(' ') }).Count) {
        throw "Unsafe inventory path: $Path"
    }
}
Assert-Ordinary $destinationRoot
if ($destinationRoot.Equals($repo, [StringComparison]::OrdinalIgnoreCase)) { throw 'Staging must have its own directory.' }
$copies = [ordered]@{}
function Add-Resource([string]$Source, [string]$Relative) {
    Assert-Relative $Relative
    Assert-Ordinary $Source
    if (!(Test-Path -LiteralPath $Source -PathType Leaf)) { throw "Missing explicit staging input: $Source" }
    if ($copies.Contains($Relative)) { throw "Duplicate staging destination: $Relative" }
    $copies[$Relative] = [IO.Path]::GetFullPath($Source)
}
# This allowlist is intentionally independent of historical package inventories.
Add-Resource $Executable 'ARTestDev.exe'
Add-Resource (Join-Path (Split-Path -Parent $Executable) 'ARTestDevNative.exe') 'ARTestDevNative.exe'
Add-Resource (Join-Path $PSScriptRoot 'resources/ARTestDevNative.targets') 'native-sdk/build/native/ARTestDevNative.targets'
$suffix = if ($Configuration -eq 'Debug') { 'd' } else { '' }
foreach ($module in 'Core','Gui','Widgets','Concurrent') {
    Add-Resource (Join-Path $QtRoot "bin/Qt6$module$suffix.dll") "Qt6$module$suffix.dll"
}
Add-Resource (Join-Path $QtRoot "plugins/platforms/qwindows$suffix.dll") "platforms/qwindows$suffix.dll"
Add-Resource (Join-Path $QtRoot 'sbom/qtbase-6.8.3.spdx') 'notices/qtbase-6.8.3.spdx'
Add-Resource (Join-Path $PSScriptRoot 'resources/STAGING-NOTICES.md') 'notices/STAGING-NOTICES.md'
foreach ($header in 'Command.h','Context.h','Definition.h','Extension.h','InstrumentDriver.h','Metadata.h',
    'MetadataGenerator.h','Parameters.h','Result.h','Schema.h','Testing.h',
    'detail/Marshalling.h','detail/NativeAdapter.h','detail/NativeContext.h') {
    Add-Resource (Join-Path $repo "source/ARTest.SDK/include/ARTest/$header") "native-sdk/include/ARTest/$header"
}
Add-Resource (Join-Path $repo 'source/ARTest.SDK/include/ARTestExtensionAbi.h') 'native-sdk/include/ARTestExtensionAbi.h'
Add-Resource (Join-Path $repo 'source/ThirdParty/json.hpp') 'native-sdk/include/nlohmann/json.hpp'
Add-Resource (Join-Path $repo 'source/ARTest.SDK/sdk-version.json') 'native-sdk/sdk-version.json'
Add-Resource (Join-Path $repo 'source/ARTest.SDK/distribution/ARTestSDK.props') 'native-sdk/build/native/ARTestSDK.props'
Add-Resource (Join-Path $repo 'source/ARTest.SDK/distribution/THIRD_PARTY_NOTICES.md') 'native-sdk/THIRD_PARTY_NOTICES.md'
foreach ($name in '.gitignore','ARTestExtensionStarter.vcxproj','Extension.cpp','MultipleInstruments.json',
    'ReadValueCommand.h','SimulatedValueSource.h','TestPlan.json','README.md') {
    Add-Resource (Join-Path $repo "source/ARTest.SDK/templates/ARTestExtension/$name") "native-sdk/templates/ARTestExtension/$name"
}
foreach ($name in 'project.py','package.py') {
    Add-Resource (Join-Path $repo "source/ARTest.Python/tools/$name") "python/tools/$name"
}
Add-Resource (Join-Path $PSScriptRoot 'resources/prepare.py') 'python/tools/prepare.py'
foreach ($name in '__init__.py','api.py','schema.py') {
    Add-Resource (Join-Path $repo "source/ARTest.Python/artest_sdk/$name") "python/artest_sdk/$name"
}
$wheelHashes = [ordered]@{
    'artest_python-0.2.0-py3-none-any.whl' = 'ac124e00c1ba832b7d1c24377ded10a90a9dd1073f99ebbcb9b2f5861d2fa677'
    'protobuf-6.33.4-cp310-abi3-win_amd64.whl' = '8f11ffae31ec67fc2554c2ef891dcb561dae9a2a3ed941f9e134c2db06657dbc'
    'pywin32-311-cp313-cp313-win_amd64.whl' = '718a38f7e5b058e76aee1c56ddd06908116d35147e133427e59a3983f703a20d'
}
$wheelManifest = Join-Path $Wheelhouse 'artest-offline-wheelhouse.json'
Assert-Ordinary $wheelManifest
$offline = Get-Content -Raw -LiteralPath $wheelManifest | ConvertFrom-Json
if ($offline.schema -ne 'artest.schema.python-wheelhouse.v1' -or $offline.files.Count -ne $wheelHashes.Count) { throw 'Unexpected offline wheelhouse declaration.' }
$wheelSeen = @{}
foreach ($entry in $offline.files) {
    if (!$wheelHashes.Contains($entry.path) -or $wheelSeen.ContainsKey($entry.path) -or $entry.sha256 -cne $wheelHashes[$entry.path]) { throw 'Unverified or duplicate offline wheel input.' }
    $wheelSeen[$entry.path] = $true
}
foreach ($name in $wheelHashes.Keys) {
    $inputWheel = Join-Path $Wheelhouse $name
    Assert-Ordinary $inputWheel
    if ((Get-FileHash -LiteralPath $inputWheel -Algorithm SHA256).Hash.ToLowerInvariant() -cne $wheelHashes[$name]) { throw "Unverified offline wheel: $inputWheel" }
    Add-Resource $inputWheel "python/wheels/$name"
}
Add-Resource $wheelManifest 'python/wheels/artest-offline-wheelhouse.json'
foreach ($name in '.gitignore','artest-project.json','artest-project.local.example.json','requirements.lock','src/extension.py','plan/measurement.json') {
    Add-Resource (Join-Path $repo "source/ARTest.Python/templates/minimal/$name") "python/templates/minimal/$name"
}
$version = Get-Content -Raw -LiteralPath (Join-Path $repo 'source/ARTest.SDK/sdk-version.json') | ConvertFrom-Json
if ($version.sdkVersion -ne '0.4.0') { throw 'Review staging component compatibility before changing SDK version.' }
$pythonVersion = Get-Content -Raw -LiteralPath (Join-Path $repo 'source/ARTest.Python/artest_sdk/__init__.py')
if ($pythonVersion -notmatch '__version__ = "0\.2\.0"') { throw 'Review staging compatibility before changing Python SDK version.' }

# Never bless corruption by refreshing hashes. Existing outputs must verify before any write.
if (Test-Path -LiteralPath $destinationRoot) {
    $oldFiles = @(Get-OrdinaryFiles $destinationRoot)
    if ($oldFiles.Count) {
        $descriptorPath = Join-Path $destinationRoot $descriptorName
        $old = Get-Content -Raw -LiteralPath $descriptorPath | ConvertFrom-Json
        if ($old.internalVersion -ne 1 -or $old.configuration -ne $Configuration -or $old.platform -ne 'windows-x64') {
            throw 'Unrecognized staging ownership. Preserve it and choose a fresh development directory.'
        }
        $seen = @{}
        foreach ($entry in $old.files) {
            Assert-Relative $entry.path
            if ($seen.ContainsKey($entry.path) -or !$copies.Contains($entry.path) -or $entry.sha256 -cnotmatch '^[0-9a-f]{64}$') {
                throw "Unexpected/duplicate previous inventory entry: $($entry.path)"
            }
            $seen[$entry.path] = $true
            $oldPath = Join-Path $destinationRoot $entry.path
            Assert-Ordinary $oldPath
            if ((Get-FileHash -LiteralPath $oldPath -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.sha256) {
                throw "Corrupt staging preserved without rewriting hashes: $oldPath"
            }
        }
        foreach ($entry in $oldFiles) {
            $relative = [IO.Path]::GetRelativePath($destinationRoot, $entry.FullName).Replace('\','/')
            if ($relative -ne $descriptorName -and !$seen.ContainsKey($relative)) { throw "Unknown staging file preserved: $relative" }
        }
        if ($oldFiles.Count -ne $seen.Count + 1 -or $seen.Count -ne $copies.Count) { throw 'Incomplete or changed previous inventory; use a fresh staging directory.' }
    }
}
foreach ($relative in $copies.Keys) {
    $target = Join-Path $destinationRoot $relative
    $null = New-Item -ItemType Directory -Force -Path ([IO.Path]::GetDirectoryName($target))
    Copy-Item -LiteralPath $copies[$relative] -Destination $target
}
$inventory = @(foreach ($relative in $copies.Keys | Sort-Object) {
    [ordered]@{ path = $relative; sha256 = (Get-FileHash -LiteralPath (Join-Path $destinationRoot $relative) -Algorithm SHA256).Hash.ToLowerInvariant() }
})
[ordered]@{
    nativeBuildProfile = 1
    internalVersion = 1
    platform = 'windows-x64'
    configuration = $Configuration
    components = [ordered]@{ nativeSdk = '0.4.0'; pythonSdk = '0.2.0'; qt = '6.8.3' }
    paths = [ordered]@{ executable = 'ARTestDev.exe'; nativeSdk = 'native-sdk'; projectTool = 'python/tools/project.py'; pythonTemplate = 'python/templates/minimal' }
    files = $inventory
} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $destinationRoot $descriptorName) -Encoding utf8
Write-Output "Development staging: $destinationRoot ($($inventory.Count) inventoried files)"
