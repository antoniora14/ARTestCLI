# Private ARTestDev development staging

This is not a released SDK, ZIP or installer. ARTest resources come from the
repository's explicit StageDevelopment.ps1 allowlist. Native headers and the
starter are version 0.4.0; Python tooling/templates target SDK 0.2.0. Generate/Edit
do not require an interpreter, prepared environment or Engine in this tree.
DEV-01.3 adds private preparation tooling, the SDK wheel and the explicitly
verified protobuf 6.33.4/pywin32 311 offline wheels. Their metadata and license
files remain inside the original wheels. They are installed only into owned
project environments by the private preparation service, using external CPython.
DEV-01.4 adds ARTestDevNative.exe and private ARTestDevNative.targets. Generated
C++ projects build DLL/metadata without a runtime and await separate CLI validation.
Use GENERATE-EDIT project guidance, not historical publication commands.

Qt 6.8.3 Core/Gui/Widgets/Concurrent and the Windows platform plugin are copied
from the maintainer's configured Qt SDK; its SPDX notice accompanies these files.
Qt is copyright The Qt Company Ltd. and other contributors, under its applicable
LGPL/GPL/commercial licenses; see https://www.qt.io/licensing/ and the SPDX file.
This development copy does not assert public redistribution readiness.
The nlohmann/json copyright/license is preserved in its header and in
native-sdk/THIRD_PARTY_NOTICES.md. No third-party runtime is downloaded or installed.
