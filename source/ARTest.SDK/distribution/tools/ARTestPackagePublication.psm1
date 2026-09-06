Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Assert-ARTestPlainPath {
    param([string]$Path, [switch]$Tree)
    $full = [IO.Path]::GetFullPath($Path)
    $ancestor = $full
    while ($ancestor) {
        if (Test-Path -LiteralPath $ancestor) {
            if ((Get-Item -LiteralPath $ancestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "ARTESTPKG010: Reparse point is not allowed: $ancestor"
            }
        }
        $ancestor = Split-Path -Parent $ancestor
    }
    if ($Tree -and (Test-Path -LiteralPath $full -PathType Container)) {
        foreach ($entry in Get-ChildItem -LiteralPath $full -Force) {
            Assert-ARTestPlainPath $entry.FullName -Tree
        }
    }
}

function Write-ARTestJson {
    param([string]$Path, $Value)
    $text = $Value | ConvertTo-Json -Depth 64 -Compress
    $bytes = [Text.UTF8Encoding]::new($false).GetBytes($text)
    $stream = [IO.File]::Open($Path, [IO.FileMode]::Create, [IO.FileAccess]::Write, [IO.FileShare]::None)
    try { $stream.Write($bytes, 0, $bytes.Length); $stream.Flush($true) }
    finally { $stream.Dispose() }
}

function Get-ARTestInventory {
    param([string]$Path)
    Assert-ARTestPlainPath $Path -Tree
    $prefix = [IO.Path]::GetFullPath($Path).TrimEnd('\') + '\'
    $result = @{}
    foreach ($file in Get-ChildItem -LiteralPath $Path -File -Recurse -Force) {
        $relative = $file.FullName.Substring($prefix.Length).Replace('\', '/')
        if ($relative -eq '.artest-generated-package.json') { continue }
        $result[$relative] = Get-ARTestHash $file.FullName
    }
    return $result
}

function Get-ARTestHash {
    param([string]$Path)
    $stream = [IO.File]::OpenRead($Path)
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($algorithm.ComputeHash($stream)).Replace('-', '') }
    finally { $algorithm.Dispose(); $stream.Dispose() }
}

function Move-ARTestPackageDirectory {
    param([string]$Source, [string]$Destination)
    [IO.Directory]::Move($Source, $Destination)
}

function Assert-ARTestInventory {
    param([string]$Path, $Expected)
    $actual = Get-ARTestInventory $Path
    $properties = @($Expected.PSObject.Properties)
    if ($actual.Count -ne $properties.Count) { throw "ARTESTPKG011: Package inventory changed: $Path" }
    foreach ($entry in $properties) {
        if (-not $actual.ContainsKey($entry.Name) -or $actual[$entry.Name] -cne $entry.Value) {
            throw "ARTESTPKG011: Package content changed: $Path/$($entry.Name)"
        }
    }
}

function Assert-ARTestOwnedPackage {
    param([string]$Path, [string]$ExtensionId)
    $marker = Join-Path $Path '.artest-generated-package.json'
    if (-not (Test-Path -LiteralPath $marker -PathType Leaf)) {
        throw "ARTESTPKG012: Refusing to replace an unowned directory: $Path"
    }
    Assert-ARTestPlainPath $Path -Tree
    $owner = Get-Content -LiteralPath $marker -Raw | ConvertFrom-Json
    if ($owner.format -ne 'ARTest.GeneratedPackage' -or $owner.version -ne 1 -or
        ($ExtensionId -and $owner.extensionId -cne $ExtensionId)) {
        throw "ARTESTPKG012: Package ownership does not match: $Path"
    }
    Assert-ARTestInventory $Path $owner.files
}

function Invoke-ARTestBuildTool {
    param([string]$Executable, [string]$Argument, [int]$TimeoutSeconds)
    # One explicit child process: no shell interpolation and no hardware session.
    if ($Argument.Contains('"')) { throw 'ARTESTPKG013: Invalid tool argument.' }
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = [IO.Path]::GetFullPath($Executable)
    $start.Arguments = '"' + $Argument.TrimEnd('\') + '"'
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.StandardOutputEncoding = [Text.Encoding]::UTF8
    $start.StandardErrorEncoding = [Text.Encoding]::UTF8
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    try {
        $null = $process.Start()
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
            $process.Kill()
            $process.WaitForExit()
            throw "ARTESTPKG014: Build tool timed out: $Executable"
        }
        $output = $stdout.GetAwaiter().GetResult()
        $errors = $stderr.GetAwaiter().GetResult()
        if ($process.ExitCode -ne 0) {
            throw "ARTESTPKG015: Build tool failed ($($process.ExitCode)): $errors $output"
        }
        if ($output.Length -gt 16MB) { throw 'ARTESTPKG016: Build tool output is too large.' }
        return $output
    }
    finally { $process.Dispose() }
}

function Repair-ARTestPublication {
    param([string]$Transaction, [string]$Destination)
    if (-not (Test-Path -LiteralPath $Transaction)) { return }
    Assert-ARTestPlainPath $Transaction -Tree
    $journal = Get-Content -LiteralPath (Join-Path $Transaction 'owner.json') -Raw | ConvertFrom-Json
    if ($journal.format -ne 'ARTest.PackageTransaction' -or $journal.version -ne 1 -or
        $journal.destination -cne $Destination) {
        throw 'ARTESTPKG017: Invalid transaction ownership; preserved for inspection.'
    }
    $backup = Join-Path $Transaction 'backup'
    if (Test-Path -LiteralPath $backup) {
        Assert-ARTestInventory $backup $journal.previousFiles
        if (-not (Test-Path -LiteralPath $Destination)) {
            # A process died between renames. Restore the complete prior generation.
            Assert-ARTestPlainPath $Destination
            [IO.Directory]::Move($backup, $Destination)
            Write-Host 'ARTESTPKG_RECOVERED: Previous package restored.'
        } else {
            # New directory was promoted. Do not discard the backup if it is damaged.
            Assert-ARTestOwnedPackage $Destination $journal.extensionId
        }
    }
    # The destination never comes from journal input. This fixed sibling is the
    # only recursive cleanup target; reject junctions before walking/removing it.
    $expected = Join-Path (Split-Path -Parent $Destination) ('.' + [IO.Path]::GetFileName($Destination) + '.artest-publish')
    if ([IO.Path]::GetFullPath($Transaction) -cne $expected) { throw 'ARTESTPKG018: Unsafe transaction cleanup.' }
    Remove-Item -LiteralPath $Transaction -Recurse -Force
}

function Publish-ARTestGeneratedPackage {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory=$true)][string]$GeneratorPath,
        [Parameter(Mandatory=$true)][string]$BinaryPath,
        [Parameter(Mandatory=$true)][string]$ValidatorPath,
        [Parameter(Mandatory=$true)][string]$PackageDirectory,
        [ValidateRange(1,3600)][int]$ToolTimeoutSeconds = 120,
        [bool]$AdoptLegacyPackage = $false
    )
    $destination = [IO.Path]::GetFullPath($PackageDirectory).TrimEnd('\')
    $leaf = [IO.Path]::GetFileName($destination)
    $parent = Split-Path -Parent $destination
    if (-not $parent -or $leaf -cnotmatch '^[A-Za-z0-9][A-Za-z0-9_-]*$') {
        throw 'ARTESTPKG019: A named package subdirectory is required.'
    }
    Assert-ARTestPlainPath $destination -Tree
    foreach ($inputPath in @($GeneratorPath, $BinaryPath, $ValidatorPath)) {
        Assert-ARTestPlainPath $inputPath
        if (-not (Test-Path -LiteralPath $inputPath -PathType Leaf)) { throw "Missing build input: $inputPath" }
        if ([IO.Path]::GetFullPath($inputPath).StartsWith($destination + '\', [StringComparison]::OrdinalIgnoreCase)) {
            throw 'ARTESTPKG020: Build inputs must be outside the published package.'
        }
    }
    $null = New-Item -ItemType Directory -Path $parent -Force
    $transaction = Join-Path $parent ('.' + $leaf + '.artest-publish')
    $lockPath = Join-Path $parent ('.' + $leaf + '.artest-publish.lock')
    Assert-ARTestPlainPath $lockPath
    # Keep the empty lock file after release: deleting it introduces a lock race.
    $lock = [IO.File]::Open($lockPath, [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
    try {
        Repair-ARTestPublication $transaction $destination
        $binary = Get-Item -LiteralPath $BinaryPath
        $bundle = Invoke-ARTestBuildTool $GeneratorPath $binary.Name $ToolTimeoutSeconds | ConvertFrom-Json
        if ($bundle.format -cne 'ARTest.MetadataBundle' -or $bundle.version -ne 1) {
            throw 'ARTESTPKG021: Unsupported metadata bundle.'
        }
        $manifest = $bundle.manifestText | ConvertFrom-Json
        if ($manifest.runtime.entry -cne $binary.Name -or @($manifest.components).Count -eq 0) {
            throw 'ARTESTPKG022: Metadata does not describe the build output.'
        }
        $previous = @{}
        if (Test-Path -LiteralPath $destination) {
            if (Test-Path -LiteralPath (Join-Path $destination '.artest-generated-package.json')) {
                Assert-ARTestOwnedPackage $destination $manifest.extensionId
            } elseif ($AdoptLegacyPackage) {
                # Opt-in migration only; do not swallow unrelated files into ownership.
                $legacy = Get-Content -LiteralPath (Join-Path $destination 'artest-extension.json') -Raw | ConvertFrom-Json
                if ($legacy.extensionId -cne $manifest.extensionId) { throw 'ARTESTPKG023: Legacy identity mismatch.' }
                $allowed = @('artest-extension.json', $legacy.runtime.entry)
                foreach ($component in $legacy.components) {
                    foreach ($schema in $component.schemas) { $allowed += $schema.path }
                }
                $inventory = Get-ARTestInventory $destination
                if (@(Compare-Object @($inventory.Keys | Sort-Object) @($allowed | Sort-Object -Unique)).Count) {
                    throw 'ARTESTPKG023: Legacy directory contains unowned or missing files.'
                }
            } else { throw 'ARTESTPKG012: Existing directory is not owned by this publisher.' }
            $previous = Get-ARTestInventory $destination
        }
        $null = New-Item -ItemType Directory -Path $transaction
        Write-ARTestJson (Join-Path $transaction 'owner.json') ([ordered]@{
            format='ARTest.PackageTransaction'; version=1; destination=$destination
            extensionId=$manifest.extensionId; previousFiles=$previous
        })
        try {
            $catalog = Join-Path $transaction 'staging'
            $candidate = Join-Path $catalog $leaf
            $null = New-Item -ItemType Directory -Path (Join-Path $candidate 'schemas') -Force
            $utf8 = [Text.UTF8Encoding]::new($false)
            foreach ($file in $bundle.schemas.PSObject.Properties) {
                if ($file.Name -cnotmatch '^schemas/[a-z0-9]+([.-][a-z0-9]+)*\.json$' -or $file.Value -isnot [string]) {
                    throw 'ARTESTPKG024: Invalid generated schema path/content.'
                }
                $null = $file.Value | ConvertFrom-Json
                [IO.File]::WriteAllText((Join-Path $candidate $file.Name), $file.Value, $utf8)
            }
            Copy-Item -LiteralPath $binary.FullName -Destination (Join-Path $candidate $binary.Name)
            $hash = (Get-ARTestHash (Join-Path $candidate $binary.Name)).ToLowerInvariant()
            $manifest | Add-Member -NotePropertyName integrity -NotePropertyValue ([pscustomobject]@{sha256=$hash}) -Force
            Write-ARTestJson (Join-Path $candidate 'artest-extension.json') $manifest
            $verdict = Invoke-ARTestBuildTool $ValidatorPath $catalog $ToolTimeoutSeconds | ConvertFrom-Json
            if ($verdict.valid -isnot [bool] -or -not $verdict.valid) {
                throw 'ARTESTPKG025: Binary validation did not return a successful verdict.'
            }
            Write-ARTestJson (Join-Path $candidate '.artest-generated-package.json') ([ordered]@{
                format='ARTest.GeneratedPackage'; version=1; extensionId=$manifest.extensionId
                files=(Get-ARTestInventory $candidate)
            })
            Assert-ARTestOwnedPackage $candidate $manifest.extensionId
            # Same-volume renames never merge generations. There is a short name
            # availability gap; this is crash-recoverable replacement, not hot reload.
            if (Test-Path -LiteralPath $destination) {
                $expectedPrevious = Get-Content -LiteralPath (Join-Path $transaction 'owner.json') -Raw | ConvertFrom-Json
                Assert-ARTestInventory $destination $expectedPrevious.previousFiles
                Move-ARTestPackageDirectory $destination (Join-Path $transaction 'backup')
            }
            try { Move-ARTestPackageDirectory $candidate $destination }
            catch {
                Repair-ARTestPublication $transaction $destination
                throw
            }
            Repair-ARTestPublication $transaction $destination
            Write-Host "ARTESTPKG_PUBLISHED: $destination"
        }
        catch {
            # Validation failures leave the live directory untouched. Interrupted
            # promotion is repaired using the journal, never by copying fragments.
            Repair-ARTestPublication $transaction $destination
            throw
        }
    }
    finally { $lock.Dispose() }
}
Export-ModuleMember -Function Publish-ARTestGeneratedPackage
