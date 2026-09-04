# IPv4 Repeater Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** Provide direct upstream IPv4 access to Venus, retain local operation
without an uplink, and display the live Venus address in Settings.

**Architecture:** A bounded packet core handles MAC translation and address
learning; a pinned lwIP adapter manages hooks and DHCP domains. An application
tracker binds Venus identity and supplies both the Modbus endpoint and settings
status. Existing Wi-Fi profile and display coordinators remain the owners of
their respective lifecycles.

**Tech Stack:** Arduino-ESP32 3.3.11, C++17 host tests, TFT_eSPI 2.5.43,
XPT2046_Touchscreen 1.4, existing Docker toolchain and pinned Velxio runtime.

**Spec:** `docs/superpowers/specs/2026-09-04-ipv4-repeater-design.md`

## Global Constraints

- Work directly on `develop`; no branch/worktree switch.
- IPv4 only; direct upstream access without port forwards or upstream routes.
- No SSH command/path on the Venus OS settings page.
- Local fallback uses 192.168.50.0/24; transitions may briefly reconnect clients.
- Internet/DNS failure alone does not change the addressing domain.
- Preserve profiles, touch calibration, time settings, and explicit selection.
- No Wokwi cloud run; no silent fallback to NAPT.
- Test and build locally; record physical acceptance separately.

### Task 1: Read-only Venus OS settings page

**Files:** Create `VictronCYD_Modbus/VenusConnectionStatus.h`; modify
`VictronCYD_Modbus/SettingsUi.h`, `SettingsUi.cpp`, and
`tests/host/settings_ui_test.cpp`.

**Interfaces:** Produces `VenusConnectionStatus` with `char address[16]`,
`bool current`, and `bool reachable`, all zero/default initialized.
`SettingsUi::setVenusStatus(const VenusConnectionStatus&)` copies the status and
requests redraw only when changed while the Venus view is visible.

- [x] Extend the real settings UI tests before implementation. Open the root,
  release, tap the third row at (160,184), and assert `SettingsView::Venus`.
  Render known/current/reachable, current/unreachable, last-seen, and unknown
  fixtures. Check address/status draw records, Back, timeout, and release guard.
- [x] Run the focused g++ settings UI compile in the toolchain container and
  capture the missing-feature failure.
- [x] Add `Venus` view and the third menu button at `(4,164,312,40)`.
  Keep existing Time and Wi-Fi positions. Render IP/status and Back using the
  established display style; no command string, input field, or extra settings.
- [x] Run settings UI and application navigation host tests. Self-review the
  diff and report exact commands/results for independent review.

Example consumer:

```cpp
VenusConnectionStatus status{};
std::strcpy(status.address, "192.168.1.73");
status.current = true;
status.reachable = true;
settingsUi.setVenusStatus(status);
```

### Task 2: IPv4 frame core and network adapter

**Files:** Create `Ipv4BridgeCore.h/.cpp`, `Ipv4Bridge.h/.cpp` and focused host
fixtures; modify `CamperNetwork.h/.cpp`, test Wi-Fi/platform fakes, the network
tests, and `tools/run-host-tests.sh`.

**Interfaces:** The packet core accepts bounded Ethernet frames from AP or STA
and emits forwarding/local-delivery decisions plus client address observations.
The adapter owns core access on the TCP/IP thread and returns copied client
snapshots to the application. `CamperNetwork` drives it with station readiness,
not WAN DNS health, and exposes current AP-client address snapshots.

- [x] Write literal Ethernet fixtures testing ARP requests/replies, IPv4 traffic
  from two AP clients and back, upstream DHCP ACK unicast/broadcast, DHCP NAK,
  malformed/truncated packets, IP fragments, table capacity, and client removal.
- [x] Run the fixture test red, then implement bounded parsing/MAC translation.
  Preserve IPv4 source/destination and transport payload; repair only checksums
  for any DHCP fields deliberately changed. Never accept invalid lengths.
- [x] Test own ESP32 traffic to AP-side clients as well as upstream traffic.
  Add low-level hooks using the SDK APIs verified from the installed 3.3.11
  toolchain. Chain local traffic exactly once and honor pbuf ownership.
- [x] Write lifecycle tests for cold boot fallback, station acquisition,
  radio loss, upstream subnet change, DHCP mode ordering, and DNS-only failure.
  Ensure local DHCP is stopped before bridge forwarding and retained on setup
  errors. Disconnect clients only on actual addressing-domain changes.
- [x] Replace NAPT setup and update affected host contracts. Compile the real
  production adapter in the firmware build, resolving link/API compatibility.
- [x] Review packet/lifecycle code independently before completion.

Behavior fixture sketch:

```cpp
// A hand-built IPv4/TCP frame addressed to 192.168.1.73 from upstream must
// emerge on AP with the learned client MAC, unchanged IPv4 and TCP bytes.
// A second frame to 192.168.1.74 must use the second client's MAC.
// Neither frame is a TCP connection to the ESP32 itself.
```

### Task 3: Venus address identity and runtime integration

**Files:** Create `VenusAddressTracker.h/.cpp`; modify
`TcpModbusCycleSource.h/.cpp`, `VictronCYD_Modbus.ino`, simulator boundaries,
`ProvisioningPortal.h/.cpp`, focused host tests, and build/test source lists.

**Interfaces:** Consume copied AP-client MAC/IPv4 observations from Task 2 and
produce `VenusConnectionStatus` from Task 1 plus the selected Modbus endpoint.
Persist only the verified Venus MAC in a dedicated `venusidentity` namespace.

- [x] Add failing tests for first observed candidates, successful GX binding,
  rejected unrelated clients, address change of the bound MAC, disconnection,
  stale-generation result, NVS load/save failure, and no upstream subnet scan.
- [x] Implement bounded candidate rotation among associated AP clients using
  existing successful Modbus system-register reads as confirmation. Once bound,
  follow that MAC rather than the first client or a fixed DHCP address.
- [x] Make Modbus endpoint updates close the previous connection; exchange
  endpoint/result state across the existing worker boundary without races.
  Preserve last-seen display data but invalidate current/healthy on domain loss.
- [x] Connect `setVenusStatus` to the existing periodic application update and
  simulation snapshots. Keep telemetry healthy only after a valid current read.
- [x] Replace the provisioning portal's fixed-subnet test with associated-client
  membership supplied by the network adapter, retaining physical activation,
  pairing code, expiry, and single-use behavior.
- [x] Run focused tracker, Modbus, portal, and application host tests; build
  production and simulation variants and obtain integration review.

### Task 4: Local visual acceptance and durable evidence

**Files:** Add `simulation/velxio/venus-address.yaml` and its local mapping,
update simulator command handling and manifests, relevant local goldens, README,
`docs/README.md`, and a research/verification record.

- [x] Add deterministic current, changed, disconnected/last-seen, and fallback
  Venus fixtures and navigate the real Settings page; cover unknown status in
  the host UI tests. Assert current Modbus
  target and visible address derive from one state source.
- [x] Run local host/tooling tests and production/simulator builds. Run targeted
  Velxio `venus-address`, `time-settings`, and relevant connection/outage checks.
- [x] Inspect actual captured images. Promote only reviewed captures from the
  same attested local run; update intentional menu golden changes without a
  redundant simulation run.
- [x] Replace current outbound-only documentation with the approved behavior,
  document Venus DHCP migration and real-device acceptance steps, and explicitly
  state which network/hardware checks remain unperformed.
- [x] Obtain a final independent review, fix substantive findings, rerun their
  covering checks, and report results and remaining physical limits.
