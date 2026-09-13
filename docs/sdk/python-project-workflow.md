# Create a minimal Python extension project

PY-DX-01 Stage 1 provides a hardware-free project scaffold and static
configuration validation. It does not prepare an environment, install packages,
compile a plan or run ARTest.

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
path kinds and portable-path containment. It does not verify the configured Python,
SDK wheel, CLI executable or vendor dependencies; those checks belong to a later
PY-DX-01 stage.

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
  }
}
```

These paths may be absolute. Relative local paths are resolved from the project
root but are not constrained to remain there. Stage 1 validates only their JSON
shape; the files need not exist yet. `vendorPaths` is optional and maps
author-chosen dependency names to local paths. The active local file and all
future generated `.artest/` content are ignored by the generated `.gitignore`.
The example contains placeholders only and is not loaded as active configuration.

Omitting `artest-project.local.json` is valid for both creation and portable
configuration validation.

## Focused tests

The Stage 1 tests use only the Python standard library and temporary directories:

    & 'C:\Path\To\Python313\python.exe' -I -B source/ARTest.Python/tests/test_project.py -v

They do not require the Engine, a prepared environment, vendor software, hardware
or network access. Reparse-point cases run when the host permits creation of test
directory links; otherwise those individual cases report a skip.
