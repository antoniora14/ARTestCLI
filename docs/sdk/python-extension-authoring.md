# Develop Python commands and Instrument Drivers

D4.2 supports standard GIL-enabled CPython 3.13 x64 on Windows. Python 3.7 and
free-threaded builds are unsupported. The Python SDK is experimental 0.2.0;
native SDK 0.4.0, native ABI 0.2 and Engine API 0.4 remain experimental.
Python SDK/host/Engine require private protocol 0.2. Old 0.1 packages must be
regenerated and prepared in new environments; do not rewrite their receipts.

## Prepare the working example

From the ARTestCLI repository root in PowerShell:

    .\scripts\build.ps1 -Configuration Release -Platform x64
    .\scripts\prepare-python-example.ps1 -Python 'C:\Users\anton\AppData\Local\Programs\Python\Python313\python.exe' -IncludeFaultTests -OutputRoot .\artifacts\python-c01-final

Use the actual absolute interpreter path on another machine. Preparation pins
protobuf 6.33.4 and pywin32 311 with wheel hashes and installs ARTest's generated
SDK wheel into isolated venvs. It does not modify global Python packages.
Visual Studio/v145 and the pinned protoc build are needed to build this repository;
Python extension authors do not need to compile a C++ Python interpreter bridge.

Preparation prints the machine-local mapping file. It verifies unchanged existing
outputs. Changed source/SDK inputs require a new -OutputRoot; do not edit receipts
or installed metadata to bypass a mismatch. Preserve the previous output until
the new environment has passed its checks.

Run the simulated measurement:

    $cli = '.\artifacts\bin\x64\Release\ARTestCLI.exe'
    $catalog = '.\artifacts\python-c01-final\extensions'
    $mapping = '.\artifacts\python-c01-final\environments\python-environments.json'
    $plan = '.\source\ARTest.Python\examples\PythonMeasurement.json'
    & $cli compile $plan --extensions $catalog
    & $cli extension-run $plan $catalog --python-environments $mapping
    $LASTEXITCODE

Compilation is offline. Execution prints PYTHON_WORKER_READY, initialization,
a passed measurement, shutdown and final result JSON; the exit code is 0.
The final JSON is terminal output unless you explicitly redirect it.

## Authoring surface

Read source/ARTest.Python/examples/simulated/extension.py. Its behavior classes use
only artest_sdk. A dataclass declares configuration/parameters and defaults;
parameter(minimum=..., maximum=...) supplies numeric bounds. Metadata generation
produces JSON files; developers do not handwrite command/driver manifests.

- Driver.initialize(config, context) acquires resources at runtime.
- @operation(contract_operation_id, ParameterType) names an async service handler.
- Driver.shutdown(context) performs bounded cleanup, including partial initialization.
- Command.execute(parameters, context) returns an explicit Result.
- context.instrument(contract) uses the step's configured instrument instance.
- await context.sleep(seconds) is cooperative.
- await context.blocking(vendor_function, *arguments) wraps bounded synchronous I/O.
- Result.verdict(passed, data, schema_id, message) reports a test verdict.
- Result.failure(message), or a raised exception, reports a technical error.
- Result.indeterminate(message, status="error") reports an unconfirmed device effect.

Keep constructors and define_extension free of hardware acquisition and expensive
imports. Metadata generation executes trusted Python code; it is not a sandbox.
Do not retain Context, detach tasks, mutate shared global instrument state, or
swallow failed service calls. Use async with context.instrument(...) so service
leases are released on every exit path.

One registered driver type can back PS1, PS2 or multiple oscilloscopes. Declare
separate instrument IDs/configurations in a plan and select the intended ID on
each command step. The Engine checks the primary contract, not vendor/model names.

## Build your own package

Copy the simulated example's code directory, choose unique extension/component
IDs, retain the intended service contract, and implement your behavior. Use a
dependency lock containing exact versions and wheel hashes for every transitive
dependency, including the SDK's protobuf/pywin32 requirements. Source distributions
and implicit downloads during execution are not supported.

The private build tool accepts:

    python.exe -I -B source/ARTest.Python/tools/package.py package --source D:\MyDriver --entry-point extension:define_extension --lock D:\MyDriver\requirements.lock --output D:\MyPackages\MyDriver
    python.exe -I -B source/ARTest.Python/tools/package.py prepare --package D:\MyPackages\MyDriver --sdk artifacts/python-c01-final/sdk/artest_python-0.2.0-py3-none-any.whl --output D:\MyEnvironments\MyDriver

Use an explicit supported interpreter. The mapping from extensionId to absolute
artest-environment.json path is deployment configuration, not portable extension
metadata. ARTestEngine accepts that object as pythonEnvironments in its creation
configuration. Do not put machine-specific interpreter paths in a manifest.

The SDK wheel and build tooling are an evaluation workflow, not a public package
feed or a complete offline deployment kit. Licensing/release policy, offline
wheelhouse deployment and external-consumer acceptance remain D4.4 work.

## Results and failure semantics

A successful function call does not prove a passing measurement. For example:

    return Result.verdict(
        measured >= 4.8,
        {"value": measured, "unit": "V", "minimum": 4.8},
        "artest.schema.measurement.voltage.v1",
        "Voltage minimum check",
    )

A reading of 4.2 V produces Failed. A communication exception produces Error.
Cancellation and timeout are separate statuses. The CLI uses run-result.v2,
which retains the measurement and schema on each step and attempt. Existing
C++ hosts can opt in through {"resultSchemaVersion": 2}; legacy v1 remains default.

If a worker crashes or ignores cancellation, its device effects are indeterminate.
Engine will not retry or continue that run, even if the plan requests it. A killed
process cannot confirm that a physical supply is off. Use device interlocks,
vendor timeouts and explicit operator recovery appropriate to the equipment.

A worker can remain alive while a device acknowledgement is missing. Explicitly
return Result.indeterminate in that case; a generic exception cannot tell the
Engine whether bytes were sent. See [external-effect uncertainty](external-effect-uncertainty.md).

## Performance and support limits

Python's interpretation, serialization and process IPC add overhead compared
with in-process C++. Async does not remove the GIL or make CPU-heavy Python run
in parallel. Vectorized/native-library computation may have different behavior.
For tight real-time loops or very frequent small transactions, prefer C++ or
batch operations at the driver boundary. Do not infer an exact latency ratio;
D4.4 measures startup, round trips, percentiles and cancellation response.

Control payloads are capped at 1 MiB. This interface is not a waveform-streaming
channel. Native callbacks remain cooperative. No .NET host, remote workers,
automatic environment repair, hot reload or hardware certification is included.

## Regression

    .\scripts\test-python-runtime.ps1 -Configuration Release -PythonRoot .\artifacts\python-c01-final
    & 'C:\Users\anton\AppData\Local\Programs\Python\Python313\python.exe' -I -B .\source\ARTest.Python\tests\test_sdk.py -v

The first command checks the installed worker's cancellation contract, then
explicitly enables the optional Python Google Test suite.
Missing environments are failures, never evidence of support. XML/HTML results
are under artifacts/test-results/x64/Release with the ARTestPython.Integration name.
