# Create a minimal Python extension project

PY-DX-01 Stages 1 and 2 provide a hardware-free project scaffold, static
configuration validation, explicit local-prerequisite checks and an optional
generated plan copy with machine-local vendor paths. They do not prepare an
environment, install packages, compile a plan or run ARTest.

## Create and inspect a project

From the ARTestCLI repository root, choose an empty destination whose parent
directory already exists:

    & 'C:\Path\To\Python313\python.exe' -I -B source/ARTest.Python/tools/project.py create D:\Work\MyPythonExtension `
        --extension-id com.acme.lab.minimal `
        --driver-id com.acme.lab.driver.simulated-source `
        --command-id com.acme.lab.command.measure-value `
        --author 'Acme Lab'
    & 'C:\Path\To\Python313\python.exe' -I -B source/ARTest.Python/tools/project.py validate D:\Work\MyPythonExtension

`create` rejects a nonempty destination and publishes the complete scaffold only
after its bundled configuration validates. It does not import the generated
extension or `artest_sdk`. It also does not start another process, use the network,
install dependencies or access hardware. Existing files are never overwritten.
The author must supply distinct extension, driver and command IDs using the
lower-case stable-ID form accepted by current extension manifests, plus a
nonempty publisher/author name. The command applies those IDs consistently to
the Python definition and plan. Its simulated contract and schema IDs are derived
from the author-owned extension ID.

`validate` reads configuration only. Its output shows the paths resolved from the
project root, even when the command is invoked from another current directory.
It verifies JSON structure, configuration versions, entry-point syntax, referenced
path kinds and portable-path containment. It does not verify or execute the
configured Python, inspect the SDK wheel/CLI, or require vendor dependencies.
Use the separate `check` operation when those local prerequisites are available.

## Generated layout

    MyPythonExtension/
      .gitignore
      artest-project.json
      artest-project.local.example.json
      requirements.lock
      src/
        extension.py
      plan/
        measurement.json

`src/extension.py` contains a simulated driver and a command that reaches the
driver through `context.instrument(...)` and the existing broker service surface.
The IDs in that definition match the driver and command references in the plan.
They are the author-owned values supplied to `create`; the author remains
responsible for their global uniqueness. The contract and schema IDs share the
supplied extension-ID namespace.

`requirements.lock` is the current ARTest Python exact-version, hashed lock. Do
not replace it with unpinned requirements or use Stage 1 as a dependency resolver.

## Portable configuration

`artest-project.json` is source-controlled and has schema version 1:

```json
{
  "schemaVersion": 1,
  "sourceDirectory": "src",
  "entryPoint": "extension:define_extension",
  "dependencyLock": "requirements.lock",
  "plan": "plan/measurement.json"
}
```

All four paths or references are portable project inputs. File paths must be
relative, must exist with the expected file or directory kind, and must resolve
inside the project root. Absolute paths, `..` escapes and links that resolve
outside the project are rejected. Entry points use
`dotted.module:callable` syntax; validation does not import that module.

## Local configuration

`artest-project.local.example.json` documents the separate machine-local shape.
Copy it to `artest-project.local.json` only when local values are available:

```json
{
  "schemaVersion": 1,
  "python": "C:/Tools/Python313/python.exe",
  "sdkWheel": "C:/ARTest/artest_python-0.2.0-py3-none-any.whl",
  "cliExecutable": "C:/ARTest/ARTestCLI.exe",
  "vendorPaths": {
    "exampleSdk": "C:/Vendor/bin/vendor.dll"
  },
  "vendorDlls": ["exampleSdk"],
  "planBindings": []
}
```

These paths may be absolute. Relative local paths are resolved from the project
root but are not constrained to remain there. `validate` checks only their JSON
shape; the files need not exist yet. `vendorPaths` maps author-chosen dependency
names to files or directories. List names that denote DLLs in `vendorDlls` so
`check` also inspects their PE format and architecture. Both fields are optional.
The active local file and generated `.artest/` content are ignored by the generated
`.gitignore`. The example contains placeholders only and is not loaded as active
configuration.

Omitting `artest-project.local.json` is valid for both creation and portable
configuration validation.

## Check local prerequisites

Run the explicit check after configuring the workstation:

    & 'C:\Path\To\Python313\python.exe' -I -B source/ARTest.Python/tools/project.py check D:\Work\MyPythonExtension
    $LASTEXITCODE

