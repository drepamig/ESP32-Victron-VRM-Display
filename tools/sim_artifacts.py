#!/usr/bin/env python3
import argparse
import hashlib
import json
import re
import shutil
from pathlib import Path, PurePosixPath


DUMMY_CONFIG_ID = "cyd-sim-dummy-v1"
ARTIFACT_PATHS = (
    PurePosixPath("build/simulation/firmware.bin"),
    PurePosixPath("build/simulation/firmware.elf"),
)


def artifact_paths(mode: str = "simulation") -> tuple[PurePosixPath, ...]:
    if mode not in {"simulation", "velxio"}:
        raise ValueError(f"unsupported simulator mode: {mode}")
    return tuple(PurePosixPath(f"build/{mode}/firmware.{ext}") for ext in ("bin", "elf"))


def runtime_identity(repo: Path) -> dict:
    lock_path = repo / "simulation/velxio/runtime-lock.json"
    if not lock_path.is_file() or lock_path.is_symlink():
        raise ValueError("Velxio runtime lock is missing or unsafe")
    lock = json.loads(lock_path.read_text(encoding="utf-8"))
    adapters = {}
    for path in sorted((repo / "tools/velxio").rglob("*")):
        relative = path.relative_to(repo).as_posix()
        if path.suffix not in {".py", ".mts", ".js"} or "__pycache__" in path.parts:
            continue
        if not path.is_file() or path.is_symlink() or not _inside(path, repo / "tools/velxio"):
            raise ValueError("Velxio adapter is missing or unsafe")
        adapters[relative] = sha256(path)
    if not adapters:
        raise ValueError("Velxio maintained adapters are missing")
    digest = hashlib.sha256(lock_path.read_bytes() + b"\0")
    for relative, value in adapters.items():
        digest.update(relative.encode() + b"\0" + value.encode() + b"\0")
    return {"lock": lock, "lockSha256": sha256(lock_path), "adapters": adapters,
            "sha256": digest.hexdigest()}


def build_configuration_identity(repo: Path) -> dict:
    cache_path = repo / ".tools/velxio/runtime.json"
    if not cache_path.is_file() or cache_path.is_symlink() or not _inside(cache_path, repo):
        raise ValueError("Velxio toolchain cache is missing or unsafe; run tools/dev.ps1 setup")
    try:
        cache = json.loads(cache_path.read_text(encoding="utf-8"))
    except (ValueError, OSError) as error:
        raise ValueError("Velxio toolchain cache is invalid; run tools/dev.ps1 setup") from error
    image_id = cache.get("toolchainImageId") if isinstance(cache, dict) else None
    if not isinstance(image_id, str) or not re.fullmatch(r"sha256:[0-9a-f]{64}", image_id):
        raise ValueError("Velxio toolchain image is not pinned; run tools/dev.ps1 setup")
    files = {}
    for relative in ("tools/arduino-build.sh", ".devcontainer/Dockerfile"):
        path = repo / relative
        if not path.is_file() or path.is_symlink() or not _inside(path, repo):
            raise ValueError(f"Velxio build configuration is missing or unsafe: {relative}")
        files[relative] = sha256(path)
    return {"fqbn": "esp32:esp32:esp32:FlashMode=dio", "defines": ["CYD_SIMULATION"],
            "files": files, "toolchainImageId": image_id}


def verify_dio_image(path: Path) -> None:
    # Arduino's ESP32 merged image starts at flash address zero; the bootloader
    # image header is at 0x1000, with SPI flash mode in its third byte.
    with path.open("rb") as image:
        image.seek(0x1000)
        header = image.read(4)
    if len(header) != 4 or header[0] != 0xE9 or header[2] != 2:
        raise ValueError("Velxio merged firmware must contain a DIO ESP32 bootloader at 0x1000")


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
    if mode not in {"production", "simulation", "velxio"}:
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
    production_only = name.startswith("TcpModbusCycleSource") or name in {"CamperNetwork.cpp", "Ipv4Bridge.cpp"}
    if mode == "production":
        return not simulator_only
    return not production_only


def stage_sources(repo: Path, mode: str) -> Path:
    repo = repo.resolve()
    source_hash = source_tree_hash(repo)
    identity = runtime_identity(repo) if mode == "velxio" else None
    build_identity = build_configuration_identity(repo) if mode == "velxio" else None
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
    if source_tree_hash(repo) != source_hash:
        raise ValueError("source changed since staging began; rebuild")
    inputs = {
        "sourceSha256": source_hash,
        "files": {
            path.relative_to(stage_root).as_posix(): sha256(path)
            for path in stage_root.rglob("*") if path.is_file()
        },
    }
    if identity is not None:
        if runtime_identity(repo) != identity:
            raise ValueError("runtime or adapter changed since staging began; rebuild")
        inputs["runtimeIdentity"] = identity
        if build_configuration_identity(repo) != build_identity:
            raise ValueError("build configuration changed since staging began; rebuild")
        inputs["buildConfiguration"] = build_identity
    (stage_root / "inputs.json").write_text(json.dumps(inputs), encoding="utf-8")
    return stage_root / "VictronCYD_Modbus"


def check_secret_isolation(repo: Path, stage_root: Path, mode: str) -> None:
    stage_root = stage_root.resolve()
    if not _inside(stage_root, repo / "build" / "staging"):
        raise ValueError("staging path is outside build/staging")
    secret_files = [path for path in stage_root.rglob("*") if path.name.lower() == "secrets.h"]
    if mode in {"simulation", "velxio"} and secret_files:
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


