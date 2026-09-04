# D3.3-B manual acceptance

Status: pending manual execution. Record actual results and evidence in the new Word report.
Do not alter earlier evidence documents. All fixtures use simulations only.

## Setup

Open PowerShell and run:

```powershell
Set-Location 'D:\GitHub\main\ARTestCLI'
$cli = '.\artifacts\bin\x64\Release\ARTestCLI.exe'
$extensions = '.\artifacts\extensions\x64\Release'
$data = '.\quality\manual-tests\stage-d3.3b\data'
```

Run each command separately and inspect `$LASTEXITCODE` immediately afterward.
A negative test passes when it produces its expected nonzero exit and diagnostic.
`extension-run` prints a final JSON line to the console; it does not create a result file.
The `debug` command prints a textual summary instead.
Do not require identical durations or interleaving of stdout and stderr.

## MT-D3.3B-001 Build and verify the migrated references

Confirm the complete regression, SDK distribution and developer-facing boundaries.

Preconditions: Visual Studio 18 Insiders and C++ tools are installed. Use the repository root.

1. .\scripts\build.ps1 -Configuration Release -Platform x64

   Expected: Exit 0. Architecture gates, ABI layout, installed SDK consumer and all 161 Google Tests pass.

2. Open artifacts\test-results\x64\Release\ARTestCLI.UnitTests.html.

   Expected: Overall PASSED; total 161, passed 161, failed 0, skipped 0. Individual verdicts agree.

3. Open source\ARTestCLI.sln in Visual Studio. Inspect the four reference projects and their entry files.

   Expected: All ten projects load. The references import ARTestSDK.props. Entry files contain registration; behavior is in separate command or driver classes.

4. Inspect source\ARTest.SDK\sdk-version.json and run .\scripts\verify-sdk-authoring.ps1.

   Expected: SDK 0.1.1, Engine API 0.4, native ABI 0.1. Boundary verification PASSED. No private ABI helper in reference code.

## MT-D3.3B-002 Preserve catalog identity and successful execution

Verify all four packages and both canonical IDs and legacy aliases.

Preconditions: Case 001 passed. Define $cli, $extensions and $data as shown on the setup page.

1. & $cli extensions doctor $extensions
   $LASTEXITCODE

   Expected: Exit 0; catalog active, four extensions, seven components; integrity verified.

2. & $cli extension-run "$data\mixed-aliases.json" $extensions
   $LASTEXITCODE

   Expected: Exit 0. Final JSON: status passed; plannedSteps, executedSteps and passedSteps are all 4.

3. & $cli extension-run "$data\mixed-canonical.json" $extensions
   $LASTEXITCODE

   Expected: Exit 0 and the same four-step verdict. JSON commands now use canonical component IDs.

4. Inspect completion messages and cleanup from both runs.

   Expected: Power On, Send CAN Message and Power Off completed. The sample command succeeds; PS1 and CAN1 shut down. No failed steps.

## MT-D3.3B-003 Preserve input and cleanup failures

Verify invalid input and simultaneous initialization and cleanup errors cannot become successful runs.

Preconditions: Case 001 passed. Use the supplied fixtures without modifying package manifests.

1. & $cli extension-run "$data\invalid-can-frame.json" $extensions
   $LASTEXITCODE

   Expected: Exit 5. Final JSON status error; executedSteps 1, passedSteps 0, errorSteps 1. Message: Invalid CAN identifier, DLC or data length.

2. & $cli extension-run "$data\can-init-cleanup-failure.json" $extensions
   $LASTEXITCODE

   Expected: Exit 4. Final JSON status error; executedSteps 0, skippedSteps 4. Both CAN_RESOURCE_MISSING and Simulated CAN cleanup failure. remain visible.

3. & $cli extension-run "$data\mixed-canonical.json" $extensions
   $LASTEXITCODE

   Expected: Exit 0, status passed, passedSteps 4. Failed runs did not damage packages or leak simulated state.

## MT-D3.3B-004 Interrupt execution and guarantee cleanup

Verify cooperative timeout, user cancellation and fresh-run recovery.

Preconditions: Case 001 passed. No physical instruments are involved.

1. & $cli extension-run "$data\timeout-cleanup-failure.json" $extensions
   $LASTEXITCODE

   Expected: Exit 5. Final JSON: status error, timedOutSteps 1, passedSteps 0. Step duration is normally tens of milliseconds, not the full 1000 ms hold. Shutdown failure is retained.

2. & $cli debug "$data\mixed-canonical.json" --extensions $extensions
   At the first pause, enter q and press Enter. Then run $LASTEXITCODE.

   Expected: Exit 5. CLEANING_UP, simulated power shutdown and final CANCELLED; executed 0, skipped 4.

3. Repeat the debug command, enter c and press Enter. Then run $LASTEXITCODE.

   Expected: Exit 0. All four steps pass and cleanup runs. The earlier cancellation does not leak into the new session.

4. .\artifacts\bin\x64\Release\ARTestCLI.UnitTests.exe --gtest_filter=Reference*
   $LASTEXITCODE

   Expected: Exit 0. All 20 reference tests pass, including the real-DLL early-timeout regression. Full XML/HTML reports are not overwritten.

## Acceptance and evidence

All four cases must pass with actual results, screenshots or log references, tester,
date, configuration and revision recorded. Manual acceptance remains Pending until
execution. A failed run in cases 003 or 004 is intentional and is not a failed test
when every expected diagnostic and counter agrees.

The source cases are in cases.json. Generate a blank report using
scripts/manual-tests/generate_stage_d3_3b_report.py with python-docx installed.
The generator refuses to overwrite an existing report; use --output for another name.
Build outputs and SDK ZIPs under artifacts are not source files.
