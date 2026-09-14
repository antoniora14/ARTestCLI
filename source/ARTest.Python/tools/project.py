"""Create, validate, and inspect local prerequisites for an ARTest Python project."""

import argparse
import copy
from dataclasses import dataclass
from email.parser import BytesParser
from email.policy import default as email_policy
import json
import os
from pathlib import Path, PureWindowsPath
import re
import shutil
import stat
import struct
import subprocess
import tempfile
import threading
import zipfile


CONFIG_NAME = "artest-project.json"
LOCAL_CONFIG_NAME = "artest-project.local.json"
CONFIG_VERSION = 1
PYTHON_PROBE_TIMEOUT_SECONDS = 5.0
PYTHON_PROBE_TERMINATION_TIMEOUT_SECONDS = 1.0
PYTHON_PROBE_READER_TIMEOUT_SECONDS = 1.0
PYTHON_PROBE_OUTPUT_LIMIT = 16 * 1024
SUPPORTED_PYTHON = (3, 13)
SUPPORTED_PYTHON_SDK_NAME = "artest-python"
SUPPORTED_PYTHON_SDK_VERSION = "0.2.0"
SUPPORTED_PYTHON_REQUIREMENT = ">=3.13,<3.14"
SUPPORTED_WHEEL_TAG = "py3-none-any"
PE_MACHINE_AMD64 = 0x8664
PE32_PLUS_MAGIC = 0x20B
PE_EXECUTABLE_IMAGE = 0x0002
PE_DLL = 0x2000
GENERATED_PLAN = Path(".artest/stage2/local-plan.json")
ENTRY_POINT_PATTERN = re.compile(
    r"[A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*:[A-Za-z_][A-Za-z0-9_]*\Z"
)
STABLE_ID_PATTERN = re.compile(r"[a-z0-9]+(?:[.-][a-z0-9]+(?:-[a-z0-9]+)*)+\Z")


class ProjectError(ValueError):
    """A project cannot be created or its configuration is invalid."""


class InterpreterProbeError(RuntimeError):
    """The configured interpreter could not produce a usable bounded probe."""

    def __init__(self, code: str, cause: str):
        super().__init__(cause)
        self.code = code
        self.cause = cause


class PeError(ValueError):
    """A configured Windows binary does not contain readable PE metadata."""


@dataclass(frozen=True)
class ProjectConfiguration:
    root: Path
    source_directory: Path
    entry_point: str
    dependency_lock: Path
    plan: Path


@dataclass(frozen=True)
class LocalConfiguration:
    python: Path
    sdk_wheel: Path
    cli_executable: Path
    vendor_paths: dict[str, Path]
    vendor_dlls: frozenset[str]
    plan_bindings: tuple["PlanBinding", ...]


@dataclass(frozen=True)
class PlanBinding:
    vendor_path: str
    instrument_id: str
    config_field: str


@dataclass(frozen=True)
class Project:
    configuration: ProjectConfiguration
    local: LocalConfiguration | None


@dataclass(frozen=True)
class ProjectIdentity:
    extension_id: str
    driver_id: str
    command_id: str
    author: str


@dataclass(frozen=True)
class Diagnostic:
    code: str
    operation: str
    stage: str
    field: str
    path: str | None
    expected: str
    cause: str
    correction: str

    def as_dict(self) -> dict:
        return {
            "code": self.code,
            "operation": self.operation,
            "stage": self.stage,
            "field": self.field,
            "path": self.path,
            "expected": self.expected,
            "cause": self.cause,
            "correction": self.correction,
        }


@dataclass(frozen=True)
class PrerequisiteReport:
    diagnostics: tuple[Diagnostic, ...]
    python_probe: dict | None
    sdk_metadata: dict | None

    @property
    def success(self) -> bool:
        return not self.diagnostics

    def as_dict(self) -> dict:
        return {
            "operation": "check",
            "stage": "PY-DX-01 Stage 2 local prerequisites",
            "success": self.success,
            "diagnostics": [item.as_dict() for item in self.diagnostics],
            "observed": {
                "python": self.python_probe,
                "sdk": self.sdk_metadata,
            },
            "limitations": [
                "PE inspection checks file format and x64 machine type only.",
                "It does not prove transitive DLL availability, required exports, "
                "device compatibility, or successful runtime loading.",
                "CLI PE inspection does not prove product identity or execute a version check.",
                "No Engine, extension, SDK, vendor code, installer, or instrument was run.",
            ],
        }


def _is_reparse_point(path: Path) -> bool:
    try:
        metadata = os.lstat(path)
    except FileNotFoundError:
        return False
    except OSError as error:
        raise ProjectError(f"Cannot inspect path for reparse points: {path}: {error}") from error
    attributes = getattr(metadata, "st_file_attributes", 0)
    reparse_attribute = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0)
    return stat.S_ISLNK(metadata.st_mode) or bool(attributes & reparse_attribute)


def _reject_reparse_ancestors(path: Path) -> None:
    absolute = path.absolute()
    for candidate in (absolute, *absolute.parents):
        if _is_reparse_point(candidate):
            raise ProjectError(f"Reparse-point traversal is not allowed: {candidate}")


def _reject_reparse_tree(root: Path) -> None:
    _reject_reparse_ancestors(root)
    for current, directories, files in os.walk(root, topdown=True, followlinks=False):
        current_path = Path(current)
        for name in directories + files:
            candidate = current_path / name
            if _is_reparse_point(candidate):
                raise ProjectError(f"Project templates cannot contain reparse points: {candidate}")


