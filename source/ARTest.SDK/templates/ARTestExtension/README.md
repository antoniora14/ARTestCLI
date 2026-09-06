# ARTest native extension starter

This runnable template contains one Command and one simulated Instrument Driver.
Remove the component you do not need from the C++ definition and update the plan.
Extension.cpp is the only source of package metadata. Do not create or edit
artest-extension.json or schema files manually.

## Build

Set ARTestSDKRoot to an extracted ARTest SDK package:

    msbuild .\ARTestExtensionStarter.vcxproj /p:Configuration=Release /p:Platform=x64 "/p:ARTestSDKRoot=D:\SDKs\ARTestSDK-0.2.2-windows-x64"

For Visual Studio Insiders, create ARTestSDK.local.props beside this project:

```xml
<Project>
  <PropertyGroup>
    <ARTestSDKRoot>D:\SDKs\ARTestSDK-0.2.2-windows-x64</ARTestSDKRoot>
  </PropertyGroup>
</Project>
```

Then open ARTestExtensionStarter.vcxproj and build x64. The local props file is
loaded early enough to resolve SDK imports; do not put this setting only in
.vcxproj.user. On another PC install the complete SDK and update the local path.
The extension does not link Engine/Core; generation uses the SDK's bundled
validator and matching Engine as build tools.

The packaged extension is written below out/extensions by default. To test it:

    $cli = 'D:\ARTest\ARTestCLI.exe'
    $extensions = '.\out\extensions\x64\Release'
    & $cli compile '.\TestPlan.json' --extensions $extensions
    & $cli run '.\TestPlan.json' --extensions $extensions

Expected output includes Computed value 84.000000. and a PASSED summary.
Run MultipleInstruments.json instead to use the same command with ValueSource1,
ValueSource2, ValueSource1. Expected values are 24, 48, 24 with three passed steps.

## Customize safely

1. Replace every com.example identifier in C++ and the input plans.
2. Declare Schema, aliases and requiredContracts in Extension.cpp. The build
   generates JSON from this same definition, validates the DLL and publishes
   only a complete package. Stop running consumers before rebuilding.
3. Keep constructors free of hardware access; acquire resources in Initialize.
4. Make Shutdown safe after partial initialization and cancellation.
5. Add strict schemas and local tests before using physical equipment.
6. Build Debug and Release, validate the catalog, then execute through the Engine.

The native ABI is experimental 0.1. Do not copy SDK detail headers or ABI tables
into component code.
