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
- `StageDExtensionTests.cpp`: ABI negotiation, manifest validation, native
  loading, command-to-driver services, host wait semantics, cancellation,
  cleanup override, handle ownership, and unload safety.
- `StageDThinHostTests.cpp`: Engine API 0.1/0.2 compatibility, detailed
  compilation, controlled sessions, and source-level dependency enforcement.
- `CliThinHostTests.cpp`: compile, run, debug, break, validation, initialization
  failure, cancellation, legacy output, and process exit-code contracts.

The Stage D2 baseline contains 61 test cases across 18 suites.

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
5. Confirm that 61 tests from 18 suites are discovered.
6. Select **Run All Tests**.
7. Verify that all 61 tests finish with a `Passed` verdict.

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
