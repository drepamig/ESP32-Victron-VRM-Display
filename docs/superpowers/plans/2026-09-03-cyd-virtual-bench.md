# CYD Virtual Bench Implementation Plan

> Execute in the current repository checkout on `develop` unless the user
> explicitly instructs otherwise, following the root `AGENTS.md`. The design in
> `docs/superpowers/specs/2026-09-03-cyd-virtual-bench-design.md` is already
> approved; no additional approval gate is required.

**Goal:** Build a reproducible, pixel-exact Wokwi bench for
`VictronCYD_Modbus`, with fast native tests and production-safe simulator
boundaries.

**Toolchain:** Docker/devcontainer, Arduino CLI 1.5.1, Arduino-ESP32 3.3.11,
TFT_eSPI 2.5.43, XPT2046_Touchscreen 1.4, Adafruit FT6206 1.1.1, Wokwi CLI
0.26.1, C++17, Python/Pillow.

## Constraints

- Preserve unrelated local work, `docs/handoff.lnk`, and both ignored
  `secrets.h` files.
- Begin each new runtime behavior with a focused failing test.
- Never stage production secrets or copy them into simulator inputs.
- Ordinary test commands never alter golden images.
- Only attested dummy simulator firmware may be sent to Wokwi under the user's
  existing authorization. Never flash physical firmware or push without a request.
- Wokwi execution may be reported as token-blocked only after all local checks
  and builds that do not require the token have run.

## Tasks

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
- [x] Verify source provenance; no physical flash, merge, or push performed.

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
