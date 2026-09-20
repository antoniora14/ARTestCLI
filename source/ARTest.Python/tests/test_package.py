import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


TOOL = Path(__file__).resolve().parents[1] / "tools" / "package.py"
SPEC = importlib.util.spec_from_file_location("artest_package_tool", TOOL)
package = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(package)


class OfflineWheelhouseTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.sdk = self.root / "artest_python-0.2.0-py3-none-any.whl"
        self.dependency = self.root / "protobuf-6.33.4-cp39-abi3-win_amd64.whl"
        self.sdk.write_bytes(b"sdk")
        self.dependency.write_bytes(b"dependency")
        self.write_manifest()

    def tearDown(self):
        self.temporary.cleanup()

    @staticmethod
    def sha256(path):
        return hashlib.sha256(path.read_bytes()).hexdigest()

    def write_manifest(self, files=None):
        if files is None:
            files = [
                {"path": self.sdk.name, "sha256": self.sha256(self.sdk)},
                {"path": self.dependency.name, "sha256": self.sha256(self.dependency)},
            ]
        (self.root / package.OFFLINE_WHEELHOUSE_NAME).write_text(
            json.dumps({"schema": package.OFFLINE_WHEELHOUSE_SCHEMA, "files": files}),
            encoding="utf-8",
        )

    def test_verified_adjacent_wheelhouse_is_selected(self):
        self.assertEqual(package.offline_wheelhouse(self.sdk), self.root)

    def test_missing_manifest_preserves_legacy_explicit_flow(self):
        (self.root / package.OFFLINE_WHEELHOUSE_NAME).unlink()
        self.assertIsNone(package.offline_wheelhouse(self.sdk))

    def test_corrupt_wheel_is_rejected(self):
        self.dependency.write_bytes(b"changed")
        with self.assertRaisesRegex(ValueError, "checksum mismatch"):
            package.offline_wheelhouse(self.sdk)

    def test_uninventoried_wheel_is_rejected(self):
        (self.root / "unexpected-1.0-py3-none-any.whl").write_bytes(b"unexpected")
        with self.assertRaisesRegex(ValueError, "full file inventory mismatch"):
            package.offline_wheelhouse(self.sdk)

    def test_unsafe_or_duplicate_paths_are_rejected(self):
        cases = [
            [{"path": "../escape.whl", "sha256": "0" * 64}],
            [
                {"path": self.sdk.name, "sha256": self.sha256(self.sdk)},
                {"path": self.sdk.name.upper(), "sha256": self.sha256(self.sdk)},
            ],
        ]
        for files in cases:
            with self.subTest(files=files):
                self.write_manifest(files)
                with self.assertRaises(ValueError):
                    package.offline_wheelhouse(self.sdk)


if __name__ == "__main__":
    unittest.main()
