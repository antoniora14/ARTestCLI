# D4 - Python and .NET execution design

Status: target design; the [D4.1 foundation](stage-d4-1-process-foundation.md)
is implemented. Python/.NET execution is not yet shipped.
This document refines the earlier managed-runtime bridge strategy using the
post-D3.4 source tree. It supersedes its illustrative protocol versions,
handwritten-schema examples and unspecified process granularity.

## 1. Scope and current facts

The native SDK/compatibility work is the regression baseline, not a prerequisite
for an unlimited certification effort. D4 starts with a bounded managed vertical
slice. Native ABI 0.1 and Engine API 0.4 remain experimental and unchanged by this
design. Python and .NET extension execution is not currently implemented.

EngineContext and ComponentAdapters now consume IExtensionRuntime/ComponentLease;
the native backend remains the composition default. NativeServiceBroker retains
native component types. ExtensionCatalog still rejects non-native runtimes;
RuntimeDescriptor represents native ABI fields. D4.1 adds a separate draft
managed requirements model. The production coordinator/managed adapter and
cross-runtime broker are D4.2 work, not implied by the extracted port.

Preserve the parser, data-only compiler, execution policy, result model, catalog
IDs, primary service contracts and public host boundary. ARTestCLI remains thin;
ARTestStudio will use the same Engine and catalog, not launch language runtimes.

## 2. Target responsibilities

| Boundary | Responsibility |
|---|---|
| ExtensionRuntimeCoordinator | Catalog preparation, adapter selection, activation transaction and session endpoint leases |
| ComponentEndpoint / ComponentLease | Internal language-neutral create/invoke/shutdown/destroy operations and ownership |
| NativeRuntimeAdapter | Existing native ABI, module and callback translation; no managed-runtime dependencies |
| ProcessRuntimeAdapter | Remote component proxy and mapping to canonical operation results |
| SessionServiceBroker | Contract/instance routing and scoped leases across all endpoint kinds |
| WorkerSupervisor | Launch, handshake, process lifetime, disconnect handling and bounded termination |
| LocalTransport / ProtocolCodec | Framing, limits, correlation and wire encoding, with no sequencing policy |
| ARTestPythonHost / ARTestDotNetHost | Runtime loading, component instances and language exception conversion |
| Language SDK | Friendly typed authoring and generated metadata; no wire/handle plumbing exposed to authors |

These are responsibility names, not a requirement to create a project or interface
for each noun. Introduce interfaces only at actual runtime/transport seams.
Core keeps its existing ICommand/IInstrument ports. Endpoint/protocol types stay
inside Engine-side adapters, never in Core or the public native ABI.

Patterns: ports and adapters for runtimes, explicit factory registration for
runtime selection, RAII/session leases for ownership, and state machines for
workers/invocations. Do not add a service locator, global plugin singleton,
parallel scheduler or second implementation of retry/failure policy.

Native packages retain the current loader behavior and lifetime. Managed workers
are one process per package per Engine session, started lazily during activation
and reused across steps. Multiple configured instruments create distinct instances
inside that worker; they do not each require another interpreter. No process is
launched per step. Session restart creates fresh managed workers and device state.
Cross-session worker pools and mixed-package process sharing are deferred.

## 3. Local protocol

Use local Windows named pipes and length-prefixed Protocol Buffers, consistent
with the original architecture. Do not embed CPython/CLR or add gRPC/network
servers merely to obtain local RPC. Wire protocol starts at experimental 0.1,
independently of native ABI, SDK, manifest and Engine API versions.

D4.1 supplies the exact .proto field numbers and golden messages. See its
implementation note for the supported subset and explicit limits. Target semantics:

- Four-byte unsigned little-endian frame length; reject invalid/oversized frames
  before allocation. Start with a 1 MiB control-message ceiling and bounded queues;
  large waveform streaming is explicitly unsupported in this first slice.
- Envelope includes protocol version, worker generation, correlation/invocation
  identity and a typed message body. IDs are opaque integers, never pointers.
