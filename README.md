# ARTestCLI

ARTestCLI is the command-line prototype of the ARTest test-sequencing engine.
It loads versioned JSON scripts, validates commands and instrument bindings,
initializes the required instruments, and executes each test step in sequence.

The project is still under active development. Stage D3.2 keeps ARTestCLI a true
thin host: every CLI command now uses the versioned C ABI exposed by
ARTestEngine.dll. Command Plugins consume Instrument Driver services without
linking to driver binaries, while ARTestEngine.Core remains a private static
implementation detail of the Engine DLL.

## Current capabilities

- Visual Studio 18 Insiders with the `v145` toolset and x64 targets.
- C++20, `/W4`, standard conformance mode, and UTF-8 source compilation.
- Canonical, versioned JSON documents using `ARTest.Script` version `1`.
- Offline script compilation and validation without opening hardware resources.
- Explicit instrument initialization and shutdown during execution.
- Typed diagnostics and per-step/run results.
- Non-zero process exit codes for invalid input, initialization failures,
  execution failures, and unexpected exceptions.
- Native power/CAN command packages, a Core wait intrinsic, and reserved IF.
- Interactive step-by-step execution and command-index breakpoints.
- Google Test regression suite with XML and validated HTML reports.
- Reusable `ARTestEngine.Core` static library with no console dependency.
- Typed `TestPlan`, `StepDefinition`, and `CompiledStep` models.
- Separate JSON parser, semantic compiler, and runtime executor.
- Injectable command and instrument registries with explicit registration.
- Structured engine events through `IEventSink`.
- Simulated driver DLLs for development; private test doubles for unit tests.
- Validated execution state machine with asynchronous session ownership.
- Cooperative Ctrl+C cancellation and per-step timeouts.
- Per-step retry, retry-delay, and stop/continue failure policies.
- Guaranteed reverse-order instrument cleanup with cleanup diagnostics.
- Attempt-level, step-level, and aggregate execution reports.
- Experimental ARTestEngine host C ABI and first-party C++ RAII facade.
- Manifest-first catalog restricted to an explicitly approved extension root.
- Native Command Plugin and Instrument Driver packages with opaque handles.
- Service routing by stable instance and contract IDs.
- ABI-safe cancellation, diagnostics, result sinks, cleanup, and module unload.
- ARTestEngine API 0.2 detailed compilation reports and host-controlled sessions.
- ARTestEngine API 0.3 side-effect-free catalog validation and report schema v2.
- Manifest schema, path-containment, duplicate-ID, and optional SHA-256 checks.
- Failure-contained catalog activation plus `extensions list|validate|doctor`.
- API 0.4 metadata-only preparation, typed schemas, and data-only compiled plans.
- Fresh per-session native instances and owner-token atomic factory registration.
- Unified compile/run/debug/break with an optional --extensions root.
- API 0.1, 0.2 and 0.3 table-size negotiation retained for compatibility tests.
- Thin-host enforcement in MSBuild and Google Test; ARTestCLI cannot reference Core.
- Legacy compile, run, debug, break, output, and exit-code contracts preserved.

The extension ABI remains experimental at version 0.1, and the Engine host API
is experimental at version 0.4. No 1.0 stability promise has been made. Production hardware
drivers, managed runtime hosts, parallel execution, and persistent report files
remain later-stage work.

## Requirements

- Windows x64.
- Visual Studio 18 Insiders with the Desktop development with C++ workload.
- Default installation path:

  ```text
  D:\Program Files\Microsoft Visual Studio\18\Insiders
  ```

- vcpkg integration available through Visual Studio/MSBuild. The repository
  manifest restores Google Test automatically.

## Repository layout

| Path | Purpose |
|---|---|
| `source/ARTestEngine.Core/` | Private parser, metadata compiler, executor, models, intrinsics, events, and registries |
| `source/ARTestEngine/` | Public Engine DLL, native catalog, loader, and runtime adapter |
| `source/ARTest.SDK/` | C ABI contracts and first-party C++ facade |
| `source/ARTestCmdSample/` | Reference native Command Plugin |
| `source/ARTestDrvSimPower/` | Reference simulated Instrument Driver |
| `source/ARTestCmdHardware/` | Power and CAN commands using driver services |
| `source/ARTestDrvSimCAN/` | Simulated CAN Instrument Driver |
| `tests/TestSupport/Fakes/` | Test-only C++ instrument doubles |
| `source/ARTestCLI/` | Thin command-line host and console adapters |
| `source/Scripts/` | Versioned sample test plans |
| `tests/` | Google Test project and characterization tests |
| `scripts/` | Reproducible build, report, and manual-test tooling |
| `docs/architecture/` | Architecture decisions and stage boundaries |
| `quality/manual-tests/` | Manual regression fixtures and local evidence templates |
| `artifacts/` | Local binaries and generated reports; excluded from Git |

The Visual Studio solution is located at:

```text
source\ARTestCLI.sln
```

## Build and test

Run the following commands from the repository root:

```powershell
.\scripts\build.ps1 -Configuration Debug -Platform x64
.\scripts\build.ps1 -Configuration Release -Platform x64
```

The default Visual Studio installation path can be overridden:

```powershell
.\scripts\build.ps1 `
    -Configuration Debug `
    -Platform x64 `
    -VisualStudioPath 'D:\CustomPath\Microsoft Visual Studio\18\Insiders'
```

To build without executing Google Test:

```powershell
.\scripts\build.ps1 -Configuration Debug -Platform x64 -SkipTests
```

`build.cmd` provides the same workflow and keeps its console window open so
the result can be reviewed.

Build outputs are written to:

```text
artifacts\bin\<Platform>\<Configuration>\
```

## Command-line usage

```powershell
$cli = '.\artifacts\bin\x64\Debug\ARTestCLI.exe'

