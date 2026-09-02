[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ProjectDirectory
)

$ErrorActionPreference = 'Stop'

$resolvedProjectDirectory = (Resolve-Path -LiteralPath $ProjectDirectory).Path
$projectPath = Join-Path $resolvedProjectDirectory 'ARTestCLI.vcxproj'
if (-not (Test-Path -LiteralPath $projectPath -PathType Leaf)) {
    throw "ARTestCLI.vcxproj was not found in: $resolvedProjectDirectory"
}

$sourceFiles = Get-ChildItem -LiteralPath $resolvedProjectDirectory -File |
    Where-Object { $_.Extension -in '.cpp', '.h', '.hpp', '.vcxproj' }
$violations = $sourceFiles |
    Select-String -SimpleMatch 'ARTestEngine.Core'
if ($violations) {
    $details = $violations | ForEach-Object {
        "{0}:{1}: {2}" -f $_.Path, $_.LineNumber, $_.Line.Trim()
    }
    throw "Thin-host boundary violation detected:`n$($details -join [Environment]::NewLine)"
}

$projectText = Get-Content -LiteralPath $projectPath -Raw
if ($projectText -notmatch 'ARTestEngine\\ARTestEngine\.vcxproj') {
    throw 'ARTestCLI must reference ARTestEngine.vcxproj.'
}
if ($projectText -notmatch 'ARTest\.SDK\\include') {
    throw 'ARTestCLI must compile against the public ARTest.SDK include directory.'
}

Write-Host 'Thin-host architecture verification: PASSED'
