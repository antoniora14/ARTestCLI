[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$Case,
    [ValidateSet('Debug','Release')][string]$Configuration = 'Debug',
    [string]$CrashDestination = ''
)
$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
Import-Module (Join-Path $repository 'source\ARTest.SDK\distribution\tools\ARTestPackagePublication.psm1') -Force
$module = Get-Module ARTestPackagePublication
$binaryRoot = Join-Path $repository "artifacts\bin\x64\$Configuration"
$generator = Join-Path $repository "artifacts\obj\ARTestSdkExample\x64\$Configuration\metadata\bin\ARTestSdkExampleMetadata.exe"
$validator = Join-Path $binaryRoot 'ARTestSdkValidate.exe'
$binary = Join-Path $binaryRoot 'ARTestSdkExample.dll'
$root = Join-Path ([IO.Path]::GetTempPath()) ('ARTest-Publication-' + [guid]::NewGuid().ToString('N'))
$destination = Join-Path $root 'Package'
if ($CrashDestination) { $destination = $CrashDestination }
$arguments = @{ GeneratorPath=$generator; BinaryPath=$binary; ValidatorPath=$validator; PackageDirectory=$destination; ToolTimeoutSeconds=10 }
function Assert([bool]$Condition, [string]$Message) { if (-not $Condition) { throw $Message } }
function Inventory([string]$Path) {
    return (& $module { param($p) Get-ARTestInventory $p } $Path) | ConvertTo-Json -Compress
}
function Reject([scriptblock]$Action, [string]$Pattern) {
    $rejected = $false
    try { & $Action }
    catch {
        if ($_.Exception.Message -notmatch $Pattern) { throw }
        $rejected = $true
    }
    Assert $rejected "Expected failure matching $Pattern"
}
if ($CrashDestination) {
    # Test-local fault injection, never a production environment variable.
    & $module {
        param($phase, $target)
        $script:crashPhase = $phase
        $script:crashTarget = $target
        function script:Move-ARTestPackageDirectory {
            param($Source, $Destination)
            [IO.Directory]::Move($Source, $Destination)
            if (($script:crashPhase -eq 'InterruptedBackup' -and $Source -eq $script:crashTarget) -or
                ($script:crashPhase -eq 'InterruptedPromoted' -and $Destination -eq $script:crashTarget)) {
                Stop-Process -Id $PID -Force
            }
        }
    } $Case $destination
    Publish-ARTestGeneratedPackage @arguments
    throw 'Crash injection was not reached.'
}
try {
    Publish-ARTestGeneratedPackage @arguments
    $before = Inventory $destination
    switch ($Case) {
        'FreshAndStale' {
            [IO.File]::WriteAllText((Join-Path $destination 'obsolete.json'), '{}')
            & $module {
                param($p)
                $marker = Join-Path $p '.artest-generated-package.json'
                $owner = Get-Content -LiteralPath $marker -Raw | ConvertFrom-Json
                $owner.files = Get-ARTestInventory $p
                Write-ARTestJson $marker $owner
            } $destination
            Publish-ARTestGeneratedPackage @arguments
            Assert (-not (Test-Path -LiteralPath (Join-Path $destination 'obsolete.json'))) 'Stale file survived replacement.'
        }
        'BadGenerator' {
            $arguments.BinaryPath = Join-Path $binaryRoot 'ARTestCLI.exe'
            Reject { Publish-ARTestGeneratedPackage @arguments } 'ARTESTPKG015'
        }
        'WrongDll' {
            $wrong = Join-Path $root 'build'
            $null = New-Item -ItemType Directory -Path $wrong
            $arguments.BinaryPath = Join-Path $wrong 'ARTestSdkExample.dll'
            Copy-Item -LiteralPath (Join-Path $binaryRoot 'ARTestCmdSample.dll') -Destination $arguments.BinaryPath
            Reject { Publish-ARTestGeneratedPackage @arguments } 'EXTENSION_ID_MISMATCH'
        }
        { $_ -in 'InvalidSchema', 'ReorderedDescriptors', 'InvalidVerdict' } {
            & $module {
                param($mode, $gen, $val)
                $script:originalTool = (Get-Command Invoke-ARTestBuildTool).ScriptBlock
                $script:mode = $mode; $script:generator = $gen; $script:validator = $val
                function script:Invoke-ARTestBuildTool {
                    param($Executable, $Argument, $TimeoutSeconds)
                    if ($script:mode -eq 'InvalidVerdict' -and $Executable -eq $script:validator) {
                        return '{"valid":false}'
                    }
                    $text = & $script:originalTool $Executable $Argument $TimeoutSeconds
                    if ($Executable -ne $script:generator) { return $text }
                    $bundle = $text | ConvertFrom-Json
                    if ($script:mode -eq 'InvalidSchema') {
                        $file = @($bundle.schemas.PSObject.Properties)[0]
                        $schema = $file.Value | ConvertFrom-Json
                        $schema | Add-Member -NotePropertyName unsupported -NotePropertyValue $true
                        $file.Value = $schema | ConvertTo-Json -Depth 64 -Compress
                    }
                    if ($script:mode -eq 'ReorderedDescriptors') {
                        $manifest = $bundle.manifestText | ConvertFrom-Json
                        [array]::Reverse($manifest.components)
                        $bundle.manifestText = $manifest | ConvertTo-Json -Depth 64 -Compress
                    }
                    return $bundle | ConvertTo-Json -Depth 64 -Compress
                }
            } $Case $generator $validator
            if ($Case -eq 'ReorderedDescriptors') {
                Publish-ARTestGeneratedPackage @arguments
                $before = Inventory $destination
            } else {
                $pattern = if ($Case -eq 'InvalidSchema') { 'SCHEMA_KEYWORD_UNSUPPORTED' } else { 'ARTESTPKG025' }
                Reject { Publish-ARTestGeneratedPackage @arguments } $pattern
            }
        }
        'FailedPromotion' {
            & $module {
                function script:Move-ARTestPackageDirectory {
                    param($Source, $Destination)
                    if ($Source -match '[\\/]staging[\\/]') { throw 'Injected promotion failure.' }
                    [IO.Directory]::Move($Source, $Destination)
                }
            }
            Reject { Publish-ARTestGeneratedPackage @arguments } 'Injected promotion failure'
        }
        { $_ -in 'InterruptedBackup', 'InterruptedPromoted' } {
            $start = [Diagnostics.ProcessStartInfo]::new()
            $start.FileName = Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\powershell.exe'
            $start.Arguments = '-NoProfile -ExecutionPolicy Bypass -File "' + $PSCommandPath + '" -Case ' + $Case +
                ' -Configuration ' + $Configuration + ' -CrashDestination "' + $destination + '"'
            $start.UseShellExecute = $false; $start.CreateNoWindow = $true
            $process = [Diagnostics.Process]::Start($start)
            try {
                Assert ($process.WaitForExit(30000)) 'Crash fixture hung.'
                Assert ($process.ExitCode -ne 0) 'Crash fixture did not terminate abnormally.'
            } finally {
                if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
                $process.Dispose()
            }
            Assert (Test-Path -LiteralPath (Join-Path $root '.Package.artest-publish\backup')) 'No interrupted transaction was produced.'
            Publish-ARTestGeneratedPackage @arguments
        }
        'LockedWriter' {
            $handle = [IO.File]::Open((Join-Path $root '.Package.artest-publish.lock'), 'Open', 'ReadWrite', 'None')
            try { Reject { Publish-ARTestGeneratedPackage @arguments } '(another process|otro proceso|being used|utilizando)' }
            finally { $handle.Dispose() }
        }
        'ForeignContent' {
            [IO.File]::WriteAllText((Join-Path $destination 'user-notes.txt'), 'Preserve me.')
            $before = Inventory $destination
            Reject { Publish-ARTestGeneratedPackage @arguments } 'ARTESTPKG011'
            Assert ([IO.File]::ReadAllText((Join-Path $destination 'user-notes.txt')) -eq 'Preserve me.') 'User file was changed.'
        }
        'ToolTimeout' {
            $powerShell = Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\powershell.exe'
            Reject { & $module { param($exe) Invoke-ARTestBuildTool $exe 'Start-Sleep -Seconds 30' 1 } $powerShell } 'ARTESTPKG014'
        }
        default { throw "Unknown publication case: $Case" }
    }
    Assert ((Inventory $destination) -ceq $before) 'Unexpected live package content change.'
    Assert (-not (Test-Path -LiteralPath (Join-Path $root '.Package.artest-publish'))) 'Transaction directory was not retired.'
    Write-Output "PASSED: $Case"
}
finally {
    if (-not $root.StartsWith([IO.Path]::GetTempPath(), [StringComparison]::OrdinalIgnoreCase) -or
        [IO.Path]::GetFileName($root) -notmatch '^ARTest-Publication-[a-f0-9]{32}$') { throw 'Unsafe test cleanup.' }
    & $module { param($p) Assert-ARTestPlainPath $p -Tree } $root
    if (Test-Path -LiteralPath $root) { Remove-Item -LiteralPath $root -Recurse -Force }
}
