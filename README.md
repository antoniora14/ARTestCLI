# ARTestCLI

ARTestCLI is the command-line prototype of the ARTest test-sequencing engine.
It loads versioned JSON scripts, validates commands and instrument bindings,
initializes the required instruments, and executes each test step in sequence.

The project is still under active development. Stage B extracts the sequencing
engine into a reusable library that can later be consumed by ARTestStudio and
future command or instrument adapters.

## Current capabilities

- Visual Studio 18 Insiders with the `v145` toolset and x64 targets.
- C++20, `/W4`, standard conformance mode, and UTF-8 source compilation.
- Canonical, versioned JSON documents using `ARTest.Script` version `1`.
- Offline script compilation and validation without opening hardware resources.
- Explicit instrument initialization and shutdown during execution.
- Typed diagnostics and per-step/run results.
- Non-zero process exit codes for invalid input, initialization failures,
  execution failures, and unexpected exceptions.
- Built-in power supply, CAN, wait, and reserved conditional command types.
- Interactive step-by-step execution and command-index breakpoints.
- Google Test regression suite with XML and validated HTML reports.
- Reusable `ARTestEngine.Core` static library with no console dependency.
- Typed `TestPlan`, `StepDefinition`, and `CompiledStep` models.
- Separate JSON parser, semantic compiler, and runtime executor.
- Injectable command and instrument registries with explicit registration.
- Structured engine events through `IEventSink`.
- Fake power supply and CAN instruments for deterministic development and tests.

The current instrument implementations are deterministic fakes. Production
hardware drivers, asynchronous cancellation, timeouts, parallel execution, and
the final DLL plugin ABI remain later-stage work.

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
| `source/ARTestEngine.Core/` | Reusable parser, compiler, executor, models, commands, events, registries, and fake instruments |
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
```

| Command | Behavior |
|---|---|
| `help` | Displays CLI usage |
| `compile` | Parses and validates the complete script without initializing instruments |
| `run` | Initializes instruments and executes the complete sequence |
| `debug` | Pauses before every command and accepts next, continue, or quit |
| `break` | Pauses at the supplied zero-based command indexes |

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
      }
    }
  ]
}
```

The loader rejects malformed JSON, unsupported versions, files larger than
4 MiB, duplicate identifiers, unknown commands, missing instrument bindings,
and invalid command parameters. Validation is atomic: an invalid definition
prevents the entire sequence from running.

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

The current Stage B baseline contains 25 tests across seven suites. See
[TESTING.md](TESTING.md) for the regression procedure.

## Architecture and roadmap

Stage B separates parsing, semantic compilation, and execution behind a
console-independent `ARTestEngine.Core`. ARTestCLI is now a composition root and
host adapter instead of the engine itself. Static self-registration was removed
in favor of explicit, injectable registries.

See
[Stage B - ARTestEngine.Core](docs/architecture/stage-b-engine-core.md)
for the current architecture boundary, dependency rules, and deferred decisions.
