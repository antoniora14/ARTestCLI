# PY-DX-01 execution plan

Official name: **PY-DX-01 — Python Developer Experience: create, prepare and run**.
Status: initial scope approved by the owner on 2026-09-13; Stages 1-4 accepted
after Architect review. Stages 4A-4C and Stage 5 are also accepted. Overall
PY-DX-01 is closed with the owner-approved deferrals in
[the closure record](../architecture/py-dx-01-closure.md). The owner-requested flow update
on 2026-09-15 adds mandatory delivery stages 4A/4B/4C before Stage 5.
Position: after accepted C-02, before mandatory C-03 and C-04; no .NET work.
Authority: [current roadmap](../architecture/roadmap-pre-dotnet.md) and
[AGENTS.md](../../AGENTS.md). This is a Python-specific authoring tool, not a
multi-language abstraction. The [SDK authoring journey](sdk-authoring-journey.md)
is the authority for the added delivery scope: an installed SDK entry point,
Python/C++ authoring via existing tools, and registration with a selected ARTest
installation. C# remains explicitly unavailable until the .NET gates.

Next authoring work is the separately scoped [DEV-01 roadmap](artestdev-roadmap.md),
positioned before C-03. It does not add stages to or reopen this closed plan.

## Outcome and scope

The user-facing completion criterion is install SDK -> create by name/folder/
language -> edit source -> build -> register. The starter must not require a
repository checkout or manual internal configuration. Optional explicit run uses
Stage 4. The detailed additions are in the SDK authoring journey and stages 4A-4C.

A developer creates a small driver/command/plan project, configures workstation
prerequisites once, and explicitly prepares or runs it. The tool chooses and
validates owned output locations, generates the existing environment association
and delegates offline compilation and execution to existing ARTestCLI commands.
Unchanged inputs reuse an intact preparation; changed sources are detected on the
next prepare/run and create a new preparation without editing historical receipts.

Included:
- Minimal project template with a Python driver, broker-based command and one plan.
- Portable persistent configuration for entry point, dependencies, plan and paths
  relative to the project, independent of the caller's working directory.
- Separate local settings for supported Python, SDK wheel and vendor DLL/dependency
  locations. Also record the local ARTestCLI executable needed for orchestration.
- Preparation, association generation, validation and execution orchestration.
- Exact-input preparation reuse and source-change detection at the next invocation.
- Actionable diagnostics for incompatible Python, missing SDK, wrong DLL and missing
  dependency, with stage, affected path/value and a concrete correction.

Excluded: hot reload; changing active sessions; global shared environments; advanced
dependency-conflict resolution; global automatic cleanup; automatic PicoSDK
installation; waveform streaming; IPC optimization; Studio; .NET; changes to native
ABI, Engine API, IPC protocol, receipt formats or execution-manifest formats.
A self-contained local evaluation development kit and installed-target registration
are now included by the 2026-09-15 scope update. Public package feeds, complete
production/offline deployment, and a general plugin/language framework remain out.

## Existing mechanisms and ownership

Read [Python authoring](../sdk/python-extension-authoring.md),
[Python runtime](../architecture/stage-d4-2-python-runtime.md), and
[managed-process ADR](../architecture/decisions/0002-managed-extensions-out-of-process.md).
Inspect these implementation entry points before changing tooling:
- `source/ARTest.Python/tools/package.py`: package metadata/inventory, prepare,
  pinned dependency installation and receipt validation.
- `scripts/prepare-python-example.ps1` and `scripts/prepare-tcp-hello-python.ps1`:
  current association-generation examples.
- `source/ARTest.Python/examples/simulated/extension.py` and its sample plan.
- `source/ARTest.Python/tests/test_sdk.py`, `test_worker.py` and
  `scripts/test-python-runtime.ps1`: existing regression paths.

Proposed new ownership is `source/ARTest.Python/tools/project.py`, a minimal
`source/ARTest.Python/templates/minimal/` template and focused project-tool tests.
Stages 1-3 have delivered creation, structural/prerequisite checks and verified
preparation in these paths; execution and SDK delivery were subsequently accepted. Keep new logic
there; extract a small helper only when this implementation actually needs one.
The existing C++ CLI, Engine, Core, SDK runtime API, broker, supervisor and worker
need no feature changes. Packaging integration may need a small private helper;
it must preserve existing low-level invocations and their validation behavior.

