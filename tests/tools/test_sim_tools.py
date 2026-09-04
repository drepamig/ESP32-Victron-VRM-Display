import json
import re
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
        sim_artifacts.stage_sources(root, "simulation")

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

    def test_source_change_during_build_cannot_receive_fresh_attestation(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.make_repo(root)
            sim_artifacts.stage_sources(root, "simulation")
            (root / "VictronCYD_Modbus" / "App.h").write_text(
                "#pragma once\n// edit after staging\n", encoding="utf-8"
            )
            with self.assertRaisesRegex(ValueError, "changed since staging"):
                sim_artifacts.create_attestation(root)

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

    def test_runner_rejects_production_path_in_wokwi_config(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.make_repo(root)
            sim_artifacts.create_attestation(root)
            config = root / "simulation" / "wokwi.toml"
            config.write_text(
                '[wokwi]\nversion = 1\n'
                'firmware = "../build/simulation/firmware.bin"\n'
                'elf = "../build/simulation/firmware.elf"\n', encoding="utf-8"
            )
            run_wokwi.verify_launch_artifacts(root)
            config.write_text(
                '[wokwi]\nversion = 1\n'
                'firmware = "../build/firmware/production.bin"\n'
                'elf = "../build/simulation/firmware.elf"\n', encoding="utf-8"
            )
            with self.assertRaisesRegex(ValueError, "unattested firmware"):
                run_wokwi.verify_launch_artifacts(root)


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
            baseline = Image.new("RGBA", (320, 240), (10, 20, 30, 255))
            baseline.save(expected)
            baseline.save(actual)
            self.assertTrue(compare_images.compare(expected, actual, failures))
            self.assertFalse(failures.exists())

            changed = baseline.copy()
            changed.putpixel((19, 23), (11, 20, 30, 255))
            changed.save(actual)
            self.assertFalse(compare_images.compare(expected, actual, failures))
            self.assertTrue((failures / "expected.png").is_file())
            self.assertTrue((failures / "actual.png").is_file())
            diff = Image.open(failures / "diff.png")
            self.assertEqual(diff.getpixel((19, 23)), (255, 0, 255, 255))

    def test_wrong_dimensions_are_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            expected = root / "expected.png"
            actual = root / "actual.png"
            Image.new("RGBA", (320, 240)).save(expected)
            Image.new("RGBA", (240, 320)).save(actual)
            with self.assertRaisesRegex(ValueError, "320x240"):
                compare_images.compare(expected, actual, root / "failures")

    def test_new_golden_still_validates_actual_dimensions(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            actual = root / "build" / "simulation" / "results" / "sample" / "screen.png"
            actual.parent.mkdir(parents=True)
            Image.new("RGBA", (240, 320)).save(actual)
            scenario = {"name": "sample", "screenshots": ["screen"]}
            with self.assertRaisesRegex(ValueError, "320x240"):
                run_wokwi.compare_scenario(root, scenario)


class ScenarioPathTests(unittest.TestCase):
    def test_wokwi_manifest_reader_excludes_local_reboot_scenario(self):
        repo = TOOLS.parent
        self.assertNotIn('reboot-persistence', [s['name'] for s in run_wokwi.load_scenarios(repo, None)])
        with self.assertRaisesRegex(ValueError, 'unknown|unsupported'):
            run_wokwi.load_scenarios(repo, 'reboot-persistence')

    def test_firmware_crash_is_rejected_even_when_scenario_exits_successfully(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / "serial.log"
            log.write_text("SIM READY\nassert failed: queue.c:1709\nRebooting...\n", encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "firmware crash"):
                run_wokwi.validate_serial_log(log)
            log.write_text("SIM READY\n[MB] result=valid\n", encoding="utf-8")
            run_wokwi.validate_serial_log(log)

    def test_screenshot_paths_resolve_to_ignored_results_directory(self) -> None:
        repo = TOOLS.parent
        expected_root = (repo / "build" / "simulation" / "results").resolve()
        scenario_files = sorted((repo / "simulation" / "scenarios").glob("*.yaml"))
        self.assertTrue(scenario_files)
        for scenario_file in scenario_files:
            for match in re.finditer(
                r"^\s*save-to:\s*['\"]([^'\"]+)['\"]\s*$",
                scenario_file.read_text(encoding="utf-8"),
                re.MULTILINE,
            ):
                resolved = (scenario_file.parent / match.group(1)).resolve()
                try:
                    resolved.relative_to(expected_root)
                except ValueError:
                    self.fail(f"unsafe screenshot path in {scenario_file}: {match.group(1)}")


if __name__ == "__main__":
    unittest.main()
