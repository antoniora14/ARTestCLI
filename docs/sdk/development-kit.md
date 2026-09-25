# ARTest development kit (PY-DX-01 Stages 4A, 4B, 4C, 4 and 5)

Stage 4A produced the inventory-checked Windows x64 evaluation kit. Stage 4 adds
explicit Python Test plan execution to the accepted installation-scoped `new`,
`build`, and `register` flow without a
repository checkout, internal IDs, a global Python installation, or downloads.
It reuses the native SDK distribution and accepted Python Stages 1-3 tools.
The extracted kit includes `FIRST_USE.md`, the short Stage 5 exercise for a new
engineer. Stage 5 and PY-DX-01 are accepted with documented usability deferrals;
see [the closure record](../architecture/py-dx-01-closure.md).

ARTestDev is planned separately in [DEV-01](../architecture/artestdev-initial-scope.md).
It is not included in this accepted ZIP; the commands below remain the available
workflow until the new graphical kit passes its own acceptance. The new DEV-01
kit will exclude ARTestCLI/Engine and Python runtimes, use manually installed
prerequisites and allow a selected SDK installation folder. These requirements do
not describe or change the historical ZIP documented below.

## Start here: accepted SDK and authoring guide

This is the canonical current guide for the Python/C++ developer workflow.
Use `FIRST_USE.md` inside the extracted ZIP for the step-by-step starter exercise,
then the API guides for real driver/command logic: [Python](python-extension-authoring.md)
and [C++](extension-authoring.md).

The tested local artifact is:

```text
artifacts/acceptance/py-dx-01/stage5-candidate/20260921T004123Z-9c977c49/ARTestDevelopmentKit-0.4.0-evaluation-windows-x64.zip
SHA-256: 3f14011c33511e7b4bd78588573eea29dd2b44da1c98b0cb9bca9b01c7bc989b
```

Send that complete ZIP to the developer. It is not committed to Git or published
as a release; cloning the repository does not download it. The archived ZIP and
its original evidence remain immutable; pending labels inside it are historical.

First-use clarifications from human observation:

- Open PowerShell 7 (`pwsh`), not Windows PowerShell 5.1. Installation is separate;
  the current kit does not provide an early compatible-shell diagnostic.
- Choose short working and target paths. `$env:TEMP` in the exercise is only
  the current directory, to verify independence from the project directory.
- In Python, edit the result-message occurrence of `Minimum simulated value check`
  in the command implementation, not the description in `Extension(...)`.
- In the C++ driver-command starter, edit `ReadValueCommand.h` for command behavior
  and `SimulatedValueSource.h` for driver behavior. `Extension.cpp` defines metadata;
  the current `new` edit hint points there.
- Python build reports preparation reuse. C++ build need not return a `reused`
  field; repeated registration has its own reuse result.

These clarifications do not change the accepted kit or its contracts.

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
  ARTestDevelopmentKit-0.4.0-evaluation-windows-x64/
  ARTestDevelopmentKit-0.4.0-evaluation-windows-x64.zip
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

## Register with an installation

The first registration names and verifies the target CLI plus separate writable
catalog and configuration locations. That selection is retained only in the
authoring tool's local configuration; it is not an Engine registry.

```powershell
$entry = 'D:\SDKs with spaces\ARTest Development Kit\artest.ps1'
& $entry register --project 'D:\Work\My Python extension' `
  --target station-a `
  --cli 'C:\Program Files\ARTest\ARTestCLI.exe' `
  --catalog 'D:\ARTest Data\extensions' `
  --config 'D:\ARTest Data\configuration'

& $entry register --project 'D:\Work\My native extension' --target station-a
```

`register` checks/builds current sources, retains immutable revisions, validates
the complete candidate catalog, and switches one active revision per extension
ID. Python environments are prepared directly below the target configuration;
they are never moved or patched. Existing packages and mappings are preserved.
Repeating an identical registration is a verified no-op. Conflicts, incompatible
targets, locks, permission failures, and failed or interrupted publication leave
the prior catalog and selected profile usable without elevation or fallback.

