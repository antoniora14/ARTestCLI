# D3.3-A - C++ extension authoring SDK

Status: implemented; manual acceptance pending. Engine API 0.4 and extension ABI
0.1 remain unchanged. Debug and Release each pass 139 tests across 33 suites,
including 41 SDK tests, ABI checks, boundary enforcement and report consistency.

## Design goal

A community developer implements component behavior, not function tables.
The public entry point is <ARTest/Extension.h>. C++20 classes and JSON values
exist only within the extension module. The generated adapter exposes the
existing C ABI; no STL object, virtual interface or exception crosses it.

## Author-facing model

- Command: Execute(parameters, context), with optional side-effect-free Validate.
- InstrumentDriver: Initialize(configuration, context), Shutdown(context), and
  explicitly registered operations. Constructors never open hardware.
- Parameters: strict typed reads with named diagnostics, no silent coercion.
- Result: an explicit operation status, diagnostic and optional JSON object.
- Context: cancellation/deadline checkpoints, cooperative waits, logging and
  service calls. An instrument call uses the command's configured binding.
- Extension: explicit local registration of metadata and factories.
- ARTEST_EXPORT_EXTENSION: the only required macro, in one translation unit.
  It adapts a metadata-only definition function to ARTestExtension_Query.
  There is no global registration or static-constructor registration order.

## Responsibility boundaries

The adapter owns handles, result serialization, exception containment, operation
dispatch and lifecycle state. All component construction happens at runtime,
not during Query, metadata discovery or offline compilation. Driver shutdown
must remain callable after partial initialization and after cancellation.
The developer still owns vendor resource safety, bounded I/O, semantic validation,
and propagation of unsuccessful Result values.

Context and Parameters are borrowed for the current synchronous call. Never
retain them, use them from background threads, or retain a service callback.
Pure C++ test contexts allow behavioral tests without loading the Engine.

Result reports operation success/failure. It does not introduce a new measurement
PASS/FAIL contract. Engine 0.4 preserves the command message in its run result;
arbitrary result fields are not yet promoted to step measurements.

## Scope and non-goals

A adds the authoring API, adapter, an isolated compilable example and automated
contract/behavior tests. It does not migrate the existing reference packages
(B), publish a distributable SDK (C), freeze ABI 1.0, implement managed hosting,
or add real instrument I/O. JSON uses the existing nlohmann/json 3.12.0 dependency.
The source-tree build adapter is not a public distribution promise.

## Acceptance

- Ordinary component code contains no opaque handle casts or ABI function tables.
- A command and simulated driver execute through the real Engine unchanged.
- Strict parameters, result/error buffers, malformed input, cancellation,
  deadlines, service release, partial initialization and exceptions are tested.
- Query does not create components; duplicate metadata fails closed.
- Debug/Release regressions and public-header compilation pass.
- Guides describe both human and AI-agent extension workflows with executable
  examples. The existing four-package CLI catalog remains unchanged.