`Lib/site-packages` holds installed SDK/dependencies inside a prepared environment.
`Scripts` holds environment executables/tools; the existing receipt and launcher
govern actual worker startup. Automate their creation through the current prepare
tool, not directory copying, manual edits or shell activation. Runtime must never
install packages. Missing vendor software remains an explicit local prerequisite.

Every stage preserves AGENTS.md and the approved scope. In particular preserve
out-of-process Python, data-only offline compilation, integrity and eager package
activation/validation before catalog publication, activation recovery, unchanged
`Wait(30000)`, existing broker access and no retry/continue after an indeterminate
effect. Preparation can import trusted author metadata through the existing tool;
that is not offline Engine compilation and must not open hardware.

## Small, verifiable implementation stages

Stages 1-5 and 4A-4C are accepted. Stage descriptions below retain their original acceptance requirements; final disposition is recorded in the closure record.
4A may proceed independently of Stage 4; 4B depends on 4A, 4C depends on 4B,
and Stage 5 requires all four. Accept each unit before expanding
into the next.

Stage 1 closure: `PY-DX-01 STAGE 1 ACCEPTED`. The Architect reproduced 22 tests
on CPython 3.12.14 and 3.13.15 (20 passed, 2 skipped per interpreter). Only real
symlink creation was skipped with WinError 1314; the deterministic reparse guard
passed. The real create/validate smoke with a different CWD and spaces, absent
local configuration, generated Python AST and byte-identical dependency lock
passed. The lock SHA-256 is
`975a6bd0df618afac3d884c77632fdb9dbda028470cc5497052a58da2c7865b6`.
All nine Stage 1 files passed explicit whitespace inspection. Windows drive/root
anchors are rejected for all three portable paths; local path rules are unchanged.
Debug/Release builds, runtime acceptance, preparation and Engine execution are
outside this unit's acceptance. This is not acceptance of the complete PY-DX-01.

### 1. Project contract and minimal scaffold (first implementable unit)

**Objective:** create a portable, readable project without requiring hardware,
an SDK import, a prepared environment or an Engine process.

**Files/components:** new `tools/project.py`, `templates/minimal/` and
`tests/test_project.py` under `source/ARTest.Python/`; a focused new
`docs/sdk/python-project-workflow.md` section documenting only delivered commands.

**Expected changes:**
- Implement only project creation and configuration parsing/validation.
- Use project-local `artest-project.json` for a versioned tooling configuration:
  source directory, entry point, dependency-lock path and plan path. Dependencies
  use the existing exact-version/hash lock; do not add a resolver.
- Use ignored `artest-project.local.json` for interpreter, SDK wheel, CLI executable
  and optional vendor paths. Generate a documented local example without personal
  paths. Neither file changes a runtime manifest or receipt schema.
- Template layout separates `src/`, dependency lock, plan and local/generated
  outputs. Supply unique author-chosen extension/component IDs consistently.
  A small simulated driver and a command using its broker service suffice;
  the template does not depend on PicoSDK.
- Reject malformed configuration, unknown configuration versions, invalid entry
  point syntax and portable paths escaping the project. Resolve paths from the
  project root, not current directory. Do not overwrite a nonempty destination
  or follow reparse points outside owned output locations.

**Invariants:** configuration and scaffold operations do not import extension/SDK
code, launch subprocesses, install anything, access instruments or alter existing
projects. Local vendor/interpreter paths may be absolute; portable paths may not.

**Acceptance criteria:** a new project contains consistent driver/command/plan
references and dependency declarations; another current directory gives the same
resolved project inputs; missing optional local settings do not prevent creation.
Invalid inputs fail clearly and a collision leaves original files intact.

**Tests:** standard-library unit tests for valid/invalid configuration, versions,
entry points, relative/escaping paths, directories with spaces, local/portable
separation, destination collisions and template consistency. Parse generated Python
syntax without importing it. Use temporary test-owned directories; no network,
vendor DLL, Engine binary or prepared environment required for this unit.

**Do not touch:** `package.py`, existing runtime/SDK code, Engine/Core/CLI,
build/publication graph, prior examples or receipts. Do not implement prepare,
run, caching or prerequisite probes in this unit.

