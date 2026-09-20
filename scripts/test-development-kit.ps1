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
$gitExecutable = @(Get-Command git -CommandType Application -ErrorAction Stop)[0].Source
$version = Get-Content -LiteralPath (
    Join-Path $repositoryRoot 'source\ARTest.SDK\development-kit\development-kit-version.json') -Raw | ConvertFrom-Json
$artifactRoot = [IO.Path]::GetFullPath((Join-Path $repositoryRoot "artifacts\sdk-packages\$Platform\$Configuration"))
$packageName = "ARTestDevelopmentKit-$($version.kitVersion)-evaluation-windows-$Platform"
$packageRoot = Join-Path $artifactRoot $packageName
$archivePath = "$packageRoot.zip"
$runId = (Get-Date).ToUniversalTime().ToString('yyyyMMddTHHmmssZ') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 8)
$externalDrive = [IO.Path]::GetPathRoot($env:SystemRoot)
$testRoot = [IO.Path]::GetFullPath((Join-Path $externalDrive "ARTest Stage4A external $runId"))
$evidenceRoot = [IO.Path]::GetFullPath((Join-Path $repositoryRoot "artifacts\acceptance\py-dx-01\stage4a-candidate\$runId"))
$results = [Collections.Generic.List[object]]::new()
$commands = [Collections.Generic.List[string]]::new()
$fatal = $null

function Invoke-RecordedCase {
    param([string]$Name, [scriptblock]$Action)
    $watch = [Diagnostics.Stopwatch]::StartNew()
    try {
        $detail = & $Action
        $watch.Stop()
        $results.Add([ordered]@{
            name = $Name
            status = 'PASSED'
            durationMs = $watch.ElapsedMilliseconds
            detail = @($detail | ForEach-Object { [string]$_ })
        })
        Write-Host "$Name`: PASSED"
    }
    catch {
        $watch.Stop()
        $results.Add([ordered]@{ name = $Name; status = 'FAILED'; durationMs = $watch.ElapsedMilliseconds; detail = @($_.Exception.Message) })
        throw
    }
}

function Test-ArchiveEntries {
    param([string]$Archive)
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [IO.Compression.ZipFile]::OpenRead($Archive)
    try {
        $seen = @{}
        foreach ($entry in $zip.Entries) {
            $path = $entry.FullName.Replace('\', '/')
            if ([string]::IsNullOrWhiteSpace($path) -or
                [IO.Path]::IsPathRooted($path) -or
                $path.Split('/') -contains '..' -or
                (($entry.ExternalAttributes -shr 16) -band 0xF000) -eq 0xA000) {
                throw "Unsafe development-kit archive entry: $path"
            }
            $key = $path.ToLowerInvariant()
            if ($seen.ContainsKey($key)) {
                throw "Duplicate development-kit archive entry: $path"
            }
            $seen[$key] = $true
        }
    }
    finally {
        $zip.Dispose()
    }
}

function Expand-VerifiedArchive {
    param([string]$Archive, [string]$Destination)
    Test-ArchiveEntries $Archive
    [IO.Compression.ZipFile]::ExtractToDirectory($Archive, $Destination)
}

function Invoke-Kit {
    param([string[]]$KitArguments)
    $output = & $script:entryPoint @KitArguments 2>&1
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) {
        throw "artest.ps1 $($KitArguments -join ' ') failed ($exitCode): $($output | Out-String)"
    }
    return @($output)
}

function Assert-KitFailure {
    param([string]$Code, [string[]]$KitArguments)
    $powerShell = Join-Path $PSHOME 'pwsh.exe'
    $output = & $powerShell -NoLogo -NoProfile -NonInteractive -File $script:entryPoint @KitArguments 2>&1
    $exitCode = $LASTEXITCODE
    if ($exitCode -eq 0 -or ($output | Out-String) -notmatch [regex]::Escape($Code)) {
        throw "Expected $Code, observed exit $exitCode`: $($output | Out-String)"
    }
    return "$Code observed"
}

function ConvertTo-CommandLiteral {
    param([string]$Value)
    return "'" + $Value.Replace("'", "''") + "'"
}

function Lock-RepositoryCheckout {
    $relativePaths = @(& $gitExecutable -C $repositoryRoot -c core.quotepath=false ls-files --cached --others --exclude-standard)
    if ($LASTEXITCODE -ne 0 -or $relativePaths.Count -eq 0) {
        throw 'Could not enumerate the repository checkout for isolation.'
    }
    $locks = [Collections.Generic.List[IO.FileStream]]::new()
    try {
        foreach ($relative in $relativePaths | Sort-Object -Unique) {
            $path = [IO.Path]::GetFullPath((Join-Path $repositoryRoot $relative))
            if (-not $path.StartsWith($repositoryRoot.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase) -or
                -not (Test-Path -LiteralPath $path -PathType Leaf)) {
                throw "Unsafe or missing checkout file while establishing isolation: $relative"
            }
            $locks.Add([IO.File]::Open($path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::None))
        }
        return ,$locks
    }
    catch {
        foreach ($lock in $locks) { $lock.Dispose() }
        throw
    }
}

