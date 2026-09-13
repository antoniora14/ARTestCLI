import ast
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock


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
        )
        for value, message in cases:
            with self.subTest(value=value):
                (root / project.LOCAL_CONFIG_NAME).write_text(
                    json.dumps(value), encoding="utf-8"
                )
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
