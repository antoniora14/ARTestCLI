# ARTest C++ extension authoring SDK

Experimental SDK 0.2.0 - D3.4.1 declarative metadata and first generated example.

The source-tree SDK example now generates its manifest and schemas from C++.
See [metadata generation](../../docs/sdk/metadata-generation.md) for the design,
supported API and build procedure. Installed starter migration follows in D3.4.3.
C++20, native extension ABI 0.1.
The Engine host API remains 0.4. The SDK can be packaged for external
compatibility evaluation; it is not a frozen binary contract or public release.

Start with [the developer guide](../../docs/sdk/extension-authoring.md).
For AI-assisted work, also read [the authoring checklist](../../docs/sdk/ai-extension-authoring.md).

## What you implement

| Component | Your responsibility |
|---|---|
| Command | Execute parameters against services; optionally validate semantic relationships |
| InstrumentDriver | Initialize configuration, register operations, shut down resources |
| Extension definition | Explicit component IDs, names, contracts and factories |
| Manifest and schemas | Offline discovery, bindings and input validation |

Include `<ARTest/Extension.h>` in the DLL entry point, and use the narrower
`Command.h` / `InstrumentDriver.h` headers in component code.
`ARTestSDK.props` configures includes, C++20 and the DLL export definition.
It does not add any Engine or Core linker dependency.

[ARTestSdkExample](examples/ARTestSdkExample) is a working command + simulated
driver package. It builds with /W4 /WX and runs through the same Engine as CLI.
Its output is isolated under artifacts/sdk-examples so it does not alter the
four-package reference catalog.

## Public vocabulary

- `Parameters.Get<T>(name)`: required, strictly typed input.
- `Parameters.Optional<T>(name, fallback)`: fallback only when absent.
- `Result.Success()` / `Success(message)` / `WithData(object)`.
- `Result.Failure(status, message)`; check every returned Result.
- `Context.Checkpoint()`, `WaitFor(milliseconds)`, `Log(level, message)`.
- `Context.CallInstrument(contract, operation, request)`: configured binding.
- `Context.Call(contract, instanceId, operation, request)`: explicit binding.
- `InstrumentDriver.RegisterOperation(id, handler)`: local constructor registration.
- `Extension.AddCommand<T>(info)` / `AddDriver<T>(info)`.
- `ARTEST_EXPORT_EXTENSION(definitionFunction)`: once per DLL, in a .cpp.
- `testing::TestContext` from `<ARTest/Testing.h>`: deterministic local tests.

The C++ types stay inside your module. Do not include anything from detail/
in extension code. Existing ARTestEngineClient.h serves application hosts;
it is not needed by command/driver DLLs.

D3.3-B has migrated all four reference packages to this API.
See [the reference walkthrough](../../docs/sdk/reference-extensions.md).
D3.3-C packages dependencies/templates and validates external consumers.
See [the distribution guide](../../docs/sdk/sdk-distribution.md).
