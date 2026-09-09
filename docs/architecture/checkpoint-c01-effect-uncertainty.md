# C-01 - Structured external-effect uncertainty

## Decision and scope

C-01 closes the explicit live-driver lost-acknowledgement propagation gap left
after D4.2. A native or Python driver can report a possible external write without
confirmed completion while its process is still alive. Engine owns the policy;
neither PythonRuntime nor the command author owns a global execution flag.

No TCP tutorial, real hardware, .NET host, Studio integration, generic transaction
manager, rollback, automatic reconciliation or additional planning checkpoint is
implemented here. Existing D4.2 acceptance remains historical evidence.

## Contracts

| Surface | Candidate version | Compatibility decision |
| --- | --- | --- |
| C++ SDK | 0.4.0 | Result::Indeterminate and IsIndeterminate |
| Native extension ABI | 0.2 | New status flag; unchanged struct layouts/function tables |
| Engine host API | 0.4 | Unchanged |
| Python SDK | 0.2.0 | Result.indeterminate and effect_indeterminate |
| Private process protocol | 0.2 | Response bool field 5; exact version match required |
| Native manifests | 1 / 2 | Existing schema; generated required ABI is 0.2 |
| Managed manifest | 3 | Existing schema; required protocol is 0.2 |
| Run results | 1 default / 2 opt-in | Existing shapes; v2 already carries outcome.indeterminate |

ABI 0.2 encodes ARTEST_STATUS_EFFECT_INDETERMINATE_FLAG (65536) alongside
a recognized non-OK base ARTestStatus. It is not a new status ordinal and not a
measurement verdict. Even when the caller's diagnostic buffer is absent or too
small, the base cause and flag survive; required_size still describes missing
text. SDK validation disallows success plus uncertainty. Wire validation also
rejects OK plus effect_indeterminate.

New SDK adapters reject a Query/create host version below 0.2 rather than losing
the signal. The loader queries each package's declared ABI minor, checks the
returned version and supports older 0.1 packages. A service callback strips the
new bit for an older caller only after Engine has latched it. Thus an old command
can receive a familiar error without regaining the ability to replay the effect.
Old Engines cannot load packages requiring ABI 0.2. No ABI 1.0 freeze is claimed.

Python 0.1 workers/packages are deliberately rejected. The protocol uses exact
version matching so old receivers cannot silently ignore the new safety field.
Rebuild packages and prepare new environments; do not mutate accepted D4.2 ones.

## Propagation and ownership

1. Result marshals the uncertainty flag independently of the base failure cause.
2. ExtensionRuntime::Invoke creates a stack-owned InvocationEffects scope.
3. InvokeAbi records the first flagged result from any nested broker call,
   preserving its diagnostic and operation ID.
4. Native and Python calls use the same scope, including mixed nested services.
   Further broker invocation is blocked once the latch is set.
5. Root Invoke publishes the latch through InvocationOutput even if the command
   throws, wraps the error or reports success.
6. ComponentAdapters maps it into Core's StepResult. Existing sequencer policy
   suppresses retry/continue and records attempt/step outcomes.
7. Unconditional shutdown uses a separate scope; the next session starts clean.

The scope follows the existing owner-thread/reentrant invocation model; it is
not thread-local global state, a device-global flag or a Python process member.
Do not add concurrent root calls without revisiting this ownership model.
Core and the public SDK do not depend on the private Python/process implementation.

WorkerSupervisor preserves response payload and uncertainty when cancellation
or timeout overrides its base status. Python Context.blocking preserves an
explicit vendor uncertainty result before running the post-I/O checkpoint.
Process-crash uncertainty and unconfirmed cleanup behavior from D4.2 are retained.

Regression exposed a result/cancellation-ACK crossing race. The supervisor now
consumes both the terminal response and the requested cancellation ACK before
leaving that call. The Python worker retains at most 64 completed-call tombstones
and acknowledges a cancellation that crossed a terminal response, without
reopening work. Unknown and duplicate cancellations still fault the protocol.
Tests force result-before-ACK ordering and verify that the next call/cleanup
remains usable; installed-worker tests also verify bounded history.

## Verification and reproduction

