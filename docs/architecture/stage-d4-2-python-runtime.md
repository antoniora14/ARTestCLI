# D4.2 Python execution

Status: implemented and automatically validated on 2026-09-07.
Manual acceptance was confirmed by the tester on 2026-09-08; the submitted report
records PASS for MT01 through MT10. No ARTestStudio integration or .NET activation
is included in this slice.

## Boundaries and ownership

ARTestCLI remains a public Engine SDK consumer. ExtensionRuntime coordinates
catalog publication and delegates native ABI calls to NativeInvocation and
Python calls to PythonRuntime. PythonRuntime owns one WorkerSupervisor per
package per session. Core still owns sequencing, retry policy and verdicts.
Neither Python, protobuf nor Windows process handles cross into Core's interfaces.

The Python authoring API contains Command, Driver, Context, Result and dataclass
schema generation. artest_host owns wire dispatch and component lifecycle.
Driver instances have separate state and an asyncio lock. Concurrent service
dispatch permits a Python command to call another component in its own worker.
It does not permit simultaneous calls on the same driver instance.

Commands are destroyed before driver shutdown. ExecutionSession then invokes its
optional runtime-finalization callback on the same worker thread. Workers stop
before the run becomes terminal. A subsequent session creates fresh workers.
Native catalog/module activation remains immutable for an Engine instance.

## Package and environment contracts

Managed manifest v3 activates Python 3.13 x64 / outOfProcess / private wire 0.1.
Native v1/v2 packages retain their prior behavior. .NET v3 activation is rejected.

The build imports an explicit metadata-only definition function. It never calls
component factories. It generates input schemas, component declarations and a
complete SHA-256 package inventory. Runtime Describe must match those declarations.
Offline discovery and compilation neither import Python nor require its installation.

Preparation is an explicit developer/deployment command. It installs hashed,
binary-only dependencies in a package-specific venv and emits a machine-local
receipt bound to the package manifest, installed file inventory, SDK wheel and
selected interpreter/runtime DLLs. Native-only use has no Python dependency.
Runtime never invokes pip, venv creation or dependency resolution.

The Engine checks containment, reparse points, inventory completeness and file
hashes before launch. A Windows venv executable is a redirector; launching it
would break the supervisor's exact child-PID check. The host therefore launches
the pinned base interpreter directly with -I -B -S and an inventoried launcher.
Only that environment's site-packages and the required pywin32 directories are
added explicitly. Ambient PYTHONPATH, user sites and executable .pth hooks are
not loaded. Installed bytecode is inventoried too: -B suppresses writes, not reads.

The receipt is trusted, machine-local deployment configuration. Hashes detect
changes, not malicious publishers. Same-user code and native Python wheels remain
trusted. This is dependency/process isolation, not a security sandbox. The
standard library installation is trusted; the receipt is not a signed inventory
of the entire operating system or CPython installation.

Publication deliberately refuses changed existing packages/environments. Build
changed inputs into a new output root, validate them, then update the host mapping.
Identical inputs may be reused after inventory validation. Production installation,
offline wheelhouse restore and update policy belong to D4.4.

## Calls and faults

C++ and Python commands resolve the same primary instrument contract and configured
instance ID through the Engine service broker. Worker service tokens are scoped
to that worker; no native address travels on the wire. Callback execution occurs
outside broker/catalog locks, while component leases preserve ownership.

Python handlers are async. Context.sleep cooperates with cancellation.
Context.blocking executes synchronous vendor I/O in a thread but waits for its
actual completion before releasing the driver lock. It never reports a detached
thread as completed. A call that exceeds the supervisor grace terminates the
entire owned job. Vendor timeouts are still required.

Defaults are 5 seconds for startup/lifecycle, 5 minutes for an invocation without
a plan deadline, and 2 seconds of cancellation grace. The plan's remaining
deadline constrains nested calls. Cleanup has a separate budget and is attempted
after partial initialization and cancellation. Arbitrary native callbacks remain
cooperative; the supervisor cannot forcibly interrupt C++ code inside Engine.

A process failure latches an indeterminate outcome across native-to-Python service
calls. Core forbids retries and continuation even when maxAttempts is greater
than one and onFailure is continue. PYTHON_CLEANUP_UNCONFIRMED means physical
hardware cleanup is unknown, not successful. Never automatically replay such
work. A normal command exception remains an execution error; a cooperative
deadline/cancel remains timedOut/cancelled.

