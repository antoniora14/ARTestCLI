# SDK authoring journey: install, create, build and register

Status: planning update requested by the owner on 2026-09-15.
This document translates the owner's flow diagram into acceptance requirements.
It changes delivery/authoring scope, not Engine architecture. No feature in this
document is claimed as implemented merely by recording this decision.

## User outcome

An engineer who does not know the repository must be able to:
1. Download and install/extract the ARTest development SDK.
2. Generate a project by providing its name, destination and supported language.
3. Edit the generated driver/command source.
4. Build using the SDK entry point or the supported native IDE/toolchain.
5. Register with the chosen ARTest installation using the SDK entry point.

A successful registration makes the extension discoverable by the selected
installation's configured catalog. Running a test is a separate explicit action;
registration must not initialize hardware or execute the example measurement.

Proposed entry point, to be implemented and tested rather than documented as
already available: `artest.ps1 new|build|register|run`. It lives at the SDK root
and locates bundled tools relative to itself. An interactive console wizard is
sufficient; a GUI is not required. Arguments provide an equivalent repeatable path.

The normal flow asks for name, folder and language. A driver+command example is
the default; a driver-only or command-only choice may be offered without making
extra expert inputs mandatory. A command-only template declares the existing
instrument capability it needs; it must not create a direct driver dependency.
Generate persistent IDs, schemas, references and a minimal plan consistently.
Advanced overrides remain available without becoming first-run requirements.

Only first-time workstation setup may ask for genuinely unavailable prerequisites
or an ambiguous target installation. Remember local selections. Do not require
manual edits to wheel paths, environment paths, receipts, associations or internal
IDs. Display a short success/failure summary, with detailed diagnostics available.

## Language behavior and prerequisites

| Language | Initial journey | Build behavior |
| --- | --- | --- |
| Python | Required for completion of PY-DX-01 | Delegate packaging and verified preparation to delivered Python tooling; do not describe this as compiling a native DLL |
| C++ | Narrow parity through existing installed SDK/template/build tools | Generate an IDE-buildable project; compile DLL and metadata with the supported native toolchain |
| C# | Clearly unavailable until D4.3/.NET gates are completed | Do not generate a supposedly runnable C# project or add a host now |

The shared entry point only dispatches to these existing authoring paths. No
generic language runtime, cross-language dependency resolver or unified execution
manifest is introduced. Language-version selection is future work; use the
currently supported matrix and validate it, without exposing untested versions.

The Python development kit must provide a compatible private CPython 3.13 x64
runtime with the venv/pip capabilities required by existing preparation, the ARTest
SDK wheel, tools/templates, and the pinned dependencies needed by the shipped
example. Validate the actual bundled layout: an arbitrary embeddable Python ZIP
is not assumed to support the current preparation mechanism. No global Python
installation is required for the Python starter. An explicit compatible local
interpreter override remains possible.

C++ requires the documented compatible native compiler/build components.
Detect them and give one actionable setup diagnostic if absent; do not bundle or
silently install Visual Studio. This prerequisite is disclosed in SDK setup,
not discovered after editing a project. Vendor-specific software/hardware remains
an explicit prerequisite only for projects that need it. Never auto-install PicoSDK.

Development-kit redistribution must have its included components and notices
reviewed before public publication. Creating/testing a local evaluation kit is not
a public release, signing service or package feed. Keep the existing product
licensing decision separate from implementation; do not claim a public download
is available until a real release artifact exists.

## Fit with the accepted work

| Diagram step | Current implementation | Required completion |
| --- | --- | --- |
| Download/install SDK | Native SDK packaging exists; Python tooling is repository-oriented | Stage 4A: complete evaluation development kit, including matching CLI/Engine/process dependencies |
| Generate project | Stage 1 creates a Python scaffold but exposes interpreter/tool paths and IDs | Stage 4B: SDK-root wizard/commands, generated defaults, C++ template dispatch |
| Edit code | Python and C++ examples exist | Stage 4B: clear edit points and minimal runnable example; no prerequisite knowledge of internal JSON |
| Build | Native build and Python Stage 3 preparation exist | Stage 4B: one build entry point; preserve IDE builds and low-level tools |
| Integrate with ARTest | Python associations exist within a development project | Stage 4C: explicit installation target, safe publication and discovery through that target |
| Optionally execute/check behavior | Stage 4 is planned | Keep Stage 4 and reuse its run orchestration from the installed SDK |

