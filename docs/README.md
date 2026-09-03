# Gateway status and acceptance record

Updated 2026-09-03 against `develop` at `6ef6c93`. This is the tracked status
record for the ESP32 Venus-Starlink touch bridge. Work in the current checkout
on `develop` according to [AGENTS.md](../AGENTS.md).

The firmware implementation includes the original gateway work, later touch
corrections, on-device Wi-Fi password entry, and the virtual bench. Hardware
acceptance is incomplete, and two functional findings remain open.

## Plan reconciliation

The original 2026-08-28 plan and design files are absent from this checkout.
The local, ignored `.superpowers/sdd/2026-08-28-esp32-venus-starlink-touch-bridge/`
directory retains task briefs and a historical ledger. Its former nested
checkout and feature branch are historical; do not recreate them to resume.
The ledger's previous final checkpoint, `1d5f95c`, is 20 commits behind the
reviewed `6ef6c93` baseline.

| Original task | Current status |
| --- | --- |
| 1: Fork and baseline | Implemented; the firmware repository is now the current checkout. |
| 2: Pure gateway policies | Implemented with host coverage. |
| 3: Persistent profiles | Implemented with transactional storage and host coverage; application selection has finding R1 below. |
| 4: AP, NAPT, uplink and scans | Implemented; finding R2 remains open and physical routing/recovery acceptance is pending. |
| 5: Touch and calibration | Implemented; inset mapping correction and historical hardware checks passed. |
| 6: Network-selection UI | Implemented; historical navigation checks passed; keyboard entry was added later. |
| 7: Physical phone portal | Implemented and retained as the explicit **Use phone** fallback; hardware boundaries remain to be exercised. |
| 8: Application integration | Implemented; saved-profile activation needs correction under R1. |
| 9: Independent ESP32 bench | Partially complete; remaining checks are listed below. |
| 10: Venus migration | Pending; supported configuration, stable addressing, TCP 502, and rollback must be established at the camper. |
| 11: End-to-end field acceptance | Pending; requires real Starlink and live Venus/GX together. |
| 12: Final documentation and verification | Partially complete; current docs/tooling exist, but measured acceptance, deployment/rollback runbook, and final review remain open. |

The [password-entry design](superpowers/specs/2026-09-02-on-device-wifi-password-design.md)
supersedes automatic phone-portal entry for an unknown protected SSID. The
keyboard opens first; **Use phone** starts the portal. Both submission sources
share the pending-profile path. The
[virtual bench plan](superpowers/plans/2026-09-03-cyd-virtual-bench.md) is
implemented and was merged at `4e5384f`; simulated network/GX evidence does not
complete physical gateway acceptance.

## Open review findings

### R1: Saved selection does not update the active profile

Status: open, confirmed by source inspection at `6ef6c93`.

In [the sketch](../VictronCYD_Modbus/VictronCYD_Modbus.ino),
`handleUiAction(ConnectSaved)` loads and connects the selected profile, then
calls `acceptPendingProfile()` without updating the profile store's active
index. Only the new-credential commit path calls `upsertAndActivate()`.
`refreshSavedProfiles()`, boot, and pending-attempt rollback still use the
stored active index.

With A stored as active, successfully connecting saved B therefore leaves the
active marker on A; reboot and a later failed credential attempt return to A.
The existing passing suites do not establish correct saved-selection
persistence through the application.

Acceptance for the correction: after B successfully associates and receives
an address, the active marker and persisted selection become B. Reboot and
rollback from a subsequent failed new-profile attempt use B. A failed switch
must preserve the prior successful selection. Add coverage of the production
application path as well as the profile store.

### R2: Upstream loss stays amber instead of red/offline

Status: open, reproduced against the production module with host Wi-Fi fakes
at `6ef6c93`; this was not a physical outage test.

[CamperNetwork::poll](../VictronCYD_Modbus/CamperNetwork.cpp) assigns
`WanPhase::Connecting` whenever the station lacks a connection/address but a
profile remains selected. It stays there throughout backoff. The dashboard
renders Connecting as amber; Task 9 requires red/offline during an extended
upstream outage.

The review probe started the real module, connected and DNS-validated a
synthetic profile, accepted it, then removed the fake station connection and
address. After polling through 300,000 ms of outage, the result was
`Connecting` with the AP still ready. The Offline acceptance assertion failed.
The probe is an ignored review artifact, not a committed regression test.

Acceptance for the correction: represent an unavailable uplink as red/offline
while retaining selected-profile retry/backoff and AP availability. Cover the
five-minute outage and successful recovery with a production-module
regression, then measure the same behavior on hardware.

## Verification evidence

| Evidence | Result and scope |
| --- | --- |
| Review at `6ef6c93`, 2026-09-03: `tools/dev.ps1 test` | Passed: 15 C++ suites and 14 Python tooling tests. |
| Same review: `tools/dev.ps1 firmware-build` | Passed using the generated dummy production configuration: 1,035,746 bytes flash and 49,492 bytes globals; only the documented TFT_eSPI `TOUCH_CS` warnings. |
| Same review: focused WAN-loss probe | Failed the planned Offline condition after five minutes; AP remained ready in the fake environment. See R2. |
| Same review: `git diff --check` | Passed; no tracked source changes were made by the review. |
| Earlier virtual bench acceptance, 2026-09-03 | Records successful production/simulator builds, isolation/attestation, 9 Wokwi scenarios and 25 exact screenshot matches. See the linked virtual bench plan for that run's evidence. |

