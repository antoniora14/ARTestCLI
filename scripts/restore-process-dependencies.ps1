[CmdletBinding()]
param([string]$VisualStudioPath = 'D:\Program Files\Microsoft Visual Studio\18\Insiders')
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot
$vcpkg = Join-Path $VisualStudioPath 'VC/vcpkg/vcpkg.exe'
if (!(Test-Path -LiteralPath $vcpkg)) { throw "vcpkg not found: $vcpkg" }
$previous = $env:ARTEST_VISUAL_STUDIO_PATH
try {
    $env:ARTEST_VISUAL_STUDIO_PATH = $VisualStudioPath
    $arguments = @('install', '--triplet=artest-x64-windows-static-md',
        '--host-triplet=artest-x64-windows-static-md',
        "--overlay-triplets=$(Join-Path $PSScriptRoot 'triplets')",
        "--x-manifest-root=$(Join-Path $repo 'source/ARTestEngine.Process')",
        "--x-install-root=$(Join-Path $repo 'artifacts/vcpkg-process')")
    & $vcpkg @arguments
    if ($LASTEXITCODE -ne 0) { throw "Process dependencies failed to restore ($LASTEXITCODE)." }
}
finally { $env:ARTEST_VISUAL_STUDIO_PATH = $previous }
