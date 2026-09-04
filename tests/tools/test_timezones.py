"""Offline checks for the country-local timezone catalog and its pinned inputs."""

import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SOURCES = ROOT / "tools" / "timezones"
GENERATOR = ROOT / "tools" / "generate_timezones.py"
CATALOG = ROOT / "VictronCYD_Modbus" / "TimeZoneCatalog.inc"
ORACLE = ROOT / "tests/host/fixtures/timezone_oracle.tsv"
PINNED = {
    "tzdata2026c.tar.gz": "e4a178a4477f3d0ea77cc31828ff72aa38feff8d61aa13e7e99e142e9d902be4",
    "tzcode2026c.tar.gz": "b1cffc3ace4c4c7cd0efba2f7add86ec3d0b79da48bcf03582671fd3c8feace8",
}


def entries():
    return [json.loads("[" + row + "]") for row in
            re.findall(r'^  \{(.+)\},$', CATALOG.read_text(encoding="utf-8"), re.M)]


class TimezoneCatalogTests(unittest.TestCase):
    def test_sources_are_pinned_and_identify_release(self):
        for name, digest in PINNED.items():
            with self.subTest(archive=name):
                path = SOURCES / name
                self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), digest)
                with tarfile.open(path) as archive:
                    self.assertEqual(archive.extractfile("version").read().strip(), b"2026c")

    def test_catalog_retains_every_country_local_name_including_links(self):
        self.assertTrue(CATALOG.is_file(), "generated firmware catalog is missing")
        with tarfile.open(SOURCES / "tzdata2026c.tar.gz") as archive:
            source = archive.extractfile("zone.tab").read().decode("ascii")
        expected = {"UTC": "UTC"}
        for line in source.splitlines():
            if not line or line.startswith("#"):
                continue
            country, _, zone, *_ = line.split("\t")
            if country in {"US", "CA", "MX"}:
                expected[zone] = country
        actual = entries()
        self.assertEqual({zone: country for zone, country, _, _ in actual}, expected)
        self.assertEqual(len(actual), len(expected), "duplicate selectable zone IDs")
        self.assertEqual(expected["America/Atikokan"], "CA")
        self.assertEqual(expected["America/Creston"], "CA")

    def test_oracle_covers_every_zone_and_specific_regional_changes(self):
        self.assertTrue(ORACLE.is_file(), "pinned TZif oracle is missing")
        cases = {}
        for line in ORACLE.read_text(encoding="utf-8").splitlines():
            if not line or line.startswith("#"):
                continue
            zone, epoch, offset = line.split("\t")
            cases.setdefault(zone, {})[int(epoch)] = int(offset)
        self.assertEqual(set(cases), {row[0] for row in entries()})
        for zone, values in cases.items():
            self.assertGreater(len(values), 20, zone)
        for zone, offset in {"America/Edmonton": -21600, "America/Vancouver": -25200,
                             "America/Mexico_City": -21600, "UTC": 0}.items():
            self.assertEqual(set(cases[zone].values()), {offset}, zone)
        self.assertEqual(set(cases["America/St_Johns"].values()), {-12600, -9000})
        self.assertEqual(set(cases["America/Inuvik"].values()), {-25200, -21600})

    def test_generator_requires_explicit_source_inputs(self):
        run = subprocess.run([sys.executable, str(GENERATOR)], capture_output=True, text=True)
        self.assertNotEqual(run.returncode, 0)
        self.assertIn("--tzdata", run.stderr)
        self.assertIn("--tzcode", run.stderr)

    def test_generator_rejects_changed_archive_without_writing_outputs(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            bad = root / "changed.tar.gz"
            bad.write_bytes(b"not the pinned release")
            output = root / "output"
            run = subprocess.run([
                sys.executable, str(GENERATOR), "--tzdata", str(bad),
                "--tzcode", str(SOURCES / "tzcode2026c.tar.gz"), "--output-root", str(output)
            ], capture_output=True, text=True)
            self.assertNotEqual(run.returncode, 0)
            self.assertIn("SHA-256 mismatch", run.stderr)
            self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
