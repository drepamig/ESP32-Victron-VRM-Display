# CYD Virtual Bench Implementation Plan

> Execute in the isolated `codex/cyd-virtual-bench` worktree. The design in
> `docs/superpowers/specs/2026-09-03-cyd-virtual-bench-design.md` is already
> approved; no additional approval gate is required.

**Goal:** Build a reproducible, pixel-exact Wokwi bench for
`VictronCYD_Modbus`, with fast native tests and production-safe simulator
boundaries.

**Toolchain:** Docker/devcontainer, Arduino CLI 1.5.1, Arduino-ESP32 3.3.11,
TFT_eSPI 2.5.43, XPT2046_Touchscreen 1.4, Adafruit FT6206 1.1.1, Wokwi CLI
0.26.1, C++17, Python/Pillow.

## Constraints

- Preserve the original checkout, `docs/handoff.lnk`, and both ignored
  `secrets.h` files.
- Begin each new runtime behavior with a focused failing test.
- Never stage production secrets or copy them into simulator inputs.
- Ordinary test commands never alter golden images.
- Never upload firmware or push without separate explicit authorization.
- Wokwi execution may be reported as token-blocked only after all local checks
  and builds that do not require the token have run.

## Tasks

### 1. Record isolation and baseline

- [x] Create a sibling linked worktree from the active branch tip.
- [x] Compile and run all 11 existing host suites from fresh binaries.
- [x] Write and self-review the approved design and this executable plan.

### 2. Build the repository-owned toolchain

- [ ] Add the pinned devcontainer image and executable checksum validation.
- [ ] Add repository-owned TFT_eSPI settings and Arduino CLI configuration.
- [ ] Add the Docker-native `tools/dev.ps1` command dispatcher.
- [ ] Add container scripts for host tests, firmware builds, simulation staging,
      Wokwi execution, screenshot comparison, and golden promotion.
- [ ] Ignore `.tools`, builds, caches, staging, and generated simulator images.
- [ ] Verify `doctor` reports exact dependency versions.

### 3. Extract raw touch and test coordinate normalization

- [ ] Write a failing test for FT6206-to-XPT raw coordinate normalization and
      boundary clamping.
- [ ] Add `RawTouchDevice`, production XPT implementation, and simulator FT6206
      implementation.
- [ ] Inject the boundary into `TouchInput` without changing calibration or
      gesture behavior.
- [ ] Run the focused test and existing touch suites.

### 4. Add deterministic network runtime

- [ ] Write failing fixture/transition tests for scan, connect success/failure,
      RSSI, AP state, pending acceptance, cancellation, and reset.
- [ ] Add `SimCamperNetwork` and compile-time `CamperNetworkRuntime` selection.
- [ ] Replace the sketch's concrete network type with the runtime alias.
- [ ] Run focused and existing network/application suites.

### 5. Extract Modbus source and fixtures

- [ ] Write failing tests for nominal, stale, offline, and partial cycles.
- [ ] Move TCP acquisition behind `ModbusCycleSource::fetch`.
- [ ] Add the simulator cycle source and deterministic fixtures.
- [ ] Run focused and snapshot-policy suites.

### 6. Add serial simulation control

- [ ] Write failing parser tests for every accepted command, reset, malformed
      input, unknown fixture, whitespace, and overlong lines.
- [ ] Add simulation-only parser and runtime state wiring.
- [ ] Prove production preprocessing/object inspection contains no simulator
      parser, adapter, or dummy configuration symbols.

### 7. Build and attest simulator artifacts

- [ ] Add tracked dummy config and explicit source allow-list.
- [ ] Add Wokwi manifest and wired virtual CYD diagram.
- [ ] Stage only allow-listed sources and generated dummy `secrets.h`.
- [ ] Emit merged BIN, ELF, and source/artifact attestation.
- [ ] Add rejection tests for stale attestations, tampered hashes, path escapes,
      production artifacts, and secret references.

### 8. Add scenarios and exact image comparison

- [ ] Add the approved Wokwi YAML scenarios and checkpoint manifest.
- [ ] Add 320x240 RGB exact comparator with expected/actual/highlighted diff
      retention.
- [ ] Verify a one-pixel mutation fails and produces a usable diff.
- [ ] Add guarded golden promotion that prints the changed-image list.
- [ ] Run all scenarios and commit generated goldens when a token is available.

### 9. Document and verify

- [ ] Update README setup, token, commands, golden review, and physical release
      instructions.
- [ ] Run all host suites, production compile, simulator compile, scenario/pixel
      suite, secret isolation, negative attestation, and clean-tree review.
- [ ] Self-review the complete diff for correctness and scope because this task
      does not authorize subagent delegation.
- [ ] Follow the branch-finishing workflow without uploading or pushing.

