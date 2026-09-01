# Stage D - Extension platform architecture

## Status

Design baseline. No production ABI is frozen by this document. The first
implementation slice uses ABI `0.x` and may change before ABI `1.0` is declared
stable.

## Objective

Stage D turns ARTestEngine into a reusable extension platform shared by
ARTestCLI and ARTestStudio. It must support trusted native command packages and
instrument drivers now without making future Python and .NET extensions depend
on C++ implementation details.

The extension platform has four design goals:

1. Keep execution policy, state, cancellation, cleanup, and reporting inside
   ARTestEngine.
2. Make commands and instrument drivers independently deployable.
3. Keep the binary contract stable across compiler and runtime revisions.
4. Give native, Python, and .NET extensions the same logical component model,
   descriptors, schemas, diagnostics, and lifecycle.

## Terminology

| Term | Meaning |
|---|---|
| Extension package | Deployable directory containing one manifest and one runtime entry point |
| Extension module | Loaded native library or managed extension endpoint |
| Command component | Test-sequencing step implementation |
| Instrument driver | Hardware or simulated-instrument adapter |
| Tool component | Future non-step utility such as discovery, conversion, analysis, or calibration support |
| Component type | Stable registered type that can create component instances |
| Contract ID | Versioned capability implemented or consumed by a component |
| Runtime adapter | Engine-side adapter for native ABI or remote managed protocol |

The term **Instrument Driver** is used consistently. It is not called an
instrument plugin in public naming or documentation.

## Binary naming convention

Production filenames use the ARTest brand followed by a compact PascalCase
role and identity:

```text
ARTestEngine.dll
ARTestCLI.exe
ARTestStudio.exe

ARTestCmdPower.dll
ARTestCmdDiagnostics.dll
ARTestDrvKeysightN6705C.dll
ARTestDrvVectorCAN.dll
ARTestDrvSimPower.dll
```

Filenames are deployment labels, not identity keys. Renaming a file must not
change the stable extension ID, component type ID, or contract ID stored in its
manifest.

## Target architecture

```text
ARTestCLI                    ARTestStudio
    |                             |
    +----------+------------------+
               |
        ARTestEngine public facade
               |
     Extension Catalog and Registry
               |
     Canonical Component Endpoint
       |                       |
 NativeRuntimeAdapter     ProcessRuntimeAdapter
       |                       |
 Stable C ABI             Versioned local RPC
       |                 +-----+----------------+
 Trusted native DLL      |                      |
                         ARTestPythonHost       ARTestDotNetHost
                               |                      |
                         Python package          .NET assembly
```

ARTestEngine contains the canonical extension model. Runtime adapters translate
that model either to native C function tables or to a process protocol. Parser,
compiler, executor, and hosts never depend directly on a DLL class, Python
object, CLR type, vendor SDK, or transport library.

The host-facing `ARTestEngine.dll` boundary is also a versioned C ABI with a
first-party C++ client facade. It is specified separately in
[`stage-d-engine-api-v0.md`](stage-d-engine-api-v0.md). This prevents STL,
exceptions, and internal Engine classes from becoming part of the DLL contract
used by ARTestCLI or ARTestStudio.

## Dependency rules

1. ARTestCLI and ARTestStudio depend on ARTestEngine, never on an extension.
2. Command components consume instrument capabilities through host services,
   never by linking directly to an Instrument Driver DLL.
3. Instrument Drivers may link to vendor SDKs but never to ARTestCLI or
   ARTestStudio.
4. Native extensions include only the public ABI/SDK surface. They do not link
   to ARTestEngine internal C++ classes.
5. Python and .NET extensions communicate through runtime hosts. ARTestEngine
   does not embed CPython or CLR in its process.
6. Runtime adapters produce the same internal descriptors, invocation results,
   diagnostics, events, cancellation behavior, and cleanup obligations.

## Canonical component model

Every extension component is described using transport-neutral metadata:

- stable extension ID;
- stable component type ID;
- component kind: `command`, `instrumentDriver`, or future `tool`;
- component semantic version;
- versioned contract ID;
- display name and vendor;
- configuration, request, and result schemas;
- declared capabilities and required capabilities;
- concurrency and isolation flags.

Every runtime adapter implements the same logical operations:

```text
Discover -> Validate -> Load endpoint -> Enumerate component types
         -> Create instance -> Invoke operation -> Shutdown -> Destroy
```

