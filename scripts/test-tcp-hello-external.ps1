param(
    [ValidateSet('Debug','Release')][string]$Configuration = 'Release',
    [Parameter(Mandatory)][string]$SDKArchive,
    [string]$VisualStudioPath = 'D:\Program Files\Microsoft Visual Studio\18\Insiders'
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$source = Join-Path $repo 'examples\ARTestTcpHello'
$SDKArchive = (Resolve-Path -LiteralPath $SDKArchive).Path
$token = [guid]::NewGuid().ToString('N')
$external = Join-Path ([IO.Path]::GetTempPath()) "ARTestTcpHello-External-$token"
$evidence = "$repo\artifacts\acceptance\c02\external-$Configuration-$token"
New-Item -ItemType Directory -Path "$external\example", "$external\sdk", $evidence | Out-Null
# Explicit source inventory: no build products, repository headers, user settings or test fakes.
$files = @('ARTestTcpHello.vcxproj','Extension.cpp','SetAndMeasureCommand.h','TcpVoltageDriver.h',
    'tcp\Transport.h','simulator\Server.h','simulator\Main.cpp','simulator\ARTestTcpSimulator.vcxproj',
    'ExamplePlan.json','Build.ps1','Run.ps1','README.md','python\extension.py')
foreach ($relative in $files) {
    $destination = Join-Path "$external\example" $relative
    New-Item -ItemType Directory -Path (Split-Path $destination) -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $source $relative) -Destination $destination
}
Expand-Archive -LiteralPath $SDKArchive -DestinationPath "$external\sdk"
$manifest = @(Get-ChildItem -LiteralPath "$external\sdk" -Filter sdk-version.json -Recurse -File)
if ($manifest.Count -ne 1) { throw 'Expected exactly one installed SDK root.' }
$sdk = $manifest[0].DirectoryName
$inventory = Get-Content -LiteralPath "$sdk\sdk-manifest.json" -Raw | ConvertFrom-Json
foreach ($entry in $inventory.files) {
    $file = [IO.Path]::GetFullPath((Join-Path $sdk $entry.path))
    if (!$file.StartsWith($sdk.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase) -or
        (Get-FileHash -LiteralPath $file).Hash -ine $entry.sha256) { throw 'Installed SDK inventory mismatch.' }
}
if (@(Get-ChildItem -LiteralPath $sdk -Recurse -File).Count -ne $inventory.files.Count + 1) {
    throw 'Installed SDK contains unlisted files.'
}
& "$external\example\Build.ps1" -SDKRoot $sdk -Configuration $Configuration -VisualStudioPath $VisualStudioPath |
    Tee-Object -FilePath "$evidence\build.log"
$cli = "$repo\artifacts\bin\x64\$Configuration\ARTestCLI.exe"
& $cli compile "$external\example\ExamplePlan.json" --extensions "$external\example\out\extensions\x64\$Configuration" |
    Tee-Object -FilePath "$evidence\offline.log"
if ($LASTEXITCODE -ne 0) { throw 'External offline compilation failed.' }
& "$external\example\Run.ps1" -CLI $cli -Configuration $Configuration | Tee-Object -FilePath "$evidence\run.log"
$run = @(Get-ChildItem -LiteralPath "$external\example\out\runs" -Directory)
if ($run.Count -ne 1) { throw 'Expected one independently owned external run.' }
Copy-Item -LiteralPath $run[0].FullName -Destination "$evidence\run" -Recurse
$journal = @(Get-Content -LiteralPath "$evidence\run\server.jsonl" | ForEach-Object { $_ | ConvertFrom-Json })
$final = Get-Content -LiteralPath "$evidence\run\cli.txt" | Select-Object -Last 1 | ConvertFrom-Json
if ($final.status -ne 'passed' -or $final.summary.totalAttempts -ne 2 -or
    $final.steps[0].outcome.data.value -ne 12 -or $final.steps[1].outcome.data.value -ne 5) {
    throw 'External typed measurement result mismatch.'
}
if (@($journal | Where-Object event -eq 'applied').Count -ne 2 -or $journal[-1].event -ne 'stopped') {
    throw 'External run effect/stop evidence failed.'
}
@{baseCommit=(& git -C $repo rev-parse HEAD); externalRoot=$external; sdkArchive=$SDKArchive;
  sdkSha256=(Get-FileHash -LiteralPath $SDKArchive).Hash; configuration=$Configuration;
  files=@($files | ForEach-Object { @{path=$_; sha256=(Get-FileHash -LiteralPath (Join-Path $source $_)).Hash} });
  status='passed'} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath "$evidence\receipt.json" -Encoding utf8
Write-Host "External kit gate passed: $evidence"
Write-Host "Owned copied kit retained for inspection: $external"
