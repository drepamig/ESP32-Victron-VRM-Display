# Linux continuation handoff

Reconciled 2026-09-03 against `develop` at `6ef6c93`. The tracked
[status and acceptance record](../../README.md) is the current task tracker;
this handoff preserves the earlier bench evidence and continuation context.

This bundle is the portable continuation record for the camper gateway work
originally developed on branch `codex/esp32-venus-starlink-touch-bridge`.
Continue on `develop` unless the user explicitly instructs otherwise, following
the root `AGENTS.md`; the original branch name is historical context only.

The latest recorded verified upload was built from commit
`a6827e6e92db1870f70ccacd73f8d2b0cf4d5a20`. Later commits include keyboard
firmware and the virtual bench, not just documentation. There was no fresh
board inspection or upload during the 2026-09-03 review.

## Project outcome

The current branch implements:

- a persistent private `Camper-Victron` AP at `192.168.50.1/24`;
- outbound NAPT through one explicitly selected upstream Wi-Fi profile;
- a realtime Victron Modbus dashboard whose GX and WAN health are independent;
- XPT2046 resistive-touch calibration stored separately in NVS;
- a physically activated on-device keyboard for secured upstream networks, with
  the time-limited phone portal available through **Use phone**; and
- touch management for up to five saved upstream profiles.

Do not modify Venus OS files, add inbound forwarding, log credentials, or let
the firmware silently roam to a stronger saved profile.

## Completed development

Original implementation Tasks 1 through 8 were completed and reviewed. The
2026-09-03 review reopened follow-up work in Tasks 4 and 8: R1 (saved active
selection is not persisted) and R2 (an unavailable uplink remains Connecting).
Both are detailed in the [open findings](../../README.md#open-review-findings).
Historical Task 9 bench work recorded the following on physical hardware:

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

Firmware commit `a6827e6` then corrected the calibration-to-display coordinate
contract after a lower-edge probe isolated unreliable top-row taps. It maps the
stored 20-pixel calibration targets to their actual display positions without
changing the saved calibration format or touch debounce.

## Latest physical checkpoint

The recorded `a6827e6` upload was hash-verified on the ESP32. It wrote
the bootloader, partition table, boot app, and application without erasing NVS
at `0x9000..0xDFFF`, so saved touch calibration and any profiles were preserved.

Physical checks passed on 2026-09-02:

- `Nearby` and `Refresh` each paint `Scanning...` immediately;
- sliding outside WAN during the hold cancels the countdown/highlight and
  restores the normal header immediately;
- calibration remains stored across the upload; and
- single deliberate center taps on `Nearby`, `Saved`, and `Back` all register.

The branch now opens a masked on-device keyboard when an unknown protected
network is selected. **Use phone** starts the existing private, time-limited
portal fallback. The newer credential-entry flow has host, compile, and
recorded virtual-bench coverage. No later physical upload or validation is
recorded for its keyboard layout, touch accuracy, masking, retry, or fallback.

The next development work is correcting R1 and R2 with regressions and review.
After verification and an NVS-preserving upload of the privately configured
image, continue controlled upstream provisioning through the keyboard-first flow.

## Remaining Task 9 work

The [current acceptance checklist](../../README.md#remaining-task-9-acceptance)
includes saved-selection reboot/rollback coverage, reset protection, and timing
records. Resolve R1 and R2 before completing the affected hardware scenarios.

Use the user-controlled bench upstream in place of real Starlink during this
phase. Ask the user for its current SSID when needed; do not add that SSID or
its password to tracked files or logs.

1. Select the unknown secured upstream and physically validate the masked
   QWERTY keyboard, Shift, `123`, `#+=`, `ABC`, Space, Backspace, Show/Hide,
   character count, and Connect gating.
2. Enter the password on-device and verify WAN progresses through
   connecting/validating to online.
3. Join a laptop to `Camper-Victron`; verify `192.168.50.1`, DNS resolution,
   and outbound HTTPS through NAPT.
4. Try deliberately bad credentials and prove the previously active profile is
   restored, the password returns masked and editable, and the bad password is
   absent from output.
5. Add a second controlled hotspot, select between saved profiles, delete the
   second profile with confirmation, and verify the first remains active.
6. Select **Use phone**, then verify the fallback portal boundaries: unavailable
   before physical activation, wrong-code rejection, timeout rejection,
   single-use behavior, and closure after an accepted submission.
7. Disable the selected upstream for at least five minutes. Verify WAN goes
   offline while the private AP and setup UI remain available and no two-minute
   reboot occurs. Re-enable it and measure automatic reconnect.
8. Record measured scan, association, interruption, timeout, and reconnect
   behavior in `docs/README.md`, then commit those observed results.

Live Venus/GX Modbus and real Starlink field validation remain deferred until
the hardware is co-located for Tasks 10 and 11.

## Current repository verification

At `6ef6c93`, the 2026-09-03 review reran `tools/dev.ps1 test` (15 C++ suites
and 14 Python tests passed) and `tools/dev.ps1 firmware-build` (dummy production
build passed at 1,035,746 bytes flash and 49,492 bytes globals). A separate
production-module outage probe failed the planned Offline condition after
five minutes. Wokwi and hardware checks were not repeated in this review.

The virtual bench's earlier acceptance records 9 Wokwi scenarios and 25 exact
screenshot matches; see its
[implementation record](../../superpowers/plans/2026-09-03-cyd-virtual-bench.md).

## Historical firmware verification

At `a6827e6`:

- all nine C++17 host suites rebuilt with warnings treated as errors and passed;
- Arduino-ESP32 3.3.11 compiled the sketch successfully;
- flash use was 1,028,330 bytes and global RAM use was 49,324 bytes;
- the only compile warning was TFT_eSPI reporting no `TOUCH_CS`, which is
  expected because this project drives XPT2046 through its separate library;
- the final touch correction changed only coordinate mapping, calibration-target
  placement, and the focused host regression; and
- credential/log scans, ignored-secret checks, and `git diff --check` passed;
  the user-owned untracked shortcut remained untouched and excluded.

## Safety and data boundaries

- `VictronCYD_Modbus/secrets.h` is intentionally ignored. Transfer it through a
  private channel or recreate it from `secrets.example.h`; never commit it.
- Do not print, display in chat, or copy either Wi-Fi password into a report.
- Do not erase ESP32 NVS unless a specific calibration/profile reset is
  explicitly authorized. Ordinary uploads must preserve `0x9000..0xDFFF`.
- The board's NVS state lives on the board, not in this repository.
- Build outputs under `build/` are ignored and should be rebuilt on Linux.
- Push only to `origin`, the user's fork. Do not push to `upstream`
  or open an upstream pull request without a separate explicit request.

## Start on Linux

1. Clone the user's fork on `develop` unless the user explicitly requests a
   different branch:

   ```bash
   git clone --branch develop https://github.com/drepamig/ESP32-Victron-VRM-Display.git
   cd ESP32-Victron-VRM-Display
   git branch --show-current
   ```

2. Read `docs/README.md`, this file, `RESUME_PROMPT.md`, `VERIFY_LINUX.md`,
   and the root `README.md`.
3. Confirm the branch contains firmware commit `a6827e6` in its history.
4. Run the repository-owned Docker verification in `VERIFY_LINUX.md`; it needs
   no private configuration. Preserve any existing ignored secrets file.
5. Recreate or privately transfer `VictronCYD_Modbus/secrets.h` only when a
   privately configured deployment build is needed. Confirm it stays ignored
   without printing its contents.
6. Open the repository folder as the local Codex project and start with the
   prompt in `RESUME_PROMPT.md`.
