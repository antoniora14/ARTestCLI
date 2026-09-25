# DEV-01 — ARTestDev delivery roadmap

Status (2026-09-25): DEV-01.1/01.2/01.2-A/01.3/01.4/01.4-A are ACCEPTED.
The Architect accepted DEV-01.4-A after reviewing the selective-recompilation
correction and its Debug/Release evidence. DEV-01.5–DEV-01.7 remain pending.
The owner-approved flow diagram supersedes the earlier primary Build and integrate
journey. The subsequent clarification excludes functional Studio integration;
only its disabled menu item is included. The latest SDK amendment excludes bundled
Python, sets the suggested workspace and requires prerequisite diagnostics only.
Acceptance applies only to the stages marked ACCEPTED below;
it does not authorize starting later stages together.

Read [AGENTS.md](../../AGENTS.md), [scope and exact UI behavior](../architecture/artestdev-initial-scope.md)
and [canonical sequence](../architecture/roadmap-pre-dotnet.md).
PY-DX-01 remains closed. Preserve the existing working tree, historical kits and
manual evidence. Do not rewrite past acceptance to match the new distribution.

## Product outcome

Welcome -> Create your own Driver, Command or both -> project form -> Generate ->
Edit -> developer edits (and builds C++ in Visual Studio) -> Integrate ->
Integrate to ARTestCLI -> detected/manual installation selection -> result.

Python preparation occurs inside Integrate; the user does not compile it into a
DLL or manage package receipts. C++ Integrate consumes current IDE build outputs;
it reports missing/stale builds rather than silently rebuilding. A separate
Run Test plan action remains optional, explicit and secondary to this diagram.

The SDK/ARTestDev, project source/output and ARTestCLI installation stay separate.
Create/open/edit do not require an installed ARTest runtime. The new authoring SDK
contains no ARTestCLI/Engine or Python runtime. Native/Python authoring resources
are shared across projects; Python tooling uses a compatible machine-installed
interpreter. Missing prerequisites produce manual setup instructions only. The normal journey uses no shell, internal JSON edits
or repository checkout. C++ compiler/editor setup is disclosed; basic widgets suffice.

`Integrate to ARTestStudio` is present but disabled with a future-work explanation.
There is no Studio discovery, adapter, registration, launch or acceptance dependency.
C-03/C-04, .NET and D5 sequencing is unchanged.

## Delivery order and mapping

| Stage | Visible outcome / responsibility |
| --- | --- |
| DEV-01.1 | ACCEPTED: project/SDK inspection, bounded process handling and separate target readiness |
| DEV-01.2 | ACCEPTED: Welcome, default/editable workspace, Generate and Edit; detect tools and explain missing prerequisites |
| DEV-01.2-A | ACCEPTED: automatic SDK location from the executable and private development staging |
| DEV-01.3 | ACCEPTED: Python preparation/reuse service for the Integrate pipeline |
| DEV-01.4 | ACCEPTED: native IDE outputs, provenance and validation without PowerShell |
| DEV-01.4-A | ACCEPTED: incremental native builds and bounded project-local retention; selective recompilation verified in Debug/Release |
| DEV-01.5 | Integrate menu, CLI target discovery/selection, safe registration and visible result |
| DEV-01.6 | Secondary explicit Test plan validation/execution |
| DEV-01.7 | Minimal SDK with selectable installation folder; first-use acceptance after manual prerequisite setup |

Hand off one stage at a time. A helper/service can be tested before its final GUI
integration; temporary development controls must not become mandatory user steps.
All stages preserve the architecture invariants in the scope document.

## DEV-01.1 — Application foundation and opening a project

**Status:** ACCEPTED. The review blockers below are historical and closed.

**Objective:** the basic Windows application that reads existing Python/C++
projects/SDKs and reports prerequisites honestly, without generation or mutation.

**Components:** `source/ARTestDev/`, its local CMake/tests and
`docs/sdk/artestdev-dev01-1.md`. Retain C++20/Qt Widgets and the documented tested
Qt/CMake/compiler combination. Do not migrate Engine/SDK builds.

