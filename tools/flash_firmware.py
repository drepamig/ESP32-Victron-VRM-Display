"""Flash the pinned default ESP32 partition layout without overwriting NVS."""

import argparse
import subprocess
import sys
from pathlib import Path


def flash_command(repo: Path, port: str) -> list[str]:
    output = (repo / "build" / "firmware").resolve()
    # NVS occupies 0x9000..0xdfff in the pinned default Arduino ESP32 layout.
    # Never use the merged image: its padding covers that region too.
    segments = (
        (0x1000, 0x7000, "VictronCYD_Modbus.ino.bootloader.bin"),
        (0x8000, 0x1000, "VictronCYD_Modbus.ino.partitions.bin"),
        (0xe000, 0x2000, "boot_app0.bin"),
        (0x10000, 0x140000, "VictronCYD_Modbus.ino.bin"),
    )
    command = [sys.executable, "-m", "esptool", "--chip", "esp32", "--port", port, "write-flash"]
    for offset, maximum, filename in segments:
        path = output / filename
        if not path.is_file() or path.is_symlink() or path.resolve().parent != output:
            raise ValueError(f"missing or unsafe firmware segment: {filename}; run firmware-build")
        if not 0 < path.stat().st_size <= maximum:
            raise ValueError(f"unsafe segment size: {filename}")
        command.extend((hex(offset), str(path)))
    return command


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    print("Flashing production-mode DUMMY bench firmware; NVS is preserved.", flush=True)
    return subprocess.call(flash_command(repo, args.port))


if __name__ == "__main__":
    raise SystemExit(main())
