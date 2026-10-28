import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]


def module(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / "tools" / f"{name}.py")
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


project = module("project")
bundle = module("flash_bundle")


class ToolsTests(unittest.TestCase):
    def test_no_erase_without_explicit_confirmation(self):
        with patch("sys.argv", ["project.py", "erase"]), patch.object(project, "idf") as run:
            with self.assertRaisesRegex(RuntimeError, "confirm-erase"):
                project.main()
            run.assert_not_called()

    def test_multiple_ports_are_not_guessed(self):
        env = {"IDF_PYTHON_ENV_PATH": "/example"}
        with patch.object(project.subprocess, "check_output", return_value='["COM3", "COM5"]'):
            with self.assertRaisesRegex(RuntimeError, "--port"):
                project.choose_port(None, env)
        self.assertEqual(project.choose_port("COM5", env), "COM5")

    def test_wrong_sdk_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)
            (path / "tools").mkdir()
            (path / "tools/idf.py").touch()
            with patch.object(project.subprocess, "check_output", return_value="wrong-commit\n"):
                with self.assertRaisesRegex(RuntimeError, "v5.5.3"):
                    project.check_idf(path)

    def test_windows_export_expands_path_without_a_shell(self):
        with patch.object(project, "check_idf"), patch.dict(project.os.environ, {"PATH": "original", "IDF_PYTHON_ENV_PATH": "/venv"}, clear=True):
            with patch.object(project.subprocess, "check_output", return_value="PATH=C:\\tools;%PATH%\nESP_IDF_VERSION=5.5.3\n"):
                _, env = project.idf_environment()
                self.assertEqual(env["PATH"], "C:\\tools;original")

    def test_package_contains_offsets_and_verifiable_images(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            build = root / "build"
            build.mkdir()
            (build / "firmware.bin").write_bytes(b"firmware-test-bytes")
            (build / "flasher_args.json").write_text(json.dumps({"flash_files": {"0x20000": "firmware.bin"},
                                                               "flash_settings": {"flash_size": "8MB"}}))
            (build / "project_description.json").write_text('{"project_version":"0.1.0"}')
            result = project.package(build, root / "dist")
            manifest = json.loads((result / "manifest.json").read_text())
            self.assertEqual(manifest["protocol_status"], "upstream-implemented-hardware-unverified")
            args = bundle.image_arguments(result, manifest)
            self.assertEqual(args[0], "0x20000")
            self.assertEqual(Path(args[1]).read_bytes(), b"firmware-test-bytes")
            Path(args[1]).write_bytes(b"corrupted")
            with self.assertRaisesRegex(ValueError, "校验失败"):
                bundle.image_arguments(result, manifest)

    def test_bundle_rejects_path_escape(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            manifest = {"files": {"0": {"file": "../outside.bin", "sha256": "unused"}}}
            with self.assertRaisesRegex(ValueError, "越界"):
                bundle.image_arguments(root, manifest)

    def test_font_sources_cover_all_ui_non_ascii_characters(self):
        source = (ROOT / "components/ui/include/rc/strings.hpp").read_text(encoding="utf-8")
        # lv_font_conv emits a glyph comment for every included codepoint.
        for size in (20, 24):
            generated = (ROOT / f"components/ui/src/font_{size}.c").read_text(encoding="utf-8")
            for char in set(source):
                if ord(char) > 127:
                    self.assertIn(f"U+{ord(char):04X}", generated, (size, char))


if __name__ == "__main__":
    unittest.main()
