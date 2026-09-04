# Supported local Velxio bench — V1–V4

Approved in conversation on 2026-09-03. This implements the local-backend
direction in `2026-09-03-cyd-virtual-bench.md`.

**Status: V1–V4 complete and reviewed on 2026-09-03.** The final matrix passed
17 C++ suites and 54 tooling tests; production and both simulator builds and
isolation passed. Five supported local scenarios produced 18 exact RGBA image
comparisons, including a repeated fresh-worker reboot with retained B and
calibration. Five new reboot captures were visually reviewed before recorded
promotion. See the [acceptance report](../../research/2026-09-03-velxio-local-runner.md)
for run identities, the resolved heartbeat timing difference, explicit coverage
gaps, and physical acceptance boundaries. No Wokwi executions occurred.

## Contract

- Work on develop and preserve existing R1/touch changes. No production behavior,
  NVS schema, flashing, Venus changes, Wokwi execution, or push.
- Default sim-build, sim-test, and all to Velxio. Explicit backend plus named
  scenario or full-suite selection is required for cloud execution; no fallback.
- Pin the research report's Velxio revision/digest, Arduino toolchain,
  Node 24.3.0, Pillow 11.3.0 and PyYAML 6.0.2. Cache during setup; run offline.
- Separate DIO outputs in build/velxio; attest source, build, runtime and adapter
  identities. Keep production and Wokwi outputs separate.
- Maintain FT6206 I2C, icount 3, guest-clock, inversion and RAMWR adapters.
  Interpret supported YAML steps, require SIM READY and fresh acknowledgements,
  reject unsupported steps/crashes/stalls, bound readiness to 30 seconds and
  scenarios to 600 wall seconds, and clean up all processes.
- Capture actual SPI pixels, preserving exact RGBA comparisons. Promote only
  explicitly selected current recorded captures without a new simulation.
- Reboot through a fresh worker using flushed retained flash bound to its run.
  Failed shutdown invalidates the snapshot. Independent scenarios start fresh.

## Tasks and acceptance

1. Build/runtime identity: DIO target, allowlist and attestation regressions,
   runtime/dependency setup, offline execution and default-local command dispatch.
2. Maintained runner: guarded worker protocol, guest timing, decoder, capture and
   failure tests. Two fresh exact Calibration/Saved/Nearby runs; short press,
   long hold and stable release checks.
3. Scenarios: boot-calibration, wan-hold, setup-navigation, saved-switch and new
   reboot-persistence. Verify progress/cancel/blocked controls/timeout/rollback,
   B activation, retained calibration/B after reboot and later failed submission.
4. Final host/tooling matrix, production and both simulator builds, isolation,
   supported local suite, screenshot review, docs/status/handoff and scoped review.

Keep other scenarios visible as coverage gaps. Retain hashes, serial logs,
guest timing, captures, comparisons and failures. The known startup watchdog
warning remains a documented limitation; physical acceptance remains pending.
