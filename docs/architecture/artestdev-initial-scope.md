# DEV-01 — ARTestDev: graphical extension development

Status: scope and roadmap formalized on 2026-09-21 at the owner's request.
Owner amendments: separate SDK/projects from the runtime; follow the supplied
Welcome -> Generate -> Edit -> Integrate flow. Only ARTestCLI integration is in
scope. The ARTestStudio menu item is a disabled future placeholder. The latest
owner amendment removes bundled Python and limits missing-prerequisite handling
to diagnostics/manual setup; dependency downloading/installing is not included.
DEV-01.1/01.2/01.2-A/01.3/01.4/01.4-A are ACCEPTED. The performance/retention
amendment DEV-01.4-A was accepted on 2026-09-25 after its selective-build correction;
see the [current native-build guide](../sdk/artestdev-dev01-4.md#dev-014-a--incremental-native-builds-and-bounded-retention).
This acceptance does not start DEV-01.5.
The Architect previously resolved the SDK format blocker by
authorizing private development staging; see [the delivery guide](../sdk/artestdev-dev01-2.md#dev-012-a--localización-automática-y-staging-privado).
PY-DX-01 remains accepted and closed.
Position: after PY-DX-01, before C-03; C-03 and C-04 remain mandatory before .NET.
Authority: [AGENTS.md](../../AGENTS.md), [sequencing](roadmap-pre-dotnet.md)
and [delivery roadmap](../planning/artestdev-roadmap.md).

## Outcome

The owner's flow diagram and subsequent clarification define the primary journey:
**Welcome -> project form -> Generate -> Edit -> Integrate -> result**.
C++ editing and compilation take place in Visual Studio; Python editing takes
place in an external Python-capable IDE/editor and its preparation is coordinated
inside Integrate. There is no mandatory separate shell command or Build button.
This replaces the previously proposed primary **Build and integrate** action.

The integration destination implemented in DEV-01 is **ARTestCLI only**.
The menu must also show **Integrate to ARTestStudio**, disabled and identified as
future work. Do not discover or launch Studio, write its directories or implement
an adapter. Studio availability is not a dependency or acceptance gate for DEV-01.

This is a small development utility, not Studio, an IDE or another sequencer.
Basic Qt controls, a project summary and a persistent diagnostics panel suffice.
One open project and one mutating/check operation at a time are enough. The
normal journey requires no terminal, PowerShell installation, repository checkout
or manual internal configuration. Optional explicit Test plan execution remains
secondary and is never a prerequisite or automatic consequence of integration.

## Distribution amendment: SDK, project and installation

The owner rejected requiring a complete ARTestCLI runtime inside every new SDK
kit. The new distribution separates three responsibilities:

- **Authoring SDK and ARTestDev:** development tools, headers/templates, Python
  SDK/tooling and their necessary dependencies, shared by developer projects.
  No Python runtime/interpreter or pre-created environment is bundled. Use a
  compatible Python installed on the developer's machine. Only mandatory SDK
  wheels/locked authoring dependencies may accompany Python tooling; these are
  packages, not an interpreter. Qt runtime dependencies required by ARTestDev belong
  to its installation, not each project. Do not ship the Qt development SDK.
- **Extension project:** source/IDE project, portable configuration, Test plan and
  owned build/package outputs. C++ outputs include DLL plus generated metadata and
  schemas, not just the DLL. Python retains its existing private preparation
  outputs and verified environments. Do not copy ARTestCLI/Engine into projects.
- **ARTest installation:** separately installed or extracted CLI, Engine and their
  runtime dependencies. Multiple projects may reference the same selected local
  installation. The new authoring distribution must not embed that runtime.

The developer downloads `ARTestDevelopmentKit-0.X.0-evaluation-windows-x64`
and installs it at a chosen folder on C: or another drive. DEV-01.7 includes
selectable-destination SDK installation; this does not authorize public publication
or an installer for third-party prerequisites. Pin the actual version through the
kit version authority; do not replace the historical 0.4.0 artifact in place.

The SDK installation contains only project authoring resources (templates, native
headers/build support, Python SDK packages) and mandatory ARTestDev tools/scripts,
runtime libraries, inventories, notices and concise usage guidance. No ARTestCLI,
ARTestEngine, process workers, Python runtime, Visual Studio/compiler payload,
prebuilt sample outputs or old acceptance logs belong in the new SDK. Necessary
application libraries may overlap another product's dependencies; justify them by
ARTestDev's own use, never by an embedded ARTest runtime.
DEV-01.2-A requires resolving SDK resources from the actual ARTestDev executable
location, through one private service shared by inspection and Generate. Remove
manual SDK selection and ignore its old preference without deleting other settings.
Do not use CWD, PATH, personal paths, global searches or fallback SDKs. Missing,
corrupt or incompatible resources require repair/reinstall diagnostics and block
only dependent operations. Preserve integrity, compatibility and reparse checks.
The Architect authorizes private, provisional artestdev-staging.json: internal
version, platform/configuration, component versions, relative paths and full
SHA-256 file inventory. An explicit resource allowlist assembles separate Debug
and Release folders with ARTestDev/Qt, native-sdk/ and python/. Visual Studio
starts that executable. This resolves the blocker without changing historical
manifests or defining DEV-01.7's public format, ZIP 0.5.0 or installer.
Generated projects, environments, outputs,
local settings and logs stay outside the installed SDK. It must work from a
read-only installation directory without elevating ARTestDev for normal use.

ARTestDev discovers existing saved installation profiles and may use bounded PATH
candidate discovery. Provide a folder/executable picker when no candidate exists;
if several exist, ask the user to select one. Persist selection through the
existing local profile mechanism. Do not scan entire disks, assume an MSI/registry
installation exists, or download/install a runtime automatically. Verify declared
compatibility, architecture, necessary files and existing read-only CLI capabilities;
finding a file called ARTestCLI.exe is not proof of a usable installation.
Never activate packages or initialize devices merely to discover an installation.

No ARTestCLI installation is required to open/edit/create a project. Show separate readiness
for authoring, package validation, registration and execution. If runtime-dependent
validation is unavailable, explain the missing installation rather than declaring
the SDK/project corrupt. Do not mark a package validated/integrated on the strength
of compilation alone. Preserve local target paths outside portable source settings.

The current C++ SDK ships `ARTestSdkValidate.exe` plus `ARTestEngine.dll` and its
metadata build requires that validator. The new generated C++ project must instead
support IDE compilation of DLL and generated metadata without an ARTest runtime
installed. These are **compiled, not yet target-validated** outputs. DEV-01.4
separates that local output step from Engine-backed validation, which the Integrate
pipeline performs against the selected CLI installation before registration.
Preserve descriptor/integrity checks and generated metadata; prove equivalence
with the existing validation behavior. No source execution for hardware access.
Old SDK targets and frozen consumers keep their existing behavior unchanged.
Python does not compile into a native DLL; existing package/prepare operations
run automatically during its explicit Integrate action and reuse verified inputs.

The accepted all-in-one PY-DX-01 ZIP is historical and stays unchanged. Its declared
inventory must still be verified completely when opened: do not relax the old
manifest to make missing runtime files pass. Define a distinct, versioned tooling
package declaration for the new authoring-only distribution in the packaging work;
this is not a change to extension manifests, receipts, ABI or Engine API. Detection
of an independent installation does not repair a corrupt historical kit.

## Workspace and prerequisite diagnostics

The form suggests **`C:\Users\Public\ArtestDev`** as its initial workspace. The
user can choose another drive/folder, and the project is generated in its own
named child directory. Workspace and SDK installation are separate selections.
Check permissions, existing content and path containment; do not overwrite another
project or loosen permissions on the shared Public folder. Keep user preferences
and installation profiles per user, not shared merely because the workspace is.
Remember an explicitly changed workspace rather than resetting it on each launch.

ARTestDev itself and the native-only journey must start/work without Python.
For a Python operation, detect a compatible installed interpreter, validate its
actual implementation/version/architecture/GIL and required venv/pip capabilities,
and reuse the existing bounded checks. Current Python authoring requires standard
CPython 3.13 Windows x64 with the GIL; do not suggest arbitrary latest Python.
A launcher alias or an editor executable is not proof of a suitable interpreter.
A missing interpreter may block Python generation where it delegates to existing
Python tooling; inspection/navigation and native project generation remain usable.
There is no hidden fallback interpreter inside the SDK.

For C++, distinguish a Visual Studio IDE from its required MSBuild, C++ compiler,
toolset and Windows SDK components. Detect the versions required by the generated
extension project, independently of the compiler used to build ARTestDev itself.
Check other dependencies only for the selected language/operation/project; never
make a native-only user install Python or an unrelated vendor SDK.

When a prerequisite is missing/incompatible, show its name, required compatible
version/architecture/components, what was found, the blocked operation and what
the user must download/install manually. An official URL may be displayed as
reference; do not open the browser automatically. Provide a way to check again
and to select an already installed tool. Preserve the project and entered inputs.

**No Download and install action in this iteration.** Do not download installers,
invoke package managers/installer executables, request elevation for setup, change
machine PATH or repair installed tools. Opening/inspection stays read-only.
Python package preparation inside project-owned environments remains the existing
explicit Integrate operation; it is not authorization to install system Python or
Visual Studio. Vendor installers and generic dependency resolution remain out.

Distinguish: missing prerequisite, incompatible prerequisite, check pending,
check failed and ready. Re-evaluate after the user completes manual setup; neither
a displayed URL nor finding a file establishes that installation succeeded.

References for manual setup/discovery information:
- [Official Python Windows downloads](https://www.python.org/downloads/windows/).
- [Microsoft Visual Studio instance detection](https://learn.microsoft.com/en-us/visualstudio/install/tools-for-managing-visual-studio-instances).
- [Visual Studio installation guidance](https://learn.microsoft.com/en-us/visualstudio/install/install-visual-studio).

## Technology decision

Use **C++20 with Qt 6 Widgets**, initially Windows x64. Use CMake for this new
application and its private helpers/tests; Visual Studio is the initial IDE.
This does not migrate the Engine solution or extension SDK builds to CMake.
Qt supplies desktop widgets and cross-platform facilities for files/processes;
keep platform-specific tool discovery, PE checks, locking and process containment
behind small internal adapters. Do not create a general plugin framework.

C# with Avalonia is a viable Windows/Linux alternative. C++/Qt is selected to
keep the native authoring path independent of Python and .NET and to implement
the required native tooling without adding another implementation language.
A C# GUI would not itself constitute .NET extension hosting; these are separate
choices. Do not substitute a Windows-only UI framework for the selected stack.

Pin the tested Qt, CMake and application compiler versions in DEV-01.1. The Qt
platform matrix currently lists MSVC 2022 for Windows x64; do not assume the
repository's existing v145 extension toolchain is a supported Qt binary pairing.
The application and generated extensions may use different supported toolchains:
they communicate through processes/files, not a shared C++ binary interface.
Record the distinction and discover installations without personal fixed paths.
Ship required Qt runtime files/plugins and component notices with the kit;
`ARTestDev.exe` is an entry point, not a promise of a single-file deployment.

Linux is a design constraint, not a supported target in DEV-01. Keep UI and
project operations free of unnecessary Windows assumptions. Engine, native SDK,
Python runtime checks, publisher filesystem semantics and compiler discovery
still need separate Linux work and acceptance. Qt portability alone does not
make an ARTest extension or its vendor dependencies portable.

Primary references consulted for this decision:
- [Qt supported platforms](https://doc.qt.io/qt-6/supported-platforms.html).
- [Qt Widgets](https://doc.qt.io/qt-6/qtwidgets-index.html).
- [Qt with CMake](https://doc.qt.io/qt-6/cmake-get-started.html).
- [Qt component licensing](https://doc.qt.io/qt-6/licensing.html).
- [Avalonia supported platforms](https://docs.avaloniaui.net/docs/supported-platforms).

## Required screens, actions and states

The labels below implement the owner's diagram. Visual polish is out of scope.

| Screen / action | Required behavior and enabling condition |
| --- | --- |
| Welcome | Show `Welcome to ARTestDev`, `The Art of Testing` and the primary button `Create your own Driver, Command or both`. Offer opening an existing project as a secondary action. No kit inspector is a mandatory landing step. |
| Project form | On the primary click, navigate to a form with language (Python/C++), component kind (driver, command, both), name and destination folder, initially suggesting workspace `C:\Users\Public\ArtestDev`. Ask only for inputs needed by the selected variant; generate coherent internal IDs. Keep diagnostics visible. |
| Generate | Enable when required inputs and authoring resources are valid. Generate into a safe destination without overwriting user content; report exact failures in the panel and preserve the entered values. Do not require an IDE or ARTestCLI installation merely to generate. |
| Project ready / Edit | Enable Edit after successful generation or opening a valid existing project. Detect a suitable Python IDE/editor or a Visual Studio installation for C++; resolve ambiguity with a choice and allow manual executable/folder selection. Remember the editor preference locally. Open the project and identify the actual behavior file, not just metadata. |
| Editing / compilation | Developer edits Python in the editor or edits/builds C++ in Visual Studio. Opening/closing the IDE is not proof of a successful build. No automatic Test plan execution. |
| Integrate menu | Expose `Integrate to ARTestCLI` and disabled `Integrate to ARTestStudio` (future implementation). The CLI action is available for a recognized project; it may report build/target prerequisites instead of success. No Studio detection or writes. |
| CLI destination | On Integrate to ARTestCLI, resolve the remembered/detected compatible installation. Offer manual selection if missing, and a choice if ambiguous. Display the chosen target. Missing/incompatible installation leaves project and prior registration intact. |
| Integrating | For Python, prepare/reuse verified package inputs. For C++, require current IDE-produced DLL and generated metadata; if missing/stale, ask the user to build in Visual Studio and return to Integrate. Validate against the chosen runtime and register with existing ownership/recovery rules. |
| Result | Report integrated extension/revision and target only after publication and installed-target discovery succeed. Otherwise show the operation, cause, correction and whether the previous registration was preserved. An executable merely existing is never integration success. |

The diagnostics panel is available throughout generation, editor launch and
integration. Work is asynchronous; prevent duplicate operations and ignore results
from superseded selections. Provide separate states for invalid form, generating,
project ready, prerequisite missing/incompatible, editor unavailable, build required/stale, target unavailable,
checking/preparing, integrating, integrated, failed and recovery required.
Errors return to the relevant actionable state without clearing the project.
Recompute inputs/output identity at action time; a saved GUI flag is not proof.
Source changes invalidate readiness for the new revision while the previously
registered revision remains selected until a successful update.

Editor detection is bounded to known installed/configured candidates and explicit
selection, not a disk-wide search. A Python interpreter is not a Python IDE.
Do not require installing an IDE just to inspect/create a project. Check native
compiler components/toolset compatibility separately from finding Visual Studio.
Use executable paths and argument arrays when opening a user-selected editor;
launching the interactive editor must not block the UI or imply ownership of its
session. Closing ARTestDev must not kill the user's editor or Visual Studio.
No IDE plugin, code injection or embedded debugger is needed. Missing external
tools produce manual-install diagnostics; no dependency installation is offered.

A C++ package includes generated metadata/schemas alongside its DLL. DEV-01.4
must provide local build provenance adequate to reject stale source/output or
SDK/configuration combinations; this is tooling state, not a new public manifest.
Do not silently recompile C++ as part of Integrate or accept an arbitrary old DLL.
Python preparation/reuse remains internal to Integrate; no fake native compilation
step or manual receipt/environment administration is exposed to the developer.

Use a verified, separately located CLI installation; there is no bundled target.
Registration updates its configured catalog, not files beside Engine DLLs.
Save target/editor paths locally; preserve the portable project and Test plan.
The command-only starter uses the existing value-source contract with a compatible
simulated/registered driver. Do not invent a direct concrete-driver dependency.
A driver-only starter does not promise a measurement command it does not contain.

**Run Test plan**, if exposed by DEV-01.6, remains a separate secondary action
following integration, with target/revision shown and offline validation before
execution. Integration never means a Test plan passed or hardware was exercised.

## Reuse and removal of the shell dependency

Reuse behavior and compatibility, not the old all-in-one distribution requirement.

Reuse PY-DX-01 project/configuration formats, templates, Python `project.py` and
`package.py`, native metadata generation/validation, and installed CLI commands.
Keep Python preparation, receipt verification and exact-input reuse in Python
rather than implementing a second preparation engine in C++.

Current `artest.ps1`, `authoring.ps1` and `registration.ps1` contain orchestration
that must move into tested private application services where necessary. Preserve
their accepted behavior and retain the existing entry points for compatibility.
Use executable paths plus argument arrays; do not construct shell command strings.
Do not rely on `powershell.exe` or `pwsh.exe`, even invisibly, for the new journey.
Maintainer build/test scripts and the legacy user path may continue using them.

In particular, `source/ARTest.SDK/distribution/ARTestMetadata.targets` launches
`Publish-ARTestPackage.ps1` during a native build. Calling MSBuild from a GUI
alone does not remove this dependency. DEV-01 includes a native publisher and
an explicit new-kit build integration preserving generation, validation,
transaction ownership and recovery. Do not reinterpret existing SDK properties,
mutate frozen SDK baselines or silently convert existing projects. The new kit
must retain legacy entry points and offer its generated C++ projects a build
path that does not invoke PowerShell. This tooling replacement is not permission
to repair the historical solution build-order incident or change runtime contracts.

Preserve the existing portable/local split. Save interpreter, vendor paths
and installation selection locally; keep reusable source and Test plan paths
project-relative. No global environment pools or automatic vendor installation.
Expose supported local settings through basic file pickers, not internal JSON
editing. After manual prerequisite setup, the initial example uses the installed
Python and the SDK's mandatory pinned packages without automatic downloads. New
third-party dependencies may require explicit user-provided inputs and diagnostics.

## Native build experience amendment (2026-09-24)

DEV-01.4-A replaces the accepted implementation's always-Rebuild policy,
repeated broad hashing and indefinite retention with incremental IDE Build,
explicit Rebuild and bounded project-local native outputs. Content verification
and recovery remain required; no timestamp-only readiness, hash removal, or
DLL-only package is authorized. A DLL still needs generated metadata/schemas.
Only verified, settled, owned obsolete native outputs may be retired; unknown,
active or ambiguous state is preserved. Python preparation/environments and legacy
SDK targets are unchanged. See the guide for the measurement and acceptance gate.
This scoped local retention is distinct from out-of-scope global cleanup.

## Boundaries and invariants

- No Engine/Core linkage in ARTestDev. Use existing CLI subprocesses; keep Engine,
  ABI, Engine API, IPC, execution manifests, receipts and broker contracts intact.
  Do not introduce another validator, runtime or execution policy.
- Python remains out of process. Native-only projects do not discover, launch or
  require Python/.NET. Test this with a native-only catalog and unavailable Python.
- No loading vendor code, hardware initialization or Test plan execution during
  discovery, prerequisites, creation, build or registration. Preserve metadata-only
  generation and the existing descriptor-validation boundaries.
- Preserve eager activation, integrity, `Wait(30000)`, effect uncertainty and
  propagation. Do not retry an invocation after an indeterminate effect.
- Keep operations off the UI thread, bound child-process waits/output/cleanup and
  serialize mutations of a project/target across instances. Cancellation and app
  closure must not erase recovery evidence or claim unconfirmed termination.
- Preserve ownership, full-catalog validation, associations and profile rollback.
  Transaction phase and filesystem topology determine promotion, not equality of
  inventories. Unknown files/reparse points/ambiguous state fail closed.
- Prepare Python environments at final paths; do not move environments, patch
  launchers/receipts or invalidate another project's registered selection.
- Keep offline compilation data-only and registration separate from execution.
  Process termination proves neither hardware shutdown nor C-03 acceptance.

Out of scope: visual design/themes, embedded IDE/debugger, Test plan designer,
marketplace/download service, public release/signing, prerequisite downloads or
installation (Python, IDE/compiler, vendor tools), hardware qualification, global
cleanup, hot reload,
active-session replacement, streaming/IPC optimization, C# extensions, .NET host,
functional Studio integration (only the disabled menu item is included), C-03/C-04
implementation, Engine/SDK Linux port and general language adapters.

C-06 remains required for its existing Engine API/Studio integration scope.
This subprocess-based utility does not introduce API subscriptions or claim that
gate completed. A future direct Engine integration needs a separate decision.
