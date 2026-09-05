# ARTest extension authoring checklist for AI agents

Use this checklist with the developer guide. It is not permission to change
the Engine or access physical equipment.

## Start here

1. Read source/ARTest.SDK/README.md and docs/sdk/extension-authoring.md.
2. Inspect the three small ARTestSdkExample source files; reuse their actual API.
   The migrated power/CAN reference packages are described in reference-extensions.md.
3. Confirm whether the user needs a Command, InstrumentDriver, or both.
4. Record component ID, service contract, operation IDs, parameter/configuration
   schema, lifecycle assumptions, and simulated versus real I/O.
5. If a vendor protocol or SDK detail is missing, ask or consult authoritative
   vendor documentation. Never invent instrument commands or API signatures.

## Implementation boundary

- Public includes: ARTest/Command.h, InstrumentDriver.h, Extension.h, Testing.h.
- Do not include ARTestEngine.Core, Engine internals, ExtensionSupport or detail/.
- Do not handwrite ARTestExtension_Query or opaque handle structs.
- Register components explicitly with AddCommand/AddDriver; export once in a .cpp.
- Register driver operations locally, not with global/static registration.
- Keep components default-constructible and constructors free of hardware I/O.
- Read strict typed parameters; use schema-supported keywords only.
- Propagate unsuccessful Result values. Never convert catch (...) to Success.
- Check optional result data before reading it.
- Preserve contract-specific response schemas with Result::WithData(data, schemaId).
- Do not invent a measurement-verdict schema; operation status is the current contract.
- Keep all C++ objects, JSON allocation and destruction within the same module.
- Do not retain Context, Parameters, borrowed payloads or callbacks.

## Resource and fault checklist

- Bound every vendor I/O operation and check cancellation between operations.
- Shutdown must tolerate no initialization, partial initialization and cancellation.
- Use RAII for local resources; destructors must not throw.
- No driver-to-driver links or service-handle casts in command code.
- No background host callbacks or shared mutable global device state.
- State the in-process crash/isolation limitations accurately.
- Hardware tests require explicit user authorization; default to simulations.

## Verification evidence

- Add pure C++ behavior tests with TestContext.
- Test missing/wrong parameters, driver/service failure, timeout/cancellation,
  partial initialization, shutdown failure and result propagation as applicable.
- Build with C++20 and /W4 /WX for the new component.
- Validate the manifest and execute the package through the Engine.
- Run Debug and Release regression, not only your new test filter.
- Keep manual evidence pending until the user actually executes it.
- Do not commit unfinished Word reports, lock files or build artifacts.
- Document build commands and expected exit codes, not just screenshots.

## Current scope

Engine API 0.4 / extension ABI 0.1 remain experimental. D3.3-A authoring,
D3.3-B reference migration and D3.3-C installed-SDK consumers are implemented.
SDK package 0.1.1 adds schema-preserving Results without a C ABI change.
D3.4.1 introduces Schema/ComponentMetadata and generates the source-tree example.
Read metadata-generation.md; do not invent a raw-JSON schema escape hatch or
duplicate generated example manifests in source. SDK version is now 0.2.0.
Python/.NET hosting, real driver certification and ABI 1.0 are not complete.
