# Development-kit provenance and third-party notices

This local evaluation kit preserves independent ARTest component versions in
`sdk-manifest.json`. The complete per-file SHA-256 inventory is authoritative for
the candidate; do not replace individual bundled files.

## CPython and pip

The private runtime is a standard GIL-enabled CPython 3.13 Windows x64 installed
distribution copied without global `site-packages`. Its exact patch version and
hashes are recorded in the manifest. The Python Software Foundation license is
included at `python/runtime/LICENSE.txt`. The standard-library `ensurepip` bundle
contains pip and its bundled license metadata. An arbitrary embeddable Python ZIP
is not a supported substitute because Stage 3 requires working `venv` and pip.

## Python example dependencies

The unmodified wheels for protobuf 6.33.4 and pywin32 311 are stored in
`python/wheels` and are pinned by name, version and SHA-256. Their distribution
metadata and license files remain inside the wheel archives. Protobuf is provided
under its BSD 3-Clause license. pywin32 is provided under the Python Software
Foundation license terms recorded by that distribution.

## Microsoft runtime

The Release evaluation runtime may contain app-local Microsoft Visual C++
Redistributable DLLs copied from the supported Visual Studio v145 redist layout.
Their exact names and hashes are recorded in the manifest. This local evaluation
does not broaden Microsoft redistribution rights or install a global runtime.

## Native SDK dependency

The nested native SDK includes nlohmann/json 3.12.0 and its MIT notice in
`native-sdk/THIRD_PARTY_NOTICES.md` and the vendored header. ARTest licensing and
public redistribution terms remain a separate owner decision.
