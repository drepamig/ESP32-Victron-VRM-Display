# IPv4 repeater and Venus address display

Approved in chat on 2026-09-04. The user approved direct IPv4 access, automatic
local fallback, and a Venus OS settings page, and explicitly excluded an SSH
command/path from that page. Implement directly on `develop`.

## Intended behavior

Treat the selected upstream Wi-Fi and ESP32 AP as one trusted IPv4 LAN.
Starlink provides the internet boundary. Replace the outbound-only NAPT
requirement with bidirectional IPv4 forwarding through MAC translation.
Venus uses DHCP and obtains its address from the upstream router while linked.
An upstream computer connects to Venus's address directly, including TCP 22;
individual port forwards and upstream static routes are unnecessary.

Keep the existing private AP SSID/password, explicit upstream selection,
connection rollback, capped retry, physical provisioning controls, and separate
GX/WAN health. Preserve saved profiles, touch calibration, and time settings.
IPv6, multicast discovery, and full Ethernet MAC transparency are outside scope.

## Address lifecycle and recovery

The bridge observes IPv4/ARP and DHCP traffic only for associated AP clients.
It learns the mapping between each client's Wi-Fi MAC and current IPv4 address,
including DHCP acknowledgments, lease expiry, and address changes. Fixed-size
tables and bounded parsing prevent packet traffic from allocating unlimited RAM.
The ESP32's own network traffic must continue to work, including Modbus requests
to AP clients that share its upstream subnet.

Keep the forwarding/DHCP mode tied to radio association plus valid upstream
IPv4 configuration, independently of internet/DNS validation. A DNS/internet
outage alone must not renumber the LAN. On loss of the Wi-Fi uplink, stop
upstream forwarding and provide the existing local 192.168.50.0/24 DHCP network.
Stop the local DHCP server before enabling upstream DHCP forwarding. Disconnect
AP clients on a change between addressing domains so they request fresh leases;
the AP service and credentials remain available. A brief interruption during
this change is accepted. Cold boot with no upstream supports local telemetry.

Clear current address mappings across network-domain changes and client
disconnection; never treat a prior lease from another network as current.
Preserve the last known Venus address for display, with an explicit stale label.

## Venus identity and telemetry

Use observed AP-client addresses as candidates and confirm a Venus/GX using
successful reads of the existing Victron Modbus system registers. Do not scan
the upstream subnet. Bind the confirmed device's Wi-Fi MAC in a dedicated NVS
record and subsequently follow that device's address. Candidate selection is
bounded and does not replace an established identity with a different client.
Refresh the Modbus target when its address changes, closing the previous TCP
connection. A response from an old address generation cannot mark the new
target healthy. Modbus failures do not erase a currently observed DHCP address.

## Settings page

Add `Venus OS` to the existing dashboard gear Settings menu. Its read-only page
shows the current IPv4 address prominently and the connection state. Before an
address is known show `Not found`. When only a prior address is available, label
it `Last seen` and show disconnected status. Never display an SSH command/path.
The address displayed and the Modbus target come from the same tracker.

Follow existing Back, inactivity, release-guard, rendering, and interaction
ownership behavior. Page updates are driven by status changes. Preserve the
current Time and Wi-Fi controls and their touch positions where possible.

## Implementation boundaries

Separate a host-testable Ethernet/IPv4/ARP/DHCP forwarding core from a pinned
Arduino/lwIP adapter. Keep low-level hooks on the TCP/IP thread and transfer
bounded snapshots to the application; never draw, write NVS, or probe Modbus
inside a packet callback. Adapt portal client validation to AP membership
instead of assuming all clients have a 192.168.50.x address.

The simulator uses deterministic address and outage fixtures. It verifies UI
and policy; it does not attest real Wi-Fi forwarding. The production build
must include and link the real adapter, with no silent return to NAPT.

## Acceptance

Host tests cover packet routing, checksums where changed, malformed frames,
DHCP identity/expiry, multiple clients, client removal, target changes, stale
results, radio-vs-internet outage, transition ordering, and settings navigation.
Run firmware and local simulator builds. Use targeted Velxio scenarios for the
Venus page, time-menu regression, connection/outage transitions, and retained
settings. Review actual images before promoting recorded local captures.

Physical acceptance remains explicit: upstream DHCP, upstream-to-Venus SSH,
ESP32-to-Venus Modbus, internet traffic, uplink loss/recovery, cold boot without
Starlink, and preservation of credentials/calibration. No Wokwi cloud run is
needed. Deployment and Venus DHCP migration require actual device access and
must be recorded separately from local test success.

## Research references

- Current implementation: `CamperNetwork.cpp`, `TcpModbusCycleSource.cpp`,
  `ProvisioningPortal.cpp`, and `SettingsUi.cpp`.
- [ESP32 repeater implementation](https://github.com/martin-ger/esp32_nat_router/tree/esp32_wifi_repeater)
  demonstrates the MAC-translation/netif approach; it is a research reference,
  not an unpinned runtime dependency.
