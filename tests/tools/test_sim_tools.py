import json
import sys
import tempfile
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[2] / "tools"
sys.path.insert(0, str(TOOLS))

import sim_artifacts  # noqa: E402
import run_wokwi  # noqa: E402


class ArtifactAttestationTests(unittest.TestCase):
    def make_repo(self, root: Path) -> None:
        (root / "simulation").mkdir(exist_ok=True)
        (root / "VictronCYD_Modbus").mkdir(exist_ok=True)
        (root / "build" / "simulation").mkdir(parents=True, exist_ok=True)
        (root / "simulation" / "source-allowlist.txt").write_text(
            "VictronCYD_Modbus/App.ino\nVictronCYD_Modbus/App.h\n",
            encoding="utf-8",
        )
        (root / "VictronCYD_Modbus" / "App.ino").write_text(
            '#include "App.h"\n', encoding="utf-8"
        )
        (root / "VictronCYD_Modbus" / "App.h").write_text(
            "#pragma once\n", encoding="utf-8"
        )
        (root / "build" / "simulation" / "firmware.bin").write_bytes(b"bin")
        (root / "build" / "simulation" / "firmware.elf").write_bytes(b"elf")

    def test_attestation_accepts_current_artifacts(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.make_repo(root)
            attestation = sim_artifacts.create_attestation(root)
            self.assertEqual(attestation["schema"], 1)
            sim_artifacts.verify_attestation(root)

    def test_attestation_rejects_stale_source_and_tampered_artifact(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.make_repo(root)
            sim_artifacts.create_attestation(root)
            (root / "VictronCYD_Modbus" / "App.h").write_text(
                "#pragma once\n// changed\n", encoding="utf-8"
            )
            with self.assertRaisesRegex(ValueError, "source hash"):
                sim_artifacts.verify_attestation(root)

            self.make_repo(root)
            sim_artifacts.create_attestation(root)
            (root / "build" / "simulation" / "firmware.bin").write_bytes(b"tampered")
            with self.assertRaisesRegex(ValueError, "artifact hash"):
                sim_artifacts.verify_attestation(root)

    def test_attestation_rejects_artifact_outside_simulator_directory(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.make_repo(root)
            sim_artifacts.create_attestation(root)
            path = root / "build" / "simulation" / "attestation.json"
            data = json.loads(path.read_text(encoding="utf-8"))
            data["artifacts"][0]["path"] = "build/production.bin"
            path.write_text(json.dumps(data), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "outside build/simulation"):
                sim_artifacts.verify_attestation(root)

    def test_allowlist_rejects_secrets_and_path_escape(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.make_repo(root)
            allowlist = root / "simulation" / "source-allowlist.txt"
            allowlist.write_text("VictronCYD_Modbus/secrets.h\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "secret"):
                sim_artifacts.load_allowlist(root)
            allowlist.write_text("../outside.cpp\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "unsafe"):
                sim_artifacts.load_allowlist(root)

    def test_simulator_stage_has_no_secrets_file(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.make_repo(root)
            stage = sim_artifacts.stage_sources(root, "simulation")
            self.assertFalse(any(path.name.lower() == "secrets.h" for path in stage.rglob("*")))
            production_stage = sim_artifacts.stage_sources(root, "production")
            secret_files = [
                path for path in production_stage.rglob("*") if path.name.lower() == "secrets.h"
            ]
            self.assertEqual(len(secret_files), 1)
            self.assertIn("dummy-ap-pass-123", secret_files[0].read_text(encoding="utf-8"))


class PixelComparisonTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        global Image, compare_images
        from PIL import Image
        import compare_images

    def test_exact_match_and_one_pixel_failure(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            expected = root / "expected.png"
            actual = root / "actual.png"
            failures = root / "failures"
            baseline = Image.new("RGB", (320, 240), (10, 20, 30))
            baseline.save(expected)
            baseline.save(actual)
            self.assertTrue(compare_images.compare(expected, actual, failures))
            self.assertFalse(failures.exists())

            changed = baseline.copy()
            changed.putpixel((19, 23), (11, 20, 30))
            changed.save(actual)
            self.assertFalse(compare_images.compare(expected, actual, failures))
            self.assertTrue((failures / "expected.png").is_file())
            self.assertTrue((failures / "actual.png").is_file())
            diff = Image.open(failures / "diff.png")
            self.assertEqual(diff.getpixel((19, 23)), (255, 0, 255))

    def test_wrong_dimensions_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            expected = root / "expected.png"
            actual = root / "actual.png"
            Image.new("RGB", (320, 240)).save(expected)
            Image.new("RGB", (240, 320)).save(actual)
            with self.assertRaisesRegex(ValueError, "320x240"):
                compare_images.compare(expected, actual, root / "failures")

    def test_new_golden_still_validates_actual_dimensions(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            actual = root / "build" / "simulation" / "results" / "sample" / "screen.png"
            actual.parent.mkdir(parents=True)
            Image.new("RGB", (240, 320)).save(actual)
            scenario = {"name": "sample", "screenshots": ["screen"]}
            with self.assertRaisesRegex(ValueError, "320x240"):
                run_wokwi.compare_scenario(root, scenario)


if __name__ == "__main__":
    unittest.main()
