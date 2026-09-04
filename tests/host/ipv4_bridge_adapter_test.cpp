#include <iostream>
#include "Ipv4BridgeSdkFake.h"
#include "Ipv4Bridge.h"
namespace {
int failures = 0;
std::vector<uint8_t> lastLocalFrame;
void check(bool ok, const char* label) { if (!ok) { ++failures; std::cerr << "FAIL: " << label << '\n'; } }
err_t local(pbuf* p, netif* n) {
  if (FakeBridgeSdk::inputFails) return ERR_IF;
  lastLocalFrame.resize(p->tot_len); pbuf_copy_partial(p, lastLocalFrame.data(), p->tot_len, 0);
  if (n == &FakeBridgeSdk::ap) ++FakeBridgeSdk::localAp; else ++FakeBridgeSdk::localSta;
  pbuf_free(p); return ERR_OK;
}
err_t output(netif*, pbuf*, const ip4_addr_t*) { ++FakeBridgeSdk::originalOutputs; return ERR_OK; }
err_t tx(netif* n, pbuf* p) {
  std::vector<uint8_t> frame(p->tot_len); pbuf_copy_partial(p, frame.data(), p->tot_len, 0);
  (n == &FakeBridgeSdk::ap ? FakeBridgeSdk::apFrames : FakeBridgeSdk::staFrames).push_back(frame);
  return ERR_OK;
}
const uint8_t client[6] = {2, 3, 4, 5, 6, 7};
std::vector<uint8_t> ipFrame(uint32_t source, uint32_t dest) {
  std::vector<uint8_t> f(54); std::memcpy(f.data() + 6, client, 6);
  f[12] = 8; f[14] = 0x45; f[17] = 40; f[23] = 6;
  for (int i=0; i<4; ++i) { f[26+i] = source >> (24-8*i); f[30+i] = dest >> (24-8*i); }
  return f;
}
pbuf* packet(const std::vector<uint8_t>& f, bool chained = false) {
  auto* p = pbuf_alloc(PBUF_RAW, chained ? 10 : f.size(), PBUF_RAM);
  std::memcpy(p->payload, f.data(), p->len);
  if (chained) { p->next = pbuf_alloc(PBUF_RAW, f.size()-10, PBUF_RAM); std::memcpy(p->next->payload, f.data()+10, f.size()-10); p->tot_len = f.size(); }
  return p;
}
void receive(netif& n, const std::vector<uint8_t>& f, bool chained = false) {
  auto* p = packet(f, chained); if (n.input(p, &n) != ERR_OK) pbuf_free(p);
}
}
int main() {
  using namespace FakeBridgeSdk;
  ap.input = sta.input = local; ap.output = sta.output = output; ap.linkoutput = sta.linkoutput = tx;
  ap.hwaddr[0] = sta.hwaddr[0] = 2; ap.hwaddr[5] = 1; sta.hwaddr[5] = 2;
  Ipv4Bridge bridge;
  check(bridge.begin(), "cold boot succeeds");
  check(events == std::vector<std::string>({"stop","ip","lease","start"}), "cold fallback DHCP ordering without deauth");
  auto boot = bridge.snapshot(0);
  check(boot.ready && !boot.bridged && boot.generation != 0 && leaseMinutes == 1, "cold fallback status and one minute lease");
  events.clear(); bridge.poll(false, 1); bridge.begin();
  check(events.empty() && bridge.snapshot(1).generation == boot.generation, "repeat begin/poll leaves domain alone");
  associated.num = 1; std::memcpy(associated.sta[0].mac, client, 6);
  esp_netif_pair_mac_ip_t lease; std::memcpy(lease.mac, client, 6); lease.ip.addr = lwip_htonl(0xc0a83264); leases.push_back(lease);
  bridge.poll(false, 2);
  check(bridge.snapshot(2).count == 1 && bridge.snapshot(2).clients[0].address == 0xc0a83264, "fallback tracks local DHCP by MAC");
  staInfo.ip.addr = lwip_htonl(0xc0a80110); staInfo.netmask.addr = lwip_htonl(0xffffff00); staInfo.gw.addr = lwip_htonl(0xc0a80101);
  events.clear(); bridge.poll(true, 3);
  check(events == std::vector<std::string>({"stop","ip","deauth"}), "bridge stops DHCP and clears AP IPv4 before deauth");
  const auto failedGeneration = bridge.snapshot(3).generation;
  check(bridge.snapshot(5).ready && bridge.snapshot(5).bridged && !apInfo.ip.addr, "bridge uses zero AP IP");
  events.clear(); bridge.poll(true, 6);
  check(events.empty() && bridge.snapshot(6).generation == failedGeneration, "DNS-only failure cannot change ready radio domain");
  receive(ap, ipFrame(0xc0a80164, 0xc0a80101), true); drain();
  check(staFrames.size() == 1 && std::memcmp(staFrames.back().data()+6, sta.hwaddr, 6) == 0 && livePbufs == 0, "chained RX forwards private frame with balanced ownership");
  check(bridge.snapshot(7).count == 1, "valid AP traffic learns client");
  auto* outgoing = pbuf_alloc(PBUF_RAW, 40, PBUF_RAM); ip4_addr_t destination{lwip_htonl(0xc0a80164)};
  ++coreDepth;  // lwIP invokes IPv4 output under its core lock.
  check(sta.output(&sta, outgoing, &destination) == ERR_OK && apFrames.size() == 1 && originalOutputs == 0, "ESP traffic to known client uses AP output");
  allocationFails = true;
  check(sta.output(&sta, outgoing, &destination) == ERR_MEM && originalOutputs == 0, "own output allocation failure does not leak upstream");
  allocationFails = false; stationListFails = true;
  check(sta.output(&sta, outgoing, &destination) == ERR_IF && originalOutputs == 0, "association query failure does not misroute known client upstream");
  stationListFails = false; pbuf_free(outgoing); --coreDepth;
  receive(ap, ipFrame(0xc0a80164, 0xc0a80110)); drain();
  check(localSta == 1 && livePbufs == 0 && std::memcmp(lastLocalFrame.data(), sta.hwaddr, 6) == 0,
        "AP management IP enters saved STA input with STA destination MAC once");
  const auto broadcast = ipFrame(0xc0a80101, 0xffffffff);
  receive(sta, broadcast); drain();
  check(localSta == 2 && lastLocalFrame == broadcast &&
        std::memcmp(apFrames.back().data()+6, ap.hwaddr, 6) == 0,
        "broadcast forwarding mutates only its private copy, keeping local original");
  inputFails = true; receive(ap, ipFrame(0xc0a80164, 0xc0a80110)); drain(); inputFails = false;
  check(livePbufs == 0, "saved input error frees original");
  queueFails = true; receive(ap, ipFrame(0xc0a80164, 0xc0a80101)); queueFails = false;
  check(livePbufs == 0 && callbacks.empty(), "queue rejection leaves caller ownership");
  for (int i=0; i<12; ++i) receive(ap, ipFrame(0xc0a80164, 0xc0a80101));
  check(callbacks.size() <= 8 && livePbufs <= 8, "RX descriptor bound enforced");
  events.clear(); failOperation = "start"; bridge.poll(false, 8);
  check(!bridge.snapshot(8).ready && events == std::vector<std::string>({"stop","ip","lease","start"}), "fallback start failure stays unready and does not deauth");
  failOperation = "deauth"; events.clear(); bridge.poll(false, 8);
  check(!bridge.snapshot(8).ready && events == std::vector<std::string>({"start","deauth"}), "deauth failure resumes after successful DHCP start");
  failOperation.clear(); events.clear(); bridge.poll(false, 8); const auto sent = staFrames.size(); drain();
  check(staFrames.size() == sent && livePbufs == 0, "queued old-domain packets discarded");
  check(events == std::vector<std::string>({"deauth"}) && bridge.snapshot(8).ready, "deauth retry completes fallback without repeating DHCP setup");
  const auto fallbackGeneration = bridge.snapshot(8).generation;
  events.clear(); bridge.poll(false, 9);
  check(events.empty() && bridge.snapshot(9).generation == fallbackGeneration, "offline repeats do not deauth again");
  bridge.poll(true, 10); const auto generation = bridge.snapshot(10).generation;
  staInfo.ip.addr = lwip_htonl(0xc0a80111); events.clear(); bridge.poll(true, 11);
  check(bridge.snapshot(11).generation != generation && events.back() == "deauth", "upstream address change creates new domain");
  associated.num = 0; bridge.poll(true, 12);
  check(bridge.snapshot(12).count == 0, "client disassociation removes mapping");
  ipInfoFails = true; events.clear(); bridge.poll(true, 13);
  check(bridge.snapshot(13).ready && bridge.snapshot(13).bridged && events.empty(), "transient IP query failure preserves usable bridge without DHCP mutation");
  ipInfoFails = false; bridge.poll(true, 14);
  check(bridge.snapshot(14).ready && events.empty(), "transient IP query failure recovers same domain without renumbering");
  bridge.poll(false, 100);
  associated.num = 1; std::memcpy(associated.sta[0].mac, client, 6);
  const auto initialFallbackGeneration = bridge.snapshot(100).generation;
  failBridgeIp = true; events.clear(); bridge.poll(true, 101);
  check(bridge.snapshot(101).ready && !bridge.snapshot(101).bridged && dhcpRunning &&
        apInfo.ip.addr == lwip_htonl(0xc0a83201), "failed bridge configuration restores usable local DHCP");
  check(bridge.snapshot(101).generation != initialFallbackGeneration,
        "failed activation and fallback restoration invalidate old generations");
  const auto localBefore = localAp;
  receive(ap, ipFrame(0xc0a83264, 0xc0a83201)); drain();
  check(localAp == localBefore + 1 && livePbufs == 0, "restored fallback accepts AP client traffic");
  const auto restoredGeneration = bridge.snapshot(101).generation;
  events.clear(); bridge.poll(true, 102); bridge.poll(true, 5100);
  check(events.empty() && bridge.snapshot(5100).generation == restoredGeneration,
        "persistent bridge error does not retry or flap clients on each poll");
  bridge.poll(true, 5101);
  check(bridge.snapshot(5101).ready && !bridge.snapshot(5101).bridged && dhcpRunning && !events.empty(),
        "persistent bridge error retries after five seconds and restores fallback again");
  failBridgeIp = false; events.clear(); bridge.poll(true, 10100);
  check(events.empty(), "successful retry still waits for backoff deadline");
  bridge.poll(true, 10101);
  check(bridge.snapshot(10101).bridged && !dhcpRunning, "recovered SDK activates bridge at retry deadline");
  bridge.poll(false, 11000);
  failOnceOperation = "deauth"; events.clear(); bridge.poll(true, 11001);
  check(bridge.snapshot(11001).ready && !bridge.snapshot(11001).bridged && dhcpRunning,
        "activation deauth failure also restores fallback DHCP");
  bridge.poll(false, 12000);  // A real radio loss cancels the prior bridge retry.
  failBridgeIp = true; failOperation = "start"; events.clear(); bridge.poll(true, 12001);
  check(!bridge.snapshot(12001).ready && !dhcpRunning,
        "fallback restoration failure remains honestly unready");
  events.clear(); bridge.poll(true, 12002);
  check(events.empty(), "failed fallback restoration retries are bounded");
  failOperation.clear(); bridge.poll(true, 13001);
  check(bridge.snapshot(13001).ready && !bridge.snapshot(13001).bridged && dhcpRunning,
        "fallback restoration resumes failed stage before any new bridge attempt");
  ipInfoFails = true; events.clear(); bridge.poll(true, 18001);
  check(bridge.snapshot(18001).ready && !bridge.snapshot(18001).bridged && dhcpRunning && events.empty(),
        "IP query failure preserves usable local fallback at retry deadline");
  ipInfoFails = false; failBridgeIp = false;
  bridge.poll(false, 19000); events.clear(); failOnceOperation = "stop"; bridge.poll(true, 19001);
  check(bridge.snapshot(19001).ready && !bridge.snapshot(19001).bridged && dhcpRunning &&
        events == std::vector<std::string>({"stop","stop","ip","lease","start","deauth"}),
        "one-shot DHCP stop failure also restores fallback before timed retry");
  bridge.poll(false, UINT32_MAX - 2000); failBridgeIp = true;
  bridge.poll(true, UINT32_MAX - 1000); events.clear();
  bridge.poll(true, 3998);
  check(events.empty(), "bridge retry does not fire early across timer wrap");
  bridge.poll(true, 3999);
  check(!events.empty() && bridge.snapshot(3999).ready && dhcpRunning,
        "bridge retry fires on wrapped deadline and restores fallback");
  failBridgeIp = false;
  if (failures) return 1;
  std::cout << "IPv4 bridge adapter tests passed\n";
}
