# ESP32-Victron-VRM-Display

A live **Victron energy-flow dashboard** on the **ESP32-2432S028R** — the cheap 2.8" touch
TFT board widely known as the **"Cheap Yellow Display" (CYD)**.

It reproduces the Victron tile view — **Grid · Inverter · AC Loads · Battery · DC Loads · Solar** —
on a board that costs ~€10.

> **This doesn't replace your GX device** — a Cerbo GX / Venus OS (or a Venus-on-RPi) is still the
> brain of the system. It replaces the *display*: a cheap, always-on wall dashboard instead of a
> ~€350 GX Touch, a Raspberry Pi + monitor, or keeping VictronConnect open on your phone.

![dashboard](docs/dashboard.jpg)

## Two versions (pick one)

| | **VictronCYD** (VRM cloud) | **VictronCYD_Modbus** (realtime) ⭐ |
|---|---|---|
| Data source | Victron **VRM API** (cloud) | **GX device** on your LAN via **Modbus TCP** |
| Refresh | ~20 s (VRM logs every ~60 s) | **~2 s, true realtime** |
| Needs internet | Yes | **No** (local network only) |
| Needs a token | Yes (VRM Personal Access Token) | **No** |
| Library deps | TFT_eSPI, ArduinoJson, StreamUtils | **TFT_eSPI, XPT2046_Touchscreen 1.4** |

**Use Modbus** if your GX is on the same network (instant, offline-capable). **Use VRM cloud**
if you want to watch a site you're not on the same network as.

## Features

- Live **Grid / AC Loads / DC Loads / Solar (PV)** power and **Inverter state**
- **Battery**: SoC %, charge bar, state, voltage / current / power, temperature
- Energy-flow connectors, Victron-style dark theme, NTP clock, WiFi indicator
- Credentials kept out of git via `secrets.h`
- Touch Wi-Fi setup supports direct masked QWERTY password entry for unknown
  protected networks, with the private phone portal available through
  **Use phone**.
- Modbus time settings: persistent 12/24-hour display and named US, Canadian,
  Mexican, and UTC timezones with automatic seasonal changes where applicable.

## Development status

The gateway, on-device keyboard, and virtual bench are implemented. Physical
acceptance remains incomplete. Saved selection now persists only after the
selected SSID associates and receives an IP address. Its progress screen offers
Back to cancel; cancellation or failure within the 60-second attempt restores
the previous active network. With no previous profile, upstream remains
disconnected and the private AP stays available. An established upstream loss
now reports red/Offline throughout automatic retries; restored association and
DHCP report amber/Validating until fresh DNS succeeds. New connection attempts
retain amber/Connecting. See the [status and acceptance record](docs/README.md) for
evidence, remaining bench/field checks, and the latest recorded firmware upload.
Continue development on `develop` following [AGENTS.md](AGENTS.md).

## Modbus time and Wi-Fi settings

Tap the dashboard's upper-left gear to open **Settings**, then **Time** or
**Wi-Fi**. Holding WAN for three seconds still opens Wi-Fi directly. Settings
and WAN entry wait until any active connection attempt finishes.

In **Time**, choose **12h** or **24h** and use **Change timezone** to select a
country and city/region. **Save** applies both choices immediately and retains
them across reboot. **Back** discards unsaved time edits; picker Back goes up
one level. Sixty seconds without interaction exits without saving. Wi-Fi Back
returns to Settings when entered through the gear, or to the dashboard when
entered through WAN; automatic exits return to the dashboard.

The default is **America/Chicago, 12-hour AM/PM**. The clock shows `--:--` until
NTP supplies a valid time; settings can still be changed offline. Timezones use
pinned IANA 2026c current/future rules, with updates delivered through firmware.
The VRM cloud sketch retains its existing time behavior.

## Hardware

- **ESP32-2432S028R** (CYD) — ILI9341 240×320 TFT
- Micro-USB cable, 2.4 GHz WiFi (the ESP32 has no 5 GHz)
- For the Modbus version: a **Victron GX device** (Cerbo GX / Venus OS) reachable on your LAN

## 1. Configure TFT_eSPI for the CYD

The repository-owned build uses `config/TFT_eSPI_CYD.h`; it never edits an
installed library. If you build manually in Arduino IDE, copy the equivalent
settings below into `User_Setup.h` (or a selected custom setup):

```c
#define ILI9341_2_DRIVER        // if the screen stays blank, try #define ILI9341_DRIVER
#define TFT_WIDTH  240
#define TFT_HEIGHT 320
#define TFT_MISO 12
#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_CS   15
#define TFT_DC    2
#define TFT_RST  -1
#define TFT_BL   21
#define TFT_BACKLIGHT_ON HIGH
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT
#define SPI_FREQUENCY       55000000
#define SPI_READ_FREQUENCY  20000000
#define SPI_TOUCH_FREQUENCY  2500000
```

