# Virtual CYD scenarios

Run from the repository root with `tools/dev.ps1 sim-test`. To select one
scenario, add `-Scenario password-entry` (or another manifest name).
Only `sim-update-goldens` promotes screenshots; ordinary tests never do.

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

Screenshot paths are relative to the YAML file, so use
`../../build/simulation/results/<scenario>/<checkpoint>.png`. List every
checkpoint in `scenario-manifest.json` and keep each scenario's output folder
separate. Review the actual pixels: a successful scenario exit proves that
the script ran, not that a tap hit the intended control.

Baseline images preserve the simulator's native 320x240 RGBA pixels, including
the production display inversion setting. Do not recolor, resize, or generate
replacement artwork. Physical panel inversion still needs hardware validation.
Pixel mismatches retain actual, expected, and magenta-highlighted diff images
under `build/simulation/diffs`.

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
