# ARTest SDK 0.1.0

This is the experimental D3.3-C C++20 SDK for trusted native ARTest extensions.
It targets Windows x64 and Visual Studio 18 Insiders with the v145 toolset.
Engine API 0.4 and native extension ABI 0.1 remain experimental.

## Start

1. Copy templates/ARTestExtension outside this package.
2. Open ARTestExtensionStarter.vcxproj.
3. Set ARTestSDKRoot to this extracted package directory.
4. Rename all example IDs and implement the component behavior.
5. Build x64 Debug and Release and validate the produced extension package.

From Developer PowerShell:

    $sdk = 'D:\SDKs\ARTestSDK-0.1.0-windows-x64'
    $project = '.\ARTestExtensionStarter\ARTestExtensionStarter.vcxproj'
    msbuild $project /p:Configuration=Release /p:Platform=x64 "/p:ARTestSDKRoot=$sdk"

The project imports build/native/ARTestSDK.props; no Engine or Core library is
linked. The authoring adapter is compiled into the extension DLL and exposes only
the native C ABI. tools/package-extension.ps1 packages the DLL, schemas and an
integrity hash after a successful build.

Read docs/extension-authoring.md and docs/ai-extension-authoring.md.
The complete file inventory and SHA-256 hashes are in sdk-manifest.json.

## Distribution status

This package is suitable for compatibility evaluation and extension development.
It is not an ABI 1.0 promise. ARTest project redistribution terms must be chosen
before a public SDK release; bundled nlohmann/json terms are recorded separately.
