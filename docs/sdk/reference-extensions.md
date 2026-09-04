# Learn from the reference extensions

D3.3-B migrates the existing power, CAN and command packages to the same public
C++ API used by external developers. These are simulated reference implementations,
not certified drivers for physical equipment.

In the ARTestCLI repository, start with source/ARTestCmdHardware/PowerOffCommand.cpp.
It validates a channel, invokes a configured power-supply service and propagates
failure before returning its completion message. Next read PowerOnCommand.cpp
for a sequence of dependent calls and source/ARTestCmdSample/PowerCycleCommand.cpp
for a cooperative wait.

For a driver, read source/ARTestDrvSimCAN/SimCanDriver.h and SimCanDriver.cpp.
The constructor registers a local handler; Initialize checks configuration;
Send validates a frame; Shutdown releases simulated state. The SDK controls
whether those operations are legal in the current lifecycle state.

source/ARTestDrvSimPower/SimPowerExtension.cpp shows explicit registration of
two types sharing an implementation but having different resource requirements.
Each DLL entry file ends with one ARTEST_EXPORT_EXTENSION definition.

## Responsibilities that remain with the author

- Declare valid configuration and parameter schemas; keep them aligned with metadata.
- Use SDK Parameters for strict reads and propagate each unsuccessful Result.
- Keep hardware acquisition out of constructors and metadata definitions.
- Bound physical I/O and make Shutdown safe after partial initialization.
- Do not keep borrowed Context or Parameters beyond the current call.
- If a service has a specific response schema, use
  Result::WithData(data, schemaId). SchemaId() survives native service calls.
  The one-argument overload still uses artest.schema.generic-json.v1.
- Do not treat Result data as a new measurement-verdict contract.

tests/ReferenceExtensionTests.cpp shows deterministic TestContext tests and
test-only ABI lifecycle checks. tests/ReferenceExtensionIntegrationTests.cpp
runs the packaged DLLs through the unchanged Engine.

## Build and test

From the repository root:

    .\scripts\build.ps1 -Configuration Debug -Platform x64
    .\scripts\build.ps1 -Configuration Release -Platform x64

After a build, the focused reference suite can be run without replacing the
complete regression reports:

    .\artifacts\bin\x64\Release\ARTestCLI.UnitTests.exe --gtest_filter=Reference*

For standalone extension development, copy the installed SDK template and follow
[the distribution guide](sdk-distribution.md). The repository component sources
are learning references, not dependencies that your DLL must link.

Manifests and schemas are still maintained as source files and packaged by the
build. Automatic metadata generation is a subsequent authoring improvement.
