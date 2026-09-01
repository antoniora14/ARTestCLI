# ADR 0001 - Use a C ABI for native extensions

- Status: Accepted for Stage D design
- Date: 2026-08-31
- Decision scope: Native Command Plugins and Instrument Drivers

## Context

ARTestEngine must load independently built native DLLs. Exposing C++ virtual
interfaces, STL types, exceptions, or ownership across the boundary would couple
extensions to a compiler, CRT, build configuration, and object layout.

## Decision

Native extensions use a versioned C ABI with one exported query symbol, opaque
handles, size-versioned function tables, fixed-width values, UTF-8 views,
schema-identified payloads, caller-owned error buffers, and callback result
sinks. A C++ SDK projects this ABI into convenient developer-facing classes.

ABI `0.x` remains experimental through the D1 vertical slice. ABI `1.0` is
frozen only after separately built command and driver packages pass the ABI
compatibility gate.

## Consequences

- Native extensions can be built independently from Engine internals.
- ABI ownership and error behavior are explicit.
- SDK wrappers require additional implementation and testing.
- The ABI cannot expose arbitrary C++ objects directly.
- Native memory corruption remains process-fatal unless a future process adapter
  isolates the driver.

## Rejected alternatives

- **C++ virtual interfaces across DLLs:** insufficient compiler/CRT stability.
- **COM as the only extension model:** strong Windows ABI but unnecessary
  complexity and poor symmetry with Python/.NET process endpoints.
- **Static linking of all extensions:** prevents independent deployment and
  runtime catalog discovery.
