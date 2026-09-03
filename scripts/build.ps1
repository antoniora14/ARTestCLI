[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',

    [ValidateSet('x64')]
    [string]$Platform = 'x64',

    [string]$VisualStudioPath = 'D:\Program Files\Microsoft Visual Studio\18\Insiders',

    [switch]$SkipTests
)

$ErrorActionPreference = 'Stop'

$repositoryRoot = Split-Path -Parent $PSScriptRoot
& (Join-Path $PSScriptRoot 'verify-core-boundary.ps1')
& (Join-Path $PSScriptRoot 'verify-sdk-authoring.ps1')
$solutionPath = Join-Path $repositoryRoot 'source\ARTestCLI.sln'
$msbuildPath = Join-Path $VisualStudioPath 'MSBuild\Current\Bin\MSBuild.exe'

if (-not (Test-Path -LiteralPath $msbuildPath)) {
    throw "Visual Studio Insiders MSBuild was not found at: $msbuildPath"
}

& $msbuildPath $solutionPath /m "/p:Configuration=$Configuration" "/p:Platform=$Platform" /verbosity:minimal
if ($LASTEXITCODE -ne 0) {
    throw "The build failed with exit code $LASTEXITCODE."
}

if (-not $SkipTests) {
    $abiTestExecutable = Join-Path $repositoryRoot "artifacts\bin\$Platform\$Configuration\ARTestAbi.ContractTests.exe"
    if (-not (Test-Path -LiteralPath $abiTestExecutable)) {
        throw "The ABI contract test executable was not found: $abiTestExecutable"
    }
    & $abiTestExecutable
    if ($LASTEXITCODE -ne 0) {
        throw "The C/C++ ABI layout contract failed with exit code $LASTEXITCODE."
    }

    $testExecutable = Join-Path $repositoryRoot "artifacts\bin\$Platform\$Configuration\ARTestCLI.UnitTests.exe"
    if (-not (Test-Path -LiteralPath $testExecutable)) {
        throw "The test executable was not found: $testExecutable"
    }

    $testResultsDirectory = Join-Path $repositoryRoot "artifacts\test-results\$Platform\$Configuration"
    [System.IO.Directory]::CreateDirectory($testResultsDirectory) | Out-Null
    $xmlReportPath = Join-Path $testResultsDirectory 'ARTestCLI.UnitTests.xml'
    $htmlReportPath = Join-Path $testResultsDirectory 'ARTestCLI.UnitTests.html'

    & $testExecutable "--gtest_output=xml:$xmlReportPath"
    $testExitCode = $LASTEXITCODE

    if (Test-Path -LiteralPath $xmlReportPath) {
        $reportScript = Join-Path $PSScriptRoot 'test-report\New-GoogleTestHtmlReport.ps1'
        $reportValidationScript = Join-Path $PSScriptRoot 'test-report\Test-GoogleTestHtmlReport.ps1'
        & $reportValidationScript
        & $reportScript -XmlPath $xmlReportPath -HtmlPath $htmlReportPath -Configuration $Configuration -Platform $Platform
    }

    if ($testExitCode -ne 0) {
        throw "The test suite failed with exit code $testExitCode. Report: $htmlReportPath"
    }

    Write-Host "XML report: $xmlReportPath"
    Write-Host "HTML report: $htmlReportPath"
    Write-Host "Extensions: $(Join-Path $repositoryRoot "artifacts\extensions\$Platform\$Configuration")"
}
