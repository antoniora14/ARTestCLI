# ARTest development kit (PY-DX-01 Stages 4A and 4B)

Stage 4A produced the inventory-checked Windows x64 evaluation kit. The Stage 4B
candidate adds guided `new` and `build` dispatch for Python and C++ without a
repository checkout, internal IDs, a global Python installation, or downloads.
It reuses the native SDK distribution and accepted Python Stages 1-3 tools.

## Build the local candidate

Build Release first. Supply an already installed standard GIL-enabled CPython
3.13 Windows x64 interpreter and a directory containing the exact unmodified
Windows x64 wheels pinned by `source/ARTest.Python/requirements.lock`. Packaging
does not download them or modify the supplied interpreter.

```powershell
.\scripts\build.ps1 -Configuration Release -Platform x64
.\scripts\package-development-kit.ps1 `
  -Configuration Release -Platform x64 `
  -PythonRuntime 'C:\Python313\python.exe' `
  -DependencyWheelRoot 'C:\PreparedInputs\wheels'
```

The version authority is
`source/ARTest.SDK/development-kit/development-kit-version.json`. Its kit version
is independent of native SDK 0.4.0, Python SDK 0.2.0, Engine API 0.4, native ABI
0.2 and the exact CPython patch version recorded in the generated manifest.
Outputs are generated only under:

```text
artifacts/sdk-packages/x64/Release/
  ARTestDevelopmentKit-0.2.0-evaluation-windows-x64/
  ARTestDevelopmentKit-0.2.0-evaluation-windows-x64.zip
```

The packager probes and then copies a complete installed CPython layout needed by
`venv` and `ensurepip`; it does not accept an arbitrary embeddable ZIP. It removes
global `site-packages`, creates a real staged venv, checks pip, builds the SDK
wheel, verifies the pinned dependency wheel hashes, nests the existing native SDK,
and includes the matching Release CLI/Engine plus app-local supported CRT files.

## Extract, verify and use

Preserve the ZIP and its SHA-256. Extract it to a new directory; the focused gate
checks every archive entry for rooted paths, `..`, duplicates and link entries
before extraction. From any other current directory run:

```powershell
& 'D:\SDKs with spaces\ARTest Development Kit\artest.ps1' verify
```

`verify` checks the exact complete SHA-256 inventory, nested component versions,
private CPython identity, and a new venv's pip. Diagnostics distinguish missing
inventory (`ARTESTKIT001`), malformed/unsafe content (`ARTESTKIT002`), missing
components (`ARTESTKIT003`), corrupt files (`ARTESTKIT004`), unexpected files
(`ARTESTKIT005`), incompatible components (`ARTESTKIT006`) and missing venv/pip
capability (`ARTESTKIT007`). Do not repair a failed kit in place; rebuild it.

## Guided create and build

From any working directory, supply the three normal inputs. The folder is an
existing parent directory; `new` creates a child directory named after the
project and refuses any existing destination.

```powershell
$entry = 'D:\SDKs with spaces\ARTest Development Kit\artest.ps1'
& $entry new --name 'My Python extension' --folder 'D:\Work' --language python
& $entry build --project 'D:\Work\My Python extension'

& $entry new --name 'My native extension' --folder 'D:\Work' --language cpp
& $entry build --project 'D:\Work\My native extension'
```

Omit the normal arguments for an interactive prompt. `driver-command` is the
default; `--variant driver-only` and `--variant command-only` select the other
approved starters. `--author` is optional. C++ discovery can be made explicit
with `--msbuild`; the selected tool is retained only in ignored local config.
C# is rejected with a message explaining the pending .NET gates.

Python `build` performs Stage 2 checking and Stage 3 preparation/reuse. C++
`build` invokes the generated `.vcxproj` using the existing SDK props/targets and
metadata publisher. The project remains directly buildable in Visual Studio.
Generated `artest-sdk-project.json`, component IDs and plan references are
portable. Private Python, wheel, CLI, native SDK and MSBuild paths are local-only.

The accepted low-level Python workflow is preserved:

```powershell
$entry = 'D:\SDKs with spaces\ARTest Development Kit\artest.ps1'
& $entry python-project create 'D:\Work\My Python extension' `
  --extension-id com.example.my-extension `
  --driver-id com.example.driver.simulated-source `
  --command-id com.example.command.measure-value `
  --author 'Example Lab'
& $entry python-project check 'D:\Work\My Python extension'
& $entry python-project prepare 'D:\Work\My Python extension'
```

After successful creation the entry writes only that project's ignored
`artest-project.local.json`, pointing to the private interpreter, SDK wheel and
evaluation CLI by absolute paths derived from the extracted kit. Preparation uses
the adjacent inventoried wheelhouse with `pip --no-index --find-links`; no global
environment changes occur. The venv is created directly inside its final immutable
Stage 3 revision and is never moved or patched. An explicit compatible local
interpreter remains available through the underlying low-level configuration.

Run the complete focused gate with the same prepared inputs:

```powershell
.\scripts\test-development-kit.ps1 `
  -PythonRuntime 'C:\Python313\python.exe' `
  -DependencyWheelRoot 'C:\PreparedInputs\wheels'
```

It retains candidate identity, hashes, commands and results under
`artifacts/acceptance/py-dx-01/stage4a-candidate/<run-id>/`. Generated kits,
prepared environments and acceptance evidence are not source changes.

## Boundary and redistribution status

This implements only the Stage 4B candidate over accepted Stage 4A. It does not
implement Stage 4C installation registration, Stage 4 plan execution, or Stage 5
acceptance. Existing C++ consumers continue to use the unchanged nested native
SDK and v145 toolchain. C# remains unavailable.

The artifact is for local evaluation, not a public release. Review
`THIRD_PARTY_NOTICES.md`, the private runtime's `LICENSE.txt`, embedded wheel
license metadata and Microsoft redistribution rights before any publication.
ARTest licensing, signing, feeds and a public download remain separate decisions.
