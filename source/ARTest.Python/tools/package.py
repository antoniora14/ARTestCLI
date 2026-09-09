"""Explicit build/preparation commands. Runtime never invokes pip or this module."""
import argparse
import base64
import csv
import hashlib
import importlib
import io
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import zipfile

def digest(path): return hashlib.sha256(path.read_bytes()).hexdigest()
def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, ensure_ascii=True) + "\n", encoding="utf-8")
def checked(command): subprocess.run([str(item) for item in command], check=True)
def inventory(root):
    return [{"path": item.relative_to(root).as_posix(), "sha256": digest(item)}
            for item in sorted(root.rglob("*"))
            if item.is_file() and item.name != "artest-environment.json"]
def verify_inventory(root, records, excluded):
    expected = {item["path"]: item["sha256"] for item in records}
    actual = {}
    for item in root.rglob("*"):
        if item.is_symlink() or item.is_junction(): raise ValueError("Reparse points are not supported")
        if item.is_file() and item.relative_to(root).as_posix() != excluded:
            actual[item.relative_to(root).as_posix()] = digest(item)
    if actual != expected: raise ValueError("Full file inventory mismatch")
def runtime_check():
    if sys.version_info[:2] != (3, 13) or sys.maxsize != 2**63 - 1 or not sys._is_gil_enabled():
        raise RuntimeError("D4.2 requires standard GIL-enabled CPython 3.13 x64")

def sdk(args):
    runtime_check()
    source = Path(__file__).resolve().parents[1]
    args.output.mkdir(parents=True, exist_ok=True)
    wheel = args.output / "artest_python-0.2.0-py3-none-any.whl"
    with tempfile.TemporaryDirectory(prefix="artest-sdk-") as scratch:
        root = Path(scratch)
        for name in ("artest_sdk", "artest_host"):
            shutil.copytree(source / name, root / name, ignore=shutil.ignore_patterns("__pycache__", "*.pyc"))
        checked([args.protoc, "--proto_path=" + str(args.protocol.parent),
                 "--python_out=" + str(root / "artest_host"), args.protocol])
        info = root / "artest_python-0.2.0.dist-info"
        info.mkdir()
        (info / "METADATA").write_text(
            "Metadata-Version: 2.1\nName: artest-python\nVersion: 0.2.0\n"
            "Requires-Python: >=3.13,<3.14\nRequires-Dist: protobuf==6.33.4\n"
            "Requires-Dist: pywin32==311; sys_platform == 'win32'\n", encoding="utf-8")
        (info / "WHEEL").write_text("Wheel-Version: 1.0\nGenerator: ARTest\nRoot-Is-Purelib: true\nTag: py3-none-any\n", encoding="utf-8")
        record = io.StringIO()
        writer = csv.writer(record, lineterminator="\n")
        for item in sorted(root.rglob("*")):
            if item.is_file():
                encoded = base64.urlsafe_b64encode(hashlib.sha256(item.read_bytes()).digest()).rstrip(b"=").decode()
                writer.writerow([item.relative_to(root).as_posix(), "sha256=" + encoded, item.stat().st_size])
        writer.writerow([info.name + "/RECORD", "", ""])
        (info / "RECORD").write_text(record.getvalue(), encoding="utf-8")
        with zipfile.ZipFile(wheel, "w", zipfile.ZIP_DEFLATED) as archive:
            for item in sorted(root.rglob("*")):
                if item.is_file():
                    info = zipfile.ZipInfo(item.relative_to(root).as_posix(), date_time=(2026, 1, 1, 0, 0, 0))
                    info.compress_type = zipfile.ZIP_DEFLATED
                    archive.writestr(info, item.read_bytes())
    print(wheel)

