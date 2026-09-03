#!/usr/bin/env python3
import argparse
import hashlib
import json
import shutil
from pathlib import Path, PurePosixPath


DUMMY_CONFIG_ID = "cyd-sim-dummy-v1"
ARTIFACT_PATHS = (
    PurePosixPath("build/simulation/firmware.bin"),
    PurePosixPath("build/simulation/firmware.elf"),
)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _inside(path: Path, parent: Path) -> bool:
    try:
        path.resolve().relative_to(parent.resolve())
        return True
    except ValueError:
        return False


def load_allowlist(repo: Path) -> list[PurePosixPath]:
    allowlist_path = repo / "simulation" / "source-allowlist.txt"
    entries: list[PurePosixPath] = []
    for raw_line in allowlist_path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        path = PurePosixPath(line)
        if path.is_absolute() or ".." in path.parts or not path.parts:
            raise ValueError(f"unsafe allowlist path: {line}")
        if path.name.lower() == "secrets.h":
            raise ValueError(f"secret files are forbidden in the allowlist: {line}")
        resolved = repo.joinpath(*path.parts)
        if not resolved.is_file() or resolved.is_symlink() or not _inside(resolved, repo):
            raise ValueError(f"allowlisted source is missing or unsafe: {line}")
        entries.append(path)
    if not entries:
        raise ValueError("source allowlist is empty")
    return entries


def source_tree_hash(repo: Path) -> str:
    digest = hashlib.sha256()
    allowlist_path = repo / "simulation" / "source-allowlist.txt"
    digest.update(b"allowlist\0")
    digest.update(allowlist_path.read_bytes())
    for relative in load_allowlist(repo):
        encoded = relative.as_posix().encode("utf-8")
        digest.update(encoded)
        digest.update(b"\0")
        digest.update(repo.joinpath(*relative.parts).read_bytes())
        digest.update(b"\0")
    return digest.hexdigest()


def _safe_stage_root(repo: Path, mode: str) -> Path:
    if mode not in {"production", "simulation"}:
        raise ValueError(f"unsupported build mode: {mode}")
    parent = (repo / "build" / "staging").resolve()
    target = (parent / mode).resolve()
    if target.parent != parent:
        raise ValueError("unsafe staging target")
    return target


def _dummy_secrets_text() -> str:
    return (
        '#define SECRET_CAMPER_AP_SSID "CYD-Bench-AP"\n'
        '#define SECRET_CAMPER_AP_PASS "dummy-ap-pass-123"\n'
        '#define SECRET_GX_IP "192.0.2.50"\n'
        '#define SECRET_SITE_NAME "CYD Virtual Bench"\n'
    )


def _included_in_mode(relative: PurePosixPath, mode: str) -> bool:
    name = relative.name
    simulator_only = (
        name.startswith("SimCamperNetwork")
        or name.startswith("SimModbusCycleSource")
        or name.startswith("SimulationClock")
        or name.startswith("SimulationControl")
        or name == "SimulationDummyConfig.h"
    )
    production_only = name.startswith("TcpModbusCycleSource") or name == "CamperNetwork.cpp"
    if mode == "production":
        return not simulator_only
    return not production_only


def stage_sources(repo: Path, mode: str) -> Path:
    repo = repo.resolve()
    stage_root = _safe_stage_root(repo, mode)
    if stage_root.exists():
        if not _inside(stage_root, repo / "build" / "staging"):
            raise ValueError("refusing to clear unsafe staging path")
        shutil.rmtree(stage_root)
    stage_root.mkdir(parents=True)
    for relative in load_allowlist(repo):
        if not _included_in_mode(relative, mode):
            continue
        destination = stage_root.joinpath(*relative.parts)
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(repo.joinpath(*relative.parts), destination)
    if mode == "production":
        (stage_root / "VictronCYD_Modbus" / "secrets.h").write_text(
            _dummy_secrets_text(), encoding="utf-8", newline="\n"
        )
    check_secret_isolation(repo, stage_root, mode)
    return stage_root / "VictronCYD_Modbus"


