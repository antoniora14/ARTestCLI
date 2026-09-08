# ARTest SDK 0.3.0

This is the experimental C++20 SDK for trusted native ARTest extensions.
It targets Windows x64 and Visual Studio 18 Insiders with the v145 toolset.
Engine API 0.4 and native extension ABI 0.1 remain experimental.

## Start

1. Copy templates/ARTestExtension outside this package.
2. Create ARTestSDK.local.props next to the copied project (example below).
3. Open ARTestExtensionStarter.vcxproj in Visual Studio Insiders.
4. Rename all example IDs and implement the component behavior.
5. Build x64 Debug and Release. Each build generates, validates and publishes the package.

Machine-local SDK location, loaded before the SDK imports:

```xml
<Project>
  <PropertyGroup>
    <ARTestSDKRoot>D:\SDKs\ARTestSDK-0.3.0-windows-x64</ARTestSDKRoot>
  </PropertyGroup>
</Project>
```

This file is ignored by the starter's .gitignore. Install the complete SDK on
another PC and update this local path. Visual Studio alone does not supply the
ARTest headers or publishing tools. Use the matching Debug/Release SDK tools
and MSVC runtime; the SDK itself does not require the repository.

From Developer PowerShell:

    $sdk = 'D:\SDKs\ARTestSDK-0.3.0-windows-x64'
    $project = '.\ARTestExtensionStarter\ARTestExtensionStarter.vcxproj'
    msbuild $project /p:Configuration=Release /p:Platform=x64 "/p:ARTestSDKRoot=$sdk"

The project imports build/native/ARTestSDK.props; no Engine or Core library is
linked. The authoring adapter is compiled into the extension DLL and exposes only
the native C ABI. Extension.cpp owns IDs, aliases, requirements and Schema
declarations. ARTestMetadata.targets builds a metadata-only executable from that
same definition, validates the staged DLL through the Engine and safely publishes
out/extensions/x64/Configuration/ARTestExtensionStarter. Do not hand-edit output JSON.
TestPlan.json and MultipleInstruments.json are input sequences, not package metadata.

Read docs/extension-authoring.md, docs/ai-extension-authoring.md and
docs/metadata-generation.md. examples/ARTestSdkExample offers a power-supply
walkthrough; templates/ARTestExtension is the neutral project starter.
Both use the same generated-metadata flow. The bundled validator host and matching
Engine DLL are build tools; the extension and generator do not link to the Engine.
The complete file inventory and SHA-256 hashes are in sdk-manifest.json.

## Distribution status

This package is suitable for compatibility evaluation and extension development.
It is not an ABI 1.0 promise. ARTest project redistribution terms must be chosen
before a public SDK release; bundled nlohmann/json terms are recorded separately.
