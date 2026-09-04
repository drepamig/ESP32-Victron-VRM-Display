# CYD Virtual Bench Implementation Plan

**Status, reconciled 2026-09-03:** The original Wokwi bench was implemented
and merged into `develop` at `4e5384f`. The approved local-backend integration
below is complete for V1–V4. The original checked
steps and acceptance evidence describe the earlier implementation run.
Continue in the current checkout on `develop`
following the root `AGENTS.md`; do not recreate the historical feature checkout.

The later review at `6ef6c93` reran all 15 C++ suites, 14 Python tooling tests,
and the dummy production build successfully. It did not rerun Wokwi or flash
hardware. R1 has since been implemented with controller integration tests and
a saved-switch Wokwi scenario. The subsequent [R2 correction](2026-09-03-r2-wan-outage.md)
reports established upstream loss as Offline throughout retries. The acceptance
block below remains the original bench evidence.
See [current status and acceptance](../../README.md) before physical release.

**Original implementation goal:** Build a reproducible, pixel-exact Wokwi bench for
`VictronCYD_Modbus`, with fast native tests and production-safe simulator
boundaries.

**Toolchain:** Docker/devcontainer, Arduino CLI 1.5.1, Arduino-ESP32 3.3.11,
TFT_eSPI 2.5.43, XPT2046_Touchscreen 1.4, Adafruit FT6206 1.1.1, Wokwi CLI
0.26.1, C++17, Python/Pillow.

## Approved testing strategy — 2026-09-03

**Decision: adopt Velxio as the local simulator backend alongside host tests.**
This direction was approved after the feasibility investigation and FT6206
release fix. R2 adds `wan-outage` to the runner's five original scenarios; six others
remain explicit coverage gaps. Do not restart backend selection as if no decision
had been made.

| Layer | Role in the development plan |
| --- | --- |
| Local host/tooling tests and firmware builds | Fast checks for routine changes; production controller/store/network regressions remain here. |
| Local Velxio | Routine firmware UI, touch, navigation, and simulated application-behavior checks once each scenario is supported by the integrated runner. No Wokwi quota is needed. |
| Wokwi | Occasional, explicitly selected comparison runs or an agreed release checkpoint. State the selected scenarios and purpose before a cloud run. Full suites require an explicit request or agreed release checkpoint. |
| Physical CYD and real networks/GX | Acceptance for real Wi-Fi, AP availability, NAPT, timing, persistence through actual reboot, and deployment behavior. Simulated success does not close these checks. |