**Accepted changes:** portable relative-path compatibility and
CWD independence; complete inventory/compatibility checks for the declared kit
type; structured Python results reflected in readiness; supervised bounded cleanup
when process termination is unconfirmed, without overlapping operations.
Reuse existing Python read-only checks; native inspection never invokes Python.
Separate SDK/project readiness from missing CLI readiness. Existing profile lookup
and manual target selection are read-only/in-memory here; persistence and final
Integrate flow belong to DEV-01.5. Legacy all-in-one kit inventories remain strict;
do not assume their runtime requirement applies to a future authoring-only package.

**Acceptance/tests:** application Debug/Release build/launch; valid and malformed
configs, current supported relative paths, spaces/different CWD, missing/corrupt
kit declarations, missing/incompatible machine-installed tools, native independence
and real Python check success/
failure. Exercise process errors, output limits, timeout and the actual unconfirmed
termination branch using controlled faults. Verify UI state rejects stale results
and stays responsive. The user-requested deletion of historical logs is not a
reason to fabricate or demand reconstruction of old observations.

**Do not touch:** Welcome/generation/IDE launch implementation (DEV-01.2), preparing,
extension builds/publication, registration, execution, Studio, Engine, old targets
or contracts. DEV-01.1 acceptance does not authorize these later operations.

## DEV-01.2 — Welcome, Generate and Edit

**Objective:** implement the diagram's first three screens/actions. Welcome displays
`Welcome to ARTestDev`, `The Art of Testing` and
`Create your own Driver, Command or both`. The button navigates to the project form.

**Components:** ARTestDev UI, project generation and editor-launch adapters; existing
SDK/Python templates/configuration readers; focused tests and short usage guide.
Port only necessary guided creation from `authoring.ps1`, delegating to Python
creation tooling where applicable. Keep legacy commands and file formats compatible.

**Changes:** name, destination, Python/C++ and driver/command/both inputs; Generate
is enabled after validation. Suggest workspace `C:\Users\Public\ArtestDev`, allow
another drive/folder and remember the choice. Keep SDK installation, workspace and
per-user settings separate. Maintain a diagnostics panel and the form on error.
Generate coherent IDs/Test plan/code without overwriting user files. Enable Edit
for the resulting project; discover suitable Python editors or Visual Studio,
allow selection/manual executable location and remember editor preferences in
private local settings. Open the project and point to the real behavior source.
Editor launch is asynchronous and does not prove compilation or preparation.
Detect compatible installed Python or the native IDE/compiler components required
for the selected operation. Missing/incompatible prerequisites yield concrete
manual-download/install messages, with an official reference URL if helpful.
No download/install button, installer invocation, package manager or automatic
browser launch. Recheck after manual setup. Python generation that delegates to
Python tooling may require a compatible interpreter; do not embed one or silently
rewrite that tooling to avoid the prerequisite.

**Acceptance/tests:** both languages and all three variants; invalid form and
Generate enablement; destination conflicts, case-insensitive starter-name collisions,
reparse/escape rejection, spaces and different CWD. No runtime/IDE required to
create/open. Correct editing location and launch arguments; missing/multiple IDEs,
manual selection, launch failure and an already-running IDE. Include default and
custom workspace, unwritable/occupied folders, SDK on another drive and per-user
settings isolation. Test missing Python, incompatible Python, incomplete Visual
Studio C++ components and successful recheck after simulated/manual setup. Verify
no network acquisition or installer side effects. Do not terminate external editors
on app close. Command-only preserves the existing driver contract.

**Do not touch:** builds, preparation, registration/run, embedded editor/debugger,
C# templates, installing IDEs or aesthetic redesign. Detecting a compiler is not
building with it. The Integrate behavior arrives in DEV-01.5.

## DEV-01.2-A — Automatic SDK location

Remove manual SDK selection in creation/inspection and ignore the old SDK
preference. Share executable-relative resolution, integrity/compatibility and
reparse protection through a private service. Keep the editable, persisted
`C:\Users\Public\ArtestDev` workspace separate. Resource failures must explain
repair/reinstallation and block only dependent actions, with no fallback SDK.