- Hello/ack negotiates supported versions/features, expected package identity,
  package fingerprint and generation before creating components.
- Describe, create, initialize, invoke, cancel, shutdown, destroy, events and
  bounded worker shutdown have explicit request/result semantics.
- ResolveService, InvokeService and ReleaseService are bidirectional operations:
  a Python command must be able to call a C++ or .NET driver through the Engine,
  not open a direct connection to another runtime.
- Payloads retain schema IDs and canonical JSON semantics inside the typed
  envelope. No pickle, BinaryFormatter, eval, object graphs or language exceptions
  cross the boundary. Reject non-finite numbers and unsupported schema values.
- Exactly one accepted terminal result per invocation. Reject stale generations,
  duplicate responses, unknown mandatory operations and invalid correlations.
  Forward-compatible protobuf fields alone do not guarantee semantic compatibility.
- Engine deadlines remain authoritative. Send a remaining time budget for local
  monotonic accounting, never copy a C++ steady_clock epoch into another language.
  Include nested service calls in the original budget; reject late completion.
- Cancel acknowledgement is not invocation completion. The control reader remains
  responsive while user work is active; queued logs cannot block cancellation.
- Diagnose log overflow and bound log size/queue length. Never discard terminal
  results or transport failures as if they were ordinary log messages.

The reader/dispatcher cannot wait synchronously for a command while also owning
the only path that receives its service response. Serialize device operations per
instance, not all protocol traffic under one worker-wide invocation lock.
Same-worker command-to-driver calls must work. Track call ancestry and reject
cyclic service calls instead of deadlocking. Preserve conservative native module
serialization until its existing callback/reentrancy semantics are verified.

## 4. Lifetime, failure and hardware safety

Worker states: Created -> Starting -> Handshaking -> Ready -> Stopping -> Exited.
Failure/disconnect transitions to Faulted and invalidates all generation leases.
Invocation states distinguish queued, running, cancel requested and terminal.

Engine owns retry and failure policy. Neither transport nor language host
automatically replays a timed-out/disconnected hardware call. After disconnect,
the device may have performed an operation whose acknowledgement was lost.
Report an indeterminate outcome in diagnostics and stop the run in the first
slice; do not reconnect and silently continue. Native policy behavior is retained.

Teardown stops new calls, requests cancellation, waits a bounded grace period,
destroys commands/releases services, attempts driver shutdown in reverse order,
and closes workers. Cleanup has a separate bounded budget: a cancelled command
must not automatically suppress the driver's opportunity to put hardware safe.

If a worker cannot cooperate, terminate its owned process tree after the bounded
grace period. Mark cleanup unconfirmed and invalidate all affected instances.
A killed process cannot execute finally/Dispose: **physical safe state is not
guaranteed**. Instrument interlocks/watchdogs and operator recovery remain device
responsibilities. Surface cleanup failure even after otherwise successful steps.

WorkerSupervisor owns process and pipe handles; no detached orphan workers.
Use Windows Job Objects where supported and verify containment before dispatch.
Missing containment fails managed activation clearly; do not kill unrelated
vendor services. Heartbeat may aid diagnosis, but deadlines/process exit are the
authority; a busy Python thread is not proof that an operation succeeded.

## 5. Package and dependency model

Keep native manifest v2 packages valid and unchanged. Propose manifest v3 for
managed runtime requirements, entrypoint, protocol and complete package inventory;
D4.1 specifies its draft typed model and schema rather than repurpose a DLL-only
hash as a whole Python package fingerprint. Full catalog activation, file checks
and canonical fingerprint computation follow in D4.2. Do not convert native files.

The author declares metadata once in Python/C# code. Python package-build tooling
and .NET source generation produce the manifest and schema profile used by native
components. Offline discovery/compilation reads these outputs without importing
Python, loading an assembly or starting a worker.

