# Stage D3.2 manual acceptance

Run from D:\GitHub\main\ARTestCLI in PowerShell. No physical hardware is required.
The Word report is an evidence form, not a claim that manual cases already passed.
Keep the unfinished prior reports unchanged.

Define these variables after the official Release build:

```powershell
$cli = '.\artifacts\bin\x64\Release\ARTestCLI.exe'
$extensions = '.\artifacts\extensions\x64\Release'
$sample = '.\source\Scripts\ExtensionScript.json'
```

Record the tester, date, commit/branch and environment. For each case attach
console/report screenshots showing the command, exit code and verdict. A failure
expected by a negative test makes that test pass only if its exact diagnostic and
exit code match. All eight manual cases must pass for acceptance.

The Word report uses the established compact ARTest QA format and includes
actual-result, evidence-reference, verdict and defect fields. Insert screenshots
under each evidence block; tables expand as evidence is added.

cases.json is the source of the exact steps below and the Word generator. Generate
a new blank report with scripts/manual-tests/generate_stage_d3_2_report.py using
Python with python-docx. The generator refuses to overwrite an existing report.

## MT-D3.2-001 — Official Debug and Release regression

Confirm the build, architecture gates, ABI layout and every automated verdict.

Preconditions: Open PowerShell at D:\GitHub\main\ARTestCLI. Visual Studio 18 Insiders / v145 is installed.

1. Run .\scripts\build.ps1 -Configuration Debug -Platform x64.

   Expected: Both architecture checks and ABI contract pass. Google Test: 98 passed, 0 failed, 0 skipped, 25 suites.

2. Open artifacts\test-results\x64\Debug\ARTestCLI.UnitTests.html.

   Expected: Overall PASSED; Total=98, Passed=98, Failed=0. All individual verdicts agree.

3. Run .\scripts\build.ps1 -Configuration Release -Platform x64.

   Expected: Release passes the same 98 tests. The XML/HTML consistency validator passes.

4. Open artifacts\test-results\x64\Release\ARTestCLI.UnitTests.html. Attach both report screenshots.

   Expected: Configuration, platform, counts and final verdict are visible. Record build/commit identification.

## MT-D3.2-002 — Discover and activate four packages

Distinguish non-loading validation from deliberate DLL activation.

Preconditions: Release regression passed. Define $cli and $extensions as shown on the environment page.

1. Run & $cli extensions list $extensions; $LASTEXITCODE.

   Expected: Four VALID packages; all sha256=verified. Exit code 0.

2. Run & $cli extensions validate $extensions; $LASTEXITCODE.

   Expected: schema is artest.schema.extension-catalog.v2; valid=true; status=validated; generation=0; extensions is empty. Exit 0.

3. Run & $cli extensions doctor $extensions; $LASTEXITCODE.

   Expected: Catalog activation event followed by status=active, generation=1 and four active extensions. Exit 0.

## MT-D3.2-003 — Legacy script through migrated DLLs

Verify existing command names and instrument aliases still work after moving implementations out of Core.

Preconditions: Release binaries and their packaged extensions are present.

1. Run & $cli compile .\source\Scripts\TestScript.json; $LASTEXITCODE.

   Expected: Only 'Valid script. No instruments were initialized.' and exit 0. No initialization or activation events.

2. Run & $cli run .\source\Scripts\TestScript.json; $LASTEXITCODE.

   Expected: PowerSupply, CAN and Wait steps pass. State sequence includes INITIALIZING, RUNNING, CLEANING_UP, COMPLETED. Final PASSED; exit 0.

3. Inspect artifacts\extensions\x64\Release in File Explorer.

   Expected: ARTestCmdHardware, ARTestCmdSample, ARTestDrvSimCAN and ARTestDrvSimPower each contain a DLL, artest-extension.json and schemas.

## MT-D3.2-004 — Unified extension execution and debugging

Verify compile/run/debug/break use the same extension-aware path.

Preconditions: Define $sample = '.\source\Scripts\ExtensionScript.json'. $cli and $extensions are defined.

1. Run & $cli compile $sample --extensions $extensions; $LASTEXITCODE.

   Expected: Compilation succeeds without activation or driver initialization. Exit 0.

2. Run & $cli run $sample --extensions $extensions; $LASTEXITCODE.

   Expected: The sample power-cycle command passes through the simulated driver service; shutdown occurs; exit 0.

