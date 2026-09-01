# ADR 0002 - Host Python and .NET extensions out of process

- Status: Accepted for forward-compatible Stage D design
- Date: 2026-08-31
- Decision scope: Python, .NET, and future managed runtimes

## Context

ARTest is expected to support developer-provided Python packages and .NET
assemblies for commands, Instrument Drivers, and future tools. Embedding CPython
or CLR into ARTestEngine would couple engine stability to runtime versions,
dependency resolution, global runtime state, background threads, and unreliable
unloading.

## Decision

Managed extensions run in dedicated local runtime-host processes. A canonical,
versioned component protocol maps to the same IDs, schemas, operations, statuses,
cancellation, diagnostics, and lifecycle used by the native extension adapter.
The intended transport is local Windows named pipes with length-prefixed
Protocol Buffer messages.

## Consequences

- Runtime crashes and dependency conflicts are isolated from ARTestEngine.
- Python and .NET can evolve independently from the native engine.
- Process startup, transport, heartbeat, and payload-copy overhead are added.
- Hard termination becomes available only as a last-resort isolation mechanism.
- Parser, compiler, executor, and ARTestStudio remain language-neutral.

## Rejected alternatives

- **Embed CPython:** GIL, native-wheel, interpreter, and unloading risks.
- **Load CLR in Engine:** runtime/assembly conflicts and incomplete unload
  guarantees.
- **Design separate Python and .NET plugin systems:** duplicates discovery,
  schemas, lifecycle, testing, and ARTestStudio integration.