def _read_json_object(path: Path) -> dict:
    def unique_object(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ProjectError(f"Duplicate property {key!r} in {path}")
            result[key] = value
        return result

    try:
        value = json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=unique_object)
    except FileNotFoundError as error:
        raise ProjectError(f"Configuration file does not exist: {path}") from error
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise ProjectError(f"Cannot read JSON configuration {path}: {error}") from error
    if not isinstance(value, dict):
        raise ProjectError(f"Configuration must be a JSON object: {path}")
    return value


def _validate_keys(value: dict, required: set[str], optional: set[str], path: Path) -> None:
    missing = sorted(required - value.keys())
    unknown = sorted(value.keys() - required - optional)
    if missing:
        raise ProjectError(f"Missing properties in {path}: {', '.join(missing)}")
    if unknown:
        raise ProjectError(f"Unknown properties in {path}: {', '.join(unknown)}")


def _validate_version(value: object, path: Path) -> None:
    if type(value) is not int:
        raise ProjectError(f"schemaVersion in {path} must be an integer")
    if value != CONFIG_VERSION:
        raise ProjectError(
            f"Unsupported schemaVersion {value!r} in {path}; expected {CONFIG_VERSION}"
        )


def _string(value: object, property_name: str, path: Path) -> str:
    if not isinstance(value, str) or not value.strip():
        raise ProjectError(f"{property_name} in {path} must be a nonempty string")
    return value


def _has_nonportable_anchor(path_text: str) -> bool:
    windows_path = PureWindowsPath(path_text)
    return (
        Path(path_text).is_absolute()
        or bool(windows_path.drive)
        or bool(windows_path.root)
    )


def _contained_portable_path(
    root: Path, value: object, property_name: str, config_path: Path
) -> Path:
    path_text = _string(value, property_name, config_path)
    if _has_nonportable_anchor(path_text):
        raise ProjectError(f"{property_name} in {config_path} must be relative to the project")
    try:
        candidate = (root / path_text).resolve(strict=False)
    except (OSError, RuntimeError) as error:
        raise ProjectError(f"Cannot resolve {property_name} in {config_path}: {error}") from error
    try:
        candidate.relative_to(root)
    except ValueError as error:
        raise ProjectError(f"{property_name} in {config_path} escapes the project: {path_text}") from error
    try:
        return (root / path_text).resolve(strict=True)
    except (OSError, RuntimeError) as error:
        raise ProjectError(f"Cannot resolve {property_name} in {config_path}: {error}") from error


def _local_path(root: Path, value: object, property_name: str, config_path: Path) -> Path:
    path_text = _string(value, property_name, config_path)
    path = Path(path_text)
    if not path.is_absolute():
        path = root / path
    try:
        return path.resolve(strict=False)
    except (OSError, RuntimeError) as error:
        raise ProjectError(f"Cannot resolve {property_name} in {config_path}: {error}") from error


def _local_string_list(
    value: object, property_name: str, config_path: Path
) -> tuple[str, ...]:
    if not isinstance(value, list):
        raise ProjectError(f"{property_name} in {config_path} must be a JSON array")
    result = []
    for index, item in enumerate(value):
        result.append(_string(item, f"{property_name}[{index}]", config_path))
    if len(set(result)) != len(result):
        raise ProjectError(f"{property_name} in {config_path} contains duplicate names")
    return tuple(result)


def _local_plan_bindings(
    value: object, vendor_paths: dict[str, Path], config_path: Path
) -> tuple[PlanBinding, ...]:
    if not isinstance(value, list):
        raise ProjectError(f"planBindings in {config_path} must be a JSON array")
    bindings = []
    targets = set()
    for index, item in enumerate(value):
        if not isinstance(item, dict):
            raise ProjectError(f"planBindings[{index}] in {config_path} must be a JSON object")
        _validate_keys(
            item,
            {"vendorPath", "instrumentId", "configField"},
            set(),
            config_path,
        )
        vendor_path = _string(
            item["vendorPath"], f"planBindings[{index}].vendorPath", config_path
        )
        instrument_id = _string(
            item["instrumentId"], f"planBindings[{index}].instrumentId", config_path
        )
        config_field = _string(
            item["configField"], f"planBindings[{index}].configField", config_path
        )
        if vendor_path not in vendor_paths:
            raise ProjectError(
                f"planBindings[{index}].vendorPath in {config_path} names unknown "
                f"vendorPaths entry {vendor_path!r}"
            )
        target = (instrument_id, config_field)
        if target in targets:
            raise ProjectError(
                f"planBindings in {config_path} contains duplicate target "
                f"{instrument_id}.config.{config_field}"
            )
        targets.add(target)
        bindings.append(PlanBinding(vendor_path, instrument_id, config_field))
    return tuple(bindings)


