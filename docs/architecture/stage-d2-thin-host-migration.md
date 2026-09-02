# Stage D2 - Thin-host migration

## Outcome

ARTestCLI is now a host of ARTestEngine rather than a second composition root
for ARTestEngine.Core. All production command paths cross the public Engine
boundary:

```text
ARTestCLI
   |
ARTest.SDK C++ facade
   |
ARTestEngine API 0.2
   |
ARTestEngine.dll
   |
ARTestEngine.Core.lib
```

ARTestEngine.Core remains a static library because it is the private,
compiler-local implementation of ARTestEngine.dll. Hosts neither include its
headers nor link its library.

## Migrated commands

| Command | Public Engine path |
|---|---|
| `compile` | `create_engine` -> `compile_plan_detailed` |
| `run` | detailed compile -> `start_session` -> wait -> result |
| `debug` | detailed compile -> `start_session_controlled` -> wait -> result |
| `break` | detailed compile -> `start_session_controlled` -> wait -> result |
| `extension-run` | catalog refresh -> compile -> start -> wait -> result |

Arguments, zero-based breakpoint interpretation, console presentation, Ctrl+C
behavior, and exit codes remain compatible with the Stage C/D1 CLI.

## Engine API 0.2 additions

API 0.2 appends two pointers to the API 0.1 table; it does not reorder or change
the first 144 bytes.

### Detailed compilation

`compile_plan_detailed` separates operation transport from plan validity. A
successful function call always produces `artest.schema.compile-result.v1`:

- `valid` indicates whether a plan handle was created;
- `summary` reports instrument-definition and step counts;
- `diagnostics` preserves every parser, binding, and command diagnostic;
- compilation never initializes hardware.

The CLI renders those diagnostics in its established text format. ARTestStudio
can consume the same JSON directly for an Error List or document annotations.

### Controlled sessions

`start_session_controlled` accepts `ARTestSessionOptionsV0`. Its synchronous
`before_step` callback receives:

- zero-based command index;
- stable script step ID;
- command component name.

It returns Continue or Cancel. The Engine owns scheduling and cleanup; the host
owns presentation and user interaction. A callback exception is contained by
the first-party C++ facade and converted to a host failure diagnostic.

## Compatibility rules

- Engine API major remains 0; no stability promise exists yet.
- A host requesting minor 1 supplies a 144-byte table.
- A host requesting minor 2 supplies the full 160-byte table.
- Query writes no bytes beyond the negotiated minor-version size.
- New minor functions are appended only.
- Opaque handle ownership and destroy order are unchanged.
- Extension ABI 0.1 is independent of Engine host API 0.2.

## Architectural enforcement

`scripts/verify-thin-host.ps1` runs before ARTestCLI compilation in Visual
Studio and command-line MSBuild. It rejects Core names in CLI source and project
files and verifies that the host references the SDK include directory and
ARTestEngine project.

Google Test independently verifies the source boundary and exercises:

- API 0.1 table compatibility;
- complete structured compilation diagnostics;
- before-step callback order;
- compile, run, debug, and break compatibility;
- invalid-script and initialization-failure exit codes;
- cancellation from the debug prompt;
- existing native extension and cleanup behavior.

## Deferred work

D2 does not claim SDK 1.0 stability. The next SDK-hardening iteration should
add distributable package layout, semantic capability discovery, API support
policy, generated examples, and consumer builds outside the repository. Native
process isolation and managed Python/.NET runtime bridges remain later Stage D
increments.
