# Create and run a minimal Python extension project

PY-DX-01 Stages 1 through 4 provide a hardware-free project scaffold, static
configuration validation, explicit local-prerequisite checks, an optional
generated Test plan copy with machine-local vendor paths, verified preparation,
offline validation, and explicit execution. Stage 3 creates or exactly reuses an
immutable package/environment revision; Stage 4 coordinates the existing CLI
without changing Engine execution policy.

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

## Prepare a package and isolated environment

Configure `python` and `sdkWheel` in `artest-project.local.json`, then run:

    & 'C:\Path\To\Python313\python.exe' -I -B source/ARTest.Python/tools/project.py prepare D:\Work\MyPythonExtension
    $LASTEXITCODE

Preparation requires standard GIL-enabled CPython 3.13 Windows x64, including
its `venv` and `ensurepip` components, and an unmodified compatible
`artest-python` 0.2.0 wheel/private wire 0.2. The operation checks those two
local prerequisites before writing an attempt. `cliExecutable` and vendor paths
remain inputs to `check` and later execution, but are not preparation inputs.
Dependency installation occurs only inside the new project-owned environment.
It uses the existing exact-version/hash lock and `package.py` commands; nothing
is installed globally. Metadata generation imports the trusted author definition
through the existing tool, so `define_extension` and constructors must remain
deterministic, hardware-free, and independent of machine state.

The result is a JSON object containing `preparationId`, `reused`, `revision`,
`package`, `receipt`, and `association`. `association` is the existing mapping
format from the verified package's extension ID to the absolute verified
`artest-environment.json` path. Stage 3 produces that file but does not pass it
to the Engine.

### Owned output and publication

All generated state is ignored and remains under the project's `.artest/`:

    .artest/
      stage2/
        local-plan.json
      stage3/
        ready.json
        work/
        incomplete/
        revisions/
          <preparation-id>/
            package/
            environment/
              artest-environment.json
            preparation.json
            python-environments.json

`work/` contains only unpublished attempt/publication markers. The package and
environment payload is created directly at its final `revisions/<preparation-id>`
path because the generated launcher intentionally pins absolute environment and
site-package paths. While creation or validation is in progress, that revision
contains `preparation.incomplete.json` and cannot validate as ready or reusable;
the tool never relocates a successfully prepared environment. A caught failed or
interrupted attempt is moved to `incomplete/` with a disposition record and is
never reused. An abrupt stop can leave a lock, work marker, or incomplete revision;
the next invocation fails closed so an operator can preserve and inspect it.
`revisions/` otherwise contains immutable, fully described candidates, including
older preparations that may still be in use. The tool does not edit or delete
them. `ready.json` is the sole current selection and is atomically replaced only
after package, environment, receipt, launcher location, association, and current
inputs pass validation. Therefore a failed preparation leaves the prior selection
unchanged.

Preparations for one project use an exclusive `.artest/stage3/preparation.lock`.
A concurrent invocation, or a stale lock after an unconfirmed interruption, is
rejected with the exact lock path. The tool never guesses that such a lock is
safe to remove. Unknown entries in the owned Stage 3 root, reparse-point output
paths, mixed input snapshots, corrupt inventories, broken receipt bindings, and
inconsistent associations are errors; Stage 3 does not repair receipts, prune
evidence, or overwrite foreign files.

### Identity and exact reuse

The preparation ID is a canonical SHA-256 identity, not a timestamp. It covers:

- every packaged source path and file digest, using the same exclusions as the
  low-level packager;
- the entry point and exact dependency-lock digest;
- the SDK wheel digest;
- the configured interpreter's absolute path, executable and runtime-DLL
  digests, reported implementation/version/platform/architecture/GIL identity,
  plus the standard `venv` and bundled `ensurepip` inputs;
- the project/package preparation tools and the source SDK metadata-authoring
  surface used by `package.py`.

