# D4.1 - Runtime-neutral process foundation

Status: implemented foundation; Python/.NET execution is not yet enabled.
Native Engine API 0.4, extension ABI 0.1 and SDK 0.2.2 are unchanged.

## Scope and ownership

The Engine context and Core-facing component adapters consume the private
`IExtensionRuntime` port and `ComponentLease`. The native implementation
owns its module handles and performs ABI translation. The adapters no longer
retain `NativeComponentInstance` or include native module plumbing.

`ARTestEngine.Process.lib` is a private Engine-side library: codec, managed
requirement projection, local pipe channel, process launch and supervisor.
It is not a public SDK or a new sequencer. Core does not depend on Windows,
protobuf, workers or the native SDK; the boundary gate enforces this.
The CLI continues to use only the Engine's public host API.

D4.1 verifies a real C++ test subprocess hosted by the unit-test executable.
It does **not** add a production ProcessRuntimeAdapter, activate managed packages,
or implement Python/C# authoring. NativeServiceBroker remains the native backend.
D4.2 will introduce the coordinator/managed adapter and cross-runtime broker,
using this port and transport, with end-to-end language conformance tests.
Do not infer Python support from the C++ worker tests.

## Wire contract and limits

Authority: [artest_process.proto](../../source/ARTestEngine.Process/protocol/artest_process.proto).
Experimental wire 0.1 is independent of the native ABI. Only an exact 0.1
handshake is accepted; multi-version negotiation is deferred.

| Contract | Implemented rule |
|---|---|
| Frame | Four-byte unsigned little-endian length, then protobuf; 1 MiB maximum before allocation |
| Envelope | Version, random nonzero generation, nonzero correlation, parent correlation and typed body |
| Identity | Hello/ack with package ID, expected fingerprint and 32-byte bootstrap nonce |
| Operations | Describe, create, initialize, invoke, shutdown, destroy, resolve/invoke/release service and stop |
| Results | Distinct OK, invalid argument, not found, cancelled, timed out, extension/protocol/host failure |
| Payload | Schema ID plus JSON; malformed/non-finite JSON rejected; component/schema validation remains above transport |
| Correlation | Engine calls use even IDs; worker service calls use increasing odd IDs with an active parent |
| Bounds | 16 active nested calls, at most one pending terminal per active call, 128 delivered events per pump |
| Logs | At most 4 KiB text/256-byte category; overflow counted by DroppedEvents, never substituted for a terminal result |

Unknown optional protobuf fields are accepted. Unknown bodies/operations,
unsupported versions, stale generations and duplicate/unknown terminal results
fail closed. Golden wire bytes and malformed-frame tests protect these rules.
Fragmented prefixes/bodies survive polling timeouts without losing bytes.

The supervisor is an **owner-thread, synchronous/reentrant pump**, not a parallel
scheduler. While waiting, it dispatches scoped service requests and can reenter
Call from the service handler. Ancestry rejects cycles and nested calls inherit
the earliest deadline and parent cancellation. Engine callbacks must be bounded:
process isolation cannot interrupt arbitrary in-process callback code.
The language hosts must keep their control reader responsive independently of
blocking user/vendor work; that implementation belongs to D4.2/D4.3.

Only a remaining monotonic budget crosses the process boundary. Cancel ack is
not completion. Late success becomes cancelled/timed out. After interruption,
new resolve/invoke service work is rejected; release is still allowed for cleanup.
A failed/disconnected/blocked worker is never automatically replayed or restarted.

## Process containment and teardown

Launch uses an explicit existing absolute executable, correctly quoted arguments,
no shell, a controlled working directory and a small environment allowlist.
Only the bootstrap read handle is inherited. The nonce is in a bounded anonymous
pipe record, not command-line text. The local named pipe has a current-user DACL,
rejects remote clients, uses a random name and verifies the peer process identity.

A suspended worker enters a kill-on-close Job Object **before** execution. Failure
to establish containment fails launch. Worker states are Created, Starting,
Handshaking, Ready, Stopping, Exited and Faulted.