The destination CLI independently runs `extensions validate` and offline
`compile --extensions ... --python-environments ...`. Registration never runs a
plan, initializes hardware, replaces CLI/Engine binaries, or reloads an active
session. Custom hosts still consume their documented catalog configuration.

## Explicitly run a Python Test plan

Run is separate from registration and must be requested explicitly. It uses the
selected installation profile, or the named `--target`, for the CLI and delegates
the ordered preparation, offline validation, and execution flow to the Python
project tool:

```powershell
$entry = 'D:\SDKs with spaces\ARTest Development Kit\artest.ps1'
& $entry run --project 'D:\Work\My Python extension' --target station-a --mode sources
$LASTEXITCODE
```

Omit `--target` to reuse the profile selected by registration. `run` does not
register, publish, or modify the installation profile, catalog, or associations.
`--mode sources` is the default. It prepares the current source project into a
project-owned immutable revision, then creates or reuses an immutable project-local
execution catalog. That local catalog contains the selected installation's
registered packages and associations, with only this project's extension replaced
by its prepared local revision. This lets a local Python Test script use a driver
already registered in another package without changing the target.

Use `--mode registered` to compile and execute the selected target's already
registered revision directly. This mode does not prepare or copy current Test
script sources and fails if the project's extension ID is not present in the
selected catalog and Python association. Both modes snapshot their catalog and
association inputs, compile the Test plan offline, verify that those inputs remain
current, and only then execute. C++ keeps its existing native CLI workflow; this
Python-specific command does not add another native execution path.

Edit the Python Test script or Test plan and invoke the same command again.
Identical inputs reuse preparation. A Test script edit prepares a new revision; a
Test plan-only edit reuses the environment and is compiled again. CLI diagnostics,
final run-result JSON, and exit codes are preserved. Failed prerequisites,
preparation, or offline validation stop before execution; runtime failure,
cancellation, and indeterminate effects are not retried.

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

Run the Stage 4C extracted-kit registration gate with the same inputs and the
documented native toolchain:

```powershell
.\scripts\test-development-kit-stage4c.ps1 `
  -PythonRuntime 'C:\Python313\python.exe' `
  -DependencyWheelRoot 'C:\PreparedInputs\wheels'
```

Run the Stage 4 Debug/Release project-execution gate with those same prepared
inputs:

```powershell
.\scripts\test-python-project-stage4.ps1 `
  -PythonRuntime 'C:\Python313\python.exe' `
  -DependencyWheelRoot 'C:\PreparedInputs\wheels'
```

It retains candidate identity, hashes, commands and results under
`artifacts/acceptance/py-dx-01/stage4a-candidate/<run-id>/`. Generated kits,
prepared environments and acceptance evidence are not source changes. Stage 4C
records candidate identity, hashes, commands and results under
`artifacts/acceptance/py-dx-01/stage4c-candidate/<run-id>/`.
Stage 4 records the kit identity, Debug/Release results, unchanged/edited
preparation identities, commands, logs, and preservation checks under
`artifacts/acceptance/py-dx-01/stage4-candidate/<run-id>/`.

## Boundary and redistribution status

This implements the accepted Stage 4 flow over accepted Stages 4A, 4B, and 4C
and supplies the Stage 5 acceptance guide. Stage 5 and overall PY-DX-01 are
accepted with the owner-approved deferrals recorded in the closure document. Existing C++ consumers continue to use the unchanged nested native
SDK and v145 toolchain. C# remains unavailable.

The artifact is for local evaluation, not a public release. Review
`THIRD_PARTY_NOTICES.md`, the private runtime's `LICENSE.txt`, embedded wheel
license metadata and Microsoft redistribution rights before any publication.
ARTest licensing, signing, feeds and a public download remain separate decisions.