Stage 2 closure: `PY-DX-01 STAGE 2 ACCEPTED`. The Architect reproduced 46 tests
on CPython 3.12.14 (43 passed, 3 skipped) and 3.13.15 (44 passed, 2 skipped),
with no failures. Common skips are real symlink creation blocked by WinError
1314; the deterministic guard passed. The additional 3.12 skip is the supported
3.13 interpreter smoke, which passed on 3.13.15. The timeout cleanup now bounds
its termination wait to 1 second after the initial 5-second wait and joins each
reader for at most 1 second. Unconfirmed process termination produces an explicit
failure diagnostic. The whitespace check passed. No Stage 3 preparation, cache,
receipt changes, Engine execution or native build changes are part of this closure.

### 2. Local prerequisites and concrete diagnostics

**Objective:** identify a usable local toolchain and explain failures before
preparation or execution.

**Files/components:** project tool and focused tests; local-config documentation.
Use existing compatibility checks as authority, not a new runtime support policy.

**Expected changes:** validate the configured standard GIL-enabled CPython 3.13
Windows x64 interpreter, SDK wheel presence/compatibility and CLI location.
Validate declared vendor DLL paths/PE architecture and required local dependencies.
Keep vendor paths out of portable metadata. Define a small explicit binding of
local vendor paths to existing driver configuration fields in a generated plan
copy; do not invent a new execution-plan language or mutate the source plan.
Prerequisite checks must not acquire a device. Static DLL checks do not certify
all transitive dependencies or device compatibility; preserve actual loader errors
at runtime and explain missing dependencies without claiming more than was checked.

**Invariants:** no automatic vendor installation or global Python modifications;
no hardware during configuration/metadata/offline compile. No public diagnostic
contract changes; author-tool diagnostics may add context to existing errors.

**Acceptance criteria:** each required diagnostic identifies the failing stage,
path/value, expected condition and corrective action. Missing local prerequisites
block dependent operations, while creation remains available. Machine-specific
paths appear only in local settings/generated outputs.

**Tests:** subprocess-probe doubles plus a supported interpreter smoke check;
missing wheel/CLI, wrong Python version/GIL/architecture, missing DLL, wrong PE
architecture and declared missing dependency. No real PicoScope required.

**Do not touch:** supported runtime matrix, ABI, Engine validation, vendor install,
dependency solver or command/driver public APIs.

### 3. Preparation, association and verified reuse

**Objective:** eliminate manual output suffixes, environment directories and
association edits without weakening package integrity.

**Files/components:** project tool, preparation integration tests and workflow
documentation; only a narrowly justified private `package.py` helper if required.

**Expected changes:**
- Delegate metadata/package/prepare operations to existing tooling with explicit
  arguments. Generate the existing extension-ID-to-receipt association from the
  verified resulting package, never by hand-authoring receipts.
- Own generated outputs beneath project-local ignored `.artest/`. Separate
  incomplete work from ready revisions; preserve earlier valid revisions.
- Define and test preparation identity from all preparation-affecting inputs:
  source inventory/content, entry point, exact dependency lock, SDK wheel,
  interpreter identity and relevant preparation-tool/runtime inputs.
  A source timestamp alone is insufficient. Plan-only edits need a fresh compile,
  not an environment rebuild when preparation inputs are unchanged.
- Reuse only after current inputs and existing package/environment inventories,
  receipt binding and interpreter/SDK identity pass the existing checks.
  Detect source changes at the next prepare/run; choose new owned outputs.
- Publish the local ready selection/association only after successful validation.
  Detect inputs changing during preparation; never mark a mixed snapshot ready.
  Serialize competing prepares for the same project or reject them clearly.
  Do not modify an environment used by an existing session.
- Corruption or unknown files fail closed with a useful diagnostic; no silent
  receipt rewrite, integrity bypass, automatic destructive repair or global GC.

**Invariants:** all package/receipt formats and eager runtime validation stay
unchanged. No Engine environment management or shared global environment cache.
Same-project serialization is not a new runtime/session-locking guarantee.

**Acceptance criteria:** first prepare succeeds; unchanged intact inputs reuse
without reinstalling dependencies; source/lock/SDK/interpreter changes invalidate
the appropriate preparation; a failed preparation preserves the prior ready
revision. Old low-level commands continue to work unchanged.

**Tests:** actual package/prepare round trip with the minimal template; unchanged
reuse, same-size source edits, dependency/SDK/interpreter changes, plan-only edits,
corrupt package/receipt/environment, failed installation, interrupted preparation,
input mutation and concurrent prepare attempts. Use controlled doubles for rare
failures and real existing validators for the successful/reuse/integrity paths.

