# CYD Virtual Bench Design

**Date:** 2026-09-03

## Purpose

The virtual bench makes `VictronCYD_Modbus` testable without an attached
ESP32-2432S028R. It provides two complementary feedback loops:

- Native C++ tests exercise state machines and simulator adapters quickly.
- Wokwi executes the complete ESP32 image, including the production
  `TFT_eSPI` drawing path, and validates exact 320x240 screenshots after
  scripted touch and serial input.

The Windows host needs Docker and Python only. All firmware, compiler,
library, simulator, and image-comparison dependencies are versioned by the
repository. Host-side flashing uses an ignored Python virtual environment so
physical release work does not require globally installed Arduino tooling.

## Safety and isolation

Development occurs in a linked worktree based on the approved branch tip.
The active checkout used by the other Codex task, its untracked
`docs/handoff.lnk`, and the ignored `secrets.h` files are not copied, read,
modified, staged, or committed.

The simulator build is a deny-by-default staging operation. It copies an
explicit source allow-list and writes a tracked dummy `secrets.h` only inside
the ignored simulator staging directory. The stage is scanned before compile;
neither production secret path may be present or referenced. Wokwi only
receives attested artifacts produced inside `build/simulation`.

## Reproducible toolchain

`.devcontainer/Dockerfile` pins:

- Arduino CLI 1.5.1
- Arduino-ESP32 3.3.11
- TFT_eSPI 2.5.43
- XPT2046_Touchscreen 1.4
- Adafruit FT6206 Library 1.1.1 (simulator only)
- Wokwi CLI 0.26.1
- the distro C++ compiler and Python/Pillow packages used by host tests

Downloaded standalone executables are verified against SHA-256 values kept in
the Dockerfile before installation. Arduino dependency versions are resolved
into the image during the image build and checked by the bench doctor.

`tools/dev.ps1` is the stable Windows entry point. It invokes Docker directly,
mounts the checkout at `/workspace`, and keeps Arduino data, downloads, build
outputs, Wokwi results, and host flashing packages in ignored project paths.
It exposes `doctor`, `setup`, `test`, `firmware-build`, `sim-build`, `sim-test`,
`sim-update-goldens`, `flash`, `monitor`, and `all`.

The CYD `TFT_eSPI` settings live in `config/TFT_eSPI_CYD.h` and are selected
with compiler defines. No installed library is edited in place.

## Runtime boundaries

### Raw touch

`RawTouchDevice` returns `RawTouchSample { contact, point, pressure }`.
`Xpt2046RawTouchDevice` owns the production HSPI controller on the existing CYD
pins and preserves the pressure threshold and rotation behavior. Under
`CYD_SIMULATION`, `Ft6206RawTouchDevice` reads the Wokwi FT6206 over GPIO 32/25
and deterministically maps its 320x240 display coordinates into the same raw
range expected by the existing calibration, mapping, debounce, and gesture
pipeline. `TouchInput` depends only on the boundary.

### Network

`CamperNetworkRuntime` aliases `CamperNetwork` in production and
`SimCamperNetwork` under `CYD_SIMULATION`. The simulated network has the same
public contract and deterministic fixtures for scans, connection success or
failure, RSSI, local IP, AP state, and pending-profile state. It performs no
real Wi-Fi or internet operations.

### Modbus

`ModbusCycleSource` exposes `fetch(ModbusReadCycle&)`. The production
implementation retains the current Modbus TCP transaction validation and
connection reset behavior. The simulation implementation supplies nominal,
stale, offline, and partial-data cycles. Snapshot merge and stale/offline
presentation remain shared production logic.

### Simulation control

Only simulation builds include `SimulationControl::poll()`. It consumes
newline-delimited serial commands with this complete grammar:

```text
SIM clock=<fixture>
SIM scan=<fixture>
SIM connect=<success|failure>
SIM modbus=<fixture>
SIM reset
```

Successful state changes print `SIM OK`; unknown, malformed, overlong, or
unsupported commands print `SIM ERROR`. Reset restores all deterministic
defaults. Production object files contain neither this parser nor simulator
fixtures.

## Wokwi virtual device

`simulation/diagram.json` models an ESP32 DevKit v1, ILI9341, and FT6206 touch
controller. TFT wiring matches the physical CYD display pins (MISO 12, MOSI
13, SCLK 14, CS 15, DC 2, reset unconnected, backlight 21). Simulator-only I2C
uses SDA 32 and SCL 25 so GPIO 21 remains dedicated to backlight.

The simulator build emits a merged binary, ELF, and JSON attestation with:

- a schema/version marker;
- the allow-listed source-tree hash;
- the dummy-configuration identifier;
- the build mode and output directory;
- SHA-256 values for each artifact.

The Wokwi runner recalculates and rejects every mismatch, stale source hash,
unexpected configuration identifier, missing artifact, path escape, or
artifact outside `build/simulation`. `WOKWI_CLI_TOKEN` is forwarded only to the
runtime container process and never written to an image, file, log, or command
argument. Scenarios do not use custom access points or private gateways.

## Scenarios and pixel oracle

YAML automation scenarios cover calibration, WAN hold behavior, setup
navigation, protected network selection, password keyboard behavior,
connection outcomes, saved profile deletion/clear, fixed-code phone portal,
and nominal/stale/offline dashboards.

Each checkpoint names a committed 320x240 RGB PNG in
`simulation/goldens/<scenario>/`. Ordinary `sim-test` saves actual images under
`build/simulation/results`, compares all pixels, and on failure keeps expected,
actual, and a highlighted diff. It never writes goldens.

Only `sim-update-goldens` can replace baselines. It first runs the scenario,
validates every image dimension and mode, prints the full changed-image list,
and then promotes generated images. Baseline changes remain visible in Git for
human review.

## Determinism

Simulation uses tracked dummy SSIDs, passwords, site name, GX address, fixed
time fixtures, fixed pairing code, and fixed pseudo-random values. No dummy
value resembles or derives from local production configuration.

## Verification and release boundary

`tools/dev.ps1 all` performs host tests, production compilation, simulator
compilation, source/secret isolation checks, artifact attestation checks, and
all Wokwi pixel scenarios. The Wokwi portion requires a user-provided token.

Passing the virtual bench does not replace these physical release checks:
panel inversion, resistive-touch noise and calibration feel, AP/NAPT routing,
real GX Modbus connectivity, watchdog recovery, and verified flashing without
erasing NVS.

