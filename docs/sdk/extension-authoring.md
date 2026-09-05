# Write an ARTest command or Instrument Driver

## Supported baseline and scope

D3.3-A provides an experimental C++20 authoring API over the unchanged native
ABI 0.1. Use Visual Studio 18 Insiders / v145, x64, and Debug or Release.
The examples are simulated and never access physical equipment.

The SDK is header-only. Its only non-standard C++ dependency is
nlohmann/json 3.12.0. Public headers use <nlohmann/json.hpp>; the repository
provides an include adapter to its existing vendored copy. D3.3-C supplies the
self-contained installed SDK ZIP; see [distribution](sdk-distribution.md) if
you are building outside the repository. SDK 0.2.0 adds declarative metadata;
see [generation](metadata-generation.md). It retains the compatibility fixes
exercised by the D3.3-B [reference extensions](reference-extensions.md).

## 1. Build and run the complete example

Run from D:\GitHub\main\ARTestCLI:

```powershell
.\scripts\build.ps1 -Configuration Release -Platform x64
$cli = '.\artifacts\bin\x64\Release\ARTestCLI.exe'
$extensions = '.\artifacts\sdk-examples\x64\Release'
$plan = '.\source\ARTest.SDK\examples\ARTestSdkExample\ExamplePlan.json'
& $cli compile $plan --extensions $extensions
& $cli run $plan --extensions $extensions
$LASTEXITCODE
```

Compilation prints "Valid script. No instruments were initialized."
Execution reports "Measured 12.000000 V.", shuts down the driver, ends PASSED,
and returns exit code 0.

Open source/ARTestCLI.sln to inspect the tenth project, ARTestSdkExample.
It has no ProjectReference to Core, Engine, or another driver. The source-tree
ARTestSDK.props import supplies include paths and ARTEST_EXTENSION_EXPORTS for
DLL targets. If using your own build system, define ARTEST_EXTENSION_EXPORTS
for the extension target before including SDK headers and provide SDK/include
and the nlohmann/json include directory. A missing export definition produces
a compile-time diagnostic, not a silently unloadable DLL.

## 2. Understand the three example files

- [ReadVoltageCommand.h](../../source/ARTest.SDK/examples/ARTestSdkExample/ReadVoltageCommand.h):
  typed parameters, validation, cooperative wait, service call, explicit result.
- [SimulatedSupplyDriver.h](../../source/ARTest.SDK/examples/ARTestSdkExample/SimulatedSupplyDriver.h):
  operation registration, initialization, simulated state and shutdown.
- [ExampleExtension.cpp](../../source/ARTest.SDK/examples/ARTestSdkExample/ExampleExtension.cpp):
  local metadata/factories and one export macro.

A command derives from artest::sdk::Command. Implement Execute and optionally
Validate. Validate is a side-effect-free runtime semantic check and can run
more than once. Offline compilation uses manifest schemas, never this callback.

A driver derives from artest::sdk::InstrumentDriver. Implement Initialize and
Shutdown. Register named handlers in its constructor; handlers can delegate to
ordinary private methods. Constructors must not acquire hardware or start threads.

## 3. Read parameters without coercion

```cpp
const auto channel = parameters.Get<int>("channel");
const auto voltage = parameters.Get<double>("voltage");
const auto settleMs = parameters.Optional<int>("settleMs", 0);
```

Supported Get types are integral/floating-point types, bool, std::string, and
sdk::Json. Integer 1 is not interchangeable with 1.0, true, or "1".
Out-of-range numeric conversions, negative-to-unsigned conversions and non-finite
values fail. Floating-point reads accept JSON numbers with normal floating-point
rounding; use an integral type when exact integer precision is required.
An explicit null is not an absent optional field. Parameters requires an object;
Get<Json> allows nested data for custom parsing.

Malformed input throws std::invalid_argument, which the ABI adapter converts
to InvalidArgument. When testing the C++ class directly, assert that exception.
Do not catch all exceptions in user code and return Success.

Schemas enforce structure/ranges before execution. Use Validate or operation
handlers for relationships that schemas cannot express. Follow
[Schema Profile 1](../architecture/schema-profile-v1.md); it is not full JSON Schema.

## 4. Propagate results and use services

```cpp
auto response = context.CallInstrument(
    "artest.contract.instrument.power-supply.v1",
    "artest.instrument.power-supply.v1/read-state",
    {{"channel", channel}});
if (!response)
    return response;
```

CallInstrument uses the command's configured instrument ID. The manifest must
declare the matching configured requirement. Commands never cast service handles
or link directly to a driver. Call allows an explicit instance ID when appropriate.
The SDK resolves, invokes, copies the result and releases the service on every
path. A service callback or release failure cannot become success. The call is
not replayed automatically, including when an error buffer is too small.

