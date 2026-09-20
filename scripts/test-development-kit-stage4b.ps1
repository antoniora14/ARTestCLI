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
$git = @(Get-Command git -CommandType Application -ErrorAction Stop)[0].Source
$version = Get-Content -LiteralPath (
    Join-Path $repositoryRoot 'source\ARTest.SDK\development-kit\development-kit-version.json') -Raw | ConvertFrom-Json
$packageName = "ARTestDevelopmentKit-$($version.kitVersion)-evaluation-windows-$Platform"
$packageRoot = Join-Path $repositoryRoot "artifacts\sdk-packages\$Platform\$Configuration\$packageName"
$archivePath = "$packageRoot.zip"
$runId = (Get-Date).ToUniversalTime().ToString('yyyyMMddTHHmmssZ') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 8)
$externalDrive = [IO.Path]::GetPathRoot($env:SystemRoot)
$testToken = $runId.Substring($runId.Length - 8)
$testRoot = [IO.Path]::GetFullPath((Join-Path $externalDrive "ARTest 4B $testToken"))
$evidenceRoot = Join-Path $repositoryRoot "artifacts\acceptance\py-dx-01\stage4b-candidate\$runId"
$results = [Collections.Generic.List[object]]::new()
$commands = [Collections.Generic.List[string]]::new()
$fatal = $null

function Quote-CommandArgument {
    param([string]$Value)
    return "'" + $Value.Replace("'", "''") + "'"
}

function Invoke-Case {
    param([string]$Name, [scriptblock]$Action)
    $watch = [Diagnostics.Stopwatch]::StartNew()
    try {
        $detail = @(& $Action)
        $watch.Stop()
        $results.Add([ordered]@{
            name = $Name; status = 'PASSED'; durationMs = $watch.ElapsedMilliseconds
            detail = @($detail | ForEach-Object { [string]$_ })
        })
        Write-Host "$Name`: PASSED"
    }
    catch {
        $watch.Stop()
        $results.Add([ordered]@{
            name = $Name; status = 'FAILED'; durationMs = $watch.ElapsedMilliseconds
            detail = @($_.Exception.ToString())
        })
        throw
    }
}

function Invoke-Kit {
    param([string[]]$KitArguments)
    $commands.Add("& $(Quote-CommandArgument $script:entry) " + (($KitArguments | ForEach-Object { Quote-CommandArgument $_ }) -join ' '))
    $output = & $script:entry @KitArguments 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "artest.ps1 $($KitArguments -join ' ') failed: $($output | Out-String)"
    }
    return @($output)
}

function Assert-KitFailure {
    param([string]$Expected, [string[]]$KitArguments)
    $powerShell = Join-Path $PSHOME 'pwsh.exe'
    $commands.Add("& $(Quote-CommandArgument $powerShell) -NoProfile -NonInteractive -File $(Quote-CommandArgument $script:entry) " +
        (($KitArguments | ForEach-Object { Quote-CommandArgument $_ }) -join ' ') + " # expect $Expected")
    $output = & $powerShell -NoLogo -NoProfile -NonInteractive -File $script:entry @KitArguments 2>&1
    if ($LASTEXITCODE -eq 0 -or ($output | Out-String) -notmatch [regex]::Escape($Expected)) {
        throw "Expected $Expected, observed exit $LASTEXITCODE`: $($output | Out-String)"
    }
    return "$Expected observed"
}

function ConvertFrom-LastJsonObject {
    param([object[]]$Output)
    $text = ($Output | Out-String)
    $start = $text.LastIndexOf("`n{")
    if ($start -lt 0 -and $text.TrimStart().StartsWith('{')) { $start = -1 }
    $json = if ($start -ge 0) { $text.Substring($start + 1) } else { $text }
    try { return $json | ConvertFrom-Json }
    catch { throw "Command produced no final JSON object: $text" }
}

function Invoke-InteractiveKit {
    param([string]$Command, [string[]]$InputLines)
    $powerShell = Join-Path $PSHOME 'pwsh.exe'
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $powerShell
    $start.UseShellExecute = $false
    $start.RedirectStandardInput = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    foreach ($argument in @('-NoLogo', '-NoProfile', '-File', $script:entry, $Command)) {
        $null = $start.ArgumentList.Add($argument)
    }
    $process = [Diagnostics.Process]::Start($start)
    foreach ($line in $InputLines) { $process.StandardInput.WriteLine($line) }
    $process.StandardInput.Close()
    $output = $process.StandardOutput.ReadToEnd()
    $errorText = $process.StandardError.ReadToEnd()
    $process.WaitForExit()
    $exitCode = $process.ExitCode
    $process.Dispose()
    $commands.Add("# interactive: artest.ps1 $Command <= " + (($InputLines | ForEach-Object { Quote-CommandArgument $_ }) -join ', '))
    if ($exitCode -ne 0) { throw "Interactive $Command failed ($exitCode): $output $errorText" }
    return $output
}

