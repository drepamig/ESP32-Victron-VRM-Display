import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import flash_firmware


class FlashSafetyTests(unittest.TestCase):
    def make_outputs(self, root):
        output = root / "build" / "firmware"
        output.mkdir(parents=True)
        for suffix in ("bootloader.bin", "partitions.bin", "bin"):
            (output / f"VictronCYD_Modbus.ino.{suffix}").write_bytes(b"test")
        (output / "boot_app0.bin").write_bytes(b"test")
        (output / "VictronCYD_Modbus.ino.merged.bin").write_bytes(b"do not flash")
        return output

    def test_flash_uses_discrete_segments_without_touching_nvs(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.make_outputs(root)
            command = flash_firmware.flash_command(root, "COM3")
            self.assertEqual(command[2:7], ["esptool", "--chip", "esp32", "--port", "COM3"])
            self.assertEqual(command[7], "write-flash")
            self.assertEqual(command[8::2], ["0x1000", "0x8000", "0xe000", "0x10000"])
            self.assertFalse(any("merged" in item for item in command))

    def test_missing_segment_and_oversized_partition_table_are_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output = self.make_outputs(root)
            partition = output / "VictronCYD_Modbus.ino.partitions.bin"
            partition.write_bytes(b"x" * 4097)
            with self.assertRaisesRegex(ValueError, "unsafe segment size"):
                flash_firmware.flash_command(root, "COM3")
            partition.unlink()
            with self.assertRaisesRegex(ValueError, "missing or unsafe"):
                flash_firmware.flash_command(root, "COM3")


if __name__ == "__main__":
    unittest.main()
