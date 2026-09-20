# ARTest development kit 0.1.0 (evaluation)

This extracted Windows x64 kit is the PY-DX-01 Stage 4A development artifact.
It contains independent component versions: native SDK 0.4.0, Python SDK 0.2.0,
a compatible private standard CPython 3.13 x64 GIL runtime, pinned example wheels,
and a matching Release CLI/Engine evaluation runtime.

From any current directory, verify the complete inventory and the private
`venv`/`pip` capability:

    & 'D:\SDK path\artest.ps1' verify

Use the accepted low-level Python workflow without a repository or global Python:

    & 'D:\SDK path\artest.ps1' python-project create 'D:\Work\My extension' `
        --extension-id com.example.my-extension `
        --driver-id com.example.driver.simulated-source `
        --command-id com.example.command.measure-value `
        --author 'Example Lab'
    & 'D:\SDK path\artest.ps1' python-project check 'D:\Work\My extension'
    & 'D:\SDK path\artest.ps1' python-project prepare 'D:\Work\My extension'

The root entry locates only kit-relative tools and writes the generated project's
machine-local paths after `create`. Preparation uses the bundled exact wheelhouse
with `pip --no-index`; it installs nothing globally and prepares the environment
directly in its immutable final project revision.

Stage 4A intentionally does not provide the guided `new`/`build` wizard, C++
dispatch, installation registration, plan execution, or final journey acceptance.
Those remain Stages 4B, 4C, Stage 4, and Stage 5 respectively. The nested native
SDK remains available at `native-sdk`; use its README and supported MSVC v145
toolchain for existing C++ consumers.

This is a local evaluation artifact, not a public release, signed installer,
package feed, ABI 1.0 promise, or redistribution-license decision.