The initial implementation supports `command` and `instrumentDriver`. `tool`
is reserved in the manifest and contract so that future utilities do not need a
new discovery system.

## Payload envelope

The extension boundary does not expose C++ objects. Requests and results use a
schema-identified byte envelope:

```text
schema ID + media type + byte length + byte data
```

JSON UTF-8 is the default for configuration, command parameters, diagnostics,
and low-volume control operations. Binary encodings can be selected for CAN
traffic, waveforms, images, or other high-volume data without changing the ABI.
The initial supported media types are:

- `application/json; charset=utf-8`;
- `application/octet-stream`;
- a future registered CBOR or Protobuf media type.

Schema IDs are stable identifiers rather than local file paths. The manifest
maps schema IDs to packaged schema files used by the compiler and ARTestStudio.

## Stable identifiers

Identifiers use lower-case reverse-domain notation controlled by the publisher:

```text
com.artest.command.power.turn-on
com.artest.driver.sim.power
com.keysight.driver.n6700
com.vector.driver.can

artest.contract.command.v1
artest.contract.instrument.power-supply.v1
artest.contract.instrument.can.v1
```

Display names and filenames may change. Stable IDs do not change after release.
Duplicate IDs are rejected atomically before any component instance is created.

## Manifest-first discovery

Each extension package contains `artest-extension.json`. ARTestEngine reads and
validates the manifest before loading executable code. The catalog can therefore
show metadata and reject incompatible packages without calling `LoadLibrary` or
starting a managed runtime.

Example package layouts:

```text
extensions/ARTestDrvKeysightN6705C/
  artest-extension.json
  ARTestDrvKeysightN6705C.dll
  schemas/
  vendor/

extensions/ExamplePythonTools/
  artest-extension.json
  python/
  requirements.lock

extensions/ExampleDotNetDiagnostics/
  artest-extension.json
  ExampleDotNetDiagnostics.dll
  ExampleDotNetDiagnostics.deps.json
```

The catalog validates manifest schema, IDs, semantic versions, runtime kind,
architecture, ABI/protocol compatibility, entry-point containment within the
package, and duplicate component registrations. Hash and signature policy is a
later hardening layer but has reserved manifest fields.

## Native runtime

Trusted native extensions are loaded in process through a C ABI. The ABI uses:

- one exported query symbol;
- fixed-width integer status and flag fields;
- explicit `struct_size`, ABI major, and ABI minor fields;
- UTF-8 string views with explicit lengths;
- opaque handles;
- function tables;
- schema-identified payloads;
- caller-owned error buffers and callback result sinks;
- no STL, RTTI, C++ exceptions, references, or C++ virtual classes;
- no allocation that must be freed by a different module.

The native ABI is specified in
[`stage-d-native-abi-v0.md`](stage-d-native-abi-v0.md). A C-compatible reference
header is stored under `docs/architecture/contracts` until D1 promotes it into
the production SDK.

## Managed runtime strategy

Python and .NET extensions use dedicated out-of-process runtime hosts. This is a
deliberate architectural boundary, not merely an implementation detail.

Directly embedding CPython or CLR into ARTestEngine would create runtime-version
conflicts, global interpreter state, difficult unload behavior, dependency
collisions, and process-wide failure risk. Process isolation gives ARTestEngine
a deterministic crash boundary and lets each extension use an appropriate
runtime and dependency set.

The managed path is specified in
[`stage-d-managed-runtime-bridges.md`](stage-d-managed-runtime-bridges.md).

## Ownership and lifecycle

ARTestEngine owns:

- catalog entries and registration state;
- execution sessions and cancellation sources;
- Instrument Driver instance routing;
- command and driver cleanup order;
- result records, diagnostics, and events;
- service handles returned to command components.

An extension owns only its module state and instances. It must destroy every
internal allocation when the host invokes its destroy function. Views passed
across the ABI are borrowed for the documented call duration.

The shutdown sequence is:

```text
Stop new invocations
-> request cancellation
-> wait for bounded cooperative completion
-> invoke component shutdown operations
-> release host service handles
-> destroy component instances
-> destroy extension instance
-> terminate runtime endpoint if applicable
-> unload native module only when no callbacks or handles remain
```

