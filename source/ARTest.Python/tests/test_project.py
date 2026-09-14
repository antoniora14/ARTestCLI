import ast
from contextlib import redirect_stdout
import io
import json
import os
from pathlib import Path
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
        self.assertEqual(command[:4], [str(python), "-I", "-S", "-c"])
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
