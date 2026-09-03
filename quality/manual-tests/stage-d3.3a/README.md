# D3.3-A manual acceptance - C++ extension authoring SDK

Status: pending manual execution. Record real results in the new Word evidence report.
Do not edit or overwrite previous evidence reports. No physical hardware is required.

Run PowerShell from D:\GitHub\main\ARTestCLI:

```powershell
$cli = '.\artifacts\bin\x64\Release\ARTestCLI.exe'
$extensions = '.\artifacts\sdk-examples\x64\Release'
$sample = '.\source\ARTest.SDK\examples\ARTestSdkExample\ExamplePlan.json'
```

Baseline: 139 Google Tests / 33 suites; the Sdk* subset contains 41 tests.
The solution contains 10 projects. The original four-package catalog is unchanged.

## MT-D3.3A-001 - Solution and complete regression

Verify project loading, builds, ABI checks and all automated verdicts.

Preconditions: Visual Studio 18 Insiders / v145. No physical equipment is required.

1. Open source\ARTestCLI.sln in Visual Studio. Select x64 and Debug.

   Expected: All 10 projects load, including ARTestSdkExample; no duplicate-item errors.

2. Run .\scripts\build.ps1 -Configuration Debug -Platform x64.

   Expected: Architecture and ABI checks pass. Google Test: 139 passed, 0 failed, 0 skipped.

3. Run .\scripts\build.ps1 -Configuration Release -Platform x64.

   Expected: The same 139 tests pass; XML/HTML consistency validation passes.

4. Open artifacts\test-results\x64\Debug\ARTestCLI.UnitTests.html and the corresponding Release report.

   Expected: Both show PASSED, Total=139, Passed=139, Failed=0. Record both screenshots.

## MT-D3.3A-002 - Inspect the author-facing API

Confirm the example exposes component behavior without handwritten binary plumbing.

Preconditions: The SDK example project is visible in Solution Explorer.

1. Open ReadVoltageCommand.h and SimulatedSupplyDriver.h in ARTestSdkExample.

   Expected: Command implements Validate/Execute; driver implements Initialize/Shutdown and registers one operation.

2. Open ExampleExtension.cpp.

   Expected: It declares AddCommand/AddDriver and one ARTEST_EXPORT_EXTENSION. No ABI function table or handle cast is handwritten.

3. Open source\ARTest.SDK\README.md and docs\sdk\ai-extension-authoring.md.

   Expected: Public entry points, lifetimes, error propagation, testing and agent constraints are documented. Record evidence and any ambiguity.

## MT-D3.3A-003 - Compile and execute the SDK example

Verify the new C++ adapter integrates with the existing Engine.

Preconditions: Define $cli, $extensions and $sample as shown on the environment page.

1. Run & $cli compile $sample --extensions $extensions; $LASTEXITCODE.

   Expected: Valid script. No instruments were initialized. Exit 0; no initialization events.

2. Run & $cli run $sample --extensions $extensions; $LASTEXITCODE.

   Expected: Driver initializes, step reports Measured 12.000000 V., driver shuts down, final PASSED; exit 0.

3. Inspect artifacts\sdk-examples\x64\Release\ARTestSdkExample.

   Expected: Contains ARTestSdkExample.dll, artest-extension.json and schemas. The normal extensions directory still contains four packages.

## MT-D3.3A-004 - Reject invalid parameters and configuration

Ensure schemas reject invalid input before execution and do not report success.

Preconditions: Use the supplied fixtures; do not edit the example or packaged manifests.

1. Run & $cli compile ".\quality\manual-tests\stage-d3.3a\data\invalid-channel.json" --extensions $extensions; $LASTEXITCODE.

   Expected: PARAMETER_RANGE_INVALID identifies channel. Exit 3; no driver initialization.

2. Run & $cli run ".\quality\manual-tests\stage-d3.3a\data\invalid-config.json" --extensions $extensions; $LASTEXITCODE.

   Expected: PARAMETER_RANGE_INVALID identifies voltage in configuration. Exit 3; no execution.

3. Run & $cli run $sample --extensions $extensions; $LASTEXITCODE.

   Expected: The valid example still completes PASSED with exit 0. Earlier invalid attempts did not damage the package.

## MT-D3.3A-005 - Cancel with guaranteed cleanup

Verify cancellation of the SDK command still shuts down its driver.

Preconditions: Release example and SDK catalog are built.

1. Run & $cli debug $sample --extensions $extensions.

   Expected: Driver initializes and the debugger pauses before step 1.

2. At the pause, enter q and press Enter. Then run $LASTEXITCODE.

   Expected: CLEANING_UP and SDK simulated power supply shut down. are printed; final CANCELLED; exit 5.

3. Repeat with & $cli debug $sample --extensions $extensions, then enter c.

   Expected: Execution completes PASSED, measurement is printed, cleanup occurs; exit 0.

## MT-D3.3A-006 - SDK fault and ownership evidence

Record direct evidence of ABI and fault cases that the CLI alone cannot expose.

Preconditions: Full Release regression passed; its HTML report is available.

1. Run .\scripts\test-sdk-authoring.ps1 -Configuration Release; $LASTEXITCODE.

   Expected: All 41 selected SDK tests pass; exit 0. The full 139-test XML/HTML report is not overwritten.

2. In the full HTML report, search for SmallErrorBuffers and PartialInitializationCanCleanUp.

   Expected: Both PASSED. Small buffers cannot truncate diagnostics or replay commands; partial initialization can clean up after cancellation.

3. Search for ReleaseFailuresCannotTurnIntoSuccess and MalformedOrRepeatedServiceResults.

   Expected: Both PASSED. Failed release or invalid service output cannot become success.

4. Search for CommandBehaviorIsTestableWithoutEngineOrDll and ExampleDllRunsThroughTheUnmodifiedEngine.

   Expected: Both PASSED, covering local behavior and real DLL integration. Attach evidence.

## MT-D3.3A-007 - Configuration and fresh-run isolation

Verify per-run driver configuration and state isolation.

Preconditions: Use the voltage-5v fixture without editing binaries or source.

1. Run & $cli run ".\quality\manual-tests\stage-d3.3a\data\voltage-5v.json" --extensions $extensions; $LASTEXITCODE.

   Expected: Measured 5.000000 V.; final PASSED; exit 0.

2. Run & $cli run $sample --extensions $extensions; $LASTEXITCODE.

   Expected: Measured 12.000000 V.; final PASSED; exit 0. The earlier configured voltage does not leak.

3. Complete actual results, evidence references and final acceptance fields.

   Expected: All seven cases must be Passed before acceptance. Do not infer manual completion from the template.

## Evidence document

Create a new blank report with scripts/manual-tests/generate_stage_d3_3a_report.py.
The generator refuses to overwrite an existing report. The default filename is
ARTestCLI_Manual_Test_Report_SDK_Authoring_v1.0.docx in this directory.
