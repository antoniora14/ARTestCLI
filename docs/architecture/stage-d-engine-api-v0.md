# Stage D - ARTestEngine host API 0.x

## Status

Implemented public boundary between `ARTestEngine.dll` and its host
applications. It is separate from the extension ABI and has independent version
negotiation. Engine API `0.2` remains experimental through D2.

## Decision

ARTestCLI and ARTestStudio do not consume ARTestEngine internal C++ classes
across the DLL boundary. `ARTestEngine.dll` exposes a C ABI and the repository
provides a C++ client wrapper for first-party applications.

```text
ARTestCLI / ARTestStudio
        |
ARTestEngine C++ client facade
        |
Versioned ARTestEngine C ABI
        |
ARTestEngine.dll internal C++ implementation
```

This keeps DLL ownership stable and makes future .NET P/Invoke, Python native
bindings, test automation, and additional hosts possible without exposing STL,
exceptions, or compiler-specific object layouts.

## Contract separation

| Contract | Direction | Responsibility |
|---|---|---|
| Engine host API | Host -> ARTestEngine | Catalog, compilation, execution sessions, events, results |
| Extension ABI | ARTestEngine -> native extension | Component discovery, creation, service invocation, cleanup |
| Managed process protocol | ARTestEngine <-> runtime host | Python/.NET equivalent of the extension ABI |

Versions are negotiated independently. An Engine API revision does not force an
extension ABI revision unless extension semantics also change.

## Export model

`ARTestEngine.dll` exposes one query symbol returning a size-versioned function
table:

```c
ARTestStatus ARTEST_ABI_CALL ARTestEngine_QueryApi(
    uint32_t requested_api_major,
    uint32_t requested_api_minor,
    ARTestEngineApiV0* engine_api,
    ARTestErrorBuffer* error);
```

The host obtains all other operations from `ARTestEngineApiV0`. This allows
minor versions to append optional functions without expanding the exported
symbol surface.

The Engine API follows the same low-level rules as the extension ABI:

- explicit structure sizes and versions;
- fixed-width statuses and flags;
- UTF-8 strings with explicit lengths;
- opaque handles;
- borrowed input views;
- caller-owned output buffers and result sinks;
- no STL, C++ exceptions, virtual classes, or cross-module deallocation.

The two ABIs may share foundational POD definitions in a future
`ARTestAbiTypes.h`, but they do not share lifecycle handles or function tables.

## Host-visible handles

The initial host API uses distinct opaque handles:

| Handle | Owner | Purpose |
|---|---|---|
| `ARTestEngineHandle` | Engine | Configured engine instance and catalog |
| `ARTestCompiledPlanHandle` | Engine | Immutable validated plan |
| `ARTestSessionHandle` | Engine | One asynchronous execution session |
| `ARTestResultHandle` | Engine | Immutable completed run result |
| `ARTestSubscriptionHandle` | Engine | Event callback registration |

The engine handle must outlive every handle created from it. Each successful
create/compile/start/subscribe call has one matching destroy/release call.

## Minimum D1 function groups

### Engine lifecycle

```text
create_engine
destroy_engine
```

Creation receives engine configuration as a schema-identified JSON payload. It
does not initialize instruments.

### Extension catalog

```text
refresh_catalog
get_catalog_snapshot
```

Catalog refresh validates manifests and compatibility without creating command
or Instrument Driver instances. The snapshot is returned as a versioned JSON
payload suitable for CLI diagnostics and ARTestStudio.

### Compilation

```text
compile_plan
destroy_compiled_plan
```

Compilation consumes an `ARTest.Script` payload, resolves catalog component IDs,
validates schemas and bindings atomically, and returns either an immutable plan
handle or structured diagnostics. It never initializes hardware.

API 0.2 appends `compile_plan_detailed`. It returns a canonical
`artest.schema.compile-result.v1` payload for both valid and invalid plans. A
valid report includes a plan handle; an invalid report includes every available
diagnostic and a null handle. Transport or ABI failures remain status codes.

### Event subscription