**Do not touch:** hashing algorithms, `ManagedIntegrity.cpp`, `PythonRuntime::Load`,
timeouts, receipt schema, dependency conflict resolution or native build graph.

Stage 3 closure: `PY-DX-01 STAGE 3 ACCEPTED` (2026-09-14). The corrected
candidate prepares environments at their definitive revision paths before selecting
ready.json. Published launcher paths and exact reuse were verified without
executing a plan. Source and retained artifact hashes, full inventories and the
package/receipt/association binding match the evidence in
`artifacts/acceptance/py-dx-01/stage3-fix-candidate/acceptance-record.md`.
The Architect reproduced 57 tests: 55 passed and 2 admissible WinError 1314
symlink skips. Matching Debug/Release XML reports show 242 enabled tests per
configuration, no failures/errors and 31 disabled tests. The previous defective
candidate evidence was preserved; it is not acceptance evidence for this closure.
At Stage 3 closure, Stage 4 and overall acceptance were still pending; see the final closure record for current status.

### 4. Prepare, offline validate and execute from the project

**Objective:** one explicit project run coordinates preparation and execution
without manual catalog/mapping management.

**Files/components:** project tool, integration tests and workflow documentation.
Reuse existing `ARTestCLI compile` and `extension-run` commands.

**Expected changes:** resolve local/project paths, ensure the current preparation,
produce the existing mapping and any local plan copy, compile offline and execute
only after successful validation. Use argument arrays, propagate failures and
retain result/diagnostic output. Run may prepare because the author explicitly
requested the workflow; the Engine itself never restores or installs packages.
No background watcher or mutation of a running session. Use existing cancellation
and effect semantics; do not retry failed runs automatically.

**Invariants:** compile remains data-only; runtime activation still performs its
own validation. A cached preparation is not permission to skip activation checks.
Commands reach drivers through the broker. Native CLI use remains independent
of Python and this tool.

**Acceptance criteria:** new project runs using configured prerequisites from
another directory; unchanged rerun reuses preparation; an edit is reflected on
the next run via a new valid revision. Compile/preparation failure prevents
execution; runtime failure is reported as failure, including indeterminate effects.
Local plan bindings do not alter the portable plan.

**Tests:** template end-to-end against Debug and Release; changed-source rerun,
paths with spaces/different working directory, missing prerequisite, invalid plan,
failed prepare, command error, cancellation and indeterminate-effect propagation.
Use simulated/fault fixtures and existing runtime tests; no physical device required.

**Do not touch:** CLI command contracts, Core execution policy, broker/supervisor/
worker, active sessions, retry rules, IPC or C-03 recovery behavior.

Stage 4 closure: `PY-DX-01 STAGE 4 ACCEPTED` at
`8b8427ff04167990012ba5bb855fccffa5fcf694`. Evidence:
`artifacts/acceptance/py-dx-01/stage4-candidate/20260920T211457Z-30284d3a/`.
The 20/20 execution cases passed, including Debug/Release, exact reuse, edited
source regeneration, cross-package sources/registered modes, real cancellation,
and single-attempt indeterminate-effect propagation. Stage 5 and overall
acceptance were subsequently completed; see the final closure record.

### 4A. Installable SDK development kit

**Objective:** deliver the authoring journey from an extracted SDK without a
repository checkout. See [SDK authoring journey](sdk-authoring-journey.md).

**Files/components:** SDK packaging scripts, distribution README/notices/inventory
and focused extracted-kit tests. Reuse native distribution assets; package the
Python tools, metadata SDK, wheel, templates, compatible private interpreter and
starter dependencies, plus a matching evaluation CLI/runtime bundle.

**Expected changes:** a versioned, inventory-checked development artifact with
SDK-relative tool discovery and a root entry point. Preserve embedded component
versions independently; do not rename native SDK or change contracts to version
the kit. Record redistribution requirements; do not publish a public release here.

**Invariants:** no Engine environment manager, global installation mutation or
dependency on developer machine paths. Preserve existing SDK consumers and tools.

**Acceptance criteria:** installed tools and Python scaffold/preparation operate
from a path with spaces with repository access absent and without global Python.
All bundled component paths, runtime dependencies and hashes are verified.

**Tests:** archive inventory/path safety, extracted-copy operation, private Python
venv/pip functionality, starter preparation and missing/corrupt kit diagnostics.

**Do not touch:** ABI, Engine API, IPC, receipt formats, native toolchain support
matrix or unrelated publication-graph incidents. No generic installer platform.

