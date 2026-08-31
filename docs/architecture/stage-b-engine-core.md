# Stage B - ARTestEngine.Core extraction

## Outcome

Stage B separates the reusable sequencing engine from its command-line host.
`ARTestEngine.Core` is a C++20 static library with no dependency on console I/O,
process arguments, or interactive input. `ARTestCLI` is a thin composition root
that translates process concerns into engine calls.

## Dependency direction

```text
ARTestCLI
  -> ARTestEngine.Core
       -> command and instrument abstractions
       -> nlohmann JSON header

ARTestCLI.UnitTests
  -> ARTestEngine.Core
```

The Core never references the CLI. Tests link the same production library used
by the executable instead of recompiling production source files.

## Processing pipeline

```text
JSON file
  -> JsonTestPlanParser
  -> TestPlan / StepDefinition
  -> TestPlanCompiler
  -> CompiledStep collection
  -> TestExecutor
  -> RunResult + IEventSink events
```

Parsing handles JSON syntax, top-level format/version, and structural schema.
Compilation resolves command types and instrument bindings, configures commands,
checks semantic invariants, and fails atomically. Execution owns runtime context,
step order, cancellation decisions, exception boundaries, records, and events.

## Public models

- `TestPlan` is the complete parsed plan.
- `InstrumentDefinition` contains an instrument type, identifier, and adapter
  configuration.
- `StepDefinition` contains a stable step identifier, command name, optional
  instrument identifier, and parameters.
- `CompiledStep` owns a validated command instance ready for execution.
- `RunResult` and `StepExecutionRecord` expose runtime outcomes without parsing
  console text.

## Composition and registration

`CommandRegistry` and `InstrumentRegistry` are ordinary injectable instances.
There are no global factories and no static registration macros. The host calls
`RegisterBuiltInCommands` and `RegisterFakeInstruments` explicitly. Duplicate or
invalid registrations return typed diagnostics.

This is an intermediate boundary toward plugins: a later DLL adapter can register
factories at the composition root without changing the parser, compiler, or
executor. A stable binary ABI is not defined yet.

## Events and host isolation

The engine publishes structured `EngineEvent` values through `IEventSink`.
Instrument operations, step lifecycle, run completion, and runtime diagnostics
are observable without a console. `ConsoleEventSink` is implemented only in the
CLI host. Tests use `RecordingEventSink`.

Interactive debugging is also host-owned. `ConsoleExecutionControl` implements
`IExecutionControl`, while normal Core consumers can use
`RunToCompletionControl` or another policy.

## Fake instruments

`FakePowerSupply` and `FakeCanDevice` implement the same interfaces expected by
built-in commands. They validate lifecycle, retain observable state, record CAN
messages, and emit instrument events. No physical hardware is accessed.

These fakes preserve Stage A behavior while creating a seam for future production
drivers. Production adapters should live outside the Core and be selected by the
host or plugin layer.

## Deferred work

- Production hardware adapters.
- Cooperative cancellation while a command is running.
- Per-step and run timeouts.
- Progress reporting beyond lifecycle events.
- Parallel execution semantics.
- A versioned C ABI and DLL plugin SDK.
- Plugin discovery, compatibility checks, trust policy, and isolation.
