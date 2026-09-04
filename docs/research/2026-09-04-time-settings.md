# Issue #1 time-settings verification

Implemented on `develop` after `bdbd0d2`, following the
[approved plan](../superpowers/plans/2026-09-04-time-settings.md). The Modbus
dashboard now has a gear menu with Time and Wi-Fi, persistent 12/24-hour
formatting, and 65 named US/Canadian/Mexican/UTC zones. Defaults are Chicago
and 12-hour AM/PM. The VRM sketch is unchanged.

## Implementation and host evidence

- New `TimeSettings` model/store/formatter, generated `TimeZoneCatalog`, and
  `SettingsUi` are production-used and included in source attestation/builds.
- Time and format share one versioned NVS string record in `timesettings`.
  Invalid/missing records use defaults; unchanged Save avoids writes. Ordinary
  write failure restores the active timezone and retains the draft for retry.
  Profile and calibration storage schemas are unchanged.
- NTP supplies UTC; formatting applies the selected current/future POSIX rule.
  Simulation now feeds the same formatter UTC fixtures. Original fixture epochs
  and profile timestamps remain unchanged. See the pinned
  [timezone sources and regeneration instructions](../../tools/timezones/README.md).
- Backend regressions failed against interface stubs before implementation;
  new DST serial fixture regression failed before the fixtures were added.
  Final matrix passed: **19 C++ suites and 60 Python tests**. The timezone
  oracle covers **4,972 samples** across all 65 zones, including regional DST
  exceptions, half-hour offsets, and date/time boundaries. Offline regeneration
  reproduced the catalog, oracle, and provenance byte for byte.
- UI/policy tests cover draft/save/discard/retry, country/city pagination,
  selection, entry origins, one-contact guards, inactivity/wraparound, and
  pending-attempt ownership. Independent review found four integration issues:
  the actual portal-expiry path, pending Dashboard WAN re-entry, outgoing touch
  carryover, and stale result inactivity timing. These were fixed and covered
  by regressions; final source/spec review found no actionable issues.

## Final builds

| Target | Flash bytes | Global bytes | Result |
| --- | ---: | ---: | --- |
| Production smoke, dummy configuration | 1,047,094 | 49,588 | Passed |
| Local Velxio, DIO | 582,002 | 34,888 | Passed; attestation verified |
| Standard simulator | 581,986 | 34,888 | Passed; attestation verified |

Both simulator attestations and production/simulator isolation passed. The
existing TFT_eSPI `TOUCH_CS` warning remains expected because touch uses the
separate driver. Build hashes and the shared source digest are in the
[machine-readable evidence](2026-09-04-time-settings-evidence.json).

## Actual local simulation

| Retained run ID | Scenarios | Captures |
| --- | --- | ---: |
| `2c9b7b612a5d436587f4dac50aeaf331` | time-settings | 21 |
| `b91fb63ca1df4a7898c89a64bd678580` | boot-calibration, wan-hold, setup-navigation, wan-outage | 13 |
| `db35f1c8e6e24abda1d8b56f509a1d6e` | saved-switch | 7 |
| `4b0206dc1a7d4e2db52e11dc7d6f1219` | reboot-persistence | 5 |

All seven scenarios completed successfully using the same attested firmware.
The new captures visibly prove Chicago's spring `1:59 AM` → `3:00 AM` and autumn
`1:59 AM` → `1:00 AM` transitions, discarded drafts, country/city pagination,
UTC/24h persistence after a flash-preserving worker reboot, menu-origin Wi-Fi
Back, inactivity discard, and unavailable time. The wider regressions preserve
saved A/B switching, cancellation, timeout rollback, profile persistence,
calibration, and the R2 outage/recovery presentation.

All 21 new captures and nine changed dashboard captures were visually inspected
before recorded promotion. Each existing dashboard difference is exactly 835
pixels confined to the header (gear, bounded site name, and clock); the other
16 existing reference images remain unchanged. Two pixel-identical images
copied by whole-scenario promotion were restored from the recorded input bytes
to avoid compression-only changes.

Initial comparison commands returned nonzero for missing new goldens or the
expected old header. Execution itself passed. After reviewed promotion, an
offline recomparison checked capture/result hashes, current native RGBA pixels,
and the shared source digest: **46/46 captures match the current goldens exactly**.
Promotion and recomparison reused recorded captures without rerunning simulations.
The immutable original result JSON retains its pre-promotion comparisons.

## Remaining acceptance

The implementation checkpoint and companion JSON above precede release;
they record no flashing or pushing during implementation. The subsequent
[release and physical upload](2026-09-04-time-settings-upload.md) committed and
pushed firmware `8e2cd3b`, then flashed it on `COM3` with NVS preserved byte
for byte. No Wokwi execution, Venus changes, or issue closure was performed.
Physical acceptance must verify NTP-derived local time, touch navigation, time
settings after reboot, and existing profiles/calibration after an NVS-preserving
upload. Real Wi-Fi/AP/NAPT and R1/R2 acceptance remain pending.

The six previously unsupported local scenarios remain coverage gaps. In
particular, `dashboard-states` references retain the old header until an actual
local acceptance run or explicitly selected cloud comparison can replace them.
No unobserved captures were edited or promoted. The existing Velxio startup
watchdog warning and emulator/cache limitations remain documented limitations.
