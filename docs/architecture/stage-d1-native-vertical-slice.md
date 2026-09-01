# Stage D1 - Native vertical slice

## Status

Implemented as an experimental ABI 0.1 slice. This is not yet the stable
extension SDK promised for D2.

## Delivered boundary

~~~text
ARTestCLI
  -> ARTestEngine C++ facade
  -> ARTestEngine host C ABI
  -> ARTestEngine.dll
  -> manifest catalog / native runtime adapter
  -> ARTestCmdSample.dll
  -> host service router
  -> ARTestDrvSimPower.dll
~~~

ARTestEngine.Core remains a static implementation library linked only inside
first-party binaries. No Core class, STL type, exception, allocator, or virtual
interface crosses either public DLL boundary.

## Engine host API

ARTestEngine_QueryApi returns one size-versioned function table. D1 implements
lifecycle, catalog refresh and snapshot, plan compilation, event subscription,
asynchronous sessions, cooperative cancellation, host wait timeouts, immutable
results, and JSON result serialization.

The first-party ARTestEngineClient.h facade owns handles with RAII while still
calling only the C ABI. It does not expose Engine internals.

## Native catalog and loader

The catalog scans only direct packages under an explicitly approved root. It:

1. reads artest-extension.json before loading code;
2. enforces the native, in-process, x64, ABI 0.1 contract;
3. canonicalizes the package and rejects entry paths outside it;
4. loads the single ARTestExtension_Query export;
5. validates the returned function table and extension ID;
6. compares every binary descriptor with its manifest declaration;
7. rejects duplicate component IDs before registry mutation;
8. retains modules until Engine destruction.

Catalog publication is atomic: any invalid package prevents the candidate
catalog from replacing runtime state.

## Service routing

The simulated driver registers its configured instance only after successful
initialization. The sample command requests that instance using the stable
artest.contract.instrument.power-supply.v1 contract. The service handle owns a
temporary shared lease and is released by the command on every return path.

The command DLL has no project reference, import library, header dependency, or
runtime symbol dependency on the driver DLL.

## Safety behavior

- Native entry points contain C++ exceptions and return ARTestStatus.
- Calls use borrowed UTF-8 views and caller-owned error/result storage.
- Module calls are serialized by default and permit same-thread service reentrancy.
- Host wait timeout does not cancel an execution.
- Cancellation is cooperative and reaches command and driver invocations.
- Instrument shutdown is attempted after pass, failure, timeout, or cancellation.
- Cleanup failure overrides an otherwise passing result.
- Sessions and compiled plans are rejected when used with a different Engine.
- Native modules unload only after all Engine-owned runtime objects are gone.

## Reference packages

| Binary | Stable extension ID | Component |
|---|---|---|
| ARTestCmdSample.dll | com.artest.extension.sample-command | com.artest.command.sample.power-cycle |
| ARTestDrvSimPower.dll | com.artest.extension.sim-power | com.artest.driver.sim.power |

Build packaging creates one directory per extension under
artifacts/extensions/x64/(Configuration).

## Automated acceptance

The D1 regression cases execute through the exported ABI and packaged DLLs.
They cover API negotiation, manifest rejection before code execution,
command-to-driver invocation, events and immutable results, cancellation and
shutdown, non-cancelling host waits, cleanup verdict override, handle ownership,
and module release.

The complete Debug and Release baselines contain 49 tests across 14 suites.

## Deferred to D2 and later

- ABI 1.0 freeze and semantic-version compatibility policy;
- standalone SDK packaging, templates, and separately built compatibility kit;
- cryptographic package integrity and publisher trust;
- schema compilation and generated ARTestStudio parameter editors;
- catalog refresh/unload while an Engine remains active;
- migration of every built-in command and fake instrument;
- Python and .NET runtime-host proof of concept.
