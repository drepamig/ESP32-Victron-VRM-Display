# Virtual CYD scenarios

Velxio is the default local backend. Run `tools/dev.ps1 setup` once to cache
pinned dependencies, then use `tools/dev.ps1 sim-test -Scenario setup-navigation`.
On Linux, run `bash tools/bench.sh sim-test --scenario setup-navigation` on the
Docker host. Local runs disable networking and require no Wokwi token.

## Supported local coverage

| Scenario | Checks |
| --- | --- |
| boot-calibration | Fresh boot, four-point calibration and dashboard |
| wan-hold | Short press cancellation and three-second setup hold |
| setup-navigation | Saved and Nearby navigation |
| saved-switch | Progress, Back cancellation, blocked controls, 60-second timeout, rollback and B activation |
| reboot-persistence | New worker retains flash, calibration and B; failed submission rolls back to B |

Unfiltered `sim-test` runs this supported set and lists gaps. The remaining
password, profile-editing, phone-portal, connection-flow, and dashboard-state
scenarios are not yet accepted locally. Explicit unsupported selections fail.
`all` runs the host/tooling matrix, dummy production build, local scenarios,
and isolation checks. Physical Wi-Fi/AP/NAPT and deployment acceptance remain
required; these simulations use the existing network and GX fixtures.

## Pinned runtime and artifacts

The [implementation plan](../docs/superpowers/plans/2026-09-03-velxio-local-runner.md)
builds on the [investigation](../docs/research/2026-09-03-velxio-investigation.md).
`simulation/velxio/runtime-lock.json` identifies the upstream image/revision,
Node 24.3.0, Pillow 11.3.0, and PyYAML 6.0.2. Maintained adapters live in
`tools/velxio`; their upstream license and modifications are recorded there.

DIO firmware, staging and compiler outputs are isolated from the Wokwi and
production targets. Attestation checks current source, actual DIO header,
build script/toolchain identity, runtime lock/adapters, and firmware hashes.
The worker receives only dummy inputs and writes to a per-run directory.

Scenario waits use QEMU guest time with `-icount 3`. Readiness is bounded to
30 wall seconds and scenarios to 600; stalls, crashes, missing captures and
unexpected reboots fail. Captures decode actual SPI traffic and apply only the
panel rotation. Reboot uses cleanly flushed flash from the same run and starts
a fresh worker; independent scenarios always start from the attested image.
The known startup `TWDT already initialized` warning is retained in reports.
Other watchdog errors fail. The cache-error workaround remains an emulator
limitation, and a passing scenario proves only its stated checks.

The local `wan-hold` YAML waits 1200 ms after short-press release to sample the
existing heartbeat ON phase. The Wokwi scenario retains its original timing;
both compare against the same unchanged reference image. Backend-specific YAML
files are selected through `backendFiles` in the scenario manifest.

## Captures and golden review

`build/velxio/results/<run-id>/<scenario>` retains run/result JSON, serial and
worker logs, event traffic, PNG captures and pixel diffs. The sibling `_inputs`
directory retains input and expected-image snapshots. No ordinary test changes
tracked goldens. Compare exact native 320x240 RGBA pixels; never recolor,
rescale or increase tolerance to make a mismatch pass.

Visually inspect actual captures before promoting a selected completed run:

```powershell
tools/dev.ps1 sim-update-goldens -Run RUN_ID -Scenario reboot-persistence
```

Promotion checks current identity and capture integrity, then copies recorded
images without a simulator rerun. Failed execution and missing/stale/tampered
records cannot be promoted. This command currently supports local records.

## Conserving cloud minutes

`sim-build`, `sim-test`, and `all` default to Velxio and use zero Wokwi minutes.
Use `-Backend wokwi -Scenario NAME` only for an explicitly selected comparison;
state the scenario and reason before starting. Cloud full suites require an
explicit request or agreed checkpoint and `-Backend wokwi -FullSuite`.
Wokwi's token is forwarded by environment name only. There is no cloud fallback.
`sim-build -Backend wokwi` creates the standard cloud image locally without
running it. Old cloud captures remain historical evidence for their own build.

## Verification history

