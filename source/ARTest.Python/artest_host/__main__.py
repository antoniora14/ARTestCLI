"""Private worker entry point. Never invoke manually; Engine supplies bootstrap."""
import argparse
import asyncio
import importlib
import json
from pathlib import Path
import sys
from .transport import Transport
from .worker import Worker

def main():
    if sys.version_info[:2] != (3, 13) or sys.maxsize != 2**63 - 1 or not sys._is_gil_enabled():
        raise RuntimeError("Host requires standard CPython 3.13 x64")
    parser = argparse.ArgumentParser()
    parser.add_argument("--package", required=True)
    parser.add_argument("--artest-bootstrap", type=int, required=True)
    args = parser.parse_args()
    root = Path(args.package).resolve(strict=True)
    manifest = json.loads((root / "artest-extension.json").read_text(encoding="utf-8"))
    runtime = manifest["runtime"]
    sys.path.insert(0, str(root / runtime["entry"]))
    module, symbol = runtime["entryPoint"].split(":")
    extension = getattr(importlib.import_module(module), symbol)()
    transport = Transport(args.artest_bootstrap)
    if extension.identity != transport.bootstrap["extension"]: raise ValueError("Extension identity mismatch")
    transport.handshake()
    asyncio.run(Worker(transport, extension).run())

if __name__ == "__main__": main()
