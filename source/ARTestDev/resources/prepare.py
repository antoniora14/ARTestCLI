"""Private ARTestDev preparation adapter. Not a public command or configuration."""
import argparse
import importlib.util
import json
from pathlib import Path
import re
import subprocess
import sys


def load_tool(path):
    spec = importlib.util.spec_from_file_location("_artestdev_project", path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def offline_lock(path):
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line and not line.startswith("#") and not re.fullmatch(
                r"[A-Za-z0-9_.-]+==[A-Za-z0-9_.+!-]+(?:\s+--hash=sha256:[0-9a-f]{64})+", line):
            raise ValueError("Offline lock requires pinned wheel names and SHA-256 hashes; URLs and pip options are not allowed")


def package_operation(arguments, sdk_root):
    tool = load_tool(sdk_root / "python/tools/project.py")
    package = tool._package_module()
    wheel = sdk_root / "python/wheels/artest_python-0.2.0-py3-none-any.whl"
    if package.offline_wheelhouse(wheel) is None:
        raise ValueError("Missing offline wheelhouse; repair SDK resources")
    if arguments[0] == "prepare":
        offline_lock(Path(arguments[arguments.index("--package") + 1]) / "requirements.lock")
    checked = package.checked

    def offline_checked(command):
        command = [str(item) for item in command]
        if "pip" in command and "install" in command:
            command += ["--no-cache-dir", "--no-index"]
        checked(command)

    package.checked = offline_checked
    sys.argv = [str(sdk_root / "python/tools/package.py"), *arguments]
    package.main()


def prepare(root, python, sdk_root, *, authorize_commit=None):
    tool = load_tool(sdk_root / "python/tools/project.py")
    wheel = sdk_root / "python/wheels/artest_python-0.2.0-py3-none-any.whl"
    project = tool._load_project(root, preparation=(python, wheel))
    tool._reject_reparse_ancestors(Path(root))
    if Path(root).resolve().is_relative_to(sdk_root.resolve()) or sdk_root.resolve().is_relative_to(Path(root).resolve()):
        raise tool.ProjectError("Project and preparation outputs must be outside the installed SDK")
    package = tool._package_module()
    if package.offline_wheelhouse(wheel) is None:
        raise tool.ProjectError("Offline preparation requires the installed, inventoried wheelhouse; repair SDK resources")
    # Reject pip directives and URL dependencies before invoking the existing tool.
    # The private offline path accepts only pinned, hash-locked wheel requirements.
    offline_lock(project.configuration.dependency_lock)

    def run_package(interpreter, arguments):
        # Inherit bounded host pipes; never buffer arbitrary metadata/pip output here.
        process = subprocess.run([str(interpreter), "-I", "-B", str(Path(__file__).resolve()),
                                  "--package-arguments", json.dumps(arguments)], stdin=subprocess.DEVNULL, check=False)
        if process.returncode:
            raise tool.ProjectError(f"package.py {arguments[0]} failed (exit {process.returncode}); see bounded process output")

    observed_probe = None

    def identity(value, probe):
        nonlocal observed_probe
        observed_probe = probe
        inputs = tool._preparation_inputs(value, probe)
        # This is private preparation identity, not an environment receipt format.
        inputs["tools"]["artestDevAdapterSha256"] = tool._sha256(Path(__file__))
        inputs["tools"]["offlineWheelhouse"] = tool._content_inventory(wheel.parent)
        return inputs

    publish = tool._publish_ready
    release = tool._release_preparation_lock
    committed = False
    warnings = []

    def publish_when_authorized(output_root, preparation_id):
        nonlocal committed
        if authorize_commit is not None:
            authorize_commit()
        if tool._canonical_digest(identity(project, observed_probe)) != preparation_id:
            raise tool.ProjectError("Preparation inputs changed before selection publication; previous selection preserved")
        publish(output_root, preparation_id)
        committed = True

    def release_lock(lock, token):
        try:
            release(lock, token)
        except Exception as error:
            if not committed:
                raise
            warnings.append(f"Selection published, but lock needs inspection: {error}")

    def preserve_in_place(attempt, incomplete, error):
        # ARTestDev never relocates an environment, including failed candidates.
        # Returning false retains project.py's work marker and blocks retries.
        try:
            tool._write_json_exclusive(incomplete / f"{attempt.name}.json", {
                "revision": str(attempt), "cause": str(error),
                "disposition": "failed in final location; not selected; inspect work marker before retrying",
            })
        except (OSError, tool.ProjectError):
            pass
        return False

    tool._publish_ready = publish_when_authorized
    tool._release_preparation_lock = release_lock
    tool._preserve_failed_attempt = preserve_in_place
    result = tool.prepare_project(project, package_runner=run_package, identity_builder=identity).as_dict()
    if warnings:
        result["diagnostic"] = "; ".join(warnings)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", type=Path)
    parser.add_argument("--python", type=Path)
    parser.add_argument("--package-arguments")
    args = parser.parse_args()
    if args.package_arguments is not None:
        package_operation(json.loads(args.package_arguments), Path(__file__).resolve().parents[2])
        return 0
    if args.project is None or args.python is None:
        parser.error("project and selected python are required")
    def authorize_commit():
        print("ARTESTDEV_COMMIT_READY", flush=True)
        if sys.stdin.readline() != "commit\n":
            raise RuntimeError("Preparation selection was not authorized; previous selection preserved")
    try:
        result = prepare(args.project, args.python, Path(__file__).resolve().parents[2], authorize_commit=authorize_commit)
        code = 0
    except Exception as error:
        result = {"success": False, "diagnostic": str(error)}
        code = 1
    print("\nARTESTDEV_PREPARATION=" + json.dumps(result, ensure_ascii=True), flush=True)
    return code


if __name__ == "__main__":
    raise SystemExit(main())