The review did not rerun Wokwi, flash a board, or perform hardware/network
checks. A successful dummy build is not a configured real-GX deployment image.
Rebuild and rerun appropriate verification after any source correction.

## Recorded physical observations

The latest recorded verified upload is firmware
`a6827e6e92db1870f70ccacd73f8d2b0cf4d5a20`, with physical confirmation on
2026-09-02. Its upload preserved NVS at `0x9000..0xDFFF`. This is a historical
record, not a fresh reading of the connected board. Later keyboard and virtual
bench firmware has no recorded physical validation.

The following results are carried forward from the original bench ledger and
[Linux handoff](handoff/linux-2026-08-31/README.md). They were not repeated in
the 2026-09-03 review.

| Check | Recorded observation |
| --- | --- |
| Boot without upstream/GX | Private AP starts; dashboard remains stable with GX unavailable. |
| Calibration | No dashboard overlay during targets; calibration persists across reboot and NVS-preserving upload. |
| WAN hold | Centered font-2 HOLD 3/2/1 fits the header and opens setup automatically after three seconds. |
| Hold cancellation | Early release and slide-out restore the normal header without opening setup. |
| Setup rendering | Periodic full-screen flicker resolved. |
| Navigation | Five Nearby rows, Previous/Next, Saved, and Back work; deliberate top-row center taps register after the inset-mapping correction. |
| Scan feedback | Nearby and Refresh show Scanning immediately; controlled upstream is visible with expected signal-strength data. |

No measured scan duration, association time, AP/channel interruption duration,
or reconnect time has been recorded here. Qualitative observations above do
not establish timing bounds or uninterrupted routing.

## Remaining Task 9 acceptance

First correct R1 and R2, review the changes, and run the complete host/tooling
matrix and firmware build. For physical testing, build the reviewed source
with private deployment configuration and preserve NVS during verified upload.
Use a controlled bench upstream in place of real Starlink; keep its SSID and
password out of tracked results.

- [ ] Validate protected-network keyboard entry: all character pages, Shift,
      Space, Backspace, Show/Hide, count, and 8..63-character Connect gating.
- [ ] Verify successful association/DHCP, WAN validation, saved credentials,
      and reboot persistence through the on-device entry path.
- [ ] Verify Back/cancel, masked correction after failure, and the five-minute
      entry inactivity timeout; confirm failure restores the correct profile.
- [ ] From a laptop on the private AP, verify local gateway reachability, DNS,
      and outbound HTTPS through NAPT; measure any scan/channel interruption.
- [ ] Add a second controlled profile, switch both ways, verify active marker
      and reboot persistence, cancel/confirm deletion, and exercise early
      cancellation and completion of the ten-second Clear Saved hold.
- [ ] Exercise **Use phone**: physical activation, wrong-code rejection,
      ten-minute expiry, single use, cancellation, and AP-subnet restriction.
- [ ] Disable the selected upstream for at least five minutes; verify red WAN,
      responsive setup, continuous AP availability, and no no-data reboot.
      Restore it and measure reconnect without roaming to another profile.
- [ ] Record observed timing, firmware commit, test conditions, and pass/fail
      results below; do not replace unperformed checks with simulator results.

| Measurement | Firmware/date | Observed result |
| --- | --- | --- |
| Scan duration and AP/channel interruption | Pending | Not measured |
| Association/DHCP and WAN validation time | Pending | Not measured |
| NAPT DNS/HTTPS and local reachability | Pending | Not measured |
| Portal timeout and closure | Pending | Not measured on hardware |
| Five-minute outage and reconnect time | Pending | Not measured on hardware |

## Field work and completion

Task 10 requires a recorded rollback configuration, supported Venus service
settings, stable GX addressing on the private AP, and a successful TCP 502
check before migration proceeds. Task 11 must then validate live Modbus data,
independent GX/WAN outages and recovery, real Starlink behavior, and watchdog
recovery. No Venus changes or field results were performed during this review.

Finish Task 12 with measured bench/field tables, an installation and rollback
runbook matching the actual equipment, triage of historical deferred review
items, and final source/release verification. Build/test completion alone
does not close Tasks 9 through 12.

## Documentation map

- [Build and operator overview](../README.md)
- [Continuation handoff](handoff/linux-2026-08-31/README.md)
- [Resume prompt](handoff/linux-2026-08-31/RESUME_PROMPT.md)
- [Linux verification](handoff/linux-2026-08-31/VERIFY_LINUX.md)
- [Completed touch mapping correction](plans/2026-09-02-task-9-top-row-touch-plan.md)
- [Password-entry implementation record](superpowers/plans/2026-09-02-on-device-wifi-password.md)
- [Virtual bench implementation and evidence](superpowers/plans/2026-09-03-cyd-virtual-bench.md)
- [Simulator scenarios](../simulation/README.md)

The dashboard photo referenced by the root README belongs at `docs/dashboard.jpg`.
