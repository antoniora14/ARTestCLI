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

The Stage B baseline contains 25 test cases across seven suites.

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
5. Confirm that 25 tests from seven suites are discovered.
6. Select **Run All Tests**.
7. Verify that all 25 tests finish with a `Passed` verdict.

## Reports

Each execution generates:

- `artifacts\test-results\<Platform>\<Configuration>\ARTestCLI.UnitTests.xml`
- `artifacts\test-results\<Platform>\<Configuration>\ARTestCLI.UnitTests.html`

The workflow first tests the report generator with synthetic `PASSED`, `FAILED`,
and `SKIPPED` cases. It then compares Google Test aggregate counters against
every individual case verdict. A contradiction stops the build.

Reports are local artifacts excluded from Git. Attach them as execution evidence;
do not commit them.

## Add a test

1. Choose the suite that owns the changed responsibility.
2. Name the test after observable behavior.
3. Do not depend on physical hardware; use fake instruments or local doubles.
4. Run Debug and Release.
5. Confirm that the case appears in Test Explorer, XML, and HTML.
