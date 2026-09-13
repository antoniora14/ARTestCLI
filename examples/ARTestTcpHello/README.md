# ARTest TCP Hello World — example kit 0.1.0

Build a small C++ Instrument Driver and a typed command using only ARTestSDK.
The instrument is a separate native TCP simulator, not an in-memory mock.
Two plan bindings (V1/V2) use independent connections to the same simulator.
The optional Python command uses the very same driver through the Engine broker.

This is an experimental teaching example, not a physical instrument driver.
It does not switch real outputs or guarantee a physical safe state.

## Prerequisites

- Windows x64; Visual Studio 2026 Insiders with Desktop development with C++,
  MSVC v145, Windows SDK and C++20 support.
- Extracted ARTestSDK 0.4.0 Windows x64, matching Debug or Release configuration.
  Its bundled JSON headers and metadata tools are required. No vcpkg downloads,
  private Core library, Engine project or Python are needed to build this kit.
- To execute a sequence: a matching ARTestCLI host and ARTestEngine.dll.
  The extension DLL itself does not link Engine/Core.
- PowerShell 7 for the commands below.

## Build outside the repository

Copy this whole folder (excluding out and .vs) to a developer-owned directory,
for example D:\Work\ARTestTcpHello. Extract the SDK ZIP separately.
In PowerShell, from the copied folder:

    .\Build.ps1 -SDKRoot 'D:\SDK\ARTestSDK-0.4.0-windows-x64' -Configuration Release

Override -VisualStudioPath if Insiders is installed elsewhere. Build.ps1 builds
the DLL, automatically generates and validates its manifest/schemas, publishes
out/extensions/x64/Release/ARTestTcpHello, then builds the simulator.
Do not author or edit the generated JSON files. Change Extension.cpp instead.

For Visual Studio: set ARTestSDKRoot in the environment before launching devenv,
then open ARTestTcpHello.vcxproj; open simulator/ARTestTcpSimulator.vcxproj to
build the simulator. Select x64 and the configuration matching the SDK.
Build.ps1 is the reproducible equivalent; the projects have no repository
dependencies when ARTestSDKRoot is supplied.

## Execute and inspect evidence

    .\Run.ps1 -CLI 'D:\ARTest\ARTestCLI.exe' -Configuration Release

Run.ps1 owns the simulator process. It waits for a PID-checked ready event, reads
the OS-assigned free loopback port, produces a plan and isolated catalog, then
runs the CLI. It signals stop in finally and terminates only its own process if
graceful stopping exceeds 3 seconds. The simulator also watches its parent.
Do not start it as a service or add a firewall rule. Ctrl+C stops the run; inspect
the evidence directory printed by the script after stopping.

Each run creates out/runs/<unique-id> with plan.json, server.jsonl and cli.txt.
The two steps should pass, with V1=12 V and V2=5 V. The journal must contain one
applied event for each step and close requests. The current CLI and C-02 automated
tests explicitly select resultSchemaVersion=2 to expose typed outcome and attempt
fields; the Engine API default remains v1 for older hosts.

To discover and compile without starting a simulator:

    & 'D:\ARTest\ARTestCLI.exe' compile .\ExamplePlan.json --extensions .\out\extensions\x64\Release

Port 50250 in the source example is a placeholder; offline compilation does not
connect. Run.ps1 replaces it in its generated copy, leaving the source unchanged.

## Where to start reading

1. Extension.cpp: one metadata declaration for component IDs, schemas and binding.
2. SetAndMeasureCommand.h: parameters, broker call, typed data and limit verdict.
3. TcpVoltageDriver.h: per-instance lifecycle, service operations and error policy.
4. tcp/Transport.h: private WinSock RAII and bounded nonblocking operations.
5. simulator/Main.cpp and Server.h: normal process and bounded protocol server.
6. python/extension.py: the same command expressed with the Python SDK.

The driver advertises artest.contract.example.tcp-voltage.v1, a deliberately
small example-owned contract, not a general power-supply standard.
Commands know the service and instrument binding; they never know a socket or
concrete driver class. To add a second model, implement the same service contract.
Do not change the contract's meaning while keeping its identifier.

## Timeouts, uncertainty and cleanup

Every operation shares a single monotonic local budget across waiting, sending
and receiving. Engine cancellation/deadline is additionally checked; Context
does not expose its absolute deadline to this driver. Each wait is at most 10 ms,
but Windows scheduling means this is not a hard real-time guarantee.
Initialization and cleanup have separate finite budgets.

The JSON/LF protocol has a 1024-byte limit. An ACK must match version, request ID
and operation. Sending bytes does not prove application. If a mutating request
was sent and no valid ACK arrives, the result is Indeterminate, including for
timeout/cancellation. Engine stops retries and continuation. The link is discarded;
there is no hidden reconnect or retransmission. Failed reads are not uncertain
writes but also discard a desynchronized link. Start a new session to reconnect.

A socket close is not proof that an instrument entered a safe state. Missing close
ACK remains a cleanup failure. The simulator's flushed journal proves its own
logical action only, not physical hardware behavior or power-loss durability.

## Optional Python command

Native usage above needs no Python. Python usage requires CPython 3.13 x64,
ARTest Python SDK 0.2.0 and its hash-pinned dependencies in a prepared environment;
Python 3.7 is unsupported. Do not install these into the native process.
Environment preparation may download wheels; execution never installs dependencies.

From the ARTestCLI repository, prepare a separate environment:

    .\scripts\prepare-tcp-hello-python.ps1 -Python '<Python313\python.exe>' -SDKWheel '<artest_python-0.2.0-py3-none-any.whl>'

Then from the example folder:

    .\Run.ps1 -CLI '<ARTestCLI.exe>' -PythonPackage '<artifacts\python-c02\extensions\ARTestPyTcpHello>' -PythonEnvironment '<artifacts\python-c02\environment\artest-environment.json>'

The Python command has the same voltage/minimum/maximum parameters and measurement
payload. Its only instrument access is Context.instrument + invoke. Python adds
worker startup and interprocess overhead; prefer native code for tight latency
requirements. No general C++/Python timing ratio is promised. The native example
kit is independent; Python packaging currently uses the repository's Python tools
and explicit SDK wheel, not the native SDK ZIP.
