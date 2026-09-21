[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repositoryRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$sourceRoot = Join-Path $repositoryRoot 'source\ARTest.SDK\development-kit'
$required = @(
    'artest.ps1',
    'authoring.ps1',
    'registration.ps1',
    'README.md',
    'FIRST_USE.md',
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
    $kitVersion.kitVersion -ne '0.4.0' -or
    $kitVersion.stability -ne 'evaluation' -or
    $kitVersion.platform -ne 'windows-x64' -or
    $kitVersion.nativeSdkVersion -ne $nativeVersion.sdkVersion -or
    $kitVersion.pythonSdkVersion -ne '0.2.0' -or
    $kitVersion.pythonRuntime -ne '3.13' -or
    $kitVersion.pythonGil -ne 'required') {
    throw 'The Stage 4A development-kit component versions are inconsistent.'
}

$entryText = Get-Content -LiteralPath (Join-Path $sourceRoot 'artest.ps1') -Raw
if ($entryText -notmatch "ValidateSet\('verify', 'paths', 'python-project', 'new', 'build', 'register', 'run'\)") {
    throw 'The development-kit entry point must expose explicit Stage 4 run.'
}
foreach ($path in @(
        (Join-Path $sourceRoot 'artest.ps1'),
        (Join-Path $sourceRoot 'authoring.ps1'),
        (Join-Path $sourceRoot 'registration.ps1'),
        (Join-Path $repositoryRoot 'scripts\package-development-kit.ps1'),
        (Join-Path $repositoryRoot 'scripts\test-development-kit.ps1'),
        (Join-Path $repositoryRoot 'scripts\test-development-kit-stage4b.ps1'),
        (Join-Path $repositoryRoot 'scripts\test-development-kit-stage4c.ps1'),
        (Join-Path $repositoryRoot 'scripts\test-python-project-stage4.ps1'),
        (Join-Path $repositoryRoot 'scripts\test-py-dx-01-stage5.ps1'))) {
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

$registrationText = Get-Content -LiteralPath (Join-Path $sourceRoot 'registration.ps1') -Raw
foreach ($requiredRegistration in @(
        'ARTESTREG005', 'ARTESTREG008', 'ARTEST_SDK_REGISTER_FAILPOINT',
        "'extensions', 'validate'", "'compile', `$plan", '--python-environments',
        'Repair-RegistrationTransaction', 'Save-InstallationSelection',
        'function Invoke-GuidedRun', "@('project','target','mode')", '--cli-executable',
        '--installation-catalog', '--installation-python-environments',
        '--expected-extension-id', "@('sources','registered')")) {
    if ($registrationText -notmatch [regex]::Escape($requiredRegistration)) {
        throw "The development-kit registration/run adapters are missing required behavior: $requiredRegistration"
    }
}
$projectToolText = Get-Content -LiteralPath (Join-Path $repositoryRoot 'source\ARTest.Python\tools\project.py') -Raw
if ($projectToolText -notmatch [regex]::Escape('--output-root') -or
    $projectToolText -notmatch [regex]::Escape('_prepare_directories(project, output_root)') -or
    $projectToolText -notmatch [regex]::Escape('"extension-run"') -or
    $projectToolText -notmatch [regex]::Escape('CLI_EXECUTION_TIMEOUT_SECONDS') -or
    $projectToolText -notmatch [regex]::Escape('EXECUTION_CATALOG_ROOT') -or
    $projectToolText -notmatch [regex]::Escape('_compose_execution_catalog') -or
    $projectToolText -notmatch [regex]::Escape('_registered_execution_inputs')) {
    throw 'Python project execution does not expose required Stage 4 preparation and catalog composition.'
}

$stage4BTest = Get-Content -LiteralPath (Join-Path $repositoryRoot 'scripts\test-development-kit-stage4b.ps1') -Raw
foreach ($requiredCoverage in @(
        "'new'", "'build'", 'Invoke-InteractiveKit', 'driver-only', 'command-only',
        'author source change', 'ARTESTSDK002', 'ARTESTSDK003', 'ARTESTSDK005',
        "@('python-project')", 'stage4b-candidate', 'externalTestRootRemoved')) {
    if ($stage4BTest -notmatch [regex]::Escape($requiredCoverage)) {
        throw "The Stage 4B gate is missing required coverage: $requiredCoverage"
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

$stage4Test = Get-Content -LiteralPath (Join-Path $repositoryRoot 'scripts\test-python-project-stage4.ps1') -Raw
foreach ($requiredCoverage in @(
        "@('Debug','Release')", "@('run','--project'", "@('python-project','run'",
        'unchanged-rerun', 'edited-rerun', 'profileUnchanged', 'portableTestPlanUnchanged',
        'sources-command-registered-driver', 'registered-revision-run',
        'invalid-test-plan', 'command-error', 'real-cancellation', 'CTRL_BREAK_EVENT',
        'indeterminate-no-retry', 'selectedCatalogSha256', 'testCounts', 'stage4-candidate')) {
    if ($stage4Test -notmatch [regex]::Escape($requiredCoverage)) {
        throw "The Stage 4 gate is missing required coverage: $requiredCoverage"
    }
}
if (-not (Test-Path -LiteralPath (Join-Path $repositoryRoot 'scripts\test-stage4-cancel.py') -PathType Leaf)) {
    throw 'The Stage 4 real cancellation launcher is missing.'
}

$firstUseText = Get-Content -LiteralPath (Join-Path $sourceRoot 'FIRST_USE.md') -Raw
foreach ($requiredCoverage in @(
        'Do not open an ARTest source checkout', 'Do not use a global Python',
        '--mode registered', 'Test plan', 'extension-run', 'intervention')) {
    if ($firstUseText -notmatch [regex]::Escape($requiredCoverage)) {
        throw "The Stage 5 first-use guide is missing required coverage: $requiredCoverage"
    }
}
$stage5Test = Get-Content -LiteralPath (Join-Path $repositoryRoot 'scripts\test-py-dx-01-stage5.ps1') -Raw
foreach ($requiredCoverage in @(
        'stage4-provenance-applicability', 'no-global-python-visible',
        'frozen-native-consumer', 'low-level-python-prepare',
        'sourcePathsUnavailable', 'detached-cpp-native-run', 'humanExercise')) {
    if ($stage5Test -notmatch [regex]::Escape($requiredCoverage)) {
        throw "The Stage 5 acceptance harness is missing required coverage: $requiredCoverage"
    }
}

Write-Host 'PY-DX-01 Stage 4A/4B/4C/4/5 development-kit source verification: PASSED'