def load_project(project_root: Path | str) -> Project:
    """Read and structurally validate portable and optional local configuration."""
    supplied_root = Path(project_root)
    try:
        root = supplied_root.resolve(strict=True)
    except (OSError, RuntimeError) as error:
        raise ProjectError(f"Project root cannot be resolved: {supplied_root}: {error}") from error
    if not root.is_dir():
        raise ProjectError(f"Project root is not a directory: {root}")

    config_path = root / CONFIG_NAME
    if _is_reparse_point(config_path):
        raise ProjectError(f"Portable configuration cannot be a reparse point: {config_path}")
    portable = _read_json_object(config_path)
    _validate_keys(
        portable,
        {"schemaVersion", "sourceDirectory", "entryPoint", "dependencyLock", "plan"},
        set(),
        config_path,
    )
    _validate_version(portable["schemaVersion"], config_path)
    entry_point = _string(portable["entryPoint"], "entryPoint", config_path)
    if not ENTRY_POINT_PATTERN.fullmatch(entry_point):
        raise ProjectError(
            f"entryPoint in {config_path} must use dotted.module:callable syntax"
        )

    source_directory = _contained_portable_path(
        root, portable["sourceDirectory"], "sourceDirectory", config_path
    )
    dependency_lock = _contained_portable_path(
        root, portable["dependencyLock"], "dependencyLock", config_path
    )
    plan = _contained_portable_path(root, portable["plan"], "plan", config_path)
    if not source_directory.is_dir():
        raise ProjectError(f"sourceDirectory is not a directory: {source_directory}")
    for property_name, referenced_path in (("dependencyLock", dependency_lock), ("plan", plan)):
        if not referenced_path.is_file():
            raise ProjectError(f"{property_name} is not a file: {referenced_path}")

    configuration = ProjectConfiguration(
        root=root,
        source_directory=source_directory,
        entry_point=entry_point,
        dependency_lock=dependency_lock,
        plan=plan,
    )

    local_path = root / LOCAL_CONFIG_NAME
    if _is_reparse_point(local_path):
        raise ProjectError(f"Local configuration cannot be a reparse point: {local_path}")
    if not local_path.exists():
        return Project(configuration=configuration, local=None)
    local_value = _read_json_object(local_path)
    _validate_keys(
        local_value,
        {"schemaVersion", "python", "sdkWheel", "cliExecutable"},
        {"vendorPaths", "vendorDlls", "planBindings"},
        local_path,
    )
    _validate_version(local_value["schemaVersion"], local_path)
    vendor_value = local_value.get("vendorPaths", {})
    if not isinstance(vendor_value, dict):
        raise ProjectError(f"vendorPaths in {local_path} must be a JSON object")
    vendor_paths = {}
    for name, path_value in vendor_value.items():
        if not isinstance(name, str) or not name.strip():
            raise ProjectError(f"vendorPaths keys in {local_path} must be nonempty strings")
        vendor_paths[name] = _local_path(root, path_value, f"vendorPaths.{name}", local_path)
    vendor_dlls = frozenset(
        _local_string_list(local_value.get("vendorDlls", []), "vendorDlls", local_path)
    )
    unknown_dlls = sorted(vendor_dlls - vendor_paths.keys())
    if unknown_dlls:
        raise ProjectError(
            f"vendorDlls in {local_path} names unknown vendorPaths entries: "
            + ", ".join(unknown_dlls)
        )
    plan_bindings = _local_plan_bindings(
        local_value.get("planBindings", []), vendor_paths, local_path
    )
    local = LocalConfiguration(
        python=_local_path(root, local_value["python"], "python", local_path),
        sdk_wheel=_local_path(root, local_value["sdkWheel"], "sdkWheel", local_path),
        cli_executable=_local_path(root, local_value["cliExecutable"], "cliExecutable", local_path),
        vendor_paths=vendor_paths,
        vendor_dlls=vendor_dlls,
        plan_bindings=plan_bindings,
    )
    return Project(configuration=configuration, local=local)


def _diagnostic(
    code: str,
    stage: str,
    field: str,
    path: Path | None,
    expected: str,
    cause: str,
    correction: str,
) -> Diagnostic:
    return Diagnostic(
        code=code,
        operation="check",
        stage=stage,
        field=field,
        path=str(path) if path is not None else None,
        expected=expected,
        cause=cause,
        correction=correction,
    )


PYTHON_PROBE = (
    "import json,platform,struct,sys;"
    "g=getattr(sys,'_is_gil_enabled',None);"
    "print(json.dumps({'implementation':sys.implementation.name,"
    "'version':[sys.version_info.major,sys.version_info.minor,sys.version_info.micro],"
    "'platform':sys.platform,'machine':platform.machine(),"
    "'pointerBits':struct.calcsize('P')*8,'gilEnabled':g() if g else None}))"
)


def _bounded_pipe_reader(stream, result: dict) -> None:
    data = bytearray()
    try:
        while len(data) <= PYTHON_PROBE_OUTPUT_LIMIT:
            block = stream.read(min(4096, PYTHON_PROBE_OUTPUT_LIMIT + 1 - len(data)))
            if not block:
                break
            data.extend(block)
    finally:
        stream.close()
    result["overflow"] = len(data) > PYTHON_PROBE_OUTPUT_LIMIT
    result["data"] = bytes(data[:PYTHON_PROBE_OUTPUT_LIMIT])


def _join_probe_readers(readers) -> bool:
    for reader in readers:
        reader.join(timeout=PYTHON_PROBE_READER_TIMEOUT_SECONDS)
    return not any(reader.is_alive() for reader in readers)


