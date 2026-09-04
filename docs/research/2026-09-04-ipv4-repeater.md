# IPv4 repeater verification and physical acceptance

Date: 2026-09-04. Working branch: `develop`, based on `dccc9c3`.
The [approved design](../superpowers/specs/2026-09-04-ipv4-repeater-design.md)
and [implementation plan](../superpowers/plans/2026-09-04-ipv4-repeater.md)
replace outbound-only NAPT with bidirectional IPv4 forwarding.

## Implemented behavior

AP clients obtain upstream DHCP leases while the selected Wi-Fi connection has
an IPv4 address. A bounded MAC/IPv4 table translates Ethernet identities without
translating client IPv4 addresses or TCP ports. The ESP32 can also initiate
Modbus traffic to those clients. Real Wi-Fi/IP loss restores local DHCP on
`192.168.50.0/24`; Internet/DNS-only failure leaves addressing unchanged.

Venus identification uses valid existing Modbus system-register reads from
observed AP clients. Its MAC is retained in the separate `venusidentity` NVS
namespace. Worker messages carry copied MAC/address/generation tokens, and stale
replies cannot bind or refresh the current endpoint. Removing/changing an
endpoint closes the old TCP connection when the worker accepts the change.

**Settings → Venus OS** shows the current IP and connection status. A missing
device retains an explicitly labeled **Last seen** address; an unidentified
device shows **Not found**. There is no SSH command or path. The phone portal
checks current AP-client membership, and its screen shows the ESP32's current
management address.

## Local verification

Final source verification after review fixes:

| Check | Result |
| --- | --- |
| `python tools/bench_cli.py test` | Passed: all 23 C++ suites and 61 Python tooling tests. |
| Production build | Passed: 1,056,510 bytes flash, 50,148 bytes globals; real SDK adapter linked. |
| Local DIO simulator build | Passed: 585,398 bytes flash, 35,072 bytes globals. |
| `tools/check-isolation.sh --mode velxio` | Passed: current attestation and production/simulator isolation. |

Both builds used the pinned offline Docker toolchain. Only the existing TFT_eSPI
`TOUCH_CS` warning was emitted; touch uses the dedicated driver. Production
application binary SHA-256:
`25f0b6cf707e61e13f78a1d9375856b27c0dbc0496c90b0fa902c8c1ab55c72d`.
Local merged DIO binary SHA-256:
`b0f08687b1910ea44f3238c9dba49a9b741d44a315f03c90aac73159a3d35060`.

The packet tests use literal Ethernet/ARP/IPv4/DHCP frames. They cover forwarding
to multiple clients, preserving IP/transport bytes and DHCP hardware identity,
lease changes/expiry, malformed lengths, fragments, client removal, and domain
reset. The real adapter also compiles against host SDK fakes to check TCP/IP
serialization, pbuf ownership, bounded queues, DHCP ordering, fallback and
ESP-originated traffic. These tests do not emulate an ESP32 radio.

Independent review identified and corrected self-ARP probe replies after DHCP
ACKs, weaker observations displacing active leases, and local availability after
bridge activation errors. The first two defects were reproduced before their
fixes and checked with address/undefined-behavior sanitizers. An empty IP can no
longer display Connected, and the phone portal no longer shows a fixed address.

The simulator uses dummy IP/MAC/Modbus observations with the production tracker
and UI. Unknown-address rendering is covered by host tests; the local Venus
scenario covers current, changed, unreachable, last-seen, and fallback states.
Only the dummy serial fixture allowlist changed in the local runner. Its runtime,
DIO, timing, touch, and display adapters remain pinned.

| Retained local run | Scenario | Captures |
| --- | --- | ---: |
| `6e7d7f7973f74015b1c48dd954545a66` | venus-address | 7 |
| `9a3dccbf67f34c0fb2d923e6047fcade` | time-settings | 21 |
| `4dff62501ff84cbfa135794ce55ad6ab` | wan-outage | 7 |
| `7d04f6661b2d40609bf9a3ec14f08865` | saved-switch | 7 |

All four scenarios completed against the same attested image. All **42 native
RGBA images** match the current reviewed references. The seven new Venus
captures and two changed Settings menus were visually inspected before recorded
promotion; the other 33 images matched existing references. Promotion and final
comparison reused the captures and did not rerun simulation. The runner's known
startup `TWDT already initialized` warning remains recorded. See the
[machine-readable evidence](2026-09-04-ipv4-repeater-evidence.json).

Final independent review approved local code acceptance with no remaining
actionable findings. Core, adapter, and tracker sanitizer runs passed. The TCP
target test passed normally and on a bounded sanitizer rerun with its signal
handler disabled after an initial nondiagnostic sanitizer runtime failure.
Final core coverage also passed with sanitizers for unicast DHCP renewal and
four-client table capacity/reuse. `git diff --check` passed. Hardware acceptance
is separate from these results.

## Physical acceptance still required

This change has not been flashed. The last recorded physical firmware is
`8e2cd3b`, which predates the repeater. No Wokwi cloud run was performed.

1. Make an NVS-preserving upload, then verify existing profiles, calibration,
   time settings, AP SSID/password, and explicit upstream selection.
2. Configure Venus Wi-Fi for DHCP on the ESP32 AP. Enable Modbus TCP on Venus;
   enable SSH if it is required for the installation. Confirm that the upstream
   DHCP lease belongs to Venus's MAC and matches the Settings page.
3. From another computer on the upstream LAN, connect directly to Venus's
   displayed IPv4 address on TCP 22. Verify ESP32-to-Venus Modbus concurrently,
   plus AP-client DNS/HTTPS and access between two AP clients.
4. Disconnect upstream Wi-Fi for at least five minutes. Verify local DHCP,
   Venus Modbus and the local portal at `192.168.50.1`, red WAN, responsive touch,
   and no unexpected reboot. Restore upstream and verify new leases, the same
   Venus identity, refreshed page IP, direct upstream access, and WAN recovery.
5. Disable Internet/DNS while retaining upstream Wi-Fi and DHCP. Confirm no
   address change or AP-client reconnection. Repeat a cold boot without upstream
   and a switch to an upstream on another subnet.
6. Exercise the physically activated portal from an AP client with an upstream
   lease: pairing, expiry, single use, and rejection of upstream-only clients.
   Record actual reconnect times and any dropped sessions during domain changes.

Rollback uses the previously verified firmware with NVS preserved, followed by
the prior Venus addressing configuration. Firmware that predates this change
uses the old compiled GX address; record it privately before migration. Do not
infer hardware acceptance from local tests or screenshots.
