# Local Velxio runner acceptance — 2026-09-03

The approved V1–V4 integration runs the real simulator firmware locally from
the `develop` checkout. `sim-build`, `sim-test`, and `all` default to Velxio.
Wokwi requires explicit backend and scenario/full-suite selection. This phase
used **zero Wokwi executions**, performed no flashing or Venus changes, and
made no production behavior or NVS schema changes.

See the [approved plan](../superpowers/plans/2026-09-03-velxio-local-runner.md)
and [machine-readable evidence](2026-09-03-velxio-local-runner-evidence.json).
Full run records remain under ignored `build/velxio/results/<run-id>`; the
tracked evidence preserves their identities, comparisons, and log hashes.

## Delivered behavior

- Setup caches the pinned Velxio image/revision, Node 24.3.0, Pillow 11.3.0,
  PyYAML 6.0.2, and Arduino toolchain. Doctor verifies the cached versions.
- The separate DIO target attests current allowlisted source, firmware hashes,
  actual DIO boot header, build configuration, runtime lock, adapters, and
  producing toolchain image. Production and standard Wokwi outputs stay separate.
- The host coordinates offline runtime containers with only dummy inputs,
  maintained adapters, and an isolated output directory. Workers have neither
  repository access nor the Docker socket. There is no cloud fallback.
- The FT6206 proxy, guest-clock timing at `-icount 3`, and ILI9341 inversion/RAMWR
  corrections are maintained and tested. Captures decode actual SPI traffic
  into native 320x240 RGBA pixels with panel rotation only.
- Readiness, acknowledgements, guest stalls, crashes, unsupported operations,
  missing captures, event loss, and incomplete shutdown fail explicitly.
  Shutdown requires the final transport record, EOF, and clean worker exit.
- Simulated reboot flushes and hashes retained flash, then starts a fresh
  worker. Independent scenarios start from fresh attested firmware.
- Golden promotion checks current run identity, image integrity, and safe
  destinations before copying reviewed recorded captures. It performs no
  simulation. Ordinary tests never alter references.

## Verification

| Check | Measured result |
| --- | --- |
| Complete host/tooling matrix | 17 C++ suites and 54 Python tests passed, including Linux symlink checks. |
| Production smoke build | 1,038,650 bytes flash; 49,524 bytes globals. Dummy configuration. |
| Local DIO build | 559,660 bytes flash; 34,516 bytes globals. |
| Standard simulator build | 559,644 bytes flash; 34,516 bytes globals. Built locally; no cloud run. |
| Isolation and identity | Both simulator attestations and production isolation passed. |
| PowerShell entry point | `tools/dev.ps1 doctor` passed with exact pinned local versions. |
| Scoped and integrated review | No remaining important actionable findings after regression-backed fixes. |

Builds emitted the documented TFT_eSPI `TOUCH_CS` warnings. Each emulator boot
retained its known startup `TWDT already initialized` warning; no unexpected
watchdog event was accepted. Regressions verify rejection of event loss and
missing final transport records instead of treating process exit as sufficient.

The first `all` run returned 1 for a 21-pixel heartbeat-phase difference and
five expected missing reboot references. It is retained as evidence, not
reported as a passing command. The heartbeat difference was isolated to the
production one-second blink timer. An explicit local `wan-hold` timing variant
waits 1200 ms after release instead of 200 ms. Two subsequent runs matched both
unchanged references exactly. No firmware pixels, comparison tolerances, or
existing goldens were changed.

The current accepted checks use these runs:

| Scenario | Run | Exact RGBA images |
| --- | --- | --- |
| boot-calibration | `31902cf167ae4268bb24e69c2a796e27` | 2 |
| setup-navigation | `31902cf167ae4268bb24e69c2a796e27` | 2 |
| wan-hold | `d9137c1ba5b64f638e35a4da50a59d94` | 2 |
| saved-switch | `6c855edaa69f44ab95de9579d5ac8bfc` | 7 |
| reboot-persistence | `a919de0d34294706b8be6da03494c11b` | 5 |

Calibration/Saved/Nearby also matched in the preceding fresh runs within
`6c855edaa69f44ab95de9579d5ac8bfc`, satisfying the two-run navigation gate.
Saved switching exercised progress, cancellation, blocked controls, timeout,
rollback to A, successful selection, and B's active marker. Its guest timeline
reached 103.916 seconds, including the 60-second failure wait.

All five new reboot captures from `6c855edaa69f44ab95de9579d5ac8bfc` were
visually inspected before recorded promotion: B active before restart, a
dashboard without recalibration after restart, B still active, a masked failed
submission, and rollback to B. Both boot segments reached `SIM READY`, with a
clean retained-flash snapshot between them. The second segment reached 77.651
guest seconds. B's before/after/rollback PNGs were byte-identical.

The 32 existing references were retained. Exact comparison concerns decoded
RGBA values: existing PNG file hashes can differ because encoders use different
compression or metadata. The five new reboot PNGs were copied directly from
the reviewed run without rerunning it.

The targeted reboot repeat passed all five exact comparisons, reproduced the
same retained-flash hash, and completed in 280.762 wall seconds. Together the
five supported scenarios account for 18 current exact image comparisons.
The evidence export rechecked current attestation, scenario hashes, result
integrity, capture hashes, and the expected golden hashes for all five records.

## Remaining boundaries

Six existing scenarios remain explicit local coverage gaps: protected-network,
password-entry, connection-flow, saved-profiles, phone-portal, and
dashboard-states. Unsupported local selections fail rather than using Wokwi.
The reboot scenario covers one failed keyboard submission; it does not accept
the complete keyboard or portal suites.

The emulator cache-error workaround, startup watchdog warning, and adapters
remain maintenance obligations. This work verifies simulated application
behavior, not real radio authentication, AP/NAPT routing, GX transport, touch
noise, physical reboot, or deployment timing. R1 still needs physical A→B,
reboot, cancellation, failed-switch rollback, and AP availability checks using
an NVS-preserving upload. R2's WAN-outage color correction remains separate.