def _run_python_probe(python: Path) -> dict:
    try:
        process = subprocess.Popen(
            [str(python), "-I", "-S", "-c", PYTHON_PROBE],
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            shell=False,
        )
    except OSError as error:
        raise InterpreterProbeError("PYTHON_NOT_EXECUTABLE", str(error)) from error

    stdout_result = {}
    stderr_result = {}
    readers = (
        threading.Thread(
            target=_bounded_pipe_reader, args=(process.stdout, stdout_result), daemon=True
        ),
        threading.Thread(
            target=_bounded_pipe_reader, args=(process.stderr, stderr_result), daemon=True
        ),
    )
    for reader in readers:
        reader.start()
    try:
        return_code = process.wait(timeout=PYTHON_PROBE_TIMEOUT_SECONDS)
    except subprocess.TimeoutExpired as error:
        kill_error = None
        try:
            process.kill()
        except OSError as termination_error:
            kill_error = termination_error
        try:
            process.wait(timeout=PYTHON_PROBE_TERMINATION_TIMEOUT_SECONDS)
        except (OSError, subprocess.TimeoutExpired) as termination_error:
            _join_probe_readers(readers)
            cause = (
                f"probe exceeded {PYTHON_PROBE_TIMEOUT_SECONDS:g} seconds; "
                f"termination was not confirmed within "
                f"{PYTHON_PROBE_TERMINATION_TIMEOUT_SECONDS:g} second"
            )
            if kill_error is not None:
                cause += f"; kill failed: {kill_error}"
            cause += f"; bounded termination wait failed: {termination_error}"
            raise InterpreterProbeError(
                "PYTHON_PROBE_TERMINATION_UNCONFIRMED", cause
            ) from error
        readers_closed = _join_probe_readers(readers)
        cause = f"probe exceeded {PYTHON_PROBE_TIMEOUT_SECONDS:g} seconds"
        if kill_error is not None:
            cause += f"; kill failed ({kill_error}), but process exit was subsequently observed"
        if not readers_closed:
            cause += "; output reader closure was not confirmed within the bounded cleanup"
        raise InterpreterProbeError(
            "PYTHON_PROBE_TIMEOUT",
            cause,
        ) from error
    except OSError as error:
        try:
            process.kill()
        except OSError:
            pass
        _join_probe_readers(readers)
        raise InterpreterProbeError("PYTHON_PROBE_FAILED", str(error)) from error
    if not _join_probe_readers(readers):
        raise InterpreterProbeError(
            "PYTHON_PROBE_FAILED", "probe output streams did not close"
        )
    if stdout_result.get("overflow") or stderr_result.get("overflow"):
        raise InterpreterProbeError(
            "PYTHON_PROBE_INVALID_OUTPUT",
            f"probe output exceeded {PYTHON_PROBE_OUTPUT_LIMIT} bytes",
        )
    stdout = stdout_result.get("data", b"")
    stderr = stderr_result.get("data", b"")
    if return_code != 0:
        detail = stderr.decode("utf-8", errors="replace").strip()
        raise InterpreterProbeError(
            "PYTHON_PROBE_FAILED",
            f"probe exited with code {return_code}" + (f": {detail}" if detail else ""),
        )
    try:
        value = json.loads(stdout.decode("utf-8"))
    except (UnicodeError, json.JSONDecodeError) as error:
        raise InterpreterProbeError(
            "PYTHON_PROBE_INVALID_OUTPUT", f"probe did not return one UTF-8 JSON value: {error}"
        ) from error
    required = {
        "implementation",
        "version",
        "platform",
        "machine",
        "pointerBits",
        "gilEnabled",
    }
    if not isinstance(value, dict) or set(value) != required:
        raise InterpreterProbeError(
            "PYTHON_PROBE_INVALID_OUTPUT",
            "probe JSON did not contain the exact expected identity fields",
        )
    if (
        not isinstance(value["implementation"], str)
        or not isinstance(value["version"], list)
        or len(value["version"]) != 3
        or any(type(item) is not int for item in value["version"])
        or not isinstance(value["platform"], str)
        or not isinstance(value["machine"], str)
        or type(value["pointerBits"]) is not int
        or (
            value["gilEnabled"] is not None
            and type(value["gilEnabled"]) is not bool
        )
    ):
        raise InterpreterProbeError(
            "PYTHON_PROBE_INVALID_OUTPUT", "probe JSON fields have invalid types"
        )
    return value


def _python_diagnostics(
    local: LocalConfiguration, probe_runner=None
) -> tuple[list[Diagnostic], dict | None]:
    path = local.python
    expected = "an executable standard GIL-enabled CPython 3.13 Windows x64 interpreter"
    correction = "Set local field python to the supported python.exe installation."
    if not path.exists():
        return [
            _diagnostic(
                "PYTHON_MISSING", "interpreter", "python", path, expected,
                "the configured path does not exist", correction,
            )
        ], None
    if not path.is_file():
        return [
            _diagnostic(
                "PYTHON_NOT_EXECUTABLE", "interpreter", "python", path, expected,
                "the configured path is not a regular file", correction,
            )
        ], None
    runner = probe_runner or _run_python_probe
    try:
        observed = runner(path)
    except InterpreterProbeError as error:
        specific_correction = correction
        if error.code == "PYTHON_PROBE_TIMEOUT":
            specific_correction = (
                "Select a responsive supported interpreter and retry; inspect local "
                "endpoint-security or process-launch policy if it remains blocked."
            )
        elif error.code == "PYTHON_PROBE_TERMINATION_UNCONFIRMED":
            specific_correction = (
                "The configured probe process may still be running. Inspect that exact "
                "process and local process-control policy before retrying with a responsive "
                "supported interpreter."
            )
        return [
            _diagnostic(
                error.code, "interpreter", "python", path, expected,
                error.cause, specific_correction,
            )
        ], None
    checks = (
        (
            "PYTHON_IMPLEMENTATION_INCOMPATIBLE", "python.implementation", "cpython",
            observed.get("implementation"), "Install and select standard CPython 3.13 x64.",
        ),
        (
            "PYTHON_VERSION_INCOMPATIBLE", "python.version", "3.13.x",
            ".".join(str(item) for item in observed.get("version", [])),
            "Install and select a CPython 3.13.x interpreter; 3.12 scaffold "
            "testing is not runtime support.",
        ),
        (
            "PYTHON_PLATFORM_INCOMPATIBLE", "python.platform", "win32",
            observed.get("platform"), "Select a supported Windows CPython installation.",
        ),
        (
            "PYTHON_ARCHITECTURE_INCOMPATIBLE", "python.architecture",
            "Windows x64 (64-bit, AMD64)",
            f"{observed.get('pointerBits')}-bit {observed.get('machine')}",
            "Install and select the Windows x64 CPython build.",
        ),
        (
            "PYTHON_GIL_INCOMPATIBLE", "python.gilEnabled", "true",
            observed.get("gilEnabled"),
            "Select the standard GIL-enabled build, not a free-threaded build.",
        ),
    )
    compatible = (
        observed["implementation"] == "cpython",
        observed["version"][:2] == list(SUPPORTED_PYTHON),
        observed["platform"] == "win32",
        observed["pointerBits"] == 64
        and observed["machine"].lower() in {"amd64", "x86_64"},
        observed["gilEnabled"] is True,
    )
    diagnostics = []
    for matches, (code, field, required, actual, fix) in zip(compatible, checks):
        if not matches:
            diagnostics.append(
                _diagnostic(
                    code, "interpreter", field, path, required,
                    f"probe reported {actual!r}", fix,
                )
            )
    return diagnostics, observed