The [Velxio investigation](../docs/research/2026-09-03-velxio-investigation.md)
demonstrated local ESP32 boot, calibration, press/hold, and three exact
screen matches using a separate DIO build and experimental adapters. A
navigation repeat exposed an FT6206 touch-release race in the simulation
boundary. The fix now has a host regression; two subsequent local runs
passed all three exact frame comparisons. Runner integration was pending then.
At investigation time Velxio was not wired into the commands. The maintained
runner described above supersedes that temporary harness.
Its [acceptance record](../docs/research/2026-09-03-velxio-local-runner.md)
includes saved switching and a fresh worker boot with retained calibration/B.
The standard simulator was rebuilt after this correction, but Wokwi was not
rerun. The R1 cloud results below describe the earlier build.

The bench is implemented and its original acceptance records 9 scenarios and
25 exact screenshots. See the [implementation evidence](../docs/superpowers/plans/2026-09-03-cyd-virtual-bench.md).
The later review at `6ef6c93` reran host/tooling tests and the dummy production
build, not live Wokwi. R1 now adds the `saved-switch` scenario with seven
checkpoints: A active, progress, cancellation, timeout, rollback to A, success,
and B active. The production controller host suite checks persistence and
rollback against the real store and network module. R2 remains open: uplink
loss stays Connecting. Track
corrections and physical release checks in [gateway status](../docs/README.md).

R1 verification on 2026-09-03 completed all 10 live scenarios and 32 exact
screenshot comparisons. The seven new saved-switch images were visually
reviewed before promotion; a repeat run reproduced them byte for byte. The
25 existing baselines remained unchanged. Reboot persistence is covered by
store reconstruction in the host integration suite; real reboot, Wi-Fi
authentication, and AP/NAPT availability still require physical acceptance.

The firmware is built from the same development sources as production, with
`CYD_SIMULATION` replacing only hardware/data boundaries. It does not use the
ignored production secrets. Each scenario starts a fresh simulator image and
performs four-point calibration through the real touch/gesture pipeline.

## Authoring and reviewing scenarios

Use Wokwi's documented [automation operations](https://docs.wokwi.com/wokwi-ci/automation-scenarios).
Touch coordinates are FT6206 controller coordinates, not 320x240 screen
pixels. Reuse the calibration prefix and nearby control coordinates from an
existing scenario. Leave a 300 ms release gap between keyboard taps, and allow
1 second for static screens to settle before capturing them. Transient
Connecting checkpoints use 500 ms so they precede the 1-second fake connection
completion. Holds use explicit `touch-press` and `touch-release` operations.

For Wokwi, screenshot paths are relative to the YAML file, so use
`../../build/simulation/results/<scenario>/<checkpoint>.png`. List every
checkpoint in `scenario-manifest.json` and keep each scenario's output folder
separate. The local runner uses each path's checkpoint name and writes into
its isolated run directory; it rejects unsupported operations and captures.
Review the actual pixels: a successful scenario exit proves that
the script ran, not that a tap hit the intended control.

Baseline images preserve the simulator's native 320x240 RGBA pixels, including
the production display inversion setting. Do not recolor, resize, or generate
replacement artwork. Physical panel inversion still needs hardware validation.
Local pixel mismatches retain the actual PNG and a magenta-highlighted diff
in the run directory, with expected PNGs in its `_inputs` snapshot. Wokwi
comparisons use `build/simulation/diffs`.

## Serial fixtures

Each line must end in LF (CRLF also works). Successful commands print `SIM OK`;
malformed commands print `SIM ERROR`.

| Command | Accepted fixtures |
| --- | --- |
| `SIM clock=...` | `fixed` (12:34), `morning` (08:15), `evening` (21:45), `unavailable` |
| `SIM scan=...` | `nominal`, `empty`, `failure` |
| `SIM connect=...` | `success`, `failure` |
| `SIM modbus=...` | `nominal`, `stale`, `offline`, `partial` |
| `SIM reset` | Restore all fixture defaults; does not erase profiles or calibration |

The clock also supplies fixed profile timestamps. Simulation performs no NTP
lookup. Pairing code `424242` and all network credentials are dummy fixtures.
Real AP/NAPT routing, network authentication, GX transport, and touch noise
remain physical-release checks.