Stage 4A closure: `PY-DX-01 STAGE 4A ACCEPTED`. The accepted evaluation ZIP is
`ARTestDevelopmentKit-0.1.0-evaluation-windows-x64.zip`, SHA-256
`b268ed96cd5da16c36dc90ec813545d96ff2691a92b4746d6c2ae3d9063fffcf`.
Evidence: `artifacts/acceptance/py-dx-01/stage4a-candidate/20260920T002306Z-7ec0288e/`.
The external Python flow passed with all 408 Git-visible checkout files locked
and a confirmed sharing-violation probe; the extracted native SDK consumer built
and activated with the candidate CLI/Engine. A real junction was rejected.
Source, report and artifact hashes are recorded in the evidence provenance.
At that historical closure, only Stage 4A was accepted and Stage 4B was next. This closure does
not accept Stage 4 execution, 4C registration, Stage 5 or authorize .NET.

### 4B. Guided create and build from the SDK

**Objective:** implement name + folder + supported language -> editable project
-> build, using the root SDK entry point.

**Files/components:** SDK authoring entry point/templates, Python project tool
adapters, existing native template/build adapters, workflow docs and focused tests.

**Expected changes:** interactive console and equivalent explicit arguments;
generate consistent persistent IDs/configuration/plan without expert mandatory
inputs. Default to driver+command, offer supported authoring choices. Python build
uses Stage 3; C++ build/IDE uses existing SDK props/targets and metadata generation.
Remember workstation paths locally; derive bundled paths from the kit. C# is
unavailable until .NET is implemented. This is fixed dispatch, not a language framework.

**Invariants:** preserve low-level commands and author-owned edits, separate local
and portable settings, and do not put native extensions behind a Python dependency.

**Acceptance criteria:** Python and C++ starter projects are generated outside the
repository using the three normal inputs and build through supported paths. A
missing compiler/vendor dependency produces an actionable setup message. No manual
ID, wheel, receipt or association editing is required for the starter.

**Tests:** both language starters, consistent references, driver/command choices,
invalid paths/nonempty destinations, repeat builds, local settings, missing native
toolchain and explicit rejection of C#; retain Stage 1-3 regression tests.

**Do not touch:** runtime contracts, automatic compiler/PicoSDK installation,
language-version selection or advanced third-party dependency resolution.

Stage 4B closure: `PY-DX-01 STAGE 4B ACCEPTED` (2026-09-20).
Evidence: `artifacts/acceptance/py-dx-01/stage4b-candidate/20260920T023615Z-51bf0a2f/`;
`candidate.json` SHA-256:
`d84c491df235ca2e75f2e8cd4e612969c29671bcc59be75c4ef54e745e56306c`.
The 9/9 focused cases passed. C++ template-name collisions preserve the project;
Python and C++ command-only starters use the unchanged native driver's contract
and operation, with real interoperability producing 42 * 2 = 84.
The accepted development-kit 0.2.0 ZIP SHA-256 is
`340a243d4ea622d5175d517cfe02873a012bc9f7fb7671b88dd4a0df7c2b9928`.
At that historical closure, only new/build was accepted and Stage 4C was next;
Stage 4 execution, Stage 5 and .NET remain outside this closure.

### 4C. Register with the selected ARTest installation

**Objective:** make the built extension discoverable by a named installation,
without manual file placement. Follow the registration semantics in the journey.

**Files/components:** SDK entry point, installation-profile/publication adapter,
Python preparation adapter, existing native publication tools and registration tests.

**Expected changes:** explicit target selection saved locally; build-if-needed;
validate/publish owned package outputs and environment association; verify target
catalog discovery. Prepare Python environments at their definitive target paths.
Preserve other registrations, existing sessions and previous valid selections.
Do not overwrite runtime binaries. Repeated unchanged registration is idempotent.

**Invariants:** existing catalog/manifest/receipt/API semantics; no hidden hardware
I/O, global registry, hot reload or relocation of prepared environments.

**Acceptance criteria:** registration works from the extracted kit for both
supported languages; the selected installed runtime discovers the extension;
deleting access to the source project does not break the registered installation.
Failed or incompatible updates preserve the previous usable registration.
Success names the target and extension, and does not claim a measurement ran.

**Tests:** first/repeated/updated registration, multiple package ownership and ID
conflicts, target mismatch, access denied, failed/interrupted publication, complete
catalog preservation, Python path/receipt integrity, source independence and
installed-target discovery. Explicit simulated run is checked through Stage 4.