Defaults: 5-second startup, 2-second cancellation grace, 10 ms polling,
250 ms write budget, 500 ms stop response, 1-second root exit wait and bounded
250 ms job-accounting drain. Tests use smaller explicit budgets. Pipe cancellation
is drained before OVERLAPPED buffers leave scope.

A successful root exit is insufficient when job descendants remain alive.
Forced cleanup terminates the owned job and reports Faulted/unconfirmed cleanup.
Stopping from a nested callback aborts safely instead of leaving a dangling pipe.
This is trusted-code crash containment, **not a malicious-code sandbox**.
Terminating a process cannot prove a physical instrument is safe; hardware
watchdogs/interlocks and operator recovery remain necessary.

## Draft managed manifest

[Manifest v3 schema](contracts/artest-extension-manifest-v3.schema.json) describes
managed runtime requirements plus common component metadata and a bounded SHA-256
file inventory. Python specifies an inventoried code directory and module
entrypoint; .NET specifies an inventoried assembly and type entrypoint. Both
declare a dependency lock, runtime version, x64/outOfProcess and wire 0.1.

`ParseManagedPackageRequirements` validates/project-types **only** the runtime and
inventory subset: safe portable paths, case-insensitive uniqueness, known versions,
required entry/lock coverage and hash syntax. It does not read files, verify hashes,
validate all component schemas, load language code or prepare environments.
The draft schema is not wired into the production catalog; native manifests v1/v2
retain their existing behavior. Full inventory containment/hash verification,
canonical package fingerprinting, prepared environments and descriptor matching
are D4.2 activation gates. Hashes alone do not authenticate a publisher.

## Reproducible build dependencies

Use Visual Studio Insiders, MSVC v145, C++20 and the existing Windows SDK.
The solution restores the private manifest in source/ARTestEngine.Process/vcpkg.json
through scripts/restore-process-dependencies.ps1 and its repository-local triplet.
The baseline is 21a093fd906aa328bf40559e3011d0e34a96c124.

Validated graph: protobuf 6.33.4#2, abseil 20260107.1#3, utf8-range 6.33.4.
The protoc tool and C++ runtime come from the same restore. Static libraries use
the matching Debug/Release dynamic CRT; no protobuf DLL is added to native SDK
deployment. First restore needs the approved vcpkg source/binary downloads and
can take several minutes. It does not install Python or .NET.

Generated .pb.cc/.pb.h and dependency binaries stay under artifacts/, never source
control. Generation is an MSBuild prerequisite in both Visual Studio and scripts.
Do not copy generated code from a different protoc version: the
[protobuf C++ compatibility rules](https://protobuf.dev/support/cross-version-runtime-guarantee/)
require matching generated code and runtime.

## Verification and next prerequisite

Run the normal Debug and Release builds described in [TESTING.md](../../TESTING.md),
then the frozen SDK 0.2.1 compatibility matrix. Focused process tests exercise real
child processes, nested services, cancellation, corrupt frames, crashes, blocked
work, descendant cleanup, environment isolation and native-adapter parity.
No physical hardware or manual test report is necessary for this foundation slice.

Next: D4.2 Python host/SDK, generated metadata, explicit environment preparation,
one simulated driver/command and C++/Python interoperability. Install standard
GIL-enabled **CPython 3.13 x64** with pip/venv, alongside any legacy Python 3.7
installation. Select the 3.13 Windows 64-bit installer from
[Python's official Windows downloads](https://www.python.org/downloads/windows/);
do not choose the free-threaded or embedded distribution.

Verify the selected interpreter explicitly (replace the path with its install path):

~~~powershell
& 'C:\Python313\python.exe' -c "import sys, struct, venv; print(sys.version); print(struct.calcsize('P') * 8)"
& 'C:\Python313\python.exe' -m pip --version
~~~

Expected: Python 3.13.x and 64 bits. This selects the next target, not an already
validated ARTest Python support claim. Python 3.7 is outside this matrix and
[reached end of life](https://peps.python.org/pep-0537/).
Do not globally install protobuf/pywin32 or vendor packages now: D4.2 will pin and
restore them in a dedicated environment. The C++ restore does not supply Python
wheels. Existing .NET SDK 10.0.400/runtime 10.0.11 were observed on this machine;
.NET 10 x64 is the D4.3 target, with no additional installation needed now.
