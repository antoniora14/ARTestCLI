[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$includeRoot = [IO.Path]::GetFullPath((Join-Path $repositoryRoot 'source\ARTest.SDK\include'))
$authoringRoot = Join-Path $includeRoot 'ARTest'
foreach ($file in Get-ChildItem -LiteralPath $authoringRoot -Filter '*.h' -File -Recurse) {
    $text = Get-Content -LiteralPath $file.FullName -Raw
    if ($text -match 'ARTestEngine\.Core|ExtensionSupport|ARTestEngine[/\\]') {
        throw "SDK authoring boundary violation: $($file.FullName)"
    }
    foreach ($include in [regex]::Matches($text, '#include\s+"([^"]+)"')) {
        $target = [IO.Path]::GetFullPath((Join-Path $file.DirectoryName $include.Groups[1].Value))
        if (-not $target.StartsWith($includeRoot + [IO.Path]::DirectorySeparatorChar,
                                   [StringComparison]::OrdinalIgnoreCase)) {
            throw "Public SDK header includes a file outside the SDK: $target"
        }
        if (-not (Test-Path -LiteralPath $target -PathType Leaf)) { throw "Missing SDK header: $target" }
    }
}
$exampleProject = Join-Path $repositoryRoot 'source\ARTest.SDK\examples\ARTestSdkExample\ARTestSdkExample.vcxproj'
if ((Get-Content -LiteralPath $exampleProject -Raw) -match 'ProjectReference|ARTestEngine') {
    throw 'The SDK example must build without an Engine/Core project dependency.'
}
# Reference packages are SDK consumers, not another implementation of the ABI.
foreach ($package in 'ARTestCmdHardware', 'ARTestCmdSample', 'ARTestDrvSimPower', 'ARTestDrvSimCAN') {
    $root = Join-Path $repositoryRoot "source\$package"
    $project = Get-Content -LiteralPath (Join-Path $root "$package.vcxproj") -Raw
    if ($project -match 'ProjectReference|ExtensionSupport|ARTestEngine' -or
        $project -notmatch 'ARTestSDK\.props' -or
        $project -notmatch '<TreatWarningAsError>true</TreatWarningAsError>') {
        throw "$package must be a strict standalone SDK consumer."
    }
    foreach ($file in Get-ChildItem -LiteralPath $root -Recurse -File |
             Where-Object { $_.Extension -in '.h', '.cpp' }) {
        $source = Get-Content -LiteralPath $file.FullName -Raw
        if ($source -match 'ARTestEngine|ExtensionSupport|ARTestExtensionAbi|ARTestExtension_Query|ARTEST_ABI_CALL|reinterpret_cast|ARTest/(detail/)|struct\s+ARTest\w*Opaque') {
            throw "Reference component contains private dependencies or handwritten ABI: $($file.FullName)"
        }
        foreach ($include in [regex]::Matches($source, '#include\s+"([^"]+)"')) {
            $target = [IO.Path]::GetFullPath((Join-Path $file.DirectoryName $include.Groups[1].Value))
            if (-not $target.StartsWith($root + [IO.Path]::DirectorySeparatorChar,
                                        [StringComparison]::OrdinalIgnoreCase) -or
                -not (Test-Path -LiteralPath $target -PathType Leaf)) {
                throw "Reference component include must remain in its own package: $target"
            }
        }
    }
}
Write-Host 'SDK authoring and reference-package boundary verification: PASSED'
