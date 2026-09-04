# ARTestCLI automated testing

## Structure

The `tests\ARTestCLI.UnitTests.vcxproj` project uses Google Test 1.18, is part of
`source\ARTestCLI.sln`, and links the production `ARTestEngine.Core` library.

- `ScriptDocumentTests.cpp`: JSON parsing, schema, format, version, and typed models.
- `InstrumentFactoryTests.cpp`: injectable registries, lifetime, cleanup, events,
  and fake instrument behavior.
- `CommandFactoryTests.cpp`: semantic compilation, command registration, bindings,
  validation, and atomic rejection.
- `ScriptExecutorTests.cpp`: execution results, events, cancellation, exception
  containment, and execution context.
- `StageCExecutionTests.cpp`: state transitions, cancellation deadlines, retry,
  timeout, failure policies, asynchronous sessions, and cleanup guarantees.
- `StageDExtensionTests.cpp`: ABI negotiation, side-effect-free catalog
  validation, path containment, SHA-256 integrity, duplicate IDs, failure
  containment, native loading, services, cancellation, and unload safety.
- `StageDThinHostTests.cpp`: Engine API 0.1/0.2/0.3 compatibility, detailed
  compilation, controlled sessions, and source-level dependency enforcement.
- `CliThinHostTests.cpp`: compile, run, debug, break, catalog list/validate/doctor,
  validation, cancellation, legacy output, and process exit-code contracts.

The Stage D3.3-B baseline contains 161 test cases across 36 suites.
`StageD32Tests.cpp` adds schema, metadata-only compilation, transaction ownership,
catalog revision, native lifetime, and ABI-prefix regressions. The official build
also executes `scripts/verify-core-boundary.ps1`.

- `SdkAuthoringTests.cpp`: strict parameter reads, explicit results, local
  commands/drivers and deterministic cancellation/deadline testing.
- `SdkAbiAdapterTests.cpp`: metadata, opaque-handle ownership, lifecycle,
  partial initialization cleanup, errors, malformed input and service release.
- `SdkAuthoringIntegrationTests.cpp`: the SDK example loaded by the unchanged
  Engine, fresh sessions, schema rejection, CLI execution and architecture checks.

The SDK example and these three test sources compile with `/W4 /WX`. The
official workflow also runs `scripts/verify-sdk-authoring.ps1`.

- `ReferenceExtensionTests.cpp`: migrated behavior, service ordering, state,
  typed validation, cancellation, partial initialization and cleanup failures.
- `ReferenceExtensionIntegrationTests.cpp`: all four real DLLs, alias/canonical
  compatibility, malformed CAN frames, initialization and timeout failures.

The reference components and new tests also compile with `/W4 /WX`. The boundary
gate rejects private dependencies and handwritten ABI code in reference packages.

## Run from PowerShell

From the repository root:

```powershell
.\scripts\build.ps1 -Configuration Debug -Platform x64
```

Before integrating changes, run both configurations:

```powershell
.\scripts\build.ps1 -Configuration Release -Platform x64
```

The script returns a non-zero exit code if compilation, any test, the report
generator validation, or report consistency validation fails.

To compile without running tests:

```powershell
.\scripts\build.ps1 -Configuration Debug -Platform x64 -SkipTests
```

## Run from Visual Studio Test Explorer

1. Open `source\ARTestCLI.sln` with Visual Studio Insiders.
2. Select `x64` and `Debug` or `Release`.
3. Open **Test > Test Explorer**.
4. Build the solution with **Build > Build Solution**.
5. Confirm that 161 tests from 36 suites are discovered.
6. Select **Run All Tests**.
7. Verify that all 161 tests finish with a `Passed` verdict.

### Project loading validation

The solution contains ten projects. `Directory.Build.targets` checks evaluated
file items before each C++ build, including builds started directly in Visual
Studio. Duplicate full paths (also across item types, imports or overlapping
wildcards) fail with `ARTESTBUILD001`. Ordinary MSBuild compilation can otherwise
accept duplicates that the Visual Studio project loader rejects.

If Visual Studio has kept `ARTestCmdHardware` or `ARTestDrvSimCAN` unloaded after
the D3.2 project-file correction, right-click each project and select **Reload
Project**, or close and reopen `source\ARTestCLI.sln`. Confirm that all ten
projects load without duplicate-item errors, then run the regression above.
There is no need to delete `.vs` or reset IDE settings for this correction.

## Stage D3.3-A SDK acceptance

Run the focused SDK regression after building:

```powershell
.\scripts\test-sdk-authoring.ps1 -Configuration Release
```

It now runs 43 tests without overwriting the full XML/HTML baseline. This subset does
not replace the complete Debug and Release regressions before integration.
The example is packaged under `artifacts/sdk-examples/x64/<Configuration>`,
separate from the four-package reference catalog.

Use [the seven-case manual protocol](quality/manual-tests/stage-d3.3a/README.md).
It covers solution loading, author-facing source, a real 12 V run, schema
rejection, cancellation/cleanup, fault-test evidence, and a fresh 5 V/12 V run.
Record actual results in the new Word report; do not overwrite earlier evidence.

## Stage D3.3-B reference migration

After building, run the focused reference regression without replacing reports:

