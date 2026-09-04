# Gateway status and acceptance record

Updated 2026-09-04 for the verified physical upload of R2 firmware `d69e0cf`
on `develop`. This is the tracked status
record for the ESP32 Venus-Starlink touch bridge. Work in the current checkout
on `develop` according to [AGENTS.md](../AGENTS.md).

The firmware implementation includes the original gateway work, later touch
corrections, on-device Wi-Fi password entry, and the virtual bench. Hardware
acceptance is incomplete. R1 and R2 are implemented, reviewed, and automatically
verified; their physical acceptance remains pending.

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
| 3: Persistent profiles | Implemented with transactional storage and host coverage; saved activation is implemented under R1 below. |
| 4: AP, NAPT, uplink and scans | Implemented, including the R2 outage correction; physical routing/recovery acceptance is pending. |
| 5: Touch and calibration | Implemented; inset mapping correction and historical hardware checks passed. |
| 6: Network-selection UI | Implemented; historical navigation checks passed; keyboard entry was added later. |
| 7: Physical phone portal | Implemented and retained as the explicit **Use phone** fallback; hardware boundaries remain to be exercised. |
| 8: Application integration | Implemented; R1 now uses the shared connection controller. |
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

## Local testing workflow and next development task

Velxio is the chosen local simulator backend alongside the existing host
tests. Use local host/tooling tests, firmware builds, and Velxio for supported
UI, navigation, and simulated-behavior scenarios. Reserve Wokwi for targeted comparisons
or an agreed release checkpoint. Physical hardware remains the acceptance
authority for real Wi-Fi/AP/NAPT, reboot persistence, and deployment behavior.

The [Velxio runner](superpowers/plans/2026-09-03-velxio-local-runner.md) now pins
the runtime/adapters, builds an isolated DIO image, and runs offline against
dummy inputs. `sim-build`, `sim-test`, and `all` default to local execution.
Cloud runs require an explicit Wokwi backend and scenario/full-suite selection;
there is no fallback. Golden promotion reuses reviewed recorded local captures.
See the [implementation evidence](research/2026-09-03-velxio-local-runner.md).
R2 adds a local `wan-outage` scenario. Six earlier scenarios still lack local
acceptance, and all outstanding physical acceptance remains open. The next task
is controlled Task 9 hardware acceptance.

## Review findings

### R1: Persist successful saved-network selection

Status: implemented and reviewed on 2026-09-03; physical acceptance pending.

The production-used [GatewayConnectionController](../VictronCYD_Modbus/GatewayConnectionController.h)
now owns saved, keyboard, open-network, and phone-submitted attempts. It reads
the candidate and previous active profile before replacing connectivity.
Saved B becomes active through transactional `activate()` only when association
and DHCP match B's SSID; stale A readiness cannot commit B. This path preserves
stored credentials, metadata, calibration, and the NVS schema. New submissions
continue using `upsertAndActivate()` after successful association/DHCP.

The SavedConnecting view paints B's name, Connecting, WAN status, and Back
before Wi-Fi work starts. It blocks other controls and setup inactivity exit.
Back cancels to Saved with the previous active marker. Rejection, 60-second
timeout, cancellation, and activation-write failure restore the retained
previous profile. Without one, upstream stays disconnected and the AP remains
available. Successful B selection drives the refreshed marker, reboot, and
rollback from later failed credential attempts. Internet/DNS validation is
not required to persist the selection. Boot and automatic reconnection retain
their existing behavior.

Regressions demonstrated failure before the fix for stale A readiness,
missing progress painting, and saved attempt start. The controller integration
suite uses the real profile store and network module with hardware fakes. It
covers reboot reconstruction, later rollback to B, write/read failures, timeout
boundaries and wraparound, cancellation and late success, replacement, no
previous profile, duplicate prevention, AP availability, and credential-free
output. UI/controller tests verify paint-before-connect and Back contact
capture. Independent review found two extraction regressions in portal failure
ownership and lifecycle replacement; both were fixed with regressions and the
follow-up review reported no remaining findings.

Physical acceptance must still prove A→B switching, reboot persistence,
cancellation, failed-switch rollback, and continued private AP availability
using an NVS-preserving upload. The later upload below preserves NVS, but does
not complete those interactive acceptance checks.

