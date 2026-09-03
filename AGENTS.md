# ARTest maintainer context

This file is the fast entry point for human and AI maintainers. Read the
stage-specific architecture documents before changing a public contract.

## Current architecture

- `ARTestCLI` is a thin host. It may include `ARTest.SDK`, but it must never
  include or link `ARTestEngine.Core`.
- `ARTestEngine.dll` owns parsing, compilation, execution, extension loading,
  registries, sessions, and results.
- `ARTestEngine.Core.lib` is a private implementation library, not a public SDK.
- `ARTest.SDK` contains the C ABI and the first-party C++ RAII facade.
- Native extensions depend only on the SDK ABI. Commands obtain Instrument
  Driver capabilities through host services, not direct driver links.

## Contract status

- Engine host API: experimental `0.3`. API additions are append-only.
- Native extension ABI: experimental `0.1`.
- Script document: `ARTest.Script` version `1`.
- Extension manifest: `schemaVersion` `1`.
- Catalog report: `artest.schema.extension-catalog.v2`.
- Do not claim ABI `1.0` stability until external consumer compatibility has
  been validated and a formal freeze decision is recorded.

## D3.1 catalog invariants

1. `ExtensionCatalog::Discover` is side-effect free and never calls extension
   code.
2. All manifests are validated before `LoadLibrary` is called.
3. Entry and schema paths must resolve inside their package directory.
4. Duplicate extension and component IDs reject the entire catalog.
5. A declared `integrity.sha256` must match the native entry.
6. Registry activation occurs only after every package and binary descriptor
   passes. The active in-process catalog is immutable for an Engine instance.
7. Failed activation must not prevent a corrected catalog from being loaded by
   the same Engine instance.

## Verification

Run both commands from the repository root before integration:

```powershell
.\scripts\build.ps1 -Configuration Debug -Platform x64
.\scripts\build.ps1 -Configuration Release -Platform x64
```

The build enforces `/W4`, runs ABI contract tests, the thin-host check, Google
Test, and XML/HTML report consistency. Generated `artifacts/` and completed
manual-test evidence documents are not source changes and must not be committed
unless the user explicitly requests them.

## Maintenance guidance

- Preserve existing diagnostics and exit codes when adding behavior.
- Never let C++ exceptions, STL objects, or cross-module allocation ownership
  cross a C ABI boundary.
- Add new Engine API functions only at the end of `ARTestEngineApiV0`, define
  the prior minor-size constant, and add a sentinel compatibility test.
- Keep comments focused on ownership, ABI, concurrency, security, and other
  non-obvious invariants; do not narrate ordinary syntax.
- Use fake instruments for automated tests. Physical hardware is reserved for
  explicitly scoped integration tests.
