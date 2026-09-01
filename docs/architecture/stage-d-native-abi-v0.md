# Stage D - Native extension ABI 0.x

## Status and compatibility

This is the experimental binary contract for the D1 vertical slice. ABI major
`0` is intentionally not stable. ABI `1.0` is frozen only after a command DLL,
an Instrument Driver DLL, ARTestEngine, and the standalone SDK have passed
cross-project compatibility tests.

The normative reference header is
[`contracts/ARTestExtensionAbiV0.h`](contracts/ARTestExtensionAbiV0.h). This
document defines the semantics that cannot be expressed by C declarations.

## Scope

The ABI connects ARTestEngine to trusted native extension modules. It does not
expose ARTestEngine's C++ object model. It covers:

- ABI negotiation;
- extension creation and destruction;
- component type discovery;
- command, Instrument Driver, and reserved tool component instances;
- service resolution between commands and drivers;
- schema-identified request and result payloads;
- host logging, cancellation, deadlines, and monotonic time;
- status and error propagation.

The ABI does not define vendor hardware APIs, GUI components, package download,
or managed-language hosting.

## Export surface

Every native extension exports exactly one required symbol:

```c
ARTestStatus ARTEST_ABI_CALL ARTestExtension_Query(
    uint32_t requested_abi_major,
    uint32_t requested_abi_minor,
    ARTestExtensionApiV0* extension_api,
    ARTestErrorBuffer* error);
```

The symbol uses C linkage. It must be exported without C++ name mangling and
must use the calling convention declared by `ARTEST_ABI_CALL`.

`ARTestExtension_Query` is side-effect free. It must not open hardware, start a
thread, register a component, call a host service, or depend on global
constructor order. It only validates the requested version and returns a static
function table.

## Version negotiation

ABI compatibility has two numbers:

- **major** changes when an existing layout or semantic contract breaks;
- **minor** appends optional fields or functions without changing existing
  fields.

Rules:

1. The host and extension major versions must match exactly.
2. Every ABI structure starts with `struct_size`.
3. The producer writes only fields contained in the consumer-provided size.
4. The consumer reads only fields contained in the returned size.
5. New function pointers are appended and may be null when unsupported.
6. Reserved fields must be zero.
7. ABI `0.x` may deliberately break these rules before ABI `1.0`; every such
   change requires rebuilding all D1 prototypes.

The extension manifest declares its required ABI range. Manifest validation
happens before `LoadLibrary`; query validation happens immediately after load.

## Data representation

### Integer and Boolean values

Boundary values use `<stdint.h>` fixed-width types. C enums and C++ `bool` are
not used in structures because their binary representation can vary. Status,
kind, encoding, severity, and flag values are fixed-width integer aliases with
named constants.

### Strings

`ARTestStringView` is UTF-8 data plus an explicit byte length. Data is not
required to be null terminated. Embedded null bytes are not valid in IDs, media
types, schema IDs, or human-readable messages.

### Payloads

`ARTestPayloadView` contains:

- a schema ID;
- a media type;
- immutable bytes and byte length.

The receiver must copy data it needs after the call returns. JSON payloads use
UTF-8. Binary payload interpretation is defined by the schema ID and media type.

### Handles

Extension, component, and service handles are opaque. A handle is valid only in
the process and only until its owning destroy or release operation. It must not
be serialized, compared across providers, dereferenced by the other side, or
reused after release.

### Structure packing

The reference header applies 8-byte structure packing and restores the previous
compiler setting after its declarations. Extensions must not copy ABI
declarations into locally packed structures or include the header inside a
different active packing region. D1 records and tests expected x64 structure
sizes as part of its ABI fingerprint.

## Memory ownership

The ABI follows a no-cross-module-free rule:

- each module destroys memory it allocates;
- input views are borrowed only for the duration of the call;
- descriptor views returned by an extension remain valid until the extension
  instance is destroyed;
