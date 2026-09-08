param([ValidateSet('Debug','Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$repo = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$root = Join-Path $repo ('artifacts\manual-tests\d42-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path "$root\catalog" -Force | Out-Null
foreach ($package in 'ARTestCmdSample','ARTestDrvSimPower') {
    Copy-Item -LiteralPath "$repo\artifacts\extensions\x64\$Configuration\$package" -Destination "$root\catalog\$package" -Recurse
}
Copy-Item -LiteralPath "$repo\artifacts\python\extensions\ARTestPySimulated" -Destination "$root\catalog\ARTestPySimulated" -Recurse
$source = Get-Content -LiteralPath "$repo\source\ARTest.Python\examples\PythonMeasurement.json" -Raw
function Write-Case([string]$Name, [scriptblock]$Edit) {
    $plan = $source | ConvertFrom-Json
    & $Edit $plan
    $plan | ConvertTo-Json -Depth 20 | Set-Content -LiteralPath "$root\$Name.json" -Encoding utf8
}
Write-Case 'PythonPassed' { param($p) }
Write-Case 'PythonFailed' { param($p) $p.commands[0].params.voltage = 4.2 }
Write-Case 'PythonToNative' { param($p) $p.instruments[0].type = 'com.artest.driver.sim.power' }
Write-Case 'NativeToPython' { param($p)
    $p.commands[0].name = 'com.artest.command.sample.power-cycle'
    $p.commands[0].params = @{channel=1; voltage=12.0; holdMs=10}
}
Write-Case 'PythonError' { param($p) $p.commands[0].params | Add-Member -NotePropertyName fail -NotePropertyValue $true }
Write-Case 'PythonTimeout' { param($p)
    $p.commands[0].params | Add-Member -NotePropertyName holdMs -NotePropertyValue 2000
    $p.commands[0].policy.timeoutMs = 100
}
Write-Case 'PythonCancel' { param($p)
    $p.commands[0].params | Add-Member -NotePropertyName holdMs -NotePropertyValue 15000
    $p.commands[0].policy.timeoutMs = 30000
}
Write-Case 'PythonCleanupError' { param($p) $p.instruments[0].config | Add-Member -NotePropertyName failShutdown -NotePropertyValue $true }
Write-Output $root