### R2: Upstream loss stays amber instead of red/offline

Status: implemented, automatically verified, and independently reviewed on
2026-09-03; physical acceptance pending. See the
[approved R2 plan](superpowers/plans/2026-09-03-r2-wan-outage.md) and
[verification evidence](research/2026-09-03-r2-wan-outage.md).

[CamperNetwork::poll](../VictronCYD_Modbus/CamperNetwork.cpp) now tracks whether
the selected SSID has established association and DHCP in its current
lifecycle. Losing either reports Offline on the next poll and throughout
automatic retries. Restoration reports Validating until fresh DNS succeeds.
Stale DNS results cannot restore Online, and a draining old worker cannot
prevent fresh validation. Only the selected SSID can establish readiness.

Fresh boot connection, explicit selection, and rollback retain Connecting.
The existing 5/10/20/40/60-second capped backoff, selected profile, AP/NAPT,
display colors, refresh cadence, and touch feedback are preserved. Acceptance
of a saved profile remains based on matching association/DHCP, independent of
DNS. No credential storage or NVS schema changes are included.

The original `6ef6c93` probe exposed Connecting after 300,000 ms. The maintained
production-module regression now exercises five-minute association and DHCP
outages, exact retry boundaries and wraparound, same-profile recovery, stale
SSID/DNS state, and AP continuity. New regressions failed before the fix.
The local `wan-outage` fixtures check rendering and setup/Back navigation;
they do not simulate radio failures or prove production retry behavior.

Physical acceptance must still measure a five-minute outage, red WAN,
responsive setup, AP availability, and recovery after an NVS-preserving upload.

## Verification evidence

