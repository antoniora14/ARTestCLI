# Stage C - Robust execution

## Purpose

Stage C turns the sequential prototype into a controlled execution service
without moving console responsibilities back into `ARTestEngine.Core`. It adds
an explicit lifecycle, cooperative cancellation and timeout, retry and failure
policies, guaranteed cleanup, and structured reporting.

## Runtime boundary

`ExecutionSession` owns one asynchronous run. It coordinates initialization,
`TestExecutor`, and cleanup and is the only component that publishes the final
post-cleanup run result. ARTestCLI constructs the session, installs the Windows
Ctrl+C adapter, waits for completion, and maps the result to a process exit
code.

```text
ARTestCLI
  |-- ConsoleCancellationHandler
  |-- ConsoleEventSink
  `-- ExecutionSession (async owner)
        |-- ExecutionStateMachine
        |-- CancellationSource
        |-- InstrumentManager.InitializeAll
        |-- TestExecutor
        `-- InstrumentManager.ShutdownAll
```

The Core still has no dependency on console input/output or Windows APIs.

## State machine

The valid lifecycle is:

```text
IDLE -> INITIALIZING -> RUNNING -> CLEANING_UP -> COMPLETED
                    \-> CANCELLING -> CLEANING_UP -> CANCELLED
                                      CLEANING_UP -> FAILED
                                      CLEANING_UP -> TIMED_OUT
```

Initialization and execution failures also pass through `CLEANING_UP` before a
terminal state. Invalid transitions are rejected. A session can be started
only once.

## Step policy

Every step has a typed `StepExecutionPolicy`:

| Field | Default | Validation | Meaning |
|---|---:|---|---|
| `maxAttempts` | 1 | 1 through 100 | Total attempts, including the first |
| `retryDelayMs` | 0 | Non-negative | Cancellation-aware delay before retry |
| `timeoutMs` | 0 | Non-negative | Per-attempt deadline; zero disables it |
| `onFailure` | `stop` | `stop` or `continue` | Whether the next step may run |

The JSON object is optional, so existing `ARTest.Script` version 1 documents
retain their previous behavior. Policy is parsed into `StepDefinition`,
validated during compilation, and copied to `CompiledStep`.

## Cancellation and timeout contract

Cancellation is cooperative. `ICommand::Execute` receives a
`CancellationToken`; long-running commands and instrument adapters must observe
it and return promptly. `Time.WaitMs` uses a condition-variable wait so Ctrl+C
and timeout wake it immediately.

A timeout is also cooperative. After a command returns, the executor gives
precedence to an external cancellation request and then to an expired deadline.
The engine deliberately does not kill or detach arbitrary command threads:
forced termination could leave a hardware transaction or driver lock in an
unknown state. Future production adapters must define bounded I/O at the driver
layer as well as checking the token.

## Retry and failure semantics

- A failed, error, or timed-out attempt is retried until `maxAttempts` is
  exhausted.
- Cancellation is never retried.
- Retry delay observes the parent cancellation token.
- `onFailure: stop` ends the sequence and counts remaining steps as skipped.
- `onFailure: continue` runs later steps but preserves the worst run verdict.
- Attempt records keep their own result and duration; the step record contains
  the final result, total duration, and all attempts.

## Cleanup guarantee

`ExecutionSession` enters `CLEANING_UP` and calls `ShutdownAll` after every
initialization or execution outcome, including exceptions, cancellation, and
timeout. Initialized instruments are shut down in reverse initialization order.
Each shutdown is exception-contained so one bad adapter does not prevent later
cleanup attempts.

Cleanup diagnostics are appended to the final result. A cleanup failure changes
the overall status to `ERROR` and the failure kind to `Cleanup`, even when all
steps passed, because the equipment cannot be assumed safe.

## Reporting

`IEventSink` receives state transitions, step starts, attempt starts, scheduled
retries, step completions, diagnostics, and one final session completion event.
The final `RunResult` includes:

- every executed step and attempt;
- per-attempt and per-step duration;
- planned, executed, passed, failed, error, timeout, cancelled, and skipped
  counts;
- total attempt count and total session duration;
- initialization, execution, cleanup, or internal failure classification;
- structured diagnostics.

## Test strategy

Google Test covers transition validation, cancellation wake/deadline behavior,
retry success, stop and continue policies, timeout, asynchronous completion,
Ctrl+C-equivalent cancellation, initialization failure, and cleanup failure.
The manual Stage C report additionally validates the real CLI, exit codes, and
the Windows Ctrl+C handler in Debug and Release builds.

## Deferred work

- Persistent JSON/JUnit execution report files.
- Production driver I/O deadlines and hardware-specific recovery.
- Parallel branches and resource scheduling.
- Public DLL/plugin ABI and plugin isolation.
- Cross-process execution host for drivers that require hard termination.