3. Run & $cli debug $sample --extensions $extensions. At the pause enter c, then inspect $LASTEXITCODE.

   Expected: Pause identifies step 1 before execution. Continue completes PASSED; exit 0.

4. Run & $cli break $sample 0 --extensions $extensions. At the pause enter q, then inspect $LASTEXITCODE.

   Expected: Breakpoint index 0 pauses at step 1. Quit requests cancellation; cleanup occurs; final CANCELLED; exit 5.

## MT-D3.2-005 — Reject invalid input while offline

Confirm parameter and binding validation fail before DLL activation or instrument initialization.

Preconditions: Use the versioned fixtures under quality\manual-tests\stage-d3.2\data.

1. Run & $cli compile .\quality\manual-tests\stage-d3.2\data\invalid-parameters.json --extensions $extensions; $LASTEXITCODE.

   Expected: PARAMETER_RANGE_INVALID identifies voltage in step parameters. Exit 3; no activation/initialization events.

2. Run & $cli compile .\quality\manual-tests\stage-d3.2\data\wrong-contract.json --extensions $extensions; $LASTEXITCODE.

   Expected: COMMAND_INSTRUMENT_CONTRACT_MISMATCH. A CAN driver cannot satisfy the power-supply requirement. Exit 3.

3. Run & $cli run .\quality\manual-tests\stage-d3.2\data\invalid-parameters.json --extensions $extensions; $LASTEXITCODE.

   Expected: The same compile diagnostic and exit 3. Execution never enters INITIALIZING.

## MT-D3.2-006 — Retry and cleanup failure regression

Verify the migrated simulated driver preserves retry controls and cleanup verdicts.

Preconditions: Stage C fixture files are unchanged. No physical equipment is connected.

1. Run & $cli run .\quality\manual-tests\stage-c\data\retry-success.json; $LASTEXITCODE.

   Expected: Step 1 passes on attempt 3; step 2 passes on attempt 1. Final PASSED, planned=2, passed=2, attempts=4. Exit 0.

2. Run & $cli run .\quality\manual-tests\stage-c\data\cleanup-failure.json; $LASTEXITCODE.

   Expected: The step passes, then shutdown reports EXTENSION_INVOCATION_FAILED. State FAILED; overall ERROR; exit 5.

3. Repeat the retry-success command.

   Expected: Again 3 attempts on step 1 and 4 total. No state from the previous process leaks into the next run.

## MT-D3.2-007 — Compatibility alias and final JSON

Confirm extension-run remains compatible without creating a second execution implementation.

Preconditions: $sample, $cli and $extensions are defined.

1. Run & $cli extension-run $sample $extensions; $LASTEXITCODE.

   Expected: Engine events show initialization, running, cleanup and completion. Exit 0.

2. Inspect the final JSON object printed before the exit-code line.

   Expected: status=passed; summary.plannedSteps=1, executedSteps=1, passedSteps=1. Command is com.artest.command.sample.power-cycle.

3. Attach a screenshot of the final JSON and exit code.

   Expected: The result is console output. No automatic JSON result file is expected or required.

## MT-D3.2-008 — Lifetime and offline-boundary evidence

Record direct evidence for cases not observable from the CLI alone.

Preconditions: Full Release regression passed. Do not modify packaged production binaries.

1. Run .\scripts\test-d3.2.ps1 -Configuration Release; $LASTEXITCODE.

   Expected: All 18 selected cases pass; exit 0. This focused run does not overwrite the full HTML report.

2. In the full Release HTML report, use Ctrl+F for "CompilesMetadataEvenWhenEntry" and "ChangedSchemaInvalidatesActivation".

   Expected: Both PASSED. Invalid binaries remain unloadable at runtime; changed schemas cannot silently rebind a prepared plan.

3. Search for "RepeatedRunsUseFreshDriver" and "OneSessionOwnsTheEngine".

   Expected: Both PASSED. Sequential reuse gets fresh instances; a live session prevents another from acquiring its Engine.

4. Search for "ConflictInDriverBatch" and "MinorThreeNegotiation".

   Expected: Both PASSED. Registration is atomic and an API 0.3 consumer's table is not overwritten.

5. Record screenshots and complete the final acceptance fields.

   Expected: All 8 manual cases must be Passed before acceptance. Record real results; do not infer manual completion from this template.
