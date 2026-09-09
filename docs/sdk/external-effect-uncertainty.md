# Unconfirmed device writes

Native SDK 0.4.0 and Python SDK 0.2.0 add an explicit driver result for an
external effect whose completion cannot be confirmed. This is not a measurement
verdict and is not inferred from exception text.

## Choose the correct result

| Device observation | Result | Engine policy |
| --- | --- | --- |
| Failure is confirmed before any write | Ordinary failure | Existing retry/continue policy applies |
| Response confirms a measurement outside limits | TestVerdict / verdict(false, ...) | Failed measurement, not uncertain I/O |
| Write completed or may have completed; acknowledgement missing | Indeterminate / indeterminate | Stop the run; no automatic retry or continuation |
| Cancellation/deadline plus an unconfirmed write | Indeterminate with Cancelled/TimedOut cause | Retain both facts; no automatic replay |

Use a vendor-specific error classification that actually distinguishes pre-send
failure from an unknown completion. If the API cannot prove that nothing was
sent, do not classify a potentially mutating operation as safely retryable.
Do not use this flag for every failed read or failed limit check.

## C++ driver

Inside the operation handler, after the transport determines that a mutating
request may have reached the device but its acknowledgement was lost:

    return Result::Indeterminate(
        "Output-enable request sent; device acknowledgement missing",
        Status::TimedOut);

Result::Code() remains TimedOut. Result::IsIndeterminate() returns true.
The default cause is ExtensionFailure. A success cause is rejected.
Before any send, a confirmed unavailable resource can instead return:

    return Result::Failure(Status::ResourceUnavailable, "Connection not opened; nothing sent");

Commands should propagate a failed service Result intact:

    const auto result = context.CallInstrument(
        "artest.contract.instrument.power-supply.v1",
        "artest.instrument.power-supply.v1/turn-on", {{"channel", 1}});
    if (!result) return result;
    return Result::Success();

Use the contract and operation IDs actually declared by your driver.
The Engine also latches uncertainty before returning a nested service result,
so wrapping the failure, throwing an exception, or returning success cannot
erase the original signal. Subsequent broker invocations in that root call are
blocked before reaching the driver. This is defense in depth, not permission
to ignore Result values.

## Python driver

An async operation can directly return:

    return Result.indeterminate(
        "Output-enable request sent; device acknowledgement missing",
        status="timedOut",
    )

Accepted uncertain causes are error, timedOut, cancelled and invalidArgument.
Use the cause appropriate to the observation; status="ok" is rejected.

For bounded synchronous vendor I/O, return that Result from the function passed
to await context.blocking(...). The SDK raises OperationError with the explicit
uncertain Result before its post-call cancellation checkpoint can replace it.
Let OperationError propagate. Do not detach the vendor operation or convert it
to an ordinary success/failure. Service failures likewise propagate through
OperationError; normal command authors do not need protocol code.

## Lifetime, cleanup and recovery

The latch belongs to one root invocation and all nested service calls, regardless
of language or driver instance. The sequencer records it on the step/attempt and
suppresses retries and continue-on-failure even when maxAttempts is greater than
one. Cleanup remains a separate invocation and is attempted; its failures remain
visible alongside the original effect diagnostic. A later session gets fresh
state, not stale uncertainty from the previous run.

Run-result v2 exposes outcome.indeterminate. Legacy v1 keeps its existing shape,
failure status and diagnostic, and enforces the same no-replay policy.
The diagnostic EXTENSION_OUTCOME_INDETERMINATE retains the originating operation's
message; outer command diagnostics are retained separately.

This is not exactly-once delivery, rollback or proof that the device is safe.
ARTest cannot intercept a DLL's direct vendor calls or retries hidden inside a
vendor library. Drivers must stop their own local write/retry loops immediately.
Real hardware recovery needs device-specific reconciliation, safe-state checks
and, where appropriate, operator approval. No automatic recovery is added here.

## Required versions

New native packages declare ABI 0.2 automatically and reject ABI 0.1 hosts.
The current Engine separately supports tested older ABI 0.1 packages. An older
command receives the ordinary base failure status from a new driver, while the
Engine retains the uncertainty latch and blocks replay independently.

Python packages require SDK/host/private protocol 0.2. Regenerate old 0.1 packages
and prepare new isolated environments; preserve old environments as historical
evidence. Do not edit manifest versions, wheel metadata or preparation receipts
to force compatibility. See python-extension-authoring.md for commands.