function Get-Stage4ASourceInventory {
    $paths = @(
        'docs/sdk/development-kit.md',
        'docs/sdk/sdk-distribution.md',
        'scripts/build.ps1',
        'scripts/package-development-kit.ps1',
        'scripts/test-development-kit.ps1',
        'scripts/verify-development-kit.ps1',
        'source/ARTest.Python/tests/test_package.py',
        'source/ARTest.Python/tests/test_project.py',
        'source/ARTest.Python/tools/package.py',
        'source/ARTest.Python/tools/project.py',
        'source/ARTest.SDK/distribution/README.md'
    )
    $paths += Get-ChildItem -LiteralPath (Join-Path $repositoryRoot 'source/ARTest.SDK/development-kit') -Recurse -File |
        ForEach-Object { [IO.Path]::GetRelativePath($repositoryRoot, $_.FullName).Replace('\', '/') }
    return @($paths | Sort-Object -Unique | ForEach-Object {
        $fullPath = Join-Path $repositoryRoot $_
        if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf)) {
            throw "Stage 4A source is missing while recording evidence: $_"
        }
        [ordered]@{
            path = $_.Replace('\', '/')
            sha256 = (Get-FileHash -LiteralPath $fullPath -Algorithm SHA256).Hash.ToLowerInvariant()
        }
    })
}

try {
    if ($testRoot.StartsWith($repositoryRoot.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'The Stage 4A external test root must not be inside the repository checkout.'
    }
    if (-not $testRoot.StartsWith((Join-Path $externalDrive 'ARTest Stage4A external '), [StringComparison]::OrdinalIgnoreCase)) {
        throw "Unsafe Stage 4A external test root: $testRoot"
    }
    $null = New-Item -ItemType Directory -Path $testRoot
    $null = New-Item -ItemType Directory -Path $evidenceRoot

    Invoke-RecordedCase 'Stage 4A source verification' {
        $commands.Add("& $(ConvertTo-CommandLiteral (Join-Path $PSScriptRoot 'verify-development-kit.ps1'))")
        & (Join-Path $PSScriptRoot 'verify-development-kit.ps1')
    }

    Invoke-RecordedCase 'Development-kit packaging without downloads' {
        $arguments = @{
            Configuration = $Configuration
            Platform = $Platform
            PythonRuntime = $PythonRuntime
            DependencyWheelRoot = $DependencyWheelRoot
            VisualStudioPath = $VisualStudioPath
        }
        if ($ProtocPath) { $arguments.ProtocPath = $ProtocPath }
        if ($VisualCRTRoot) { $arguments.VisualCRTRoot = $VisualCRTRoot }
        $packageCommand = "& $(ConvertTo-CommandLiteral (Join-Path $PSScriptRoot 'package-development-kit.ps1')) -Configuration $Configuration -Platform $Platform -PythonRuntime $(ConvertTo-CommandLiteral ([IO.Path]::GetFullPath($PythonRuntime))) -DependencyWheelRoot $(ConvertTo-CommandLiteral ([IO.Path]::GetFullPath($DependencyWheelRoot))) -VisualStudioPath $(ConvertTo-CommandLiteral ([IO.Path]::GetFullPath($VisualStudioPath)))"
        if ($ProtocPath) { $packageCommand += " -ProtocPath $(ConvertTo-CommandLiteral ([IO.Path]::GetFullPath($ProtocPath)))" }
        if ($VisualCRTRoot) { $packageCommand += " -VisualCRTRoot $(ConvertTo-CommandLiteral ([IO.Path]::GetFullPath($VisualCRTRoot)))" }
        $commands.Add($packageCommand)
        & (Join-Path $PSScriptRoot 'package-development-kit.ps1') @arguments
        if (-not (Test-Path -LiteralPath $packageRoot -PathType Container) -or
            -not (Test-Path -LiteralPath $archivePath -PathType Leaf)) {
            throw 'The development-kit directory and archive were not both produced.'
        }
    }

    Invoke-RecordedCase 'Safe archive inventory and extraction' {
        Test-ArchiveEntries $archivePath
        $malicious = Join-Path $testRoot 'unsafe.zip'
        $stream = [IO.File]::Open($malicious, [IO.FileMode]::CreateNew)
        $zip = [IO.Compression.ZipArchive]::new($stream, [IO.Compression.ZipArchiveMode]::Create)
        try {
            $null = $zip.CreateEntry('../escape.txt')
        }
        finally {
            $zip.Dispose()
            $stream.Dispose()
        }
        try {
            Test-ArchiveEntries $malicious
            throw 'Unsafe archive was accepted.'
        }
        catch {
            if ($_.Exception.Message -notmatch 'Unsafe development-kit archive entry') { throw }
        }
        $script:installedRoot = Join-Path $testRoot 'extracted SDK with spaces'
        Expand-VerifiedArchive $archivePath $script:installedRoot
        $script:entryPoint = Join-Path $script:installedRoot 'artest.ps1'
    }

    Invoke-RecordedCase 'Extracted native SDK builds and activates an external C++ consumer' {
        $msbuildPath = Join-Path $VisualStudioPath 'MSBuild\Current\Bin\MSBuild.exe'
        if (-not (Test-Path -LiteralPath $msbuildPath -PathType Leaf)) {
            throw "MSBuild is missing: $msbuildPath"
        }
        $nativeSdkRoot = Join-Path $script:installedRoot 'native-sdk'
        $consumerRoot = Join-Path $testRoot 'external C++ consumer with spaces'
        $extensionRoot = Join-Path $testRoot 'external C++ packages'
        Copy-Item -LiteralPath (Join-Path $nativeSdkRoot 'templates\ARTestExtension') -Destination $consumerRoot -Recurse
        $localProps = '<Project><PropertyGroup><ARTestSDKRoot>' +
            [Security.SecurityElement]::Escape($nativeSdkRoot) +
            '</ARTestSDKRoot></PropertyGroup></Project>'
        Set-Content -LiteralPath (Join-Path $consumerRoot 'ARTestSDK.local.props') -Value $localProps -Encoding utf8
        $consumerProject = Join-Path $consumerRoot 'ARTestExtensionStarter.vcxproj'
        $commands.Add("& $(ConvertTo-CommandLiteral $msbuildPath) $(ConvertTo-CommandLiteral $consumerProject) /m /p:Configuration=$Configuration /p:Platform=$Platform /p:ARTestPackageRoot=$(ConvertTo-CommandLiteral $extensionRoot) /verbosity:minimal")
        & $msbuildPath $consumerProject /m "/p:Configuration=$Configuration" "/p:Platform=$Platform" "/p:ARTestPackageRoot=$extensionRoot" /verbosity:minimal
        if ($LASTEXITCODE -ne 0) { throw "Extracted native SDK consumer build failed: $LASTEXITCODE" }
        $publishedPackage = Join-Path $extensionRoot 'ARTestExtensionStarter'
        $consumerDll = Join-Path $publishedPackage 'ARTestExtensionStarter.dll'
        if (-not (Test-Path -LiteralPath $consumerDll -PathType Leaf) -or
            -not (Test-Path -LiteralPath (Join-Path $publishedPackage '.artest-generated-package.json') -PathType Leaf)) {
            throw 'The extracted native SDK consumer did not publish its owned package.'
        }
        $paths = ((Invoke-Kit @('paths')) | Out-String) | ConvertFrom-Json
        $commands.Add("& $(ConvertTo-CommandLiteral $paths.cliExecutable) extensions doctor $(ConvertTo-CommandLiteral $extensionRoot)")
        $doctor = & $paths.cliExecutable extensions doctor $extensionRoot 2>&1
        if ($LASTEXITCODE -ne 0 -or ($doctor | Out-String) -notmatch '"status"\s*:\s*"active"') {
            throw "The candidate CLI/Engine could not activate the extracted-SDK consumer: $($doctor | Out-String)"
        }
        "consumerDll=$consumerDll"
        "consumerSha256=$((Get-FileHash -LiteralPath $consumerDll -Algorithm SHA256).Hash.ToLowerInvariant())"
    }

    Invoke-RecordedCase 'Kit reparse-point guard rejects a real junction' {
        $reparseTarget = Join-Path $testRoot 'reparse target'
        $reparsePath = Join-Path $script:installedRoot 'reparse-probe'
        $null = New-Item -ItemType Directory -Path $reparseTarget
        try {
            $junction = New-Item -ItemType Junction -Path $reparsePath -Target $reparseTarget
            if (($junction.Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) {
                throw 'The negative test did not create a reparse point.'
            }
            $commands.Add("& $(ConvertTo-CommandLiteral (Join-Path $PSHOME 'pwsh.exe')) -NoProfile -File $(ConvertTo-CommandLiteral $script:entryPoint) paths # expect ARTESTKIT002")
            $null = Assert-KitFailure 'ARTESTKIT002' @('paths')
        }
        finally {
            if (Test-Path -LiteralPath $reparsePath) { Remove-Item -LiteralPath $reparsePath -Force }
        }
        'ARTESTKIT002 observed for a directory junction and the candidate was restored.'
    }

    $previousTemp = $env:TEMP
    $previousTmp = $env:TMP
    $previousPath = $env:PATH
    $previousPythonPath = $env:PYTHONPATH
    $previousIndex = $env:PIP_INDEX_URL
    try {
        $isolatedTemp = Join-Path $testRoot 'temporary files'
        $null = New-Item -ItemType Directory -Path $isolatedTemp
        $env:TEMP = $isolatedTemp
        $env:TMP = $isolatedTemp
        $env:PATH = $PSHOME
        $env:PYTHONPATH = 'Z:\must-not-be-used'
        $env:PIP_INDEX_URL = 'http://127.0.0.1:9/no-network-allowed'

        Push-Location (Join-Path $testRoot 'temporary files')
        try {
            Invoke-RecordedCase 'Extracted kit verification, private venv and pip from another CWD' {
                $commands.Add("& $(ConvertTo-CommandLiteral $script:entryPoint) verify")
                $output = Invoke-Kit @('verify')
                $report = ($output | Out-String) | ConvertFrom-Json
                if ($report.status -ne 'ready' -or $report.paths.kitRoot -ne $script:installedRoot) {
                    throw 'Kit verification returned unexpected relative-discovery evidence.'
                }
                $output
            }

            Invoke-RecordedCase 'Matching CLI/Engine bundle loads from the extracted kit' {
                $paths = ((Invoke-Kit @('paths')) | Out-String) | ConvertFrom-Json
                $emptyCatalog = Join-Path $testRoot 'empty extension catalog'
                $null = New-Item -ItemType Directory -Path $emptyCatalog
                $cliOutput = & $paths.cliExecutable extensions doctor $emptyCatalog 2>&1
                if ($LASTEXITCODE -ne 6 -or
                    ($cliOutput | Out-String) -notmatch 'artest.schema.extension-catalog.v2' -or
                    ($cliOutput | Out-String) -notmatch 'EXTENSION_CATALOG_EMPTY') {
                    throw "Bundled CLI/Engine did not inspect an empty catalog: $($cliOutput | Out-String)"
                }
                $cliOutput
            }

            $projectRoot = Join-Path $testRoot 'external Python project with spaces'
            Invoke-RecordedCase 'Repository-inaccessible create, check, prepare and exact reuse' {
                $isolatedPowerShell = Join-Path $PSHOME 'pwsh.exe'
                $restrictedResult = Join-Path $testRoot 'restricted-flow-result.json'
                $restrictedScript = Join-Path $testRoot 'restricted-flow.ps1'
                $repositorySentinel = Join-Path $repositoryRoot 'scripts\test-development-kit.ps1'
                $template = @'
$ErrorActionPreference = 'Stop'
$entryPoint = {{ENTRY}}
$projectRoot = {{PROJECT}}
$repositoryRoot = {{REPOSITORY}}
$repositorySentinel = {{SENTINEL}}
$resultPath = {{RESULT}}
$workDirectory = {{WORK}}
$result = [ordered]@{ status = 'RUNNING'; error = ''; repositoryAccessible = $null; repositoryProbe = ''; repositoryProbeHResult = $null; preparation = $null; reuse = $null }
try {
    try {
        $stream = [IO.File]::OpenRead($repositorySentinel)
        $stream.Dispose()
        $result.repositoryAccessible = $true
        throw 'Restricted process unexpectedly opened a repository source file.'
    }
    catch [IO.IOException] {
        $result.repositoryAccessible = $false
        $result.repositoryProbe = $_.Exception.GetType().FullName
        $result.repositoryProbeHResult = $_.Exception.HResult
    }
    $env:TEMP = Join-Path $workDirectory 'temp'
    $env:TMP = $env:TEMP
    $null = New-Item -ItemType Directory -Path $env:TEMP -Force
    $env:PATH = [Environment]::GetFolderPath('System')
    $env:PYTHONPATH = 'Z:\must-not-be-used'
    $env:PIP_INDEX_URL = 'http://127.0.0.1:9/no-network-allowed'
    Push-Location $workDirectory
    try {
        & $entryPoint verify | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "Kit verify failed: $LASTEXITCODE" }
        & $entryPoint python-project create $projectRoot --extension-id com.example.stage4a.extension --driver-id com.example.stage4a.driver.simulated-source --command-id com.example.stage4a.command.measure-value --author 'Stage 4A Evaluation' | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "Project create failed: $LASTEXITCODE" }
        $checkText = & $entryPoint python-project check $projectRoot | Out-String
        if ($LASTEXITCODE -ne 0 -or ($checkText | ConvertFrom-Json).success -ne $true) { throw "Project check failed: $checkText" }
        $prepareText = & $entryPoint python-project prepare $projectRoot | Out-String
        $prepared = $prepareText | ConvertFrom-Json
        if ($LASTEXITCODE -ne 0 -or $prepared.success -ne $true -or $prepared.reused -ne $false) { throw "First prepare failed: $prepareText" }
        $reuseText = & $entryPoint python-project prepare $projectRoot | Out-String
        $reused = $reuseText | ConvertFrom-Json
        if ($LASTEXITCODE -ne 0 -or $reused.success -ne $true -or $reused.reused -ne $true -or $reused.preparationId -ne $prepared.preparationId) { throw "Exact reuse failed: $reuseText" }
        $local = Get-Content -LiteralPath (Join-Path $projectRoot 'artest-project.local.json') -Raw | ConvertFrom-Json
        foreach ($path in @($local.python, $local.sdkWheel, $local.cliExecutable)) {
            if (-not ([IO.Path]::GetFullPath($path).StartsWith([IO.Path]::GetDirectoryName($entryPoint).TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase))) {
                throw "Generated local path does not belong to the extracted kit: $path"
            }
        }
        $receipt = Get-Content -LiteralPath $prepared.receipt -Raw | ConvertFrom-Json
        if (-not ([IO.Path]::GetFullPath($receipt.interpreter).StartsWith([IO.Path]::GetDirectoryName($entryPoint).TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase))) {
            throw 'Prepared receipt does not pin the private kit interpreter.'
        }
        $revision = [IO.Path]::GetFullPath($prepared.revision).TrimEnd('\') + '\'
        foreach ($path in @($prepared.package, $prepared.receipt, $prepared.association)) {
            if (-not ([IO.Path]::GetFullPath($path).StartsWith($revision, [StringComparison]::OrdinalIgnoreCase))) {
                throw "Preparation output escaped its immutable revision: $path"
            }
        }
        $needles = @($repositoryRoot, $repositoryRoot.Replace('\', '/'), $repositoryRoot.Replace('\', '\\'))
        foreach ($file in Get-ChildItem -LiteralPath $projectRoot -Recurse -File) {
            $text = [Text.Encoding]::UTF8.GetString([IO.File]::ReadAllBytes($file.FullName))
            foreach ($needle in $needles) {
                if ($text.IndexOf($needle, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
                    throw "Repository path variant leaked into $($file.FullName): $needle"
                }
            }
        }
        $result.preparation = $prepared
        $result.reuse = $reused
    }
    finally { Pop-Location }
    $result.status = 'PASSED'
}
catch {
    $result.status = 'FAILED'
    $result.error = $_.Exception.ToString()
}
finally {
    $result | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $resultPath -Encoding utf8
}
if ($result.status -ne 'PASSED') { exit 1 }
'@
                $wrapper = $template.
                    Replace('{{ENTRY}}', (ConvertTo-CommandLiteral $script:entryPoint)).
                    Replace('{{PROJECT}}', (ConvertTo-CommandLiteral $projectRoot)).
                    Replace('{{REPOSITORY}}', (ConvertTo-CommandLiteral $repositoryRoot)).
                    Replace('{{SENTINEL}}', (ConvertTo-CommandLiteral $repositorySentinel)).
                    Replace('{{RESULT}}', (ConvertTo-CommandLiteral $restrictedResult)).
                    Replace('{{WORK}}', (ConvertTo-CommandLiteral (Join-Path $testRoot 'temporary files')))
                Set-Content -LiteralPath $restrictedScript -Value $wrapper -Encoding utf8
                $arguments = "-NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File `"$restrictedScript`""
                $commands.Add("# separate PowerShell consumer; every Git-visible checkout file held with FileShare.None; repository probe must return ERROR_SHARING_VIOLATION; handles released in finally")
                $commands.Add("& $(ConvertTo-CommandLiteral $isolatedPowerShell) $arguments")
                $commands.Add("& $(ConvertTo-CommandLiteral $script:entryPoint) python-project create $(ConvertTo-CommandLiteral $projectRoot) --extension-id com.example.stage4a.extension --driver-id com.example.stage4a.driver.simulated-source --command-id com.example.stage4a.command.measure-value --author 'Stage 4A Evaluation'")
                $commands.Add("& $(ConvertTo-CommandLiteral $script:entryPoint) python-project check $(ConvertTo-CommandLiteral $projectRoot)")
                $commands.Add("& $(ConvertTo-CommandLiteral $script:entryPoint) python-project prepare $(ConvertTo-CommandLiteral $projectRoot) # first preparation")
                $commands.Add("& $(ConvertTo-CommandLiteral $script:entryPoint) python-project prepare $(ConvertTo-CommandLiteral $projectRoot) # exact reuse")
                $repositoryLocks = Lock-RepositoryCheckout
                try {
                    & $isolatedPowerShell -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $restrictedScript
                    $exitCode = $LASTEXITCODE
                }
                finally {
                    foreach ($lock in $repositoryLocks) { $lock.Dispose() }
                }
                if (-not (Test-Path -LiteralPath $restrictedResult -PathType Leaf)) {
                    throw "Restricted flow produced no result; exit $exitCode"
                }
                $report = Get-Content -LiteralPath $restrictedResult -Raw | ConvertFrom-Json
                if ($exitCode -ne 0 -or $report.status -ne 'PASSED' -or $report.repositoryAccessible -ne $false -or
                    $report.repositoryProbe -ne 'System.IO.IOException' -or
                    $report.repositoryProbeHResult -ne -2147024864) {
                    throw "Restricted flow failed or repository access was not denied: $($report | ConvertTo-Json -Depth 12)"
                }
                $report | Add-Member -NotePropertyName repositoryLockedFileCount -NotePropertyValue $repositoryLocks.Count
                $report | Add-Member -NotePropertyName repositoryLocksReleased -NotePropertyValue $true
                $report | ConvertTo-Json -Depth 12
            }

            Invoke-RecordedCase 'Missing, corrupt, unexpected and incompatible diagnostics' {
                $readme = Join-Path $script:installedRoot 'README.md'
                $readmeBytes = [IO.File]::ReadAllBytes($readme)
                Remove-Item -LiteralPath $readme
                $null = Assert-KitFailure 'ARTESTKIT003' @('paths')
                [IO.File]::WriteAllBytes($readme, $readmeBytes)

                $entryBytes = [IO.File]::ReadAllBytes($script:entryPoint)
                Add-Content -LiteralPath $script:entryPoint -Value '# corruption'
                $null = Assert-KitFailure 'ARTESTKIT004' @('paths')
                [IO.File]::WriteAllBytes($script:entryPoint, $entryBytes)

                $unexpected = Join-Path $script:installedRoot 'unexpected.file'
                Set-Content -LiteralPath $unexpected -Value 'unexpected'
                $null = Assert-KitFailure 'ARTESTKIT005' @('paths')
                Remove-Item -LiteralPath $unexpected

                $manifestPath = Join-Path $script:installedRoot 'sdk-manifest.json'
                $manifestBytes = [IO.File]::ReadAllBytes($manifestPath)
                $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
                $manifest.components.pythonRuntime.version = '3.12.0'
                $manifest | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $manifestPath -Encoding utf8
                $null = Assert-KitFailure 'ARTESTKIT006' @('paths')
                [IO.File]::WriteAllBytes($manifestPath, $manifestBytes)
                'ARTESTKIT003/004/005/006 observed and candidate restored.'
            }
        }
        finally {
            Pop-Location
        }
    }
    finally {
        $env:TEMP = $previousTemp
        $env:TMP = $previousTmp
        $env:PATH = $previousPath
        $env:PYTHONPATH = $previousPythonPath
        $env:PIP_INDEX_URL = $previousIndex
    }

    Invoke-RecordedCase 'Focused Python Stage 1-3 and offline-wheelhouse regressions' {
        $privatePython = Join-Path $script:installedRoot 'python\runtime\python.exe'
        foreach ($test in @('test_package.py', 'test_project.py')) {
            $testPath = Join-Path $repositoryRoot "source\ARTest.Python\tests\$test"
            $commands.Add("& $(ConvertTo-CommandLiteral $privatePython) -I -B $(ConvertTo-CommandLiteral $testPath) -v")
            & $privatePython -I -B $testPath -v
            if ($LASTEXITCODE -ne 0) { throw "$test failed." }
        }
    }
}
catch {
    $fatal = $_
}
finally {
    $null = New-Item -ItemType Directory -Path $evidenceRoot -Force
    $sourceFiles = @()
    try { $sourceFiles = @(Get-Stage4ASourceInventory) }
    catch {
        if (-not $fatal) { $fatal = $_ }
        $results.Add([ordered]@{ name = 'Stage 4A source hash inventory'; status = 'FAILED'; durationMs = 0; detail = @($_.Exception.Message) })
    }
    $candidate = [ordered]@{
        schema = 'artest.schema.py-dx-01-stage4a-evidence.v1'
        runId = $runId
        createdAtUtc = (Get-Date).ToUniversalTime().ToString('o')
        scope = 'PY-DX-01 Stage 4A only'
        configuration = $Configuration
        platform = $Platform
        gitHead = (& $gitExecutable -C $repositoryRoot rev-parse HEAD | Out-String).Trim()
        gitStatus = @(& $gitExecutable -C $repositoryRoot status --short)
        sourceFiles = $sourceFiles
        package = $packageRoot
        archive = $archivePath
        archiveSha256 = if (Test-Path -LiteralPath $archivePath) { (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant() } else { $null }
        manifestSha256 = if (Test-Path -LiteralPath (Join-Path $packageRoot 'sdk-manifest.json')) { (Get-FileHash -LiteralPath (Join-Path $packageRoot 'sdk-manifest.json') -Algorithm SHA256).Hash.ToLowerInvariant() } else { $null }
        result = if ($fatal) { 'FAILED' } else { 'PASSED' }
        evidenceProvenance = 'evidence-provenance.json'
        limitations = @(
            'Local evaluation only; no public release, signing or feed.',
            'Stage 4B wizard/build, Stage 4C registration, Stage 4 execution and Stage 5 acceptance are not implemented.',
            'No hardware or vendor-tool installation was attempted.'
        )
    }
    $candidate | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $evidenceRoot 'candidate.json') -Encoding utf8
    $results | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $evidenceRoot 'test-results.json') -Encoding utf8
    $commands | Set-Content -LiteralPath (Join-Path $evidenceRoot 'commands.txt') -Encoding utf8

    $referencedCommands = @(
        "& $(ConvertTo-CommandLiteral (Join-Path $PSScriptRoot 'build.ps1')) -Configuration Debug -Platform x64",
        "& $(ConvertTo-CommandLiteral (Join-Path $PSScriptRoot 'build.ps1')) -Configuration Release -Platform x64",
        "& $(ConvertTo-CommandLiteral (Join-Path $PSScriptRoot 'test-python-runtime.ps1')) -Configuration Debug -PythonRoot $(ConvertTo-CommandLiteral (Join-Path $repositoryRoot 'artifacts\python-c01-final'))",
        "& $(ConvertTo-CommandLiteral (Join-Path $PSScriptRoot 'test-python-runtime.ps1')) -Configuration Release -PythonRoot $(ConvertTo-CommandLiteral (Join-Path $repositoryRoot 'artifacts\python-c01-final'))",
        "& $(ConvertTo-CommandLiteral (Join-Path $PSScriptRoot 'test-sdk-authoring.ps1')) -Configuration Debug",
        "& $(ConvertTo-CommandLiteral (Join-Path $PSScriptRoot 'test-sdk-authoring.ps1')) -Configuration Release",
        "& $(ConvertTo-CommandLiteral (Join-Path $PSScriptRoot 'test-native-compatibility.ps1')) -BaselineDirectory $(ConvertTo-CommandLiteral (Join-Path $repositoryRoot 'artifacts\compatibility\baselines\sdk-0.2.1-native-v1-x64-Release')) -Configuration Debug",
        "& $(ConvertTo-CommandLiteral (Join-Path $PSScriptRoot 'test-native-compatibility.ps1')) -BaselineDirectory $(ConvertTo-CommandLiteral (Join-Path $repositoryRoot 'artifacts\compatibility\baselines\sdk-0.2.1-native-v1-x64-Release')) -Configuration Release",
        "& $(ConvertTo-CommandLiteral (Join-Path $PSScriptRoot 'test-native-compatibility-guards.ps1')) -BaselineDirectory $(ConvertTo-CommandLiteral (Join-Path $repositoryRoot 'artifacts\compatibility\baselines\sdk-0.2.1-native-v1-x64-Release')) -EnginePath $(ConvertTo-CommandLiteral (Join-Path $repositoryRoot 'artifacts\bin\x64\Release\ARTestEngine.dll'))"
    )
    @('# Previously executed gates whose immutable report copies are referenced by this candidate.') + $referencedCommands |
        Set-Content -LiteralPath (Join-Path $evidenceRoot 'referenced-commands.txt') -Encoding utf8

    $reportRecords = [Collections.Generic.List[object]]::new()
    $reportRoot = Join-Path $evidenceRoot 'reports'
    $null = New-Item -ItemType Directory -Path $reportRoot -Force
    $reports = @(
        [ordered]@{ name = 'unit-debug.xml'; path = Join-Path $repositoryRoot 'artifacts\test-results\x64\Debug\ARTestCLI.UnitTests.xml' },
        [ordered]@{ name = 'unit-debug.html'; path = Join-Path $repositoryRoot 'artifacts\test-results\x64\Debug\ARTestCLI.UnitTests.html' },
        [ordered]@{ name = 'unit-release.xml'; path = Join-Path $repositoryRoot 'artifacts\test-results\x64\Release\ARTestCLI.UnitTests.xml' },
        [ordered]@{ name = 'unit-release.html'; path = Join-Path $repositoryRoot 'artifacts\test-results\x64\Release\ARTestCLI.UnitTests.html' },
        [ordered]@{ name = 'python-debug.xml'; path = Join-Path $repositoryRoot 'artifacts\test-results\x64\Debug\ARTestPython.Integration.xml' },
        [ordered]@{ name = 'python-debug.html'; path = Join-Path $repositoryRoot 'artifacts\test-results\x64\Debug\ARTestPython.Integration.html' },
        [ordered]@{ name = 'python-release.xml'; path = Join-Path $repositoryRoot 'artifacts\test-results\x64\Release\ARTestPython.Integration.xml' },
        [ordered]@{ name = 'python-release.html'; path = Join-Path $repositoryRoot 'artifacts\test-results\x64\Release\ARTestPython.Integration.html' }
    )
    foreach ($configurationName in @('Debug', 'Release')) {
        $compatibilityRoot = Join-Path $repositoryRoot "artifacts\test-results\x64\$configurationName\native-compatibility"
        $selected = Get-ChildItem -LiteralPath $compatibilityRoot -Directory -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -match '^\d{8}T\d{6}-[0-9a-f]{8}$' -and (Test-Path -LiteralPath (Join-Path $_.FullName 'compatibility.json')) } |
            Sort-Object LastWriteTimeUtc -Descending |
            Select-Object -First 1
        if ($selected) {
            foreach ($extension in @('json', 'xml', 'html')) {
                $reports += [ordered]@{
                    name = "native-compatibility-$($configurationName.ToLowerInvariant()).$extension"
                    path = Join-Path $selected.FullName "compatibility.$extension"
                }
            }
        }
    }
    foreach ($report in $reports) {
        if (-not (Test-Path -LiteralPath $report.path -PathType Leaf)) {
            if (-not $fatal) { $fatal = [InvalidOperationException]::new("Referenced report is missing: $($report.path)") }
            continue
        }
        $destination = Join-Path $reportRoot $report.name
        Copy-Item -LiteralPath $report.path -Destination $destination
        $reportRecords.Add([ordered]@{
            source = [IO.Path]::GetFullPath($report.path)
            copy = [IO.Path]::GetRelativePath($evidenceRoot, $destination).Replace('\', '/')
            sha256 = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant()
            size = (Get-Item -LiteralPath $destination).Length
        })
    }

    $candidate.result = if ($fatal) { 'FAILED' } else { 'PASSED' }
    $candidate | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $evidenceRoot 'candidate.json') -Encoding utf8

    $restrictedEvidenceRoot = Join-Path $evidenceRoot 'restricted-flow'
    $null = New-Item -ItemType Directory -Path $restrictedEvidenceRoot -Force
    foreach ($name in @('restricted-flow.ps1', 'restricted-flow-result.json')) {
        $path = Join-Path $testRoot $name
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            Copy-Item -LiteralPath $path -Destination (Join-Path $restrictedEvidenceRoot $name)
        }
    }

    $binaryRecords = [Collections.Generic.List[object]]::new()
    $binaryPaths = @(
        $archivePath,
        (Join-Path $packageRoot 'sdk-manifest.json'),
        (Join-Path $packageRoot 'runtime\x64\Release\ARTestCLI.exe'),
        (Join-Path $packageRoot 'runtime\x64\Release\ARTestEngine.dll'),
        (Join-Path $packageRoot 'python\runtime\python.exe'),
        (Join-Path $packageRoot 'python\wheels\artest_python-0.2.0-py3-none-any.whl'),
        (Join-Path $packageRoot 'native-sdk\sdk-manifest.json'),
        (Join-Path $repositoryRoot 'artifacts\bin\x64\Debug\ARTestEngine.dll'),
        (Join-Path $repositoryRoot 'artifacts\bin\x64\Release\ARTestEngine.dll'),
        (Join-Path $repositoryRoot 'artifacts\compatibility\baselines\sdk-0.2.1-native-v1-x64-Release\bin\ARTestCompatHost.exe'),
        (Join-Path $repositoryRoot 'artifacts\compatibility\baselines\sdk-0.2.1-native-v1-x64-Release\catalog\ARTestCompatExtension\ARTestCompatExtension.dll'),
        (Join-Path $testRoot 'external C++ packages\ARTestExtensionStarter\ARTestExtensionStarter.dll')
    )
    foreach ($binary in $binaryPaths | Sort-Object -Unique) {
        if (Test-Path -LiteralPath $binary -PathType Leaf) {
            $binaryRecords.Add([ordered]@{
                path = [IO.Path]::GetFullPath($binary)
                sha256 = (Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash.ToLowerInvariant()
                size = (Get-Item -LiteralPath $binary).Length
            })
        }
    }

    $evidenceFiles = @(
        Get-ChildItem -LiteralPath $evidenceRoot -Recurse -File |
            Where-Object { $_.Name -ne 'evidence-provenance.json' } |
            Sort-Object FullName |
            ForEach-Object {
                [ordered]@{
                    path = [IO.Path]::GetRelativePath($evidenceRoot, $_.FullName).Replace('\', '/')
                    sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
                    size = $_.Length
                }
            }
    )
    [ordered]@{
        schema = 'artest.schema.py-dx-01-stage4a-provenance.v1'
        createdAtUtc = (Get-Date).ToUniversalTime().ToString('o')
        sourceFiles = $sourceFiles
        reports = @($reportRecords)
        binaries = @($binaryRecords)
        evidenceFiles = $evidenceFiles
    } | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $evidenceRoot 'evidence-provenance.json') -Encoding utf8

    if (-not $fatal -and (Test-Path -LiteralPath $testRoot)) {
        Remove-Item -LiteralPath $testRoot -Recurse -Force
    }
}

if ($fatal) { throw $fatal }
Write-Host "Stage 4A development-kit tests: PASSED"
Write-Host "Candidate evidence: $evidenceRoot"
