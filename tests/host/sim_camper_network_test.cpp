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
}  // namespace

int main() {
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
