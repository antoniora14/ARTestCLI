# Python/.NET authoring, prerequisites and performance

Status: D4 authoring design; the D4.1 process foundation is implemented, but this
is not an available language SDK/API tutorial. No Python/.NET package is
published or installed by this document. Production C++ authoring is unchanged.
See [the D4 architecture](../architecture/stage-d4-managed-execution.md).
See [D4.1 implementation and installation prerequisites](../architecture/stage-d4-1-process-foundation.md)
for the private C++ dependencies and the Python 3.13 x64 preparation checklist.

## Developer experience target

Authors implement typed command operations and driver initialize/operation/shutdown
methods. The SDK supplies registration, status conversion, cancellation context,
scoped instrument services, structured logs and metadata generation.

- Python: package artest-sdk, import artest_sdk; type hints, explicit registration
  or decorators, dataclass-style parameters and concise async operations.
- C#: proposed ARTestSdk NuGet package and ARTest.Sdk namespace; typed interfaces,
  CancellationToken and source-generated metadata.
- ARTestPythonHost and ARTestDotNetHost are framework runtime roles, not files
  authors reimplement. The Python host can initially be an installed module
  launched by the selected interpreter, not necessarily a compiled .exe.

These names/APIs remain proposals. Do not publish a second hand-maintained schema
beside a dataclass/attribute definition. The SDK must enforce the same supported
schema profile as C++, including absent/default parameters, integer/boolean
distinction, numeric limits, unknown properties and response schema identity.

Device state belongs to a driver instance, never a module/class static variable.
DSO1 and DSO2 may use the same driver type with different configurations. The
step selects its instrument; a command resolves that bound capability through
the SDK, without branching on the driver language or manufacturer.

Async is the preferred host-facing model, not a requirement that all vendor SDKs
already be async. Offer a managed blocking-I/O adapter with serialized per-device
work and bounded vendor timeouts. Cancelling an await does not reliably stop an
arbitrary vendor call running on another thread. Cleanup cannot run concurrently
against that still-active device call; use the bounded failure/termination path.

## Candidate dependency baseline

These are selected initial targets, not a claim of validated compatibility.

| Role | Required for D4 implementation | Not required |
|---|---|---|
| Engine maintainer | Existing VS Insiders/MSVC v145, C++20, Windows SDK, PowerShell 7; pinned protobuf compiler/C++ runtime through managed build dependencies | Embedding CPython, CLR, gRPC server |
| Python extension author | Standard GIL-enabled CPython 3.13 x64, venv/pip, ARTest SDK/host wheels and pinned package dependencies | Full ARTest repository or Visual Studio for pure-Python authoring |
| Python host | Locked protobuf runtime; initial Windows pipe binding choice pywin32, hidden behind the SDK | Author-written Win32/IPC code |
| Python instrument package | Only its declared vendor/protocol libraries, e.g. VISA/serial/native SDK if that driver needs them | VISA, NumPy, pandas or every vendor SDK as universal dependencies |
| .NET extension author | .NET 10 SDK x64; proposed ARTestSdk package/source generator and dependency lock | Visual Studio specifically; dotnet CLI is sufficient |
| .NET runtime user | Initially .NET 10 runtime plus framework-dependent ARTestDotNetHost and package dependencies | .NET SDK/compiler on a test station |
| Native-only user | Existing native deployment prerequisites | Python and .NET merely because managed extensions are supported |

