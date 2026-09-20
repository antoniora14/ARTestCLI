Set-StrictMode -Version 3.0

$script:InstallationSchema = 'artest.schema.sdk-installations.v1'
$script:RegistrationSchema = 'artest.schema.sdk-registration-state.v1'
$script:TransactionSchema = 'artest.schema.sdk-registration-transaction.v1'

function Get-RegistrationConfigurationRoot {
    $override = [Environment]::GetEnvironmentVariable('ARTEST_SDK_CONFIG_ROOT')
    if (-not [string]::IsNullOrWhiteSpace($override)) { return [IO.Path]::GetFullPath($override) }
    return [IO.Path]::GetFullPath((Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'ARTest\sdk-authoring'))
}

function Get-SafeRegistrationId {
    param([string]$Value)
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($algorithm.ComputeHash([Text.Encoding]::UTF8.GetBytes($Value)))).Replace('-', '').ToLowerInvariant() }
    finally { $algorithm.Dispose() }
}

function Get-RegistrationBytesHash {
    param([byte[]]$Bytes)
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($algorithm.ComputeHash($Bytes))).Replace('-', '').ToLowerInvariant() }
    finally { $algorithm.Dispose() }
}

function ConvertTo-RegistrationJsonBytes {
    param([object]$Value)
    return [Text.UTF8Encoding]::new($false).GetBytes(($Value | ConvertTo-Json -Depth 16) + [Environment]::NewLine)
}

function Assert-RegistrationObjectProperties {
    param([object]$Value, [string[]]$Required, [string]$Description, [switch]$Exact)
    if ($null -eq $Value) { Stop-Kit 'ARTESTREG007' "$Description is missing; journal preserved for inspection." }
    $actual = @($Value.PSObject.Properties.Name)
    foreach ($name in $Required) {
        if ($actual -cnotcontains $name) { Stop-Kit 'ARTESTREG007' "$Description is incomplete; journal preserved for inspection." }
    }
    if ($Exact -and (@($actual | Sort-Object) -join "`n") -cne (@($Required | Sort-Object) -join "`n")) {
        Stop-Kit 'ARTESTREG007' "$Description has unknown fields; journal preserved for inspection."
    }
}

