# Stage A - Safe ARTestCLI baseline

## Objective

Stabilize the current prototype before extracting `ARTestEngine` as a reusable
library. This stage prevents later refactoring from depending on implicit
behavior or on a CLI that reports success after an actual failure.

## Adopted boundaries

1. **Document boundary.** `ScriptDocumentLoader` is solely responsible for
   reading the file and accepting `format = ARTest.Script`, `version = 1`.
2. **Offline construction.** Loading definitions and compiling commands does
   not initialize hardware. The `compile` command is safe for validation and CI.
3. **Explicit lifecycle.** `InstrumentFactory` creates, initializes, and shuts
   down instruments. After partial initialization, it releases resources that
   were already acquired.
4. **Typed results instead of implicit success.** Instruments, steps, and runs
   return typed results with diagnostics. The executor stops at the first
   failure and the CLI propagates a non-zero exit code.
5. **Atomic validation.** Duplicate instruments, duplicate steps, unresolved
   references, or unknown commands invalidate the complete document. A partial
   sequence is never executed.
6. **Exception boundary.** Configuration, initialization, and execution
   exceptions are converted into diagnostics or a controlled failure exit code.

## Minimum canonical format

```json
{
  "format": "ARTest.Script",
  "version": 1,
  "instruments": [],
  "commands": []
}
```

The loader limits files to 4 MiB, requires an object at the document root, and
requires arrays for `instruments` and `commands`. Future versions must be
introduced through explicit migration rather than silent tolerance.

## Deferred decisions

This stage does not yet convert the engine into a DLL or define the plugin ABI.
It also defers cancellation, timeout, parallel execution, complete control flow,
telemetry, and production instrument drivers. Those decisions follow ARTestCLI
characterization and definition of the shared contract with ARTestStudio.

## Exit criteria

- Debug and Release x64 compile without level-4 warnings.
- All 15 Google Test cases pass in both configurations.
- `compile` validates without initializing hardware.
- File, schema, binding, initialization, and execution failures produce a
  non-zero exit code.
- XML and HTML reports produce matching per-test and aggregate verdicts.
