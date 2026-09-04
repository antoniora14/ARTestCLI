# Package and validate the ARTest SDK

Run from the repository root:

    .\scripts\package-sdk.ps1 -Configuration Release -Platform x64

Outputs:

    artifacts\sdk-packages\x64\Release\ARTestSDK-0.1.1-windows-x64\
    artifacts\sdk-packages\x64\Release\ARTestSDK-0.1.1-windows-x64.zip

For the complete compatibility gate:

    .\scripts\test-sdk-distribution.ps1 -Configuration Release -Platform x64

The gate verifies SHA-256 inventory and safe archive paths, extracts the ZIP,
copies its starter project outside the installation, builds with only the
installed headers and props, and executes the resulting DLL through ARTestCLI.
No physical equipment is used.

Use sdk-version.json as the contract authority and sdk-manifest.json as the
inventory authority. Do not manually edit a generated package. Rebuild it from
source. A package is not accepted when its checksum inventory, external build,
catalog activation, offline compilation, execution or cleanup validation fails.

The SDK currently supports Windows x64, Visual Studio 18 Insiders/v145 and
C++20. ABI 0.1 remains experimental. Before public community distribution, the
repository owner must choose explicit licensing terms for ARTest itself.
