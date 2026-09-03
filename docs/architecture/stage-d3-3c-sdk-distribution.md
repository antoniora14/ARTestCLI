# D3.3-C - SDK distribution and external consumer validation

Status: implemented; manual acceptance pending. D3.3-B reference-extension
migration remains independent and is not implied by this stage.

## Outcome

The source-tree authoring facade can now be emitted as a self-contained,
versioned Windows x64 SDK directory and ZIP. A copied starter project consumes
only the extracted SDK and builds a Command plus simulated Instrument Driver.
The resulting DLL is packaged, integrity-checked, activated and executed through
the unchanged ARTestEngine API 0.4 and native ABI 0.1.

## Package contract

source/ARTest.SDK/sdk-version.json is the version authority. The package contains:

- public host, ABI and C++ authoring headers under include;
- the complete nlohmann/json single header under include/nlohmann;
- MSBuild integration under build/native;
- Engine result/event/catalog schemas under share/ARTest/schemas;
- developer and AI-agent guidance under docs;
- a runnable external extension template under templates;
- package-extension tooling and third-party notices;
- sdk-manifest.json with a sorted SHA-256 inventory.

Generated output lives only under artifacts/sdk-packages and is never source.
The packaging script stages into a unique directory, restricts replacement and
cleanup to that artifact root, then creates the directory and ZIP.

## Compatibility gate

scripts/test-sdk-distribution.ps1 validates the source manifest, file inventory,
every checksum and archive-entry path. It extracts the ZIP to a clean artifact
directory, copies the starter outside the installation, and builds it with
Visual Studio 18 Insiders/v145 using only ARTestSDKRoot.

The gate then uses ARTestCLI to:

1. activate and inspect the external package;
2. compile its plan without initializing an instrument;
3. execute a command-to-driver service call;
4. verify the computed result and guaranteed driver shutdown.

The official build runs this gate for its selected Debug or Release
configuration. The package remains experimental: passing one compiler/toolset
matrix is evidence for compatibility, not an ABI 1.0 freeze.

## Deliberate limits

- D3.3-B still owns migration of the existing ARTest reference extensions.
- The supported packaging target is Windows x64/MSBuild v145.
- CMake, NuGet/vcpkg publication, signing and update feeds are future work.
- The repository owner must select ARTest redistribution terms before a public
  community release. Third-party notices do not license ARTest code.
- Python and .NET runtimes remain separate bridge implementations.
