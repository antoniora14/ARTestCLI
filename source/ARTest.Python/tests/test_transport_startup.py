"""Cold worker import regression against an existing long-path prepared environment.

Run with the receipt's base interpreter and -I -B -S; the environment is read-only.
The negative control restores only the old import order in memory.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import subprocess
import sys


def child(launcher, transport, legacy):
    assert sys.flags.isolated and sys.flags.no_site and sys.dont_write_bytecode
    assert not any(name in sys.modules for name in
                   ("pywintypes", "win32file", "win32event", "artest_host.transport"))
    # Use exactly the generated launcher's path/DLL setup, without starting main.
    prefix, separator, _ = launcher.read_text(encoding="utf-8").partition(
        "from artest_host.__main__ import main")
    assert separator, "Unrecognized prepared launcher"
    exec(compile(prefix, str(launcher), "exec"), globals())
    dll = launcher.parent / "Lib/site-packages/pywin32_system32/pywintypes313.dll"
    assert dll.is_file() and len(str(dll)) > 260, str(dll)
    assert not any(name in sys.modules for name in ("pywintypes", "win32file", "win32event"))
    print(json.dumps({"dll": str(dll), "length": len(str(dll)),
                      "preloaded": False, "legacy": legacy}), flush=True)
    source = transport.read_text(encoding="utf-8")
    if legacy:
        corrected = "import pywintypes\nimport win32file\nimport win32event"
        assert source.count(corrected) == 1, "Expected corrected production imports"
        source = source.replace(corrected, "import win32file\nimport win32event\nimport pywintypes")
    spec = importlib.util.spec_from_file_location("artest_host.transport", transport)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    exec(compile(source, str(transport), "exec"), module.__dict__)
    from artest_host.__main__ import main
    assert callable(main)
    print("Cold worker imports passed", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--launcher", type=Path, required=True)
    parser.add_argument("--transport", type=Path,
                        default=Path(__file__).resolve().parents[1] / "artest_host/transport.py")
    parser.add_argument("--child", action="store_true")
    parser.add_argument("--legacy", action="store_true")
    args = parser.parse_args()
    if args.child:
        child(args.launcher, args.transport, args.legacy)
        return
    for legacy in (True, False):
        command = [sys.executable, "-I", "-B", "-S", str(Path(__file__).resolve()),
                   "--child", "--launcher", str(args.launcher), "--transport", str(args.transport)]
        if legacy:
            command.append("--legacy")
        result = subprocess.run(command, capture_output=True, text=True, timeout=30)
        print(json.dumps({"command": command, "exit": result.returncode,
                          "stdout": result.stdout, "stderr": result.stderr}), flush=True)
        if legacy:
            assert result.returncode != 0 and "ImportError" in result.stderr and "pywintypes" in result.stderr, "Old order did not reproduce the DLL import failure"
        else:
            assert result.returncode == 0, "Production worker cold import failed"
    print("PASS: old order fails; production order passes in separate cold processes")


if __name__ == "__main__":
    main()