- errors are written into a caller-owned buffer;
- invocation results are delivered through a caller-owned result sink callback;
- no function returns an owning raw string or buffer;
- no `new`, `delete`, `malloc`, `free`, STL allocator, COM allocator, or CRT
  object crosses the boundary.

`ARTestErrorBuffer.required_size` includes the null terminator when textual error
output is requested. If capacity is insufficient, the function returns
`ARTEST_STATUS_BUFFER_TOO_SMALL`, writes the required size, and must not truncate
the diagnostic into a misleading message.

## Pointer and callback validity

- Output pointers and required function-table pointers must be non-null.
- An error-buffer pointer may be null when the caller does not request text.
- An invocation request may be null when the operation schema declares no
  request payload.
- A result sink may be null when the caller discards output or the operation has
  no result payload.
- An invocation context is required for every component and service invocation.
- A cancellation callback may be null only when cancellation is unavailable;
  a zero deadline still means no deadline.
- Functions validate `struct_size` before accessing any field after it.
- On success, functions set `required_size` to zero when a non-null error buffer
  is supplied.

## Extension lifecycle

```text
Validate manifest
-> LoadLibrary
-> ARTestExtension_Query
-> create_extension
-> enumerate component descriptors
-> create zero or more component instances
-> invoke operations
-> shutdown component operations
-> destroy all component instances
-> destroy_extension
-> retain module until engine shutdown in ABI 0.x
```

`create_extension` receives the immutable host function table and the validated
manifest payload. The extension may retain the host table pointer only until
`destroy_extension` returns. ARTestEngine guarantees that the host table and its
context outlive the extension instance.

`destroy_extension` and `destroy_component` are unconditional resource-release
operations and return no status. Fallible hardware cleanup is performed through
the well-known shutdown operation before destruction. Destruction must still
release local memory even if shutdown failed.

## Component descriptors

Each module exposes one or more component types. A descriptor provides stable
metadata and is compared with the manifest:

- component kind;
- component flags;
- type ID;
- implemented contract ID;
- component semantic version;
- display name;
- configuration schema payload.

The host rejects the entire module when runtime descriptors contradict the
manifest, contain duplicate type IDs, contain unsupported kinds, or expose
invalid UTF-8/schema data.

Component kinds in ABI 0.x are:

| Value | Meaning |
|---:|---|
| 1 | Command component |
| 2 | Instrument Driver component |
| 3 | Reserved tool component |

Tool discovery is reserved for forward compatibility. ARTestEngine D1 rejects
tool instantiation until its lifecycle is implemented.

## Well-known operations

Generic component invocation is used so the logical model can also travel over
the future managed runtime protocol. Well-known lifecycle and command operation
IDs are:

```text
artest.lifecycle.initialize.v1
artest.lifecycle.shutdown.v1
artest.component.validate.v1
artest.command.execute.v1
```

Instrument operations belong to capability contracts, for example:

```text
artest.instrument.power-supply.v1/set-voltage
artest.instrument.power-supply.v1/turn-on
artest.instrument.power-supply.v1/turn-off
artest.instrument.can.v1/send
```

Operation request and result schemas are published by the owning contract. A
component must return `ARTEST_STATUS_OPERATION_NOT_SUPPORTED` for an unknown
operation rather than treating it as success.

## Host services

`ARTestHostApiV0` is immutable and remains valid for the extension lifetime. It
provides:

- structured log publication;
- monotonic clock access;
- service resolution by contract and configured instance ID;
- invocation of a resolved Instrument Driver or other service;
- service-handle release.

A command component uses these functions instead of linking to a driver DLL or
casting a service to a C++ interface. ARTestEngine therefore remains responsible
for routing, lifecycle validation, serialization, cancellation, and reporting.

Service resolution returns a borrowed host-managed capability behind an opaque
handle. Each successful resolve must be paired with `release_service` before the
component instance is destroyed.

## Invocation semantics

`invoke_component` and `invoke_service` are synchronous at ABI 0.x. ARTestEngine
runs them on execution workers, so the UI and CLI control paths remain
asynchronous. The synchronous ABI deliberately avoids allowing an extension to
retain callbacks or request views after return.

