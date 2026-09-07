# Independent native compatibility kit

## Purpose and limits

This is the first measured compatibility slice, **not an ABI 1.0 freeze**.
A small host EXE and command/Instrument Driver DLL compile against an explicit
installed SDK, outside the repository. The resulting binaries are frozen and
reused against candidate Engine DLLs without recompiling either consumer.

SDK 0.2.1 and SDK 0.2.2 both expose Engine API 0.4 and native extension ABI 0.1.
Testing those SDK revisions does not demonstrate compatibility between different
ABI versions. The initial baseline is built now with the older SDK; it is not a
historical binary recovered from an earlier release. Preserve it unchanged to
obtain old-binary evidence for future Engines.

The current scope is Windows x64, C++20, MSVC v145. Python/.NET bridges, other
compilers/operating systems, 32-bit consumers, hardware transports and arbitrary
third-party DLLs are not validated. Each configuration must be tested explicitly.

## Prerequisites

- PowerShell 7 (pwsh), including for validation-only scripts.
- Baseline creation: Visual Studio Insiders with MSVC v145 and Windows SDK;
  a complete extracted ARTestSDK package with its original hash inventory.
  Native publication also uses Windows PowerShell.
- Validation only: a frozen baseline, candidate ARTestEngine.dll and matching
  MSVC runtime. No Visual Studio, SDK source, repository, vcpkg or network is
  required. A Debug Engine requires developer/debug runtime dependencies.
- Trusted native inputs only. The kit executes supplied DLLs in process.
  SHA-256 inventories detect accidental changes, not maliciously rewritten
  unsigned inventories. Protect baseline archives accordingly.

The default VS path is D:\Program Files\Microsoft Visual Studio\18\Insiders.
Override -VisualStudioPath for another installation.

## 1. Create a baseline once

Run from the repository root (or the extracted kit root):

