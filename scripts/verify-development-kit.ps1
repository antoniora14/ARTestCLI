[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repositoryRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$sourceRoot = Join-Path $repositoryRoot 'source\ARTest.SDK\development-kit'
$required = @(
    'artest.ps1',
    'README.md',
    'THIRD_PARTY_NOTICES.md',
    'development-kit-version.json'
)
foreach ($relative in $required) {
    if (-not (Test-Path -LiteralPath (Join-Path $sourceRoot $relative) -PathType Leaf)) {
        throw "Required Stage 4A development-kit source is missing: $relative"
    }
}

$kitVersion = Get-Content -LiteralPath (Join-Path $sourceRoot 'development-kit-version.json') -Raw | ConvertFrom-Json
$nativeVersion = Get-Content -LiteralPath (Join-Path $repositoryRoot 'source\ARTest.SDK\sdk-version.json') -Raw | ConvertFrom-Json
if ($kitVersion.schema -ne 'artest.schema.development-kit-version.v1' -or
    $kitVersion.kitVersion -ne '0.1.0' -or
    $kitVersion.stability -ne 'evaluation' -or
    $kitVersion.platform -ne 'windows-x64' -or
    $kitVersion.nativeSdkVersion -ne $nativeVersion.sdkVersion -or
    $kitVersion.pythonSdkVersion -ne '0.2.0' -or
    $kitVersion.pythonRuntime -ne '3.13' -or
    $kitVersion.pythonGil -ne 'required') {
    throw 'The Stage 4A development-kit component versions are inconsistent.'
}

$entryText = Get-Content -LiteralPath (Join-Path $sourceRoot 'artest.ps1') -Raw
if ($entryText -match "ValidateSet\([^\)]*'new'" -or
    $entryText -match "ValidateSet\([^\)]*'build'" -or
    $entryText -match "ValidateSet\([^\)]*'register'" -or
    $entryText -match "ValidateSet\([^\)]*'run'") {
    throw 'The Stage 4A root entry point crosses into Stage 4, 4B or 4C commands.'
}
foreach ($path in @(
        (Join-Path $sourceRoot 'artest.ps1'),
        (Join-Path $repositoryRoot 'scripts\package-development-kit.ps1'),
        (Join-Path $repositoryRoot 'scripts\test-development-kit.ps1'))) {
    $tokens = $null
    $errors = $null
    $null = [Management.Automation.Language.Parser]::ParseFile($path, [ref]$tokens, [ref]$errors)
    if ($errors.Count -ne 0) {
        throw "PowerShell syntax validation failed for $path`: $($errors.Message -join '; ')"
    }
    $text = Get-Content -LiteralPath $path -Raw
    if ($text -match 'Invoke-WebRequest|Invoke-RestMethod|Start-BitsTransfer|pip\s+download') {
        throw "Stage 4A packaging must not download dependencies: $path"
    }
}

$testText = Get-Content -LiteralPath (Join-Path $repositoryRoot 'scripts\test-development-kit.ps1') -Raw
foreach ($requiredEvidence in @(
        'separate PowerShell consumer',
        'repositoryAccessible',
        'System.IO.IOException',
        'FileShare]::None',
        '-2147024864',
        "Join-Path `$script:installedRoot 'native-sdk'",
        'New-Item -ItemType Junction',
        'Get-Stage4ASourceInventory',
        'evidence-provenance.json',
        'referenced-commands.txt')) {
    if ($testText -notmatch [regex]::Escape($requiredEvidence)) {
        throw "The Stage 4A acceptance harness is missing required external/provenance coverage: $requiredEvidence"
    }
}

$packageTool = Join-Path $repositoryRoot 'source\ARTest.Python\tools\package.py'
$packageText = Get-Content -LiteralPath $packageTool -Raw
if ($packageText -notmatch '--no-index' -or
    $packageText -notmatch 'artest-offline-wheelhouse\.json') {
    throw 'The Python preparation tool does not enforce the bundled offline wheelhouse path.'
}

Write-Host 'PY-DX-01 Stage 4A source verification: PASSED'