Stages 1-3 stay accepted. Stage 4 retains its original execution scope. Stages
4A, 4B and 4C are additional required delivery gates before Stage 5 acceptance;
their identifiers do not rename completed work. 4A can be implemented independently
of Stage 4; 4B depends on 4A, 4C depends on 4B, and final acceptance depends on all
four. Do not expand a Developer's existing Stage 4 assignment without a scoped
handoff. Stages 4A and 4B have now been accepted; the next delivery unit is 4C.

## Registration semantics

The phrase "where ARTestEngine.dll is installed" identifies a target installation,
not permission to overwrite that DLL or scatter package files beside it.

An installation profile, owned by authoring tooling, identifies the verified
runtime/CLI and writable catalog/configuration locations. The SDK's evaluation
runtime can be the default; an existing installation must be selected explicitly
or confirmed when ambiguous. Save this selection locally. Permission failures
must not trigger implicit elevation or writes to another installation.

Use existing catalog roots and Python environment mappings. The SDK entry point
must pass those settings to the installed CLI through its current arguments.
Engine does not gain a global registry or implicit folder scanning. Custom hosts
remain responsible for consuming their documented catalog configuration; do not
claim that every host automatically sees a registration.

`register` checks whether build outputs correspond to current sources and builds
if necessary, using the same validated mechanisms. Thus the separate build step
is useful for IDE feedback but need not become a redundant mandatory command.

Publish only into owned locations associated with the selected installation:
- Keep package versions isolated. Select one version per extension ID in the
  effective catalog; never scan all historical revisions as active duplicates.
- Python environments must be prepared at their final installation-owned paths.
  Never move a prepared environment, patch its receipt/launcher, or leave the
  installation dependent on a temporary directory or the author's source tree.
- Reuse existing native publication/validation where applicable. Validate the
  whole candidate catalog and relevant compatibility/integrity before switching
  the installation's selection. Keep the prior working selection on failure.
- Existing mappings/registrations for other packages must be preserved.
- Repeated identical registration is idempotent. Reject conflicting package
  ownership, incompatible targets and ambiguous updates with actionable errors.
- Keep existing sessions/catalogs immutable; an update is for a subsequent
  activation. Do not unload active drivers or delete outputs still in use.
- An independent installed CLI catalog check must demonstrate discovery of the
  registered extension. No device initialization or plan execution is implicit.

This is local development registration, not a global environment pool, service,
remote deployment system, migration engine or physical-safety mechanism.

## User-facing states and failure behavior

`SDK ready -> Project created -> Edited -> Built/prepared -> Registered`.
Optional `run` then performs offline plan compilation and explicit execution
through Stage 4. An edited registered project becomes "changes not registered";
the previous installed revision remains usable until a successful update.

Missing prerequisite -> one diagnosis and corrective action; resume from saved
configuration. Build failure -> remain unregistered or keep previous registration.
Publication failure -> retain prior selection and diagnostic evidence.
No hidden re-execution after uncertain device effects.

## Final acceptance from the downloaded artifact

A clean consumer location, with repository access absent, must complete the five
diagram steps using only the extracted SDK, documented native prerequisites and
the generated project. The Python starter needs neither a preinstalled Python
nor a native compiler. Test paths with spaces, a different CWD, an existing target,
the bundled target, and repeat/update/failure registration.

Verify actual installed-target discovery and an explicitly requested simulated
run, rather than only a project-local run. Also verify the registered copy remains
usable without access to the original project. Preserve compatibility tests for
existing native consumers and low-level Python commands.

FAIL if the normal path needs a repository checkout, build of ARTest itself,
manual wheel selection, hand-written IDs/receipts/mappings, manual package copying
or debugging of internal directory layouts. Required developer inputs for new
third-party dependencies may remain explicit, but cannot be smuggled into the
minimal first-run example.

Record automated evidence and a short observed first-use exercise by someone
unfamiliar with the repository. Hardware is not required; a claim of novice
usability cannot be based solely on maintainer-authored passing unit tests.
