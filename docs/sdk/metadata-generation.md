# C++ metadata generation

D3.4.1 implements the first complete generated example in SDK 0.2.0.
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

The solution still has ten top-level projects. The example invokes its small
MetadataGenerator.vcxproj as a build utility; it has no Engine/Core linkage.
Both DLL and generator compile with C++20 and /W4 /WX. The current example hook
runs generation after linking, then reuses package-extension.ps1.

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
The full Debug/Release regression has 168 tests and also checks the installed SDK.

## Publication and remaining deliveries

D3.4.1 uses an isolated temporary metadata directory, removed after packaging.
Generation failures stop the build before touching the deployed package. The
existing packager still performs sequential file copies: failure during package
replacement is not yet a transactional update. Consumers must not execute a
package while it is being rebuilt.

D3.4.2 will move this example-specific hook into reusable installed-SDK MSBuild
targets, add robust publication/recovery and stale-file pruning, and validate
generated files against the actual DLL before publishing. Offline discovery must
remain side-effect free; explicit post-build binary checks use trusted code.

D3.4.3 will migrate the four reference packages and the installed starter, removing
only their replaced source manifests/schemas and validating external generation.
The installed SDK already carries the new header API, but its starter retains
manual JSON until that migration. Python/.NET backends and ABI freeze are later
work; schemas and component identities remain transport-independent.

