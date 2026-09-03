# ADR 0003 — Metadata-only plans and session-owned runtime objects

Status: accepted for D3.2.

Compilation previously constructed ICommand instances and native validation could
invoke extension code. That made validation depend on executable modules, blurred
plan/session ownership, and forced the Engine to compile again at session start.

Decision: compile immutable data against typed component metadata. Load binaries,
create components, initialize services and bind runtime commands only on the
session worker. Keep time/control-flow intrinsics in Core; move power and CAN
commands and simulated drivers to extension packages. Use a two-registry
owner-token transaction for activation. Retain module leases until every component
and service handle is destroyed.

Consequences: offline compilation can reject schema and binding errors without
loading a DLL, plans can be reused sequentially with fresh runtime instances,
and Python/.NET bridges can later consume the same plan data. Plans require
versioned metadata and preparation revisions. A bounded schema profile is explicit,
rather than pretending that arbitrary JSON Schema documents are understood.

ABI 0.1 supports one primary service contract per component and no cross-session
resource arbitration. Therefore one live session handle per Engine is enforced.
Independent Engines are separate service scopes, not proof of safe simultaneous
access to physical equipment. Managed runtimes and a stable distributable SDK
remain separate development gates.