function Assert-RegistrationPathNoReparse {
    param([string]$Path, [string]$Description)
    $full = [IO.Path]::GetFullPath($Path)
    $root = [IO.Path]::GetPathRoot($full)
    $current = $root
    foreach ($part in $full.Substring($root.Length).Split(@('\'), [StringSplitOptions]::RemoveEmptyEntries)) {
        if ([string]::IsNullOrEmpty($current)) { $current = $root }
        $current = Join-Path $current $part
        if (Test-Path -LiteralPath $current) {
            $item = Get-Item -LiteralPath $current -Force
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                Stop-Kit 'ARTESTREG007' "$Description contains a reparse point: $current. Journal preserved for inspection."
            }
        }
    }
    return $full
}

function Get-RegistrationInventory {
    param([string]$Root)
    $root = [IO.Path]::GetFullPath($Root).TrimEnd('\')
    Assert-RegistrationPathNoReparse $root 'Registered output path' | Out-Null
    if (-not (Test-Path -LiteralPath $root -PathType Container)) { Stop-Kit 'ARTESTREG004' "Package directory is missing: $root" }
    $inventory = [ordered]@{}
    foreach ($item in Get-ChildItem -LiteralPath $root -Recurse -Force) {
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            Stop-Kit 'ARTESTREG004' "Reparse points are not allowed in registered outputs: $($item.FullName)"
        }
        if (-not $item.PSIsContainer) {
            $relative = [IO.Path]::GetRelativePath($root, $item.FullName).Replace('\', '/')
            $inventory[$relative] = (Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        }
    }
    return $inventory
}

function Get-RegistrationRevisionId {
    param([string]$Package)
    $inventory = Get-RegistrationInventory $Package
    $lines = @($inventory.Keys | Sort-Object | ForEach-Object { "$_=$($inventory[$_])" })
    return Get-RegistrationBytesHash ([Text.Encoding]::UTF8.GetBytes(($lines -join "`n")))
}

function Get-RegistrationInventoryEntries {
    param([object]$Expected)
    $entries = [ordered]@{}
    if ($Expected -is [Collections.IDictionary]) {
        foreach ($key in $Expected.Keys) { $entries[[string]$key] = [string]$Expected[$key] }
    }
    elseif ($null -ne $Expected) {
        foreach ($property in $Expected.PSObject.Properties) { $entries[$property.Name] = [string]$property.Value }
    }
    return $entries
}

function Assert-RegistrationInventoryDescriptor {
    param([object]$Expected, [string]$Description)
    if ($null -eq $Expected) { Stop-Kit 'ARTESTREG007' "$Description is missing; journal preserved for inspection." }
    $entries = Get-RegistrationInventoryEntries $Expected
    foreach ($key in $entries.Keys) {
        if ([string]::IsNullOrWhiteSpace($key) -or [IO.Path]::IsPathRooted($key) -or
            $key.Replace('\', '/').Split('/') -contains '..' -or $entries[$key] -cnotmatch '^[0-9a-f]{64}$') {
            Stop-Kit 'ARTESTREG007' "$Description is invalid; journal preserved for inspection."
        }
    }
}

function Test-RegistrationInventory {
    param([string]$Root, [object]$Expected)
    $actual = Get-RegistrationInventory $Root
    $entries = Get-RegistrationInventoryEntries $Expected
    if ($actual.Count -ne $entries.Count) { return $false }
    foreach ($key in $entries.Keys) {
        if (-not $actual.Contains($key) -or $actual[$key] -cne $entries[$key]) { return $false }
    }
    return $true
}

function Assert-RegistrationInventory {
    param([string]$Root, [object]$Expected)
    if (-not (Test-RegistrationInventory $Root $Expected)) { Stop-Kit 'ARTESTREG004' "Registered output inventory changed: $Root" }
}

function Write-RegistrationBytesAtomic {
    param([string]$Path, [byte[]]$Bytes)
    $parent = Split-Path -Parent $Path
    $null = New-Item -ItemType Directory -Path $parent -Force
    $temporary = Join-Path $parent ('.' + [IO.Path]::GetFileName($Path) + '.' + [guid]::NewGuid().ToString('N') + '.tmp')
    try {
        [IO.File]::WriteAllBytes($temporary, $Bytes)
        [IO.File]::Move($temporary, $Path, $true)
    }
    finally { if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary -Force } }
}

function Write-RegistrationJsonAtomic {
    param([string]$Path, [object]$Value)
    Write-RegistrationBytesAtomic $Path (ConvertTo-RegistrationJsonBytes $Value)
}

function Read-RegistrationObject {
    param([string]$Path, [string]$Schema)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $null }
    try { $value = Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json }
    catch { Stop-Kit 'ARTESTREG002' "Cannot read local registration configuration: $Path. $($_.Exception.Message)" }
    if ($value.schema -ne $Schema -or $value.schemaVersion -ne 1) { Stop-Kit 'ARTESTREG002' "Incompatible local registration configuration: $Path" }
    return $value
}

function Invoke-RegistrationProcess {
    param(
        [string]$Executable, [string[]]$Arguments, [string]$Operation,
        [int]$TimeoutMilliseconds = 120000, [int]$CleanupMilliseconds = 5000,
        [switch]$TestSkipTermination
    )
    if ($TimeoutMilliseconds -lt 1 -or $CleanupMilliseconds -lt 1) { Stop-Kit 'ARTESTREG006' "$Operation has invalid process bounds." }
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = [IO.Path]::GetFullPath($Executable)
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    foreach ($argument in $Arguments) { $null = $start.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::Start($start)
    try {
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit($TimeoutMilliseconds)) {
            $terminationConfirmed = $false
            if (-not $TestSkipTermination) {
                try { $process.Kill($true) } catch { }
                try { $terminationConfirmed = $process.WaitForExit($CleanupMilliseconds) } catch { }
            }
            if (-not $terminationConfirmed) {
                try { $process.StandardOutput.Dispose() } catch { }
                try { $process.StandardError.Dispose() } catch { }
                Stop-Kit 'ARTESTREG010' "$Operation timed out; process termination was not confirmed. The target may still be changing and must be inspected before retrying."
            }
            $readersClosed = $false
            try { $readersClosed = $stdout.Wait($CleanupMilliseconds) -and $stderr.Wait($CleanupMilliseconds) } catch { }
            if (-not $readersClosed) { Stop-Kit 'ARTESTREG010' "$Operation timed out; process termination was confirmed but redirected output closure was not confirmed." }
            Stop-Kit 'ARTESTREG006' "$Operation timed out after termination was confirmed; no installation selection was changed."
        }
        $readersClosed = $false
        try { $readersClosed = $stdout.Wait($CleanupMilliseconds) -and $stderr.Wait($CleanupMilliseconds) } catch { }
        if (-not $readersClosed) { Stop-Kit 'ARTESTREG010' "$Operation exited but redirected output closure was not confirmed within the cleanup bound." }
        $output = $stdout.GetAwaiter().GetResult()
        $errors = $stderr.GetAwaiter().GetResult()
        if ($process.ExitCode -ne 0) { Stop-Kit 'ARTESTREG006' "$Operation failed ($($process.ExitCode)); no installation selection was changed: $errors $output" }
        return $output
    }
    finally { $process.Dispose() }
}

function New-RegistrationFileRecord {
    param([string]$Path, [object]$NewValue)
    $full = [IO.Path]::GetFullPath($Path)
    $hadPrevious = Test-Path -LiteralPath $full -PathType Leaf
    $previous = [byte[]]::new(0)
    if ($hadPrevious) { $previous = [IO.File]::ReadAllBytes($full) }
    $new = ConvertTo-RegistrationJsonBytes $NewValue
    return [ordered]@{
        path=$full; hadPrevious=$hadPrevious; previousBase64=[Convert]::ToBase64String($previous)
        previousSha256=if($hadPrevious){Get-RegistrationBytesHash $previous}else{''}
        newBase64=[Convert]::ToBase64String($new); newSha256=Get-RegistrationBytesHash $new
    }
}

function Assert-RegistrationFileRecord {
    param([object]$Record, [string]$ExpectedPath, [string]$Description, [switch]$RequireNew)
    $required = @('path','hadPrevious','previousBase64','previousSha256','newBase64','newSha256')
    Assert-RegistrationObjectProperties $Record $required $Description -Exact
    $path = [IO.Path]::GetFullPath([string]$Record.path)
    if ($path -cne [IO.Path]::GetFullPath($ExpectedPath)) { Stop-Kit 'ARTESTREG007' "$Description path is not owned by this transaction; journal preserved for inspection." }
    Assert-RegistrationPathNoReparse $path $Description | Out-Null
    if ($Record.hadPrevious -isnot [bool]) { Stop-Kit 'ARTESTREG007' "$Description ownership marker is invalid; journal preserved for inspection." }
    try {
        $previous = [Convert]::FromBase64String([string]$Record.previousBase64)
        $new = [Convert]::FromBase64String([string]$Record.newBase64)
    }
    catch { Stop-Kit 'ARTESTREG007' "$Description snapshot is corrupt; journal preserved for inspection." }
    if (([bool]$Record.hadPrevious -and (Get-RegistrationBytesHash $previous) -cne [string]$Record.previousSha256) -or
        (-not [bool]$Record.hadPrevious -and ($previous.Length -ne 0 -or [string]$Record.previousSha256 -cne '')) -or
        (Get-RegistrationBytesHash $new) -cne [string]$Record.newSha256) {
        Stop-Kit 'ARTESTREG007' "$Description snapshot integrity failed; journal preserved for inspection."
    }
    if (Test-Path -LiteralPath $path -PathType Leaf) {
        $actual = Get-RegistrationBytesHash ([IO.File]::ReadAllBytes($path))
        $allowed = @([string]$Record.newSha256)
        if ([bool]$Record.hadPrevious) { $allowed += [string]$Record.previousSha256 }
        if ($actual -cnotin $allowed -or ($RequireNew -and $actual -cne [string]$Record.newSha256)) {
            Stop-Kit 'ARTESTREG007' "$Description was changed outside this transaction; journal preserved for inspection."
        }
    }
    elseif ($RequireNew -or [bool]$Record.hadPrevious) { Stop-Kit 'ARTESTREG007' "$Description is missing in an invalid transaction phase; journal preserved for inspection." }
}

function Restore-RegistrationFileRecord {
    param([object]$Record)
    $path = [IO.Path]::GetFullPath([string]$Record.path)
    if ([bool]$Record.hadPrevious) { Write-RegistrationBytesAtomic $path ([Convert]::FromBase64String([string]$Record.previousBase64)) }
    elseif (Test-Path -LiteralPath $path -PathType Leaf) { Remove-Item -LiteralPath $path -Force }
}

function Write-RegistrationFileRecordNew {
    param([object]$Record)
    Write-RegistrationBytesAtomic ([IO.Path]::GetFullPath([string]$Record.path)) ([Convert]::FromBase64String([string]$Record.newBase64))
}

function Test-RegistrationPathInventory {
    param([string]$Path, [object]$Expected)
    return (Test-Path -LiteralPath $Path -PathType Container) -and (Test-RegistrationInventory $Path $Expected)
}

function Get-ValidatedRegistrationJournal {
    param([object]$Selected)
    $configuration = [IO.Path]::GetFullPath([string]$Selected.Profile.configuration).TrimEnd('\')
    $journalPath = Join-Path $configuration '.artest-register-transaction.json'
    if (-not (Test-Path -LiteralPath $journalPath -PathType Leaf)) { return $null }
    Assert-RegistrationPathNoReparse $journalPath 'Registration journal path' | Out-Null
    try { $journal = Get-Content -LiteralPath $journalPath -Raw | ConvertFrom-Json }
    catch { Stop-Kit 'ARTESTREG007' "Registration journal is corrupt and was preserved: $journalPath. $($_.Exception.Message)" }
    $required = @('schema','schemaVersion','installation','profile','token','phase','catalog','candidate','backup','hadCatalog','previousCatalogInventory','candidateCatalogInventory','files')
    Assert-RegistrationObjectProperties $journal $required 'Registration journal' -Exact
    if ($journal.schema -cne $script:TransactionSchema -or $journal.schemaVersion -ne 1 -or
        [string]$journal.installation -cne [string]$Selected.Name -or [string]$journal.token -cnotmatch '^[0-9a-f]{32}$' -or
        [string]$journal.phase -cnotin @('prepared','backingUp','promoting','writingState','writingProfile','committed') -or
        $journal.hadCatalog -isnot [bool]) {
        Stop-Kit 'ARTESTREG007' 'Registration journal identity or phase is invalid; journal preserved for inspection.'
    }
    Assert-RegistrationObjectProperties $journal.profile @('cli','engine','catalog','configuration','registry') 'Registration journal profile' -Exact
    $expectedProfile = [ordered]@{
        cli=[IO.Path]::GetFullPath([string]$Selected.Profile.cli)
        engine=[IO.Path]::GetFullPath([string]$Selected.Profile.engine)
        catalog=[IO.Path]::GetFullPath([string]$Selected.Profile.catalog).TrimEnd('\')
        configuration=$configuration
        registry=[IO.Path]::GetFullPath([string]$Selected.RegistryPath)
    }
    foreach ($name in $expectedProfile.Keys) {
        if ([IO.Path]::GetFullPath([string]$journal.profile.$name).TrimEnd('\') -cne $expectedProfile[$name].TrimEnd('\')) {
            Stop-Kit 'ARTESTREG007' 'Registration journal belongs to a different installation; journal preserved for inspection.'
        }
    }
    $catalog = $expectedProfile.catalog
    $catalogParent = Split-Path -Parent $catalog
    $leaf = [IO.Path]::GetFileName($catalog)
    $candidate = Join-Path $catalogParent ('.' + $leaf + '.artest-candidate.' + [string]$journal.token)
    $backup = Join-Path $catalogParent ('.' + $leaf + '.artest-backup.' + [string]$journal.token)
    if ([IO.Path]::GetFullPath([string]$journal.catalog).TrimEnd('\') -cne $catalog -or
        [IO.Path]::GetFullPath([string]$journal.candidate).TrimEnd('\') -cne $candidate -or
        [IO.Path]::GetFullPath([string]$journal.backup).TrimEnd('\') -cne $backup) {
        Stop-Kit 'ARTESTREG007' 'Registration journal contains a foreign or escaped catalog path; journal preserved for inspection.'
    }
    foreach ($path in @($catalog,$candidate,$backup,$configuration,$expectedProfile.registry)) {
        Assert-RegistrationPathNoReparse $path 'Registration transaction path' | Out-Null
    }
    Assert-RegistrationInventoryDescriptor $journal.previousCatalogInventory 'Previous catalog inventory'
    Assert-RegistrationInventoryDescriptor $journal.candidateCatalogInventory 'Candidate catalog inventory'
    if (-not [bool]$journal.hadCatalog -and (Get-RegistrationInventoryEntries $journal.previousCatalogInventory).Count -ne 0) {
        Stop-Kit 'ARTESTREG007' 'Registration journal claims inventory for a catalog it does not own; journal preserved for inspection.'
    }
    Assert-RegistrationObjectProperties $journal.files @('state','mapping','profile') 'Registration journal file records' -Exact
    $requireNew = [string]$journal.phase -eq 'committed'
    Assert-RegistrationFileRecord $journal.files.state (Join-Path $configuration 'registrations.json') 'Registration state snapshot' -RequireNew:$requireNew
    Assert-RegistrationFileRecord $journal.files.mapping (Join-Path $configuration 'python-environments.json') 'Python mapping snapshot' -RequireNew:$requireNew
    Assert-RegistrationFileRecord $journal.files.profile $expectedProfile.registry 'Installation profile snapshot' -RequireNew:$requireNew

    $hasCatalog = Test-Path -LiteralPath $catalog -PathType Container
    $hasCandidate = Test-Path -LiteralPath $candidate -PathType Container
    $hasBackup = Test-Path -LiteralPath $backup -PathType Container
    $catalogPrevious = $hasCatalog -and [bool]$journal.hadCatalog -and (Test-RegistrationInventory $catalog $journal.previousCatalogInventory)
    $catalogNew = $hasCatalog -and (Test-RegistrationInventory $catalog $journal.candidateCatalogInventory)
    if ($hasCandidate -and -not (Test-RegistrationInventory $candidate $journal.candidateCatalogInventory)) {
        Stop-Kit 'ARTESTREG007' 'Candidate catalog ownership or integrity failed; journal preserved for inspection.'
    }
    if ($hasBackup -and (-not [bool]$journal.hadCatalog -or -not (Test-RegistrationInventory $backup $journal.previousCatalogInventory))) {
        Stop-Kit 'ARTESTREG007' 'Backup catalog ownership or integrity failed; journal preserved for inspection.'
    }
    switch ([string]$journal.phase) {
        'prepared' {
            if (-not $hasCandidate -or $hasBackup -or ([bool]$journal.hadCatalog -and -not $catalogPrevious) -or (-not [bool]$journal.hadCatalog -and $hasCatalog)) {
                Stop-Kit 'ARTESTREG007' 'Prepared registration journal topology is invalid; journal preserved for inspection.'
            }
        }
        'backingUp' {
            $previousLocations = [int][bool]$catalogPrevious + [int][bool]$hasBackup
            if (-not $hasCandidate -or ([bool]$journal.hadCatalog -and $previousLocations -ne 1) -or (-not [bool]$journal.hadCatalog -and ($hasCatalog -or $hasBackup))) {
                Stop-Kit 'ARTESTREG007' 'Catalog backup phase topology is invalid; journal preserved for inspection.'
            }
        }
        'promoting' {
            if (($hasCandidate -eq $catalogNew) -or ([bool]$journal.hadCatalog -and -not $hasBackup) -or (-not [bool]$journal.hadCatalog -and $hasBackup) -or ($hasCatalog -and -not $catalogNew)) {
                Stop-Kit 'ARTESTREG007' 'Catalog promotion phase topology is invalid; journal preserved for inspection.'
            }
        }
        default {
            if (-not $catalogNew -or $hasCandidate -or (([string]$journal.phase -ne 'committed') -and [bool]$journal.hadCatalog -and -not $hasBackup) -or
                (-not [bool]$journal.hadCatalog -and $hasBackup)) {
                Stop-Kit 'ARTESTREG007' 'Published registration journal topology is invalid; journal preserved for inspection.'
            }
        }
    }
    return [pscustomobject]@{ Journal=$journal; Path=$journalPath; Catalog=$catalog; Candidate=$candidate; Backup=$backup }
}

function Repair-RegistrationTransaction {
    param([object]$Selected)
    $validated = Get-ValidatedRegistrationJournal $Selected
    if ($null -eq $validated) { return }
    $journal = $validated.Journal
    if ([string]$journal.phase -eq 'committed') {
        if (Test-Path -LiteralPath $validated.Backup -PathType Container) {
            Assert-RegistrationInventory $validated.Backup $journal.previousCatalogInventory
            Remove-Item -LiteralPath $validated.Backup -Recurse -Force
        }
        Remove-Item -LiteralPath $validated.Path -Force
        return
    }
    $phase = [string]$journal.phase
    $catalogExists = Test-Path -LiteralPath $validated.Catalog -PathType Container
    $candidateExists = Test-Path -LiteralPath $validated.Candidate -PathType Container
    $backupExists = Test-Path -LiteralPath $validated.Backup -PathType Container
    $catalogWasPromoted = $phase -in @('writingState','writingProfile') -or
        ($phase -eq 'promoting' -and $catalogExists -and -not $candidateExists)

    if ($catalogWasPromoted) {
        Assert-RegistrationInventory $validated.Catalog $journal.candidateCatalogInventory
        Remove-Item -LiteralPath $validated.Catalog -Recurse -Force
    }
    elseif ($catalogExists) {
        if (-not [bool]$journal.hadCatalog -or $phase -notin @('prepared','backingUp')) {
            Stop-Kit 'ARTESTREG007' 'Catalog role is ambiguous during recovery; journal and content were preserved for inspection.'
        }
        Assert-RegistrationInventory $validated.Catalog $journal.previousCatalogInventory
    }
    if ([bool]$journal.hadCatalog) {
        if ($backupExists) {
            Assert-RegistrationInventory $validated.Backup $journal.previousCatalogInventory
            [IO.Directory]::Move($validated.Backup, $validated.Catalog)
        }
        elseif (-not (Test-RegistrationPathInventory $validated.Catalog $journal.previousCatalogInventory)) {
            Stop-Kit 'ARTESTREG007' 'Previous catalog cannot be proven for recovery; journal preserved for inspection.'
        }
    }
    if ($candidateExists) {
        Assert-RegistrationInventory $validated.Candidate $journal.candidateCatalogInventory
        Remove-Item -LiteralPath $validated.Candidate -Recurse -Force
    }
    Restore-RegistrationFileRecord $journal.files.state
    Restore-RegistrationFileRecord $journal.files.mapping
    Restore-RegistrationFileRecord $journal.files.profile
    Remove-Item -LiteralPath $validated.Path -Force
    Write-Output 'ARTESTREG_RECOVERED: Previous catalog and installation selection restored.'
}

function Resolve-InstallationProfile {
    param([object]$Paths, [hashtable]$Options)
    $root = Get-RegistrationConfigurationRoot
    $registryPath = Join-Path $root 'installations.json'
    $registry = Read-RegistrationObject $registryPath $script:InstallationSchema
    $profiles = if ($registry) { $registry.profiles } else { [pscustomobject]@{} }
    $target = if ($Options.ContainsKey('target')) { [string]$Options.target }
        elseif ($registry -and -not [string]::IsNullOrWhiteSpace([string]$registry.selected)) { [string]$registry.selected }
        else { 'evaluation' }
    if ($target -cnotmatch '^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$') { Stop-Kit 'ARTESTREG001' '--target must be a simple installation name up to 64 characters.' }
    $existing = $profiles.PSObject.Properties[$target]
    $supplied = @(@('cli', 'catalog', 'config') | Where-Object { $Options.ContainsKey($_) })
    if ($supplied.Count -ne 0 -and $supplied.Count -ne 3) { Stop-Kit 'ARTESTREG001' 'A new or changed target requires --cli, --catalog, and --config together.' }
    if ($supplied.Count -eq 3) {
        $profile = [pscustomobject]@{
            cli=[IO.Path]::GetFullPath([string]$Options.cli)
            engine=[IO.Path]::GetFullPath((Join-Path (Split-Path -Parent ([string]$Options.cli)) 'ARTestEngine.dll'))
            catalog=[IO.Path]::GetFullPath([string]$Options.catalog).TrimEnd('\')
            configuration=[IO.Path]::GetFullPath([string]$Options.config).TrimEnd('\')
        }
    }
    elseif ($existing) { $profile = $existing.Value }
    elseif ($target -eq 'evaluation') {
        $base = Join-Path $root 'i\evaluation'
        $profile = [pscustomobject]@{cli=[IO.Path]::GetFullPath($Paths.cliExecutable);engine=[IO.Path]::GetFullPath($Paths.engine);catalog=Join-Path $base 'extensions';configuration=Join-Path $base 'configuration'}
    }
    else { Stop-Kit 'ARTESTREG002' "Unknown installation '$target'. Supply --cli, --catalog, and --config once." }
    return [pscustomobject]@{Name=$target;Profile=$profile;Registry=$registry;RegistryPath=$registryPath}
}

function Assert-InstallationProfile {
    param([object]$Selected)
    $profile = $Selected.Profile
    foreach ($property in @('cli','engine','catalog','configuration')) {
        if ([string]::IsNullOrWhiteSpace([string]$profile.$property)) { Stop-Kit 'ARTESTREG002' "Installation '$($Selected.Name)' has no $property location." }
        $profile.$property = [IO.Path]::GetFullPath([string]$profile.$property).TrimEnd('\')
    }
    if (-not (Test-Path -LiteralPath $profile.cli -PathType Leaf) -or -not (Test-Path -LiteralPath $profile.engine -PathType Leaf) -or
        [IO.Path]::GetFileName($profile.cli) -ine 'ARTestCLI.exe' -or [IO.Path]::GetFileName($profile.engine) -ine 'ARTestEngine.dll' -or
        (Split-Path -Parent $profile.cli) -ine (Split-Path -Parent $profile.engine)) {
        Stop-Kit 'ARTESTREG003' "Installation '$($Selected.Name)' must identify an adjacent ARTestCLI.exe and ARTestEngine.dll."
    }
    foreach ($path in @($profile.cli,$profile.engine,$profile.catalog,$profile.configuration,$Selected.RegistryPath)) { Assert-RegistrationPathNoReparse $path 'Installation profile path' | Out-Null }
    if ($profile.catalog -eq [IO.Path]::GetPathRoot($profile.catalog) -or $profile.configuration -eq [IO.Path]::GetPathRoot($profile.configuration) -or
        $profile.catalog -eq $profile.configuration -or $profile.catalog.StartsWith($profile.configuration + '\',[StringComparison]::OrdinalIgnoreCase) -or
        $profile.configuration.StartsWith($profile.catalog + '\',[StringComparison]::OrdinalIgnoreCase)) {
        Stop-Kit 'ARTESTREG003' 'Catalog and configuration locations must be separate, non-nested directories below a filesystem root.'
    }
    $help = Invoke-RegistrationProcess $profile.cli @('help') 'Target CLI verification'
    if ($help -notmatch 'extensions validate' -or $help -notmatch 'compile') { Stop-Kit 'ARTESTREG003' "Installation '$($Selected.Name)' CLI does not expose the required current arguments." }
}

function Get-InstallationSelectionValue {
    param([object]$Selected)
    $profiles = [ordered]@{}
    if ($Selected.Registry) { foreach ($property in $Selected.Registry.profiles.PSObject.Properties) { $profiles[$property.Name] = $property.Value } }
    $profiles[$Selected.Name] = $Selected.Profile
    return [ordered]@{schema=$script:InstallationSchema;schemaVersion=1;selected=$Selected.Name;profiles=$profiles}
}

function Save-InstallationSelection {
    param([object]$Selected)
    Write-RegistrationJsonAtomic $Selected.RegistryPath (Get-InstallationSelectionValue $Selected)
}

function Invoke-InstalledDiscovery {
    param([object]$Profile,[string]$Catalog,[string]$Mapping,[string]$ExtensionId,[string]$Scratch)
    $reportText = Invoke-RegistrationProcess $Profile.cli @('extensions', 'validate', $Catalog) 'Installed CLI catalog validation'
    try { $report = $reportText | ConvertFrom-Json } catch { Stop-Kit 'ARTESTREG006' 'Installed CLI returned an invalid catalog report.' }
    $matches = @($report.packages | Where-Object { $_.extensionId -ceq $ExtensionId -and $_.valid -eq $true })
    if ($report.valid -ne $true -or $matches.Count -ne 1) { Stop-Kit 'ARTESTREG006' "Installed CLI did not discover exactly one valid revision of $ExtensionId." }
    $plan = Join-Path $Scratch 'registration-discovery-plan.json'
    Write-JsonFile $plan ([ordered]@{format='ARTest.Script';version=1;instruments=@();commands=@([ordered]@{stepId=1;name='Time.WaitMs';instrument='NoInstrument';params=[ordered]@{milliseconds=0}})})
    $null = Invoke-RegistrationProcess $Profile.cli @('compile', $plan, '--extensions', $Catalog, '--python-environments', $Mapping) 'Installed CLI offline discovery'
    return $report
}

function Set-RegistrationJournalPhase {
    param([string]$Path,[object]$Journal,[string]$Phase)
    $Journal.phase = $Phase
    Write-RegistrationJsonAtomic $Path $Journal
}

function Invoke-RegistrationWaitFailpoint {
    param([string]$Name)
    if ($env:ARTEST_SDK_REGISTER_FAILPOINT -cne $Name) { return }
    $marker = [Environment]::GetEnvironmentVariable('ARTEST_SDK_REGISTER_FAILPOINT_MARKER')
    if ([string]::IsNullOrWhiteSpace($marker)) { Stop-Kit 'ARTESTREG009' 'Interrupted-publication failpoint requires a marker path.' }
    [IO.File]::WriteAllText([IO.Path]::GetFullPath($marker),$Name)
    $event = [Threading.ManualResetEventSlim]::new($false)
    try { $null = $event.Wait(120000) } finally { $event.Dispose() }
    Stop-Kit 'ARTESTREG009' 'Interrupted-publication failpoint expired without termination.'
}

function Invoke-GuidedRegister {
    param([object]$Paths,[string[]]$Values)
    $parsed = Read-GuidedArguments -Values $Values -AllowedOptions @('project','target','cli','catalog','config','configuration','msbuild') -MaximumPositionals 1
    if ($parsed.Options.ContainsKey('project') -and $parsed.Positionals.Count -gt 0) { Stop-Kit 'ARTESTREG001' 'Specify the project either positionally or with --project, not both.' }
    $projectText = if ($parsed.Options.ContainsKey('project')) { [string]$parsed.Options.project }
        elseif ($parsed.Positionals.Count -eq 1) { [string]$parsed.Positionals[0] }
        elseif (Test-Path -LiteralPath (Join-Path (Get-Location) $script:GuidedProjectName) -PathType Leaf) { [string](Get-Location) }
        else { Read-GuidedValue @{} 'project' 'Project folder' $null -Required }
    $project = Read-GuidedProject $projectText

    $configurationRoot = Get-RegistrationConfigurationRoot
    try { $null = New-Item -ItemType Directory -Path $configurationRoot -Force }
    catch { Stop-Kit 'ARTESTREG005' "Local installation profiles are not writable; elevation was not attempted: $($_.Exception.Message)" }
    $selectionLockPath = Join-Path $configurationRoot '.artest-installations.lock'
    try { $selectionLock = [IO.File]::Open($selectionLockPath,[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None) }
    catch { Stop-Kit 'ARTESTREG005' "Local installation profiles are busy or not writable; elevation was not attempted: $($_.Exception.Message)" }
    try {
        $selected = Resolve-InstallationProfile -Paths $Paths -Options $parsed.Options
        Assert-InstallationProfile $selected
        $profile = $selected.Profile
        $configurationParent = Split-Path -Parent $profile.configuration
        try { $null = New-Item -ItemType Directory -Path $configurationParent -Force }
        catch { Stop-Kit 'ARTESTREG005' "Installation '$($selected.Name)' is not writable; elevation was not attempted: $($_.Exception.Message)" }
        $installationLockPath = Join-Path $configurationParent ('.' + [IO.Path]::GetFileName($profile.configuration) + '.artest-register.lock')
        try { $installationLock = [IO.File]::Open($installationLockPath,[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None) }
        catch { Stop-Kit 'ARTESTREG005' "Installation '$($selected.Name)' is busy or not writable; elevation was not attempted: $($_.Exception.Message)" }
        try {
            try {
                $null = New-Item -ItemType Directory -Path $profile.configuration -Force
                $null = New-Item -ItemType Directory -Path (Split-Path -Parent $profile.catalog) -Force
            }
            catch { Stop-Kit 'ARTESTREG005' "Installation '$($selected.Name)' is not writable; elevation was not attempted: $($_.Exception.Message)" }
            if (-not (Test-Path -LiteralPath $profile.configuration -PathType Container) -or
                (Test-Path -LiteralPath $profile.catalog -PathType Leaf)) {
                Stop-Kit 'ARTESTREG005' "Installation '$($selected.Name)' catalog or configuration location has an incompatible type; elevation was not attempted."
            }
            Repair-RegistrationTransaction $selected

            $safeId = Get-SafeRegistrationId ([string]$project.Value.extensionId)
            $revisionRoot = Join-Path $profile.configuration ("r\" + $safeId.Substring(0,16))
            $language = [string]$project.Value.language
            if ($language -eq 'python') {
                if ($parsed.Options.ContainsKey('configuration') -or $parsed.Options.ContainsKey('msbuild')) { Stop-Kit 'ARTESTREG001' '--configuration and --msbuild apply only to C++ registration.' }
                $check = (Invoke-LowLevelPythonProject $Paths @('check',$project.Root) | Out-String) | ConvertFrom-Json
                if ($check.success -ne $true) { Stop-Kit 'ARTESTREG004' 'Python prerequisite check failed.' }
                $prepared = (Invoke-LowLevelPythonProject $Paths @('prepare',$project.Root,'--output-root',(Join-Path $revisionRoot 'py')) | Out-String) | ConvertFrom-Json
                $package = [IO.Path]::GetFullPath([string]$prepared.package)
                $receipt = [IO.Path]::GetFullPath([string]$prepared.receipt)
                $revisionId = [string]$prepared.preparationId
            }
            else {
                $buildArguments = @('--project',$project.Root)
                if ($parsed.Options.ContainsKey('configuration')) { $buildArguments += @('--configuration',[string]$parsed.Options.configuration) }
                if ($parsed.Options.ContainsKey('msbuild')) { $buildArguments += @('--msbuild',[string]$parsed.Options.msbuild) }
                $built = (Invoke-GuidedBuild $Paths $buildArguments | Out-String) | ConvertFrom-Json
                $sourcePackage = [IO.Path]::GetFullPath([string]$built.package)
                $revisionId = Get-RegistrationRevisionId $sourcePackage
                $revision = Join-Path $revisionRoot "$revisionId\package"
                if (-not (Test-Path -LiteralPath $revision)) {
                    $null = New-Item -ItemType Directory -Path (Split-Path -Parent $revision) -Force
                    Copy-Item -LiteralPath $sourcePackage -Destination $revision -Recurse
                }
                elseif ((Get-RegistrationRevisionId $revision) -cne $revisionId) { Stop-Kit 'ARTESTREG004' "Existing native revision failed integrity verification: $revision" }
                $package = $revision
                $receipt = $null
            }
            $manifest = Get-Content -LiteralPath (Join-Path $package 'artest-extension.json') -Raw | ConvertFrom-Json
            if ($manifest.extensionId -cne [string]$project.Value.extensionId) { Stop-Kit 'ARTESTREG004' 'Built package identity does not match the guided project.' }
            $packageInventory = Get-RegistrationInventory $package
            $statePath = Join-Path $profile.configuration 'registrations.json'
            $mappingPath = Join-Path $profile.configuration 'python-environments.json'
            $state = Read-RegistrationObject $statePath $script:RegistrationSchema
            $registrations = [ordered]@{}
            if ($state) { foreach ($property in $state.registrations.PSObject.Properties) { $registrations[$property.Name] = $property.Value } }
            $persistedMapping = [ordered]@{}
            if (Test-Path -LiteralPath $mappingPath -PathType Leaf) {
                try { $existingMapping = Get-Content -LiteralPath $mappingPath -Raw | ConvertFrom-Json }
                catch { Stop-Kit 'ARTESTREG002' "Cannot read existing Python environment mapping: $mappingPath" }
                foreach ($property in $existingMapping.PSObject.Properties) { $persistedMapping[$property.Name] = $property.Value }
            }
            $extensionId = [string]$manifest.extensionId
            $mappingMatches = if ($language -eq 'python') {
                $persistedMapping.Contains($extensionId) -and [string]$persistedMapping[$extensionId] -ceq $receipt
            } else { -not $persistedMapping.Contains($extensionId) }
            $mapping = [ordered]@{}
            foreach ($key in $persistedMapping.Keys) { $mapping[$key] = $persistedMapping[$key] }
            if ($language -eq 'python') { $mapping[$extensionId] = $receipt } elseif ($mapping.Contains($extensionId)) { $mapping.Remove($extensionId) }
            $packageName = "registered-$($safeId.Substring(0,16))"
            $existingRegistration = if ($registrations.Contains($extensionId)) { $registrations[$extensionId] } else { $null }
            $activePackage = Join-Path $profile.catalog $packageName
            if ($existingRegistration -and [string]$existingRegistration.revisionId -ceq $revisionId -and
                [string]$existingRegistration.packageName -ceq $packageName -and (Test-Path -LiteralPath $activePackage -PathType Container) -and $mappingMatches) {
                Assert-RegistrationInventory $activePackage $existingRegistration.inventory
                $scratch = Join-Path $profile.configuration ('.artest-register-scratch.' + [guid]::NewGuid().ToString('N'))
                try {
                    $null = New-Item -ItemType Directory -Path $scratch
                    $report = Invoke-InstalledDiscovery $profile $profile.catalog $mappingPath $extensionId $scratch
                    Save-InstallationSelection $selected
                    [ordered]@{
                        status='registered';message="Registration for $extensionId in installation '$($selected.Name)' is already current. No plan was executed."
                        installation=$selected.Name;extensionId=$extensionId;version=[string]$manifest.version;language=$language;revisionId=$revisionId;reused=$true
                        catalog=$profile.catalog;configuration=$profile.configuration;package=$activePackage;pythonEnvironments=$mappingPath;discoverySchema=[string]$report.schema;measurementRan=$false
                    } | ConvertTo-Json -Depth 8
                    return
                }
                finally { if (Test-Path -LiteralPath $scratch) { Remove-Item -LiteralPath $scratch -Recurse -Force } }
            }

            $catalogParent = Split-Path -Parent $profile.catalog
            $token = [guid]::NewGuid().ToString('N')
            $candidate = Join-Path $catalogParent ('.' + [IO.Path]::GetFileName($profile.catalog) + ".artest-candidate.$token")
            $backup = Join-Path $catalogParent ('.' + [IO.Path]::GetFileName($profile.catalog) + ".artest-backup.$token")
            $scratch = Join-Path $profile.configuration ".artest-register-scratch.$token"
            $journalPath = Join-Path $profile.configuration '.artest-register-transaction.json'
            try {
                if (Test-Path -LiteralPath $profile.catalog -PathType Container) { Copy-Item -LiteralPath $profile.catalog -Destination $candidate -Recurse }
                else { $null = New-Item -ItemType Directory -Path $candidate }
                $activeById = @{}
                foreach ($directory in Get-ChildItem -LiteralPath $candidate -Directory -Force) {
                    $manifestPath = Join-Path $directory.FullName 'artest-extension.json'
                    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) { continue }
                    $activeManifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
                    if ($activeById.ContainsKey([string]$activeManifest.extensionId)) { Stop-Kit 'ARTESTREG008' "Catalog already contains multiple revisions of $($activeManifest.extensionId)." }
                    $activeById[[string]$activeManifest.extensionId] = $directory.Name
                }
                if ($activeById.ContainsKey($extensionId)) {
                    $activeName = [string]$activeById[$extensionId]
                    if (-not $existingRegistration -or $activeName -cne [string]$existingRegistration.packageName -or $activeName -cne $packageName) {
                        Stop-Kit 'ARTESTREG008' "Extension ID $extensionId is owned by another package in installation '$($selected.Name)'."
                    }
                    Assert-RegistrationInventory (Join-Path $candidate $activeName) $existingRegistration.inventory
                    Remove-Item -LiteralPath (Join-Path $candidate $activeName) -Recurse -Force
                }
                elseif ($existingRegistration) { Stop-Kit 'ARTESTREG008' "Registration state for $extensionId does not own an active package in installation '$($selected.Name)'." }
                Copy-Item -LiteralPath $package -Destination (Join-Path $candidate $packageName) -Recurse
                Assert-RegistrationInventory (Join-Path $candidate $packageName) $packageInventory
                $registrations[$extensionId] = [ordered]@{
                    language=$language;packageName=$packageName;revisionId=$revisionId;version=[string]$manifest.version;package=$package;receipt=$receipt
                    inventory=$packageInventory;registeredAtUtc=(Get-Date).ToUniversalTime().ToString('o')
                }
                $newState = [ordered]@{schema=$script:RegistrationSchema;schemaVersion=1;registrations=$registrations}
                $hadCatalog = Test-Path -LiteralPath $profile.catalog -PathType Container
                $previousCatalogInventory = if ($hadCatalog) { Get-RegistrationInventory $profile.catalog } else { [ordered]@{} }
                $candidateCatalogInventory = Get-RegistrationInventory $candidate
                $selectionValue = Get-InstallationSelectionValue $selected
                $journal = [ordered]@{
                    schema=$script:TransactionSchema;schemaVersion=1;installation=$selected.Name
                    profile=[ordered]@{cli=$profile.cli;engine=$profile.engine;catalog=$profile.catalog;configuration=$profile.configuration;registry=$selected.RegistryPath}
                    token=$token;phase='prepared';catalog=$profile.catalog;candidate=$candidate;backup=$backup;hadCatalog=$hadCatalog
                    previousCatalogInventory=$previousCatalogInventory;candidateCatalogInventory=$candidateCatalogInventory
                    files=[ordered]@{
                        state=New-RegistrationFileRecord $statePath $newState
                        mapping=New-RegistrationFileRecord $mappingPath $mapping
                        profile=New-RegistrationFileRecord $selected.RegistryPath $selectionValue
                    }
                }
                Write-RegistrationJsonAtomic $journalPath $journal
                $null = New-Item -ItemType Directory -Path $scratch
                $candidateMapping = Join-Path $scratch 'python-environments.json'
                Write-JsonFile $candidateMapping $mapping
                $report = Invoke-InstalledDiscovery $profile $candidate $candidateMapping $extensionId $scratch
                if ($env:ARTEST_SDK_REGISTER_FAILPOINT -eq 'before-promotion') { Stop-Kit 'ARTESTREG009' 'Simulated interrupted publication before promotion.' }
                Set-RegistrationJournalPhase $journalPath $journal 'backingUp'
                Invoke-RegistrationWaitFailpoint 'before-backup-wait'
                if ($hadCatalog) { [IO.Directory]::Move($profile.catalog,$backup) }
                if ($env:ARTEST_SDK_REGISTER_FAILPOINT -eq 'after-backup') { Stop-Kit 'ARTESTREG009' 'Simulated interrupted publication after catalog backup.' }
                Set-RegistrationJournalPhase $journalPath $journal 'promoting'
                Invoke-RegistrationWaitFailpoint 'after-backup-wait'
                [IO.Directory]::Move($candidate,$profile.catalog)
                Set-RegistrationJournalPhase $journalPath $journal 'writingState'
                Write-RegistrationFileRecordNew $journal.files.state
                Write-RegistrationFileRecordNew $journal.files.mapping
                if ($env:ARTEST_SDK_REGISTER_FAILPOINT -eq 'after-state') { Stop-Kit 'ARTESTREG009' 'Simulated interrupted publication after state update.' }
                Set-RegistrationJournalPhase $journalPath $journal 'writingProfile'
                if ($env:ARTEST_SDK_REGISTER_FAILPOINT -eq 'profile-save') { Stop-Kit 'ARTESTREG009' 'Simulated installation profile save failure.' }
                Write-RegistrationFileRecordNew $journal.files.profile
                Set-RegistrationJournalPhase $journalPath $journal 'committed'
                try { Repair-RegistrationTransaction $selected }
                catch { Write-Warning "ARTESTREG011 Registration committed; transaction cleanup is deferred: $($_.Exception.Message)" }
                [ordered]@{
                    status='registered';message="Registered $extensionId with installation '$($selected.Name)'. No plan was executed."
                    installation=$selected.Name;extensionId=$extensionId;version=[string]$manifest.version;language=$language;revisionId=$revisionId
                    reused=$false;catalog=$profile.catalog;configuration=$profile.configuration
                    package=(Join-Path $profile.catalog $packageName);pythonEnvironments=$mappingPath;discoverySchema=[string]$report.schema;measurementRan=$false
                } | ConvertTo-Json -Depth 8
            }
            catch {
                $original = $_.Exception
                if (Test-Path -LiteralPath $journalPath -PathType Leaf) {
                    try { Repair-RegistrationTransaction $selected }
                    catch { Stop-Kit 'ARTESTREG007' "Registration failed and recovery also failed; journal was preserved. Original: $($original.Message) Recovery: $($_.Exception.Message)" }
                }
                elseif (Test-Path -LiteralPath $candidate -PathType Container) {
                    Assert-RegistrationPathNoReparse $candidate 'Unpublished candidate' | Out-Null
                    Remove-Item -LiteralPath $candidate -Recurse -Force
                }
                throw $original
            }
            finally {
                if (Test-Path -LiteralPath $scratch) {
                    Assert-RegistrationPathNoReparse $scratch 'Registration scratch path' | Out-Null
                    Remove-Item -LiteralPath $scratch -Recurse -Force
                }
            }
        }
        finally { $installationLock.Dispose() }
    }
    finally { $selectionLock.Dispose() }
}

function Invoke-GuidedRun {
    param([object]$Paths,[string[]]$Values)
    $parsed = Read-GuidedArguments -Values $Values -AllowedOptions @('project','target','mode') -MaximumPositionals 1
    if ($parsed.Options.ContainsKey('project') -and $parsed.Positionals.Count -gt 0) {
        Stop-Kit 'ARTESTREG001' 'Specify the project either positionally or with --project, not both.'
    }
    $projectText = if ($parsed.Options.ContainsKey('project')) { [string]$parsed.Options.project }
        elseif ($parsed.Positionals.Count -eq 1) { [string]$parsed.Positionals[0] }
        elseif (Test-Path -LiteralPath (Join-Path (Get-Location) $script:GuidedProjectName) -PathType Leaf) { [string](Get-Location) }
        else { Read-GuidedValue @{} 'project' 'Project folder' $null -Required }
    $project = Read-GuidedProject $projectText
    if ($project.Value.language -ne 'python') {
        Stop-Kit 'ARTESTSDK003' 'PY-DX-01 Stage 4 run supports Python Test plan projects only.'
    }
    $mode = if ($parsed.Options.ContainsKey('mode')) { [string]$parsed.Options.mode } else { 'sources' }
    if ($mode -notin @('sources','registered')) {
        Stop-Kit 'ARTESTREG001' '--mode must be sources or registered.'
    }

    $selected = Resolve-InstallationProfile -Paths $Paths -Options $parsed.Options
    Assert-InstallationProfile $selected
    $mapping = Join-Path ([string]$selected.Profile.configuration) 'python-environments.json'
    & $Paths.privatePython -I -B $Paths.pythonProjectTool run $project.Root `
        --cli-executable ([string]$selected.Profile.cli) `
        --mode $mode `
        --installation-catalog ([string]$selected.Profile.catalog) `
        --installation-python-environments $mapping `
        --expected-extension-id ([string]$project.Value.extensionId)
    $runExit = $LASTEXITCODE
    if ($runExit -ne 0) { exit $runExit }
}
