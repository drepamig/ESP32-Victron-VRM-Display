# Physical firmware upload — 2026-09-04

The user authorized committing and flashing the latest changes. Firmware
`d69e0cf03ffd8a99cdbb825efdd9303a69f1962e` was built from a clean `develop`
checkout with the restored, ignored private `VictronCYD_Modbus/secrets.h`, then
flashed to the detected ESP32 on `COM3`. No credentials or raw NVS contents are
included in this record.

## Build and upload evidence

- Arduino CLI 1.5.1, Arduino-ESP32 3.3.11, TFT_eSPI 2.5.43, and
  XPT2046_Touchscreen 1.4; native core-bundled esptool 5.3.1.
- Default ESP32 partition layout, erase-all disabled, repository-owned TFT
  configuration, and production XPT2046 boundary. Simulator markers absent.
- Successful private build: 1,038,902 bytes program storage and 49,532 bytes
  globals. Only the existing TFT_eSPI `TOUCH_CS` warnings were emitted.
- Pre-commit verification passed all 17 C++ suites and 55 tooling tests.
  R2's local simulator evidence remains in its [verification report](2026-09-03-r2-wan-outage.md).
- Four separate firmware segments were written at 460800 baud. All four
  passed esptool's data-hash verification. No merged image or full erase was used.

| Segment | Offset | File bytes | SHA-256 |
| --- | --- | --- | --- |
| Bootloader | `0x1000` | 24,992 | `427f96e10c620c4f062dab15da54fc45494d897e8397ae6f3aecc98c42d7e379` |
| Partition table | `0x8000` | 3,072 | `148b959cbff1c38aa8e1d5c0ba9d612c54997b945e56a63f41223eef650653a1` |
| Boot app | `0xe000` | 8,192 | `f94c5d786a7a8fab06ac5d10e33bf37711a6697636dc037559ea19cc410a17f0` |
| Application | `0x10000` | 1,039,056 | `2e7a5b93d307925dc3a073c5d56c51968f41016eca3ac2251bbb72cdc249d414` |

The ELF SHA-256 is
`8dab76f85174b3d222176bde42f673179df80e61d6bf5815bf51cc27fbad288b`.

The connected board's NVS partition was confirmed at `0x9000`, length `0x5000`.
Raw bytes were read before and after flashing while application execution was
held in the bootloader. All 20,480 bytes matched exactly. Thus the upload
preserved the storage containing saved profiles and touch calibration.

The initial native build attempts encountered a Windows log-file lock during
cleaning and a redundant export to the build directory. Both were resolved;
the final compile exited 0. The first serial write attempt failed to reconnect
before writing any flash. Explicit bootloader reset before each operation
resolved it; the successful upload and NVS comparison both exited 0.

## Observed startup and remaining acceptance

After a normal reset, a 25-second serial observation recorded one boot banner,
private AP ready, no AP failure, no unavailable Modbus worker, no simulator
marker, and no panic/watchdog signature. One invalid Modbus result and no valid
GX result were observed; this does not establish live GX connectivity.

Display appearance and touch operation were not observed in this upload check.
Physical keyboard/provisioning, R1 switching/reboot/cancel/rollback, R2's
five-minute outage/recovery, and private AP/NAPT reachability remain pending in
the [acceptance checklist](../README.md#remaining-task-9-acceptance).

Private build artifacts, upload manifest, NVS snapshots, and serial log remain
under ignored `build/physical-d69e0cf-2026-09-04`; the successful compile log is
`build/r2/physical-compile-final.log`. Raw snapshots and private firmware must
remain out of Git and simulator inputs. No Wokwi execution, Venus change, or
push was performed.