Use Result::Success(message) for a successful command message, WithData(object)
for a JSON object response, and Failure(Status, message) for an operation error.
Use WithData(object, schemaId) when preserving a specific service response schema;
SchemaId() exposes it after a native service call. The one-argument overload
continues to use artest.schema.generic-json.v1.
If a data object contains message, it must be a string. Data() is optional:
check it before reading a service response. Message() also exposes a successful
message directly, without requiring JSON access.

A Result is an operation status, not a measurement verdict. Engine API 0.4
preserves command messages in run reports; arbitrary data fields are not yet
mapped into persistent step measurements. Do not invent a PASS/FAIL payload
convention that the Engine does not interpret.

## 5. Respect lifetimes, cancellation and cleanup

- Context and Parameters are borrowed only for the current call. Do not retain
  them or use them from a background thread. Copy values you need to store.
- Check Checkpoint between bounded device operations. WaitFor is cooperative.
  Configure real vendor I/O timeouts separately; the SDK cannot interrupt an
  arbitrary blocking vendor call or protect against native memory corruption.
- Each runtime instance has its own state. No mutable globals for device sessions.
- Driver initialization runs at most once per instance. Operations require
  successful initialization. A stopped instance cannot be reinitialized.
- Shutdown can be called before initialization, after partial initialization,
  or after cancellation. Make it safe in all three cases.
- The adapter suppresses cancellation/deadline checks during Shutdown so cleanup
  is attempted. Shutdown still needs its own bounded device I/O.
- Shutdown is attempted at most once by the adapter. A failure stays a failure;
  repeated shutdown is a no-op, not a retry of unknown hardware side effects.
- Fallible hardware cleanup belongs in Shutdown. Destructors must remain noexcept
  and release local resources through RAII.
- Host callbacks and component operations remain synchronous. No background
  callback invocation, cross-module C++ objects, or cross-module allocation/free.

## 6. Declare metadata and schemas

Use explicit AddCommand/AddDriver calls in a definition function that returns
Extension. AddDriver requires a service contract; set DriverMode::Simulated
for simulations. An empty component version inherits the extension version.
Duplicate component IDs and reserved/duplicate operation registrations fail.

The definition function is metadata-only and can run during Query. Do not open
devices, call services, construct components or publish global registrations there.
ARTEST_EXPORT_EXTENSION belongs exactly once in a .cpp, never a shared header.

The source-tree example declares its parameters/configuration through Schema and
ComponentMetadata in ExampleExtension.cpp. Its build generates the manifest and
schema files automatically; do not recreate the deleted source JSON files.
The [generation guide](metadata-generation.md) explains the shared definition.
The four reference packages and installed starter still use source manifests
until D3.4.3; keep their JSON and C++ declarations consistent during that transition.

## 7. Test behavior locally, then test the native boundary

```cpp
#include <ARTest/Testing.h>
// With ReadVoltageCommand included from the example:
artest::sdk::testing::TestContext context;
context.instrumentId = "PS1";
context.onCall = [](const auto&) {
    return artest::sdk::Result::WithData({{"voltage", 3.3}});
};
const artest::sdk::Json values = {{"channel", 1}};
const artest::sdk::Parameters parameters{values};
artest::examples::ReadVoltageCommand command;
auto validation = command.Validate(parameters);
if (validation) {
    auto result = command.Execute(parameters, context);
    // Assert result, context.calls, context.logs and context.elapsed in your test.
}
```

TestContext does not sleep, load DLLs or access hardware. It advances logical
time, supports cancellation/deadline checks, records logs/calls, and routes
service calls to a test callback. It does not replace ABI or lifecycle testing.

```powershell
.\scripts\test-sdk-authoring.ps1 -Configuration Release
```

This runs all Sdk* Google Tests without overwriting the complete XML/HTML report.
The complete regression includes real Engine/DLL integration as well.

## Troubleshooting

| Symptom | Check |
|---|---|
| Missing export-definition diagnostic | Import ARTestSDK.props for the DLL target or define ARTEST_EXTENSION_EXPORTS |
| COMMAND_TYPE_UNKNOWN | Prepare the sdk-examples catalog with --extensions |
| Parameter rejected | Match the schema and exact JSON type; do not rely on coercion |
| No configured instrument | Declare requires/configured in the manifest and bind instrument in the plan |
| OperationNotSupported | Register the exact operation ID used by the command |
| InvalidState | Initialize the driver before operations; do not reuse stopped instances |
| Native binary/manifest mismatch | Rebuild and repackage metadata and DLL together |

D3.3-B has migrated the CAN/power/sample/hardware reference DLLs. D3.3-C
provides an evaluation SDK ZIP, an external project template and installed
consumer compatibility validation. See [SDK distribution](sdk-distribution.md).
The authoring API remains experimental until the larger compatibility and
release-readiness gates are satisfied.
