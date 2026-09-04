# Resume prompt for the Linux Codex task

Copy the text below into the first message of the Linux task after opening this
repository as its local project.

---

Continue the ESP32 Venus-Starlink touch-bridge project in the current repository
checkout on `develop` unless the user explicitly instructs otherwise, following
the root `AGENTS.md`.

First read, in order:

1. `docs/README.md`
2. `docs/handoff/linux-2026-08-31/README.md`
3. `docs/handoff/linux-2026-08-31/VERIFY_LINUX.md`
4. the root `README.md`

Use `docs/README.md` as the current task and acceptance record. The review
baseline is `6ef6c93` on 2026-09-03. Do not restart Tasks 1-8 or repeat completed
Task 9 calibration/countdown/navigation work as if it had never been done.
The latest recorded verified physical upload is
`a6827e6e92db1870f70ccacd73f8d2b0cf4d5a20`; later commits include the keyboard
and virtual bench. Confirm actual device state before any future upload.
The old nested checkout, feature branch, and missing original plan/spec paths
in the ignored SDD records are historical references.

Before making changes, verify the branch, working tree, baseline commits in
history, remotes, and the repository-owned toolchain. Run the complete current
test script; it now includes 17 C++ suites and 54 Python tooling tests,
including production controller integration for R1 and the FT6206 release-race
regression. Dummy builds and tests do not need production secrets. Preserve any
ignored `VictronCYD_Modbus/secrets.h`; never read a secret value into chat or
commit it. Do not erase NVS or modify Venus.

The approved testing direction is local host tests/builds plus Velxio for
routine supported simulator checks, targeted Wokwi comparisons only when
needed, and physical hardware for real Wi-Fi/AP/NAPT and deployment acceptance.
The maintained local runner follows
`docs/superpowers/plans/2026-09-03-velxio-local-runner.md`; use the recorded
verification status before extending its supported scenario set. Keep the
runtime/DIO/timing/touch/display adapters pinned and preserve actual captures.
`sim-build`, `sim-test`, and `all` default to Velxio. Wokwi requires explicit
backend and scenario/full-suite selection; follow `AGENTS.md` quota rules.
Promote reviewed local captures with a run ID, without a simulation rerun.

The earlier physical feedback and top-row touch checkpoints are complete.
Physical validation of the newer on-device password keyboard is still pending.
R1 is implemented and reviewed: saved switching uses a cancellable progress
view and persists only after the selected SSID associates and receives DHCP.
Its host regressions cover persistence, rollback, timeout, and UI ownership.
Physical A→B, reboot, cancel, failed-switch rollback, and AP acceptance remain
pending. R2 remains the next gateway behavior correction: extended upstream loss must show red/offline while
preserving selected-profile retries and AP availability. Add a production-module
regression and obtain review; do not treat passing R1 tests as R2 acceptance.

After those corrections and verification, resume controlled upstream
provisioning with an appropriately reviewed physical build: keep the bench
upstream available, select it from `Nearby`, validate the masked keyboard and Connect
flow first, then validate **Use phone** as the time-limited private portal
fallback. Ask the user for the current upstream SSID only when provisioning
begins; do not add it to tracked files or logs.

Continue the remaining Task 9 physical keyboard, provisioning, NAPT,
bad-credential correction and rollback, second-profile selection/deletion,
phone-fallback portal-boundary, five-minute loss, and reconnect scenarios
exactly as listed in the handoff.
Record measured timings and firmware provenance in `docs/README.md`. Keep
historical observations, fresh host results, and physical results distinct.
Record only observed hardware behavior and pause when user interaction is
required. Real Starlink and live Venus/GX validation remain deferred until the
field hardware is co-located.

Use strict test-first development for any new defect found, obtain review, run
the full host matrix and Arduino compile, then flash only the exact reviewed
image while preserving NVS.

---