Native modules remain loaded until engine shutdown in the first production
version. Hot unload is deferred because vendor SDK threads and callbacks can
outlive an apparent component instance.

## Cancellation, timeout, and cleanup

The Stage C guarantees remain authoritative:

- ARTestEngine owns retry and failure policies.
- Cancellation and timeout are cooperative.
- Every invocation receives cancellation state and an optional monotonic
  deadline.
- An extension must return promptly after cancellation and must use bounded
  vendor I/O.
- Cleanup is attempted even after initialization, execution, transport, or
  extension failures.
- Cleanup failure overrides an otherwise successful run.

Managed runtime hosts additionally support process termination after a defined
grace period. Hard process termination is an isolation fallback, not a normal
cancellation mechanism, and is never available for an in-process native DLL.

## Threading and reentrancy

Component instances are serialized by default. A descriptor may explicitly
declare safe concurrent invocation in a future ABI minor revision. Host service
callbacks are callable only during an active invocation unless a contract says
otherwise.

Extensions must not call host services from `DllMain`, global constructors, or
global destructors. The exported query function must not start threads, open
hardware, or perform registration side effects.

## Error model

All boundary calls return a numeric `ARTestStatus`. Human-readable diagnostics
use UTF-8 error buffers or structured result payloads. Status categories
distinguish invalid arguments, incompatible contracts, unavailable resources,
timeouts, cancellation, extension exceptions, transport failure, and internal
failure.

No exception may cross the native boundary. The C++ SDK catches all exceptions
and converts them to `ARTestStatus`. Managed hosts convert Python exceptions and
.NET exceptions into the same canonical diagnostic model while retaining a
sanitized stack trace for logs.

## Trust and isolation

An in-process native extension executes with the permissions of ARTestEngine and
can corrupt the process. D1 therefore loads only explicitly configured trusted
packages from approved directories. Discovery never recursively scans arbitrary
user or system locations.

Future hardening includes signed packages, digest validation, publisher trust,
allow/deny policy, process-isolated native drivers, resource limits, and an
extension health history.

## ARTestStudio integration

ARTestStudio consumes the same catalog and schemas as ARTestCLI. It does not load
extension UI code. Parameter editors and configuration forms are generated from
schemas and optional declarative presentation hints. This keeps Studio stable
when a command or Instrument Driver is added and avoids loading third-party GUI
frameworks into the application.

## Stage D delivery slices

### D0 - Design baseline

- Architecture, ABI `0.x`, manifest schema, managed bridge strategy, risks, and
  acceptance criteria.
- No production loading behavior.

### D1 - Native vertical slice

- Produce `ARTestEngine.dll`.
- Implement the minimum host-facing Engine API and C++ client facade.
- Implement manifest catalog and trusted native loader.
- Load `ARTestCmdSample.dll` and `ARTestDrvSimPower.dll`.
- Execute one end-to-end command using a driver service.
- Validate version negotiation, cancellation, cleanup, diagnostics, and unload
  safety.

### D2 - SDK and ABI stabilization

- Add the C++ wrapper SDK and project templates.
- Test with a separately built extension package.
- Correct ABI issues discovered by D1.
- Freeze ABI `1.0` only after the vertical slice passes.

### D3 - Production catalog and migration

- Migrate built-in commands and simulated drivers.
- Add duplicate, compatibility, integrity, and failure containment tests.
- Make ARTestCLI a consumer of `ARTestEngine.dll` and the catalog.

### D4 - Managed proof of concept

- Implement the local process protocol.
- Load one Python command and one .NET tool or simulated driver.
- Prove equivalent descriptors, cancellation, errors, events, and cleanup.

### D5 - ARTestStudio and distribution

- Integrate catalog and schemas into ARTestStudio.
- Package SDKs, templates, documentation, compatibility tests, and deployment
  tooling.

## D0 acceptance criteria

- The native contract compiles as both C and C++ without Engine internals.
- ABI structures have explicit size and version fields.
- No C++ standard-library type or exception crosses the ABI.
- Ownership and lifetime are defined for every pointer and handle.
- Native and managed paths map to one canonical component model.
- Python and .NET do not require embedding their runtimes in ARTestEngine.
- The manifest can describe native, Python, and .NET packages without changing
  stable component identities.
- D1 can be implemented without redesigning parser, compiler, executor, or
  ARTestStudio integration boundaries.
