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
Write-Host 'SDK authoring boundary verification: PASSED'
