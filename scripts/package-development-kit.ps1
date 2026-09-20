[CmdletBinding()]
param(
    [ValidateSet('Release')]
    [string]$Configuration = 'Release',
    [ValidateSet('x64')]
    [string]$Platform = 'x64',
    [Parameter(Mandatory = $true)]
    [string]$PythonRuntime,
    [Parameter(Mandatory = $true)]
    [string]$DependencyWheelRoot,
    [string]$ProtocPath,
    [string]$VisualStudioPath = 'D:\Program Files\Microsoft Visual Studio\18\Insiders',
    [string]$VisualCRTRoot
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$sourceRoot = Join-Path $repositoryRoot 'source\ARTest.SDK\development-kit'
$kitVersionSource = Join-Path $sourceRoot 'development-kit-version.json'
$nativeVersionSource = Join-Path $repositoryRoot 'source\ARTest.SDK\sdk-version.json'
$kitVersion = Get-Content -LiteralPath $kitVersionSource -Raw | ConvertFrom-Json
$nativeVersion = Get-Content -LiteralPath $nativeVersionSource -Raw | ConvertFrom-Json
if ($kitVersion.schema -ne 'artest.schema.development-kit-version.v1' -or
    $kitVersion.kitVersion -ne '0.4.0' -or
    $kitVersion.stability -ne 'evaluation' -or
    $kitVersion.platform -ne 'windows-x64' -or
    $kitVersion.nativeSdkVersion -ne $nativeVersion.sdkVersion -or
    $kitVersion.pythonSdkVersion -ne '0.2.0' -or
    $nativeVersion.engineApi -ne '0.4' -or
    $nativeVersion.nativeExtensionAbi -ne '0.2') {
    throw 'The development-kit and native SDK version declarations are inconsistent.'
}

if (-not $ProtocPath) {
    $ProtocPath = Join-Path $repositoryRoot 'artifacts\vcpkg-process\artest-x64-windows-static-md\tools\protobuf\protoc.exe'
}
$PythonRuntime = [IO.Path]::GetFullPath($PythonRuntime)
$DependencyWheelRoot = [IO.Path]::GetFullPath($DependencyWheelRoot)
$ProtocPath = [IO.Path]::GetFullPath($ProtocPath)
foreach ($input in @($PythonRuntime, $ProtocPath)) {
    if (-not (Test-Path -LiteralPath $input -PathType Leaf)) {
        throw "Required development-kit input is missing: $input"
    }
}
if (-not (Test-Path -LiteralPath $DependencyWheelRoot -PathType Container)) {
    throw "Dependency wheel directory is missing: $DependencyWheelRoot"
}

$artifactRoot = [IO.Path]::GetFullPath(
    (Join-Path $repositoryRoot "artifacts\sdk-packages\$Platform\$Configuration"))
$packageName = "ARTestDevelopmentKit-$($kitVersion.kitVersion)-evaluation-windows-$Platform"
$packageRoot = [IO.Path]::GetFullPath((Join-Path $artifactRoot $packageName))
$archivePath = [IO.Path]::GetFullPath((Join-Path $artifactRoot "$packageName.zip"))
$stagingRoot = [IO.Path]::GetFullPath(
    (Join-Path $artifactRoot (".$packageName.partial." + [guid]::NewGuid().ToString('N'))))
$stagingArchive = "$stagingRoot.zip"

function Assert-ChildPath {
    param([string]$Candidate, [string]$Parent)
    $fullCandidate = [IO.Path]::GetFullPath($Candidate)
    $fullParent = [IO.Path]::GetFullPath($Parent).TrimEnd('\') + '\'
    if (-not $fullCandidate.StartsWith($fullParent, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Development-kit path escapes its artifact root: $fullCandidate"
    }
}

function Assert-NoReparseTree {
    param([string]$Root, [string]$Description)
    if ((Get-Item -LiteralPath $Root -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
        throw "$Description cannot be a reparse point: $Root"
    }
    foreach ($item in Get-ChildItem -LiteralPath $Root -Recurse -Force) {
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "$Description contains a reparse point: $($item.FullName)"
        }
    }
}

function Copy-PythonRuntime {
    param([string]$Executable, [string]$Destination)
    $source = Split-Path -Parent $Executable
    Assert-NoReparseTree -Root $source -Description 'Private Python source'
    $null = New-Item -ItemType Directory -Path $Destination
    foreach ($name in @('python.exe', 'pythonw.exe', 'python3.dll', 'python313.dll', 'LICENSE.txt')) {
        $path = Join-Path $source $name
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "CPython runtime is incomplete; missing $path"
        }
        Copy-Item -LiteralPath $path -Destination (Join-Path $Destination $name)
    }
    foreach ($name in @('vcruntime140.dll', 'vcruntime140_1.dll')) {
        $path = Join-Path $source $name
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            Copy-Item -LiteralPath $path -Destination (Join-Path $Destination $name)
        }
    }
    foreach ($directoryName in @('DLLs', 'Lib')) {
        $sourceDirectory = Join-Path $source $directoryName
        if (-not (Test-Path -LiteralPath $sourceDirectory -PathType Container)) {
            throw "CPython runtime is incomplete; missing $sourceDirectory"
        }
        foreach ($item in Get-ChildItem -LiteralPath $sourceDirectory -Recurse -Force) {
            $relative = [IO.Path]::GetRelativePath($source, $item.FullName)
            if ($relative -match '^Lib\\site-packages(?:\\|$)' -or
                $relative -match '(?:^|\\)__pycache__(?:\\|$)' -or
                $item.Extension -in @('.pyc', '.pyo')) {
                continue
            }
            $target = Join-Path $Destination $relative
            if ($item.PSIsContainer) {
                $null = New-Item -ItemType Directory -Path $target -Force
            }
            else {
                $parent = Split-Path -Parent $target
                $null = New-Item -ItemType Directory -Path $parent -Force
                Copy-Item -LiteralPath $item.FullName -Destination $target
            }
        }
    }
}

function Get-CompatibleWheel {
    param([string]$Pattern, [string]$ExpectedHash, [string]$Description)
    $wheelCandidates = @(
        Get-ChildItem -LiteralPath $DependencyWheelRoot -File |
            Where-Object { $_.Name -like $Pattern -and
                (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() -eq $ExpectedHash }
    )
    if ($wheelCandidates.Count -ne 1) {
        throw "$Description requires exactly one wheel matching $Pattern with SHA-256 $ExpectedHash."
    }
    if ($wheelCandidates[0].Name -notmatch '-win_amd64\.whl$') {
        throw "$Description wheel is not the pinned Windows x64 binary: $($wheelCandidates[0].Name)"
    }
    return $wheelCandidates[0]
}

function Resolve-VisualCRTRoot {
    if ($VisualCRTRoot) {
        return [IO.Path]::GetFullPath($VisualCRTRoot)
    }
    $redistRoot = Join-Path $VisualStudioPath 'VC\Redist\MSVC'
    $candidates = @(
        Get-ChildItem -LiteralPath $redistRoot -Directory -ErrorAction SilentlyContinue |
            Sort-Object Name -Descending |
            ForEach-Object {
                Get-ChildItem -LiteralPath (Join-Path $_.FullName 'x64') -Directory -Filter 'Microsoft.VC*.CRT' -ErrorAction SilentlyContinue
            }
    )
    if ($candidates.Count -eq 0) {
        throw 'Compatible app-local Microsoft Visual C++ Redistributable DLLs were not found; pass -VisualCRTRoot.'
    }
    return $candidates[0].FullName
}

Assert-ChildPath -Candidate $packageRoot -Parent $artifactRoot
Assert-ChildPath -Candidate $archivePath -Parent $artifactRoot
Assert-ChildPath -Candidate $stagingRoot -Parent $artifactRoot
Assert-ChildPath -Candidate $stagingArchive -Parent $artifactRoot

try {
    $probeText = & $PythonRuntime -I -B -S -c "import json,platform,sys; print(json.dumps({'implementation':platform.python_implementation(),'version':platform.python_version(),'architecture':platform.machine(),'gilEnabled':sys._is_gil_enabled(),'maxsize':sys.maxsize}))" 2>&1
    if ($LASTEXITCODE -ne 0) { throw "CPython source probe failed: $probeText" }
    $pythonProbe = ($probeText | Out-String) | ConvertFrom-Json
    if ($pythonProbe.implementation -ne 'CPython' -or
        $pythonProbe.version -notmatch '^3\.13\.' -or
        $pythonProbe.architecture -notin @('AMD64', 'x86_64') -or
        $pythonProbe.gilEnabled -ne $true -or
        [Int64]$pythonProbe.maxsize -ne [Int64]::MaxValue) {
        throw 'The private runtime input must be standard GIL-enabled CPython 3.13 Windows x64.'
    }

    $null = New-Item -ItemType Directory -Path $stagingRoot
    foreach ($relative in @('python\tools', 'python\templates', 'python\wheels',
            'runtime\x64\Release', 'native-sdk')) {
        $null = New-Item -ItemType Directory -Path (Join-Path $stagingRoot $relative) -Force
    }
    Copy-Item -LiteralPath (Join-Path $sourceRoot 'artest.ps1') -Destination (Join-Path $stagingRoot 'artest.ps1')
    Copy-Item -LiteralPath (Join-Path $sourceRoot 'authoring.ps1') -Destination (Join-Path $stagingRoot 'authoring.ps1')
    Copy-Item -LiteralPath (Join-Path $sourceRoot 'registration.ps1') -Destination (Join-Path $stagingRoot 'registration.ps1')
    Copy-Item -LiteralPath (Join-Path $sourceRoot 'README.md') -Destination (Join-Path $stagingRoot 'README.md')
    Copy-Item -LiteralPath (Join-Path $sourceRoot 'THIRD_PARTY_NOTICES.md') -Destination (Join-Path $stagingRoot 'THIRD_PARTY_NOTICES.md')
    Copy-Item -LiteralPath $kitVersionSource -Destination (Join-Path $stagingRoot 'development-kit-version.json')

    & (Join-Path $PSScriptRoot 'package-sdk.ps1') -Configuration $Configuration -Platform $Platform
    $nativePackageName = "ARTestSDK-$($nativeVersion.sdkVersion)-windows-$Platform"
    $nativePackageRoot = Join-Path $artifactRoot $nativePackageName
    if (-not (Test-Path -LiteralPath (Join-Path $nativePackageRoot 'sdk-manifest.json') -PathType Leaf)) {
        throw 'The nested native SDK package is incomplete.'
    }
    Copy-Item -Path (Join-Path $nativePackageRoot '*') -Destination (Join-Path $stagingRoot 'native-sdk') -Recurse

    $pythonSource = Join-Path $repositoryRoot 'source\ARTest.Python'
    Copy-Item -LiteralPath (Join-Path $pythonSource 'tools\project.py') -Destination (Join-Path $stagingRoot 'python\tools\project.py')
    Copy-Item -LiteralPath (Join-Path $pythonSource 'tools\package.py') -Destination (Join-Path $stagingRoot 'python\tools\package.py')
    Copy-Item -LiteralPath (Join-Path $pythonSource 'artest_sdk') -Destination (Join-Path $stagingRoot 'python\artest_sdk') -Recurse
    Copy-Item -LiteralPath (Join-Path $pythonSource 'artest_host') -Destination (Join-Path $stagingRoot 'python\artest_host') -Recurse
    Copy-Item -LiteralPath (Join-Path $pythonSource 'templates\minimal') -Destination (Join-Path $stagingRoot 'python\templates\minimal') -Recurse
    Copy-Item -LiteralPath (Join-Path $pythonSource 'requirements.lock') -Destination (Join-Path $stagingRoot 'python\requirements.lock')

    $privateRuntime = Join-Path $stagingRoot 'python\runtime'
    Copy-PythonRuntime -Executable $PythonRuntime -Destination $privateRuntime
    $privatePython = Join-Path $privateRuntime 'python.exe'
    $copiedProbeText = & $privatePython -I -B -S -c "import json,platform,sys; print(json.dumps({'version':platform.python_version(),'architecture':platform.machine(),'gilEnabled':sys._is_gil_enabled(),'prefix':sys.prefix}))" 2>&1
    if ($LASTEXITCODE -ne 0) { throw "Copied private CPython probe failed: $copiedProbeText" }
    $copiedProbe = ($copiedProbeText | Out-String) | ConvertFrom-Json
    if ($copiedProbe.version -ne $pythonProbe.version -or
        $copiedProbe.architecture -notin @('AMD64', 'x86_64') -or
        $copiedProbe.gilEnabled -ne $true -or
        [IO.Path]::GetFullPath($copiedProbe.prefix) -ne [IO.Path]::GetFullPath($privateRuntime)) {
        throw 'Copied CPython is not self-contained at the staged private runtime path.'
    }
    $venvSmoke = Join-Path $stagingRoot '.python-venv-smoke'
    $previousNoBytecode = $env:PYTHONDONTWRITEBYTECODE
    try {
        $env:PYTHONDONTWRITEBYTECODE = '1'
        & $privatePython -I -B -m venv $venvSmoke
        if ($LASTEXITCODE -ne 0) { throw 'Copied CPython failed its venv smoke test.' }
        $pipText = & (Join-Path $venvSmoke 'Scripts\python.exe') -I -B -m pip --version 2>&1
        if ($LASTEXITCODE -ne 0 -or $pipText -notmatch '^pip ') {
            throw 'Copied CPython venv failed its bundled pip smoke test.'
        }
    }
    finally {
        $env:PYTHONDONTWRITEBYTECODE = $previousNoBytecode
    }
    Remove-Item -LiteralPath $venvSmoke -Recurse -Force

    $wheelRoot = Join-Path $stagingRoot 'python\wheels'
    & $privatePython -I -B (Join-Path $stagingRoot 'python\tools\package.py') sdk `
        --protoc $ProtocPath `
        --protocol (Join-Path $repositoryRoot 'source\ARTestEngine.Process\protocol\artest_process.proto') `
        --output $wheelRoot
    if ($LASTEXITCODE -ne 0) { throw 'The ARTest Python SDK wheel build failed.' }
    $sdkWheel = Join-Path $wheelRoot 'artest_python-0.2.0-py3-none-any.whl'
    $protobuf = Get-CompatibleWheel -Pattern 'protobuf-6.33.4-*.whl' `
        -ExpectedHash '8f11ffae31ec67fc2554c2ef891dcb561dae9a2a3ed941f9e134c2db06657dbc' `
        -Description 'protobuf 6.33.4'
    $pywin32 = Get-CompatibleWheel -Pattern 'pywin32-311-*.whl' `
        -ExpectedHash '718a38f7e5b058e76aee1c56ddd06908116d35147e133427e59a3983f703a20d' `
        -Description 'pywin32 311'
    Copy-Item -LiteralPath $protobuf.FullName -Destination (Join-Path $wheelRoot $protobuf.Name)
    Copy-Item -LiteralPath $pywin32.FullName -Destination (Join-Path $wheelRoot $pywin32.Name)
    $wheelInventory = @(
        Get-ChildItem -LiteralPath $wheelRoot -File -Filter '*.whl' |
            Sort-Object Name |
            ForEach-Object {
                [ordered]@{
                    path = $_.Name
                    sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
                }
            }
    )
    [ordered]@{
        schema = 'artest.schema.python-wheelhouse.v1'
        files = $wheelInventory
    } | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (
        Join-Path $wheelRoot 'artest-offline-wheelhouse.json') -Encoding utf8

    $runtimeRoot = Join-Path $stagingRoot 'runtime\x64\Release'
    foreach ($binary in @('ARTestCLI.exe', 'ARTestEngine.dll')) {
        $source = Join-Path $repositoryRoot "artifacts\bin\$Platform\$Configuration\$binary"
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
            throw "Build the Release solution before packaging the development kit; missing $binary."
        }
        Copy-Item -LiteralPath $source -Destination (Join-Path $runtimeRoot $binary)
    }
    $crtRoot = Resolve-VisualCRTRoot
    if (-not (Test-Path -LiteralPath $crtRoot -PathType Container)) {
        throw "Microsoft Visual C++ Redistributable source is missing: $crtRoot"
    }
    Assert-NoReparseTree -Root $crtRoot -Description 'Visual C++ Redistributable source'
    $crtFiles = @(Get-ChildItem -LiteralPath $crtRoot -File -Filter '*.dll')
    if ($crtFiles.Count -eq 0) { throw "No app-local CRT DLLs were found in $crtRoot" }
    foreach ($crt in $crtFiles) {
        Copy-Item -LiteralPath $crt.FullName -Destination (Join-Path $runtimeRoot $crt.Name)
    }

    $inventory = @(
        Get-ChildItem -LiteralPath $stagingRoot -Recurse -File |
            Sort-Object FullName |
            ForEach-Object {
                [ordered]@{
                    path = [IO.Path]::GetRelativePath($stagingRoot, $_.FullName).Replace('\', '/')
                    sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
                }
            }
    )
    [ordered]@{
        schema = 'artest.schema.development-kit-package.v1'
        kitVersion = $kitVersion.kitVersion
        stability = $kitVersion.stability
        platform = $kitVersion.platform
        components = [ordered]@{
            nativeSdk = [ordered]@{
                version = $nativeVersion.sdkVersion
                engineApi = $nativeVersion.engineApi
                nativeExtensionAbi = $nativeVersion.nativeExtensionAbi
                root = 'native-sdk'
                versionFile = 'native-sdk/sdk-version.json'
                manifestSha256 = (Get-FileHash -LiteralPath (Join-Path $stagingRoot 'native-sdk\sdk-manifest.json') -Algorithm SHA256).Hash.ToLowerInvariant()
            }
            pythonRuntime = [ordered]@{
                implementation = 'CPython'
                version = $pythonProbe.version
                architecture = 'x64'
                gilEnabled = $true
                executable = 'python/runtime/python.exe'
                provenance = 'caller-supplied standard CPython installation verified during kit construction'
            }
            pythonSdk = [ordered]@{
                name = 'artest-python'
                version = '0.2.0'
                privateWire = '0.2'
                wheel = 'python/wheels/artest_python-0.2.0-py3-none-any.whl'
            }
            pythonTools = [ordered]@{
                projectTool = 'python/tools/project.py'
                packageTool = 'python/tools/package.py'
                template = 'python/templates/minimal'
                dependencyLock = 'python/requirements.lock'
                wheelhouse = 'python/wheels/artest-offline-wheelhouse.json'
            }
            runtime = [ordered]@{
                configuration = 'Release'
                cli = 'runtime/x64/Release/ARTestCLI.exe'
                engine = 'runtime/x64/Release/ARTestEngine.dll'
                bundledProcessDependencies = @($crtFiles | Sort-Object Name | ForEach-Object { "runtime/x64/Release/$($_.Name)" })
            }
            authoring = [ordered]@{
                tool = 'authoring.ps1'
                languages = @('python', 'cpp')
                variants = @('driver-command', 'driver-only', 'command-only')
            }
            registration = [ordered]@{
                tool = 'registration.ps1'
                profileSchema = 'artest.schema.sdk-installations.v1'
                stateSchema = 'artest.schema.sdk-registration-state.v1'
            }
        }
        files = $inventory
    } | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $stagingRoot 'sdk-manifest.json') -Encoding utf8

    if (Test-Path -LiteralPath $packageRoot) {
        Remove-Item -LiteralPath $packageRoot -Recurse -Force
    }
    Move-Item -LiteralPath $stagingRoot -Destination $packageRoot
    Compress-Archive -Path (Join-Path $packageRoot '*') -DestinationPath $stagingArchive
    if (Test-Path -LiteralPath $archivePath) {
        Remove-Item -LiteralPath $archivePath -Force
    }
    Move-Item -LiteralPath $stagingArchive -Destination $archivePath
}
finally {
    if (Test-Path -LiteralPath $stagingRoot) {
        Assert-ChildPath -Candidate $stagingRoot -Parent $artifactRoot
        Remove-Item -LiteralPath $stagingRoot -Recurse -Force
    }
    if (Test-Path -LiteralPath $stagingArchive) {
        Assert-ChildPath -Candidate $stagingArchive -Parent $artifactRoot
        Remove-Item -LiteralPath $stagingArchive -Force
    }
}

Write-Host "Development kit directory: $packageRoot"
Write-Host "Development kit archive: $archivePath"
