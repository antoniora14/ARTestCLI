# ARTest development kit 0.3.0 (evaluation)

This extracted Windows x64 kit is the PY-DX-01 Stage 4C candidate.
It contains independent component versions: native SDK 0.4.0, Python SDK 0.2.0,
a compatible private standard CPython 3.13 x64 GIL runtime, pinned example wheels,
and a matching Release CLI/Engine evaluation runtime.

From any current directory, verify the complete inventory and the private
`venv`/`pip` capability:

    & 'D:\SDK path\artest.ps1' verify

Create and build a starter without a repository or internal IDs. The normal
non-interactive flow needs only name, parent folder, and language:

    & 'D:\SDK path\artest.ps1' new --name 'My extension' `
        --folder 'D:\Work' --language python
    & 'D:\SDK path\artest.ps1' build --project 'D:\Work\My extension'

Use `--language cpp` for an IDE-buildable native project. C++ requires Visual
Studio 18 Insiders, Desktop development with C++, and MSVC v145 x64; pass
`--msbuild <path>` if discovery is ambiguous. `new` prompts for omitted normal
inputs. `build` uses the current directory when it contains a guided project,
otherwise it prompts for the project folder.

The default starter contains a simulated driver plus a broker-based command.
`--variant driver-only` and `--variant command-only` are also supported. The
command-only plan names the compatible sample driver expected at integration;
the command declares its required service contract and never links a driver.
C# is explicitly unavailable until the .NET gates.

The accepted low-level Python commands remain available:

    & 'D:\SDK path\artest.ps1' python-project create 'D:\Work\My extension' `
        --extension-id com.example.my-extension `
        --driver-id com.example.driver.simulated-source `
        --command-id com.example.command.measure-value `
        --author 'Example Lab'
    & 'D:\SDK path\artest.ps1' python-project check 'D:\Work\My extension'
    & 'D:\SDK path\artest.ps1' python-project prepare 'D:\Work\My extension'

The root entry locates only kit-relative tools. Generated IDs and references are
portable and persistent. Python prerequisite paths and C++ SDK/MSBuild selections
are written only to ignored local configuration. Preparation uses the bundled
exact wheelhouse with `pip --no-index`; it installs nothing globally.

Register with a named installation. The first use supplies the installed CLI and
separate catalog/configuration locations; later calls reuse the local profile:

    & 'D:\SDK path\artest.ps1' register --project 'D:\Work\My extension' `
        --target station-a --cli 'C:\Program Files\ARTest\ARTestCLI.exe' `
        --catalog 'D:\ARTest Data\extensions' --config 'D:\ARTest Data\configuration'
    & 'D:\SDK path\artest.ps1' register --project 'D:\Work\My extension' `
        --target station-a

Registration publishes only installation-owned revisions, prepares Python
environments at final paths, validates the whole catalog, and performs an
installed-CLI offline discovery check. It does not run a plan, initialize
hardware, replace CLI/Engine binaries, reload sessions, or elevate permissions.

This candidate provides Stage 4C `register` over accepted `new`/`build`. It does
not provide Stage 4 plan execution or Stage 5 acceptance. The nested native
SDK remains available at `native-sdk`; existing low-level consumers remain
independent of Python.

This is a local evaluation artifact, not a public release, signed installer,
package feed, ABI 1.0 promise, or redistribution-license decision.
