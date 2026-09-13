# C-02: a bounded TCP SDK example

Status: implementation candidate, acceptance pending. Base: d6590c8 (C-01).

The independent example kit lives in examples/ARTestTcpHello (version 0.1.0).
It consumes native SDK 0.4.0, native ABI 0.2 and Engine API 0.4 without adding
public contracts to Engine/Core. Its optional Python command consumes Python SDK
0.2.0/private wire 0.2. The example is not part of the native SDK's versioned
inventory. Normal native use has no Python requirement.

## Boundaries and ownership

- SetAndMeasureCommand depends on a package-owned voltage capability, not the driver.
- TcpVoltageDriver owns Winsock lifetime, socket, sequence and transaction mutex
  per instance. Construction/metadata/discovery/offline compilation perform no I/O.
- Private tcp/Transport.h implements nonblocking WinSock against literal
  127.0.0.1, RAII and monotonic budgets. No DNS, worker-per-call, background I/O,
  retained Context or private Engine include/link.
- ARTestTcpSimulator is a separate native process. One bounded event loop owns
  up to 16 simulated connections, with independent voltage per connection.
- The Python command uses the Engine broker to reach this same native driver.
  It never opens sockets or imports the native DLL itself.

The command's technical success is distinct from its measurement verdict.
An acknowledged voltage outside limits is Failed, not Passed and not an
indeterminate effect.

## Service and wire contract

Contract: artest.contract.example.tcp-voltage.v1.
Operations: artest.example.tcp-voltage.v1/apply and .../read.
Apply request: voltage (finite number, 0..60 V); Read request: empty object.
Response schema artest.schema.example.tcp-voltage.v1: value and unit "V".
The example contract deliberately does not advertise the broader supply contract.

The private transport is JSON followed by one LF, at most 1024 bytes including LF.
Each request has v=1, a strictly increasing unsigned id, and op=hello/apply/read/close;
apply additionally carries value. Responses echo v/id/op, have ok=true and
value/unit for apply/read. A positive apply response means the simulator applied
the voltage, not merely that TCP accepted bytes. Invalid version, id, operation,
payload, delimiter, oversized data or unconfirmed exchange invalidates the link.
No pipelining, reconnect/replay or implied device-side deduplication is provided.
The server journal records the action before generating its response.

## Budgets and failure classification

One local monotonic expiry covers each complete operation, including mutex wait,
write and read. Initialize also includes connect and hello in its own budget.
Fragments do not restart the expiry. Context.Checkpoint observes Engine
cancellation/deadline; the helper does not receive or know an absolute Engine
deadline. Stop when either bound expires; polling is at most 10 ms per wait.
This is cooperative bounded waiting, not a hard real-time latency guarantee.

Defaults: initialize 1000 ms, operation 1000 ms, shutdown 500 ms; configured bounds
1..10000 ms. Cleanup has its own finite budget and may not race a live transaction.
Closing a socket releases local resources but is not proof of remote shutdown.
An unavailable link or missing close ACK remains a cleanup failure.

Proven pre-send failures remain ordinary errors. After any mutating request bytes
were accepted by send, failure to validate the ACK returns Indeterminate with its
original technical cause (including cancellation/timeout). Engine owns the
no-retry/no-continue rule. Failed reads are not uncertain writes, but still
invalidate the link to prevent a late response satisfying a subsequent call.

## Test separation and evidence

Normal simulator has no fault switches. Tests/TestSupport/Fakes/TcpHelloServer.cpp
provides a separate-process server mode in the Google Test executable. The harness
uses OS-assigned port 0, a PID-checked readiness event, named stop event and an
owned kill-on-close Job Object. It never kills unrelated processes.

Acceptance must include native and Python broker paths, server-journal lost-ACK
evidence, bounded waits/fragmentation, cleanup, isolated instances, an external
copied-kit build and Debug/Release regression/ABI/SDK/compatibility gates.
Do not infer acceptance from this design document; record executed counts and
source/evidence hashes separately under artifacts/acceptance/c02.

Out of scope: physical instruments, SCPI/VISA, generic network SDK, Linux,
CMake, physical safe-state recovery, C-03/C-04, .NET or ARTestStudio integration.