Python metadata lives in an explicitly declared, side-effect-free definition
module; vendor imports and hardware acquisition stay out of it. Build-time
execution is trusted code, not a sandbox. Runtime description must match the
generated manifest. C# generation must use source symbols, not instantiate drivers.

Package preparation is explicit and separate from execution:
1. Validate identity, containment and inventoried code/schema/dependency-lock files.
2. Select an approved runtime and verify its architecture/version.
3. Build a package-specific dependency environment from pinned, hashed inputs.
4. Record the resolved runtime, package and dependency fingerprints.
5. Activate only prepared packages; reject mismatches without invoking pip/NuGet.

The machine owns absolute interpreter/runtime paths, not a portable extension
manifest. No implicit PATH-based interpreter selection or global pip installs.
Virtual environments are recreated per machine, not copied as portable packages.
Dependency isolation is not a security sandbox; native wheels still execute code.

Named pipes are local-only with explicit current-user ACLs, unique names and
peer-process checks. Do not rely on default pipe permissions. Keep bootstrap
authentication material off command lines/logs. Only the approved child inherits
the necessary bootstrap handle. Launch without a shell; restrict inherited handles,
environment and working directory. Native/managed code is still trusted code:
same-user process isolation is crash containment, not protection from malicious
code. Signing, restricted tokens and remote execution remain out of scope.
Implementation must follow the Windows
[named-pipe access-control rules](https://learn.microsoft.com/en-us/windows/win32/ipc/named-pipe-security-and-access-rights).

## 6. Delivery plan and gates

| Slice | Deliverable | Exit gate |
|---|---|---|
| D4.0 - This design | Ownership, dependency policy, authoring/performance guidance and bounded roadmap | Review concrete decisions; no production behavior or installs |
| D4.1 - Shared foundation | Runtime-neutral seam, native adapter parity, draft manifest v3/.proto, named-pipe supervisor and test worker | Native regression/frozen consumer pass; malformed frames, crash, cancellation and nested service callbacks tested |
| D4.2 - Python first | Isolated host, typed SDK, automatic metadata, one simulated driver and command, explicit environment preparation | End-to-end Python and C++/Python calls, two instances, exceptions, timeout, blocking-I/O cancellation and cleanup evidence |
| D4.3 - .NET parity | .NET host/SDK/source generator and simulated driver/command | Same conformance suite plus Python/.NET cross-runtime service calls |
| D4.4 - Acceptance | Dependency doctor, offline deploy/restore, necessary tutorials and benchmark report | Reproducible external consumer, measured latency, crash/cleanup matrix and documented limitations |

D4.1 starts by refactoring the existing runtime seam with no Python/.NET dependency
required for native-only usage. Then add the bounded process protocol; do not
implement all languages and transports in one unreviewable change.

Use Google Test for Engine/supervisor contracts, language-level tests for each
SDK, and process integration tests for real host behavior. Preserve existing
native tests and frozen SDK 0.2.1 consumers. Language checks must fail if their
configured runtime is absent, never report a skipped language as supported.
Native-only builds must continue to work without Python/.NET installed.

D4 does not add real PicoScope hardware, waveform streaming/shared memory,
Python Engine-client bindings, .NET GUI integration, hot reload, remote agents,
arbitrary background tools, PyInstaller, NativeAOT, or ABI 1.0 certification.
A new tool execution model is not necessary to prove .NET driver support.

## 7. Decisions deferred to measured implementation

D4.1 pins and builds the C++ protoc/runtime graph through a vcpkg baseline.
Pin Python protobuf/pywin32/SDK dependencies in D4.2 after language verification,
before declaring a reproducible distribution.
Do not put "latest" dependencies into production lock files. Startup, grace,
queue and payload defaults need failure tests; the 1 MiB limit is the initial
control-plane design, not a promise to support oscilloscope waveform transfer.

See [managed authoring and performance](../sdk/managed-extension-design.md) for
candidate prerequisites, developer ergonomics, measurements and source references.
