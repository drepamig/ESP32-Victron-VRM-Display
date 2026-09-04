# IPv4 repeater physical upload — 2026-09-04

The user authorized committing, pushing, and flashing the implementation.
Firmware `d2df9e443f0093a01b3f10b7251e0f9580aec465` was committed on `develop`,
pushed to `origin/develop`, built from the clean checkout with the ignored
private configuration, and flashed to the ESP32 on `COM3`.
This supersedes the [time-settings upload](2026-09-04-time-settings-upload.md).

## Release verification

- Fresh release checks passed all 23 C++ host suites and 61 Python tooling
  tests. Rechecking the four retained local scenarios verified the final
  attestation and all 42 exact RGBA matches without rerunning simulation.
  See the [implementation evidence](2026-09-04-ipv4-repeater.md).
- Native Arduino CLI 1.5.1, Arduino-ESP32 3.3.11, TFT_eSPI 2.5.43,
  XPT2046_Touchscreen 1.4, and core-bundled esptool 5.3.1.
- The private production build passed: 1,056,622 bytes program storage and
  50,148 bytes globals. Preflight verified the source commit, clean checkout,
  production sketch path, default partition layout, disabled erase-all, private
  configuration, bounded firmware segments, and absence of simulator markers.
- The connected board's partition entries matched the build. The four segments
  below were written at 460800 baud; each passed esptool data-hash verification.
  No merged image or full erase was used.

| Segment | Offset | File bytes | SHA-256 |
| --- | --- | ---: | --- |
| Bootloader | `0x1000` | 24,992 | `427f96e10c620c4f062dab15da54fc45494d897e8397ae6f3aecc98c42d7e379` |
| Partition table | `0x8000` | 3,072 | `148b959cbff1c38aa8e1d5c0ba9d612c54997b945e56a63f41223eef650653a1` |
| Boot app | `0xe000` | 8,192 | `f94c5d786a7a8fab06ac5d10e33bf37711a6697636dc037559ea19cc410a17f0` |
| Application | `0x10000` | 1,056,768 | `18ed484f069bda7e80de12696840c94eae3705eb8667d1b85c8b0931b3908094` |

The ELF SHA-256 is
`a94908a235170b26616e3d574228c08659a5be0ae24e7e471f30032fb2dfb992`.

The NVS region at `0x9000`, length `0x5000`, was read before and after flashing
while application execution was held in the bootloader. All **20,480 bytes
matched exactly**, preserving stored profiles, calibration, and time settings
across this upload.

## Observed startup and remaining acceptance

A normal reset followed by 25 seconds of serial observation recorded one boot
banner and AP ready. There was no AP failure, unavailable Modbus worker,
simulator marker, or panic/watchdog signature. Two invalid Modbus results and
no valid GX result were observed; live Venus connectivity remains unverified.

This confirms the upload and basic startup. It does not complete real upstream
DHCP, direct IPv4 SSH, live Modbus, forwarding under load, reconnect/fallback,
or physical display/touch acceptance. Follow the
[repeater hardware checks](2026-09-04-ipv4-repeater.md#physical-acceptance-still-required).
No Venus OS configuration was changed and no Wokwi execution was performed.

Private firmware, upload manifest, NVS snapshots, and raw serial output remain
under ignored `build/physical-d2df9e4-2026-09-04`. The native compile log is
`build/ipv4-repeater-private-compile.log`; upload/monitor helpers are under
`build/repeater-release`. Credentials, private binaries, and raw NVS contents
are excluded from Git.
