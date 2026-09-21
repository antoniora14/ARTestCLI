# First-use exercise: create, edit, build, register, and run

Use a newly extracted development-kit ZIP. Do not open an ARTest source checkout
or edit generated IDs, wheels, receipts, environment mappings, or installation
state. Record the elapsed time and any help needed after each numbered step.

Prerequisites:

- Windows x64 and PowerShell 7.
- For Python: none beyond this extracted kit. Do not use a global Python.
- For C++: Visual Studio 18 Insiders, Desktop development with C++, and MSVC v145
  x64. The kit does not install the compiler.

Choose empty working and installation folders. Paths with spaces are intentional:

```powershell
$kit = 'D:\SDKs with spaces\ARTest Development Kit'
$work = 'D:\ARTest first use\Projects'
$target = 'D:\ARTest first use\Installed target'
$entry = Join-Path $kit 'artest.ps1'
New-Item -ItemType Directory -Force -Path $work, $target | Out-Null
Set-Location $env:TEMP
```

1. Verify the extracted kit.

   ```powershell
   & $entry verify
   ```

   Continue only when the result says `ready`.

2. Create a Python starter and edit its Test script.

   ```powershell
   & $entry new --name 'Python first use' --folder $work --language python
   notepad (Join-Path $work 'Python first use\src\extension.py')
   ```

   In the opened file, change only the text `Minimum simulated value check` to
   `First-use simulated value check`, then save and close it.

3. Build twice. The second result should report `reused: true`.

   ```powershell
   & $entry build --project (Join-Path $work 'Python first use')
   & $entry build --project (Join-Path $work 'Python first use')
   ```

4. Register with the extracted kit's evaluation runtime, then explicitly run the
   registered Python Test plan.

   ```powershell
   $cli = Join-Path $kit 'runtime\x64\Release\ARTestCLI.exe'
   $catalog = Join-Path $target 'extensions'
   $config = Join-Path $target 'configuration'
   & $entry register --project (Join-Path $work 'Python first use') `
       --target first-use --cli $cli --catalog $catalog --config $config
   & $entry run --project (Join-Path $work 'Python first use') `
       --target first-use --mode registered
   ```

   Registration must say that no plan was executed. The separate run must finish
   `passed` and show `First-use simulated value check`.

5. Create, edit, build, and register the C++ starter.

   ```powershell
   & $entry new --name 'C++ first use' --folder $work --language cpp
   notepad (Join-Path $work 'C++ first use\ReadValueCommand.h')
   ```

   Change only `Computed value ` to `First-use computed value `, save, then run:

   ```powershell
   & $entry build --project (Join-Path $work 'C++ first use')
   & $entry register --project (Join-Path $work 'C++ first use') --target first-use
   ```

6. Execute the C++ Test plan through the existing native CLI path. This is not a
   development-kit `run` command for C++.

   ```powershell
   $cppPlan = Join-Path $work 'C++ first use\TestPlan.json'
   $mapping = Join-Path $config 'python-environments.json'
   & $cli compile $cppPlan --extensions $catalog --python-environments $mapping
   & $cli extension-run $cppPlan $catalog --python-environments $mapping
   ```

   The final result must be `passed` and contain `First-use computed value 84`.

7. Report the observation.

   Record: engineer role and prior ARTest experience; ZIP filename and SHA-256;
   start/end time; each command's result; where the guide was unclear; every hint
   or intervention and the step where it occurred; whether any internal file was
   edited; and the final Python/C++ result. Do not include secrets or personal
   machine paths beyond the test folders.

Stop and preserve the exact diagnostic if a command fails. Do not repair receipts,
associations, generated package metadata, or installation files by hand.
