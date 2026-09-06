# C++ metadata generation

D3.4.1 introduced generated definitions; D3.4.2 completes reusable build
integration and safe package publication in SDK 0.2.1.
A developer declares component identity and schemas in C++; building the example
produces the deployable manifest and schema files. Engine API 0.4, extension ABI
0.1 and manifest version 2 remain unchanged.

## Architectural contract for D3.4

The same Extension definition is the authority for native descriptors and
build-time metadata. CommandInfo and DriverInfo retain their existing fields and
add ComponentMetadata. Existing consumers can keep source JSON until migration.

The layers are intentionally separate:

| Layer | Responsibility |
| --- | --- |
| Schema | Owned, declarative schema values and validation of supported constraints |
| Extension and ComponentMetadata | Component identities, versions, schema bindings, aliases and configured service requirements |
| GenerateMetadata | Pure deterministic projection to a manifest and schema documents |
| Build executable | Calls the shared definition, serializes the bundle, reports errors and exits |
| Build publisher | Writes generated files, packages the DLL and calculates its digest |
| Engine | Discovers offline, validates the package, checks native descriptors at activation, executes |

The runtime definition and metadata executable compile the same ExampleExtension.cpp.
The executable registers factory pointers but never invokes them. It does not
LoadLibrary the extension, initialize instruments or invoke commands. Definitions
and global initialization must therefore remain free of device I/O. Developer
code executed during a build is trusted code, not a sandbox.

The pure projection owns no filesystem state and imports no Engine/Core headers.
C++ objects remain local to their module; no new export or binary layout is required.
Metadata may be declared near a component and passed into its registration, but
it must remain accessible without constructing an instance.

## Authoring example

In source/ARTest.SDK/examples/ARTestSdkExample/ExampleExtension.cpp:

    using artest::sdk::Schema;
    artest::sdk::Extension extension{
        "com.artest.extension.sdk-example", "0.1.0", "SDK authoring example", "ARTest"};

    extension.AddCommand<artest::examples::ReadVoltageCommand>({
        .id = "com.artest.command.sdk.read-voltage",
        .name = "Read voltage",
        .metadata = {
            .schema = Schema::Object()
                .Required("channel", Schema::Integer().Minimum(1).Maximum(4))
                .Optional("settleMs", Schema::Integer().Minimum(0).Maximum(60000)),
            .schemaId = "artest.schema.sdk.read-voltage.parameters.v1",
            .requiredContracts = {"artest.contract.instrument.power-supply.v1"}}});

A driver's metadata.schema describes configuration; a command's describes
parameters. Every generated component requires an object schema, even if empty.
Objects reject additional properties by default. Optional does not inject a
default value: runtime code still uses Parameters.Optional when needed.

Supported constructors are Object, Integer, Number, Boolean, String and Array.
The builder supports Required/Optional properties, numeric Minimum/Maximum,
MinLength/MaxLength, MinItems/MaxItems, Description and AllowAdditionalProperties.
Nested schemas are copied, so later changes to a builder cannot mutate a parent.
Bounds currently use double precision; this first slice is not an exact 64-bit
integer-bound authoring API. Contradictory bounds, duplicate properties, invalid
keyword/type combinations, non-finite bounds, excess depth and excess size fail.

The generated keywords are a subset of ARTest Schema Profile 1. Arbitrary JSON,
enum/default authoring and custom schema imports are not implemented in this
slice; any future escape hatch must pass the same profile validator. Runtime
semantic checks remain necessary for relationships such as CAN DLC/data length.

## Identity and output rules

- Component version inherits the extension version when omitted.
- A schema ID defaults to componentId.parameters.v1 or componentId.configuration.v1.
  Supply schemaId explicitly to preserve an established contract. The schema
  revision is independent of the component's implementation version.
- Filenames are schemas/componentId.parameters.json or configuration.json.
  Paths are generated, never accepted from component authors.
- IDs use lowercase alphanumeric segments separated by dots or hyphens and are
  limited to 160 characters in this generator. Versions currently use numeric
  major.minor.patch; prerelease version authoring is deferred.
- Aliases remain optional and case-sensitive. Duplicate IDs, aliases and schema
  IDs fail; aliases cannot shadow another component in the package.
- Configured command requirements are declared explicitly. The generator does
  not infer contracts from Execute or invent external drivers.
- The primary contract supplies the capability; DriverMode supplies the flags.
  Additional capability/concurrency declarations require a later explicit design.
- Native/x64/inProcess and ABI 0.1 are fixed by this first build backend. Future
  backends will accept validated build/runtime descriptors, keeping runtime
  selection out of command execution logic.
- Component, alias and requirement order is canonicalized. JSON object keys are
  stable; metadata includes no timestamp, absolute checkout path or binary hash.
  SHA-256 is calculated only after the final DLL is available.

The generated bundle is an internal build interchange, not a new runtime ABI.
Its console envelope uses ASCII escapes to preserve Unicode across Windows code
pages; generated files contain UTF-8. Unknown generator versions fail closed.

## Build and run

From D:\GitHub\main\ARTestCLI:

    .\scripts\build.ps1 -Configuration Release -Platform x64

The solution has eleven projects, including the ARTestSdkValidate build tool.
After linking, ARTestMetadata.targets builds the same extension project as a
metadata executable with ARTEST_METADATA_GENERATOR defined. The child build
isolates intermediate/output directories and clears solution dependency state.
It does not rebuild dependency projects as executables. Both DLL and generator
compile with C++20 and /W4 /WX; neither links to Engine/Core.

