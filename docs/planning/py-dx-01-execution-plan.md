# PY-DX-01 execution plan

Official name: **PY-DX-01 — Python Developer Experience: create, prepare and run**.
Status: initial scope approved by the owner on 2026-09-13; Stage 1 accepted after
Architect review of the corrected candidate. Stages 2-5 remain pending.
Position: after accepted C-02, before mandatory C-03 and C-04; no .NET work.
Authority: [current roadmap](../architecture/roadmap-pre-dotnet.md) and
[AGENTS.md](../../AGENTS.md). This is a Python-specific authoring tool, not a
multi-language abstraction.

## Outcome and scope

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
No package feed, full offline deployment product or general plugin/language framework.

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
Stage 1 has delivered project creation and configuration validation in these paths;
preparation and execution commands are not yet available. Keep new logic
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

Stage 1 is accepted; Stages 2-5 remain pending. Accept each unit before expanding
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

### 5. Authoring acceptance and final evidence

**Objective:** demonstrate the complete initial author workflow and preserve
existing users before requesting Architect acceptance of PY-DX-01.

**Files/components:** workflow guide, `source/ARTest.Python/README.md`, a link
from existing Python authoring documentation, focused test harness/reporting as
needed and generated evidence under `artifacts/acceptance/py-dx-01/`.

**Expected changes:** document prerequisites once, create/prepare/run, edit/rerun,
reuse, local vendor configuration, diagnostics and ownership of generated files.
Clearly distinguish the new optional author workflow from existing low-level
commands. Supply an external-directory exercise using the documented tool/SDK/CLI
locations; no repository-private import/include path may leak into the generated
extension. This is not a standalone distribution/feed or D4.4 deployment gate.

**Invariants:** no hardware-required acceptance, installation of PicoSDK, extra
language abstraction, C-02 reacceptance or phase advance. User/manual evidence
and unrelated working-tree files are preserved.

**Acceptance criteria:** all stage criteria pass with no unexplained failures or
skipped required cases; a copied minimal project runs outside the repository;
existing low-level Python authoring and native-only execution remain usable.
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
Stage 1 is accepted. The next handoff is Stage 2 only, using its existing scope
above. No new general architecture plan is required. The owner's separate closure
instruction authorizes the Stage 1 commit/push; it does not initiate Stage 2 or
accept the overall PY-DX-01 iteration.