~~~powershell
pwsh -File .\scripts\new-native-compatibility-baseline.ps1 `
  -SdkRoot .\artifacts\sdk-packages\x64\Release\ARTestSDK-0.2.1-windows-x64 `
  -OutputDirectory .\artifacts\compatibility\baselines\sdk-0.2.1-native-v1-x64-Release `
  -Configuration Release
~~~

In an extracted kit, provide the absolute path of the separately obtained SDK.
The kit never downloads an old SDK or substitutes a newer one.

The creation script:

1. Verifies the SDK's complete SHA-256 inventory.
2. Copies the SDK and fixture into an isolated temporary directory.
   The consumer projects have no repository includes, Engine/Core project
   references, vcpkg dependencies or private libraries.
3. Builds the extension and generates/validates its package with the old SDK's
   tools. The host's import library comes solely from the public
   ARTestEngine_QueryApi export name.
4. Runs all eight control scenarios with the old SDK's Engine.
5. Writes the frozen host, extension, exact source snapshot, control evidence,
   compiler version, SDK inventory hash and complete per-file inventory.

Temporary intermediates are deleted after a reparse-point audit. MSBuild may
emit MSB8029 because the intentionally fresh, non-incremental build is under the
temporary directory; C++ compilation still uses /W4 /WX.

The destination must not exist. **Never recreate a baseline to hide an
incompatibility.** If the fixture intentionally changes, create a separately
named baseline and retain the prior one. A partially published directory is not
valid without its matching inventory; preserve and inspect it before cleanup.
Compilation/control failures happen before baseline publication.

## 2. Validate without rebuilding the consumer

Build the candidate normally, then execute matrix cells separately:

~~~powershell
pwsh -File .\scripts\test-native-compatibility.ps1 `
  -BaselineDirectory .\artifacts\compatibility\baselines\sdk-0.2.1-native-v1-x64-Release `
  -Configuration Release

pwsh -File .\scripts\test-native-compatibility.ps1 `
  -BaselineDirectory .\artifacts\compatibility\baselines\sdk-0.2.1-native-v1-x64-Release `
  -Configuration Debug
~~~

Both commands reuse the same Release host and extension; only the Engine changes.
This measures Release-consumer/Release-Engine and Release-consumer/Debug-Engine.
A Debug consumer requires its own explicit baseline.

Outside the repository, supply the candidate and results paths:

~~~powershell
pwsh -File .\scripts\test-native-compatibility.ps1 `
  -BaselineDirectory C:\ARTestCompatibility\Baselines\sdk-0.2.1-native-v1-x64-Release `
  -EnginePath C:\ARTestCandidate\ARTestEngine.dll `
  -Configuration Release `
  -OutputDirectory C:\ARTestCompatibility\Results\candidate-001
~~~

Configuration is a caller-supplied label, not inferred from PE headers.
The SHA-256 identifies the actual candidate. The host reports its loaded Engine
path, which must match the candidate copied alongside it.

Validation never calls MSBuild or repairs a baseline. It checks baseline hashes
before/after execution, uses separate catalog copies, enforces a process timeout,
and checks exit code plus structured host evidence. Missing, changed or
unverifiable inputs cannot produce PASSED.

## Scenarios and acceptance criteria

| Scenario | Required evidence |
|---|---|
| Lifecycle | No initialization during preparation/compilation; two instances route values 11/29/11 through one command type; both clean up; restart creates a fresh lifecycle. |
| ServiceFailure | Service failure remains unsuccessful with its diagnostic; both drivers receive cleanup. |
| CleanupFailure | Cleanup failure cannot become PASSED; its diagnostic survives serialization. |
| Cancellation | Cancel after command entry; bounded interruption, cancelled verdict and cleanup. |
| Timeout | One timed-out step, bounded interruption, unsuccessful overall result and cleanup. |
| InvalidParameters | Negative wait rejected at compilation; no initialization and no executable invalid plan. |
| IncompatibleAbi | Copied manifest declaring ABI major 999 rejected before initialization. |
| IntegrityMismatch | Copied manifest with incorrect DLL digest rejected before initialization. |

These are native-process compatibility scenarios, not Google Test cases. They
complement the existing 183-test Google Test regression and C/C++ ABI layout
checks; neither substitutes for the other.

A successful matrix cell requires all eight scenarios and evidence guards to
pass. A fresh report directory is created for each run:

~~~text
artifacts/test-results/x64/<Configuration>/native-compatibility/<run-id>/
    compatibility.html
    compatibility.json
    compatibility.xml
~~~

HTML provides the summary, JSON contains full provenance and per-case evidence,
and XML is JUnit-compatible for CI. A negative scenario passes only when the
expected rejection occurs. Wrong provenance, nonzero host exit, timeout or
modified binaries fail validation with a nonzero PowerShell exit. Temporary
paths recorded in JSON identify the observed execution; scratch files are removed.

## Test the test harness

~~~powershell
pwsh -File .\scripts\test-native-compatibility-guards.ps1 `
  -BaselineDirectory .\artifacts\compatibility\baselines\sdk-0.2.1-native-v1-x64-Release `
  -EnginePath .\artifacts\bin\x64\Release\ARTestEngine.dll
~~~

Thirteen guards cover PowerShell relative paths, altered/missing/unexpected files, duplicate inventory
entries, unsupported/missing baselines, overwrite rejection, wrong provenance
despite exit 0, nonzero exit, the negative-case oracle, and FAILED report/exit
consistency. Deliberate FAILED scenario lines are expected inside this self-test;
the final guard summary must pass.

## Portable kit and evidence retention

From the repository, generate a source-only ZIP:

~~~powershell
pwsh -File .\scripts\package-native-compatibility-kit.ps1 `
  -OutputPath .\artifacts\compatibility\kits\ARTestCompatibilityKit-native-v1.zip
~~~

It contains the fixture, creation/validation/self-test scripts and this guide.
It excludes the Engine, SDK and frozen baseline: these are independent inputs.
Copy the complete baseline directory, including baseline.json, to the test PC.
Preserve the old SDK ZIP, frozen baseline and reports in a protected artifact
archive. Git stores sources/scripts only; normal builds do not regenerate a
missing historical baseline.

This kit does not replace the standard Debug/Release build commands.
Before any stability decision, extend the measured matrix to historical
consumers, supported toolchain/runtime configurations and a managed bridge mock.
Unexecuted combinations remain **not validated**, never implicitly supported.
