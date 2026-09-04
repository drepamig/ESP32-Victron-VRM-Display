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
Issue #1 time settings are implemented and released in firmware `8e2cd3b`: gear
menu, persistent 12/24h and North American/UTC timezone selection. Defaults
are America/Chicago and 12h. See the current status/evidence before continuing;
the physical board has received this issue's changes, with interactive acceptance pending.
The latest recorded verified physical upload is
`8e2cd3b9d5497f67c06def6adc76c0055f3cb656` on 2026-09-04. All four segments
were hash-verified, NVS was unchanged byte for byte, and a 25-second startup
check reported private AP ready without panic/watchdog events. Firmware was
pushed to `origin/develop`. Time settings, UI/routing, and R1/R2 acceptance remain
pending. Confirm actual device state before any future upload.
The old nested checkout, feature branch, and missing original plan/spec paths
in the ignored SDD records are historical references.

Before making changes, verify the branch, working tree, baseline commits in
history, remotes, and the repository-owned toolchain. Run the complete current
test script; it now includes 19 C++ suites and 60 Python tooling tests,
including production controller integration for R1 and the FT6206 release-race
and WAN-outage regressions. Dummy builds and tests do not need production secrets. Preserve any
ignored `VictronCYD_Modbus/secrets.h`; never read a secret value into chat or
commit it. Do not erase NVS or modify Venus.

The approved testing direction is local host tests/builds plus Velxio for
routine supported simulator checks, targeted Wokwi comparisons only when
needed, and physical hardware for real Wi-Fi/IPv4 forwarding and deployment acceptance.
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
pending. R2 is implemented: established association/DHCP loss reports Offline
through retries; restoration requires fresh DNS to move from Validating to
Online. Fresh selection and rollback retain Connecting. Its host regression
covers a five-minute outage and exact retry/wraparound behavior; local
`wan-outage` checks presentation and setup/Back. R2's three builds, both simulator
attestations/isolation, independent review, and three targeted local scenarios
passed (19 exact images). Check the current status record for run provenance;
physical outage/AP/recovery acceptance remains pending.

After confirming that verification, resume controlled upstream
provisioning with an appropriately reviewed physical build: keep the bench
upstream available, select it from `Nearby`, validate the masked keyboard and Connect
flow first, then validate **Use phone** as the time-limited private portal
fallback. Ask the user for the current upstream SSID only when provisioning
begins; do not add it to tracked files or logs.

Continue the remaining Task 9 physical keyboard, provisioning, IPv4 forwarding,
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