function Assert-IdentityCoherence {
    param([string]$Project)
    $guided = Get-Content -LiteralPath (Join-Path $Project 'artest-sdk-project.json') -Raw | ConvertFrom-Json
    $sourcePath = if ($guided.language -eq 'python') { Join-Path $Project 'src\extension.py' } else { Join-Path $Project 'Extension.cpp' }
    $source = Get-Content -LiteralPath $sourcePath -Raw
    $plan = Get-Content -LiteralPath (Join-Path $Project ([string]$guided.plan)) -Raw
    foreach ($identity in @($guided.extensionId, $guided.driverId, $guided.commandId, $guided.contractId) | Where-Object { $_ }) {
        if ($source.IndexOf([string]$identity, [StringComparison]::Ordinal) -lt 0) {
            throw "Portable identity is absent from source: $identity"
        }
    }
    if ($guided.driverId -and $plan.IndexOf([string]$guided.driverId, [StringComparison]::Ordinal) -lt 0) {
        throw 'Driver ID is absent from the generated plan.'
    }
    if ($guided.commandId -and $plan.IndexOf([string]$guided.commandId, [StringComparison]::Ordinal) -lt 0) {
        throw 'Command ID is absent from the generated plan.'
    }
    return $guided
}

function Assert-NoMachinePathsInPortableFiles {
    param([string]$Project, [string[]]$Needles)
    $excluded = @('artest-project.local.json', 'artest-sdk-project.local.json', 'ARTestSDK.local.props')
    foreach ($file in Get-ChildItem -LiteralPath $Project -Recurse -File |
            Where-Object { $_.Name -notin $excluded -and $_.FullName -notmatch '[\\/]\.artest[\\/]' }) {
        $text = [Text.Encoding]::UTF8.GetString([IO.File]::ReadAllBytes($file.FullName))
        foreach ($needle in $Needles) {
            if ($text.IndexOf($needle, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
                throw "Machine path leaked into portable file $($file.FullName): $needle"
            }
        }
    }
}

function Build-And-InspectPython {
    param([string]$Project)
    $build = ConvertFrom-LastJsonObject (Invoke-Kit @('build', '--project', $Project))
    if ($build.status -ne 'built' -or $build.language -ne 'python' -or
        -not (Test-Path -LiteralPath $build.package -PathType Container) -or
        -not (Test-Path -LiteralPath $build.receipt -PathType Leaf)) {
        throw "Python build result is incomplete: $($build | ConvertTo-Json -Depth 8)"
    }
    $manifest = Get-Content -LiteralPath (Join-Path $build.package 'artest-extension.json') -Raw | ConvertFrom-Json
    $guided = Get-Content -LiteralPath (Join-Path $Project 'artest-sdk-project.json') -Raw | ConvertFrom-Json
    if ($manifest.extensionId -ne $guided.extensionId) { throw 'Prepared Python manifest changed the persistent extension ID.' }
    return $build
}

function Build-And-InspectCpp {
    param([string]$Project)
    $output = Invoke-Kit @('build', '--project', $Project)
    $build = ConvertFrom-LastJsonObject $output
    if ($build.status -ne 'built' -or $build.language -ne 'cpp' -or
        -not (Test-Path -LiteralPath $build.binary -PathType Leaf) -or
        -not (Test-Path -LiteralPath (Join-Path $build.package '.artest-generated-package.json') -PathType Leaf)) {
        throw "C++ build result is incomplete: $($build | ConvertTo-Json -Depth 8)"
    }
    $manifest = Get-Content -LiteralPath (Join-Path $build.package 'artest-extension.json') -Raw | ConvertFrom-Json
    $guided = Get-Content -LiteralPath (Join-Path $Project 'artest-sdk-project.json') -Raw | ConvertFrom-Json
    if ($manifest.extensionId -ne $guided.extensionId) { throw 'Published C++ manifest changed the persistent extension ID.' }
    return $build
}

function Assert-CommandOnlyPlan {
    param([string]$Project)
    $guided = Get-Content -LiteralPath (Join-Path $Project 'artest-sdk-project.json') -Raw | ConvertFrom-Json
    $plan = Get-Content -LiteralPath (Join-Path $Project ([string]$guided.plan)) -Raw | ConvertFrom-Json
    $instrument = @($plan.instruments)
    $command = @($plan.commands)
    if ($guided.variant -ne 'command-only' -or
        $guided.contractId -ne 'com.example.artest.contract.value-source.v1' -or
        $guided.externalDriverId -ne 'com.example.artest.driver.sim-value-source' -or
        $instrument.Count -ne 1 -or $instrument[0].type -ne $guided.externalDriverId -or
        @($instrument[0].config.PSObject.Properties.Name).Count -ne 1 -or
        $instrument[0].config.initialValue -ne 42 -or
        $command.Count -ne 1 -or $command[0].name -ne $guided.commandId -or
        @($command[0].params.PSObject.Properties.Name).Count -ne 1 -or
        $command[0].params.factor -ne 2) {
        throw "Command-only plan does not match the existing native value-source driver: $Project"
    }
    return $guided
}

function Invoke-ExistingRuntime {
    param(
        [string]$Plan,
        [string]$Catalog,
        [AllowNull()][string]$Association
    )
    $paths = ConvertFrom-LastJsonObject (Invoke-Kit @('paths'))
    $arguments = @('extension-run', $Plan, $Catalog)
    if ($Association) { $arguments += @('--python-environments', $Association) }
    $commands.Add("& $(Quote-CommandArgument $paths.cliExecutable) " +
        (($arguments | ForEach-Object { Quote-CommandArgument $_ }) -join ' '))
    $output = & $paths.cliExecutable @arguments 2>&1
    $exitCode = $LASTEXITCODE
    $text = $output | Out-String
    $report = $null
    try { $report = ConvertFrom-LastJsonObject @($output) }
    catch { }
    if ($exitCode -ne 0 -or $null -eq $report -or $report.status -ne 'passed') {
        throw "Existing runtime could not execute the generated command-only project ($exitCode): $text"
    }
    return [pscustomobject]@{ Text = $text; Report = $report }
}

function Get-Stage4BSourceInventory {
    $paths = @(
        'docs/sdk/development-kit.md',
        'scripts/package-development-kit.ps1',
        'scripts/test-development-kit-stage4b.ps1',
        'scripts/test-development-kit.ps1',
        'scripts/verify-development-kit.ps1'
    )
    $paths += Get-ChildItem -LiteralPath (Join-Path $repositoryRoot 'source\ARTest.SDK\development-kit') -Recurse -File |
        ForEach-Object { [IO.Path]::GetRelativePath($repositoryRoot, $_.FullName).Replace('\', '/') }
    return @($paths | Sort-Object -Unique | ForEach-Object {
        $path = Join-Path $repositoryRoot $_
        [ordered]@{ path = $_.Replace('\', '/'); sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() }
    })
}

try {
    if ($testRoot.StartsWith($repositoryRoot.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'The Stage 4B test root must be outside the repository.'
    }
    $null = New-Item -ItemType Directory -Path $testRoot
    $null = New-Item -ItemType Directory -Path $evidenceRoot -Force

    Invoke-Case 'Stage 4B source and scope verification' {
        & (Join-Path $PSScriptRoot 'verify-development-kit.ps1')
    }

    Invoke-Case 'Package and extract the current kit outside the repository' {
        $arguments = @{
            Configuration = $Configuration; Platform = $Platform
            PythonRuntime = $PythonRuntime; DependencyWheelRoot = $DependencyWheelRoot
            VisualStudioPath = $VisualStudioPath
        }
        if ($ProtocPath) { $arguments.ProtocPath = $ProtocPath }
        if ($VisualCRTRoot) { $arguments.VisualCRTRoot = $VisualCRTRoot }
        $commands.Add("& $(Quote-CommandArgument (Join-Path $PSScriptRoot 'package-development-kit.ps1')) # exact parameters recorded in candidate.json")
        & (Join-Path $PSScriptRoot 'package-development-kit.ps1') @arguments
        if (-not (Test-Path -LiteralPath $archivePath -PathType Leaf)) { throw "Missing kit archive: $archivePath" }
        $script:kit = Join-Path $testRoot 'Extracted SDK with spaces'
        Expand-Archive -LiteralPath $archivePath -DestinationPath $script:kit
        $script:entry = Join-Path $script:kit 'artest.ps1'
        $verification = ConvertFrom-LastJsonObject (Invoke-Kit @('verify'))
        if ($verification.status -ne 'ready' -or $verification.kitVersion -ne $version.kitVersion) {
            throw "Extracted kit did not verify as version $($version.kitVersion)."
        }
        "archiveSha256=$((Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant())"
    }

    $projects = Join-Path $testRoot 'Author projects with spaces'
    $work = Join-Path $testRoot 'Different working directory'
    $null = New-Item -ItemType Directory -Path $projects
    $null = New-Item -ItemType Directory -Path $work
    $msbuild = Join-Path $VisualStudioPath 'MSBuild\Current\Bin\MSBuild.exe'

    Push-Location $work
    try {
        Invoke-Case 'Python non-interactive create, repeat build, and changed source' {
            $project = Join-Path $projects 'Python Guided Default'
            $created = ConvertFrom-LastJsonObject (Invoke-Kit @('new', '--name', 'Python Guided Default', '--folder', $projects, '--language', 'python'))
            if ($created.variant -ne 'driver-command' -or $created.project -ne $project) { throw 'Python default creation returned unexpected metadata.' }
            $guided = Assert-IdentityCoherence $project
            $local = Get-Content -LiteralPath (Join-Path $project 'artest-project.local.json') -Raw | ConvertFrom-Json
            foreach ($path in @($local.python, $local.sdkWheel, $local.cliExecutable)) {
                if (-not ([IO.Path]::GetFullPath($path).StartsWith($script:kit.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase))) {
                    throw "Python local path was not derived from the extracted kit: $path"
                }
            }
            Assert-NoMachinePathsInPortableFiles $project @($script:kit, $repositoryRoot, $msbuild)
            $first = Build-And-InspectPython $project
            $second = Build-And-InspectPython $project
            if ($second.reused -ne $true -or $second.preparationId -ne $first.preparationId) { throw 'Repeated Python build did not reuse the exact revision.' }
            Add-Content -LiteralPath (Join-Path $project 'src\extension.py') -Value "`n# author source change"
            $changed = Build-And-InspectPython $project
            if ($changed.reused -ne $false -or $changed.preparationId -eq $first.preparationId) { throw 'Python source edit did not select a new preparation.' }
            "extensionId=$($guided.extensionId)"
            "first=$($first.preparationId) changed=$($changed.preparationId)"
        }

        Invoke-Case 'Python interactive driver-only and explicit command-only variants build' {
            $interactiveName = 'Python Interactive Driver'
            $interactiveProject = Join-Path $projects $interactiveName
            $null = Invoke-InteractiveKit 'new' @($interactiveName, $projects, 'python', 'driver-only')
            $driverConfig = Assert-IdentityCoherence $interactiveProject
            if ($driverConfig.variant -ne 'driver-only' -or $driverConfig.commandId) { throw 'Interactive Python driver-only selection was not preserved.' }
            $interactiveBuild = Invoke-InteractiveKit 'build' @($interactiveProject)
            if ($interactiveBuild -notmatch '"status"\s*:\s*"built"') { throw 'Interactive Python build did not report success.' }

            # Keep the activation path short enough for the existing Windows
            # environment inventory validator; the spaced-path cases above
            # still exercise authoring and preparation from the extracted kit.
            $commandProject = Join-Path $testRoot 'P'
            $null = Invoke-Kit @('new', '--name', 'P', '--folder', $testRoot, '--language', 'python', '--variant', 'command-only')
            $commandConfig = Assert-IdentityCoherence $commandProject
            if ($commandConfig.driverId -or -not $commandConfig.externalDriverId) { throw 'Python command-only external driver reference is inconsistent.' }
            $null = Assert-CommandOnlyPlan $commandProject
            $script:pythonCommandProject = $commandProject
            $script:pythonCommandBuild = Build-And-InspectPython $commandProject
            'python variants prepared'
        }

        Invoke-Case 'C++ non-interactive create, repeat build, source edit, and IDE settings' {
            $project = Join-Path $projects 'Native Guided Default'
            $null = Invoke-Kit @('new', '--name', 'Native Guided Default', '--folder', $projects, '--language', 'cpp', '--msbuild', $msbuild)
            $guided = Assert-IdentityCoherence $project
            $local = Get-Content -LiteralPath (Join-Path $project 'artest-sdk-project.local.json') -Raw | ConvertFrom-Json
            $props = Get-Content -LiteralPath (Join-Path $project 'ARTestSDK.local.props') -Raw
            $ignore = Get-Content -LiteralPath (Join-Path $project '.gitignore')
            if ([IO.Path]::GetFullPath($local.msbuild) -ne [IO.Path]::GetFullPath($msbuild) -or
                $props -notmatch [regex]::Escape((Join-Path $script:kit 'native-sdk')) -or
                $ignore -notcontains 'artest-sdk-project.local.json') {
                throw 'C++ machine selections are not confined to ignored local configuration.'
            }
            Assert-NoMachinePathsInPortableFiles $project @($script:kit, $repositoryRoot, $msbuild)
            $first = Build-And-InspectCpp $project
            $firstHash = (Get-FileHash -LiteralPath $first.binary -Algorithm SHA256).Hash
            $repeat = Build-And-InspectCpp $project
            $repeatHash = (Get-FileHash -LiteralPath $repeat.binary -Algorithm SHA256).Hash
            if ($repeatHash -ne $firstHash) { throw 'Unchanged repeated C++ build changed the extension binary.' }
            $header = Join-Path $project 'SimulatedValueSource.h'
            $text = Get-Content -LiteralPath $header -Raw
            Set-Content -LiteralPath $header -Value $text.Replace('42.0', '43.0') -Encoding utf8
            $changed = Build-And-InspectCpp $project
            $changedHash = (Get-FileHash -LiteralPath $changed.binary -Algorithm SHA256).Hash
            if ($changedHash -eq $firstHash) { throw 'C++ author source edit did not change the rebuilt binary.' }
            "extensionId=$($guided.extensionId)"
            "firstSha256=$($firstHash.ToLowerInvariant()) changedSha256=$($changedHash.ToLowerInvariant())"
        }

        Invoke-Case 'C++ template-name collisions retain their project and build' {
            foreach ($name in @('ARTest Extension Starter', 'ARTestExtensionStarter', 'Normal Native Project')) {
                $project = Join-Path $projects $name
                $created = ConvertFrom-LastJsonObject (Invoke-Kit @(
                    'new', '--name', $name, '--folder', $projects, '--language', 'cpp', '--msbuild', $msbuild))
                $guided = Assert-IdentityCoherence $project
                $projectFile = Join-Path $project ([string]$guided.projectFile)
                if (-not (Test-Path -LiteralPath $projectFile -PathType Leaf)) {
                    throw "C++ new removed its generated project for '$name': $projectFile"
                }
                if ($created.project -ne $project) { throw "C++ new returned the wrong project for '$name'." }
                $null = Build-And-InspectCpp $project
            }
            'collision names and normal control retained and built their vcxproj files'
        }

        Invoke-Case 'C++ interactive driver-only and explicit command-only variants build' {
            $interactiveName = 'Native Interactive Driver'
            $interactiveProject = Join-Path $projects $interactiveName
            $null = Invoke-InteractiveKit 'new' @($interactiveName, $projects, 'cpp', 'driver-only')
            $driverConfig = Assert-IdentityCoherence $interactiveProject
            if ($driverConfig.variant -ne 'driver-only' -or $driverConfig.commandId) { throw 'Interactive C++ driver-only selection was not preserved.' }
            $interactiveBuild = Invoke-InteractiveKit 'build' @($interactiveProject)
            if ($interactiveBuild -notmatch '"status"\s*:\s*"built"') { throw 'Interactive C++ build did not report success.' }

            $commandProject = Join-Path $projects 'Native Command Only'
            $null = Invoke-Kit @('new', '--name', 'Native Command Only', '--folder', $projects, '--language', 'cpp', '--variant', 'command-only', '--msbuild', $msbuild)
            $commandConfig = Assert-IdentityCoherence $commandProject
            if ($commandConfig.driverId -or -not $commandConfig.externalDriverId) { throw 'C++ command-only external driver reference is inconsistent.' }
            $null = Assert-CommandOnlyPlan $commandProject
            $script:cppCommandProject = $commandProject
            $script:cppCommandBuild = Build-And-InspectCpp $commandProject
            'native variants built'
        }

        Invoke-Case 'Python and C++ command-only projects execute against the existing native driver' {
            $referenceProjectRoot = Join-Path $testRoot 'Existing native driver project'
            $referencePackages = Join-Path $testRoot 'Existing native driver packages'
            Copy-Item -LiteralPath (Join-Path $script:kit 'native-sdk\templates\ARTestExtension') `
                -Destination $referenceProjectRoot -Recurse
            $referenceProject = Join-Path $referenceProjectRoot 'ARTestExtensionStarter.vcxproj'
            $nativeSdkRoot = Join-Path $script:kit 'native-sdk'
            $commands.Add("& $(Quote-CommandArgument $msbuild) $(Quote-CommandArgument $referenceProject) /m " +
                "/p:Configuration=Release /p:Platform=x64 /p:ARTestSDKRoot=$(Quote-CommandArgument $nativeSdkRoot) " +
                "/p:ARTestPackageRoot=$(Quote-CommandArgument $referencePackages) /verbosity:minimal")
            $referenceOutput = & $msbuild $referenceProject /m '/p:Configuration=Release' '/p:Platform=x64' `
                "/p:ARTestSDKRoot=$nativeSdkRoot" "/p:ARTestPackageRoot=$referencePackages" '/verbosity:minimal' 2>&1
            if ($LASTEXITCODE -ne 0) {
                throw "Could not build the existing native starter driver: $($referenceOutput | Out-String)"
            }
            $referencePackage = Join-Path $referencePackages 'ARTestExtensionStarter'
            if (-not (Test-Path -LiteralPath (Join-Path $referencePackage 'artest-extension.json') -PathType Leaf)) {
                throw 'The existing native starter driver package was not published.'
            }

            $pythonCatalog = Join-Path $testRoot 'Python command native driver catalog'
            $null = New-Item -ItemType Directory -Path $pythonCatalog
            Copy-Item -LiteralPath $referencePackage -Destination (Join-Path $pythonCatalog 'ExistingNativeDriver') -Recurse
            Copy-Item -LiteralPath $script:pythonCommandBuild.package -Destination (Join-Path $pythonCatalog 'PythonCommand') -Recurse
            $pythonRevision = Split-Path -Parent (Split-Path -Parent ([string]$script:pythonCommandBuild.receipt))
            $association = Join-Path $pythonRevision 'python-environments.json'
            $pythonRun = Invoke-ExistingRuntime -Plan (Join-Path $script:pythonCommandProject 'plan\measurement.json') `
                -Catalog $pythonCatalog -Association $association
            $pythonData = $pythonRun.Report.steps[0].outcome.data
            if ($pythonData.sourceValue -ne 42 -or $pythonData.factor -ne 2 -or $pythonData.value -ne 84) {
                throw "Python command-only did not consume the existing driver's value/factor contract: $($pythonRun.Text)"
            }

            $cppCatalog = Join-Path $testRoot 'C++ command native driver catalog'
            $null = New-Item -ItemType Directory -Path $cppCatalog
            Copy-Item -LiteralPath $referencePackage -Destination (Join-Path $cppCatalog 'ExistingNativeDriver') -Recurse
            Copy-Item -LiteralPath $script:cppCommandBuild.package -Destination (Join-Path $cppCatalog 'CppCommand') -Recurse
            $cppRun = Invoke-ExistingRuntime -Plan (Join-Path $script:cppCommandProject 'TestPlan.json') `
                -Catalog $cppCatalog -Association $null
            if ($cppRun.Text -notmatch 'Computed value 84\.000000\.') {
                throw "C++ command-only did not consume the existing driver's value/factor contract: $($cppRun.Text)"
            }
            'both generated commands resolved the native operation and computed 42 * 2'
        }

        Invoke-Case 'Occupied/invalid destinations, missing compiler, C#, and future commands fail closed' {
            $occupied = Join-Path $projects 'Occupied Project'
            $null = New-Item -ItemType Directory -Path $occupied
            Set-Content -LiteralPath (Join-Path $occupied 'author-code.txt') -Value 'preserve me'
            $null = Assert-KitFailure 'ARTESTSDK002' @('new', '--name', 'Occupied Project', '--folder', $projects, '--language', 'python')
            if ((Get-Content -LiteralPath (Join-Path $occupied 'author-code.txt') -Raw).Trim() -ne 'preserve me') { throw 'Occupied destination content was modified.' }
            $null = Assert-KitFailure 'ARTESTSDK002' @('new', '--name', 'Bad Parent', '--folder', (Join-Path $projects 'missing'), '--language', 'python')
            $null = Assert-KitFailure 'ARTESTSDK005' @('new', '--name', 'No Compiler', '--folder', $projects, '--language', 'cpp', '--msbuild', (Join-Path $projects 'missing\MSBuild.exe'))
            $null = Assert-KitFailure 'ARTESTSDK003' @('new', '--name', 'No CSharp', '--folder', $projects, '--language', 'c#')
            $null = Assert-KitFailure 'ARTESTKIT008' @('python-project')
            if (Test-Path -LiteralPath (Join-Path $projects 'No Compiler')) { throw 'Missing-toolchain failure left a project behind.' }
            'all negative cases preserved their targets'
        }
    }
    finally {
        Pop-Location
    }
}
catch {
    $fatal = $_
}
finally {
    $null = New-Item -ItemType Directory -Path $evidenceRoot -Force
    $sourceFiles = @()
    try { $sourceFiles = @(Get-Stage4BSourceInventory) }
    catch {
        if (-not $fatal) { $fatal = $_ }
        $results.Add([ordered]@{ name = 'Stage 4B source inventory'; status = 'FAILED'; durationMs = 0; detail = @($_.Exception.Message) })
    }
    $candidate = [ordered]@{
        schema = 'artest.schema.py-dx-01-stage4b-evidence.v1'
        runId = $runId
        createdAtUtc = (Get-Date).ToUniversalTime().ToString('o')
        scope = 'PY-DX-01 Stage 4B new/build only'
        configuration = $Configuration
        platform = $Platform
        kitVersion = $version.kitVersion
        gitHead = (& $git -C $repositoryRoot rev-parse HEAD | Out-String).Trim()
        gitStatus = @(& $git -C $repositoryRoot status --short)
        inputs = [ordered]@{
            pythonRuntime = [IO.Path]::GetFullPath($PythonRuntime)
            dependencyWheelRoot = [IO.Path]::GetFullPath($DependencyWheelRoot)
            visualStudioPath = [IO.Path]::GetFullPath($VisualStudioPath)
            protocPath = if ($ProtocPath) { [IO.Path]::GetFullPath($ProtocPath) } else { $null }
            visualCRTRoot = if ($VisualCRTRoot) { [IO.Path]::GetFullPath($VisualCRTRoot) } else { $null }
        }
        sourceFiles = $sourceFiles
        archive = $archivePath
        archiveSha256 = if (Test-Path -LiteralPath $archivePath) { (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant() } else { $null }
        externalTestRoot = $testRoot
        externalTestRootRemoved = $false
        cases = @($results)
        limitations = @(
            'Stage 4 execution, Stage 5 acceptance and .NET are not implemented.',
            'The hardware-free starters do not prove vendor SDK, physical instrument or PicoSDK availability.',
            'The evaluation artifact is not a public release, installer, package feed or ABI 1.0 claim.'
        )
    }
    $candidatePath = Join-Path $evidenceRoot 'candidate.json'
    $candidate | ConvertTo-Json -Depth 14 | Set-Content -LiteralPath $candidatePath -Encoding utf8
    $commands | Set-Content -LiteralPath (Join-Path $evidenceRoot 'commands.txt') -Encoding utf8
    if (Test-Path -LiteralPath $testRoot) {
        if ($testRoot -notmatch 'ARTest 4B [0-9a-f]{8}$' -or
            $testRoot.StartsWith($repositoryRoot.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
            if (-not $fatal) { $fatal = [InvalidOperationException]::new("Unsafe Stage 4B cleanup target: $testRoot") }
        }
        else {
            Remove-Item -LiteralPath $testRoot -Recurse -Force
            $candidate.externalTestRootRemoved = $true
            $candidate | ConvertTo-Json -Depth 14 | Set-Content -LiteralPath $candidatePath -Encoding utf8
        }
    }
    $hashes = Get-ChildItem -LiteralPath $evidenceRoot -File | Sort-Object Name | ForEach-Object {
        "{0}  {1}" -f (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant(), $_.Name
    }
    $hashes | Set-Content -LiteralPath (Join-Path $evidenceRoot 'SHA256SUMS.txt') -Encoding ascii
}

if ($fatal) {
    Write-Error "Stage 4B development-kit tests failed. Evidence: $evidenceRoot`n$($fatal.Exception.ToString())"
    exit 1
}
Write-Host "Stage 4B development-kit tests: PASSED"
Write-Host "Evidence: $evidenceRoot"
