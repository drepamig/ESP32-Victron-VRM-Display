# Linux continuation handoff

This bundle is the portable continuation record for the camper gateway work on
branch `codex/esp32-venus-starlink-touch-bridge`.

The firmware currently installed on the bench ESP32 was built from commit
`1d5f95cb10aeee5b76dd7c4b1c9081cf9260231e`. The handoff documentation was
committed afterward, so cloning the latest branch tip does not mean a newer
firmware image has been flashed.

## Project outcome

The ESP32-2432S028R runs:

- a persistent private `Camper-Victron` AP at `192.168.50.1/24`;
- outbound NAPT through one explicitly selected upstream Wi-Fi profile;
- a realtime Victron Modbus dashboard whose GX and WAN health are independent;
- XPT2046 resistive-touch calibration stored separately in NVS;
- a physically activated, time-limited credential portal for secured upstream
  networks; and
- touch management for up to five saved upstream profiles.

Do not modify Venus OS files, add inbound forwarding, log credentials, or let
the firmware silently roam to a stronger saved profile.

## Completed development

Implementation Tasks 1 through 8 are complete and reviewed. Task 9 bench work
has validated all of the following on physical hardware:

- the private AP starts without an upstream network and the dashboard remains
  stable while GX is unavailable;
- touch calibration renders without dashboard overlays and persists across
  reboot;
- holding WAN shows centered `HOLD 3`, `HOLD 2`, `HOLD 1` feedback and opens
  Network Setup automatically at three seconds;
- early release and slide cancellation restore a clean normal header;
- Network Setup no longer performs a whole-screen repaint every second;
- Saved/Nearby navigation, five-row scan rendering, Previous/Next pagination,
  and `Back` navigation work; and
- the bench-emulated upstream appears with the expected signal-strength data.

Several physical-display findings were corrected through host-tested,
production-used coordinators. The final correction at firmware commit
`1d5f95c` paints the `Scanning` setup state before starting Wi-Fi scan
initialization while retaining immediate WAN slide-cancel restoration.

## Current physical checkpoint

Firmware `1d5f95c` is flashed and hash-verified on the ESP32. Its upload wrote
the bootloader, partition table, boot app, and application without erasing NVS
at `0x9000..0xDFFF`, so saved touch calibration and any profiles were preserved.

The very next work is observation only:

1. Keep the bench-emulated upstream available, but do not select it yet.
2. Hold WAN to open Network Setup.
3. Tap `Nearby` once and verify `Scanning...` appears immediately, before scan
   results arrive.
4. Tap `Refresh` once and verify `Scanning...` again appears immediately.
5. Return with `Back`, begin a WAN hold, slide outside the WAN target before
   three seconds, and verify the countdown/highlight cancels and the normal
   header restores immediately.
6. Record the observations exactly. Do not mark an unperformed check as passed.

Only after all three feedback checks pass should upstream provisioning resume.

## Remaining Task 9 work

Use the user-controlled bench upstream in place of real Starlink during this
phase. Ask the user for its current SSID when needed; do not add that SSID or
its password to tracked files or logs.

1. Select the unknown secured upstream and verify the display shows the
   time-limited setup URL and single-use code.
2. From a phone joined to `Camper-Victron`, submit the code and password and
   verify WAN progresses through connecting/validating to online.
3. Join a laptop to `Camper-Victron`; verify `192.168.50.1`, DNS resolution,
   and outbound HTTPS through NAPT.
4. Try deliberately bad credentials and prove the previously active profile is
   retained and the bad password is absent from output.
5. Add a second controlled hotspot, select between saved profiles, delete the
   second profile with confirmation, and verify the first remains active.
6. Verify portal boundaries: unavailable before physical activation, wrong-code
   rejection, timeout rejection, single-use behavior, and closure after an
   accepted submission.
7. Disable the selected upstream for at least five minutes. Verify WAN goes
   offline while the private AP and setup UI remain available and no two-minute
   reboot occurs. Re-enable it and measure automatic reconnect.
8. Record measured scan, association, interruption, timeout, and reconnect
   behavior in `docs/README.md`, then commit those observed results.

Live Venus/GX Modbus and real Starlink field validation remain deferred until
the hardware is co-located for Tasks 10 and 11.

## Verified firmware state

At `1d5f95c`:

- all nine C++17 host suites rebuilt with warnings treated as errors and passed;
- Arduino-ESP32 3.3.11 compiled the sketch successfully;
- flash use was 1,028,222 bytes and global RAM use was 49,324 bytes;
- the only compile warning was TFT_eSPI reporting no `TOUCH_CS`, which is
  expected because this project drives XPT2046 through its separate library;
- the reviewed source delta was limited to the setup/dashboard coordinator,
  sketch wiring, and its host regression; and
- credential/log scans, ignored-secret checks, `git diff --check`, and clean
  worktree checks passed.

## Safety and data boundaries

- `VictronCYD_Modbus/secrets.h` is intentionally ignored. Transfer it through a
  private channel or recreate it from `secrets.example.h`; never commit it.
- Do not print, display in chat, or copy either Wi-Fi password into a report.
- Do not erase ESP32 NVS unless a specific calibration/profile reset is
  explicitly authorized. Ordinary uploads must preserve `0x9000..0xDFFF`.
- The board's NVS state lives on the board, not in this repository.
- Build outputs under `build/` are ignored and should be rebuilt on Linux.
- Push this branch only to `origin`, the user's fork. Do not push to `upstream`
  or open an upstream pull request without a separate explicit request.

## Start on Linux

1. Clone the user's fork and switch to the handoff branch:

   ```bash
   git clone https://github.com/drepamig/ESP32-Victron-VRM-Display.git
   cd ESP32-Victron-VRM-Display
   git switch codex/esp32-venus-starlink-touch-bridge
   ```

2. Read this file, `RESUME_PROMPT.md`, `VERIFY_LINUX.md`, the root `README.md`,
   and `docs/README.md`.
3. Confirm the branch contains firmware commit `1d5f95c` in its history.
4. Recreate or privately transfer `VictronCYD_Modbus/secrets.h`, then confirm it
   remains ignored without printing its contents.
5. Install the pinned toolchain and run the Linux verification in
   `VERIFY_LINUX.md` before changing firmware.
6. Open the repository folder as the local Codex project and start with the
   prompt in `RESUME_PROMPT.md`.