| Evidence | Result and scope |
| --- | --- |
| R2: host/tooling matrix | Passed: 17 C++ suites and 55 Python tooling tests. Production outage, fixture, parser, and protocol regressions failed before implementation. |
| R2: builds and isolation | Production 1,038,790 bytes flash / 49,532 globals; local DIO 559,944 / 34,516; standard simulator 559,928 / 34,516. All builds, both attestations, and production isolation passed. |
| R2: targeted local scenarios | `wan-outage`, `saved-switch`, and `reboot-persistence` passed with 19 exact RGBA comparisons. Seven new outage images were visually reviewed before recorded promotion and reproduced byte for byte on repeat. Existing goldens unchanged; zero Wokwi executions. [Run IDs and evidence](research/2026-09-03-r2-wan-outage.md). |
| R2: review | Independent specification PASS and quality APPROVE; no actionable findings. `git diff --check` passed. Physical outage/recovery and R1 acceptance remain pending. |
| Velxio runner: final host/tooling matrix | Passed: 17 C++ suites and 54 Python tooling tests, including failure handling, capture integrity, offline dispatch, attestation, and destination-link rejection. |
| Velxio runner: builds and isolation | Production 1,038,650 bytes flash / 49,524 globals; local DIO 559,660 / 34,516; standard simulator 559,644 / 34,516. All builds and both simulator attestations/isolation passed. No Wokwi execution. |
| Velxio runner: local acceptance | Five supported scenarios, 18 exact RGBA image comparisons; repeated calibration/navigation, hold, and flash-preserving reboot passed. Five new reboot references were visually reviewed and promoted from recorded captures. See the [implementation evidence](research/2026-09-03-velxio-local-runner.md). R2 remained open at that checkpoint; physical reboot and real Wi-Fi/AP/NAPT remain pending. |
| FT6206 release fix, 2026-09-03 | Passed: 17 C++ suites and 14 Python tests; new release-race regression failed before the fix. Actual Adafruit-driver reproduction also changed from failure to success. Independent review found no issues. |
| FT6206 fix: builds and local navigation | Production 1,038,650 bytes flash / 49,524 globals; simulator 559,644 / 34,516; attestation/isolation passed. Two local Velxio runs each matched Calibration/Saved/Nearby exactly after the fix. No Wokwi rerun or golden changes; [investigation and evidence](research/2026-09-03-velxio-investigation.md#ft6206-correction-and-local-retest). |
| R1, 2026-09-03: complete host/tooling matrix | Passed: 16 C++ suites and 14 Python tooling tests, including the new production controller integration suite. |
| R1: production smoke build | Passed with generated dummy configuration: 1,038,650 bytes flash and 49,524 bytes globals. |
| R1: simulator build and isolation | Passed: 559,636 bytes flash and 34,516 bytes globals; source/artifact attestation and production/simulator isolation verified. Both builds emit only the documented TFT_eSPI `TOUCH_CS` warnings. |
| R1: Wokwi screenshots | All 10 live scenarios completed; 32 exact screenshot matches. The 25 existing baselines were unchanged. Seven new saved-switch captures were visually reviewed before promotion; a repeat live run reproduced all seven byte for byte. Final comparison also verified attestation and serial logs. |
| R1: review and diff | Independent follow-up review found no remaining actionable issues; `git diff --check` passed. |
| Review at `6ef6c93`, 2026-09-03: `tools/dev.ps1 test` | Passed: 15 C++ suites and 14 Python tooling tests. |
| Same review: `tools/dev.ps1 firmware-build` | Passed using the generated dummy production configuration: 1,035,746 bytes flash and 49,492 bytes globals; only the documented TFT_eSPI `TOUCH_CS` warnings. |
| Same review: focused WAN-loss probe | Failed the planned Offline condition after five minutes; AP remained ready in the fake environment. See R2. |
| Same review: `git diff --check` | Passed; no tracked source changes were made by the review. |
| Earlier virtual bench acceptance, 2026-09-03 | Records successful production/simulator builds, isolation/attestation, 9 Wokwi scenarios and 25 exact screenshot matches. See the linked virtual bench plan for that run's evidence. |

The earlier review at `6ef6c93` did not rerun Wokwi, flash a board, or perform
hardware/network checks. R1 automated verification is recorded separately
above; physical checks remain pending. A successful dummy build is not a
configured real-GX deployment image.
Rebuild and rerun appropriate verification after any source correction.
The FT6206 correction affects the simulator touch boundary; its latest local
verification is recorded separately. Earlier R1 cloud results apply to the
pre-correction image. Physical acceptance remains pending; R2's current
verification is recorded separately.

The earlier R1 verification used the then-cloud `tools/dev.ps1 all` for the complete host/tooling,
production, simulator, isolation, and live scenario run. Its initial comparison
reported only the seven expected missing saved-switch baselines. After visual
review, the existing `tools/run_wokwi.py update-goldens --scenario saved-switch`
runner repeated that scenario and promoted the captures. All 32 resulting
images then passed the repository comparator against the final baselines.
No firmware flashing, Venus changes, or pushing occurred.

## Recorded physical observations

The latest recorded verified upload is firmware
`d69e0cf03ffd8a99cdbb825efdd9303a69f1962e` on 2026-09-04, using the private
configuration on `COM3`. All four firmware segments passed hash verification;
NVS at `0x9000..0xDFFF` matched byte for byte before and after flashing. A normal
boot reported the private AP ready, with one boot and no panic/watchdog event
during 25 seconds. No valid GX result was observed. See the
[upload record](research/2026-09-04-physical-upload.md) for artifact hashes,
measurements, and limits. Physical UI, R1/R2, and routing acceptance remain pending.

The following earlier touch results at firmware `a6827e6` on 2026-09-02 are
carried forward from the original bench ledger and
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

R1 and R2 implementation, automated verification, and review are complete.
The privately configured `d69e0cf` image is now uploaded and verified. Use that
image for acceptance; if source or configuration changes, rebuild the reviewed
source and preserve NVS during the next verified upload.
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
      and reboot persistence, cancel a pending switch, force a failed switch and
      confirm rollback to the previous active profile with the private AP available.
      Cancel/confirm deletion, and exercise early cancellation and completion of
      the ten-second Clear Saved hold.
- [ ] Exercise **Use phone**: physical activation, wrong-code rejection,
      ten-minute expiry, single use, cancellation, and AP-subnet restriction.
- [ ] Disable the selected upstream for at least five minutes; verify red WAN,
      responsive setup, continuous AP availability, and no no-data reboot.
      Restore it, observe Validating then Online, and measure reconnect without
      roaming to another profile. Fresh selection and rollback start Connecting.
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
