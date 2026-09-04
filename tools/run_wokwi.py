#!/usr/bin/env python3
import argparse
import json
import os
import shutil
import subprocess
import tomllib
from pathlib import Path

import compare_images
import sim_artifacts


def _inside(path: Path, parent: Path) -> bool:
    try:
        path.resolve().relative_to(parent.resolve())
        return True
    except ValueError:
        return False


def load_scenarios(repo: Path, selected: str | None) -> list[dict]:
    manifest_path = repo / "simulation" / "scenario-manifest.json"
    document = json.loads(manifest_path.read_text(encoding="utf-8"))
    if document.get("schema") != 1 or not isinstance(document.get("scenarios"), list):
        raise ValueError("invalid scenario manifest")
    scenarios = [item for item in document["scenarios"]
                 if "wokwi" in item.get("backends", ["wokwi"])]
    if selected is not None:
        scenarios = [item for item in scenarios if item.get("name") == selected]
        if not scenarios:
            raise ValueError(f"unknown scenario: {selected}")
    for scenario in scenarios:
        scenario_file = repo / "simulation" / scenario["file"]
        if not scenario_file.is_file() or not _inside(scenario_file, repo / "simulation" / "scenarios"):
            raise ValueError(f"missing or unsafe scenario file: {scenario.get('name')}")
    return scenarios


def verify_launch_artifacts(repo: Path) -> None:
    sim_artifacts.verify_attestation(repo)
    config_file = repo / "simulation" / "wokwi.toml"
    with config_file.open("rb") as source:
        config = tomllib.load(source).get("wokwi", {})
    for key, filename in (("firmware", "firmware.bin"), ("elf", "firmware.elf")):
        configured = config.get(key)
        expected = (repo / "build" / "simulation" / filename).resolve()
        if not isinstance(configured, str) or (config_file.parent / configured).resolve() != expected:
            raise ValueError(f"Wokwi config selects an unattested {key} artifact")


def run_scenario(repo: Path, scenario: dict) -> None:
    # Check the actual Wokwi inputs again immediately before each upload.
    verify_launch_artifacts(repo)
    name = scenario["name"]
    results_root = (repo / "build" / "simulation" / "results").resolve()
    scenario_root = (results_root / name).resolve()
    if scenario_root.parent != results_root:
        raise ValueError("unsafe simulator result path")
    if scenario_root.exists():
        shutil.rmtree(scenario_root)
    scenario_root.mkdir(parents=True)
    serial_log = scenario_root / "serial.log"
    command = [
        "wokwi-cli",
        "simulation",
        "--scenario",
        scenario["file"],
        "--timeout",
        "180000",
        "--timeout-exit-code",
        "1",
        "--serial-log-file",
        str(serial_log),
    ]
    subprocess.run(command, cwd=repo, env=os.environ.copy(), check=True)
    validate_serial_log(serial_log)
    for checkpoint in scenario["screenshots"]:
        actual = scenario_root / f"{checkpoint}.png"
        if not actual.is_file():
            raise ValueError(f"scenario did not create screenshot: {actual}")


def validate_serial_log(path: Path) -> None:
    output = path.read_text(encoding="utf-8", errors="replace")
    for marker in ("assert failed:", "Guru Meditation Error", "Backtrace:", "Rebooting..."):
        if marker in output:
            raise ValueError(f"firmware crash detected ({marker}); see {path}")
    if "SIM READY" not in output:
        raise ValueError(f"firmware did not become ready; see {path}")


def compare_scenario(repo: Path, scenario: dict) -> list[tuple[Path, Path]]:
    changed: list[tuple[Path, Path]] = []
    name = scenario["name"]
    for checkpoint in scenario["screenshots"]:
        expected = repo / "simulation" / "goldens" / name / f"{checkpoint}.png"
        actual = repo / "build" / "simulation" / "results" / name / f"{checkpoint}.png"
        failure_dir = repo / "build" / "simulation" / "diffs" / name / checkpoint
        compare_images.validate(actual)
        if not expected.is_file() or not compare_images.compare(expected, actual, failure_dir):
            changed.append((actual, expected))
    return changed


def main() -> int:
    parser = argparse.ArgumentParser(description="Run attested CYD Wokwi scenarios")
    parser.add_argument("mode", choices=("test", "update-goldens"))
    parser.add_argument("--scenario")
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    token = os.environ.get("WOKWI_CLI_TOKEN", "")
    if not token:
        raise SystemExit("WOKWI_CLI_TOKEN is required")
    verify_launch_artifacts(repo)
    scenarios = load_scenarios(repo, args.scenario)
    changed: list[tuple[Path, Path]] = []
    for scenario in scenarios:
        print(f"running Wokwi scenario: {scenario['name']}", flush=True)
        run_scenario(repo, scenario)
        changed.extend(compare_scenario(repo, scenario))
    if not changed:
        print("all simulator screenshots match exactly")
        return 0
    print("changed simulator screenshots:")
    for _, expected in changed:
        print(f"  {expected.relative_to(repo)}")
    if args.mode == "test":
        print("goldens were not modified")
        return 1
    for actual, expected in changed:
        expected.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(actual, expected)
    print(f"updated {len(changed)} golden screenshot(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
