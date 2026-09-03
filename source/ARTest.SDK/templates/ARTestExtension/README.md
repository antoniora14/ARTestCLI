# ARTest native extension starter

This runnable template contains one Command and one simulated Instrument Driver.
Remove the component you do not need only after updating the C++ definition,
manifest, schemas and plan consistently.

## Build

Set ARTestSDKRoot to an extracted ARTest SDK package:

    msbuild .\ARTestExtensionStarter.vcxproj /p:Configuration=Release /p:Platform=x64 "/p:ARTestSDKRoot=D:\SDKs\ARTestSDK-0.1.0-windows-x64"

The packaged extension is written below out/extensions by default. To test it:

    $cli = 'D:\ARTest\ARTestCLI.exe'
    $extensions = '.\out\extensions\x64\Release'
    & $cli compile '.\TestPlan.json' --extensions $extensions
    & $cli run '.\TestPlan.json' --extensions $extensions

Expected output includes Computed value 84.000000. and a PASSED summary.

## Customize safely

1. Replace every com.example identifier in C++, the manifest and schemas.
2. Keep the manifest and C++ descriptor metadata identical.
3. Keep constructors free of hardware access; acquire resources in Initialize.
4. Make Shutdown safe after partial initialization and cancellation.
5. Add strict schemas and local tests before using physical equipment.
6. Build Debug and Release, validate the catalog, then execute through the Engine.

The native ABI is experimental 0.1. Do not copy SDK detail headers or ABI tables
into component code.