Before reuse, Stage 3 recomputes that identity, validates the recorded identity,
package and environment full-file inventories, package-to-receipt binding,
extension-ID association, current SDK/interpreter/runtime hashes, and invokes
the existing low-level `package` and `prepare` validations against the immutable
outputs. Exact intact inputs therefore return `reused: true` without running pip
installation. A source edit—even one preserving file size—or a lock, SDK,
interpreter, or relevant tool/runtime change selects a new preparation ID and
creates a new revision. A plan-only edit is deliberately absent from preparation
identity, so it reuses the environment; compiling that changed plan belongs to
Stage 4.

The tool snapshots identity before creation, after package/environment creation,
and again after final validation. A mismatch means inputs changed during the
attempt: no ready selection is published and the mixed attempt is preserved as
incomplete. Exact hash and inventory errors are reported rather than converted
into a cache miss, because silently rebuilding over ambiguous or corrupt state
would hide evidence.

### Stage 3 boundary

`prepare` is the only new workflow operation in this stage. It does not compile
or execute the portable or materialized plan, launch the Engine, alter an active
session, watch files, install vendor software, share environments globally, or
perform cleanup/garbage collection. Use the existing low-level commands
`package.py package` and `package.py prepare` when that explicit workflow is needed;
their arguments and receipt/package formats remain unchanged.

## Prepare, validate, and run the Test plan

After configuring local prerequisites, explicitly run the project from any
current directory:

    & 'C:\Path\To\Python313\python.exe' -I -B source/ARTest.Python/tools/project.py run D:\Work\MyPythonExtension
    $LASTEXITCODE

`run` checks every configured prerequisite, prepares or exactly reuses the
current immutable revision, resolves the Test plan, calls `ARTestCLI compile`,
revalidates the project and preparation, and only then calls `ARTestCLI
extension-run`. Arguments are passed separately. CLI stdout, stderr, final
run-result JSON, and exit code pass through unchanged.

The low-level command above executes against only the project-owned prepared
revision. The development-kit `run --mode sources` adapter also supplies the
selected installation catalog and Python associations. The project tool composes
an immutable local execution catalog containing those registered packages plus
the current prepared project revision; it never changes the selected target.
Consequently a local Python Test script can use a driver registered by another
package. `run --mode registered` instead uses the selected catalog and association
directly, requires the project extension ID to be registered, and does not prepare
or copy the current Test script sources. Neither mode publishes or selects a
profile.

Without `planBindings`, both CLI commands read the portable Test plan directly.
With bindings, `run` writes a content-addressed local copy under
`.artest/stage4/test-plans/` and uses it for both commands. It never edits the
portable Test plan; Stage 2's explicitly materialized copy remains separate.

Edit `src/extension.py` and run again. Unchanged inputs reuse preparation; a Test
script edit creates a new immutable revision. A Test plan-only edit reuses the
environment but is compiled again. Missing prerequisites, failed preparation,
an invalid Test plan, or failed offline validation prevent execution. Runtime
command errors, cancellation, timeouts, and indeterminate effects retain the
existing result and failure code and are never retried automatically.

The wrapper uses finite process waits. If its outer execution bound expires, it
does not retry and reports whether termination was confirmed. An unconfirmed
exit is an indeterminate external effect that must be inspected before rerunning;
this is not a new Engine timeout or cleanup guarantee.

## Focused tests

The controlled Stage 1/2/3/4 unit cases use only the Python standard library,
fixtures and temporary directories:

    & 'C:\Path\To\Python313\python.exe' -I -B source/ARTest.Python/tests/test_project.py -v

They do not require the Engine, vendor software, hardware or network access.
Stage 3 acceptance additionally runs a real `package.py package`/`prepare` round
trip with the current SDK wheel and pinned dependencies. The supported-interpreter
smoke runs when the suite itself is executed with standard CPython 3.13 Windows
x64. Reparse-point cases run when the host permits creation of test directory
links; otherwise those individual cases report a skip.
