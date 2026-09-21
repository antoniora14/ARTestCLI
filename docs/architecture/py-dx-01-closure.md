# PY-DX-01 final closure

Status: **PY-DX-01 STAGE 5 ACCEPTED** and **PY-DX-01 ACCEPTED — PHASE MAY CLOSE**.
This records the Architect decision after the owner explicitly deferred the
PowerShell diagnostic and editing-guidance findings. All Stages 1-5 and 4A-4C
are accepted. This document records disposition, not a new test execution.

## Evidence and identity

- Stage 4 baseline commit: `8b8427ff04167990012ba5bb855fccffa5fcf694`.
- Stage 5 candidate: `artifacts/acceptance/py-dx-01/stage5-candidate/20260921T004123Z-9c977c49/`.
- Preserved ZIP: `ARTestDevelopmentKit-0.4.0-evaluation-windows-x64.zip`.
- SHA-256: `3f14011c33511e7b4bd78588573eea29dd2b44da1c98b0cb9bca9b01c7bc989b`.
- Human record: `quality/manual-tests/py-dx-01/PY-DX-01_Stage_5_Human_Observation_Live.md`.
- Automated matrix: 35/35; native Debug/Release 242 passed plus 31 disabled each;
  Python project 64 passed plus 2 real-symlink skips; SDK 10/10; worker 4/4 and
  Python integration 27/27 per configuration; SDK authoring 68/68 and frozen
  native consumer 8/8 per configuration. Deterministic reparse guards passed.
- Human result: Python PASS and C++ PASS by participant disposition, with all
  assistance retained. Python runtime passed, but the edited description was
  not the runtime message; do not claim the customized Python message was observed.
- Intermediate evidence-parser failure and overlong-path activation failure remain
  preserved with their dispositions. Short test paths succeeded; long-path support
  is not claimed. Missing human wall-clock timings remain a recorded limitation.

Original candidate records (including `human-exercise.md` marked PENDING) and
ZIP are preserved unchanged. This closure supersedes their historical acceptance
status; it does not rewrite their observations or checksums. Generated evidence
and completed manual documents are local, not part of the source commit.
Temporary exercise installations were subsequently removed at the owner's request;
the candidate ZIP, logs and human observation remain retained.

## Owner-approved non-blocking deferrals

- PowerShell 7 remains a prerequisite. Windows PowerShell 5.1 is unsupported by
  mandatory APIs; the kit lacks an early version diagnostic. Human execution
  required installing PowerShell 7.6.6; automated validation used PowerShell 7.
- Python starter has two matching message strings; the original exercise did not
  distinguish the runtime result from extension description. Automation replaced
  both occurrences. Preserve this as a usability observation, not an activation bug.
- C++ `new` suggests `Extension.cpp`, while the message exercise uses
  `ReadValueCommand.h`. The source-edit hint needs future clarification.
- Coordinator command/expectation errors, TEMP clarification and other assistance
  remain recorded. Completion does not prove an unassisted first-use experience.

The owner deferred these findings; no further human exercise is required for this
candidate's closure. Future changes require their applicable checks and new hashes.
See [current authoring guidance](../sdk/development-kit.md) for practical clarifications.

## Next-work boundary

ARTestDev.exe is a proposed GUI, not an implemented or approved delivery stage.
A separate scoped decision must define its relation to the roadmap. A GUI which
calls PowerShell scripts still depends on PowerShell. C-03 and C-04 remain
mandatory before .NET; this closure starts neither. No hardware qualification,
Studio integration, public release, signing, feed or ABI freeze is implied.