& $cli help
& $cli compile '.\source\Scripts\TestScript.json'
& $cli run     '.\source\Scripts\TestScript.json'
& $cli debug   '.\source\Scripts\TestScript.json'
& $cli break   '.\source\Scripts\TestScript.json' 0 2
& $cli extension-run '.\source\Scripts\ExtensionScript.json' '.\artifacts\extensions\x64\Debug'
& $cli extensions validate '.\artifacts\extensions\x64\Debug'
& $cli extensions doctor   '.\artifacts\extensions\x64\Debug'
```

| Command | Behavior |
|---|---|
| `help` | Displays CLI usage |
| `compile` | Parses and validates the complete script without initializing instruments |
| `run` | Initializes instruments and executes the complete sequence |
| `debug` | Pauses before every command and accepts next, continue, or quit |
| `break` | Pauses at the supplied zero-based command indexes |
| `extension-run` | Loads an approved catalog and executes through ARTestEngine.dll |
| `extensions list` | Safely validates manifests and prints a package summary |
| `extensions validate` | Emits the catalog v2 report without loading extension code |
| `extensions doctor` | Runs validation, DLL/ABI inspection, and atomic activation |

Breakpoint arguments currently refer to positions in the `commands` array,
starting at zero. They do not refer to the script's `stepId` values.

## Script format

Every script must use the canonical root object:

```json
{
  "format": "ARTest.Script",
  "version": 1,
  "instruments": [
    {
      "type": "PowerSupply",
      "id": "PS1",
      "config": {
        "model": "Example supply",
        "hw-rsrc": "GPIB0::2::INSTR"
      }
    }
  ],
  "commands": [
    {
      "stepId": 1,
      "name": "PowerSupply.TurnOn",
      "instrument": "PS1",
      "params": {
        "channel": 1,
        "voltage": 12.0,
        "currentLimit": 3.0
      },
      "policy": {
        "maxAttempts": 3,
        "retryDelayMs": 250,
        "timeoutMs": 5000,
        "onFailure": "stop"
      }
    }
  ]
}
```

The loader rejects malformed JSON, unsupported versions, files larger than
4 MiB, duplicate identifiers, unknown commands, missing instrument bindings,
and invalid command parameters. Validation is atomic: an invalid definition
prevents the entire sequence from running.

The optional `policy` object is backward compatible with existing version 1
scripts. Its defaults are one attempt, no retry delay, no timeout, and stop on
failure. `maxAttempts` must be from 1 through 100; delay and timeout values are
non-negative milliseconds; `onFailure` is either `stop` or `continue`.

Cancellation and timeout are cooperative. Commands receive a cancellation
token and must observe it during long-running operations. The built-in wait
command wakes immediately on cancellation or timeout. The engine does not
detach command threads or forcibly terminate driver code because doing so could
leave physical instruments in an unsafe state.

## Exit codes

| Code | Meaning |
|---:|---|
| 0 | Operation completed successfully |
| 2 | Invalid command-line arguments |
| 3 | Invalid script or configuration |
| 4 | Instrument initialization failed |
| 5 | Sequence execution failed |
| 10 | Unexpected failure contained at the process boundary |

These codes are part of the CLI automation contract and can be consumed by
PowerShell, CI systems, or a future ARTestStudio process adapter.

## Automated test reports

Each build with tests enabled generates:

```text
artifacts\test-results\<Platform>\<Configuration>\ARTestCLI.UnitTests.xml
artifacts\test-results\<Platform>\<Configuration>\ARTestCLI.UnitTests.html
```

The HTML generator is tested with synthetic passed, failed, and skipped cases.
It also compares the aggregate Google Test counters with every individual test
case. A contradictory report causes the build workflow to fail.

The current Stage D3.2 baseline contains 98 tests across 25 suites. See
[TESTING.md](TESTING.md) for the regression procedure.

## Architecture and roadmap

Stage C adds robust session execution on top of the Stage B boundaries.
ARTestCLI remains a thin composition root and console adapter while
`ARTestEngine.Core` owns state, policy evaluation, cancellation, reporting, and
cleanup orchestration.

See
[Stage B - ARTestEngine.Core](docs/architecture/stage-b-engine-core.md)
for the current architecture boundary, dependency rules, and deferred decisions.
See also
[Stage C - Robust execution](docs/architecture/stage-c-robust-execution.md)
and
[Stage D3.1 - Production extension catalog](docs/architecture/stage-d3-production-catalog.md)
for the extension discovery, integrity, and activation baseline.
The current boundaries are documented in
[Stage D3.2 - Offline compilation and session ownership](docs/architecture/stage-d3-2-offline-compilation.md)
and [Schema Profile 1](docs/architecture/schema-profile-v1.md).

Stage D1 implements the first trusted-native vertical slice. The extension
platform uses a versioned C ABI for native DLLs and preserves isolated runtime
bridges for future Python and .NET packages. See
[Stage D - Extension platform](docs/architecture/stage-d-extension-platform.md),
[Engine host API 0.x](docs/architecture/stage-d-engine-api-v0.md),
[Native ABI 0.x](docs/architecture/stage-d-native-abi-v0.md), and
[Managed runtime bridges](docs/architecture/stage-d-managed-runtime-bridges.md).
The implemented slice and current limitations are recorded in
[Stage D1 - Native vertical slice](docs/architecture/stage-d1-native-vertical-slice.md).

Stage D2 removes the final direct dependency from ARTestCLI to
ARTestEngine.Core. The public API now supports structured offline compilation
and synchronous before-step control callbacks for debugger-style hosts. See
[Stage D2 - Thin-host migration](docs/architecture/stage-d2-thin-host-migration.md).