.NET 10 is an LTS target according to the
[official .NET support policy](https://dotnet.microsoft.com/en-us/platform/support/policy).
Exact dependency patches must be pinned and tested rather than copied from this
design. Older .NET Framework packages require a separate compatibility decision.

CPython 3.13 is an intentionally narrow first target; do not advertise an
unverified "3.x supported" range. Free-threaded Python and additional minors are
later matrix entries, especially where vendor wheels/native SDKs are involved.
The [Python threading documentation](https://docs.python.org/3.13/library/threading.html)
distinguishes standard GIL-enabled execution from optional free-threaded builds.

The old launcher on the inspected development machine enumerated only Python
3.7; this does not prove that no other interpreter exists elsewhere. Confirm an
explicit compatible interpreter path before environment creation. Read-only checks
also found .NET SDK 10.0.400 and Microsoft.NETCore.App 10.0.11. This is an
environment observation, not the cross-machine dependency lock. No installation
or machine configuration change was performed during design.

The first prerequisite checker must distinguish interpreter absent, unsupported
version/architecture, SDK/host missing, dependency lock mismatch and unavailable
vendor runtime. It must print actionable diagnostics without installing anything
during catalog discovery or test execution.

The proposed Windows pipe binding uses
[pywin32](https://github.com/mhammond/pywin32) inside the managed environment.
Its global post-install script is not part of this deployment and must not run
inside a virtual environment. Authors should not need to import it themselves.

Use venv without global site packages and invoke its interpreter by explicit
path; activation is optional. Recreate environments on the target machine from
the lock and approved wheel source. Python documents that environments are
isolated package sets but generally
[not portable directory copies](https://docs.python.org/3.13/library/venv.html).

Python wheels, NuGet lock files and approved offline dependency caches belong to
package preparation. Do not resolve or download dependencies in Initialize.
A Python package using a native vendor DLL may still need that vendor's runtime,
matching x64 libraries and the MSVC redistributable.

Pin protoc and the C++ protobuf runtime together: protobuf requires an exact
C++ generated-code/runtime match. Python and C# have their own compatibility
rules; one version string must not be assumed valid for every language package.
See [protobuf compatibility](https://protobuf.dev/support/cross-version-runtime-guarantee/).
Generated wire types remain framework-private, never part of a developer's
command/driver API.

## Explain performance accurately

The cost of an operation includes dispatch/serialization, process communication,
SDK/user execution, instrument I/O and result delivery. Interpreter startup/imports
are a separate cold-start cost. Workers are reused across steps in a session.

Pure-Python compute-heavy loops will generally cost more than equivalent
optimized C++ loops. That does not establish a fixed multiplier for an instrument
driver. During USB/LAN/serial/VISA waits, device or transport time may dominate;
native numerical libraries can also perform substantial Python-initiated work
outside the interpreter. Measure the actual workload.

In standard CPython the GIL limits simultaneous Python-bytecode execution in
threads. Async overlaps waits; it does not turn CPU-heavy code into parallel
compute. Python documents the distinction and the limitations of
[asyncio.to_thread](https://docs.python.org/3.13/library/asyncio-task.html).
Offloading a blocking operation does not by itself make it safely cancellable.

.NET is managed/JIT-based; do not classify it as having Python's interpreter cost.
It still incurs process communication and runtime warmup, with possible GC pauses.
No ARTest C++, Python or .NET mode on ordinary Windows promises hard real-time
latency. Timing-critical sampling/triggering belongs in instrument hardware or a
suitable acquisition subsystem, not a per-sample Python step loop.

Recommended guidance for authors:

- Use Python for automation, instrument I/O and fast iteration when measurements
  meet the sequence budget.
- Use C++ or native numerical kernels for hot compute paths; use batching to avoid
  one IPC round trip per data element.
- Use .NET where its libraries and tooling fit; measure the complete path.
- Keep high-rate acquisition local to the device/driver. Large binary waveform
  streaming is outside the first D4 control protocol, not silently JSON/base64 data.
- Reuse driver connections within a run and keep hardware out of imports,
  constructors and metadata definitions.

## Required benchmark evidence

D4 acceptance reports must separate cold launch/import, warm no-op dispatch,
command-to-driver round trips, simulated I/O, equivalent pure compute and bounded
control payload sizes. Include a same-algorithm Python-native-library variant
only if labeled separately; do not compare unequal work.

Test native/native, Python/Python, Python/native and native/Python paths first;
add .NET/.NET and Python/.NET parity when the .NET host exists. Record P50/P95/P99,
sample count, failures/timeouts, payload size, process topology, runtime/SDK
versions, CPU/OS and build configuration. Use repeated runs, explicit warmup and
a monotonic high-resolution timer. Publish raw samples with the summary.

Establish latency budgets from instrument/workflow requirements and measured
baseline results. Do not invent microsecond guarantees or a universal
"Python is N times slower" claim. Functional regression verdicts and benchmark
measurements are separate; noisy measurements must not hide correctness failures.

## Acceptance documentation

Each language slice must ship one minimal simulated driver/command tutorial,
generated-metadata packaging instructions, clean-machine prerequisites,
automated test commands, error/cancellation/cleanup guidance and an AI authoring
checklist. Document real supported behavior before adding broad examples.
Manual acceptance, when a slice requires it, uses the existing Word evidence
workflow; do not generate a manual report merely for this design-only change.