def package(args):
    runtime_check()
    sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
    sys.path.insert(0, str(args.source.resolve()))
    sys.dont_write_bytecode = True
    module, symbol = args.entry_point.split(":")
    definition = getattr(importlib.import_module(module), symbol)().describe()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="artest-package-", dir=args.output.parent) as scratch:
        root = Path(scratch) / "package"
        shutil.copytree(args.source, root / "code", ignore=shutil.ignore_patterns("__pycache__", "*.pyc", ".venv", ".git"))
        shutil.copy2(args.lock, root / "requirements.lock")
        manifest = {key: value for key, value in definition.items() if key != "components"}
        manifest["schemaVersion"] = 3
        manifest["runtime"] = {"kind": "python", "entry": "code", "entryPoint": args.entry_point,
            "runtimeVersion": "3.13", "dependencyLock": "requirements.lock", "architecture": "x64",
            "isolation": "outOfProcess", "protocol": {"major": 0, "minor": 2}}
        manifest["components"] = []
        for component in definition["components"]:
            component = dict(component)
            schema, role, identity = component.pop("schema"), component.pop("schemaRole"), component.pop("schemaId")
            path = "schemas/" + component["typeId"] + ".json"
            write_json(root / path, schema)
            component["schemas"] = [{"role": role, "schemaId": identity, "path": path,
                                    "mediaType": "application/json; charset=utf-8"}]
            manifest["components"].append(component)
        manifest["inventory"] = inventory(root)
        write_json(root / "artest-extension.json", manifest)
        if args.output.exists():
            previous = json.loads((args.output / "artest-extension.json").read_text(encoding="utf-8"))
            verify_inventory(args.output, previous["inventory"], "artest-extension.json")
            if previous != manifest:
                raise FileExistsError("Package changed. Build into a new output root and prepare a matching environment.")
        else: root.rename(args.output)
    print(args.output)

def prepare(args):
    runtime_check()
    package_root = args.package.resolve(strict=True)
    manifest = json.loads((package_root / "artest-extension.json").read_text())
    verify_inventory(package_root, manifest["inventory"], "artest-extension.json")
    if args.output.exists():
        receipt = json.loads((args.output / "artest-environment.json").read_text())
        verify_inventory(args.output, receipt["files"], "artest-environment.json")
        if receipt["packageSha256"] != digest(package_root / "artest-extension.json") or receipt["sdkSha256"] != digest(args.sdk):
            raise FileExistsError("Environment inputs changed. Prepare a new environment directory.")
        if receipt["interpreterSha256"] != digest(Path(sys.executable)):
            raise ValueError("Pinned interpreter changed")
        print(args.output / "artest-environment.json")
        return
    checked([sys.executable, "-I", "-m", "venv", args.output])
    python = args.output / "Scripts/python.exe"
    checked([python, "-I", "-m", "pip", "--isolated", "install", "--disable-pip-version-check",
             "--require-hashes", "--only-binary=:all:", "-r", package_root / manifest["runtime"]["dependencyLock"]])
    checked([python, "-I", "-m", "pip", "--isolated", "install", "--no-deps", args.sdk.resolve()])
    checked([python, "-I", "-m", "pip", "--isolated", "check"])
    site = args.output.resolve() / "Lib/site-packages"
    # Launch the pinned base executable directly (no Windows venv redirector child).
    # -I -S excludes ambient packages and executable .pth startup hooks.
    launcher = args.output / "artest-launch.py"
    launcher.write_text("import os, sys\n"
        + "sys.prefix = " + repr(str(args.output.resolve())) + "\n"
        + "sys.path[:0] = " + repr([str(site), str(site / "win32"), str(site / "win32/lib")]) + "\n"
        + "_dll = os.add_dll_directory(" + repr(str(site / "pywin32_system32")) + ")\n"
        + "from artest_host.__main__ import main\nmain()\n", encoding="utf-8")
    receipt = {"schemaVersion": 1, "extensionId": manifest["extensionId"],
        "packageSha256": digest(package_root / "artest-extension.json"),
        "interpreter": str(Path(sys.executable).resolve()), "interpreterSha256": digest(Path(sys.executable)),
        "pythonVersion": sys.version.split()[0], "sdkSha256": digest(args.sdk), "launcher": launcher.name,
        "runtimeFiles": [{"path": str(path), "sha256": digest(path)} for path in
                         (Path(sys.executable).parent / "python313.dll", Path(sys.executable).parent / "python3.dll")],
        "files": inventory(args.output)}
    write_json(args.output / "artest-environment.json", receipt)
    print(args.output / "artest-environment.json")

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    build = commands.add_parser("sdk")
    build.add_argument("--protoc", type=Path, required=True)
    build.add_argument("--protocol", type=Path, required=True)
    build.add_argument("--output", type=Path, required=True)
    build.set_defaults(action=sdk)
    build = commands.add_parser("package")
    build.add_argument("--source", type=Path, required=True)
    build.add_argument("--entry-point", required=True)
    build.add_argument("--lock", type=Path, required=True)
    build.add_argument("--output", type=Path, required=True)
    build.set_defaults(action=package)
    build = commands.add_parser("prepare")
    build.add_argument("--package", type=Path, required=True)
    build.add_argument("--sdk", type=Path, required=True)
    build.add_argument("--output", type=Path, required=True)
    build.set_defaults(action=prepare)
    args = parser.parse_args()
    args.action(args)

if __name__ == "__main__": main()