```text
subscribe_events
unsubscribe_events
```

Events use schema-identified payloads and preserve Stage C state, step, attempt,
diagnostic, and completion semantics. Callbacks are serialized per session and
are never invoked while an internal Engine lock is held.

### Asynchronous execution

```text
start_session
cancel_session
wait_session
get_session_state
destroy_session
```

`start_session` returns after scheduling the worker. `cancel_session` only
signals cooperative cancellation. `wait_session` supports a host wait timeout,
which is distinct from a test-step timeout and does not cancel the run
automatically.

API 0.2 appends `start_session_controlled`. A size-versioned session-options
structure contains a synchronous `before_step` callback. The callback receives
only public POD data and returns Continue or Cancel. This supports CLI debug and
breakpoints without exposing `IExecutionControl` or any Core C++ type.

Destroying an active session first requests cancellation and waits according to
the documented shutdown policy. Hosts should explicitly cancel and wait so that
cleanup results remain observable.

### Results

```text
get_session_result
serialize_result
destroy_result
```

The result model is immutable and includes every Stage C step/attempt record,
summary, failure kind, diagnostic, and duration. JSON is the first serialization
format. JUnit or another report format is produced by adapters rather than
changing the binary result layout.

## Threading contract

- Different engine instances may be used concurrently.
- Catalog mutation and plan compilation are internally synchronized.
- Compiled plans are immutable and may be used to create separate sessions.
- A session owns one execution lifecycle.
- `cancel_session`, `wait_session`, and `get_session_state` are safe from host
  control threads.
- Event callbacks for one session are ordered and non-concurrent.
- The host must not destroy a subscription from inside its own callback; it may
  request deferred unsubscription.
- ARTestEngine never calls a host callback after unsubscribe/destroy returns.

## Callback safety

Event callbacks receive borrowed payload views valid only during the callback.
The host copies data it needs afterward. Callbacks must return quickly and must
not perform blocking UI work. ARTestStudio posts events to its UI dispatcher;
ARTestCLI formats them on its output adapter.

An exception from a first-party C++ callback is contained by the C++ client
wrapper and must never cross the C ABI.

## .NET and Python host bindings

The Engine host API makes future application-level bindings possible, but this
is distinct from extension hosting:

- A .NET application may call `ARTestEngine.dll` through P/Invoke and a managed
  client package.
- A Python automation client may use a CPython extension or `ctypes` wrapper.
- Python/.NET extensions still run out of process through their runtime hosts.

Providing a client binding does not embed the managed runtime into ARTestEngine.

## Failure behavior

- Invalid handles and sizes return `INVALID_ARGUMENT` or `INVALID_STATE`.
- Manifest and plan errors return structured diagnostics without partial state.
- Extension catalog failures do not crash the host and identify the package.
- Session execution failures remain available through immutable results.
- Cleanup failures remain visible even if a host stops consuming ordinary
  events.
- A fatal in-process native extension failure can still terminate the Engine
  process; future native process isolation addresses that risk.

## D1 design gate

Before implementing the Engine host table, D1 must define:

1. Exact foundational ABI type reuse between host and extension headers.
2. Function signatures and required/null function pointers.
3. Handle ownership tests.
4. Event callback ordering and unsubscribe tests.
5. Wait timeout versus execution timeout semantics.
6. Canonical JSON schemas for catalog, diagnostics, and run results.
7. C/C++ layout fingerprints and warnings-as-errors compilation.
8. A C++ facade used by ARTestCLI without exposing internal Engine classes.

## D2 implementation status

- `compile`, `run`, `debug`, `break`, and `extension-run` use EngineClient.
- ARTestCLI has no project, header, or linker dependency on ARTestEngine.Core.
- API 0.2 appends detailed compilation and controlled-session functions.
- Hosts compiled for API 0.1 may still provide a 144-byte table; query writes
  exactly that negotiated size and does not touch subsequent bytes.
- The C++ facade owns callback lifetime and contains host exceptions.
- MSBuild and Google Test enforce the thin-host dependency rule.
