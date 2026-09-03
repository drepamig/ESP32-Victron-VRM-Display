# Linux setup and verification

Reconciled 2026-09-03. These commands use the current repository layout and
test matrix. See [status and acceptance](../../README.md) for open findings
and the distinction between build verification and physical acceptance.

## Preferred Docker verification

Run from the repository root with a working Docker installation. The image
contains the pinned toolchain and Python dependencies; no production secrets
or Wokwi token are needed for these checks.

```bash
docker build --tag victron-cyd-virtual-bench:2026-09-03 \
  --file .devcontainer/Dockerfile .

docker run --rm --init --volume "$PWD:/workspace" --workdir /workspace \
  victron-cyd-virtual-bench:2026-09-03 bash tools/bench.sh doctor

docker run --rm --init --volume "$PWD:/workspace" --workdir /workspace \
  victron-cyd-virtual-bench:2026-09-03 bash tools/bench.sh test

docker run --rm --init --volume "$PWD:/workspace" --workdir /workspace \
  victron-cyd-virtual-bench:2026-09-03 bash tools/bench.sh firmware-build
```

`test` runs the complete C++ and Python matrices (15 and 14 at `6ef6c93`).
`firmware-build` always stages generated dummy production settings, uses
`config/TFT_eSPI_CYD.h`, and writes to `build/firmware`. Its output is a smoke
image, not a privately configured deployment image.

For simulator builds, use the same Docker invocation with `sim-build`. For
live scenarios, make the token available in the calling process environment
and pass its name only:

```bash
docker run --rm --init --env WOKWI_CLI_TOKEN \
  --volume "$PWD:/workspace" --workdir /workspace \
  victron-cyd-virtual-bench:2026-09-03 bash tools/bench.sh sim-test
```

Ordinary simulation tests do not update goldens. See the
[scenario guide](../../../simulation/README.md) for pixel review and the
limitations of simulated networks and GX data.

## Optional native toolchain

Install Arduino CLI 1.5.1, a C++17 compiler, Git, and the pinned ESP32 dependencies:

```bash
arduino-cli core update-index \
  --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core install esp32:esp32@3.3.11 \
  --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli lib install "TFT_eSPI@2.5.43"
arduino-cli lib install "XPT2046_Touchscreen@1.4"
arduino-cli core list
arduino-cli lib list
```

Use the repository-owned TFT configuration via the native compile command
below. The XPT2046 controller uses its own SPI bus and library; the
TFT_eSPI `TOUCH_CS` warning is therefore expected for this project.

If the serial device is permission-denied, use the serial-access group or udev
rule appropriate for the Linux distribution, then log out and back in. Do not
run the desktop app or the whole development workflow as root.

## Repository and secret checks

Work on `develop` unless the user explicitly instructs otherwise. Verify the
current branch before continuing and follow the root `AGENTS.md` if it differs.

```bash
git branch --show-current
git status --short --branch
git log --oneline --decorate -12
git merge-base --is-ancestor a6827e6e92db1870f70ccacd73f8d2b0cf4d5a20 HEAD
git remote -v

git check-ignore -v VictronCYD_Modbus/secrets.h
```

Docker verification does not require a secrets file. For a privately
configured native build, create it only if absent:

```bash
cp -n VictronCYD_Modbus/secrets.example.h VictronCYD_Modbus/secrets.h
```

Populate it locally or transfer it privately. Never print it while recording
terminal output, and do not overwrite an existing deployment configuration.

## Native host suites

Run from the repository root:

```bash
bash tools/run-host-tests.sh
```

Use this script instead of copying an older per-file matrix: it includes
`RawTouchDevice.cpp`, the credential dependencies, and all four simulator
suites. It compiles C++17 with warnings treated as errors and stops on failure.
For the Python tooling tests, use the pinned Docker environment above or a
native Python environment with Pillow 11.3.0:

```bash
python3 -m unittest discover -s tests/tools -p 'test_*.py' -v
```

### Touch Wi-Fi setup

Selecting an unknown protected network opens the on-device keyboard. Passwords
are masked by default; use `Show` only when visual confirmation is needed.
`Connect` becomes available after a valid password is entered. If a connection
attempt fails, the keyboard returns with the password still masked so it can be
corrected. Select `Use phone` to clear the local entry and use the private
phone portal fallback instead.

## Privately configured native firmware build

This path consumes the ignored real `VictronCYD_Modbus/secrets.h`. It is
separate from the dummy production smoke build. Use a checkout path without
spaces for the compiler's forced-include flag below.

```bash
arduino-cli compile \
  --warnings all \
  --fqbn esp32:esp32:esp32 \
  --build-path build/linux-review \
  --build-property "compiler.c.extra_flags=-include $(pwd)/config/TFT_eSPI_CYD.h" \
  --build-property "compiler.cpp.extra_flags=-include $(pwd)/config/TFT_eSPI_CYD.h" \
  VictronCYD_Modbus
```

Before claiming a reproducible baseline, also run:

```bash
git diff --check
git status --short --branch
```

## Optional hardware upload

The latest recorded verified upload is `a6827e6`; that historical entry does
not identify the device currently connected. Resolve the open findings and
review/verify the intended source before release. Upload only when the board
is connected and an upload has been requested. The command below uses the
privately configured `build/linux-review` image, not the dummy smoke output.

```bash
arduino-cli board list

# Replace /dev/ttyUSB0 with the detected port.
arduino-cli upload \
  --port /dev/ttyUSB0 \
  --fqbn esp32:esp32:esp32 \
  --build-path build/linux-review \
  --verify \
  VictronCYD_Modbus
```

An ordinary upload must not erase NVS at `0x9000..0xDFFF`.
