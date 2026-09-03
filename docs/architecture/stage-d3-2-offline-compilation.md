# Stage D3.2 — Data-only compilation and session-owned native execution

Status: implementation and automated regression complete; manual acceptance pending.
This document supersedes the current-state descriptions in the Stage B/D1 documents.

## Boundaries

| Responsibility | Implementation |
|---|---|
| Script parsing | Core/Parsing/JsonTestPlanParser |
| Offline semantic compilation | Core/Compilation/TestPlanCompiler |
| Typed component metadata and schema profile | Core/Catalog |
| Engine handles and preparation snapshots | ARTestEngine/Api/EngineContext, EngineHandles |
| Host ABI dispatch, plan, catalog, session operations | ARTestEngine/Api; EngineApi.cpp only negotiates the table |
| Manifest discovery / schema binding / SHA-256 / reporting | Extensions/ExtensionCatalog, ComponentSchema, FileIntegrity, CatalogSnapshot |
| Native module lifetime / binary inspection | Extensions/NativeModule, NativeModuleLoader |
| Service leases / command and driver adapters | Extensions/NativeServiceBroker, NativeComponentAdapters |
| Native invocation / activation transaction | Extensions/NativeInvocation, NativeExtensionRuntime |
| Console UI | ARTestCLI; public SDK only |

Core is private C++20 code without Windows, SDK, DLL-loading, console, or concrete
hardware implementations. A compiled step owns only IDs, parameters, binding,
and execution policy. It does not own an ICommand or any hardware reference.

## Preparation, compilation, and execution

1. PrepareCatalog discovers an approved directory, validates manifests, reads
   parameter/configuration schemas, hashes entries, and constructs typed metadata.
   It calls no native entry point, factory, or validation function.
2. Compile resolves canonical type IDs and legacy aliases, validates parameters,
   configured instrument contracts and policies, and produces a data-only plan.
   A plan records its owning Engine and catalog revision.
3. Start takes the Engine's exclusive session lease and launches the worker.
   The worker checks the prepared fingerprint against package bytes/schemas,
   loads and inspects DLLs, atomically publishes factories, creates instrument
   instances, initializes them, and binds fresh commands.
4. Execution applies the existing cancellation, timeout, retry and failure policies.
5. Cleanup destroys commands/service leases before shutting down drivers in
   reverse initialization order. A failed driver initialization also attempts
   shutdown. The result preserves activation/initialization/cleanup diagnostics.

A completed session handle still owns its Engine lease. Destroy it before another
run; EngineClient.Restart retires the old result/session and reuses the compiled
plan. This is sequential reuse, not parallel instrument arbitration. The Engine
must outlive plans, sessions and subscriptions. Hosts must not destroy a session
or synchronously wait for itself from its worker callback.

Package activation is immutable after success. Before activation, another
successful preparation increments the revision and invalidates older plans.
A rejected preparation does not replace the last valid compilation metadata.
Its rejected report remains available for diagnostics. A catalog modified after
preparation must be prepared and compiled again; no stale plan is silently rebound.

The fingerprint covers canonical root, manifest text, actual entry SHA-256 and
schema documents. It detects ordinary compile-to-start changes; it is NOT an
adversarial filesystem race defense, a signature, publisher trust, or native
sandbox. In-process DLLs remain trusted executable code.

## Typed metadata and registration ownership

ExtensionDescriptor owns RuntimeDescriptor, IntegrityDescriptor and typed
ComponentDescriptor values. SchemaBinding holds its validated JSON document.
The original manifest is retained only for wire reports and extension creation.

RegistryTransaction stages both registry maps and owner maps before publishing.
A conflict or allocation failure before publication leaves both registries
unchanged. A private token identifies each batch; revocation cannot remove
unrelated registrations. Factories execute outside registry locks. Native loading,
callbacks and module retirement do not run under the catalog mutex.

Only the declared primary instrument contract can satisfy a configured requirement
in ABI 0.1. Capability tags are discovery metadata, not extra service interfaces.
Unknown runtime kinds, unsupported schema keywords, unknown flags, alias
collisions and manifest/binary identity, version, kind, flag or contract mismatches
are rejected.

## Intrinsics and deployable packages

| Previous name | Current owner |
|---|---|
| Time.WaitMs | Core intrinsic, scheduler-aware cooperative wait |
| IF | Reserved Core intrinsic; still explicitly rejected as not implemented |
| PowerSupply.TurnOn / PowerSupply.TurnOff | ARTestCmdHardware.dll |
| CAN.SendMessage | ARTestCmdHardware.dll |
| PowerSupply | Alias for com.artest.driver.sim.power-legacy in ARTestDrvSimPower.dll |
| CAN | Alias for com.artest.driver.sim.can in ARTestDrvSimCAN.dll |

The canonical simulated power driver remains com.artest.driver.sim.power for
existing extension examples. The legacy alias preserves the earlier hw-rsrc
initialization failure and retry-fixture behavior. All supplied drivers are
simulated; this stage does not introduce real instrument I/O.
Unit-test fakes now live only in tests/TestSupport/Fakes.

## Contract versions

- Engine host API: experimental 0.4, 176-byte x64 table.
- New append-only function: prepare_catalog (metadata only).
- API 0.1 / 0.2 / 0.3 prefixes remain 144 / 160 / 168 bytes with sentinel tests.
- Extension ABI: unchanged experimental 0.1.
- Manifest v2: aliases and required compilation schemas. Version 1 is still
  discoverable, but every component used by compilation must supply its
  parameter/configuration schema. It cannot fall back to executing a DLL validator.
- Script: ARTest.Script v1; catalog/run/compile JSON report schemas unchanged.

The formal manifest v2 JSON schema is an editor-facing structural projection.
CatalogValidation, ComponentSchema and SchemaValidator are the runtime authority;
filesystem, identity uniqueness and binary checks cannot be represented by a
document-only JSON schema. The historical manifest v0 design file also describes
future managed runtimes; it is not a statement of current runtime support.

## CLI and deployment

Normal compile/run/debug/break commands accept --extensions <directory>.
Without it, the Engine prepares artifacts/extensions/<platform>/<configuration>,
resolved relative to the Engine DLL, not the current working directory.
The catalog is prepared during Engine creation but DLLs activate only at Start.
An absent default directory leaves intrinsics available; invalid existing packages
fail closed. Deploy the corresponding extension packages with the executable.

extension-run remains a compatibility alias using the same execution path and
retaining its final JSON console output. It does not automatically write a file.
extensions validate remains metadata-only; extensions doctor intentionally loads
trusted DLLs. Informational lifecycle messages go to stdout, warnings/errors to
stderr.

## Verification and remaining work

The official Debug and Release build scripts run architecture gates, the C/C++
layout executable, Google Test, and report-verdict consistency checks. D3.2 adds
tests for un-loadable binaries during offline compile, schema errors, contract
mismatch, alias identity, stale snapshots, fresh runs, exclusive session ownership,
partial initialization cleanup, reentrant catalog reads, transaction isolation,
all CLI modes, and API 0.3 prefix compatibility.

See [the manual protocol](../../quality/manual-tests/stage-d3.2/README.md) for the manual acceptance
protocol and evidence report. No hardware is required.

D3.3 remains the distributable SDK and external-consumer validation gate before
an ABI 1.0 freeze. Managed Python/.NET process hosts, production hardware drivers,
native fault isolation, signature enforcement, hot reload, and parallel resource
arbitration are not implemented by D3.2.
