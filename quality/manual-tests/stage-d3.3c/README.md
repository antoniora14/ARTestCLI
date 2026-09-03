# D3.3-C manual acceptance - SDK distribution

Status: pending manual execution. Record real results in the Word evidence report.
Do not modify earlier reports. No physical equipment is required.

Run PowerShell from D:\GitHub\main\ARTestCLI.

## MT-D3.3C-001 - Generate and inspect the SDK package

1. Run:

       .\scripts\package-sdk.ps1 -Configuration Release -Platform x64
       $LASTEXITCODE

   Expected: exit 0 and paths to the SDK directory and ZIP.

2. Open:

       artifacts\sdk-packages\x64\Release\ARTestSDK-0.1.0-windows-x64

   Expected: include, build, docs, share, templates and tools are present, plus
   README.md, THIRD_PARTY_NOTICES.md, sdk-version.json and sdk-manifest.json.

3. Inspect both JSON files.

   Expected: SDK 0.1.0, Engine API 0.4, native ABI 0.1, experimental stability
   and one SHA-256 inventory entry per payload file.

## MT-D3.3C-002 - Validate an installed external consumer

1. Run:

       .\scripts\test-sdk-distribution.ps1 -Configuration Release -Platform x64
       $LASTEXITCODE

   Expected: exit 0; source verification, package/hash/archive checks and the
   copied /W4 /WX consumer build all pass.

2. Inspect the doctor and compile output.

   Expected: catalog status active, integrity verified and offline compilation
   says no instruments were initialized.

3. Inspect execution output.

   Expected: Computed value 84.000000., driver shutdown, final PASSED.

4. Inspect artifacts\sdk-consumer-tests\x64\Release.

   Expected: installed SDK and external extension project prove the extracted
   package works from independent paths containing spaces.

## MT-D3.3C-003 - Official Release regression and scope

1. Run:

       .\scripts\build.ps1 -Configuration Release -Platform x64

   Expected: all architecture gates, the external consumer and all 139 Google
   Tests pass.

2. Open artifacts\test-results\x64\Release\ARTestCLI.UnitTests.html.

   Expected: PASSED; Total 139; Passed 139; Failed 0; Skipped 0.

3. Review source\ARTest.SDK\sdk-version.json and
   docs\architecture\stage-d3-3c-sdk-distribution.md.

   Expected: ABI remains experimental 0.1 and deferred release work is explicit.

## Acceptance

All three cases must be Passed and supported by recorded evidence. The template
must remain Pending until the tester performs these steps.
