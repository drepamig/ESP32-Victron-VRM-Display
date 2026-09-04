# Time-settings physical upload — 2026-09-04

The user authorized committing, pushing, and flashing the latest changes.
Time-settings firmware `8e2cd3b9d5497f67c06def6adc76c0055f3cb656` was committed
on `develop`, pushed to `origin/develop`, built from the clean checkout with
the ignored private configuration, and flashed to the ESP32 on `COM3`.
This supersedes the [R2 upload checkpoint](2026-09-04-physical-upload.md).

## Release verification

- Fresh local verification passed all 19 C++ host suites and 60 Python tooling
  tests. Both retained simulator attestations and production/simulator isolation
  passed. Rechecking the seven recorded local scenarios confirmed all 46 exact
  RGBA matches against the reviewed goldens, without rerunning simulations.
  See the [implementation evidence](2026-09-04-time-settings.md).
- Native Arduino CLI 1.5.1, Arduino-ESP32 3.3.11, TFT_eSPI 2.5.43,
  XPT2046_Touchscreen 1.4, and core-bundled esptool 5.3.1.
- Successful private production build: 1,047,206 bytes program storage and
  49,588 bytes globals. The default partition scheme, disabled erase-all,
  repository-owned TFT configuration, and production source path were checked.
  Simulator markers were absent from the ELF.
- The connected board's partition entries matched the new partition table.
  The four bounded firmware segments below were written at 460800 baud and
  each passed esptool's data-hash verification. No merged image or full erase
  was used.

| Segment | Offset | File bytes | SHA-256 |
| --- | --- | ---: | --- |
| Bootloader | `0x1000` | 24,992 | `427f96e10c620c4f062dab15da54fc45494d897e8397ae6f3aecc98c42d7e379` |
| Partition table | `0x8000` | 3,072 | `148b959cbff1c38aa8e1d5c0ba9d612c54997b945e56a63f41223eef650653a1` |
| Boot app | `0xe000` | 8,192 | `f94c5d786a7a8fab06ac5d10e33bf37711a6697636dc037559ea19cc410a17f0` |
| Application | `0x10000` | 1,047,360 | `1c60662cf2b03622031a4c6f12b460356ac5e46990dc7b9738d412a3cd57ddff` |

The ELF SHA-256 is
`2a5d9a2576b29ec436c104bf34934721666ea0cd6081b7e73ca86f5fe64b9b87`.

The NVS region at `0x9000`, length `0x5000`, was read before and after flashing
while application execution was held in the bootloader. All **20,480 bytes
matched exactly**, preserving stored profiles, calibration, and any existing
time-settings record across this upload.

## Observed startup and remaining acceptance

A normal reset followed by 25 seconds of serial observation recorded one boot
banner and private AP ready. There was no AP failure, unavailable Modbus
worker, simulator marker, or panic/watchdog signature. One invalid Modbus
result and no valid GX result were observed; live GX connectivity is unverified.

Physical display/touch navigation, NTP-derived local time, and saving/rebooting
time settings were not exercised. R1 switching/reboot/cancel/rollback, R2's
five-minute outage/recovery, and real AP/NAPT reachability remain pending in
the [acceptance checklist](../README.md#remaining-task-9-acceptance).

Private firmware, upload manifest, NVS snapshots, and raw serial output remain
under ignored `build/physical-8e2cd3b-2026-09-04`. The native compile log is
`build/time-settings-physical-compile.log`; release checks are in
`build/time-settings-release-tests.log` and
`build/time-settings-release-isolation.log`. Credentials and raw NVS contents
are excluded from Git. No Wokwi execution, Venus change, or issue closure was
performed.