def _single_metadata_value(message, field: str) -> str:
    values = message.get_all(field, [])
    if len(values) != 1 or not values[0].strip():
        raise ValueError(f"wheel metadata must contain exactly one nonempty {field} field")
    return values[0].strip()


def _inspect_sdk_wheel(path: Path) -> dict:
    try:
        with zipfile.ZipFile(path) as archive:
            entries = archive.infolist()
            names = [entry.filename for entry in entries]
            if len(names) != len(set(names)):
                raise ValueError("wheel contains duplicate archive entries")
            metadata_names = [
                name for name in names if name.endswith(".dist-info/METADATA")
            ]
            if len(metadata_names) != 1:
                raise ValueError("wheel must contain exactly one .dist-info/METADATA file")
            metadata_name = metadata_names[0]
            prefix = metadata_name[: -len("METADATA")]
            required_names = {prefix + "WHEEL", prefix + "RECORD"}
            if not required_names.issubset(names):
                raise ValueError("wheel is missing WHEEL or RECORD metadata")
            metadata_entry = archive.getinfo(metadata_name)
            wheel_entry = archive.getinfo(prefix + "WHEEL")
            if metadata_entry.file_size > 64 * 1024 or wheel_entry.file_size > 64 * 1024:
                raise ValueError("wheel metadata exceeds the 64 KiB inspection limit")
            damaged = archive.testzip()
            if damaged is not None:
                raise ValueError(f"wheel entry failed its CRC check: {damaged}")
            metadata = BytesParser(policy=email_policy).parsebytes(archive.read(metadata_entry))
            wheel = BytesParser(policy=email_policy).parsebytes(archive.read(wheel_entry))
    except (OSError, RuntimeError, zipfile.BadZipFile, KeyError) as error:
        raise ValueError(f"cannot read wheel archive: {error}") from error
    return {
        "name": _single_metadata_value(metadata, "Name"),
        "version": _single_metadata_value(metadata, "Version"),
        "requiresPython": _single_metadata_value(metadata, "Requires-Python"),
        "requiresDist": metadata.get_all("Requires-Dist", []),
        "tags": wheel.get_all("Tag", []),
        "rootIsPurelib": _single_metadata_value(wheel, "Root-Is-Purelib"),
    }


def _sdk_diagnostics(local: LocalConfiguration) -> tuple[list[Diagnostic], dict | None]:
    path = local.sdk_wheel
    expected = (
        "a readable artest-python 0.2.0 wheel for CPython 3.13 using private wire 0.2"
    )
    correction = "Set sdkWheel to the unmodified ARTest Python SDK 0.2.0 wheel."
    if not path.exists():
        return [
            _diagnostic(
                "SDK_MISSING", "SDK", "sdkWheel", path, expected,
                "the configured wheel does not exist", correction,
            )
        ], None
    if not path.is_file():
        return [
            _diagnostic(
                "SDK_INVALID", "SDK", "sdkWheel", path, expected,
                "the configured wheel is not a regular file", correction,
            )
        ], None
    try:
        observed = _inspect_sdk_wheel(path)
    except ValueError as error:
        return [
            _diagnostic(
                "SDK_INVALID", "SDK", "sdkWheel", path, expected, str(error), correction,
            )
        ], None

    diagnostics = []
    normalized_name = re.sub(r"[-_.]+", "-", observed["name"].lower())
    compatibility = (
        (
            "sdkWheel.metadata.Name", SUPPORTED_PYTHON_SDK_NAME,
            normalized_name == SUPPORTED_PYTHON_SDK_NAME,
        ),
        (
            "sdkWheel.metadata.Version", SUPPORTED_PYTHON_SDK_VERSION,
            observed["version"] == SUPPORTED_PYTHON_SDK_VERSION,
        ),
        (
            "sdkWheel.metadata.Requires-Python", SUPPORTED_PYTHON_REQUIREMENT,
            observed["requiresPython"] == SUPPORTED_PYTHON_REQUIREMENT,
        ),
        ("sdkWheel.metadata.Tag", SUPPORTED_WHEEL_TAG, SUPPORTED_WHEEL_TAG in observed["tags"]),
        ("sdkWheel.metadata.Root-Is-Purelib", "true", observed["rootIsPurelib"].lower() == "true"),
    )
    for field, required, matches in compatibility:
        if not matches:
            actual_key = {
                "sdkWheel.metadata.Name": "name",
                "sdkWheel.metadata.Version": "version",
                "sdkWheel.metadata.Requires-Python": "requiresPython",
                "sdkWheel.metadata.Tag": "tags",
                "sdkWheel.metadata.Root-Is-Purelib": "rootIsPurelib",
            }.get(field, "rootIsPurelib")
            diagnostics.append(
                _diagnostic(
                    "SDK_INCOMPATIBLE", "SDK", field, path, required,
                    f"wheel metadata reported {observed[actual_key]!r}", correction,
                )
            )
    normalized_requirements = {
        re.sub(r"\s+", "", item).lower() for item in observed["requiresDist"]
    }
    required_dependencies = {
        "protobuf==6.33.4",
        "pywin32==311;sys_platform=='win32'",
    }
    if not required_dependencies.issubset(normalized_requirements):
        diagnostics.append(
            _diagnostic(
                "SDK_INCOMPATIBLE", "SDK", "sdkWheel.metadata.Requires-Dist", path,
                "protobuf==6.33.4 and pywin32==311 for win32",
                f"wheel metadata reported {observed['requiresDist']!r}", correction,
            )
        )
    return diagnostics, observed