> **Colors inverted / black shows as white?** Some CYD panels are inverted. Both sketches call
> `tft.invertDisplay(true)`. If your colors look wrong, change it to `false`.

## 2a. Realtime version (VictronCYD_Modbus) — recommended

1. On the GX: **Settings → Services → Modbus TCP = ON**. Note the GX IP address
   (Settings → Ethernet/WiFi).
2. Copy `VictronCYD_Modbus/secrets.example.h` to the ignored
   `VictronCYD_Modbus/secrets.h`. Set the private AP SSID and a 12–63-byte AP
   password, `SECRET_GX_IP`, and `SECRET_SITE_NAME`. The GX must be reachable
   from the private AP network; its migration remains a field acceptance task.
3. Build the reviewed source with this private configuration and the CYD TFT
   settings. For bench validation, use a verified upload that preserves NVS.
4. Hold WAN for three seconds to open Network Setup. Select an upstream from
   Nearby; unknown protected networks open masked password entry. **Use phone**
   starts the private portal fallback. Upstream credentials are saved after
   successful association/DHCP; they are not compile-time settings.

Follow the [remaining acceptance checks](docs/README.md#remaining-task-9-acceptance)
before deployment. Saved-switch and WAN-outage hardware acceptance remain pending.

**GX Modbus register map used** (function 3, read holding registers):

| Value | Unit id | Register | Scale |
|---|---|---|---|
| AC Loads (W) | 100 | 817 | ×1 |
| Grid (W) | 100 | 820 | ×1, signed |
| Battery voltage | 100 | 840 | ÷10 |
| Battery current | 100 | 841 | ÷10, signed |
| Battery power (W) | 100 | 842 | ×1, signed |
| Battery SoC (%) | 100 | 843 | ×1 |
| Battery state | 100 | 844 | 0 idle / 1 charging / 2 discharging |
| PV power (W) | 100 | 850 | ×1 |
| DC system (W) | 100 | 860 | ×1, signed |
| VE.Bus state | 228 | 31 | enum |
| Battery temperature | 225 | 262 | ÷10 |

> Unit ids can differ per installation (especially 225/228). If battery temp or inverter state
> read wrong, check your GX's CCGX Modbus-TCP register list for the right unit ids.

## 2b. VRM cloud version (VictronCYD)

1. **VRM Personal Access Token** — VRM Portal → *Preferences → Integrations → Access tokens*.
2. **Installation id (`idSite`)** — in the VRM URL, or:
   `GET https://vrmapi.victronenergy.com/v2/users/{idUser}/installations`
   with header `x-authorization: Token <token>`.
3. `cd VictronCYD && cp secrets.example.h secrets.h`, fill in the values.

## 3. Build & flash

**Arduino IDE:** open the `.ino`, select *ESP32 Dev Module*, Upload.

**arduino-cli:**
```bash
arduino-cli compile --fqbn esp32:esp32:esp32 VictronCYD_Modbus
arduino-cli upload  --fqbn esp32:esp32:esp32 -p COM3 VictronCYD_Modbus   # your port
```

The camper gateway variant requires Arduino-ESP32 3.3.11 and XPT2046_Touchscreen 1.4. The ESP32 core version is required for the supported `WiFi.AP.enableNAPT()` API.

## CYD Virtual Bench

The virtual bench runs the Modbus firmware without a physical CYD. Native C++
tests cover state and policy quickly; the local Velxio runner executes the ESP32 image,
`TFT_eSPI` drawing code, FT6206-backed scripted touch input, and exact 320x240
golden screenshot comparisons. Wokwi remains available for explicit targeted comparisons.

### Prerequisites and setup

Install Docker Desktop and Python 3.11 or newer on Windows, then run from the repository
root:

```powershell
tools/dev.ps1 setup
tools/dev.ps1 doctor
```

`setup` builds a pinned Docker image containing Arduino CLI 1.5.1,
Arduino-ESP32 3.3.11, TFT_eSPI 2.5.43, XPT2046_Touchscreen 1.4, Adafruit FT6206
1.1.1, Pillow 11.3.0, and Wokwi CLI 0.26.1. Downloaded Arduino and Wokwi CLI
executables are SHA-256 verified. The optional `.devcontainer` uses this same
image definition, but neither the Dev Containers CLI nor a VS Code extension is
required.

Host flashing dependencies are pinned inside ignored `.tools/venv`. Builds,
caches, source staging, simulator results, and pixel diffs are also ignored.
Docker's own image storage remains Docker-managed.

### Commands

The [local bench plan](docs/superpowers/plans/2026-09-03-velxio-local-runner.md)
makes Velxio the default simulator. Setup caches the pinned Arduino toolchain,
Velxio runtime, Node 24.3.0, Pillow 11.3.0, and PyYAML 6.0.2. Subsequent local
checks run offline without a Wokwi token.

```powershell
tools/dev.ps1 setup
tools/dev.ps1 doctor
tools/dev.ps1 test
tools/dev.ps1 firmware-build
tools/dev.ps1 sim-build
tools/dev.ps1 sim-test -Scenario setup-navigation
```

`all` runs the host/tooling matrix, dummy production build, supported local
scenarios, and isolation checks. Unfiltered `sim-test` lists its supported
scenarios and coverage gaps. Supported local scenarios are `boot-calibration`,
`wan-hold`, `setup-navigation`, `saved-switch`, and `reboot-persistence`.
Other explicit local selections fail; they never trigger cloud execution.
On Linux use `bash tools/bench.sh` with the same commands and lower-case flags
such as `--scenario setup-navigation`, directly on the Docker host.

`firmware-build` is a production-mode smoke compile using generated dummy
settings; it does not consume real secrets. The normal unstaged sketch still
uses its ignored `VictronCYD_Modbus/secrets.h`.

`sim-build` stages the source allowlist and creates the separate DIO firmware
in `build/velxio`. Attestation binds source, build configuration, actual flash
mode, runtime/adapters, and artifacts. Wokwi builds remain in `build/simulation`.

Wokwi requires an explicit selection and a token supplied only in the process
environment. State which scenario is needed and why before executing it:

```powershell
tools/dev.ps1 sim-test -Backend wokwi -Scenario password-entry
```

`-Backend wokwi -FullSuite` is reserved for an explicitly requested cloud suite
or agreed release checkpoint. No local command silently falls back to Wokwi.

### Golden screenshot review

Runs retain their identity, serial logs, actual 320x240 RGBA captures, and
comparisons under `build/velxio/results/<run-id>/<scenario>`. Pixel mismatches
produce `<checkpoint>.diff.png`; the run's input snapshot retains the expected
image. Ordinary tests never modify goldens.

After visually reviewing the actual images, promote only the intended run:

```powershell
tools/dev.ps1 sim-update-goldens -Run RUN_ID -Scenario reboot-persistence
git diff --stat -- simulation/goldens
```

Promotion validates current source/runtime/scenario identity and capture hashes.
It copies the recorded captures without rerunning the simulator. Missing,
incomplete, stale, or tampered records are rejected.

### Physical release

Virtual tests do not replace a final hardware pass. For a **dummy-config
hardware smoke test**, build, flash, and monitor with the repo-local tools:

```powershell
tools/dev.ps1 firmware-build
tools/dev.ps1 flash -Port COM3
tools/dev.ps1 monitor -Port COM3
```

`flash` rebuilds the dummy production image and writes separate bootloader,
partition-table, OTA-initialization, and application segments; it never flashes
the merged image over NVS. This assumes the pinned Arduino default partition
layout. Existing saved Wi-Fi profiles and touch calibration remain in NVS.
For a real deployment, use the normal sketch with the ignored production
`secrets.h` and the repository-owned TFT configuration; the dummy smoke image
is not a real-GX release image.

Before releasing, verify display inversion on the actual panel, resistive-touch
noise and calibration feel, private AP/NAPT routing, real GX connectivity,
watchdog recovery, and a verified flash that does not erase NVS.

---

## Technical notes (VRM cloud version)

Reading the VRM **`/diagnostics`** endpoint on an ESP32 is the tricky bit — ~129 KB of JSON over
HTTPS. Three things are needed together:

1. **`http.useHTTP10(true)`** — otherwise the response is *chunked* and ArduinoJson reads the
   first hex chunk-size as a JSON number, "succeeds", and returns **0 records**.
2. **ArduinoJson `Filter`** — the full body won't fit in RAM; the filter keeps only the ~12 codes
   needed while streaming.
3. **A blocking `Stream` wrapper** — the default `Stream::read()` is non-blocking and returns -1
   between TLS records, so ArduinoJson stops at the first ~16 KB. The `BlockingStream` waits for
   more bytes until the connection truly closes.

Auth header: **`x-authorization: Token <PAT>`** (`Bearer` returns 401). VRM diagnostics codes:
`g1` grid, `a1` AC loads, `dc` DC, `bv`/`bc`/`bp` battery V/A/W, `bs` SoC, `bT` temp,
`bst` battery state, `ss` system state, `PVP` PV.

## Notes (Modbus version)

The Modbus client keeps one TCP connection open. To avoid the buffer desync that can freeze the
display, `mbRead()` drains stale bytes before each request, validates the response transaction id,
and **closes the socket on any error** so the next read reconnects and re-syncs automatically.

For an always-on display, a hardware task watchdog reboots the board if the main loop hangs for
more than 30 seconds. Loss of GX data is reported as stale/offline and does not intentionally
reboot the board, so Network Setup and the private AP remain available during an outage.

## Credits

Built for the ESP32-2432S028R "Cheap Yellow Display". Uses the
[Victron VRM API](https://vrm-api-docs.victronenergy.com/) and the GX Modbus-TCP interface.
Not affiliated with Victron Energy.

## License

MIT — see [LICENSE](LICENSE).