The evidence supports this choice: the real application runs locally,
calibration and press/hold work, a real touch-boundary bug was found and
fixed, and two consecutive runs reproduced Calibration/Saved/Nearby exactly
(six comparisons). See the [investigation and measured results](../../research/2026-09-03-velxio-investigation.md#ft6206-correction-and-local-retest).
This is a narrow verification result, not acceptance of every scenario.

Maintenance of the adapters is part of the decision. Pin the Velxio source
revision and image digest recorded in the investigation, the Arduino
toolchain, and decoder dependencies. Retain a separate DIO simulator target,
the tested `-icount 3` configuration and virtual-clock gesture timing, the
FT6206 I2C proxy, and the ILI9341 inversion/RAMWR corrections. Record their
provenance and rerun the relevant local checks when dependencies change.
The emulator cache-error workaround and startup watchdog warning remain
documented emulator limitations after integration.

### Local Velxio runner integration — completed

These deliverables were completed on 2026-09-03, separately from the original
tasks. See the [runner plan](2026-09-03-velxio-local-runner.md) and
[measured acceptance](../../research/2026-09-03-velxio-local-runner.md).
They do not authorize cloud runs, flashing, Venus changes, or pushing.

- [x] **V1: Reproducible local build and runtime.** Integrate the pinned runtime
      and isolated DIO build with `tools/dev.ps1`, `tools/bench.sh`, and the
      existing allowlist/attestation tooling. Build only dummy simulator
      inputs; keep production and Wokwi artifacts separate. Verify source,
      artifact, and runtime identity before execution. Once dependencies are
      cached, local runs must work offline without a Wokwi token.
- [x] **V2: Supported navigation runner.** Move the proven worker/touch/timing
      and display adapters from ignored probe artifacts into maintained
      tooling. Execute calibration, short press, hold, and Nearby navigation;
      require `SIM READY`, reject panics/assertions, enforce bounded waits,
      stop child processes, and compare real captured pixels. Add host/tooling
      regressions for failure paths. Reproduce the existing three exact
      images in two consecutive local runs before accepting this deliverable.
- [x] **V3: Saved switching, timeouts, and reboot.** Extend local scenario
      support to progress/cancellation, successful activation, failure and
      rollback, the 60-second switch timeout, and flash-preserving simulated
      reboot. Verify active selection and calibration after restart. Keep
      timer-boundary/wraparound regressions in host tests; measure guest-time
      waits in the simulator. Report unsupported behavior as a gap rather
      than treating a successful process exit as acceptance.
- [x] **V4: Make the local workflow routine.** Document explicit local entry
      points and supported scenarios in the root README, simulation guide,
      and handoff. Retain current-build hashes, serial logs, captures, and
      comparisons as evidence. Use targeted Wokwi comparisons only to resolve
      a concrete remaining discrepancy or at an agreed checkpoint; reuse
      valid captures and review actual screenshots before any golden update.

`tools/dev.ps1 sim-build`, `sim-test`, and `all` now default to Velxio.
`sim-update-goldens -Run RUN_ID -Scenario NAME` promotes reviewed recorded local
captures without another run. Wokwi execution requires `-Backend wokwi` plus
a selected scenario or explicitly requested full suite; there is no fallback.
Final acceptance passed 17 C++ suites, 54 tooling tests, all three builds,
both simulator attestations/isolation, and 18 exact comparisons across the
five supported local scenarios. No Wokwi minutes were used for integration.

R2's later implementation and verification are recorded in its linked plan.
R1's physical A→B switching, reboot, cancellation, rollback, and AP checks, and
R2's real outage/recovery checks remain pending and must use an NVS-preserving
upload. Tooling progress does not close physical acceptance or the remaining
bench/field work.

## Constraints

- Preserve unrelated local work, `docs/handoff.lnk`, and both ignored
  `secrets.h` files.
- Begin each new runtime behavior with a focused failing test.
- Never stage production secrets or copy them into simulator inputs.
- Ordinary test commands never alter golden images.
- Only attested dummy simulator firmware may be sent to Wokwi under the user's
  existing authorization. Never flash physical firmware or push without a request.
- Follow the approved testing strategy above. Routine verification must not
  depend on Wokwi minutes; report missing simulator coverage explicitly.

## Original implementation tasks (completed)

### 1. Record baseline

- [x] Compile and run all 11 existing host suites from fresh binaries.
- [x] Write and self-review the approved design and this executable plan.

### 2. Build the repository-owned toolchain

- [x] Add the pinned devcontainer image and executable checksum validation.
- [x] Add repository-owned TFT_eSPI settings and Arduino CLI configuration.
- [x] Add the Docker-native `tools/dev.ps1` command dispatcher.
- [x] Add container scripts for host tests, firmware builds, simulation staging,
      Wokwi execution, screenshot comparison, and golden promotion.
- [x] Ignore `.tools`, builds, caches, staging, and generated simulator images.
- [x] Verify `doctor` reports exact dependency versions.

### 3. Extract raw touch and test coordinate normalization

- [x] Write a failing test for FT6206-to-XPT raw coordinate normalization and
      boundary clamping.
- [x] Add `RawTouchDevice`, production XPT implementation, and simulator FT6206
      implementation.
- [x] Inject the boundary into `TouchInput` without changing calibration or
      gesture behavior.
- [x] Run the focused test and existing touch suites.

### 4. Add deterministic network runtime

- [x] Write failing fixture/transition tests for scan, connect success/failure,
      RSSI, AP state, pending acceptance, cancellation, and reset.
- [x] Add `SimCamperNetwork` and compile-time `CamperNetworkRuntime` selection.
- [x] Replace the sketch's concrete network type with the runtime alias.
- [x] Run focused and existing network/application suites.

### 5. Extract Modbus source and fixtures

- [x] Write failing tests for nominal, stale, offline, and partial cycles.
- [x] Move TCP acquisition behind `ModbusCycleSource::fetch`.
- [x] Add the simulator cycle source and deterministic fixtures.
- [x] Run focused and snapshot-policy suites.

### 6. Add serial simulation control

- [x] Write failing parser tests for every accepted command, reset, malformed
      input, unknown fixture, whitespace, and overlong lines.
- [x] Add simulation-only parser and runtime state wiring.
- [x] Prove production preprocessing/object inspection contains no simulator
      parser, adapter, or dummy configuration symbols.

### 7. Build and attest simulator artifacts

- [x] Add tracked dummy config and explicit source allow-list.
- [x] Add Wokwi manifest and wired virtual CYD diagram.
- [x] Stage only allow-listed sources, with no `secrets.h` in the simulator
      stage and one generated dummy `secrets.h` in the production smoke stage.
- [x] Emit merged BIN, ELF, and source/artifact attestation.
- [x] Add rejection tests for stale attestations, tampered hashes, path escapes,
      production artifacts, and secret references.

### 8. Add scenarios and exact image comparison

- [x] Add the approved Wokwi YAML scenarios and checkpoint manifest.
- [x] Add 320x240 RGBA exact comparator with expected/actual/highlighted diff
      retention.
- [x] Verify a one-pixel mutation fails and produces a usable diff.
- [x] Add guarded golden promotion that prints the changed-image list.
- [x] Run all scenarios and commit generated goldens when a token is available.

### 9. Document and verify

- [x] Update README setup, token, commands, golden review, and physical release
      instructions.
- [x] Run all host suites, production compile, simulator compile, scenario/pixel
      suite, secret isolation, negative attestation, and clean-tree review.
- [x] Complete the code review required by the review skill, resolve findings,
      and self-review the final diff for correctness and scope.
- [x] Verify source provenance; no physical flash, merge, or push performed
      during the original acceptance run. The later merge is recorded above.

## Acceptance evidence — 2026-09-03

`tools/dev.ps1 all` completed with exit code 0 on Windows/Docker using the
runtime `WOKWI_CLI_TOKEN`:

- All 11 original C++ suites plus 4 simulator suites passed.
- All 14 Python tooling tests passed, including one-pixel mismatch/diff,
  production-path rejection, stale/tampered artifacts, source edits during
  compilation, crash detection, and NVS-safe flashing.
  Serial grammar itself is covered by the C++ simulator suite.
- Production-mode dummy smoke compile: 1,035,746 bytes flash; 49,492 bytes RAM.
- Simulator compile: 557,216 bytes flash; 34,484 bytes RAM.
- Production/simulator isolation and artifact attestation passed.
- All 9 live Wokwi scenarios passed; all 25 RGBA screenshots matched exactly.
  The ordinary test run left every baseline file unchanged (SHA-256 checked).
- Final diagram lint reported no errors, only informational undocumented-part
  notices for Wokwi's official ESP32 DevKit and capacitive-touch display parts.
- Code review findings were fixed and re-reviewed. Physical panel inversion,
  resistive touch, AP/NAPT, real GX connectivity, watchdog, and actual flashing
  remain hardware release checks.

Historical provenance: the bench was based on `codex/esp32-venus-starlink-touch-bridge` at
`1673bbca4827ab371b7ab5ed68c2125fde66e932`, with zero development commits
missing. The original checkout and its ignored credentials were not modified.