def _read_pe_headers(path: Path) -> tuple[int, int]:
    try:
        size = path.stat().st_size
        with path.open("rb") as stream:
            dos_header = stream.read(64)
            if len(dos_header) != 64 or dos_header[:2] != b"MZ":
                raise PeError("missing or truncated DOS MZ header")
            pe_offset = struct.unpack_from("<I", dos_header, 0x3C)[0]
            if pe_offset < 64 or pe_offset > size - 26:
                raise PeError("PE header offset is outside the file")
            stream.seek(pe_offset)
            pe_header = stream.read(24)
            if len(pe_header) != 24 or pe_header[:4] != b"PE\0\0":
                raise PeError("missing or truncated PE signature")
            machine, sections = struct.unpack_from("<HH", pe_header, 4)
            optional_size, characteristics = struct.unpack_from("<HH", pe_header, 20)
            if not sections or optional_size < 2 or pe_offset + 24 + optional_size > size:
                raise PeError("truncated or invalid PE headers")
            optional_magic = struct.unpack("<H", stream.read(2))[0]
            if optional_magic != PE32_PLUS_MAGIC:
                raise PeError(f"expected PE32+ optional header, found 0x{optional_magic:04X}")
            if not characteristics & PE_EXECUTABLE_IMAGE:
                raise PeError("PE image is not marked executable")
            return machine, characteristics
    except (OSError, struct.error) as error:
        raise PeError(f"cannot read PE file: {error}") from error


def _pe_diagnostics(
    *, path: Path, stage: str, field: str, label: str, missing_code: str,
    invalid_code: str, architecture_code: str, correction: str, expect_dll: bool
) -> list[Diagnostic]:
    expected = f"an existing readable Windows x64 PE {label}"
    if not path.exists():
        return [
            _diagnostic(
                missing_code, stage, field, path, expected,
                "the configured path does not exist", correction,
            )
        ]
    if not path.is_file():
        return [
            _diagnostic(
                invalid_code, stage, field, path, expected,
                "the configured path is not a regular file", correction,
            )
        ]
    try:
        machine, characteristics = _read_pe_headers(path)
    except PeError as error:
        return [
            _diagnostic(
                invalid_code, stage, field, path, expected, str(error), correction,
            )
        ]
    is_dll = bool(characteristics & PE_DLL)
    if is_dll != expect_dll:
        expected_kind = "DLL" if expect_dll else "executable (not DLL)"
        return [
            _diagnostic(
                invalid_code, stage, field, path, f"a PE image marked as {expected_kind}",
                f"PE characteristics identify {'a DLL' if is_dll else 'a non-DLL image'}",
                correction,
            )
        ]
    if machine != PE_MACHINE_AMD64:
        return [
            _diagnostic(
                architecture_code, stage, field, path, "PE machine AMD64 (0x8664)",
                f"PE machine is 0x{machine:04X}", correction,
            )
        ]
    return []


def _cli_diagnostics(local: LocalConfiguration) -> list[Diagnostic]:
    return _pe_diagnostics(
        path=local.cli_executable,
        stage="CLI",
        field="cliExecutable",
        label="executable",
        missing_code="CLI_MISSING",
        invalid_code="CLI_INVALID",
        architecture_code="CLI_ARCHITECTURE_INCOMPATIBLE",
        correction="Set cliExecutable to the built Windows x64 ARTestCLI.exe.",
        expect_dll=False,
    )


def _vendor_diagnostics(local: LocalConfiguration) -> list[Diagnostic]:
    diagnostics = []
    for name, path in sorted(local.vendor_paths.items()):
        field = f"vendorPaths.{name}"
        if name in local.vendor_dlls:
            diagnostics.extend(
                _pe_diagnostics(
                    path=path,
                    stage="local dependency",
                    field=field,
                    label="vendor DLL",
                    missing_code="VENDOR_DLL_MISSING",
                    invalid_code="VENDOR_DLL_INVALID_PE",
                    architecture_code="VENDOR_DLL_ARCHITECTURE_INCOMPATIBLE",
                    correction=(
                        f"Install or select the vendor's Windows x64 DLL and update {field}."
                    ),
                    expect_dll=True,
                )
            )
        elif not path.exists():
            diagnostics.append(
                _diagnostic(
                    "LOCAL_DEPENDENCY_MISSING", "local dependency", field, path,
                    "the declared local vendor file or directory to exist",
                    "the configured path does not exist",
                    f"Install the dependency or update {field} to its local path.",
                )
            )
    return diagnostics


