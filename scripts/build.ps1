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
$solutionPath = Join-Path $repositoryRoot 'source\ARTestCLI.sln'
$msbuildPath = Join-Path $VisualStudioPath 'MSBuild\Current\Bin\MSBuild.exe'

if (-not (Test-Path -LiteralPath $msbuildPath)) {
    throw "No se encontro MSBuild de Visual Studio Insiders en: $msbuildPath"
}

& $msbuildPath $solutionPath /m "/p:Configuration=$Configuration" "/p:Platform=$Platform" /verbosity:minimal
if ($LASTEXITCODE -ne 0) {
    throw "La compilacion fallo con codigo $LASTEXITCODE."
}

if (-not $SkipTests) {
    $testExecutable = Join-Path $repositoryRoot "artifacts\bin\$Platform\$Configuration\ARTestCLI.UnitTests.exe"
    if (-not (Test-Path -LiteralPath $testExecutable)) {
        throw "No se encontro el ejecutable de pruebas: $testExecutable"
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
        throw "Las pruebas fallaron con codigo $testExitCode. Reporte: $htmlReportPath"
    }

    Write-Host "Reporte XML: $xmlReportPath"
    Write-Host "Reporte HTML: $htmlReportPath"
}
