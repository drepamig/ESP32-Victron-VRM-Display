# Velxio investigation — 2026-09-03

## Recommendation

Velxio is a viable candidate for a quota-free local graphical test backend.
The simulation-only FT6206 release race found during investigation is now
fixed and covered by a host regression. Two consecutive local runs passed
calibration, short press, hold, and Nearby navigation; all six comparisons
against existing Wokwi goldens were exact. This uses an experimental touch
adapter, virtual-clock timing, and two display-decoder fixes. A supported
runner and broader scenario coverage still need implementation.

The user adopted this direction on 2026-09-03. The
[approved repo plan](../superpowers/plans/2026-09-03-cyd-virtual-bench.md#approved-testing-strategy--2026-09-03)
selects Velxio for routine local simulation alongside host tests, reserves
Wokwi for targeted comparisons, and retains physical hardware acceptance.
Runner integration is the next tooling task. This investigation consumed
**zero Wokwi minutes**.

## Version and isolation

- Repository: [davidmonterocrespo24/velxio](https://github.com/davidmonterocrespo24/velxio).
- Source inspected: `77ee0cf96aff18d0b4b10d0e77b08c72bee4c1e0`.
- Public Linux/amd64 image:
  `ghcr.io/davidmonterocrespo24/velxio@sha256:117a82cc52ec7168b790bc8553c68fb1fcd86a16db202b847f948f7a573691d2`.
- Image revision label matches the inspected source; creation label is
  `2026-09-03T17:07:36.331Z`. Registry layers total 1,533,971,799 compressed
  bytes, approximately 1.5 GB.
- Ran locally through Docker on Windows, with `--network none`, no published
  ports, and temporary containers removed after execution. The normal web
  service/first-start installer was not launched.
- Only ignored investigation files and the staged dummy simulator build were
  exposed to the container. Real configuration and credentials were not used.
- Initial probes left firmware, tests, and goldens unchanged. The later
  FT6206 correction changes only the simulator touch path, adds a host
  regression, and rebuilds the standard simulator artifact. DIO experiments
  have separate output directories; all existing goldens remain unchanged.

The [project README](https://github.com/davidmonterocrespo24/velxio/blob/77ee0cf96aff18d0b4b10d0e77b08c72bee4c1e0/README.md)
describes the self-hosted project as free and open source under AGPLv3, with
an optional commercial license. No account, license key, or paid service was
needed for these offline runs of the public image. Building the image from
source has an additional runtime dependency: the
[QEMU build guide](https://github.com/davidmonterocrespo24/velxio/blob/77ee0cf96aff18d0b4b10d0e77b08c72bee4c1e0/docs/BUILD-QEMU.md)
documents its patched fork and corresponding-source download, including a
free personal key route. Unmodified upstream QEMU is not an equivalent
runtime. No license signup or custom QEMU build was attempted.

## Compatibility findings

| Requirement | Finding |
| --- | --- |
| Classic ESP32 / Xtensa LX6 | Supported by the `esp32-picsimlab` machine; exercised locally. |
| Current Arduino ESP32 3.3.11 application | Boots with `FlashMode=dio`. The existing default-target image fails during flash initialization. No Arduino-core downgrade or application-source patch was needed for the DIO probe. |
| Prebuilt firmware | The worker accepts a base64 merged flash image and pads supported flash sizes. Our 4 MB merged images were supplied directly; Velxio's compiler was not used. |
| ILI9341 display | The actual decoder produced a readable dashboard. With experimental inversion and RAMWR cursor fixes, calibration, Saved, and Nearby frames match existing goldens exactly. Full browser rendering and other screens remain unverified. |
| FT6206 capacitive touch | Missing from the inspected part implementation. `ili9341-cap-touch` registers the same SPI-only model as `ili9341`; it does not attach an FT6206 I2C device or touch events. |
| Input adapter route | An isolated adapter drives FT6206 at `0x38` on bus 1 through the existing register proxy. The discovered release race is fixed; calibration, press/hold, and navigation passed two consecutive local runs. |
| Display colors | The upstream decoder ignores inversion and the MADCTL color-order bit. Handling inversion in RGB565 before expansion matches the three tested Wokwi frames without recoloring captures or changing goldens. Other display modes are unverified. |
| Serial fixtures | Sending `SIM clock=evening` through the worker UART API returned `SIM OK`. |
| Wokwi project import | Source supports Wokwi diagram/ZIP import. This does not establish support for our Wokwi CI automation YAML. |
| Automated scenarios/screenshots | A throwaway worker harness drives calibration/navigation and replays real SPI events for exact PNG comparison. Two post-fix runs passed all three image comparisons. It is not yet a runner for our YAML scenarios. |
| Timing and reboot persistence | Experimental `-icount 3` plus QEMU virtual-clock waits exercise a 3.3-second hold. Wall-clock waits alone were inadequate. Calibration values were found in a captured flash image; persistence across worker restarts and long timeouts remain unverified. |
| Real Wi-Fi / AP / NAPT / GX behavior | Not tested. These runs use our existing simulated boundaries and do not replace physical acceptance. |

The pinned [ESP32 guide](https://github.com/davidmonterocrespo24/velxio/blob/77ee0cf96aff18d0b4b10d0e77b08c72bee4c1e0/docs/ESP32_EMULATION.md)
and Dockerfile describe Arduino ESP32 3.3.10 / IDF 5.5.4. A search-engine
copy initially showed obsolete 2.0.17 guidance; the pinned source and actual
image were used for conclusions.

Primary implementation references:

- [Worker and JSON command/event protocol](https://github.com/davidmonterocrespo24/velxio/blob/77ee0cf96aff18d0b4b10d0e77b08c72bee4c1e0/backend/app/services/esp32_worker.py).
- [ILI9341 decoder and capacitive-touch alias](https://github.com/davidmonterocrespo24/velxio/blob/77ee0cf96aff18d0b4b10d0e77b08c72bee4c1e0/frontend/src/simulation/parts/ComplexParts.ts#L943).
- [I2C proxy](https://github.com/davidmonterocrespo24/velxio/blob/77ee0cf96aff18d0b4b10d0e77b08c72bee4c1e0/backend/app/services/esp32_i2c_slaves.py).
- [WebSocket routes](https://github.com/davidmonterocrespo24/velxio/blob/77ee0cf96aff18d0b4b10d0e77b08c72bee4c1e0/backend/app/api/routes/simulation.py)
  and [project import](https://github.com/davidmonterocrespo24/velxio/blob/77ee0cf96aff18d0b4b10d0e77b08c72bee4c1e0/frontend/src/utils/importProject.ts).

## Measured results

### Existing attested image

`build/simulation/firmware.bin` was verified with
`python tools/sim_artifacts.py verify` before execution.

- Size: 4,194,304 bytes.
- SHA-256: `6a3dd10b813cfae46c30d843b580b19620cb042108db60b9eb6714a340be21f1`.
- Probe duration: 35.12 seconds.
- No `SIM READY`, no fixture acknowledgement, and no SPI bytes.
- Serial repeatedly reported `Failed to set QIE bit` followed by
  `assert failed: __esp_system_init_fn_init_flash ... (flash_ret == ESP_OK)`
  and rebooted. This happened before application setup.

Velxio's worker reported a generic `booted` event when QEMU started and did
not classify this assertion as a `crash` event. A future runner must require
application readiness and independently inspect serial output for assertions,
panics, and unexpected reboots; process exit or the worker event alone is
insufficient.

### Separate DIO build

Built from a copy of the already-staged simulator source with the existing
`victron-cyd-virtual-bench:2026-09-03` toolchain and Arduino core 3.3.11. The
functional change to build configuration was:

```text
--fqbn esp32:esp32:esp32:FlashMode=dio
```

The same `CYD_SIMULATION` and repository TFT setup defines were retained.
Outputs went to `build/velxio-investigation/dio-firmware`, separate from the
attested Wokwi image. Arduino selects DIO-specific SDK libraries and
bootloader with this option; changing a binary header alone is not the same
experiment.

- Build: 559,652 bytes flash; 34,516 bytes globals.
- Merged image: 4,194,304 bytes.
- SHA-256: `0854fbebd2bb268d93d1bf66be288f4f3873195ee326fd7d10fd06ddd8324979`.
- Reached `SIM READY` and acknowledged `SIM clock=evening` within the
  3.88-second probe.
- Captured 556,969 SPI bytes in 4,514 batches.
- No assertions, Guru Meditation, or reboots appeared in this short probe.
- Serial did contain startup watchdog messages: `TWDT already initialized`
  and `task not found`. Their significance and longer-run stability remain
  unverified; this is a boot/serial/display smoke result, not a clean full
  scenario pass.

The second run stopped after the fixture acknowledgement. It did not verify
the subsequent evening-clock redraw, touch input, saved-network switching,
or long-running timing behavior.

### Display inspection

Replayed the captured SPI and GPIO events through the **unmodified**
`ili9341Simulation` object extracted from the pinned `ComplexParts.ts`.
Node 24 ran the decoder with a minimal canvas-buffer harness. The resulting
RGBA bytes were saved directly as PNG and visually inspected: dashboard text,
cards, and outlines are readable, while colors differ from the Wokwi
baseline. No recoloring, resizing, or golden promotion was performed.

[Inspected decoder output](velxio-2026-09-03/display.png) is the native
240×320 panel buffer. It appears sideways because the browser's configured
panel rotation was not applied. This is a render from real emulator traffic,
not a screenshot of the full Velxio browser application.

## Initial follow-up test: calibration, hold, and navigation

The follow-up used the same pinned runtime and separate DIO firmware, with
no application-source changes. A throwaway wrapper adds instruction counting
and a virtual-clock query to the worker. It registers the existing I2C proxy
after the worker starts and before the application initializes touch. The
proxy supplies the FT6206 vendor/chip registers and touch-coordinate frames.

Initial touch-enabled runs without instruction counting hit a cache-error
panic in the idle path. `-icount 3` avoided that panic in the measured
gesture runs; this is a tested runtime configuration, not proof that the
underlying emulator issue is fixed. The `TWDT already initialized` startup
message remains. Host-time sleeps also produced incomplete gestures, so the
final probe waits against QEMU virtual time: 200 ms press, 300 ms release,
3,300 ms WAN hold, and 1,000 ms static-screen settling.

The test drives all four calibration targets, checks a short WAN press,
holds WAN to enter Saved, and taps Nearby. A 36.463-second wall-time run
reached 11,457.443 ms of guest time, with `SIM READY` and a serial fixture
acknowledgement. Calibration values were retained in its flash snapshot:
`minx=501`, `maxx=3498`, `miny=425`, `maxy=3574`, with swap/inversion false.
This does not yet prove persistence through a simulated reboot.

### Display comparison

Two changes to the extracted decoder were necessary:

1. Honor ILI9341 inversion commands `0x20`/`0x21`, applying inversion to the
   RGB565 value before expansion.
2. Reset the write cursor to the address-window origin on every RAMWR
   (`0x2c`). Without this, cached address windows lost two calibration-circle
   pixels and four Nearby lock-icon pixels.

The resulting captures apply only the diagram's 90-degree panel rotation;
they are not recolored, resized, or generated artwork. All three actual
screens were visually inspected. Existing goldens were not changed.

| Checkpoint | Exact RGBA comparison against existing Wokwi golden |
| --- | --- |
| [Calibration](velxio-2026-09-03-touch/calibration.png) | 0 / 76,800 pixels differ from `boot-calibration/boot.png`. |
| [Saved](velxio-2026-09-03-touch/saved.png) | 0 / 76,800 pixels differ from `setup-navigation/saved.png`. |
| [Nearby](velxio-2026-09-03-touch/nearby.png) | 0 / 76,800 pixels differ from `setup-navigation/nearby.png`. |

These are renders from actual emulator SPI traffic through the patched
decoder, not screenshots of Velxio's complete browser UI. The decoder fixes
remain experimental and are not installed into the upstream image.

### Navigation repeatability before the fix: failed

A fresh 39.567-second run again matched Calibration and Saved, but the final
Nearby checkpoint showed the [dashboard](velxio-2026-09-03-touch/repeat-nearby.png):
69,815 / 76,800 pixels differed. The worker still exited successfully and
acknowledged the serial fixture, demonstrating why image assertions are
required. Both measured runs and their comparisons are recorded in
[results.json](velxio-2026-09-03-touch/results.json).

The I2C trace shows `touched()` reading one contact, a release updating the
proxy, and the subsequent coordinate frame reporting zero contacts. In
`Ft6206RawTouchDevice::sample()`, the repository calls `getPoint()` after
`touched()` but then normalizes it with unconditional `contact=true`.
Adafruit's driver returns `(0,0,0)` when the coordinate frame has no contact.
Ignoring `point.z` converts that release to raw `(200,200)` and calibrated
screen `(20,20)`, inside the Back button.

A deterministic host reproduction compiles the actual repository
`RawTouchDevice.cpp` and installed `Adafruit_FT6206.cpp`, replacing only the
I2C boundary. It first proves an ordinary contact, then reports one contact
to `touched()` and zero to the coordinate read. It exits 1 with:

```text
steady contact=1 mapped=254,20
release race: contact=1 pressure=1000 raw=200,200 mapped=20,20 hitsBack=1
FAIL: coordinate frame reports release but RawTouchDevice reports contact
```

The [reproduction output](velxio-2026-09-03-touch/release-race.log) and
[I2C trace excerpt](velxio-2026-09-03-touch/release-trace.log) retain the
evidence. This defect is in the `CYD_SIMULATION` FT6206 path; the physical
CYD uses XPT2046 and has not been shown to have this defect. These initial
probes did not change firmware; the subsequent correction is recorded below.

## FT6206 correction and local retest

`Ft6206RawTouchDevice::sample()` now passes `point.z > 0` to the normalizer,
so a released coordinate frame yields `contact=false` and zero pressure.
The physical XPT2046 path is unchanged. The new
`tests/host/ft6206_raw_touch_device_test.cpp` exercises the real adapter with
driver-boundary fakes and is registered in `tools/run-host-tests.sh`. It
covers the two-read release race, ordinary release with stale coordinates,
and a subsequent valid press at `(0,0)`.

The regression failed on the original code with
`release between touched and getPoint must not create a press`, then passed
after the fix. The separate reproduction with the actual Adafruit driver
also changed from exit 1 to exit 0: the raced sample is now `contact=0`,
`pressure=0`, `raw=0,0`. Its printed coordinate-only `hitsBack=1` remains
irrelevant because there is no contact to deliver to the UI.

Verification after the correction:

- All 17 C++ host suites and 14 Python tooling tests passed.
- Dummy production smoke build passed: 1,038,650 bytes flash, 49,524 bytes
  globals. Standard simulator build passed: 559,644 bytes flash, 34,516 bytes
  globals. Attestation and production/simulator isolation checks passed.
  The standard builds retain the documented TFT_eSPI `TOUCH_CS` warnings.
- Separate DIO build passed: 559,660 bytes flash, 34,516 bytes globals;
  merged image SHA-256
  `74780bcc9e143a473f7cc17bf8f50533d1504d0658b2f24b3b810786d9ae85f2`.
- Two consecutive local navigation runs completed in 39.491 and 37.717
  seconds wall time, reaching 11,389.102 and 11,269.107 ms guest time. Each
  reached `SIM READY`, acknowledged the fixture, and exactly matched all
  three Calibration/Saved/Nearby goldens (zero differing pixels).
- The short-press frame equals the preceding dashboard in both runs, and
  each Nearby frame after release equals its later settled frame. Actual
  dashboard, Saved, and Nearby-after-release captures were visually reviewed.
- Independent scoped review found no issues with the fix or regression.

[Post-fix evidence](velxio-2026-09-03-touch-fix/results.json) retains both
run comparisons, hashes, serial logs, and before/after driver results. The
[Nearby capture](velxio-2026-09-03-touch-fix/nearby.png) is identical across
both runs. Original failing-run evidence remains intact. The rebuilt
standard simulator image was **not** rerun through Wokwi; earlier R1 cloud
captures belong to the earlier build. No goldens were promoted.

These two runs verify this narrow navigation sequence, not every emulator
timing condition. The startup watchdog warning and experimental runtime
configuration described above remain integration considerations.

## Next test and integration boundary

The release fix and targeted navigation retest are complete. Next, integrate
an explicitly local runner with pinned runtime/build inputs, serial failure
checks, virtual-time gestures, and image assertions. Then exercise
saved-switch progress/cancellation, timeout timing, and NVS-preserving
simulated reboot. Keep simulator integration separate from R1 and R2.

Three exact frame matches establish a limited shared-baseline proof; the
remaining screens and scenarios still need verification.
The current `tools/dev.ps1 sim-test` still invokes Wokwi and consumes quota.
This investigation does not install a replacement runner or change that
command's meaning.

## Local evidence and boundaries

The ignored `build/velxio-investigation/` directory contains the pinned
source extracts, image manifest/pull log, `boot_probe.py`, DIO build script
and log, both firmware inputs, and event/serial/stderr logs. Each runtime
probe starts `/app/app/services/esp32_worker.py` directly with
`machine=esp32-picsimlab`, the merged image encoded in `firmware_b64`,
`wifi_enabled=false`, and an empty sensor list. The first run is in
`evidence/`; the DIO run is in `evidence-dio/`. Small result records are
[retained with this report](velxio-2026-09-03/).

Follow-up harnesses are `touch_probe.py`, `touch_worker.py`,
`prepare_touch_replay.py`, `compare_touch.py`, and `release-race/` in that
ignored directory. Raw passing-capture events are in `touch-guest-time-run2/`,
the corrected replay is in `touch-corrected-replay/`, and the failed repeat
is in `touch-repeat-run/`. The trace excerpt comes from the earlier failing
`touch-guest-time-run1/`. Small follow-up results and actual images are
[retained separately](velxio-2026-09-03-touch/); large event streams, flash
snapshots, and throwaway harnesses remain local build artifacts.

The post-fix experiment uses `compile-stage-fixed/`, `build_dio_fixed.sh`,
`dio-fixed-firmware/`, and `touch_fixed_probe.py`. Runtime and decoder
settings are unchanged from the earlier test. Its raw captures are in
`touch-fixed-run1/` and `touch-fixed-run2/`; `release-race/run-fixed.sh`
repeats the real-driver reproduction with the current source. The initial
probe inputs and outputs were preserved.

No Wokwi execution, firmware flashing, Venus changes, push, or commit was
performed for this investigation. The Docker image remains cached for later
local work; no Velxio service was left running. R1 and physical acceptance
status are unchanged.
