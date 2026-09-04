#include <cstdlib>
#include <iostream>

#include "../../VictronCYD_Modbus/SimCamperNetwork.h"

namespace {
void check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

void testWanFixtureLifecycle() {
  SimCamperNetwork network;
  network.begin("Bench", "dummy-pass-123", 0);
  for (const char* value : {"offline", "validating", "online"}) {
    check(!network.setWanFixture(value), "WAN fixture rejects fresh boot");
  }
  NetworkProfile profile;
  profile.ssid = "Bench-Open";
  network.connect(profile, 0);
  check(!network.setWanFixture("online") && network.status().wanPhase == WanPhase::Connecting,
        "WAN fixture cannot shortcut a pending connection");
  network.poll(1000);
  check(network.setWanFixture("offline"), "established attempt accepts offline WAN fixture");
  network.poll(301000);
  check(network.status().wanPhase == WanPhase::Offline && !network.pendingProfileConnected() &&
            network.status().upstreamAddress == IPAddress() && network.status().upstreamRssi == 0,
        "offline fixture clears readiness and remains offline across five minutes");
  check(network.setWanFixture("validating") && network.pendingProfileConnected() &&
            network.status().wanPhase == WanPhase::Validating &&
            network.status().upstreamAddress == IPAddress(192, 0, 2, 25) &&
            network.status().upstreamRssi == -48,
        "validating restores nominal station readiness without accepting pending owner");
  for (const char* value : {static_cast<const char*>(nullptr), "", "Online", "online ", "connecting", "offline=online"}) {
    check(!network.setWanFixture(value) && network.status().wanPhase == WanPhase::Validating &&
              network.pendingProfileConnected() && network.status().upstreamRssi == -48,
          "invalid WAN values preserve phase, readiness and pending ownership");
  }
  network.acceptPendingProfile();
  check(network.setWanFixture("online") && !network.pendingProfileConnected(),
        "WAN fixture never resurrects accepted pending ownership");
  network.setApFixture(true, 3);
  check(network.setWanFixture("offline") && network.setWanFixture("validating") &&
            network.setWanFixture("online") && network.status().wanPhase == WanPhase::Online &&
            network.status().apReady && network.status().apClientCount == 3,
        "repeated outage and recovery preserve eligibility and AP client fixture");
  NetworkProfile invalid;
  check(!network.connect(invalid, 301001) && network.setWanFixture("online"),
        "rejected connect does not clear established fixture eligibility");
  network.connect(profile, 301002);
  check(!network.setWanFixture("offline"), "accepted new connect clears prior eligibility");
  network.setConnectFixture("failure");
  network.poll(302002);
  check(!network.setWanFixture("online") && network.status().wanPhase == WanPhase::Offline,
        "failed replacement cannot inherit established fixture eligibility");
  network.cancelPendingProfile();
  check(!network.setWanFixture("online"), "cancelled attempt has no fixture eligibility");
  network.setConnectFixture("success");
  network.connect(profile, 302003);
  network.acceptPendingProfile();
  network.poll(303003);
  check(network.setWanFixture("offline") && network.setWanFixture("online") &&
            !network.pendingProfileConnected(),
        "acceptance before establishment preserves ownership through fixture recovery");
  network.disconnectUpstream();
  check(!network.setWanFixture("online"), "disconnect clears fixture eligibility");
  network.connect(profile, 303004);
  network.poll(304004);
  network.resetFixtures();
  check(!network.setWanFixture("online") && network.status().wanPhase == WanPhase::Offline,
        "reset clears fixture eligibility");
}
}  // namespace

int main() {
  testWanFixtureLifecycle();
  SimCamperNetwork network;
  check(!network.begin("Bench", "too-short", 0),
        "simulated AP preserves production password validation");
  check(network.begin("Bench", "dummy-pass-123", 0), "simulated AP starts");
  check(network.status().apReady && network.status().apClientCount == 1,
        "default AP fixture is deterministic");

  check(network.setScanFixture("nominal"), "nominal scan fixture accepted");
  check(network.startScan() && network.scanPhase() == ScanPhase::Running,
        "scan begins asynchronously");
  check(network.scanComplete(), "nominal scan completes deterministically");
  ScanResult results[4];
  const size_t count = network.scanResults(results, 4);
  check(count == 3 && results[0].ssid == String("Bench-Protected") &&
            results[0].rssi == -42 && results[0].encryptionType != 0,
        "nominal scan provides stable sorted protected network");
  check(results[1].ssid == String("Bench-Open") && results[1].encryptionType == 0,
        "nominal scan provides deterministic open network");

  check(network.setScanFixture("failure") && network.startScan(),
        "failed scan can start");
  check(!network.scanComplete() && network.scanPhase() == ScanPhase::Failed,
        "failure fixture reaches failed phase");
  network.clearScanFailure();
  check(network.scanPhase() == ScanPhase::Idle, "scan failure can be cleared");
  check(!network.setScanFixture("unknown"), "unknown scan fixture rejected");

  NetworkProfile profile;
  profile.ssid = "Bench-Protected";
  profile.passphrase = "dummy-wifi-pass";
  check(network.setConnectFixture("success"), "success connection fixture accepted");
  check(network.connect(profile, 10) && network.status().wanPhase == WanPhase::Connecting,
        "connect starts in connecting phase");
  network.poll(1010);
  check(network.pendingProfileConnected() && network.status().wanPhase == WanPhase::Online &&
            network.status().upstreamRssi == -48 &&
            network.status().upstreamAddress == IPAddress(192, 0, 2, 25),
        "success fixture produces deterministic online state");
  network.acceptPendingProfile();
  check(!network.pendingProfileConnected(), "accept consumes pending state");

  check(network.setConnectFixture("failure"), "failure connection fixture accepted");
  check(network.connect(profile, 20), "failed fixture still starts attempt");
  network.poll(1020);
  check(network.status().wanPhase == WanPhase::Offline &&
            !network.pendingProfileConnected(),
        "failure fixture produces offline state");
  network.cancelPendingProfile();

  network.setApFixture(false, 0);
  check(!network.status().apReady && network.status().apClientCount == 0,
        "AP fixture is controllable");
  network.resetFixtures();
  check(network.status().apReady && network.status().apClientCount == 1 &&
            network.setScanFixture("nominal") && network.setConnectFixture("success"),
        "reset restores deterministic defaults");
  check(!network.setConnectFixture("sometimes"), "unknown connection fixture rejected");
  check(network.connect(profile, 2000), "saved-profile connection starts");
  network.acceptPendingProfile();
  network.poll(3000);
  check(network.status().wanPhase == WanPhase::Online && !network.pendingProfileConnected(),
        "accepting before connection completes must not resurrect pending state");
  network.cancelPendingProfile();
  check(network.status().wanPhase == WanPhase::Online,
        "cancel is a no-op after profile acceptance, as in production");
  network.disconnectUpstream();
  check(network.status().wanPhase == WanPhase::Offline,
        "explicit disconnect still disconnects an accepted profile");
  return 0;
}
