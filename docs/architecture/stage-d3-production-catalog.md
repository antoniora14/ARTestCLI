# Stage D3.1 - production extension catalog

## Decision

D3.1 separates package discovery from native code activation and promotes the
catalog from a vertical-slice loader to an inspectable, failure-contained
service. Manifest validation is safe to run in CI, an installer, ARTestCLI, or
future ARTestStudio because it does not call `LoadLibrary`.

The Engine API advances from experimental `0.2` to experimental `0.3` by
appending `validate_catalog`. API `0.1` and `0.2` table sizes remain negotiated
and covered by write-boundary tests. This is not an ABI `1.0` freeze.

## Processing pipeline

```text
approved root
    -> deterministic package discovery
    -> JSON and manifest contract validation
    -> path containment and optional SHA-256 verification
    -> global duplicate-ID validation
    -> native DLL load and ABI query (doctor/refresh only)
    -> manifest/binary descriptor comparison
    -> registry conflict preflight
    -> catalog activation
```

`extensions validate` and `extensions list` stop before native DLL loading.
`extensions doctor` executes the complete pipeline and exposes binary/manifest
mismatches that static validation cannot detect.

## Validation rules

- The approved root must exist and contain at least one package directory with
  `artest-extension.json`.
- Manifests are limited to 1 MiB and must use `schemaVersion: 1`.
- Stable IDs use lower-case reverse-domain notation; versions use semantic
  versioning.
- D3.1 activates only native, in-process, x64 packages compatible with native
  ABI `0.1`.
- Runtime entries and declared schema files must exist and remain inside the
  package after canonical path resolution.
- Extension IDs and component type IDs are unique across the complete catalog.
- When `integrity.sha256` is present, Windows CNG computes and compares SHA-256
  before executable code is loaded.
- Manifest extension ID, component count, kind, type ID, and contract ID must
  agree with the binary descriptors.

Validation failures are returned as data in the catalog report. A successful C
ABI call means the report was produced; callers must inspect `valid` to obtain
the catalog verdict. Transport failures still use `ARTestStatus`.

The reference extension projects call `scripts/package-extension.ps1` after
linking. It copies the binary and writes a deployment manifest containing that
configuration's SHA-256. The source manifest intentionally has no build-specific
hash.

## Failure containment and activation

All candidate module handles and descriptors remain local until validation and
binary inspection finish. A rejection destroys those candidates and leaves no
active catalog. A corrected root can then be submitted to the same Engine.

The Engine owns its command and instrument registries, and catalog refresh is
serialized. Registration is append-only after a complete conflict preflight.
Once activation succeeds, the in-process catalog is immutable for that Engine
instance. Hot reload requires a later generation-aware registry design and is
deliberately outside D3.1.

## Catalog report v2

Every report contains:

- `status`: `notLoaded`, `validated`, `rejected`, or `active`;
- `valid`: aggregate manifest and package verdict;
- `generation`: `0` before activation and `1` after the D3.1 activation;
- approved canonical `root` and supported native `abi`;
- deterministic package summaries with local diagnostics;
- activated manifest snapshots in `extensions`;
- flattened diagnostics for automation.

The JSON contract is stored in
`source/ARTest.SDK/schemas/extension-catalog.schema.json`.

## CLI operations

```powershell
$cli = '.\artifacts\bin\x64\Release\ARTestCLI.exe'
$extensions = '.\artifacts\extensions\x64\Release'

& $cli extensions list $extensions
& $cli extensions validate $extensions
& $cli extensions doctor $extensions
```

An invalid catalog returns process exit code `6`. `validate` emits the full JSON
report without loading DLLs. `doctor` emits Engine diagnostics, activates a
valid catalog, and then emits the active snapshot.

## Deferred hardening

- Publisher signature and certificate trust enforcement.
- Native out-of-process isolation and crash recovery.
- Hot reload and catalog generation replacement.
- Python and .NET runtime-host discovery.
- Installation, dependency resolution, and publisher policy.