The output is under artifacts/sdk-examples/x64/Release/ARTestSdkExample:
ARTestSdkExample.dll, artest-extension.json and two generated schema files.
ExamplePlan.json remains source because it is a user-authored test sequence,
not component metadata.

    $cli = '.\artifacts\bin\x64\Release\ARTestCLI.exe'
    $catalog = '.\artifacts\sdk-examples\x64\Release'
    $plan = '.\source\ARTest.SDK\examples\ARTestSdkExample\ExamplePlan.json'
    & $cli compile $plan --extensions $catalog
    & $cli run $plan --extensions $catalog

Expected: compile exit 0 with no instrument initialization; run exit 0,
Measured 12.000000 V., driver shutdown and final PASSED. The automated regression
already exercises these paths, invalid channel rejection and cancellation.

    .\artifacts\bin\x64\Release\ARTestCLI.UnitTests.exe --gtest_filter=SdkMetadataTests.*

Seven focused tests cover Engine schema conformance, local ownership, metadata
errors, identity collisions, deterministic output and no component construction.
The full Debug/Release regression has 180 tests and also checks the installed SDK.

## Reusable installed-SDK integration

Copy examples/ARTestSdkExample from the extracted SDK to your own project folder.
Pass ARTestSDKRoot pointing to that SDK and build its vcxproj. No handwritten
manifest, schema files or second generator project are required.

For another DLL project:

1. Import build/native/ARTestSDK.props after Microsoft.Cpp.props.
2. Keep one definition function and select ARTEST_GENERATE_METADATA versus
   ARTEST_EXPORT_EXTENSION using ARTEST_METADATA_GENERATOR, as in the example.
3. Import build/native/ARTestMetadata.targets after Microsoft.Cpp.targets.
4. Remove the superseded manual packaging hook. Any other custom build hooks must
   skip the child build when ARTestMetadataBuild=true.
5. Select an output package directory separate from source and link outputs.

| Property | Default / meaning |
| --- | --- |
| ARTestSDKRoot | Extracted SDK root |
| ARTestPackageRoot | ProjectDir/out/extensions/Platform/Configuration |
| ARTestPackageDirectory | ARTestPackageRoot/TargetName; named owned leaf |
| ARTestMetadataDirectory | IntDir/metadata; private generator build products |
| ARTestToolTimeoutSeconds | 120 seconds per generator/validator process |
| ARTestAdoptLegacyPackage | Empty/false; explicit true permits narrow legacy adoption |

Legacy adoption checks extension identity and the exact manifest/DLL/schema file
set. It is not permission to erase arbitrary folders. The repository example
opts in only to migrate D3.4.1 output; normal generated packages carry ownership.
Do not place user notes or evidence inside a generated package directory.

The SDK ships tools/ARTestSdkValidate.exe with its matching ARTestEngine.dll.
The tool uses public Engine API 0.4: metadata-only ValidateCatalog, then explicit
RefreshCatalog. It checks schema-profile compliance, integrity and native IDs,
versions, kinds, flags and contracts. Descriptor enumeration is matched by ID,
not position. This centralizes validation instead of duplicating Engine rules.
The bundled Engine is a build-tool dependency, not a dependency developers must
link into their extensions. The tool and DLL require the corresponding MSVC
runtime for the selected Debug/Release SDK configuration.

Inspection loads trusted native code, queries descriptors and creates the
extension container, but never creates command/driver instances or starts a
session. DllMain and definition code still execute: this is not a sandbox or a
hardware-behavior certification. The current package backend stages one DLL and
its schemas; additional vendor DLL deployment needs explicit future support.

## Safe publication and recovery

The publisher holds an exclusive per-package writer lock, generates metadata,
stages a complete package in a sibling transaction directory and computes the
copied DLL's SHA-256. It requires both validator exit code zero and a boolean
valid=true result. Invalid output, a crash or a timeout stops publication.

Before staging, a flushed ownership journal records the destination and prior
file inventory. Only after validation is the existing package renamed to backup;
the complete staged directory is then renamed to the destination. It never
overlays new files over old files. Retired schemas disappear with the old owned
generation.

Recovery on the next publication attempt is conservative:

- Backup present and destination absent: verify and restore the old generation.
- Backup and destination present: verify the published generation before retiring
  the old backup.
- Validation failed before renames: discard only the owned staging transaction.
- Corrupt ownership/journal, changed files, unknown user files or reparse points:
  fail closed and preserve data for inspection.

A normal promotion failure attempts rollback immediately. The transaction
directory is removed after successful completion/recovery. The empty sibling
.PackageName.artest-publish.lock file intentionally remains; deleting lock files
would introduce a concurrency race. The package's .artest-generated-package.json
owns its exact file inventory. Neither file is obsolete build garbage.

Diagnostic codes ARTESTPKG_* identify publication and recovery; errors include the
Engine's catalog diagnostics. The build log is the evidence, separate from runtime
fault logging. Recovery repairs publication, not a running Engine's catalog.

There is a brief name-availability gap between directory renames. Stop consumers
before rebuilding: this is crash-recoverable replacement, not hot reload, an
atomic directory-exchange guarantee, a signature or durable recovery from storage
hardware failure/power loss. Do not automatically delete a corrupt transaction;
inspect its journal and preserve a backup before any manual intervention.

Twelve Google Test publication cases cover successful replacement/stale pruning,
generator/DLL/schema/verdict failures, reordered descriptors, failed promotion,
two forcibly killed publisher subprocesses, writer exclusion, foreign files and
timeout. The installed-SDK gate builds and executes the generated example from
an extracted SDK. There is no additional manual acceptance report for this slice.

## Remaining delivery

D3.4.3 migrates the four reference packages and the legacy installed starter.
Their old package-extension.ps1 remains in use until then and does not acquire
this new publisher's guarantees merely because the SDK was upgraded.
Python/.NET backends and ABI freeze remain later work.