```powershell
.\artifacts\bin\x64\Release\ARTestCLI.UnitTests.exe --gtest_filter=Reference*
```

Expected: 20 tests across three suites pass. The two additional SDK schema tests
are included in the 43-test SDK subset and the complete 161-test regression.
Use [the manual protocol](quality/manual-tests/stage-d3.3b/README.md) and its new
Word evidence report. All fixtures use simulations, not physical instruments.

## Stage D3.3-C installed-SDK compatibility

The official build runs:

```powershell
.\scripts\test-sdk-distribution.ps1 -Configuration Release -Platform x64
```

This is an integration gate, not an additional Google Test count. It packages
and inventories the SDK, checks archive paths and hashes, extracts the ZIP,
builds its copied starter with `/W4 /WX`, activates the external DLL, compiles
its plan without initialization, executes a command-to-driver call, and verifies
cleanup. Run Debug and Release when package or public SDK headers change.

## Stage D3.2 acceptance

Use [the exact manual protocol](quality/manual-tests/stage-d3.2/README.md).
Its Word report has per-case results, defect references and screenshot evidence
areas. Do not commit an unfinished evidence report. The source generator and
fixture scripts are versioned separately.

## Stage D3.1 catalog regression

The two non-loading commands are safe for package inspection. `doctor` also
loads the DLLs and checks their ABI descriptors:

```powershell
$cli = '.\artifacts\bin\x64\Release\ARTestCLI.exe'
$extensions = '.\artifacts\extensions\x64\Release'

& $cli extensions list $extensions
& $cli extensions validate $extensions
& $cli extensions doctor $extensions
```

All commands must return `0` for the packaged reference catalog. The validation
report must use `artest.schema.extension-catalog.v2`; `validate` reports status
`validated` and generation `0`, while `doctor` reports status `active` and
generation `1`. Invalid catalogs return exit code `6`.

## Stage D2 thin-host boundary

Every ARTestCLI build executes `scripts\verify-thin-host.ps1` before compilation.
The build fails if a CLI source or project file references ARTestEngine.Core.
Google Test independently checks the same dependency rule and executes every
legacy command through `ARTestEngine.dll`.

From the repository root, the supported command regression is:

```powershell
$cli = '.\artifacts\bin\x64\Release\ARTestCLI.exe'
$script = '.\source\Scripts\TestScript.json'

& $cli compile $script
& $cli run $script
& $cli debug $script
& $cli break $script 1
```

For `debug`, enter `c` at the first prompt. For `break ... 1`, step 1 executes
without a prompt and the CLI pauses at script step 2. All four commands must
return 0. Invalid scripts retain exit code 3, initialization failures retain 4,
and cancelled/failed executions retain 5.

## Stage D1 native vertical slice

Build packages both reference extensions under
`artifacts\extensions\<Platform>\<Configuration>`. Run the public Engine
path from the repository root:

```powershell
$cli = '.\artifacts\bin\x64\Release\ARTestCLI.exe'
$extensions = '.\artifacts\extensions\x64\Release'

& $cli extension-run '.\source\Scripts\ExtensionScript.json' $extensions
```

The process exit code must be 0, the JSON result must report a passed status,
and the step must be com.artest.command.sample.power-cycle. The command resolves
SimPower1 through artest.contract.instrument.power-supply.v1, and cleanup shuts
the driver down before the Engine releases its native modules.

## Reports

Each execution generates:

- `artifacts\test-results\<Platform>\<Configuration>\ARTestCLI.UnitTests.xml`
- `artifacts\test-results\<Platform>\<Configuration>\ARTestCLI.UnitTests.html`

The workflow first tests the report generator with synthetic `PASSED`, `FAILED`,
and `SKIPPED` cases. It then compares Google Test aggregate counters against
every individual case verdict. A contradiction stops the build.

Reports are local artifacts excluded from Git. Attach them as execution evidence;
do not commit them.

## Stage C end-to-end regression

Use the fixtures in `quality\manual-tests\stage-c\data` with the Debug CLI:

```powershell
$cli = '.\artifacts\bin\x64\Debug\ARTestCLI.exe'

& $cli run '.\quality\manual-tests\stage-c\data\retry-success.json'
& $cli run '.\quality\manual-tests\stage-c\data\timeout-stop.json'
& $cli run '.\quality\manual-tests\stage-c\data\continue-on-failure.json'
& $cli run '.\quality\manual-tests\stage-c\data\cleanup-failure.json'
& $cli compile '.\quality\manual-tests\stage-c\data\invalid-policy.json'
```

Expected exit codes are `0`, `5`, `5`, `5`, and `3`, respectively. For the
interactive cancellation case, run `cancel-cleanup.json`, wait until
`Time.WaitMs` starts, and press Ctrl+C. The output must show `CANCELLING`, then
`CLEANING_UP`, the fake power supply `Shutdown`, and final `CANCELLED`; the
process returns `5`.

## Add a test

1. Choose the suite that owns the changed responsibility.
2. Name the test after observable behavior.
3. Do not depend on physical hardware; use fake instruments or local doubles.
4. Run Debug and Release.
5. Confirm that the case appears in Test Explorer, XML, and HTML.
