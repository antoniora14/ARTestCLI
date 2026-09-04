# D3.3-B Reference extension migration

Status: implemented; manual acceptance pending. SDK package 0.1.1,
Engine API 0.4, native extension ABI 0.1. No ABI 1.0 stability claim.

## Outcome

The four reference packages now use the public C++ SDK. DLL entry files contain
only metadata and explicit AddCommand/AddDriver calls. Behavior lives in small
component classes; no reference component implements opaque handles, ABI tables,
payload marshalling or service leases.

| Package | Behavior classes |
| --- | --- |
| ARTestDrvSimPower | SimPowerDriver and its resource-required LegacySimPowerDriver variant |
| ARTestDrvSimCAN | SimCanDriver |
| ARTestCmdSample | PowerCycleCommand |
| ARTestCmdHardware | PowerOnCommand, PowerOffCommand, SendCanMessageCommand |

The SDK owns lifecycle state, exception containment and service release.
The Engine still owns execution policy, retries, session cancellation and
unconditional driver cleanup. Constructors only register local operations.
Commands use configured service contracts and never link to another driver.

## Compatibility and intentional hardening

- Package/component IDs, aliases, versions, callable contracts, operation IDs,
  source manifests, schemas and JSON response fields remain unchanged.
- The legacy PowerSupply alias still selects the resource-required driver;
  the native simulated power type still permits an empty configuration.
- Existing initialization, shutdown and simulated turn-on failure diagnostics
  remain visible. Both failInitialize and failInitialization are supported for power.
- Power On keeps voltage, current-limit and output-enable ordering. A failed call
  stops subsequent calls. Power Cycle propagates turn-off failures.
- Power Cycle uses Context.WaitFor instead of private sleep/cancellation code.
  A cancelled/timed-out wait cannot return success. Commands do not bypass a
  deadline to call hardware: the Engine's driver Shutdown is the cleanup authority.
- Integration checks exposed a pre-existing missing deadline at the native
  invocation boundary. The Engine now projects the Core token's steady-clock
  deadline into the existing ABI field, using the host clock's epoch. Services
  inherit that context; cleanup remains deadline-free. A strengthened real-DLL
  regression checks early interruption, not only the eventual timeout verdict.
- The SDK now prevents calls before successful initialization and reuse after
  shutdown. Cleanup remains callable after partial initialization and cancellation.
- Wrong JSON types and malformed/overflowing CAN identifiers are InvalidArgument,
  not accidental coercion or an unspecified native exception failure.
- CAN retains only its observable message count, not an unbounded frame history.
- The power simulator continues to validate currentLimit without modeling current;
  this stage does not add physical I/O or new instrument capabilities.

Migration exposed a payload compatibility gap: power read-state uses
artest.schema.instrument.power-supply.result.v1. SDK 0.1.1 adds
Result::WithData(data, schemaId) and SchemaId(), retaining the one-argument
generic-JSON overload. The adapter writes the declared schema and service calls
preserve it. These are module-local C++ additions; no C ABI layout changed.

## Verification

The official Debug and Release builds run 161 Google Tests across 36 suites,
including 22 additions for this stage. Coverage includes independent state,
resource requirements, retry counters, invalid frames, service call order,
partial initialization, cancellation, timeout, shutdown failures, schema
round-trips, canonical/alias plans and activation of all four real DLLs.

The authoring boundary gate now also checks all reference packages for private
dependencies and handwritten ABI code. Their projects consume ARTestSDK.props
and enforce /W4 /WX. The obsolete ExtensionSupport/NativeSupport.h was removed;
its history remains in Git. Test-only ABI harnesses are intentionally retained.

The installed SDK ZIP consumer gate still runs. Manifests and schemas remain
external inputs to offline discovery; tests never require physical instruments.

## Deferred work

Automatic manifest/schema generation, typed service clients, new-project
generators, public licensing, signing, ABI freeze, managed runtime bridges and
ARTestStudio integration are separate work. D3.3-B does not infer a public SDK
release or completed manual acceptance.