def _plan_with_bindings(project: Project) -> tuple[dict | None, list[Diagnostic]]:
    local = project.local
    if local is None or not local.plan_bindings:
        return None, []
    try:
        plan = _read_json_object(project.configuration.plan)
    except ProjectError as error:
        return None, [
            _diagnostic(
                "PLAN_BINDING_INVALID", "local plan binding", "plan", project.configuration.plan,
                "a readable plan object containing every configured binding target",
                str(error), "Correct the portable plan or remove the invalid local binding.",
            )
        ]
    instruments = plan.get("instruments")
    if not isinstance(instruments, list):
        return None, [
            _diagnostic(
                "PLAN_BINDING_INVALID", "local plan binding", "plan.instruments",
                project.configuration.plan, "an instruments array",
                "the portable plan has no instruments array",
                "Correct the portable plan before materializing local bindings.",
            )
        ]
    generated = copy.deepcopy(plan)
    diagnostics = []
    for index, binding in enumerate(local.plan_bindings):
        matches = [
            item for item in generated["instruments"]
            if isinstance(item, dict) and item.get("id") == binding.instrument_id
        ]
        field = f"planBindings[{index}]"
        if len(matches) != 1:
            diagnostics.append(
                _diagnostic(
                    "PLAN_BINDING_INVALID", "local plan binding", field,
                    project.configuration.plan,
                    f"exactly one instrument with id {binding.instrument_id!r}",
                    f"found {len(matches)} matching instruments",
                    "Correct instrumentId in the local binding or the portable plan.",
                )
            )
            continue
        config = matches[0].get("config")
        if not isinstance(config, dict) or binding.config_field not in config:
            diagnostics.append(
                _diagnostic(
                    "PLAN_BINDING_INVALID", "local plan binding", field,
                    project.configuration.plan,
                    f"existing field instruments[{binding.instrument_id}].config."
                    f"{binding.config_field}",
                    "the target configuration field does not exist",
                    "Add the driver configuration field to the portable plan or "
                    "correct configField.",
                )
            )
            continue
        if config[binding.config_field] is not None and not isinstance(
            config[binding.config_field], str
        ):
            diagnostics.append(
                _diagnostic(
                    "PLAN_BINDING_INVALID", "local plan binding", field,
                    project.configuration.plan, "an existing string or null configuration field",
                    f"the target value has type {type(config[binding.config_field]).__name__}",
                    "Use a string/null path field or correct configField.",
                )
            )
            continue
        config[binding.config_field] = str(local.vendor_paths[binding.vendor_path])
    return generated if not diagnostics else None, diagnostics


def check_prerequisites(project: Project, probe_runner=None) -> PrerequisiteReport:
    """Inspect local prerequisites without preparing, installing, importing, or running Engine."""
    if project.local is None:
        diagnostic = _diagnostic(
            "LOCAL_CONFIGURATION_MISSING", "local configuration", LOCAL_CONFIG_NAME,
            project.configuration.root / LOCAL_CONFIG_NAME,
            "an explicit machine-local configuration for prerequisite checks",
            "the local configuration file is absent",
            f"Copy artest-project.local.example.json to {LOCAL_CONFIG_NAME} and set local paths.",
        )
        return PrerequisiteReport((diagnostic,), None, None)
    python_diagnostics, python_probe = _python_diagnostics(project.local, probe_runner)
    sdk_diagnostics, sdk_metadata = _sdk_diagnostics(project.local)
    diagnostics = python_diagnostics + sdk_diagnostics + _cli_diagnostics(project.local)
    diagnostics.extend(_vendor_diagnostics(project.local))
    _, binding_diagnostics = _plan_with_bindings(project)
    diagnostics.extend(binding_diagnostics)
    return PrerequisiteReport(tuple(diagnostics), python_probe, sdk_metadata)


def materialize_local_plan(project: Project) -> Path:
    """Create one generated plan copy with explicit machine-local path bindings."""
    if project.local is None:
        raise ProjectError(f"Local plan generation requires {LOCAL_CONFIG_NAME}")
    if not project.local.plan_bindings:
        raise ProjectError("Local plan generation requires at least one planBindings entry")
    vendor_diagnostics = _vendor_diagnostics(project.local)
    plan, binding_diagnostics = _plan_with_bindings(project)
    failures = vendor_diagnostics + binding_diagnostics
    if failures:
        first = failures[0]
        raise ProjectError(
            f"{first.stage} {first.field} failed: expected {first.expected}; "
            f"cause: {first.cause}; correction: {first.correction}"
        )
    output = project.configuration.root / GENERATED_PLAN
    if _is_reparse_point(output) or output.exists():
        raise ProjectError(
            f"Generated plan output already exists and will not be overwritten: {output}"
        )
    existing_parent = output.parent
    while not existing_parent.exists():
        existing_parent = existing_parent.parent
    _reject_reparse_ancestors(existing_parent)
    try:
        output.parent.mkdir(parents=True, exist_ok=True)
    except OSError as error:
        raise ProjectError(
            f"Cannot create generated-plan directory {output.parent}: {error}"
        ) from error
    _reject_reparse_ancestors(output.parent)
    with tempfile.TemporaryDirectory(prefix=".local-plan-", dir=output.parent) as scratch_text:
        candidate = Path(scratch_text) / output.name
        candidate.write_text(
            json.dumps(plan, indent=2, ensure_ascii=True) + "\n", encoding="utf-8"
        )
        try:
            candidate.rename(output)
        except OSError as error:
            raise ProjectError(
                f"Cannot publish generated local plan at {output}: {error}"
            ) from error
    return output


def _destination_is_available(destination: Path) -> bool:
    if _is_reparse_point(destination):
        raise ProjectError(f"Destination cannot be a reparse point: {destination}")
    try:
        entries = list(destination.iterdir())
    except FileNotFoundError:
        return False
    except NotADirectoryError as error:
        raise ProjectError(f"Destination exists and is not a directory: {destination}") from error
    except OSError as error:
        raise ProjectError(f"Cannot inspect destination {destination}: {error}") from error
    if entries:
        raise ProjectError(f"Destination is not empty: {destination}")
    return True


def _validated_identity(
    extension_id: str, driver_id: str, command_id: str, author: str
) -> ProjectIdentity:
    ids = {
        "extensionId": extension_id,
        "driverId": driver_id,
        "commandId": command_id,
    }
    for name, value in ids.items():
        if not isinstance(value, str) or not STABLE_ID_PATTERN.fullmatch(value):
            raise ProjectError(
                f"{name} must be a lower-case stable identifier such as com.example.component"
            )
    if len(set(ids.values())) != len(ids):
        raise ProjectError("Extension, driver and command IDs must be distinct")
    if not isinstance(author, str) or not author.strip():
        raise ProjectError("author must be a nonempty string")
    return ProjectIdentity(extension_id, driver_id, command_id, author)


