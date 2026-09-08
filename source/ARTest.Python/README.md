# ARTest Python SDK and host

Start with [the authoring guide](../../docs/sdk/python-extension-authoring.md).

- artest_sdk: public component API, explicit results and typed schemas.
- artest_host: private process protocol, lifecycle and service dispatch.
- examples/simulated: metadata definition, power driver and measurement command.
- tools/package.py: explicit SDK build, metadata generation and environment preparation.
- requirements.lock: supported Windows CPython 3.13 x64 runtime dependency hashes.
- tests: SDK behavior tests, without physical instruments.

No Engine or sequencing policy belongs in this package. It is an experimental
Windows-only evaluation SDK, not an ABI stability or public release declaration.
