# R2: Show WAN Offline during upstream loss

Approved in conversation on 2026-09-03. Work directly on `develop`.

## Behavior

After the selected SSID associates and obtains an IP, losing association or
DHCP reports Offline on the next poll and remains Offline during automatic
retries. Restored association/DHCP reports Validating, followed by Online
only after fresh DNS validation. Fresh boot, new selections, and rollback
attempts retain initial Connecting and the existing user-switch timeout.
Keep existing display colors, refresh cadence, and touch feedback.

Track establishment within the current selected-profile lifecycle, independent
of pending-profile acceptance and WAN phase. Stale prior-SSID readiness must
not establish the new lifecycle. Preserve retry deadlines/backoff/cap and its
reset after validation, invalidate stale DNS results, retain one worker, and
ensure recovery cannot become stuck while an invalidated worker drains.
Preserve AP/NAPT, credentials, active profiles, R1 behavior, and NVS schema.

## Implementation and verification

- [x] Extend the production network host suite; observe failure before the
      fix. Cover association/DHCP loss, 300,000 ms outage, retry boundaries
      and wraparound, same-profile recovery, DNS failure/stale results, fresh
      connection behavior, and AP/NAPT continuity.
- [x] Implement the private lifecycle marker and separate retry scheduling
      from reported WAN phase. Keep production public interfaces unchanged.
- [x] Add simulation-only `SIM wan=offline|validating|online`, gated on an
      established simulated connection; disconnect/reset/new attempts clear
      eligibility. Preserve profile ownership and existing connect fixtures.
      Add fixture/parser/local-protocol regressions before implementation.
- [x] Add local `wan-outage`: connect a dummy profile, capture Online,
      Offline, Offline after 30 guest seconds, setup/Back while offline,
      Validating, and recovered Online. Host tests prove the real five-minute
      network/retry behavior; fixture-driven images prove rendering/navigation.
- [x] Run complete host/tooling tests; production, DIO, and standard simulator
      builds; isolation and attestations. Run local `wan-outage`, `saved-switch`,
      and `reboot-persistence`; review actual images before promoting only
      recorded new scenario captures, then repeat the new scenario exactly.
- [x] Obtain scoped review and update status/handoff with measured results.

Physical acceptance stays pending: a five-minute real outage, red WAN,
responsive setup, private AP continuity, and recovery after an NVS-preserving
upload. No flashing, Venus changes, cloud execution, or pushing is included.

## Evidence

Production regressions failed against the previous implementation, then passed
after the correction (`build/r2/network-red.log` and `network-green.log`).
The dummy production build passed at 1,038,790 bytes flash / 49,532 globals
with the existing TFT_eSPI `TOUCH_CS` warnings (`production-build.log`).
Fixture/parser/protocol regressions failed before implementation. The complete
17 C++ suites and 55 Python tooling tests passed (`host-green.log` and
`tooling-green.log`). DIO (559,944 / 34,516) and standard simulator
(559,928 / 34,516) builds, both attestations, and production isolation passed.
All three targeted local scenarios passed with 19 exact RGBA comparisons.
Seven actual outage captures were reviewed, promoted from their recorded run,
and reproduced byte for byte by the repeat. Existing goldens were unchanged.
Independent review returned specification PASS and quality APPROVE, with no
actionable findings. `git diff --check` passed.

See the [verification report](../../research/2026-09-03-r2-wan-outage.md) and
its machine-readable evidence for current build identities, run IDs, logs,
and the initial expected missing-golden failure. R2 software work is complete;
physical acceptance remains pending as specified above.