The operation returns one JSON object and exits with `0` only when every declared
check succeeds; prerequisite failures return `1`. Each diagnostic includes an
identifiable code, operation, stage, affected field and path, expected condition, known
cause and concrete correction. A missing or malformed project configuration is
still a command/configuration error and exits through the normal argument-parser
error path.

`check` performs these offline checks:

- It starts only the explicitly configured interpreter, with separate arguments,
  isolated mode, no site initialization, a five-second timeout and bounded output.
  After a timeout it requests termination, waits at most one additional second to
  confirm process exit, and gives each output reader a finite one-second completion
  wait. `PYTHON_PROBE_TERMINATION_UNCONFIRMED` means exit was not observed within
  that bound; the diagnostic does not claim that the process was closed.
  The probe requires standard GIL-enabled CPython 3.13 on Windows x64. Python 3.12
  remains suitable for scaffold tests but is not a supported extension runtime.
- It reads the wheel archive metadata without installing or importing it. The
  metadata—not the wheel filename—must identify `artest-python` `0.2.0`, require
  Python `>=3.13,<3.14`, carry the current pinned SDK dependencies and provide the
  `py3-none-any` pure-Python tag. SDK 0.2.0 corresponds to the exact private wire
  0.2 contract; older SDK/wire versions are not accepted.
- It verifies that `cliExecutable` is a readable Windows x64 PE executable. It
  never launches the CLI or Engine; this static check does not prove product
  identity or obtain a CLI version.
- It verifies every declared `vendorPaths` entry exists. Entries named by
  `vendorDlls` must be readable PE DLLs with AMD64 machine type. No DLL is loaded.

Representative diagnostic codes distinguish `PYTHON_MISSING`,
`PYTHON_NOT_EXECUTABLE`, `PYTHON_PROBE_FAILED`, `PYTHON_PROBE_TIMEOUT`,
`PYTHON_PROBE_TERMINATION_UNCONFIRMED`, `PYTHON_PROBE_INVALID_OUTPUT` and
individual compatibility failures;
`SDK_MISSING`, `SDK_INVALID`, `SDK_INCOMPATIBLE`; `CLI_MISSING`, `CLI_INVALID`,
CLI architecture mismatch; `LOCAL_DEPENDENCY_MISSING`; and missing, invalid-PE
or wrong-architecture vendor DLLs.

A successful check is not hardware or deployment certification. Static PE
inspection does not prove that transitive DLLs, required exports or a compatible
device are available. Package preparation and runtime activation retain their
own integrity and loader checks, and actual loader failures remain runtime errors.

## Materialize explicit local plan bindings

`planBindings` is a deliberately small association from a named `vendorPaths`
entry to an existing top-level field of one instrument's `config` object:

```json
{
  "vendorPaths": {
    "vendorSdk": "C:/Vendor/bin/vendor.dll"
  },
  "vendorDlls": ["vendorSdk"],
  "planBindings": [
    {
      "vendorPath": "vendorSdk",
      "instrumentId": "Scope1",
      "configField": "sdkDll"
    }
  ]
}
```

The portable source plan must already contain exactly one instrument with ID
`Scope1` and an existing string or null `config.sdkDll` field. The binding does
not add fields, navigate arbitrary JSON or define a template language.

Generate the local copy explicitly:

    & 'C:\Path\To\Python313\python.exe' -I -B source/ARTest.Python/tools/project.py materialize-plan D:\Work\MyPythonExtension

The only destination is `.artest/stage2/local-plan.json` below the project. The
operation resolves each configured vendor path, validates declared local
dependencies and binding targets, and substitutes absolute paths in that copy.
It never changes the portable plan. It rejects reparse-point traversal and refuses
to overwrite an existing output, including an unrelated file. Stage 2 does not
connect this copy to preparation, compilation or execution.

## Focused tests

The Stage 1 and Stage 2 tests use only the Python standard library, controlled
fixtures and temporary directories:

    & 'C:\Path\To\Python313\python.exe' -I -B source/ARTest.Python/tests/test_project.py -v

They do not require the Engine, a prepared environment, vendor software, hardware
or network access. The supported-interpreter smoke runs when the suite itself is
executed with standard CPython 3.13 Windows x64. Reparse-point cases run when the
host permits creation of test directory links; otherwise those individual cases
report a skip.
