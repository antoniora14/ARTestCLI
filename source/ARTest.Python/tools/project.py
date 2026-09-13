"""Create and validate a minimal portable ARTest Python project."""

import argparse
from dataclasses import dataclass
import json
import os
from pathlib import Path, PureWindowsPath
import re
import shutil
import stat
import tempfile


CONFIG_NAME = "artest-project.json"
LOCAL_CONFIG_NAME = "artest-project.local.json"
CONFIG_VERSION = 1
ENTRY_POINT_PATTERN = re.compile(
    r"[A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*:[A-Za-z_][A-Za-z0-9_]*\Z"
)
STABLE_ID_PATTERN = re.compile(r"[a-z0-9]+(?:[.-][a-z0-9]+(?:-[a-z0-9]+)*)+\Z")


class ProjectError(ValueError):
    """A project cannot be created or its configuration is invalid."""


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
        {"vendorPaths"},
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
    local = LocalConfiguration(
        python=_local_path(root, local_value["python"], "python", local_path),
        sdk_wheel=_local_path(root, local_value["sdkWheel"], "sdkWheel", local_path),
        cli_executable=_local_path(root, local_value["cliExecutable"], "cliExecutable", local_path),
        vendor_paths=vendor_paths,
    )
    return Project(configuration=configuration, local=local)


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
        }
    print(json.dumps(output, indent=2, ensure_ascii=True))


def main() -> None:
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
    args = parser.parse_args()
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
        else:
            _print_validation(load_project(args.project))
    except ProjectError as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()
