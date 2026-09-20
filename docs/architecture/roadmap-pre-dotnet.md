# ARTestCLI pre-.NET roadmap

Status: approved planning decision, 2026-09-13. PY-DX-01 Stages 1-3 and 4A-4C are
accepted; Stages 4 and 5 remain pending. The 2026-09-15 journey adds mandatory
delivery gates 4A/4B/4C before Stage 5. Stage 4 is the current delivery unit.
This file is the canonical ARTestCLI sequencing record after C-02. Earlier
cross-project planning in ARTestStudio is historical for this sequencing decision;
it does not override this record. Historical D-stage identifiers are unchanged.

## Decision and baseline

C-02 is **ACCEPTED and closed**, committed in
`3ca474ee0f10fc2a3d238a244b4a1c71fc0a24c6` and pushed according to the owner's
closure confirmation. The inspected branch is `main`; its local upstream
reference matches that commit. The dedicated acceptance was executed against
source candidate
`20d3bb6b160ab7f6921fefc45219b7db6c670a3dc5381586d75b7cdb7708aa1c`,
before the commit was created. Candidate identity is not a Git commit hash.

The owner approved **PY-DX-01 — Python Developer Experience: create, prepare and
run** before C-03. This is Python authoring tooling over existing mechanisms,
not a new runtime architecture, a contract revision or a multi-language platform.
See the [execution plan](../planning/py-dx-01-execution-plan.md) for its complete
scope and staged acceptance. The [SDK authoring journey](../planning/sdk-authoring-journey.md)
records the install -> create -> edit -> build -> register outcome and the new
extracted-kit, guided-authoring and installation-registration gates. C++ parity
reuses its existing SDK; C# remains deferred to .NET.

## Mandatory order

| Order | Work | Current disposition |
| --- | --- | --- |
| 1 | C-01: external-effect uncertainty | Completed prerequisite; preserve its result semantics |
| 2 | C-02: bounded TCP SDK example | ACCEPTED and closed; see the [checkpoint](checkpoint-c02-tcp-hello.md) |
| 3 | PY-DX-01: Python Developer Experience: create, prepare and run | Stages 1-3 and 4A-4C accepted; Stage 4 and 5 pending |
| 4 | C-03: cleanup, recovery and unconfirmed physical state | Pending; mandatory before .NET |
| 5 | C-04: minimal modular instrument scenario | Pending; depends on C-03 and remains mandatory before .NET |
| 6 | D4.3: .NET runtime parity | Not authorized by this decision; requires the preceding gates and a separate start decision |
| 7 | D4.4: deployment, compatibility, resource soak and measured performance | Subsequent acceptance work |
| 8 | D5: Studio integration | Subsequent work with its own integration gates |

PY-DX-01 does not absorb C-03 or C-04 and does not claim their acceptance.
C-01..C-04 remain the pre-.NET consolidation gates; this decision adds PY-DX-01
to the required sequence. Completion of a tooling stage does not advance a phase.

## Delivery sequencing added on 2026-09-15

Stage 4 keeps its scoped run orchestration. New Stage 4A packages the development
kit; 4B depends on 4A and supplies guided create/build; 4C depends on 4B and supplies
registration with a selected installation. 4A may proceed independently of Stage
4 and is the first new delivery unit. Stage 5 waits for Stage 4 and all three new
gates, including a first-use exercise and installed-target verification.
This is an explicit extension of the original repository-oriented authoring scope,
not retroactive failure of accepted Stages 1-3 or authorization of .NET. It does
not create a multi-language runtime or waive the following C-03/C-04 gates.

## Preserve the remaining checkpoints

C-03 distinguishes cleanup attempted, acknowledged shutdown and unknown physical
state. It must cover partial initialization, disconnect and cancellation, bounded
device-specific recovery and ownership that prevents recovery racing device I/O.
Acceptance needs separate missing-ACK and confirmed-shutdown cases, retained
failure/uncertainty and cleanup opportunities. Simulator evidence proves only
simulated state. Generic emergency scripts in Core, physical-safety guarantees
and untested watchdog/interlock claims remain excluded.

C-04 uses stable contracts/manifests, configured resources and a session-scoped
inventory discovered and validated during Initialize. Offline compile validates
structure, IDs and ranges without hardware. Acceptance covers present/absent
resources, incompatible capability, invalid IDs/ranges, separate instances and
operation-state rejection. No wildcard relaxation, dynamic manifest rewrite,
hot-plug, plan expansion or general preflight phase is introduced.

C-05 (build accessibility and measured Windows CI), C-06 (subscription quiescence
and session ownership) and C-07 (stability criteria and external review) remain
separate pending work. They are not blanket prerequisites for PY-DX-01 or .NET;
a concrete shared-contract regression may change that disposition. C-06 remains
mandatory before D5 execution/UI integration or stabilization of that API.
C-07 does not declare ABI 1.0. This decision does not initiate any of these items.

## Known incidents and parallel work

| Item | Disposition for PY-DX-01 |
| --- | --- |
| Python Debug/Release timeout | Closed: `TIMEOUT CLOSURE ACCEPTED`. Do not reopen without a concrete regression. Preserve eager activation, integrity and `Wait(30000)`. |
| `ARTESTPKG015` | Separate, nonblocking publication incident. A sharing-violation mechanism was reproduced; exact attribution of the historical failure remains unproven because its original log is unavailable. Do not claim the build graph was repaired. If current tooling validation encounters it, preserve the log and report the affected gate; graph repair needs separate scope. |
| ProcessWorker Release intermittent failure | Cause remains unresolved; isolated rerun and later complete aggregate passed. Keep as a separate follow-up, not an automatic blocker. A recurrence must be recorded and assessed, never hidden with retries until green. |
| PicoScope/support | May continue in parallel as device-specific feedback. Physical availability, vendor installation and latency work are not required to accept the hardware-free initial tooling flow. |
| Additional C-02 fault/concurrency cases and other deferred improvements | Remain deferred unless the new tooling exposes a concrete regression relevant to its own acceptance. Do not expand this iteration to collect unrelated fixes. |

## Closure evidence and document ownership

The C-02 evidence index is
`artifacts/acceptance/c02/dedicated-acceptance-20d3bb6b-20260913T005756Z/acceptance-record.md`
with `provenance.json` and `SHA256SUMS.txt`. The timeout record remains under
`artifacts/acceptance/c02/python-timeout-sha-only/current-eager-candidate/`.
These are local generated evidence, not a requirement to commit artifacts or
manual reports. This roadmap records the accepted decision; it does not rerun or
replace those records.

The [PicoScope developer-experience feedback](python-extension-developer-experience-feedback.md)
is a historical input, now located in ARTestCLI. Its original proposals are not
all approved features. The scope in the execution plan governs this iteration.
