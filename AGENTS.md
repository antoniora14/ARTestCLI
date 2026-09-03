# ARTest maintainer context

This file is the fast entry point for human and AI maintainers. Read the
stage-specific architecture documents before changing a public contract.

## Current architecture

- `ARTestCLI` is a thin host. It may include `ARTest.SDK`, but it must never
  include or link `ARTestEngine.Core`.
- `ARTestEngine.dll` owns parsing, compilation, execution, extension loading,
  registries, sessions, and results.
- `ARTestEngine.Core.lib` is a private implementation library, not a public SDK.
- `ARTest.SDK` contains the C ABI, the first-party C++ host facade, and the
  D3.3-A C++ extension authoring API.
- Native extensions depend only on the SDK ABI. Commands obtain Instrument
  Driver capabilities through host services, not direct driver links.

## Contract status

- Engine host API: experimental `0.4`. API additions are append-only.
- Native extension ABI: experimental `0.1`.
- Script document: `ARTest.Script` version `1`.
- Extension manifest: version `2` for new packages; version `1` discovery remains supported.
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

Read `docs/architecture/stage-d3-2-offline-compilation.md` and
`docs/architecture/schema-profile-v1.md` before modifying compilation or loading.
CompiledStep is data-only. Native loading, factories and component validation may
run only at activation/execution, never during offline compilation. Keep Wait
and reserved IF in Core; concrete commands/drivers belong to packages. Test
doubles belong only in tests/TestSupport/Fakes. Do not equate capability tags
with additional callable service contracts in ABI 0.1.

Engine API code lives in Api/ modules, and native loading, service brokerage,
invocation, schema binding and integrity each have dedicated Extensions/ modules.
Do not rebuild the former EngineApi.cpp or NativeExtensionRuntime.cpp monoliths.
One live session handle owns an Engine lease; commands must be destroyed before
drivers and the lease must be released last. Do not call extension code while
holding registry/catalog locks.

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

## D3.3-A extension authoring

Read `docs/architecture/stage-d3-3a-extension-authoring.md`,
`docs/sdk/extension-authoring.md`, and `docs/sdk/ai-extension-authoring.md`
before adding a command or Instrument Driver.

- Component authors use `ARTest/Extension.h`; ABI plumbing stays in SDK/detail.
  C++ objects and exceptions stay inside the module.
- Register components explicitly in a metadata-only definition function.
  Constructors and Query must never acquire hardware.
- Context and Parameters are call-scoped borrows. Propagate unsuccessful Result
  values; never turn cancellation, cleanup or service-release failures into success.
- Keep shutdown available after partial initialization and cancellation.
- The SDK example catalog is isolated in `artifacts/sdk-examples`. D3.3-B,
  not A, migrates the existing sample/power/CAN packages.
- Run the full Debug/Release regressions (139 tests) and the SDK boundary gate.
  `scripts/test-sdk-authoring.ps1` runs 41 focused tests without replacing reports.
- Distribution/templates and external consumer verification belong to D3.3-C.
  Do not claim a published SDK or frozen ABI 1.0.

## D3.3-C SDK distribution

Read `docs/architecture/stage-d3-3c-sdk-distribution.md` and
`docs/sdk/sdk-distribution.md` before changing package layout or SDK versions.

- `source/ARTest.SDK/sdk-version.json` is the version authority.
- Generated SDK directories and ZIPs belong under `artifacts/sdk-packages`
  and must not be committed.
- A package is acceptable only if its complete hash inventory passes and a
  copied template builds from the extracted ZIP without repository includes.
- Keep the starter free of Engine/Core linkage and handwritten ABI plumbing.
- D3.3-B migration is still pending; do not rewrite reference extensions as
  part of distribution maintenance.
- Do not claim ABI 1.0 or a public release. Licensing, signing, CMake and package
  feeds remain explicit release-readiness work.