def check_secret_isolation(repo: Path, stage_root: Path, mode: str) -> None:
    stage_root = stage_root.resolve()
    if not _inside(stage_root, repo / "build" / "staging"):
        raise ValueError("staging path is outside build/staging")
    secret_files = [path for path in stage_root.rglob("*") if path.name.lower() == "secrets.h"]
    if mode == "simulation" and secret_files:
        raise ValueError("simulation staging contains a secrets.h file")
    if mode == "production" and len(secret_files) != 1:
        raise ValueError("production dummy staging must contain exactly one generated secrets.h")
    forbidden_paths = (
        str((repo / "VictronCYD_Modbus" / "secrets.h").resolve()),
        str((repo / "VictronCYD" / "secrets.h").resolve()),
    )
    for path in stage_root.rglob("*"):
        if not path.is_file():
            continue
        data = path.read_bytes()
        for forbidden in forbidden_paths:
            if forbidden.encode("utf-8") in data or forbidden.replace("\\", "/").encode("utf-8") in data:
                raise ValueError(f"staged file references a production secret path: {path}")


def _attestation_path(repo: Path) -> Path:
    return repo / "build" / "simulation" / "attestation.json"


def create_attestation(repo: Path) -> dict:
    repo = repo.resolve()
    simulator_root = (repo / "build" / "simulation").resolve()
    artifacts = []
    for relative in ARTIFACT_PATHS:
        path = repo.joinpath(*relative.parts)
        if not path.is_file() or path.is_symlink() or not _inside(path, simulator_root):
            raise ValueError(f"missing or unsafe simulator artifact: {relative}")
        artifacts.append({"path": relative.as_posix(), "sha256": sha256(path)})
    document = {
        "schema": 1,
        "buildMode": "CYD_SIMULATION",
        "dummyConfigId": DUMMY_CONFIG_ID,
        "sourceSha256": source_tree_hash(repo),
        "artifacts": artifacts,
    }
    path = _attestation_path(repo)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(document, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return document


def verify_attestation(repo: Path) -> dict:
    repo = repo.resolve()
    path = _attestation_path(repo)
    if not path.is_file() or path.is_symlink():
        raise ValueError("simulator attestation is missing")
    document = json.loads(path.read_text(encoding="utf-8"))
    if document.get("schema") != 1 or document.get("buildMode") != "CYD_SIMULATION":
        raise ValueError("simulator attestation schema or build mode is invalid")
    if document.get("dummyConfigId") != DUMMY_CONFIG_ID:
        raise ValueError("simulator dummy configuration identifier is invalid")
    if document.get("sourceSha256") != source_tree_hash(repo):
        raise ValueError("simulator source hash is stale")
    simulator_root = (repo / "build" / "simulation").resolve()
    artifacts = document.get("artifacts")
    if not isinstance(artifacts, list) or len(artifacts) != len(ARTIFACT_PATHS):
        raise ValueError("simulator artifact list is invalid")
    expected_paths = {path.as_posix() for path in ARTIFACT_PATHS}
    seen_paths: set[str] = set()
    for artifact in artifacts:
        relative_text = artifact.get("path", "")
        relative = PurePosixPath(relative_text)
        resolved = repo.joinpath(*relative.parts)
        if (
            relative.is_absolute()
            or ".." in relative.parts
            or not _inside(resolved, simulator_root)
        ):
            raise ValueError("attested artifact is outside build/simulation")
        if relative_text not in expected_paths:
            raise ValueError("attested artifact is not an expected simulator output")
        if not resolved.is_file() or resolved.is_symlink():
            raise ValueError("attested simulator artifact is missing or unsafe")
        if artifact.get("sha256") != sha256(resolved):
            raise ValueError(f"artifact hash mismatch: {relative_text}")
        seen_paths.add(relative_text)
    if seen_paths != expected_paths:
        raise ValueError("simulator artifact list is incomplete")
    return document


def main() -> int:
    parser = argparse.ArgumentParser(description="CYD simulator source and artifact guard")
    subparsers = parser.add_subparsers(dest="command", required=True)
    stage_parser = subparsers.add_parser("stage")
    stage_parser.add_argument("--mode", choices=("production", "simulation"), required=True)
    subparsers.add_parser("attest")
    subparsers.add_parser("verify")
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    if args.command == "stage":
        print(stage_sources(repo, args.mode))
    elif args.command == "attest":
        create_attestation(repo)
        print(_attestation_path(repo))
    else:
        verify_attestation(repo)
        print("simulator attestation: verified")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