def _render_identity(root: Path, identity: ProjectIdentity) -> None:
    derived = identity.extension_id
    replacements = {
        "__ARTEST_EXTENSION_ID__": identity.extension_id,
        "__ARTEST_DRIVER_ID__": identity.driver_id,
        "__ARTEST_COMMAND_ID__": identity.command_id,
        "__ARTEST_AUTHOR__": identity.author,
        "__ARTEST_CONTRACT_ID__": derived + ".contract.simulated-source.v1",
        "__ARTEST_READ_SCHEMA_ID__": derived + ".schema.simulated-source.read.v1",
        "__ARTEST_MEASUREMENT_SCHEMA_ID__": derived + ".schema.measurement.value.v1",
    }
    encoded_markers = {json.dumps(marker): value for marker, value in replacements.items()}
    marker_pattern = re.compile("|".join(re.escape(marker) for marker in encoded_markers))
    for relative_path in (Path("src/extension.py"), Path("plan/measurement.json")):
        path = root / relative_path
        text = path.read_text(encoding="utf-8")
        text = marker_pattern.sub(
            lambda match: json.dumps(encoded_markers[match.group(0)], ensure_ascii=True), text
        )
        if any(encoded_marker in text for encoded_marker in encoded_markers):
            raise ProjectError(f"Unresolved identity marker in bundled template: {relative_path}")
        path.write_text(text, encoding="utf-8")


def create_project(
    destination: Path | str,
    *,
    extension_id: str,
    driver_id: str,
    command_id: str,
    author: str,
) -> Path:
    """Atomically copy the bundled minimal template into an unused destination."""
    identity = _validated_identity(extension_id, driver_id, command_id, author)
    destination = Path(destination).absolute()
    parent = destination.parent
    if not parent.exists() or not parent.is_dir():
        raise ProjectError(f"Destination parent must be an existing directory: {parent}")
    _reject_reparse_ancestors(parent)
    _destination_is_available(destination)

    template = Path(__file__).resolve().parents[1] / "templates" / "minimal"
    if not template.is_dir():
        raise ProjectError(f"Bundled minimal template is missing: {template}")
    _reject_reparse_tree(template)

    with tempfile.TemporaryDirectory(prefix=".artest-create-", dir=parent) as scratch_text:
        scratch = Path(scratch_text)
        shutil.copytree(template, scratch, dirs_exist_ok=True, copy_function=shutil.copy2)
        _render_identity(scratch, identity)
        load_project(scratch)

        destination_was_empty = _destination_is_available(destination)
        removed_empty_destination = False
        try:
            if destination_was_empty:
                destination.rmdir()
                removed_empty_destination = True
            scratch.rename(destination)
        except OSError as error:
            if removed_empty_destination and not destination.exists():
                destination.mkdir()
            raise ProjectError(f"Cannot publish project at {destination}: {error}") from error
    return destination.resolve(strict=True)


def _print_validation(project: Project) -> None:
    configuration = project.configuration
    output = {
        "projectRoot": str(configuration.root),
        "sourceDirectory": str(configuration.source_directory),
        "entryPoint": configuration.entry_point,
        "dependencyLock": str(configuration.dependency_lock),
        "plan": str(configuration.plan),
        "localConfiguration": None,
    }
    if project.local is not None:
        output["localConfiguration"] = {
            "python": str(project.local.python),
            "sdkWheel": str(project.local.sdk_wheel),
            "cliExecutable": str(project.local.cli_executable),
            "vendorPaths": {key: str(value) for key, value in project.local.vendor_paths.items()},
            "vendorDlls": sorted(project.local.vendor_dlls),
            "planBindings": [
                {
                    "vendorPath": binding.vendor_path,
                    "instrumentId": binding.instrument_id,
                    "configField": binding.config_field,
                }
                for binding in project.local.plan_bindings
            ],
        }
    print(json.dumps(output, indent=2, ensure_ascii=True))


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    create = commands.add_parser("create", help="create the minimal project scaffold")
    create.add_argument("destination", type=Path)
    create.add_argument("--extension-id", required=True)
    create.add_argument("--driver-id", required=True)
    create.add_argument("--command-id", required=True)
    create.add_argument("--author", required=True)
    validate = commands.add_parser("validate", help="read and validate project configuration")
    validate.add_argument("project", type=Path, nargs="?", default=Path.cwd())
    check = commands.add_parser(
        "check",
        help="inspect explicit machine-local prerequisites without installing or running ARTest",
    )
    check.add_argument("project", type=Path, nargs="?", default=Path.cwd())
    local_plan = commands.add_parser(
        "materialize-plan", help="write a generated plan copy with configured local path bindings"
    )
    local_plan.add_argument("project", type=Path, nargs="?", default=Path.cwd())
    args = parser.parse_args(argv)
    try:
        if args.command == "create":
            print(
                create_project(
                    args.destination,
                    extension_id=args.extension_id,
                    driver_id=args.driver_id,
                    command_id=args.command_id,
                    author=args.author,
                )
            )
        elif args.command == "validate":
            _print_validation(load_project(args.project))
        elif args.command == "check":
            report = check_prerequisites(load_project(args.project))
            print(json.dumps(report.as_dict(), indent=2, ensure_ascii=True))
            return 0 if report.success else 1
        else:
            print(materialize_local_plan(load_project(args.project)))
    except ProjectError as error:
        parser.error(str(error))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