## Versioned measurement results

SDK 0.3.0 adds C++ Result::TestVerdict and Python Result.verdict. Both emit
artest.schema.command-result.v1. The envelope separates a successful technical
invocation from a passed/failed measurement and preserves data plus its schema ID.
A 4.2 V reading below a 4.8 V minimum is Failed, not Passed or transport Error.

Core retains each attempt's data, schema and indeterminate flag. Existing Engine
hosts default to the exact run-result.v1 JSON shape. Hosts that create Engine with
resultSchemaVersion: 2 receive run-result.v2 with outcome on every step/attempt.
The CLI selects v2. Native ABI 0.1 and Engine function-table API 0.4 are unchanged.
Schemas for both result versions ship with the SDK. Unknown result versions fail
creation instead of silently degrading the report.

## Planning findings before ARTestStudio

Source: ARTestStudio/docs/planning/pre-integration-architecture-review.md.

| Finding | D4.2 disposition | Remaining gate |
| --- | --- | --- |
| INT-01 subscription quiescence and session-close semantics | Deferred explicitly; Python transport does not fix public subscription teardown | Resolve and race-test before D5 runtime/UI integration |
| INT-02 technical outcome versus test verdict and payload loss | Addressed with shared verdict envelope, retained attempt data and opt-in result v2 | .NET parity in D4.3; UI presentation in D5 |
| INT-03 complete offline catalog descriptors | Existing compilation metadata reused; no new UI catalog API in this slice | Resolve before D5 schema-driven editors |
| INT-04 stable run/document/step diagnostic correlation | Wire correlation is private call identity, not a replacement for public run/document correlation | Public diagnostic contract before D5; UI mapping during D5 |
| INT-05 retry after indeterminate external effects | Addressed in Core policy and cross-runtime failure propagation | Reuse fault conformance cases for .NET |
| INT-06 Studio graph execution/persistence semantics | Out of scope | Define and implement in D5 |
| INT-07 long-lived host/resource stability | Fresh sequential Python sessions covered | D4.4 deployed-bundle soak and resource accounting |

These dispositions do not close all pre-integration findings. D4.3 is the next
language implementation; D4.4 remains the compatibility/deployment/performance
acceptance step before ARTestStudio work.

## Verification

Validated on Windows x64 with Visual Studio Insiders (v145, C++20) and
CPython 3.13.15. Manual acceptance was confirmed separately on 2026-09-08.
The submitted Word report preserves the tester's results and evidence unchanged.
Its Python path/version and generated case directory fields remain unfilled;
the working tree field records the branch, not an immutable revision.

| Gate | Debug | Release |
| --- | --- | --- |
| Native Google Test regression | 210 passed | 210 passed |
| Explicit Python integration suite | 17 passed | 17 passed |
| Frozen SDK 0.2.1 native consumer compatibility | 8 passed | 8 passed |

The Python SDK has 8 passing language-level tests. Full builds also passed ABI,
SDK boundary, installed SDK consumer/template and report-consistency gates.
XML/HTML evidence is under artifacts/test-results/x64/{Debug,Release} in
ARTestCLI.UnitTests and ARTestPython.Integration reports. The 17 disabled Python
cases in the native report are executed separately by the explicit Python gate;
they are not counted among the 210 native passes.

Run scripts/build.ps1 for Debug and Release, then the explicit Python gate in
scripts/test-python-runtime.ps1 for each configuration. Native-only Google Test
runs intentionally exclude the DISABLED_PythonIntegrationTests suite. The Python
gate enables it, requires prepared environments, and fails rather than skipping
missing prerequisites. It produces separate XML/HTML evidence.

Language tests: source/ARTest.Python/tests/test_sdk.py. They cover strict schemas,
metadata without construction, verdict separation and cooperative/blocking waits.
Process fault doubles live only in tests/TestSupport/PythonFaults and are never
published in the normal Python example catalog.

Preserve and validate the frozen SDK 0.2.1 native consumer without rebuilding it.
Manual cases and evidence are in the D4.2 Word report. Automated results do not
constitute completed manual evidence.