def _attestation_path(repo: Path, mode: str = "simulation") -> Path:
    artifact_paths(mode)
    return repo / "build" / mode / "attestation.json"


def create_attestation(repo: Path, mode: str = "simulation") -> dict:
    repo = repo.resolve()
    paths = artifact_paths(mode)
    stage = _safe_stage_root(repo, mode)
    inputs_path = stage / "inputs.json"
    if not inputs_path.is_file():
        raise ValueError("staged input manifest is missing; rebuild")
    inputs = json.loads(inputs_path.read_text(encoding="utf-8"))
    if mode == "velxio" and inputs.get("runtimeIdentity") != runtime_identity(repo):
        raise ValueError("runtime or adapter changed since staging; rebuild")
    if mode == "velxio" and inputs.get("buildConfiguration") != build_configuration_identity(repo):
        raise ValueError("build configuration changed since staging; rebuild")
    source_hash = inputs.get("sourceSha256")
    if source_tree_hash(repo) != source_hash:
        raise ValueError("source changed since staging; rebuild before attesting")
    for relative, expected_hash in inputs["files"].items():
        staged_file = stage / relative
        if not _inside(staged_file, stage) or not staged_file.is_file() or sha256(staged_file) != expected_hash:
            raise ValueError("compiled input changed since staging; rebuild")
    simulator_root = (repo / "build" / mode).resolve()
    artifacts = []
    for relative in paths:
        path = repo.joinpath(*relative.parts)
        if not path.is_file() or path.is_symlink() or not _inside(path, simulator_root):
            raise ValueError(f"missing or unsafe simulator artifact: {relative}")
        if mode == "velxio" and path.suffix == ".bin":
            verify_dio_image(path)
        artifacts.append({"path": relative.as_posix(), "sha256": sha256(path)})
    document = {
        "schema": 1,
        "buildMode": "CYD_SIMULATION",
        "dummyConfigId": DUMMY_CONFIG_ID,
        "sourceSha256": source_hash,
        "artifacts": artifacts,
    }
    if mode == "velxio":
        document.update(backend="velxio", fqbn="esp32:esp32:esp32:FlashMode=dio",
                        runtimeIdentity=inputs["runtimeIdentity"],
                        buildConfiguration=inputs["buildConfiguration"])
    path = _attestation_path(repo, mode)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(document, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return document


def verify_attestation(repo: Path, mode: str = "simulation") -> dict:
    repo = repo.resolve()
    paths = artifact_paths(mode)
    path = _attestation_path(repo, mode)
    if not path.is_file() or path.is_symlink():
        raise ValueError("simulator attestation is missing")
    document = json.loads(path.read_text(encoding="utf-8"))
    if mode == "velxio":
        if document.get("backend") != "velxio" or document.get("fqbn") != "esp32:esp32:esp32:FlashMode=dio":
            raise ValueError("Velxio backend or build configuration is invalid")
        if document.get("runtimeIdentity") != runtime_identity(repo):
            raise ValueError("Velxio runtime or adapter identity is stale")
        if document.get("buildConfiguration") != build_configuration_identity(repo):
            raise ValueError("Velxio build configuration is stale")
    elif document.get("backend") not in {None, "wokwi"}:
        raise ValueError("simulator backend is invalid")
    if document.get("schema") != 1 or document.get("buildMode") != "CYD_SIMULATION":
        raise ValueError("simulator attestation schema or build mode is invalid")
    if document.get("dummyConfigId") != DUMMY_CONFIG_ID:
        raise ValueError("simulator dummy configuration identifier is invalid")
    if document.get("sourceSha256") != source_tree_hash(repo):
        raise ValueError("simulator source hash is stale")
    simulator_root = (repo / "build" / mode).resolve()
    artifacts = document.get("artifacts")
    if not isinstance(artifacts, list) or len(artifacts) != len(paths):
        raise ValueError("simulator artifact list is invalid")
    expected_paths = {path.as_posix() for path in paths}
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
            raise ValueError(f"attested artifact is outside build/{mode}")
        if relative_text not in expected_paths:
            raise ValueError("attested artifact is not an expected simulator output")
        if not resolved.is_file() or resolved.is_symlink():
            raise ValueError("attested simulator artifact is missing or unsafe")
        if artifact.get("sha256") != sha256(resolved):
            raise ValueError(f"artifact hash mismatch: {relative_text}")
        if mode == "velxio" and resolved.suffix == ".bin":
            verify_dio_image(resolved)
        seen_paths.add(relative_text)
    if seen_paths != expected_paths:
        raise ValueError("simulator artifact list is incomplete")
    return document


def main() -> int:
    parser = argparse.ArgumentParser(description="CYD simulator source and artifact guard")
    subparsers = parser.add_subparsers(dest="command", required=True)
    stage_parser = subparsers.add_parser("stage")
    stage_parser.add_argument("--mode", choices=("production", "simulation", "velxio"), required=True)
    for command in ("attest", "verify"):
        subparsers.add_parser(command).add_argument("--mode", choices=("simulation", "velxio"), default="simulation")
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    if args.command == "stage":
        print(stage_sources(repo, args.mode))
    elif args.command == "attest":
        create_attestation(repo, args.mode)
        print(_attestation_path(repo, args.mode))
    else:
        verify_attestation(repo, args.mode)
        print("simulator attestation: verified")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