Run full builds sequentially: both configurations share the dependency restore
directory and concurrent full builds can fail its exclusive filesystem lock.

    .\scripts\build.ps1 -Configuration Debug -Platform x64
    .\scripts\build.ps1 -Configuration Release -Platform x64
    .\scripts\test-python-runtime.ps1 -Configuration Debug -PythonRoot .\artifacts\python-c01-final
    .\scripts\test-python-runtime.ps1 -Configuration Release -PythonRoot .\artifacts\python-c01-final
    .\scripts\test-native-compatibility.ps1 -BaselineDirectory .\artifacts\compatibility\baselines\sdk-0.2.1-native-v1-x64-Release -Configuration Debug
    .\scripts\test-native-compatibility.ps1 -BaselineDirectory .\artifacts\compatibility\baselines\sdk-0.2.1-native-v1-x64-Release -Configuration Release

Python preparation and its standalone SDK suite are described in
../sdk/python-extension-authoring.md. The full build includes ABI layout,
architecture boundaries, installed SDK/template/example and report consistency
gates. Native-only execution skips optional Python tests and is never counted
as Python acceptance.

C01FaultExtension is a test-only DLL, not a reference catalog package. Its
file-backed simulated effects distinguish call count, physical-effect stand-in
and cleanup count. PythonFaults provides equivalent live-worker scenarios.
Tests cover lost ACKs, wrapper errors/exceptions, swallowed success, attempted
replay, mixed/native/Python nesting, late completion, explicit timeout/cancel,
confirmed pre-send retry, failed cleanup, separate instances and fresh sessions.
These tests demonstrate policy and propagation, not actual equipment safety.

The final candidate record is artifacts/acceptance/c01/candidate.json. It captures
the base commit, dirty source inventory/hashes, Engine hashes and report hashes.
It is generated validation evidence, not a new committed source artifact.
Do not treat a previous failed/intermediate log as final candidate acceptance.

## Candidate validation results

Validated from base commit b8d98860018356cc4fff61d557a1dcb4f631a4cf with
uncommitted C-01 changes. No commit or push was performed.

| Gate | Debug x64 | Release x64 |
| --- | --- | --- |
| Native Google Test | 226 passed | 226 passed |
| Explicit Python integration | 27 passed | 27 passed |
| Installed Python worker contract | 4 passed | 4 passed |
| ABI layout, architecture and report consistency | Passed | Passed |
| Installed SDK/template/generated example | Passed | Passed |
| Frozen SDK 0.2.1 Release consumer | 8 passed | 8 passed |

The native report lists 253 cases, of which 27 optional Python cases are
disabled in that run. They are accepted only through the separate explicit
Python reports. The standalone Python SDK suite passed 10 tests. Two Release
integration cases (lost ACK and cooperative cancellation) also passed ten
iterations each: 20 extra executions, not 20 additional unique test cases.

Final logs are artifacts/c01-final-Debug.log, c01-final-Release.log,
c01-python-Debug.log, c01-python-Release.log, c01-python-sdk.log and
c01-python-repeat.log. Final frozen-consumer evidence is under
artifacts/acceptance/c01/compatibility-final-Debug and compatibility-final-Release.
The harness verified the frozen baseline inventory without rebuilding it.

Intermediate failures remain distinguishable from final acceptance:

- cancellation-race-before-fix preserves the failure that led to the ACK fix;
- concurrent-python-attempt preserves a first-call harness wait that exceeded
  30 seconds during overlapping validation. It did not recur in the final
  sequential suites or the repeated checks; its specific timing cause was not
  established. Do not infer performance guarantees from these functional tests.

D4.2 logs, frozen baselines, older SDK/environment directories and existing
manual evidence were retained. Standard test-results paths contain regenerated
current reports; the historical D4.2 counts are not current acceptance evidence.

## Remaining limits

Trusted native DLLs can bypass the broker, crash the process, or perform hidden
vendor retries. No generic Engine mechanism can infer what such I/O did.
Driver authors must classify uncertain writes and stop local retry loops.
Cancellation remains cooperative in native code; terminating a Python process
never proves safe physical cleanup. Real-device reconciliation and the remaining
planning findings are separate work, not silently closed by C-01.