Each invocation receives `ARTestInvocationContextV0` containing:

- a unique invocation ID;
- an optional monotonic deadline;
- an opaque cancellation context;
- a cancellation-check callback.

The extension must check cancellation before expensive work, between bounded
I/O operations, and while waiting. A deadline value of zero means no deadline.
Timeout classification remains owned by ARTestEngine.

The result sink may be called at most once in ABI 0.x. A successful call that
has no result payload does not call the sink. The payload view is borrowed only
during the callback.

## Threading

- `ARTestExtension_Query` may be called concurrently for discovery validation.
- An extension instance is not invoked until `create_extension` returns.
- Component instances are serialized by the host in ABI 0.x.
- Different component instances may run concurrently.
- Host callbacks used by an invocation may be called only from the invoking
  thread before the extension call returns.
- Extensions must not retain the result sink or invocation context.
- Destruction never overlaps an invocation on the same instance.

Future minor versions may add explicit concurrency flags. The default remains
serialized for safety with vendor SDKs.

## Error and exception containment

`ARTestStatus` communicates the machine-readable category. The error buffer
communicates the immediate human-readable diagnostic. Rich command results and
measurements use the result payload.

The extension SDK must place `try/catch (...)` around every exported or
host-invoked C++ function. No C++ exception, structured exception, Python
exception, CLR exception, or vendor object may cross the ABI.

ARTestEngine treats access violations or heap corruption inside an in-process
native extension as process-fatal. ABI 0.x cannot safely recover from arbitrary
native memory corruption. High-risk native drivers can later use the same
process endpoint model planned for managed extensions.

## Status categories

The reference header reserves stable categories:

| Status | Meaning |
|---|---|
| `OK` | Operation completed successfully |
| `INVALID_ARGUMENT` | Invalid pointer, size, ID, or payload |
| `INCOMPATIBLE_ABI` | ABI major/minor cannot be negotiated |
| `BUFFER_TOO_SMALL` | Caller-provided output buffer is insufficient |
| `NOT_FOUND` | Component, service, or resource is unavailable |
| `ALREADY_EXISTS` | Duplicate registration or instance |
| `INVALID_STATE` | Lifecycle precondition is not satisfied |
| `OPERATION_NOT_SUPPORTED` | Contract or operation is not implemented |
| `CANCELLED` | Cooperative cancellation was observed |
| `TIMED_OUT` | The operation observed its deadline |
| `RESOURCE_UNAVAILABLE` | Hardware or dependent resource is unavailable |
| `EXTENSION_FAILURE` | Extension-contained implementation failure |
| `HOST_FAILURE` | Host callback or routing failure |
| `INTERNAL_FAILURE` | Unclassified contained failure |

Status values are never reused with a different meaning.

## SDK projection

Native developers should normally use a C++ SDK rather than implement the
function tables manually. The SDK will provide:

- RAII wrappers for service handles;
- UTF-8 and payload helpers;
- exception-to-status containment;
- typed JSON-schema adapters;
- command and Instrument Driver base classes;
- export macros that generate `ARTestExtension_Query`;
- ABI size/version initialization;
- test harnesses and package templates.

The SDK is convenience source code. The C ABI remains the compatibility
authority.

## ABI 1.0 freeze gate

ABI `1.0` is not declared until all of the following pass:

1. C and C++ header compilation with warnings-as-errors.
2. x64 Debug and Release host/extension combinations.
3. An extension built in a separate solution and output directory.
4. Command-to-driver service invocation without direct linkage.
5. Cancellation, timeout, retry, and cleanup regression.
6. Invalid size, invalid pointer, incompatible version, duplicate ID, missing
   function, and malformed payload tests.
7. Sanitized exception conversion at every callback.
8. Manifest/runtime descriptor consistency tests.
9. ARTestCLI end-to-end vertical slice.
10. Review of the same logical contract against a Python and .NET bridge mock.
