# R2 WAN outage verification — 2026-09-03

Implemented on `develop` after `b9e92f7`, following the
[approved plan](../superpowers/plans/2026-09-03-r2-wan-outage.md). This work uses
zero Wokwi executions and includes no flashing, Venus changes, or pushing.

## Production behavior

The production network module tracks association/DHCP establishment for the
selected SSID in the current connection lifecycle. Subsequent association or
address loss reports Offline on the next poll and throughout automatic retries.
Restoration reports Validating until fresh DNS succeeds. Initial selection
and rollback start Connecting; acceptance and automatic retries retain the
establishment marker. Prior-SSID readiness cannot establish a replacement.

Retry scheduling is independent of WAN phase. The 5/10/20/40/60-second capped
backoff and selected profile are preserved, with a reset after successful DNS.
Loss invalidates an outstanding DNS result. Recovery can show Validating while
that worker drains, then starts fresh validation without becoming stuck.
Production public interfaces, profile persistence, NVS schema, AP/NAPT setup,
display colors/cadence, and touch behavior are unchanged.

## Automated verification

| Check | Result |
| --- | --- |
| Production network regressions | Failed before the correction, passed afterward. |
| Fixture, parser and local serial whitelist regressions | Failed before implementation, passed afterward. |
| Complete host/tooling matrix | 17 C++ suites and 55 Python tests passed. |
| Dummy production smoke build | 1,038,790 bytes flash; 49,532 bytes globals. |
| Local DIO simulator build | 559,944 bytes flash; 34,516 bytes globals. |
| Standard simulator build | 559,928 bytes flash; 34,516 bytes globals. Local build only. |
| Attestation and isolation | Both simulator attestations and production isolation passed. |
| Independent implementation review | Specification PASS, code quality APPROVE; no actionable findings. |

The production host tests exercise association loss, DHCP-only loss, a
300,000-ms outage, every capped retry deadline immediately before/at/after,
timer wraparound, selected-profile retention, fresh lifecycles, stale SSID
readiness, acceptance before DNS, stale DNS completion, failed validation, and
fresh recovery. AP configuration, mode, NAPT, and attached-client status remain
unchanged in the fake environment. Existing controller tests cover R1's
transactional activation, rollback, cancellation, timeout, and UI ownership.

Simulation fixtures accept `SIM wan=offline|validating|online` only after the
current simulated attempt succeeds. Tests cover pending/accepted ownership,
invalid input without mutation, and eligibility reset on disconnect, reset,
and a new connection attempt. Exact generic SIM responses contain no credentials;
the changed production code adds no logging. The existing host Serial fake
discards some print overloads, so its no-secret assertion is not a complete
production UART leak test.

Retained logs are under ignored `build/r2`: `network-red.log`,
`network-green.log`, `fixture-red.log`, `parser-red.log`, `protocol-red.log`,
`host-green.log`, `tooling-green.log`, the three build logs, and `isolation.log`.
Builds emitted only the existing TFT_eSPI `TOUCH_CS` warnings.

## Local simulation acceptance

The local `wan-outage` scenario first connects dummy Bench-Open through the
actual setup UI. Seven captures cover Online, Offline, Offline after 30 guest
seconds, setup during the outage, Back to the offline dashboard, Validating,
and recovered Online. This fixture-driven scenario proves rendering/navigation;
the production host regressions prove retry and DNS behavior over five minutes.

All three targeted local scenarios passed against the current attested build:

| Scenario | Accepted run ID | Exact RGBA comparisons |
| --- | --- | --- |
| wan-outage | `7dad6657f9214f779457776983c28ac0` | 7 |
| saved-switch | `871aaf60c2f24b8189bc352d76fa3b24` | 7 |
| reboot-persistence | `e34748d0ea02468fa2f27376c70b4377` | 5 |

The initial outage run `5e49cbaec06c494d86a521c73fe67c58` completed execution
but returned 1 for seven expected missing references. All seven actual images
were inspected before promotion from that recorded run. Online, Offline, and
Validating are visually distinct using the existing display inversion setting.
Offline, Offline after 30 seconds, and Back-to-Offline PNGs were byte-identical;
setup showed Bench-Open still active. No pixels or comparison tolerances were
changed. The repeated outage run reproduced all seven references byte for byte
and reached 49.529 guest seconds in 136.238 wall seconds.

Saved switching matched all seven existing references, including progress,
cancel, timeout, rollback to A, and activation of B. Reboot persistence matched
all five references across two ready boot segments with clean retained flash;
B remained active and a failed new submission rolled back to B. No existing
goldens changed. These runs account for 19 current exact RGBA comparisons;
the other supported local scenarios were not rerun for this scoped correction.

The [machine-readable evidence](2026-09-03-r2-wan-outage-evidence.json) preserves
attestations, firmware hashes, scenario/run/result identities, log hashes, and
comparisons. Export rechecked current source/runtime identity, run integrity,
capture hashes, and golden hashes. Full local records remain under ignored
`build/velxio/results/<run-id>`. The known startup `TWDT already initialized`
warning was retained; no unexpected watchdog events were accepted.

## Physical boundary

Physical acceptance remains pending. Use a reviewed, privately configured
image and an NVS-preserving upload. Establish the selected upstream, disable
it for at least five minutes, verify red WAN throughout retries, responsive
setup and private AP continuity, then restore it and measure validation and
reconnection to the same profile. R1's physical A→B switching, reboot,
cancellation, failed-switch rollback, and AP checks remain required too.

No real radio authentication, AP/NAPT routing, deployment timing, or hardware
reboot acceptance is inferred from host or simulator results. The six earlier
unsupported local scenarios remain explicit coverage gaps in the
[simulator guide](../../simulation/README.md).