**Do not touch:** Engine DLL replacement, other installations, live sessions,
global cleanup, public contracts, .NET or C-03/C-04 implementations.

Stage 4C closure: `PY-DX-01 STAGE 4C ACCEPTED` at
`bf89848f7f7b43d84742702e565a63b1c4e7e993`. Registration remains separate
from execution; Stage 4 and Stage 5 are not accepted by this closure.

### 5. Authoring acceptance and final evidence

**Objective:** demonstrate the complete initial author workflow and preserve
existing users before requesting Architect acceptance of PY-DX-01.

**Files/components:** workflow guide, `source/ARTest.Python/README.md`, a link
from existing Python authoring documentation, focused test harness/reporting as
needed and generated evidence under `artifacts/acceptance/py-dx-01/`.

**Expected changes:** document prerequisites once, create/prepare/run, edit/rerun,
reuse, local vendor configuration, diagnostics and ownership of generated files.
Clearly distinguish the new optional author workflow from existing low-level
commands. Start the external exercise from the extracted development kit with no
repository access. Cover the owner's complete install/create/edit/build/register
flow and an explicitly requested simulated run through the registered target.
Verify Python without global Python or a compiler, and C++ with the documented
native toolchain. Observe a first-use engineer following the short guide; record
where assistance was needed. No internal manual ID/wheel/receipt/mapping edits
are allowed in the starter flow. This is not a public feed or complete D4.4
production deployment gate.

**Invariants:** no hardware-required acceptance, installation of PicoSDK, extra
language abstraction, C-02 reacceptance or phase advance. User/manual evidence
and unrelated working-tree files are preserved.

**Acceptance criteria:** all stage criteria pass with no unexplained failures or
skipped required cases; the extracted SDK completes the journey and the installed
target discovers/runs the registered simulated package independently of its source
project. Stages 4/4A/4B/4C must pass. Existing low-level Python authoring and
native-only execution remain usable.
Run required Debug/Release aggregate checks from AGENTS.md at integration, plus
the relevant Python SDK/worker/runtime and project tests on the final candidate.
Apply installed/frozen native-consumer gates as required by AGENTS.md; reuse
unchanged baseline artifacts and do not rebuild the frozen SDK for convenience.
Do not repeat dedicated C-02 acceptance unless a concrete affected path or regression
requires it. Physical PicoScope validation remains optional parallel support work.

**Tests/evidence:** record candidate commit plus dirty-file hashes if applicable,
source/config/tool/SDK/interpreter/CLI identities, Debug/Release binary hashes,
commands, exit codes, test reports, resulting inventories/receipts/associations
and proof of reuse versus changed-source regeneration. Keep intermediate failures
with their disposition. Scrub secrets from shared evidence; never commit local
settings, environments, generated packages or vendor binaries as source.

**Do not touch:** C-03/C-04 implementations, publication incident repairs, ABI
freeze, release feeds or .NET/Studio. Return results to the Architect through the
owner; do not contact other tasks.

## Overall PASS/FAIL and handoff boundary

PASS requires every included behavior and stage criterion above, existing integrity
and compatibility checks, and traceable evidence for the actual final candidate.
A required test failure, integrity bypass, source/preparation mismatch, broken
low-level compatibility or unintended runtime/contract change blocks acceptance.
Unavailable hardware is not a failure for this hardware-free gate. An unrelated
known incident stays deferred only with a documented, concrete impact assessment;
a reproducible failure of a required new path cannot be waived as historical.

The Developer reports changed files, tests/pass/fail, candidate/evidence identity,
remaining limitations and whether the unit is ready for Architect review.
If a public-contract change or a different architectural solution appears necessary,
stop and report the smallest contradiction; do not implement a new architecture.

**Original first handoff (completed):** stage 1 only, on model Sol (`gpt-5.6-sol`) with High reasoning,
as requested by the owner. Provide AGENTS.md, this plan's scope/ownership and stage 1,
and the referenced minimal example/authoring guide; no full roadmap history needed.
Stages 1-5 and 4A-4C are accepted. There is no remaining PY-DX-01 implementation handoff; follow the final closure record for deferrals.
Final acceptance
requires the whole installed-SDK journey, not only repository tooling tests.
Final acceptance is recorded in the closure document. This record does not
start another task or authorize a subsequent implementation stage.
