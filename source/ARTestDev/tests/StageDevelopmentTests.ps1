param([Parameter(Mandatory)][string]$Executable, [Parameter(Mandatory)][string]$QtRoot,
      [Parameter(Mandatory)][string]$EvidenceRoot, [Parameter(Mandatory)][string]$Configuration,
      [Parameter(Mandatory)][string]$Wheelhouse)
$ErrorActionPreference = 'Stop'
$script = Join-Path $PSScriptRoot '../StageDevelopment.ps1'
$root = Join-Path $EvidenceRoot ("assembly-$Configuration-" + [guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $root
foreach ($fault in 'corrupt','missing','additional','duplicate') {
    $destination = Join-Path $root $fault
    & $script -Executable $Executable -QtRoot $QtRoot -Destination $destination -Configuration $Configuration -Wheelhouse $Wheelhouse
    $descriptor = Join-Path $destination 'artestdev-staging.json'
    if ($fault -eq 'corrupt') { [IO.File]::WriteAllText((Join-Path $destination 'python/tools/project.py'), 'corrupted fixture') }
    if ($fault -eq 'missing') {
        # Exact, owned test file; preserve the descriptor and all other evidence.
        Remove-Item -LiteralPath (Join-Path $destination 'python/tools/project.py')
    }
    if ($fault -eq 'additional') { [IO.File]::WriteAllText((Join-Path $destination 'unknown.txt'), 'foreign fixture') }
    if ($fault -eq 'duplicate') {
        $value = Get-Content -Raw -LiteralPath $descriptor | ConvertFrom-Json
        $value.files += $value.files[0]
        $value | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $descriptor -Encoding utf8
    }
    $before = (Get-FileHash -LiteralPath $descriptor).Hash
    $rejected = $false
    try { & $script -Executable $Executable -QtRoot $QtRoot -Destination $destination -Configuration $Configuration -Wheelhouse $Wheelhouse }
    catch { $rejected = $true; $_.ToString() | Set-Content -LiteralPath (Join-Path $root "$fault.txt") }
    if (!$rejected -or (Get-FileHash -LiteralPath $descriptor).Hash -ne $before) { throw "Failed to preserve rejected $fault staging" }
    Write-Output "PASS: $fault rejected without refreshing hashes"
}
Write-Output "Preserved assembly evidence: $root"
