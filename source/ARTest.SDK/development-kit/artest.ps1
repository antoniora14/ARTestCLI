[CmdletBinding(PositionalBinding = $false)]
param(
    [Parameter(Position = 0)]
    [ValidateSet('verify', 'paths', 'python-project', 'new', 'build', 'register', 'run')]
    [string]$Command = 'verify',

    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$Arguments
)

$ErrorActionPreference = 'Stop'
$kitRoot = [IO.Path]::GetFullPath($PSScriptRoot)
$manifestPath = Join-Path $kitRoot 'sdk-manifest.json'

function Stop-Kit {
    param([string]$Code, [string]$Message)
    throw "$Code $Message"
}

function Resolve-KitPath {
    param([string]$RelativePath, [string]$Field)
    if ([string]::IsNullOrWhiteSpace($RelativePath) -or
        [IO.Path]::IsPathRooted($RelativePath) -or
        $RelativePath.Replace('\', '/').Split('/') -contains '..') {
        Stop-Kit 'ARTESTKIT002' "Unsafe $Field path in kit manifest: $RelativePath"
    }
    $resolved = [IO.Path]::GetFullPath((Join-Path $kitRoot $RelativePath))
    $prefix = $kitRoot.TrimEnd('\') + '\'
    if (-not $resolved.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        Stop-Kit 'ARTESTKIT002' "$Field path escapes the kit: $RelativePath"
    }
    return $resolved
}

function Read-KitManifest {
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        Stop-Kit 'ARTESTKIT001' "Missing development-kit inventory: $manifestPath"
    }
    try {
        $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    }
    catch {
        Stop-Kit 'ARTESTKIT002' "Cannot read development-kit inventory: $($_.Exception.Message)"
    }
    if ($manifest.schema -ne 'artest.schema.development-kit-package.v1' -or
        $manifest.kitVersion -ne '0.4.0' -or
        $manifest.platform -ne 'windows-x64' -or
        $manifest.stability -ne 'evaluation') {
        Stop-Kit 'ARTESTKIT006' 'The development-kit declaration is incompatible with this entry point.'
    }
    return $manifest
}

function Assert-KitInventory {
    param([object]$Manifest)
    $expected = @{}
    foreach ($entry in @($Manifest.files)) {
        if ($null -eq $entry.path -or $null -eq $entry.sha256) {
            Stop-Kit 'ARTESTKIT002' 'The development-kit inventory contains an incomplete entry.'
        }
        $relative = [string]$entry.path
        $null = Resolve-KitPath $relative 'inventory'
        $key = $relative.Replace('\', '/').ToLowerInvariant()
        if ($expected.ContainsKey($key)) {
            Stop-Kit 'ARTESTKIT002' "Duplicate development-kit inventory path: $relative"
        }
        $expected[$key] = [string]$entry.sha256
    }

    $actual = @{}
    foreach ($item in Get-ChildItem -LiteralPath $kitRoot -Recurse -Force) {
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            Stop-Kit 'ARTESTKIT002' "Reparse points are not allowed in the development kit: $($item.FullName)"
        }
        if (-not $item.PSIsContainer -and $item.FullName -ne $manifestPath) {
            $relative = [IO.Path]::GetRelativePath($kitRoot, $item.FullName).Replace('\', '/')
            $actual[$relative.ToLowerInvariant()] = $relative
        }
    }
    foreach ($key in $expected.Keys) {
        if (-not $actual.ContainsKey($key)) {
            Stop-Kit 'ARTESTKIT003' "Missing development-kit component: $key"
        }
    }
    foreach ($key in $actual.Keys) {
        if (-not $expected.ContainsKey($key)) {
            Stop-Kit 'ARTESTKIT005' "Unexpected development-kit component: $($actual[$key])"
        }
    }
    foreach ($key in $expected.Keys) {
        $path = Resolve-KitPath $actual[$key] 'inventory'
        $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($hash -ne $expected[$key]) {
            Stop-Kit 'ARTESTKIT004' "Corrupt development-kit component: $($actual[$key])"
        }
    }
}

function Assert-KitCompatibility {
    param([object]$Manifest, [switch]$ProbePython, [switch]$Deep)
    if ($Manifest.components.nativeSdk.version -ne '0.4.0' -or
        $Manifest.components.nativeSdk.engineApi -ne '0.4' -or
        $Manifest.components.nativeSdk.nativeExtensionAbi -ne '0.2' -or
        $Manifest.components.pythonSdk.version -ne '0.2.0' -or
        $Manifest.components.pythonRuntime.version -notmatch '^3\.13\.' -or
        $Manifest.components.pythonRuntime.architecture -ne 'x64' -or
        $Manifest.components.pythonRuntime.gilEnabled -ne $true -or
        $Manifest.components.runtime.configuration -ne 'Release' -or
        $Manifest.components.authoring.tool -ne 'authoring.ps1' -or
        $Manifest.components.registration.tool -ne 'registration.ps1' -or
        (@($Manifest.components.authoring.languages) -join ',') -ne 'python,cpp' -or
        (@($Manifest.components.authoring.variants) -join ',') -ne 'driver-command,driver-only,command-only') {
        Stop-Kit 'ARTESTKIT006' 'One or more bundled component versions are incompatible.'
    }

    $nativeVersionPath = Resolve-KitPath $Manifest.components.nativeSdk.versionFile 'native SDK version'
    try {
        $nativeVersion = Get-Content -LiteralPath $nativeVersionPath -Raw | ConvertFrom-Json
    }
    catch {
        Stop-Kit 'ARTESTKIT006' "Cannot read the nested native SDK version: $($_.Exception.Message)"
    }
    if ($nativeVersion.sdkVersion -ne $Manifest.components.nativeSdk.version -or
        $nativeVersion.engineApi -ne $Manifest.components.nativeSdk.engineApi -or
        $nativeVersion.nativeExtensionAbi -ne $Manifest.components.nativeSdk.nativeExtensionAbi) {
        Stop-Kit 'ARTESTKIT006' 'The nested native SDK does not match the development-kit declaration.'
    }

    if (-not $ProbePython) { return }

    $python = Resolve-KitPath $Manifest.components.pythonRuntime.executable 'private Python'
    $probeScript = @'
import json, platform, sys
print(json.dumps({
    "implementation": platform.python_implementation(),
    "version": platform.python_version(),
    "architecture": platform.machine(),
    "gilEnabled": sys._is_gil_enabled(),
    "maxsize": sys.maxsize,
}))
'@
    $probeOutput = & $python -I -B -S -c $probeScript 2>&1
    if ($LASTEXITCODE -ne 0) {
        Stop-Kit 'ARTESTKIT006' "Private Python probe failed: $($probeOutput | Out-String)"
    }
    try {
        $probe = ($probeOutput | Out-String) | ConvertFrom-Json
    }
    catch {
        Stop-Kit 'ARTESTKIT006' 'Private Python returned an invalid compatibility probe.'
    }
    if ($probe.implementation -ne 'CPython' -or
        $probe.version -notmatch '^3\.13\.' -or
        $probe.architecture -notin @('AMD64', 'x86_64') -or
        $probe.gilEnabled -ne $true -or
        [Int64]$probe.maxsize -ne [Int64]::MaxValue) {
        Stop-Kit 'ARTESTKIT006' 'Private Python is not standard GIL-enabled CPython 3.13 Windows x64.'
    }

    if ($Deep) {
        $scratch = Join-Path ([IO.Path]::GetTempPath()) ('artest-kit-verify-' + [guid]::NewGuid().ToString('N'))
        $previousNoBytecode = $env:PYTHONDONTWRITEBYTECODE
        try {
            $env:PYTHONDONTWRITEBYTECODE = '1'
            & $python -I -B -m venv $scratch
            if ($LASTEXITCODE -ne 0) {
                Stop-Kit 'ARTESTKIT007' 'Private Python could not create a venv.'
            }
            $venvPython = Join-Path $scratch 'Scripts\python.exe'
            $pipOutput = & $venvPython -I -B -m pip --version 2>&1
            if ($LASTEXITCODE -ne 0 -or $pipOutput -notmatch '^pip ') {
                Stop-Kit 'ARTESTKIT007' 'The private Python venv does not provide pip.'
            }
        }
        finally {
            $env:PYTHONDONTWRITEBYTECODE = $previousNoBytecode
            if (Test-Path -LiteralPath $scratch) {
                Remove-Item -LiteralPath $scratch -Recurse -Force
            }
        }
    }
}

function Get-KitPaths {
    param([object]$Manifest)
    return [ordered]@{
        kitRoot = $kitRoot
        privatePython = Resolve-KitPath $Manifest.components.pythonRuntime.executable 'private Python'
        pythonSdkWheel = Resolve-KitPath $Manifest.components.pythonSdk.wheel 'Python SDK wheel'
        pythonProjectTool = Resolve-KitPath $Manifest.components.pythonTools.projectTool 'Python project tool'
        authoringTool = Resolve-KitPath $Manifest.components.authoring.tool 'SDK authoring tool'
        registrationTool = Resolve-KitPath $Manifest.components.registration.tool 'SDK registration tool'
        cliExecutable = Resolve-KitPath $Manifest.components.runtime.cli 'CLI'
        engine = Resolve-KitPath $Manifest.components.runtime.engine 'Engine'
        nativeSdkRoot = Resolve-KitPath $Manifest.components.nativeSdk.root 'native SDK'
    }
}

try {
    $manifest = Read-KitManifest
    Assert-KitInventory $manifest
    $probePython = $Command -in @('verify', 'paths', 'python-project', 'run')
    Assert-KitCompatibility $manifest -ProbePython:$probePython -Deep:($Command -eq 'verify')
    $paths = Get-KitPaths $manifest

    if ($Command -eq 'verify') {
        [ordered]@{
            status = 'ready'
            kitVersion = $manifest.kitVersion
            platform = $manifest.platform
            components = $manifest.components
            paths = $paths
            checks = @('complete SHA-256 inventory', 'component compatibility', 'private CPython 3.13 x64 GIL', 'venv', 'pip')
        } | ConvertTo-Json -Depth 10
        exit 0
    }
    if ($Command -eq 'paths') {
        $paths | ConvertTo-Json -Depth 4
        exit 0
    }
    if ($Command -in @('new', 'build', 'register', 'run')) {
        . $paths.authoringTool
        if ($Command -in @('register', 'run')) { . $paths.registrationTool }
        if ($Command -eq 'new') {
            Invoke-GuidedNew -Paths $paths -Values @($Arguments)
        }
        elseif ($Command -eq 'build') {
            Invoke-GuidedBuild -Paths $paths -Values @($Arguments)
        }
        elseif ($Command -eq 'register') { Invoke-GuidedRegister -Paths $paths -Values @($Arguments) }
        else { Invoke-GuidedRun -Paths $paths -Values @($Arguments) }
        exit 0
    }
    if (-not $Arguments -or $Arguments.Count -eq 0) {
        Stop-Kit 'ARTESTKIT008' 'python-project requires an existing low-level project.py command.'
    }

    & $paths.privatePython -I -B $paths.pythonProjectTool @Arguments
    $projectExit = $LASTEXITCODE
    if ($projectExit -ne 0) {
        exit $projectExit
    }
    if ($Arguments[0] -eq 'create') {
        if ($Arguments.Count -lt 2) {
            Stop-Kit 'ARTESTKIT008' 'python-project create requires a destination.'
        }
        $projectRoot = [IO.Path]::GetFullPath($Arguments[1])
        $localConfiguration = Join-Path $projectRoot 'artest-project.local.json'
        if (Test-Path -LiteralPath $localConfiguration) {
            Stop-Kit 'ARTESTKIT008' "Refusing to replace local project configuration: $localConfiguration"
        }
        [ordered]@{
            schemaVersion = 1
            python = $paths.privatePython.Replace('\', '/')
            sdkWheel = $paths.pythonSdkWheel.Replace('\', '/')
            cliExecutable = $paths.cliExecutable.Replace('\', '/')
            vendorPaths = [ordered]@{}
            vendorDlls = @()
            planBindings = @()
        } | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $localConfiguration -Encoding utf8
        Write-Host "Kit-local prerequisites: $localConfiguration"
    }
    exit 0
}
catch {
    Write-Error $_.Exception.Message
    exit 1
}