The Architect resolved the historical-format blocker: assemble separate Debug and
Release development folders with ARTestDev/Qt, native-sdk/ and python/ resources
from an explicit source allowlist. Use a dedicated private inspector and
artestdev-staging.json (internal version, platform/configuration, component
versions, relative paths and complete SHA-256 inventory excluding the descriptor).
Reject missing/additional/corrupt files, duplicates, escapes and reparse points;
never refresh hashes in response to corruption. Revalidate at Generate time.
Visual Studio launches this staging with build/log output outside it. Preserve
inspectKit and historical inventories. No CLI/Engine/validator/Python interpreter
or prepared environment is included. This is not ZIP 0.5.0, a DEV-01.7 public
manifest or an installer. See [the guide](../sdk/artestdev-dev01-2.md#dev-012-a--localización-automática-y-staging-privado).
DEV-01.2-A remains ACCEPTED; current sequencing is listed above.
## DEV-01.3 — Python preparation behind Integrate

**Status:** ACCEPTED. See the
[service delivery guide and evidence](../sdk/artestdev-dev01-3.md).

**Objective:** supply the Python preparation/reuse service that Integrate will call;
no extra required Prepare/Build command in the final user journey.

**Components:** ARTestDev Python operation adapter/settings/diagnostics and focused
tests. Reuse `project.py` and `package.py`; do not duplicate their validators or
preparation engine in C++.

**Acceptance/tests:** real preparation using a compatible externally installed
Python and the mandatory SDK wheels/locked inputs; no SDK-bundled interpreter;
exact reuse;
same-size source/lock/SDK/interpreter changes invalidate; Test plan-only changes
reuse. Failed/interrupted preparation preserves the previous selection. Final-path
launchers/receipts remain valid. Missing dependencies are actionable, with no
PowerShell, global environment writes, downloads or automatic vendor installation.
Keep missing CLI readiness distinct from authoring preparation prerequisites.

**Do not touch:** receipts/manifest formats, moved environments, global caches,
registration/execution, native compilation or new standalone mandatory UI steps.

## DEV-01.4 — C++ IDE builds and integration-ready outputs

**Status:** ACCEPTED after the recovery correction. The following adjustment
improves daily performance/storage without reopening that functional decision.

**Objective:** the generated project builds in Visual Studio without PowerShell,
Python or an installed ARTest runtime, producing DLL plus generated metadata.
These outputs await target validation; they are not yet an integrated package.

**Components:** new-kit native build integration, metadata generator orchestration,
private native output/publication helper, output provenance reader and tests.
Inspect current SDK targets, `Publish-ARTestPackage.ps1`, validator and publication
failure tests. Preserve legacy targets/properties and frozen SDK consumers.

**Changes:** separate local compilation/metadata generation from validation against
the selected installation. Record sufficient private build input/output identity
(sources, SDK, configuration/toolchain and produced files) to detect a stale build
at Integrate time. Do not infer freshness from DLL existence/size or IDE exit.
Provide a bounded validation adapter using the separate CLI installation, preserving
existing integrity/descriptor checks and metadata-only behavior. No Engine copy in
new SDK/project and no reimplementation of Engine validation.

**Acceptance/tests:** extracted SDK, real Visual Studio/MSBuild Debug/Release builds,
all native variants, no Python/PowerShell descendants or ARTest runtime needed for
local compilation. Validate against a selected compatible CLI; reject absent/stale
outputs, same-size source changes, configuration/SDK mismatch, corrupt output and
invalid descriptors. Preserve child-build isolation, ownership and interrupted
local-output publication/recovery, including identical inventories and reparse
rejection. Verify legacy SDK paths and frozen consumer compatibility separately.

**Do not touch:** automatic rebuild on Integrate, runtime registration, public ABI/
API/IPC/receipts, legacy target semantics, solution-wide CMake migration or unrelated
build-order incident repair. Project-local draft output is never target success.

## DEV-01.4-A — Incremental native builds and bounded retention

**Status:** ACCEPTED on 2026-09-25 after correction of grouped-source invalidation
and independent review of Debug/Release evidence. The initial REQUIRES FIXES is
resolved. DEV-01.5 remains pending. Accepted correction evidence is in
`source/ARTestDev/build/dev014a-selective-evidence/`. The
[guide amendment](../sdk/artestdev-dev01-4.md#dev-014-a--incremental-native-builds-and-bounded-retention)
is the detailed scope and acceptance authority for this unit.

**Objective:** a fast, repeatable edit/Build cycle without accumulating native
revisions. Keep stable DLL/metadata output and preserve integrity/recovery.

**Components:** NativeOutputs, private native targets, native tests and diagnostics;
NativeService/authoring/staging only where wiring or inventory requires it.

**Steps:** measure the current implementation; implement content-aware incremental
Build and explicit Rebuild; bound owned native retention with interruptible safe
cleanup; verify performance, storage and the accepted failure semantics.

**Acceptance:** all three native variants, Debug/Release; unchanged Build invokes
no compiler/linker/metadata generator and publishes no new revision; same-size,
restored-timestamp dependency edits invalidate correctly. Measure first, unchanged,
one-source-edit and Rebuild timings against the baseline on the same machine.
Unchanged Build median must improve by at least 50%; 5 seconds is the initial
usability target, with any miss reported explicitly rather than hidden. Require
measurable one-source-edit improvement. Repeated successful builds/checks must
converge to the documented bounded layout; demonstrate interrupted cleanup and
recovery, ownership guards and separate Engine-backed target validation.

**Do not touch:** Python preparation/receipt/environment behavior, legacy SDK build
semantics, public contracts, registration/run, global cleanup, installers or
DEV-01.5. No removal of hashes or relabeling obsolete outputs as current.

## DEV-01.5 — Integrate to ARTestCLI

**Prerequisite:** DEV-01.4-A accepted; this amendment does not start DEV-01.5.

**Objective:** deliver the diagram's Integrate menu and the complete registration
result. Include `Integrate to ARTestCLI` and disabled `Integrate to ARTestStudio`.

**Components:** ARTestDev menu/controller, CLI installation discovery/profile and
registration services; existing registration behavior and preceding language
adapters. Reuse catalog/association/profile formats. No Studio code changes.

**Changes:** discover saved/bounded candidates; offer manual folder/executable
selection when absent and user choice when multiple. Verify the CLI/Engine pair,
compatibility and required capabilities; save the chosen target locally. Do not
require users to edit catalog paths/configuration JSON or install a new runtime
inside the project. Integration is one explicit action: check inputs, prepare/reuse
Python or consume current C++ outputs, validate, publish safely and verify installed
discovery. Stale C++ outputs produce a concrete request to rebuild in the IDE.
Finding an executable or copying a DLL is not integration success.

**Acceptance/tests:** remembered/detected/manual targets, multiple/absent/stale/
incompatible installations, permissions and no implicit elevation. Both languages,
all variants, repeat/update registration and another package's driver. Retain full
catalog validation, ownership, concurrent-writer exclusion, profile rollback and
Stage 4C interruption recovery: phase/topology, not equal inventories, determines
promotion. Prepare Python environments at final installation-owned paths; do not
move or patch them. Preserve previous selection on failure and prove installed
independence from project sources. Show operation/cause/correction and target/revision.
Studio option remains disabled even if ARTestStudio.exe exists: no search, launch,
write or accidental registration. Integration never starts a Test plan.

**Do not touch:** Studio implementation, runtime DLL replacement, public contracts,
active sessions, global registries/environments, automatic installers, run or hardware.

## DEV-01.6 — Secondary explicit Test plan validation/execution

**Objective:** retain optional Run Test plan after integration without adding a
required step to the diagram or running anything automatically.

**Components:** ARTestDev result adapter, current installed CLI commands and Python
execution orchestration. Display target/revision; preserve explicit local-source
versus registered-revision semantics. Default to the integrated revision.

**Acceptance/tests:** offline-invalid Test plan never executes; explicit simulated
Python/C++ execution, command-only with registered driver, preserved catalog/
associations/profile. Propagate verdict, errors, cancellation, timeout and uncertainty
without retries. Bounded cleanup and honest termination status. Native-only run
remains Python-independent. No execution on Generate, Edit, Integrate or recovery.

**Do not touch:** Engine callbacks/API, execution policy, C-03 physical recovery,
Test plan designer, Studio, hardware or active-session changes.

## DEV-01.7 — Authoring-only SDK and first-use acceptance

**Objective:** prove the exact GUI journey from the new distributed SDK against
an independent ARTestCLI installation, without internal knowledge or shell commands.

**Components:** new tooling-package declaration, minimal SDK packaging and a local
Windows x64 SDK installer with user-selected destination, inventory, required Qt
runtime/notices, automated acceptance and a short GUI first-use guide. No third-party
prerequisite bootstrapper or automatic updater. The actual installer build method
is selected/pinned within the focused stage handoff, not by changing Engine builds. Do not overwrite
accepted PY-DX-01 kits; their old inventory/compatibility checks stay strict.

**Acceptance/tests:** source/binary/SDK SHA-256 provenance and relevant regression
and compatibility gates; extracted SDK without checkout access or ARTestCLI/Engine
bundled inside it, and no Python runtime/interpreter/pre-created environment or
unrelated development outputs. Audit each packaged category against a mandatory
ARTestDev operation or project authoring need. Install into chosen C: and alternate
drive/space-containing locations; resolve resources independent of CWD. Exercise
read-only SDK files, external project outputs and no loss of projects/settings on
SDK uninstall. Do not require users to install the Qt development SDK.
Verify no PowerShell descendants in the required journey. Native path with Python
unavailable; Python path with an externally installed compatible interpreter and no
native compiler. A machine without Python still launches ARTestDev and receives
manual setup guidance; it must not silently download or create a kit interpreter. Two
languages/three variants, spaces/other CWD, repeat/update/failure and one runtime
shared by multiple projects. Create/open/edit without any installed ARTest runtime;
manual installation/editor selection and stale-output diagnostics. Studio is only
a disabled future option and is not a dependency for acceptance.

Observe an engineer unfamiliar with internals following Welcome -> Generate -> Edit
-> IDE edit/build -> Integrate to ARTestCLI -> confirmed result for Python and C++.
Record actual steps, outputs and assistance; verify a distinctive source change,
not an automated replacement of all matching strings. A separate explicitly
requested simulated Test plan may verify behavior; distinguish it from integration
success. Python/compiler/editor and separate CLI setup are disclosed beforehand and
performed manually; distinguish those prerequisites from the SDK installer itself. Required
coordinator intervention, shell commands or internal JSON edits block the new
novice-journey acceptance until corrected and reobserved. No promised fixed time.

**Do not touch:** public release/upload/signing, dependency downloads/installers,
hardware, C-03/C-04,
.NET, Studio runtime integration, Linux runtime port or historical evidence.

## Review and handoff

DEV-01.1/01.2 remain accepted. DEV-01.2-A adds automatic local resources and private
development staging; see [delivery and usage](../sdk/artestdev-dev01-2.md). Its review
must return `DEV-01.2-A ACCEPTED` or `DEV-01.2-A REQUIRES FIXES`; later stages remain pending.
Do not expand the fix handoff into all GUI stages. Each stage reports changes,
exact tests/results, limitations and candidate evidence; follow AGENTS.md integration
checks. This documentation update authorizes no commit/push or cross-task dispatch.
Stop and report architectural contradictions or required public-contract changes.

Known incidents remain in the canonical roadmap. Preserve failures and assess
current regressions; do not retry until green or claim the historical ARTESTPKG015
cause was proved. C-03/C-04 remain mandatory before .NET, and functional Studio
integration remains future work outside this delivery.
