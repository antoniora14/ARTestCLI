import ast
import base64
from contextlib import redirect_stdout
import csv
import hashlib
import io
import json
import os
from pathlib import Path
import re
import shutil
import struct
import sys
import tempfile
import unittest
from unittest import mock
import zipfile


PYTHON_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PYTHON_ROOT / "tools"))
import project


class ProjectTests(unittest.TestCase):
    EXTENSION_ID = "com.example.tests.minimal"
    DRIVER_ID = "com.example.tests.driver.simulated-source"
    COMMAND_ID = "com.example.tests.command.measure-value"
    AUTHOR = "ARTest test author"

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="artest-project-test-")
        self.root = Path(self.temporary.name)

    def tearDown(self):
        self.temporary.cleanup()

    def create(self, name="project"):
        return project.create_project(
            self.root / name,
            extension_id=self.EXTENSION_ID,
            driver_id=self.DRIVER_ID,
            command_id=self.COMMAND_ID,
            author=self.AUTHOR,
        )

    def portable_value(self, root):
        return json.loads((root / project.CONFIG_NAME).read_text(encoding="utf-8"))

    def test_private_preparation_loader_does_not_relax_public_cli_contract(self):
        root = self.create("private preparation configuration")
        python = self.root / "selected python.exe"
        wheel = self.root / "installed sdk.whl"
        portable = (root / project.CONFIG_NAME).read_bytes()
        local = {"schemaVersion": 1, "vendorPaths": {"vendor": "vendor/library.dll"},
                 "vendorDlls": ["vendor"], "planBindings": []}
        self.write_local(root, local)
        original = (root / project.LOCAL_CONFIG_NAME).read_bytes()
        with self.assertRaises(project.ProjectError):
            project.load_project(root)
        loaded = project._load_project(root, preparation=(python, wheel))
        self.assertEqual(loaded.local.python, python.resolve())
        self.assertEqual(loaded.local.sdk_wheel, wheel.resolve())
        self.assertIsNone(loaded.local.cli_executable)
        self.assertEqual(loaded.local.vendor_paths["vendor"], (root / "vendor/library.dll").resolve())
        self.assertEqual(loaded.local.vendor_dlls, frozenset({"vendor"}))
        self.assertEqual((root / project.LOCAL_CONFIG_NAME).read_bytes(), original)
        self.assertEqual((root / project.CONFIG_NAME).read_bytes(), portable)
        local.update(python="old/python.exe", sdkWheel="old/sdk.whl", cliExecutable="old/ARTestCLI.exe")
        self.write_local(root, local)
        original = (root / project.LOCAL_CONFIG_NAME).read_bytes()
        loaded = project._load_project(root, preparation=(python, wheel))
        self.assertEqual(loaded.local.cli_executable, (root / "old/ARTestCLI.exe").resolve())
        self.assertEqual((root / project.LOCAL_CONFIG_NAME).read_bytes(), original)
        self.assertEqual(project.load_project(root).local.python, (root / "old/python.exe").resolve())

    def test_private_preparation_loader_preserves_local_validation(self):
        root = self.create("private invalid configuration")
        for local in ({"schemaVersion": 2}, {"schemaVersion": 1, "foreign": True},
                      {"schemaVersion": 1, "vendorDlls": ["unknown"]}):
            self.write_local(root, local)
            original = (root / project.LOCAL_CONFIG_NAME).read_bytes()
            with self.assertRaises(project.ProjectError):
                project._load_project(root, preparation=(self.root / "python.exe", self.root / "sdk.whl"))
            self.assertEqual((root / project.LOCAL_CONFIG_NAME).read_bytes(), original)

    def write_portable(self, root, value):
        (root / project.CONFIG_NAME).write_text(
            json.dumps(value, indent=2) + "\n", encoding="utf-8"
        )

    def write_local(self, root, value):
        (root / project.LOCAL_CONFIG_NAME).write_text(
            json.dumps(value, indent=2) + "\n", encoding="utf-8"
        )

    def write_pe(self, path, machine=project.PE_MACHINE_AMD64, dll=None):
        path.parent.mkdir(parents=True, exist_ok=True)
        if dll is None:
            dll = path.suffix.lower() == ".dll"
        data = bytearray(90)
        data[:2] = b"MZ"
        struct.pack_into("<I", data, 0x3C, 64)
        data[64:68] = b"PE\0\0"
        struct.pack_into("<H", data, 68, machine)
        struct.pack_into("<H", data, 70, 1)
        struct.pack_into("<H", data, 84, 2)
        characteristics = project.PE_EXECUTABLE_IMAGE | (project.PE_DLL if dll else 0)
        struct.pack_into("<H", data, 86, characteristics)
        struct.pack_into("<H", data, 88, project.PE32_PLUS_MAGIC)
        path.write_bytes(data)
        return path

    def write_wheel(
        self,
        path,
        *,
        name="artest-python",
        version="0.2.0",
        requires_python=">=3.13,<3.14",
        requirements=None,
        tag="py3-none-any",
    ):
        path.parent.mkdir(parents=True, exist_ok=True)
        if requirements is None:
            requirements = (
                "protobuf==6.33.4",
                "pywin32==311; sys_platform == 'win32'",
            )
        metadata = (
            "Metadata-Version: 2.1\n"
            f"Name: {name}\n"
            f"Version: {version}\n"
            f"Requires-Python: {requires_python}\n"
            + "".join(f"Requires-Dist: {item}\n" for item in requirements)
        )
        wheel = (
            "Wheel-Version: 1.0\n"
            "Generator: ARTest test\n"
            "Root-Is-Purelib: true\n"
            f"Tag: {tag}\n"
        )
        with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as archive:
            prefix = "artest_python-0.2.0.dist-info/"
            archive.writestr(prefix + "METADATA", metadata)
            archive.writestr(prefix + "WHEEL", wheel)
            archive.writestr(prefix + "RECORD", "")
        return path

    def write_installable_wheel(
        self, path, *, name, version, files, requirements=(), requires_python=None
    ):
        normalized = re.sub(r"[-_.]+", "_", name)
        info = f"{normalized}-{version}.dist-info"
        metadata = (
            "Metadata-Version: 2.1\n"
            f"Name: {name}\n"
            f"Version: {version}\n"
            + (f"Requires-Python: {requires_python}\n" if requires_python else "")
            + "".join(f"Requires-Dist: {item}\n" for item in requirements)
        ).encode("utf-8")
        members = {
            **{key: value.encode("utf-8") for key, value in files.items()},
            f"{info}/METADATA": metadata,
            f"{info}/WHEEL": (
                "Wheel-Version: 1.0\n"
                "Generator: ARTest integration test\n"
                "Root-Is-Purelib: true\n"
                "Tag: py3-none-any\n"
            ).encode("utf-8"),
        }
        record = io.StringIO()
        writer = csv.writer(record, lineterminator="\n")
        for member_name, content in sorted(members.items()):
            digest = base64.urlsafe_b64encode(hashlib.sha256(content).digest()).rstrip(b"=")
            writer.writerow([member_name, "sha256=" + digest.decode("ascii"), len(content)])
        writer.writerow([f"{info}/RECORD", "", ""])
        members[f"{info}/RECORD"] = record.getvalue().encode("utf-8")
        path.parent.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as archive:
            for member_name, content in sorted(members.items()):
                archive.writestr(member_name, content)
        return path

    def assert_published_launcher(self, result):
        environment = result.receipt.parent.resolve(strict=True)
        launcher = environment / "artest-launch.py"
        tree = ast.parse(launcher.read_text(encoding="utf-8"), filename=str(launcher))
        prefix = None
        search_paths = None
        dll_directory = None
        for node in ast.walk(tree):
            if isinstance(node, ast.Assign) and len(node.targets) == 1:
                target = node.targets[0]
                if (
                    isinstance(target, ast.Attribute)
                    and isinstance(target.value, ast.Name)
                    and target.value.id == "sys"
                    and target.attr == "prefix"
                ):
                    prefix = ast.literal_eval(node.value)
                elif isinstance(target, ast.Subscript):
                    search_paths = ast.literal_eval(node.value)
            if (
                isinstance(node, ast.Call)
                and isinstance(node.func, ast.Attribute)
                and isinstance(node.func.value, ast.Name)
                and node.func.value.id == "os"
                and node.func.attr == "add_dll_directory"
            ):
                dll_directory = ast.literal_eval(node.args[0])
        expected_site = environment / "Lib" / "site-packages"
        expected_paths = [
            str(expected_site),
            str(expected_site / "win32"),
            str(expected_site / "win32" / "lib"),
        ]
        self.assertEqual(prefix, str(environment))
        self.assertEqual(search_paths, expected_paths)
        self.assertEqual(dll_directory, str(expected_site / "pywin32_system32"))
        for value in (prefix, *search_paths, dll_directory):
            path = Path(value).resolve(strict=True)
            path.relative_to(environment)

    def supported_probe(self, **overrides):
        value = {
            "implementation": "cpython",
            "version": [3, 13, 15],
            "platform": "win32",
            "machine": "AMD64",
            "pointerBits": 64,
            "gilEnabled": True,
        }
        value.update(overrides)
        return value

    def configure_local(
        self,
        root,
        *,
        vendor_paths=None,
        vendor_dlls=None,
        plan_bindings=None,
    ):
        python = root / "local tools" / "python.exe"
        python.parent.mkdir(parents=True, exist_ok=True)
        python.write_bytes(b"test interpreter placeholder")
        wheel = self.write_wheel(root / "local tools" / "sdk wheel.whl")
        cli = self.write_pe(root / "local tools" / "ARTestCLI.exe")
        value = {
            "schemaVersion": 1,
            "python": str(python),
            "sdkWheel": str(wheel),
            "cliExecutable": str(cli),
            "vendorPaths": vendor_paths or {},
            "vendorDlls": vendor_dlls or [],
            "planBindings": plan_bindings or [],
        }
        self.write_local(root, value)
        return value

    def diagnostic_codes(self, report):
        return {item.code for item in report.diagnostics}

    def stage3_identity(self, loaded, probe):
        configuration = loaded.configuration
        local = loaded.local
        source = []
        for item in sorted(configuration.source_directory.rglob("*")):
            if item.is_file() and item.suffix != ".pyc":
                source.append(
                    {
                        "path": item.relative_to(configuration.source_directory).as_posix(),
                        "sha256": project._sha256(item),
                    }
                )
        return {
            "schemaVersion": project.PREPARATION_SCHEMA_VERSION,
            "source": source,
            "entryPoint": configuration.entry_point,
            "dependencyLockSha256": project._sha256(configuration.dependency_lock),
            "sdkWheelSha256": project._sha256(local.sdk_wheel),
            "interpreter": {
                "path": str(local.python),
                "sha256": project._sha256(local.python),
                "probe": probe,
            },
            "tools": {"testDouble": "stage3-v1"},
        }

    class Stage3PackageRunner:
        def __init__(self, owner):
            self.owner = owner
            self.calls = []
            self.install_count = 0
            self.fail_next_prepare = None
            self.after_prepare = None

        @staticmethod
        def value(arguments, option):
            return Path(arguments[arguments.index(option) + 1])

        def __call__(self, python, arguments):
            self.calls.append((Path(python), tuple(arguments)))
            command = arguments[0]
            if command == "package":
                source = self.value(arguments, "--source")
                lock = self.value(arguments, "--lock")
                output = self.value(arguments, "--output")
                if output.exists():
                    manifest = json.loads(
                        (output / "artest-extension.json").read_text(encoding="utf-8")
                    )
                    project._package_module().verify_inventory(
                        output, manifest["inventory"], "artest-extension.json"
                    )
                    return
                (output / "code").mkdir(parents=True)
                for item in source.rglob("*"):
                    if item.is_file() and item.suffix != ".pyc":
                        destination = output / "code" / item.relative_to(source)
                        destination.parent.mkdir(parents=True, exist_ok=True)
                        shutil.copy2(item, destination)
                shutil.copy2(lock, output / "requirements.lock")
                text = (source / "extension.py").read_text(encoding="utf-8")
                extension_id = re.search(r'^EXTENSION_ID = "([^"]+)"$', text, re.MULTILINE).group(1)
                manifest = {
                    "schemaVersion": 3,
                    "extensionId": extension_id,
                    "runtime": {"dependencyLock": "requirements.lock"},
                    "inventory": project._package_module().inventory(output),
                }
                (output / "artest-extension.json").write_text(
                    json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
                )
                return
            if command != "prepare":
                raise AssertionError(command)
            if self.fail_next_prepare is not None:
                failure = self.fail_next_prepare
                self.fail_next_prepare = None
                raise failure
            package_root = self.value(arguments, "--package")
            sdk = self.value(arguments, "--sdk")
            output = self.value(arguments, "--output")
            manifest = json.loads(
                (package_root / "artest-extension.json").read_text(encoding="utf-8")
            )
            if output.exists():
                receipt = json.loads(
                    (output / "artest-environment.json").read_text(encoding="utf-8")
                )
                project._package_module().verify_inventory(
                    output, receipt["files"], "artest-environment.json"
                )
                if receipt["sdkSha256"] != project._sha256(sdk):
                    raise ValueError("SDK mismatch")
                return
            self.install_count += 1
            (output / "Scripts").mkdir(parents=True)
            (output / "Scripts" / "python.exe").write_bytes(b"venv redirector")
            (output / "artest-launch.py").write_text("# controlled launcher\n", encoding="utf-8")
            receipt = {
                "schemaVersion": 1,
                "extensionId": manifest["extensionId"],
                "packageSha256": project._sha256(package_root / "artest-extension.json"),
                "interpreter": str(Path(python).resolve(strict=True)),
                "interpreterSha256": project._sha256(Path(python)),
                "pythonVersion": "3.13.15",
                "sdkSha256": project._sha256(sdk),
                "launcher": "artest-launch.py",
                "runtimeFiles": [
                    {"path": str(Path(python).resolve(strict=True)), "sha256": project._sha256(Path(python))}
                ],
                "files": project._package_module().inventory(output),
            }
            (output / "artest-environment.json").write_text(
                json.dumps(receipt, indent=2) + "\n", encoding="utf-8"
            )
            if self.after_prepare is not None:
                callback = self.after_prepare
                self.after_prepare = None
                callback()

    def prepare_with_double(self, root, runner=None):
        loaded = project.load_project(root)
        runner = runner or self.Stage3PackageRunner(self)
        result = project.prepare_project(
            loaded,
            package_runner=runner,
            probe_runner=lambda path: self.supported_probe(),
            identity_builder=self.stage3_identity,
        )
        return result, runner

    def exercise_probe_timeout_cleanup(self, *, kill_fails, termination_confirmed):
        class ControlledProcess:
            def __init__(self):
                self.stdout = io.BytesIO()
                self.stderr = io.BytesIO()
                self.wait_timeouts = []
                self.kill_calls = 0

            def wait(self, timeout=None):
                self.wait_timeouts.append(timeout)
                if len(self.wait_timeouts) == 1:
                    raise project.subprocess.TimeoutExpired("python", timeout)
                if termination_confirmed:
                    return -9
                raise project.subprocess.TimeoutExpired("python", timeout)

            def kill(self):
                self.kill_calls += 1
                if kill_fails:
                    raise OSError("controlled kill failure")

        class ControlledReader:
            instances = []

            def __init__(self, *, target, args, daemon):
                self.target = target
                self.args = args
                self.daemon = daemon
                self.join_timeouts = []
                self.running = False
                self.__class__.instances.append(self)

            def start(self):
                self.running = True
                self.target(*self.args)
                self.running = False

            def join(self, timeout=None):
                self.join_timeouts.append(timeout)

            def is_alive(self):
                return self.running

        process_double = ControlledProcess()
        with (
            mock.patch.object(project.subprocess, "Popen", return_value=process_double),
            mock.patch.object(project.threading, "Thread", ControlledReader),
        ):
            with self.assertRaises(project.InterpreterProbeError) as raised:
                project._run_python_probe(Path("C:/fake/python.exe"))
        self.assertEqual(process_double.kill_calls, 1)
        self.assertEqual(
            process_double.wait_timeouts,
            [
                project.PYTHON_PROBE_TIMEOUT_SECONDS,
                project.PYTHON_PROBE_TERMINATION_TIMEOUT_SECONDS,
            ],
        )
        self.assertEqual(len(ControlledReader.instances), 2)
        for reader in ControlledReader.instances:
            self.assertEqual(
                reader.join_timeouts, [project.PYTHON_PROBE_READER_TIMEOUT_SECONDS]
            )
        return raised.exception

    def test_prepare_creates_verified_revision_and_reuses_without_installing(self):
        root = self.create()
        self.configure_local(root)
        first, runner = self.prepare_with_double(root)
        second, _ = self.prepare_with_double(root, runner)

        self.assertFalse(first.reused)
        self.assertTrue(second.reused)
        self.assertEqual(first.preparation_id, second.preparation_id)
        self.assertEqual(runner.install_count, 1)
        self.assertTrue(first.package.is_dir())
        self.assertTrue(first.receipt.is_file())
        self.assertTrue(first.association.is_file())
        association = json.loads(first.association.read_text(encoding="utf-8"))
        self.assertEqual(association, {self.EXTENSION_ID: str(first.receipt)})
        ready = json.loads(
            (root / project.PREPARATION_ROOT / "ready.json").read_text(encoding="utf-8")
        )
        self.assertEqual(ready["preparationId"], first.preparation_id)
        commands = [arguments[0] for _, arguments in runner.calls]
        self.assertEqual(commands, ["package", "prepare", "package", "prepare", "package", "prepare"])
        for python, arguments in runner.calls:
            self.assertEqual(python, project.load_project(root).local.python)
            self.assertIn("--output", arguments)

    def test_prepare_can_publish_directly_to_an_installation_owned_root(self):
        root = self.create("source project")
        self.configure_local(root)
        output = root.parent / "installation" / "configuration" / "python"
        loaded = project.load_project(root)
        runner = self.Stage3PackageRunner(self)

        first = project.prepare_project(
            loaded,
            output_root=output,
            package_runner=runner,
            probe_runner=lambda path: self.supported_probe(),
            identity_builder=self.stage3_identity,
        )
        second = project.prepare_project(
            loaded,
            output_root=output,
            package_runner=runner,
            probe_runner=lambda path: self.supported_probe(),
            identity_builder=self.stage3_identity,
        )

        self.assertEqual(first.revision.parent, output / "revisions")
        self.assertEqual(first.receipt.parent.parent, first.revision)
        self.assertFalse(first.reused)
        self.assertTrue(second.reused)
        self.assertFalse((root / project.PREPARATION_ROOT).exists())

    @unittest.skipUnless(
        sys.platform == "win32"
        and sys.version_info[:2] == (3, 13)
        and sys.maxsize == 2**63 - 1
        and getattr(sys, "_is_gil_enabled", lambda: False)(),
        "real preparation requires supported standard CPython 3.13 Windows x64",
    )
    def test_real_prepare_launcher_uses_published_environment_and_reuses(self):
        root = self.create("real preparation")
        wheelhouse = root / "local wheels"
        protobuf = self.write_installable_wheel(
            wheelhouse / "protobuf-6.33.4-py3-none-any.whl",
            name="protobuf",
            version="6.33.4",
            files={"google/protobuf/__init__.py": "# integration fixture\n"},
        )
        pywin32 = self.write_installable_wheel(
            wheelhouse / "pywin32-311-py3-none-any.whl",
            name="pywin32",
            version="311",
            files={
                "win32/__init__.py": "# integration fixture\n",
                "win32/lib/__init__.py": "# integration fixture\n",
                "pywin32_system32/fixture.txt": "integration fixture\n",
            },
        )
        sdk = self.write_installable_wheel(
            wheelhouse / "artest_python-0.2.0-py3-none-any.whl",
            name="artest-python",
            version="0.2.0",
            requires_python=">=3.13,<3.14",
            requirements=(
                "protobuf==6.33.4",
                "pywin32==311; sys_platform == 'win32'",
            ),
            files={
                "artest_sdk/__init__.py": "# integration fixture\n",
                "artest_host/__init__.py": "# integration fixture\n",
            },
        )
        (root / "requirements.lock").write_text(
            f"protobuf @ {protobuf.as_uri()} --hash=sha256:{project._sha256(protobuf)}\n"
            f"pywin32 @ {pywin32.as_uri()} --hash=sha256:{project._sha256(pywin32)}\n",
            encoding="utf-8",
        )
        self.write_local(
            root,
            {
                "schemaVersion": 1,
                "python": str(Path(sys.executable).resolve()),
                "sdkWheel": str(sdk.resolve()),
                "cliExecutable": str(self.write_pe(root / "local tools" / "ARTestCLI.exe")),
                "vendorPaths": {},
                "vendorDlls": [],
                "planBindings": [],
            },
        )

        first = project.prepare_project(project.load_project(root))
        self.assertFalse(first.reused)
        self.assert_published_launcher(first)
        self.assertFalse(any((root / project.PREPARATION_ROOT / "work").iterdir()))

        second = project.prepare_project(project.load_project(root))
        self.assertTrue(second.reused)
        self.assertEqual(second.preparation_id, first.preparation_id)
        self.assert_published_launcher(second)

    def test_prepare_same_size_source_edit_creates_new_immutable_revision(self):
        root = self.create()
        self.configure_local(root)
        first, runner = self.prepare_with_double(root)
        source = root / "src" / "extension.py"
        original = source.read_text(encoding="utf-8")
        changed = original.replace("self.value = 0.0", "self.value = 1.0", 1)
        self.assertEqual(len(original), len(changed))
        source.write_text(changed, encoding="utf-8")

        second, _ = self.prepare_with_double(root, runner)

        self.assertNotEqual(first.preparation_id, second.preparation_id)
        self.assertEqual(runner.install_count, 2)
        self.assertTrue(first.revision.is_dir())
        self.assertTrue(second.revision.is_dir())

    def test_prepare_lock_sdk_and_interpreter_changes_each_invalidate(self):
        root = self.create()
        local_value = self.configure_local(root)
        result, runner = self.prepare_with_double(root)
        identities = [result.preparation_id]

        lock = root / "requirements.lock"
        lock.write_text(lock.read_text(encoding="utf-8") + "# lock identity\n", encoding="utf-8")
        result, _ = self.prepare_with_double(root, runner)
        identities.append(result.preparation_id)

        wheel = Path(local_value["sdkWheel"])
        self.write_wheel(wheel, requirements=(
            "protobuf==6.33.4", "pywin32==311; sys_platform == 'win32'",
        ))
        with zipfile.ZipFile(wheel, "a") as archive:
            archive.writestr("identity-marker.txt", "changed SDK content")
        result, _ = self.prepare_with_double(root, runner)
        identities.append(result.preparation_id)

        second_python = root / "local tools" / "alternate-python.exe"
        second_python.write_bytes(b"alternate test interpreter")
        local_value["python"] = str(second_python)
        self.write_local(root, local_value)
        result, _ = self.prepare_with_double(root, runner)
        identities.append(result.preparation_id)

        self.assertEqual(len(set(identities)), 4)
        self.assertEqual(runner.install_count, 4)

    def test_prepare_plan_only_change_reuses_environment(self):
        root = self.create()
        self.configure_local(root)
        first, runner = self.prepare_with_double(root)
        plan = root / "plan" / "measurement.json"
        value = json.loads(plan.read_text(encoding="utf-8"))
        value["name"] = "Plan-only edit"
        plan.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")

        second, _ = self.prepare_with_double(root, runner)

        self.assertTrue(second.reused)
        self.assertEqual(first.preparation_id, second.preparation_id)
        self.assertEqual(runner.install_count, 1)

    def test_prepare_rejects_package_receipt_and_environment_corruption(self):
        corruptions = (
            ("package", lambda result: (result.package / "code" / "extension.py").write_text("corrupt\n", encoding="utf-8")),
            ("receipt", lambda result: result.receipt.write_text("{}\n", encoding="utf-8")),
            ("environment", lambda result: (result.receipt.parent / "unknown.bin").write_bytes(b"unknown")),
        )
        for index, (label, corrupt) in enumerate(corruptions):
            with self.subTest(label=label):
                root = self.create(f"project-{index}")
                self.configure_local(root)
                first, runner = self.prepare_with_double(root)
                ready = root / project.PREPARATION_ROOT / "ready.json"
                selected = ready.read_bytes()
                corrupt(first)
                with self.assertRaises(project.ProjectError):
                    self.prepare_with_double(root, runner)
                self.assertEqual(ready.read_bytes(), selected)
                self.assertEqual(runner.install_count, 1)

    def test_prepare_failure_and_interruption_preserve_previous_selection(self):
        for index, failure in enumerate((
            project.ProjectError("controlled install failure"),
            KeyboardInterrupt("controlled interruption"),
        )):
            with self.subTest(failure=type(failure).__name__):
                root = self.create(f"project-{index}")
                self.configure_local(root)
                _, runner = self.prepare_with_double(root)
                ready = root / project.PREPARATION_ROOT / "ready.json"
                selected = ready.read_bytes()
                source = root / "src" / "extension.py"
                source.write_text(source.read_text(encoding="utf-8") + "\n# changed\n", encoding="utf-8")
                runner.fail_next_prepare = failure
                with self.assertRaises(type(failure)):
                    self.prepare_with_double(root, runner)
                self.assertEqual(ready.read_bytes(), selected)
                self.assertFalse((root / project.PREPARATION_ROOT / "preparation.lock").exists())
                incomplete = list((root / project.PREPARATION_ROOT / "incomplete").iterdir())
                self.assertEqual(len(incomplete), 1)
                disposition = json.loads((incomplete[0] / "failure.json").read_text(encoding="utf-8"))
                self.assertIn("never selected", disposition["disposition"])

    def test_prepare_detects_input_mutation_before_publication(self):
        root = self.create()
        self.configure_local(root)
        first, runner = self.prepare_with_double(root)
        ready = root / project.PREPARATION_ROOT / "ready.json"
        selected = ready.read_bytes()
        source = root / "src" / "extension.py"
        source.write_text(source.read_text(encoding="utf-8") + "\n# revision two\n", encoding="utf-8")
        runner.after_prepare = lambda: source.write_text(
            source.read_text(encoding="utf-8") + "# mutated during prepare\n", encoding="utf-8"
        )

        with self.assertRaisesRegex(project.ProjectError, "inputs changed"):
            self.prepare_with_double(root, runner)

        self.assertEqual(ready.read_bytes(), selected)
        current_ready = json.loads(ready.read_text(encoding="utf-8"))
        self.assertEqual(current_ready["preparationId"], first.preparation_id)

    def test_prepare_rejects_concurrent_or_stale_lock(self):
        root = self.create()
        self.configure_local(root)
        stage3 = root / project.PREPARATION_ROOT
        (stage3 / "work").mkdir(parents=True)
        (stage3 / "incomplete").mkdir()
        (stage3 / "revisions").mkdir()
        lock = stage3 / "preparation.lock"
        lock.write_text('{"owner":"other"}\n', encoding="utf-8")

        with self.assertRaisesRegex(project.ProjectError, "Another preparation"):
            self.prepare_with_double(root)

        self.assertTrue(lock.exists())

    def test_prepare_rejects_unknown_stage3_file_without_repair(self):
        root = self.create()
        self.configure_local(root)
        stage3 = root / project.PREPARATION_ROOT
        stage3.mkdir(parents=True)
        unknown = stage3 / "foreign.txt"
        unknown.write_text("preserve me\n", encoding="utf-8")

        with self.assertRaisesRegex(project.ProjectError, "Unknown files"):
            self.prepare_with_double(root)

        self.assertEqual(unknown.read_text(encoding="utf-8"), "preserve me\n")

    def test_main_exposes_explicit_run_after_prepare(self):
        parser_output = io.StringIO()
        with self.assertRaises(SystemExit), redirect_stdout(parser_output):
            project.main(["--help"])
        help_text = parser_output.getvalue()
        self.assertIn("prepare", help_text)
        command_set = help_text.splitlines()[1].strip()
        self.assertIn("run", command_set)

    def test_run_prepares_compiles_executes_reuses_and_detects_source_edits(self):
        root = self.create("project with spaces")
        self.configure_local(root)
        package_runner = self.Stage3PackageRunner(self)
        cli_calls = []

        def cli_runner(arguments, timeout, operation):
            cli_calls.append((tuple(arguments), timeout, operation, Path.cwd()))
            return 0

        outside = self.root / "different working directory"
        outside.mkdir()
        previous = Path.cwd()
        try:
            os.chdir(outside)
            first = project.run_project(
                project.load_project(root),
                package_runner=package_runner,
                probe_runner=lambda path: self.supported_probe(),
                identity_builder=self.stage3_identity,
                cli_runner=cli_runner,
            )
            second = project.run_project(
                project.load_project(root),
                package_runner=package_runner,
                probe_runner=lambda path: self.supported_probe(),
                identity_builder=self.stage3_identity,
                cli_runner=cli_runner,
            )
            source = root / "src" / "extension.py"
            source.write_text(
                source.read_text(encoding="utf-8") + "\n# Stage 4 edit\n",
                encoding="utf-8",
            )
            third = project.run_project(
                project.load_project(root),
                package_runner=package_runner,
                probe_runner=lambda path: self.supported_probe(),
                identity_builder=self.stage3_identity,
                cli_runner=cli_runner,
            )
        finally:
            os.chdir(previous)

        self.assertFalse(first.preparation.reused)
        self.assertTrue(second.preparation.reused)
        self.assertFalse(third.preparation.reused)
        self.assertEqual(first.preparation.preparation_id, second.preparation.preparation_id)
        self.assertNotEqual(second.preparation.preparation_id, third.preparation.preparation_id)
        self.assertEqual(package_runner.install_count, 2)
        self.assertEqual(len(cli_calls), 6)
        for index, (arguments, timeout, _, observed_cwd) in enumerate(cli_calls):
            self.assertEqual(arguments[1], "compile" if index % 2 == 0 else "extension-run")
            self.assertEqual(Path(arguments[2]), root / "plan" / "measurement.json")
            if index % 2 == 0:
                self.assertEqual(arguments[3], "--extensions")
                self.assertEqual(arguments[5], "--python-environments")
            else:
                self.assertEqual(Path(arguments[3]), Path(cli_calls[index - 1][0][4]))
                self.assertEqual(arguments[4], "--python-environments")
            self.assertGreater(timeout, 0)
            self.assertEqual(observed_cwd, outside)

    def test_sources_run_composes_selected_catalog_and_associations_without_mutation(self):
        root = self.create()
        self.configure_local(root)
        installed_catalog = self.root / "selected installation" / "catalog"
        installed_catalog.mkdir(parents=True)
        driver_package = installed_catalog / "registered-driver"
        driver_package.mkdir()
        (driver_package / "artest-extension.json").write_text(
            json.dumps({"extensionId": "com.example.registered.driver"}) + "\n",
            encoding="utf-8",
        )
        (driver_package / "driver.bin").write_bytes(b"registered driver")
        old_project = installed_catalog / "registered-old-project"
        old_project.mkdir()
        (old_project / "artest-extension.json").write_text(
            json.dumps({"extensionId": self.EXTENSION_ID}) + "\n", encoding="utf-8"
        )
        installed_mapping = self.root / "selected installation" / "python-environments.json"
        installed_mapping.write_text(
            json.dumps({"com.example.registered.driver": "D:/installed/driver-receipt.json"}) + "\n",
            encoding="utf-8",
        )
        catalog_before = project._content_inventory(installed_catalog)
        mapping_before = installed_mapping.read_bytes()
        calls = []

        result = project.run_project(
            project.load_project(root),
            package_runner=self.Stage3PackageRunner(self),
            probe_runner=lambda path: self.supported_probe(),
            identity_builder=self.stage3_identity,
            cli_runner=lambda arguments, timeout, operation: (calls.append(tuple(arguments)) or 0),
            installation_catalog=installed_catalog,
            installation_association=installed_mapping,
            expected_extension_id=self.EXTENSION_ID,
        )

        self.assertEqual(result.mode, "sources")
        self.assertTrue(result.catalog.is_relative_to(root / project.EXECUTION_CATALOG_ROOT))
        packages = project._catalog_packages(result.catalog)
        self.assertEqual(set(packages), {"com.example.registered.driver", self.EXTENSION_ID})
        composed_mapping = json.loads(result.association.read_text(encoding="utf-8"))
        self.assertEqual(
            composed_mapping["com.example.registered.driver"],
            "D:/installed/driver-receipt.json",
        )
        self.assertEqual(
            composed_mapping[self.EXTENSION_ID], str(result.preparation.receipt.resolve())
        )
        self.assertEqual(project._content_inventory(installed_catalog), catalog_before)
        self.assertEqual(installed_mapping.read_bytes(), mapping_before)
        self.assertEqual(Path(calls[0][6]), result.association)
        self.assertEqual(Path(calls[1][3]), result.catalog)

    def test_registered_run_uses_selected_revision_without_preparing_or_copying(self):
        root = self.create()
        self.configure_local(root)
        installed_catalog = self.root / "target" / "catalog"
        package = installed_catalog / "registered-project"
        package.mkdir(parents=True)
        (package / "artest-extension.json").write_text(
            json.dumps({"extensionId": self.EXTENSION_ID}) + "\n", encoding="utf-8"
        )
        mapping = self.root / "target" / "python-environments.json"
        mapping.write_text(
            json.dumps({self.EXTENSION_ID: "D:/installed/project-receipt.json"}) + "\n",
            encoding="utf-8",
        )
        calls = []

        result = project.run_project(
            project.load_project(root),
            probe_runner=lambda path: self.fail("registered mode must not probe Python"),
            identity_builder=lambda *args: self.fail("registered mode must not build an identity"),
            package_runner=lambda *args: self.fail("registered mode must not prepare"),
            cli_runner=lambda arguments, timeout, operation: (calls.append(tuple(arguments)) or 0),
            mode="registered",
            installation_catalog=installed_catalog,
            installation_association=mapping,
            expected_extension_id=self.EXTENSION_ID,
        )

        self.assertIsNone(result.preparation)
        self.assertEqual(result.mode, "registered")
        self.assertEqual(result.catalog, installed_catalog.resolve())
        self.assertEqual(result.association, mapping.resolve())
        self.assertFalse((root / project.PREPARATION_ROOT).exists())
        self.assertEqual(Path(calls[0][6]), mapping.resolve())
        self.assertEqual(Path(calls[1][3]), installed_catalog.resolve())

    def test_run_prerequisite_prepare_and_compile_failures_never_start_execution(self):
        root = self.create()
        local = self.configure_local(root)
        Path(local["cliExecutable"]).unlink()
        cli_calls = []
        with self.assertRaises(project.RunPrerequisiteError) as raised:
            project.run_project(
                project.load_project(root),
                package_runner=self.Stage3PackageRunner(self),
                probe_runner=lambda path: self.supported_probe(),
                identity_builder=self.stage3_identity,
                cli_runner=lambda *args: cli_calls.append(args),
            )
        self.assertIn("CLI_MISSING", self.diagnostic_codes(raised.exception.report))
        self.assertEqual(cli_calls, [])
        self.assertFalse((root / project.PREPARATION_ROOT).exists())

        self.write_pe(Path(local["cliExecutable"]))
        failed_runner = self.Stage3PackageRunner(self)
        failed_runner.fail_next_prepare = project.ProjectError("controlled preparation failure")
        with self.assertRaisesRegex(project.ProjectError, "controlled preparation failure"):
            project.run_project(
                project.load_project(root),
                package_runner=failed_runner,
                probe_runner=lambda path: self.supported_probe(),
                identity_builder=self.stage3_identity,
                cli_runner=lambda *args: cli_calls.append(args),
            )
        self.assertEqual(cli_calls, [])

        compile_calls = []
        result = project.run_project(
            project.load_project(root),
            package_runner=self.Stage3PackageRunner(self),
            probe_runner=lambda path: self.supported_probe(),
            identity_builder=self.stage3_identity,
            cli_runner=lambda arguments, timeout, operation: (
                compile_calls.append(tuple(arguments)) or 3
            ),
        )
        self.assertEqual(result.exit_code, 3)
        self.assertIsNone(result.execution_exit_code)
        self.assertEqual([call[1] for call in compile_calls], ["compile"])

    def test_run_propagates_execution_failures_without_retry(self):
        failures = (("command error", 4), ("cancellation", 130), ("indeterminate effect", 75))
        for index, (label, exit_code) in enumerate(failures):
            with self.subTest(label=label):
                root = self.create(f"failure-{index}")
                self.configure_local(root)
                calls = []

                def cli_runner(arguments, timeout, operation):
                    calls.append((tuple(arguments), operation))
                    return 0 if arguments[1] == "compile" else exit_code

                result = project.run_project(
                    project.load_project(root),
                    package_runner=self.Stage3PackageRunner(self),
                    probe_runner=lambda path: self.supported_probe(),
                    identity_builder=self.stage3_identity,
                    cli_runner=cli_runner,
                )
                self.assertEqual(result.exit_code, exit_code)
                self.assertEqual([call[0][1] for call in calls], ["compile", "extension-run"])

    def test_run_materializes_immutable_local_test_plan_without_editing_portable_plan(self):
        root = self.create()
        plan_path = root / "plan" / "measurement.json"
        plan = json.loads(plan_path.read_text(encoding="utf-8"))
        plan["instruments"][0]["config"]["sdkDll"] = None
        plan_path.write_text(json.dumps(plan, indent=2) + "\n", encoding="utf-8")
        portable_before = plan_path.read_bytes()
        vendor = root / "vendor with spaces" / "simulated sdk.dat"
        vendor.parent.mkdir()
        vendor.write_text("fixture\n", encoding="utf-8")
        self.configure_local(
            root,
            vendor_paths={"simulatedSdk": str(vendor)},
            plan_bindings=[{
                "vendorPath": "simulatedSdk",
                "instrumentId": "SimulatedSource1",
                "configField": "sdkDll",
            }],
        )
        calls = []
        result = project.run_project(
            project.load_project(root),
            package_runner=self.Stage3PackageRunner(self),
            probe_runner=lambda path: self.supported_probe(),
            identity_builder=self.stage3_identity,
            cli_runner=lambda arguments, timeout, operation: (
                calls.append(tuple(arguments)) or 0
            ),
        )

        self.assertEqual(plan_path.read_bytes(), portable_before)
        self.assertTrue(result.plan.is_relative_to(root / project.EXECUTION_PLAN_ROOT))
        generated = json.loads(result.plan.read_text(encoding="utf-8"))
        self.assertEqual(generated["instruments"][0]["config"]["sdkDll"], str(vendor))
        self.assertEqual(Path(calls[0][2]), result.plan)
        self.assertEqual(Path(calls[1][2]), result.plan)

    def test_run_revalidates_inputs_after_compile_and_does_not_execute_stale_sources(self):
        root = self.create()
        self.configure_local(root)
        calls = []

        def cli_runner(arguments, timeout, operation):
            calls.append(tuple(arguments))
            source = root / "src" / "extension.py"
            source.write_text(source.read_text(encoding="utf-8") + "\n# changed\n", encoding="utf-8")
            return 0

        with self.assertRaisesRegex(project.ProjectError, "inputs changed"):
            project.run_project(
                project.load_project(root),
                package_runner=self.Stage3PackageRunner(self),
                probe_runner=lambda path: self.supported_probe(),
                identity_builder=self.stage3_identity,
                cli_runner=cli_runner,
            )
        self.assertEqual([call[1] for call in calls], ["compile"])

    def test_cli_process_timeout_is_bounded_and_never_retried(self):
        class TimedOutProcess:
            def __init__(self):
                self.waits = []
                self.kills = 0

            def wait(self, timeout=None):
                self.waits.append(timeout)
                if len(self.waits) == 1:
                    raise project.subprocess.TimeoutExpired("cli", timeout)
                return -9

            def kill(self):
                self.kills += 1

        process_double = TimedOutProcess()
        with mock.patch.object(project.subprocess, "Popen", return_value=process_double) as popen:
            with self.assertRaisesRegex(project.ProjectError, "was terminated"):
                project._run_cli_process(["cli", "compile"], 7.0, "offline validation")
        popen.assert_called_once_with(["cli", "compile"], shell=False)
        self.assertEqual(process_double.waits, [7.0, project.CLI_TERMINATION_TIMEOUT_SECONDS])
        self.assertEqual(process_double.kills, 1)

    def test_create_and_load_valid_project(self):
        root = self.create()
        loaded = project.load_project(root)
        self.assertEqual(loaded.configuration.root, root)
        self.assertEqual(loaded.configuration.source_directory, root / "src")
        self.assertEqual(loaded.configuration.entry_point, "extension:define_extension")
        self.assertEqual(loaded.configuration.dependency_lock, root / "requirements.lock")
        self.assertEqual(loaded.configuration.plan, root / "plan" / "measurement.json")

    def test_create_uses_exact_supported_dependency_lock(self):
        root = self.create()
        self.assertEqual(
            (root / "requirements.lock").read_bytes(),
            (PYTHON_ROOT / "requirements.lock").read_bytes(),
        )

    def test_missing_local_configuration_is_valid_and_example_is_not_active(self):
        root = self.create()
        self.assertFalse((root / project.LOCAL_CONFIG_NAME).exists())
        self.assertTrue((root / "artest-project.local.example.json").is_file())
        self.assertIsNone(project.load_project(root).local)
        ignore = (root / ".gitignore").read_text(encoding="utf-8").splitlines()
        self.assertIn(project.LOCAL_CONFIG_NAME, ignore)
        self.assertIn(".artest/", ignore)

    def test_local_configuration_is_separate_and_accepts_absolute_paths(self):
        root = self.create()
        absolute_vendor = self.root / "vendor sdk" / "vendor.dll"
        local_value = {
            "schemaVersion": 1,
            "python": "tools/python.exe",
            "sdkWheel": str(self.root / "sdk wheel.whl"),
            "cliExecutable": "tools/ARTestCLI.exe",
            "vendorPaths": {"example": str(absolute_vendor)},
        }
        (root / project.LOCAL_CONFIG_NAME).write_text(
            json.dumps(local_value), encoding="utf-8"
        )
        loaded = project.load_project(root)
        self.assertEqual(loaded.local.python, root / "tools" / "python.exe")
        self.assertEqual(loaded.local.sdk_wheel, self.root / "sdk wheel.whl")
        self.assertEqual(loaded.local.cli_executable, root / "tools" / "ARTestCLI.exe")
        self.assertEqual(loaded.local.vendor_paths["example"], absolute_vendor)
        self.assertEqual(loaded.local.vendor_dlls, frozenset())
        self.assertEqual(loaded.local.plan_bindings, ())
        self.assertNotIn("python", self.portable_value(root))

    def test_local_configuration_structure_and_version_are_validated(self):
        root = self.create()
        base = {
            "schemaVersion": 1,
            "python": "python.exe",
            "sdkWheel": "sdk.whl",
            "cliExecutable": "ARTestCLI.exe",
        }
        cases = (
            ({**base, "schemaVersion": 2}, "Unsupported schemaVersion"),
            ({**base, "schemaVersion": True}, "must be an integer"),
            ({key: value for key, value in base.items() if key != "python"}, "Missing properties"),
            ({**base, "unknown": 1}, "Unknown properties"),
            ({**base, "vendorPaths": []}, "must be a JSON object"),
            ({**base, "vendorPaths": {"sdk": ""}}, "must be a nonempty string"),
            ({**base, "vendorDlls": {}}, "must be a JSON array"),
            ({**base, "vendorDlls": ["sdk"]}, "unknown vendorPaths"),
            ({**base, "planBindings": {}}, "must be a JSON array"),
        )
        for value, message in cases:
            with self.subTest(value=value):
                (root / project.LOCAL_CONFIG_NAME).write_text(
                    json.dumps(value), encoding="utf-8"
                )
                with self.assertRaisesRegex(project.ProjectError, message):
                    project.load_project(root)

    def test_local_binding_structure_is_validated(self):
        root = self.create()
        base = {
            "schemaVersion": 1,
            "python": "python.exe",
            "sdkWheel": "sdk.whl",
            "cliExecutable": "ARTestCLI.exe",
            "vendorPaths": {"sdk": "vendor.dll"},
        }
        cases = (
            ({**base, "planBindings": ["sdk"]}, "must be a JSON object"),
            (
                {
                    **base,
                    "planBindings": [
                        {
                            "vendorPath": "other",
                            "instrumentId": "I1",
                            "configField": "dll",
                        }
                    ],
                },
                "unknown vendorPaths",
            ),
            (
                {
                    **base,
                    "planBindings": [
                        {"vendorPath": "sdk", "instrumentId": "I1", "configField": "dll"},
                        {"vendorPath": "sdk", "instrumentId": "I1", "configField": "dll"},
                    ],
                },
                "duplicate target",
            ),
        )
        for value, message in cases:
            with self.subTest(message=message):
                self.write_local(root, value)
                with self.assertRaisesRegex(project.ProjectError, message):
                    project.load_project(root)

    def test_portable_configuration_structure_and_version_are_validated(self):
        root = self.create()
        base = self.portable_value(root)
        cases = (
            ({**base, "schemaVersion": 2}, "Unsupported schemaVersion"),
            ({**base, "schemaVersion": False}, "must be an integer"),
            ({key: value for key, value in base.items() if key != "plan"}, "Missing properties"),
            ({**base, "unexpected": "value"}, "Unknown properties"),
            ([base], "must be a JSON object"),
        )
        for value, message in cases:
            with self.subTest(value=value):
                self.write_portable(root, value)
                with self.assertRaisesRegex(project.ProjectError, message):
                    project.load_project(root)

    def test_duplicate_and_malformed_json_are_rejected(self):
        root = self.create()
        path = root / project.CONFIG_NAME
        for text, message in (
            ('{"schemaVersion": 1, "schemaVersion": 1}', "Duplicate property"),
            ("{", "Cannot read JSON configuration"),
        ):
            with self.subTest(text=text):
                path.write_text(text, encoding="utf-8")
                with self.assertRaisesRegex(project.ProjectError, message):
                    project.load_project(root)

    def test_entry_point_syntax(self):
        valid = ("extension:define_extension", "package.module_2:create")
        invalid = (
            "extension",
            "extension:",
            ":define_extension",
            "extension..py:define_extension",
            "extension:factory()",
            "extension/submodule:factory",
            "2extension:factory",
            "extension:two:parts",
        )
        for entry_point in valid + invalid:
            with self.subTest(entry_point=entry_point):
                root = self.create("entry-" + str(len(list(self.root.iterdir()))))
                value = self.portable_value(root)
                value["entryPoint"] = entry_point
                self.write_portable(root, value)
                if entry_point in valid:
                    self.assertEqual(project.load_project(root).configuration.entry_point, entry_point)
                else:
                    with self.assertRaisesRegex(project.ProjectError, "dotted.module:callable"):
                        project.load_project(root)

    def test_portable_paths_must_be_relative_and_contained(self):
        fields = ("sourceDirectory", "dependencyLock", "plan")
        for field in fields:
            for path_value in ("../outside", str(self.root / "absolute")):
                with self.subTest(field=field, path_value=path_value):
                    root = self.create("path-" + str(len(list(self.root.iterdir()))))
                    value = self.portable_value(root)
                    value[field] = path_value
                    self.write_portable(root, value)
                    with self.assertRaisesRegex(project.ProjectError, "relative|escapes"):
                        project.load_project(root)

    def test_portable_paths_reject_all_windows_anchors(self):
        fields = ("sourceDirectory", "dependencyLock", "plan")
        invalid_paths = (
            r"C:relative",
            r"\src",
            r"\ruta\proyecto\src",
            "/src",
            "/ruta/proyecto/src",
            r"C:\ruta\src",
            "C:/ruta/src",
            r"\\server\share\src",
            r"\\?\C:\ruta\src",
            r"\\.\C:\ruta\src",
            r"\\?\UNC\server\share\src",
        )
        for field in fields:
            for path_value in invalid_paths:
                with self.subTest(field=field, path_value=path_value):
                    root = self.create("anchor-" + str(len(list(self.root.iterdir()))))
                    value = self.portable_value(root)
                    value[field] = path_value
                    self.write_portable(root, value)
                    with self.assertRaisesRegex(project.ProjectError, "must be relative"):
                        project.load_project(root)

    def test_drive_relative_paths_are_rejected_even_when_they_resolve_inside_project(self):
        root = self.create()
        portable = self.portable_value(root)
        expected = {
            "sourceDirectory": root / "src",
            "dependencyLock": root / "requirements.lock",
            "plan": root / "plan" / "measurement.json",
        }
        previous = Path.cwd()
        try:
            os.chdir(root)
            self.assertTrue(root.drive)
            for field, target in expected.items():
                path_value = root.drive + str(target.relative_to(root))
                self.assertEqual((root / path_value).resolve(), target)
                value = dict(portable)
                value[field] = path_value
                self.write_portable(root, value)
                with self.subTest(field=field, path_value=path_value):
                    with self.assertRaisesRegex(project.ProjectError, "must be relative"):
                        project.load_project(root)
        finally:
            os.chdir(previous)

    def test_portable_relative_paths_remain_valid_for_all_fields(self):
        root = self.create()
        value = self.portable_value(root)
        value.update(
            {
                "sourceDirectory": "./src",
                "dependencyLock": "./requirements.lock",
                "plan": "plan/../plan/measurement.json",
            }
        )
        self.write_portable(root, value)
        loaded = project.load_project(root)
        self.assertEqual(loaded.configuration.source_directory, root / "src")
        self.assertEqual(loaded.configuration.dependency_lock, root / "requirements.lock")
        self.assertEqual(loaded.configuration.plan, root / "plan" / "measurement.json")

    def test_portable_paths_must_exist_with_expected_kinds(self):
        root = self.create()
        value = self.portable_value(root)
        value["plan"] = "plan/missing.json"
        self.write_portable(root, value)
        with self.assertRaisesRegex(project.ProjectError, "Cannot resolve plan"):
            project.load_project(root)

        value["plan"] = "src"
        self.write_portable(root, value)
        with self.assertRaisesRegex(project.ProjectError, "plan is not a file"):
            project.load_project(root)

    def test_resolution_is_independent_of_current_directory_and_handles_spaces(self):
        root = self.create("project with spaces")
        elsewhere = self.root / "different working directory"
        elsewhere.mkdir()
        previous = Path.cwd()
        try:
            os.chdir(elsewhere)
            loaded = project.load_project(root)
        finally:
            os.chdir(previous)
        self.assertEqual(loaded.configuration.source_directory, root / "src")
        self.assertEqual(loaded.configuration.plan, root / "plan" / "measurement.json")

    def test_valid_local_prerequisites_are_reported_without_using_wheel_filename(self):
        root = self.create("project with spaces")
        self.configure_local(root)
        report = project.check_prerequisites(
            project.load_project(root), lambda unused: self.supported_probe()
        )
        self.assertTrue(report.success)
        self.assertEqual(report.diagnostics, ())
        self.assertEqual(report.sdk_metadata["name"], "artest-python")
        self.assertEqual(report.sdk_metadata["version"], "0.2.0")

    def test_check_requires_local_configuration_but_validate_does_not(self):
        root = self.create("local configuration optional")
        loaded = project.load_project(root)
        self.assertIsNone(loaded.local)
        report = project.check_prerequisites(loaded)
        self.assertEqual(self.diagnostic_codes(report), {"LOCAL_CONFIGURATION_MISSING"})

    def test_python_missing_and_non_file_are_distinct(self):
        for kind, expected in (
            ("missing", "PYTHON_MISSING"),
            ("directory", "PYTHON_NOT_EXECUTABLE"),
        ):
            with self.subTest(kind=kind):
                root = self.create("python-" + kind)
                local = self.configure_local(root)
                python = Path(local["python"])
                python.unlink()
                if kind == "directory":
                    python.mkdir()
                report = project.check_prerequisites(
                    project.load_project(root), lambda unused: self.supported_probe()
                )
                self.assertIn(expected, self.diagnostic_codes(report))

    def test_python_probe_failure_invalid_output_and_timeout_are_distinct(self):
        class FakeProcess:
            def __init__(self, stdout, stderr=b"", return_code=0, timeout=False):
                self.stdout = io.BytesIO(stdout)
                self.stderr = io.BytesIO(stderr)
                self.return_code = return_code
                self.timeout = timeout
                self.killed = False

            def wait(self, timeout=None):
                if self.timeout and timeout is not None and not self.killed:
                    raise project.subprocess.TimeoutExpired("python", timeout)
                return self.return_code

            def kill(self):
                self.killed = True

        cases = (
            (
                FakeProcess(b"", b"probe failed", return_code=7),
                "PYTHON_PROBE_FAILED",
            ),
            (FakeProcess(b"not json"), "PYTHON_PROBE_INVALID_OUTPUT"),
            (
                FakeProcess(b"x" * (project.PYTHON_PROBE_OUTPUT_LIMIT + 1)),
                "PYTHON_PROBE_INVALID_OUTPUT",
            ),
            (FakeProcess(b"", timeout=True), "PYTHON_PROBE_TIMEOUT"),
        )
        for fake, expected in cases:
            with self.subTest(expected=expected):
                with mock.patch.object(project.subprocess, "Popen", return_value=fake):
                    with self.assertRaises(project.InterpreterProbeError) as raised:
                        project._run_python_probe(Path("C:/fake/python.exe"))
                self.assertEqual(raised.exception.code, expected)

        with mock.patch.object(project.subprocess, "Popen", side_effect=OSError("denied")):
            with self.assertRaises(project.InterpreterProbeError) as raised:
                project._run_python_probe(Path("C:/fake/python.exe"))
        self.assertEqual(raised.exception.code, "PYTHON_NOT_EXECUTABLE")

    def test_probe_timeout_with_confirmed_termination_is_bounded(self):
        error = self.exercise_probe_timeout_cleanup(
            kill_fails=False, termination_confirmed=True
        )
        self.assertEqual(error.code, "PYTHON_PROBE_TIMEOUT")
        self.assertNotIn("kill failed", error.cause)

    def test_probe_timeout_kill_failure_with_later_confirmed_exit_is_bounded(self):
        error = self.exercise_probe_timeout_cleanup(
            kill_fails=True, termination_confirmed=True
        )
        self.assertEqual(error.code, "PYTHON_PROBE_TIMEOUT")
        self.assertIn("kill failed", error.cause)
        self.assertIn("process exit was subsequently observed", error.cause)

    def test_probe_timeout_kill_failure_without_confirmed_exit_is_diagnostic(self):
        error = self.exercise_probe_timeout_cleanup(
            kill_fails=True, termination_confirmed=False
        )
        self.assertEqual(error.code, "PYTHON_PROBE_TERMINATION_UNCONFIRMED")
        self.assertIn("kill failed", error.cause)
        self.assertIn("termination was not confirmed", error.cause)

    def test_probe_timeout_kill_success_without_confirmed_exit_is_diagnostic(self):
        error = self.exercise_probe_timeout_cleanup(
            kill_fails=False, termination_confirmed=False
        )
        self.assertEqual(error.code, "PYTHON_PROBE_TERMINATION_UNCONFIRMED")
        self.assertNotIn("kill failed", error.cause)
        self.assertIn("termination was not confirmed", error.cause)

    def test_unconfirmed_probe_termination_becomes_structured_check_failure(self):
        root = self.create("unconfirmed termination diagnostic")
        self.configure_local(root)

        def unconfirmed(unused):
            raise project.InterpreterProbeError(
                "PYTHON_PROBE_TERMINATION_UNCONFIRMED",
                "controlled termination was not confirmed",
            )

        report = project.check_prerequisites(project.load_project(root), unconfirmed)
        diagnostic = next(
            item
            for item in report.diagnostics
            if item.code == "PYTHON_PROBE_TERMINATION_UNCONFIRMED"
        )
        self.assertFalse(report.success)
        self.assertEqual(diagnostic.operation, "check")
        self.assertEqual(diagnostic.stage, "interpreter")
        self.assertIn("may still be running", diagnostic.correction)

    def test_python_probe_uses_isolation_separate_arguments_and_bounded_pipes(self):
        payload = json.dumps(self.supported_probe()).encode("utf-8")

        class FakeProcess:
            stdout = io.BytesIO(payload)
            stderr = io.BytesIO()

            def wait(self, timeout=None):
                return 0

        with mock.patch.object(project.subprocess, "Popen", return_value=FakeProcess()) as popen:
            python = Path("C:/Python 313/python.exe")
            observed = project._run_python_probe(python)
        command = popen.call_args.args[0]
        self.assertEqual(command[:5], [str(python), "-I", "-B", "-S", "-c"])
        self.assertFalse(popen.call_args.kwargs["shell"])
        self.assertEqual(observed, self.supported_probe())

    def test_incompatible_python_identity_fields_have_specific_diagnostics(self):
        root = self.create("python-identity")
        self.configure_local(root)
        cases = (
            ({"implementation": "pypy"}, "PYTHON_IMPLEMENTATION_INCOMPATIBLE"),
            ({"version": [3, 12, 14]}, "PYTHON_VERSION_INCOMPATIBLE"),
            ({"platform": "linux"}, "PYTHON_PLATFORM_INCOMPATIBLE"),
            ({"machine": "ARM64"}, "PYTHON_ARCHITECTURE_INCOMPATIBLE"),
            ({"pointerBits": 32}, "PYTHON_ARCHITECTURE_INCOMPATIBLE"),
            ({"gilEnabled": False}, "PYTHON_GIL_INCOMPATIBLE"),
        )
        for override, expected in cases:
            with self.subTest(override=override):
                report = project.check_prerequisites(
                    project.load_project(root),
                    lambda unused, override=override: self.supported_probe(**override),
                )
                self.assertIn(expected, self.diagnostic_codes(report))

    def test_sdk_missing_invalid_and_incompatible_metadata_are_distinct(self):
        cases = (
            ("missing", "SDK_MISSING"),
            ("invalid", "SDK_INVALID"),
            ("version", "SDK_INCOMPATIBLE"),
            ("python", "SDK_INCOMPATIBLE"),
            ("dependencies", "SDK_INCOMPATIBLE"),
        )
        for kind, expected in cases:
            with self.subTest(kind=kind):
                root = self.create("sdk-" + kind)
                local = self.configure_local(root)
                wheel = Path(local["sdkWheel"])
                if kind == "missing":
                    wheel.unlink()
                elif kind == "invalid":
                    wheel.write_bytes(b"not a wheel")
                elif kind == "version":
                    self.write_wheel(wheel, version="0.1.0")
                elif kind == "python":
                    self.write_wheel(wheel, requires_python=">=3.12")
                elif kind == "dependencies":
                    self.write_wheel(wheel, requirements=("protobuf==6.33.4",))
                report = project.check_prerequisites(
                    project.load_project(root), lambda unused: self.supported_probe()
                )
                self.assertIn(expected, self.diagnostic_codes(report))

    def test_cli_missing_invalid_and_wrong_architecture_are_distinct(self):
        cases = (
            ("missing", "CLI_MISSING"),
            ("invalid", "CLI_INVALID"),
            ("x86", "CLI_ARCHITECTURE_INCOMPATIBLE"),
        )
        for kind, expected in cases:
            with self.subTest(kind=kind):
                root = self.create("cli-" + kind)
                local = self.configure_local(root)
                cli = Path(local["cliExecutable"])
                if kind == "missing":
                    cli.unlink()
                elif kind == "invalid":
                    cli.write_bytes(b"not PE")
                else:
                    self.write_pe(cli, machine=0x014C)
                report = project.check_prerequisites(
                    project.load_project(root), lambda unused: self.supported_probe()
                )
                self.assertIn(expected, self.diagnostic_codes(report))

    def test_declared_local_dependency_missing_is_diagnosed(self):
        root = self.create("missing-local-dependency")
        missing = root / "vendor files" / "calibration.json"
        self.configure_local(root, vendor_paths={"calibration": str(missing)})
        report = project.check_prerequisites(
            project.load_project(root), lambda unused: self.supported_probe()
        )
        self.assertIn("LOCAL_DEPENDENCY_MISSING", self.diagnostic_codes(report))
        diagnostic = next(
            item for item in report.diagnostics if item.code == "LOCAL_DEPENDENCY_MISSING"
        )
        self.assertEqual(diagnostic.field, "vendorPaths.calibration")
        self.assertTrue(diagnostic.expected)
        self.assertTrue(diagnostic.cause)
        self.assertTrue(diagnostic.correction)

    def test_vendor_dll_missing_invalid_pe_and_wrong_architecture_are_distinct(self):
        cases = (
            ("missing", "VENDOR_DLL_MISSING"),
            ("invalid", "VENDOR_DLL_INVALID_PE"),
            ("x86", "VENDOR_DLL_ARCHITECTURE_INCOMPATIBLE"),
        )
        for kind, expected in cases:
            with self.subTest(kind=kind):
                root = self.create("vendor-dll-" + kind)
                dll = root / "vendor files" / "vendor.dll"
                if kind == "invalid":
                    dll.parent.mkdir()
                    dll.write_bytes(b"not PE")
                elif kind == "x86":
                    self.write_pe(dll, machine=0x014C)
                self.configure_local(
                    root,
                    vendor_paths={"vendorSdk": str(dll)},
                    vendor_dlls=["vendorSdk"],
                )
                report = project.check_prerequisites(
                    project.load_project(root), lambda unused: self.supported_probe()
                )
                self.assertIn(expected, self.diagnostic_codes(report))

    def test_check_resolves_local_paths_from_project_root_in_another_cwd(self):
        root = self.create("local project with spaces")
        self.configure_local(root)
        value = json.loads((root / project.LOCAL_CONFIG_NAME).read_text(encoding="utf-8"))
        for field in ("python", "sdkWheel", "cliExecutable"):
            value[field] = str(Path(value[field]).relative_to(root))
        self.write_local(root, value)
        elsewhere = self.root / "another cwd"
        elsewhere.mkdir()
        previous = Path.cwd()
        try:
            os.chdir(elsewhere)
            report = project.check_prerequisites(
                project.load_project(root), lambda unused: self.supported_probe()
            )
        finally:
            os.chdir(previous)
        self.assertTrue(report.success)

    def test_valid_binding_materializes_local_copy_and_preserves_source_plan(self):
        root = self.create("binding project")
        portable_path = root / "plan" / "measurement.json"
        plan = json.loads(portable_path.read_text(encoding="utf-8"))
        plan["instruments"][0]["config"]["vendorDll"] = None
        portable_path.write_text(json.dumps(plan, indent=2) + "\n", encoding="utf-8")
        vendor = self.write_pe(root / "vendor files" / "vendor sdk.dll")
        self.configure_local(
            root,
            vendor_paths={"vendorSdk": str(vendor)},
            vendor_dlls=["vendorSdk"],
            plan_bindings=[
                {
                    "vendorPath": "vendorSdk",
                    "instrumentId": "SimulatedSource1",
                    "configField": "vendorDll",
                }
            ],
        )
        before = portable_path.read_bytes()
        loaded = project.load_project(root)
        report = project.check_prerequisites(loaded, lambda unused: self.supported_probe())
        self.assertTrue(report.success)
        generated_path = project.materialize_local_plan(loaded)
        generated = json.loads(generated_path.read_text(encoding="utf-8"))
        self.assertEqual(generated["instruments"][0]["config"]["vendorDll"], str(vendor))
        self.assertEqual(portable_path.read_bytes(), before)
        self.assertEqual(generated_path, root / project.GENERATED_PLAN)

    def test_invalid_binding_target_is_diagnosed_and_does_not_write(self):
        root = self.create("invalid binding")
        vendor = root / "vendor files" / "calibration.json"
        vendor.parent.mkdir()
        vendor.write_text("{}", encoding="utf-8")
        self.configure_local(
            root,
            vendor_paths={"calibration": str(vendor)},
            plan_bindings=[
                {
                    "vendorPath": "calibration",
                    "instrumentId": "MissingInstrument",
                    "configField": "calibrationPath",
                }
            ],
        )
        loaded = project.load_project(root)
        report = project.check_prerequisites(loaded, lambda unused: self.supported_probe())
        self.assertIn("PLAN_BINDING_INVALID", self.diagnostic_codes(report))
        with self.assertRaisesRegex(project.ProjectError, "local plan binding"):
            project.materialize_local_plan(loaded)
        self.assertFalse((root / project.GENERATED_PLAN).exists())

    def test_generated_plan_collision_preserves_existing_file(self):
        root = self.create("binding collision")
        portable_path = root / "plan" / "measurement.json"
        plan = json.loads(portable_path.read_text(encoding="utf-8"))
        plan["instruments"][0]["config"]["vendorPath"] = "portable-placeholder"
        portable_path.write_text(json.dumps(plan), encoding="utf-8")
        vendor = root / "vendor" / "data"
        vendor.mkdir(parents=True)
        self.configure_local(
            root,
            vendor_paths={"vendorData": str(vendor)},
            plan_bindings=[
                {
                    "vendorPath": "vendorData",
                    "instrumentId": "SimulatedSource1",
                    "configField": "vendorPath",
                }
            ],
        )
        output = root / project.GENERATED_PLAN
        output.parent.mkdir(parents=True)
        output.write_bytes(b"owner data")
        with self.assertRaisesRegex(project.ProjectError, "will not be overwritten"):
            project.materialize_local_plan(project.load_project(root))
        self.assertEqual(output.read_bytes(), b"owner data")

    def test_generated_plan_reparse_guard_precedes_destination_write(self):
        root = self.create("binding reparse guard")
        portable_path = root / "plan" / "measurement.json"
        plan = json.loads(portable_path.read_text(encoding="utf-8"))
        plan["instruments"][0]["config"]["vendorPath"] = None
        portable_path.write_text(json.dumps(plan), encoding="utf-8")
        vendor = root / "vendor"
        vendor.mkdir()
        self.configure_local(
            root,
            vendor_paths={"vendor": str(vendor)},
            plan_bindings=[
                {
                    "vendorPath": "vendor",
                    "instrumentId": "SimulatedSource1",
                    "configField": "vendorPath",
                }
            ],
        )
        generated_root = root / ".artest"
        generated_root.mkdir()
        original_check = project._is_reparse_point

        def mark_generated_root(path):
            return Path(path) == generated_root or original_check(path)

        with mock.patch.object(project, "_is_reparse_point", side_effect=mark_generated_root):
            with self.assertRaisesRegex(project.ProjectError, "Reparse-point traversal"):
                project.materialize_local_plan(project.load_project(root))
        self.assertFalse((generated_root / "stage2").exists())

    def test_create_validate_and_materialize_do_not_launch_subprocesses(self):
        root = self.create("no subprocess")
        portable_path = root / "plan" / "measurement.json"
        plan = json.loads(portable_path.read_text(encoding="utf-8"))
        plan["instruments"][0]["config"]["vendorPath"] = None
        portable_path.write_text(json.dumps(plan), encoding="utf-8")
        vendor = root / "vendor"
        vendor.mkdir()
        self.configure_local(
            root,
            vendor_paths={"vendor": str(vendor)},
            plan_bindings=[
                {
                    "vendorPath": "vendor",
                    "instrumentId": "SimulatedSource1",
                    "configField": "vendorPath",
                }
            ],
        )
        before_modules = set(sys.modules)
        with mock.patch.object(project.subprocess, "Popen") as popen:
            loaded = project.load_project(root)
            project.materialize_local_plan(loaded)
        popen.assert_not_called()
        self.assertNotIn("artest_sdk", set(sys.modules) - before_modules)

    def test_check_result_is_machine_readable_and_uses_failure_exit_code(self):
        root = self.create("check exit")
        self.configure_local(root)
        Path(json.loads((root / project.LOCAL_CONFIG_NAME).read_text())["sdkWheel"]).unlink()
        output = io.StringIO()
        with mock.patch.object(project, "_run_python_probe", return_value=self.supported_probe()):
            with redirect_stdout(output):
                exit_code = project.main(["check", str(root)])
        result = json.loads(output.getvalue())
        self.assertEqual(exit_code, 1)
        self.assertFalse(result["success"])
        self.assertIn("SDK_MISSING", {item["code"] for item in result["diagnostics"]})
        for diagnostic in result["diagnostics"]:
            self.assertEqual(diagnostic["operation"], "check")
            self.assertTrue(diagnostic["stage"])
            self.assertTrue(diagnostic["field"])
            self.assertTrue(diagnostic["expected"])
            self.assertTrue(diagnostic["cause"])
            self.assertTrue(diagnostic["correction"])

    def test_real_supported_interpreter_probe_smoke(self):
        is_supported = (
            sys.implementation.name == "cpython"
            and sys.version_info[:2] == (3, 13)
            and sys.platform == "win32"
            and sys.maxsize == 2**63 - 1
            and getattr(sys, "_is_gil_enabled", lambda: False)()
        )
        if not is_supported:
            self.skipTest("Test runner is not supported standard CPython 3.13 Windows x64")
        root = self.create("real interpreter smoke")
        local = self.configure_local(root)
        local["python"] = sys.executable
        self.write_local(root, local)
        report = project.check_prerequisites(project.load_project(root))
        self.assertTrue(report.success, report.as_dict())

    def test_nonempty_destination_collision_preserves_all_original_files(self):
        destination = self.root / "existing"
        destination.mkdir()
        original = destination / "keep.txt"
        original.write_bytes(b"owner data\x00")
        before = {path.relative_to(destination): path.read_bytes() for path in destination.rglob("*")}
        with self.assertRaisesRegex(project.ProjectError, "not empty"):
            project.create_project(
                destination,
                extension_id=self.EXTENSION_ID,
                driver_id=self.DRIVER_ID,
                command_id=self.COMMAND_ID,
                author=self.AUTHOR,
            )
        after = {path.relative_to(destination): path.read_bytes() for path in destination.rglob("*")}
        self.assertEqual(after, before)

    def test_existing_empty_destination_is_supported(self):
        destination = self.root / "empty"
        destination.mkdir()
        self.assertEqual(
            project.create_project(
                destination,
                extension_id=self.EXTENSION_ID,
                driver_id=self.DRIVER_ID,
                command_id=self.COMMAND_ID,
                author=self.AUTHOR,
            ),
            destination,
        )
        self.assertTrue((destination / project.CONFIG_NAME).is_file())

    def test_author_must_choose_valid_distinct_ids_before_any_write(self):
        cases = (
            ({"extension_id": "Example.Extension"}, "lower-case stable identifier"),
            ({"driver_id": "driver"}, "lower-case stable identifier"),
            ({"command_id": "com.example.bad_value"}, "lower-case stable identifier"),
            ({"driver_id": self.EXTENSION_ID}, "must be distinct"),
            ({"author": "  "}, "nonempty string"),
        )
        for index, (override, message) in enumerate(cases):
            destination = self.root / f"invalid-identity-{index}"
            values = {
                "extension_id": self.EXTENSION_ID,
                "driver_id": self.DRIVER_ID,
                "command_id": self.COMMAND_ID,
                "author": self.AUTHOR,
            }
            values.update(override)
            with self.subTest(override=override):
                with self.assertRaisesRegex(project.ProjectError, message):
                    project.create_project(destination, **values)
                self.assertFalse(destination.exists())

    def test_generated_python_parses_without_importing_sdk_or_extension(self):
        before_modules = set(sys.modules)
        root = self.create()
        project.load_project(root)
        source = (root / "src" / "extension.py").read_text(encoding="utf-8")
        tree = ast.parse(source, filename=str(root / "src" / "extension.py"))
        self.assertIsInstance(tree, ast.Module)
        self.assertNotIn("artest_sdk", set(sys.modules) - before_modules)

    def test_template_ids_are_consistent_between_definition_and_plan(self):
        root = self.create()
        source_path = root / "src" / "extension.py"
        source = source_path.read_text(encoding="utf-8")
        tree = ast.parse(source, filename=str(source_path))
        constants = {}
        for statement in tree.body:
            if isinstance(statement, ast.Assign) and len(statement.targets) == 1:
                target = statement.targets[0]
                if isinstance(target, ast.Name) and isinstance(statement.value, ast.Constant):
                    constants[target.id] = statement.value.value
        plan = json.loads((root / "plan" / "measurement.json").read_text(encoding="utf-8"))
        self.assertEqual(plan["instruments"][0]["type"], constants["DRIVER_ID"])
        self.assertEqual(plan["commands"][0]["name"], constants["COMMAND_ID"])
        self.assertEqual(constants["EXTENSION_ID"], self.EXTENSION_ID)
        self.assertEqual(constants["DRIVER_ID"], self.DRIVER_ID)
        self.assertEqual(constants["COMMAND_ID"], self.COMMAND_ID)
        self.assertEqual(constants["CONTRACT"], self.EXTENSION_ID + ".contract.simulated-source.v1")
        self.assertIn("Extension(EXTENSION_ID", source)
        self.assertIn("extension.driver(\n        DRIVER_ID", source)
        self.assertIn("extension.command(\n        COMMAND_ID", source)
        self.assertIn("context.instrument(CONTRACT)", source)
        self.assertIn("source.invoke(READ_OPERATION", source)
        self.assertIn(json.dumps(self.AUTHOR), source)

    def test_reparse_parent_is_rejected_without_writing_through_it(self):
        outside = self.root / "outside"
        outside.mkdir()
        link = self.root / "linked-parent"
        try:
            link.symlink_to(outside, target_is_directory=True)
        except OSError as error:
            self.skipTest(f"Directory symlinks are unavailable: {error}")
        with self.assertRaisesRegex(project.ProjectError, "Reparse-point traversal"):
            project.create_project(
                link / "project",
                extension_id=self.EXTENSION_ID,
                driver_id=self.DRIVER_ID,
                command_id=self.COMMAND_ID,
                author=self.AUTHOR,
            )
        self.assertFalse((outside / "project").exists())

    def test_reparse_guard_rejects_creation_before_destination_write(self):
        destination = self.root / "guarded" / "project"
        destination.parent.mkdir()
        original_check = project._is_reparse_point

        def mark_parent_as_reparse(path):
            return Path(path).absolute() == destination.parent.absolute() or original_check(path)

        with mock.patch.object(project, "_is_reparse_point", side_effect=mark_parent_as_reparse):
            with self.assertRaisesRegex(project.ProjectError, "Reparse-point traversal"):
                project.create_project(
                    destination,
                    extension_id=self.EXTENSION_ID,
                    driver_id=self.DRIVER_ID,
                    command_id=self.COMMAND_ID,
                    author=self.AUTHOR,
                )
        self.assertFalse(destination.exists())

    def test_portable_symlink_escape_is_rejected_when_supported(self):
        root = self.create()
        outside = self.root / "outside-source"
        outside.mkdir()
        link = root / "linked-source"
        try:
            link.symlink_to(outside, target_is_directory=True)
        except OSError as error:
            self.skipTest(f"Directory symlinks are unavailable: {error}")
        value = self.portable_value(root)
        value["sourceDirectory"] = "linked-source"
        self.write_portable(root, value)
        with self.assertRaisesRegex(project.ProjectError, "escapes the project"):
            project.load_project(root)


if __name__ == "__main__":
    unittest.main()
