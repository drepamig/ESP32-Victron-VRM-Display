#include <cstdint>
#include <iostream>
#include <string>
#include <type_traits>
#include <vector>

#include "Network.h"
#include "WiFi.h"
#include "freertos/task.h"
#include "../../VictronCYD_Modbus/CamperNetwork.h"

namespace {

static_assert(!std::is_copy_constructible<CamperNetwork>::value,
              "CamperNetwork must not copy queue/task ownership");
static_assert(!std::is_copy_assignable<CamperNetwork>::value,
              "CamperNetwork must not copy-assign queue/task ownership");
static_assert(!std::is_move_constructible<CamperNetwork>::value,
              "CamperNetwork must not move while a validation worker may run");
static_assert(!std::is_move_assignable<CamperNetwork>::value,
              "CamperNetwork must not move-assign while a validation worker may run");

int failures = 0;

void check(bool condition, const char* name) {
  if (!condition) {
    std::cerr << "FAIL: " << name << '\n';
    ++failures;
  }
}

void resetFakes() { WiFi.reset(); Network.reset(); FakeRtos::reset(); }

NetworkProfile profile(const char* ssid = "selected", const char* passphrase = "not-a-real-secret") {
  NetworkProfile value;
  value.ssid = ssid;
  value.passphrase = passphrase;
  return value;
}

void startGateway(CamperNetwork& gateway) {
  check(gateway.begin("Camper", "abcdefghijkl", 0), "gateway starts");
}

void associateStation() {
  WiFi.connected = true;
  WiFi.stationSsid = WiFi.beginSsids.back().c_str();
  WiFi.stationAddress = IPAddress(10, 20, 30, 40);
  WiFi.stationRssi = -47;
}

void testApValidationOrderAndSingleStartup() {
  resetFakes();
  CamperNetwork gateway;
  check(!gateway.begin("Camper", "12345678901", 0), "AP rejects eleven-byte password");
  check(WiFi.events.empty(), "rejected AP password has no WiFi side effects");
  check(!gateway.begin("Camper", "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ123456789012", 0),
        "AP rejects sixty-four-byte password");
  check(WiFi.events.empty(), "oversized AP password has no WiFi side effects");

  check(gateway.begin("Camper", "123456789012", 0), "AP accepts twelve-byte password");
  check(WiFi.events.size() >= 5 && WiFi.events[0] == "persistent" &&
            WiFi.events[1] == "mode" && WiFi.events[2] == "config" &&
            WiFi.events[3] == "create" && WiFi.events[4] == "napt",
        "RAM-only policy precedes AP mode/config/create/NAPT order");
  check(WiFi.persistentCalls == 1 && !WiFi.persistentValue,
        "WiFi persistence is disabled before initialization");
  check(WiFi.modeCalls == 1 && WiFi.modeValue == WIFI_AP_STA, "AP+STA mode once");
  check(WiFi.AP.configCalls.size() == 1, "AP configured once");
  if (!WiFi.AP.configCalls.empty()) {
    const FakeAccessPoint::ConfigCall& config = WiFi.AP.configCalls.front();
    check(config.local == IPAddress(192, 168, 50, 1) && config.gateway == IPAddress(192, 168, 50, 1) &&
              config.subnet == IPAddress(255, 255, 255, 0) &&
              config.leaseStart == IPAddress(192, 168, 50, 100) && config.dns == IPAddress(1, 1, 1, 1),
          "AP uses exact private subnet, lease start, and DNS");
  }
  check(WiFi.AP.createChannel == 6 && !WiFi.AP.createHidden && WiFi.AP.createMaxConnections == 4,
        "AP uses exact channel, visibility, and client limit");
  check(WiFi.AP.naptCalls == 1 && !WiFi.autoReconnect, "outbound NAPT on and implicit reconnect off");
  check(FakeRtos::lastQueueLength == 1 && FakeRtos::lastQueueItemSize == sizeof(bool),
        "WAN validation queue has length one and carries only success/failure");
  const size_t eventCount = WiFi.events.size();
  check(gateway.begin("ignored", "abcdefghijkl", 1), "repeat begin reports existing AP");
  check(WiFi.events.size() == eventCount, "repeat begin does not restart AP/NAPT");
  check(gateway.status().apReady, "status reports AP ready");
}

void testInitialAssociationAndSingleDnsWorker() {
  resetFakes();
  CamperNetwork gateway;
  startGateway(gateway);
  check(gateway.connect(profile(), 100), "profile connection accepted");
  check(WiFi.beginSsids == std::vector<std::string>{"selected"}, "initial WiFi.begin exactly once");
  check(gateway.status().wanPhase == WanPhase::Connecting, "connection begins in Connecting");

  WiFi.connected = true;
  gateway.poll(101);
  check(gateway.status().wanPhase == WanPhase::Connecting && FakeRtos::createCalls == 0,
        "association without DHCP does not start validation");
  associateStation();
  gateway.poll(102);
  check(gateway.status().wanPhase == WanPhase::Validating, "association plus DHCP starts validation");
  check(gateway.pendingProfileConnected(), "pending profile connected after association and DHCP");
  check(Network.hostCalls == 0, "poll never performs blocking DNS");
  check(FakeRtos::createCalls == 1 && FakeRtos::pendingTasks.size() == 1 &&
            FakeRtos::pendingTasks.front().core == 0,
        "one validation worker pinned to core zero");
  gateway.poll(103);
  check(FakeRtos::createCalls == 1, "poll does not create a second DNS worker");

  if (!FakeRtos::pendingTasks.empty()) FakeRtos::runNextTask();
  check(Network.hostCalls == 1 && Network.lastHost == "vrm.victronenergy.com",
        "worker resolves only the VRM host");
  gateway.poll(104);
  CamperNetworkStatus status = gateway.status();
  check(status.wanPhase == WanPhase::Online && status.upstreamAddress == IPAddress(10, 20, 30, 40) &&
            status.upstreamRssi == -47,
        "DNS success exposes online station status");
}

void testStartupFailuresStayHonestAndRetrySafely() {
  resetFakes();
  CamperNetwork badConfig;
  WiFi.AP.configResult = false;
  check(!badConfig.begin("Camper", "abcdefghijkl", 0), "AP config failure rejects startup");
  check(WiFi.AP.createCalls == 0 && WiFi.AP.naptCalls == 0 && !badConfig.status().apReady,
        "AP config failure prevents AP create and NAPT");

  resetFakes();
  CamperNetwork noQueue;
  FakeRtos::queueCreationSucceeds = false;
  check(!noQueue.begin("Camper", "abcdefghijkl", 0), "queue allocation failure rejects startup");
  check(WiFi.events.empty() && !noQueue.status().apReady,
        "queue allocation failure has no WiFi side effects and stays not ready");
  check(!noQueue.connect(profile(), 1) && WiFi.beginSsids.empty(),
        "queue allocation failure cannot accept an uplink");
  FakeRtos::queueCreationSucceeds = true;
  check(noQueue.begin("Camper", "abcdefghijkl", 2), "startup retries after queue allocation recovers");
  check(FakeRtos::queueCreateCalls == 2 && WiFi.AP.createCalls == 1 && WiFi.AP.naptCalls == 1,
        "queue retry performs one successful AP startup");
}

void testStaleDnsResultCannotValidateNewLifecycle() {
  resetFakes();
  CamperNetwork replaced;
  startGateway(replaced);
  replaced.connect(profile("first"), 0);
  associateStation();
  replaced.poll(1);
  check(FakeRtos::pendingTasks.size() == 1, "first lifecycle starts worker A");
  replaced.connect(profile("second"), 2);
  replaced.poll(3);
  check(replaced.status().wanPhase == WanPhase::Connecting && FakeRtos::createCalls == 1,
        "replacement stays Connecting while stale worker A is active");
  if (!FakeRtos::pendingTasks.empty()) FakeRtos::runNextTask();
  replaced.poll(4);
  check(replaced.status().wanPhase == WanPhase::Connecting && FakeRtos::createCalls == 1,
        "stale SSID readiness cannot validate the replacement lifecycle");
  associateStation();
  replaced.poll(5);
  check(replaced.status().wanPhase != WanPhase::Online && FakeRtos::createCalls == 2 &&
            FakeRtos::pendingTasks.size() == 1,
        "worker A result is ignored and worker B validates replacement");
  if (!FakeRtos::pendingTasks.empty()) FakeRtos::runNextTask();
  replaced.poll(6);
  check(replaced.status().wanPhase == WanPhase::Online, "worker B can validate replacement");

  resetFakes();
  CamperNetwork cancelled;
  startGateway(cancelled);
  cancelled.connect(profile(), 0);
  associateStation();
  cancelled.poll(1);
  cancelled.cancelPendingProfile();
  associateStation();
  if (!FakeRtos::pendingTasks.empty()) FakeRtos::runNextTask();
  cancelled.poll(2);
  check(cancelled.status().wanPhase == WanPhase::Offline && FakeRtos::createCalls == 1,
        "cancelled lifecycle ignores worker A and does not validate unknown station");

  resetFakes();
  CamperNetwork disconnected;
  startGateway(disconnected);
  disconnected.connect(profile(), 0);
  associateStation();
  disconnected.poll(1);
  disconnected.disconnectUpstream();
  associateStation();
  if (!FakeRtos::pendingTasks.empty()) FakeRtos::runNextTask();
  disconnected.poll(2);
  check(disconnected.status().wanPhase == WanPhase::Offline && FakeRtos::createCalls == 1,
        "disconnected lifecycle ignores worker A and does not validate unknown station");
}

void testDnsFailureAndThirtySecondRevalidation() {
  resetFakes();
  CamperNetwork gateway;
  startGateway(gateway);
  gateway.connect(profile(), 0);
  associateStation();
  Network.result = false;
  gateway.poll(1);
  if (!FakeRtos::pendingTasks.empty()) FakeRtos::runNextTask();
  gateway.poll(2);
  check(gateway.status().wanPhase == WanPhase::Offline && WiFi.disconnectCalls == 0,
        "DNS failure leaves associated station offline");
  gateway.poll(30001);
  check(FakeRtos::createCalls == 1, "DNS failure waits full thirty seconds");
  gateway.poll(30002);
  check(FakeRtos::createCalls == 2 && gateway.status().wanPhase == WanPhase::Validating,
        "DNS failure revalidates at thirty seconds");

  Network.result = true;
  if (!FakeRtos::pendingTasks.empty()) FakeRtos::runNextTask();
  gateway.poll(30003);
  check(gateway.status().wanPhase == WanPhase::Online, "successful retry moves online");
  gateway.poll(60002);
  check(FakeRtos::createCalls == 2, "online revalidation also waits thirty seconds");
  gateway.poll(60003);
  check(FakeRtos::createCalls == 3 && gateway.status().wanPhase == WanPhase::Validating,
        "online connection revalidates every thirty seconds");
}

void testRetryCadenceCapResetAndWraparound() {
  resetFakes();
  CamperNetwork gateway;
  startGateway(gateway);
  gateway.connect(profile("fixed-profile"), 0);
  gateway.poll(4999);
  check(WiFi.beginSsids.size() == 1, "no reconnect before five seconds");
  const uint32_t deadlines[] = {5000, 15000, 35000, 75000, 135000, 195000};
  for (uint32_t deadline : deadlines) gateway.poll(deadline);
  check(WiFi.beginSsids.size() == 7, "reconnect cadence reaches sixty-second cap");
  bool fixedOnly = true;
  for (const std::string& ssid : WiFi.beginSsids) fixedOnly = fixedOnly && ssid == "fixed-profile";
  check(fixedOnly, "reconnect never roams to another profile");

  associateStation();
  gateway.poll(195001);
  if (!FakeRtos::pendingTasks.empty()) FakeRtos::runNextTask();
  gateway.poll(195002);
  WiFi.connected = false;
  WiFi.stationAddress = IPAddress();
  gateway.poll(195003);
  gateway.poll(200002);
  check(WiFi.beginSsids.size() == 7, "online reset still waits five seconds");
  gateway.poll(200003);
  check(WiFi.beginSsids.size() == 8, "online transition resets retry backoff to five seconds");

  resetFakes();
  CamperNetwork wrapped;
  startGateway(wrapped);
  wrapped.connect(profile(), UINT32_MAX - 1000);
  wrapped.poll(3998);
  check(WiFi.beginSsids.size() == 1, "wraparound retry does not fire early");
  wrapped.poll(3999);
  check(WiFi.beginSsids.size() == 2, "wraparound retry fires on deadline");
}

void testAcceptCancelAndDisconnectKeepAp() {
  resetFakes();
  CamperNetwork accepted;
  startGateway(accepted);
  accepted.connect(profile(), 0);
  associateStation();
  accepted.poll(1);
  accepted.acceptPendingProfile();
  check(WiFi.disconnectCalls == 0 && !accepted.pendingProfileConnected(),
        "accept clears pending credentials without stopping station");
  WiFi.connected = false;
  WiFi.stationAddress = IPAddress();
  accepted.poll(2);
  accepted.poll(10001);
  check(WiFi.beginSsids.size() == 1, "accepted profile retry waits until its deadline");
  accepted.poll(10002);
  check(WiFi.beginSsids.size() == 2,
        "accepted profile remains selected for automatic retry while pending clears");
  check(WiFi.beginSsids.size() == 2 && WiFi.beginSsids[0] == "selected" &&
            WiFi.beginSsids[1] == "selected",
        "accepted profile retry uses the retained selected SSID");
  check(accepted.status().apReady, "accepted profile retry keeps the AP ready");

  resetFakes();
  CamperNetwork cancelled;
  startGateway(cancelled);
  cancelled.connect(profile(), 0);
  cancelled.cancelPendingProfile();
  cancelled.poll(60000);
  check(WiFi.disconnectCalls == 1 && WiFi.beginSsids.size() == 1 &&
            WiFi.disconnectEraseAp == std::vector<bool>{true} &&
            cancelled.status().wanPhase == WanPhase::Offline && cancelled.status().apReady,
        "terminal cancel stops the station attempt and erases transient STA configuration");

  resetFakes();
  CamperNetwork rollback;
  startGateway(rollback);
  rollback.connect(profile(), 0);
  rollback.cancelPendingProfile(false);
  check(WiFi.disconnectCalls == 1 && WiFi.disconnectEraseAp == std::vector<bool>{false} &&
            rollback.status().wanPhase == WanPhase::Offline && rollback.status().apReady,
        "rollback-ready cancel leaves transient STA erase to the immediate replacement");

  resetFakes();
  CamperNetwork disconnected;
  startGateway(disconnected);
  disconnected.connect(profile(), 0);
  disconnected.disconnectUpstream();
  disconnected.poll(60000);
  check(WiFi.disconnectCalls == 1 && WiFi.beginSsids.size() == 1 &&
            WiFi.disconnectEraseAp == std::vector<bool>{true} &&
            disconnected.status().wanPhase == WanPhase::Offline && disconnected.status().apReady &&
            WiFi.AP.naptCalls == 1,
        "Clear All erases transient STA configuration while AP and NAPT remain");
}

void testEstablishedOutageStaysOfflineThroughEveryRetry(bool dhcpOnly, uint32_t origin) {
  resetFakes();
  Serial.clear();
  CamperNetwork gateway;
  startGateway(gateway);
  WiFi.AP.clients = 3;
  gateway.connect(profile("outage-profile"), origin);
  associateStation();
  gateway.poll(origin + 1);
  FakeRtos::runNextTask();
  gateway.poll(origin + 2);
  gateway.acceptPendingProfile();
  WiFi.connected = dhcpOnly;
  WiFi.stationAddress = IPAddress();
  const uint32_t lostAt = origin + 3;
  gateway.poll(lostAt);
  check(gateway.status().wanPhase == WanPhase::Offline,
        "established association or DHCP loss reports Offline on first poll");
  check(gateway.status().upstreamAddress == IPAddress() && gateway.status().upstreamRssi == 0,
        "outage clears upstream address and RSSI");
  const uint32_t deadlines[] = {5000, 15000, 35000, 75000, 135000, 195000, 255000};
  size_t expectedBegins = 1;
  for (uint32_t deadline : deadlines) {
    gateway.poll(lostAt + deadline - 1);
    check(WiFi.beginSsids.size() == expectedBegins &&
              gateway.status().wanPhase == WanPhase::Offline,
          "established outage stays Offline before each retry deadline");
    gateway.poll(lostAt + deadline);
    ++expectedBegins;
    check(WiFi.beginSsids.size() == expectedBegins &&
              gateway.status().wanPhase == WanPhase::Offline,
          "established outage retries exactly at deadline and stays Offline");
    gateway.poll(lostAt + deadline + 1);
    check(WiFi.beginSsids.size() == expectedBegins &&
              gateway.status().wanPhase == WanPhase::Offline,
          "retry does not restart schedule on the following poll");
  }
  gateway.poll(lostAt + 300000);
  check(gateway.status().wanPhase == WanPhase::Offline && WiFi.beginSsids.size() == 8,
        "five-minute outage remains Offline with capped retries");
  for (const auto& ssid : WiFi.beginSsids) {
    check(ssid == "outage-profile", "outage retries retain selected profile");
  }
  check(gateway.status().apReady && gateway.status().apClientCount == 3 &&
            WiFi.AP.createCalls == 1 && WiFi.AP.configCalls.size() == 1 &&
            WiFi.AP.naptCalls == 1 && WiFi.modeCalls == 1 && WiFi.disconnectCalls == 0,
        "five-minute outage leaves AP, NAPT and attached clients unchanged");
  associateStation();
  gateway.poll(lostAt + 300001);
  check(gateway.status().wanPhase == WanPhase::Validating && FakeRtos::createCalls == 2,
        "restored association and DHCP require fresh DNS validation");
  FakeRtos::runNextTask();
  gateway.poll(lostAt + 300002);
  check(gateway.status().wanPhase == WanPhase::Online && !gateway.pendingProfileConnected(),
        "fresh DNS restores Online without reopening accepted profile");
  check(Serial.output().find("not-a-real-secret") == std::string::npos,
        "outage does not emit profile credentials");
}

void testLossBeforeDnsAndStaleWorkerDrain() {
  resetFakes();
  CamperNetwork gateway;
  startGateway(gateway);
  gateway.connect(profile(), 0);
  associateStation();
  // Acceptance can precede the first network poll; establishment is independent of it.
  gateway.acceptPendingProfile();
  gateway.poll(1);
  WiFi.connected = false;
  gateway.poll(2);
  check(gateway.status().wanPhase == WanPhase::Offline,
        "accepted association is established before DNS succeeds");
  associateStation();
  gateway.poll(3);
  check(gateway.status().wanPhase == WanPhase::Validating && FakeRtos::createCalls == 1 &&
            FakeRtos::pendingTasks.size() == 1,
        "recovery reports Validating while only the invalid old worker drains");
  FakeRtos::runNextTask();
  gateway.poll(4);
  check(gateway.status().wanPhase == WanPhase::Validating && FakeRtos::createCalls == 2,
        "stale DNS success is ignored and fresh validation starts after drain");
  Network.result = false;
  FakeRtos::runNextTask();
  gateway.poll(5);
  check(gateway.status().wanPhase == WanPhase::Offline,
        "fresh DNS failure cannot reuse invalid success");
  WiFi.connected = false;
  gateway.poll(6);
  gateway.poll(20005);
  check(WiFi.beginSsids.size() == 1 && gateway.status().wanPhase == WanPhase::Offline,
        "DNS failure does not reset accumulated retry backoff");
  gateway.poll(20006);
  check(WiFi.beginSsids.size() == 2, "failed validation retains twenty-second next retry");
  associateStation();
  gateway.poll(20007);
  check(gateway.status().wanPhase == WanPhase::Validating, "restoration cancels DNS failure delay");
  Network.result = true;
  FakeRtos::runNextTask();
  gateway.poll(20008);
  check(gateway.status().wanPhase == WanPhase::Online, "fresh DNS eventually restores Online");
}

void testNewLifecycleDoesNotInheritEstablishment() {
  resetFakes();
  CamperNetwork gateway;
  startGateway(gateway);
  check(gateway.status().wanPhase == WanPhase::Offline, "fresh boot is Offline");
  gateway.connect(profile("first"), 0);
  associateStation();
  gateway.poll(1);
  FakeRtos::runNextTask();
  gateway.poll(2);
  gateway.acceptPendingProfile();
  gateway.connect(profile("second"), 3);
  gateway.poll(4);
  check(gateway.status().wanPhase == WanPhase::Connecting && FakeRtos::createCalls == 1,
        "new lifecycle ignores old SSID DHCP readiness");
  WiFi.connected = false;
  gateway.poll(5);
  check(gateway.status().wanPhase == WanPhase::Connecting,
        "old SSID never establishes replacement lifecycle");
  gateway.cancelPendingProfile(false);
  gateway.connect(profile("first"), 6);
  gateway.acceptPendingProfile();
  gateway.poll(7);
  check(gateway.status().wanPhase == WanPhase::Connecting,
        "rollback is a fresh Connecting lifecycle even after acceptance");
  associateStation();
  gateway.poll(8);
  WiFi.connected = false;
  gateway.poll(9);
  check(gateway.status().wanPhase == WanPhase::Offline,
        "rollback becomes established only after its own association and DHCP");
  gateway.disconnectUpstream();
  gateway.connect(profile(), 10);
  gateway.poll(11);
  check(gateway.status().wanPhase == WanPhase::Connecting,
        "disconnect clears establishment for a new attempt");
}

void testAsyncScanGatingFailureAndResults() {
  resetFakes();
  CamperNetwork gateway;
  startGateway(gateway);
  WiFi.scanStartResult = WIFI_SCAN_FAILED;
  check(!gateway.startScan() && gateway.scanPhase() == ScanPhase::Failed &&
            gateway.status().apReady,
        "immediate scan rejection latches Failed without stopping AP");
  gateway.clearScanFailure();
  check(gateway.scanPhase() == ScanPhase::Idle,
        "acknowledging scan failure returns scan lifecycle to Idle");

  WiFi.scanStartResult = WIFI_SCAN_RUNNING;
  check(gateway.startScan(), "async scan starts");
  check(gateway.scanPhase() == ScanPhase::Running,
        "successful scan start clears old failure and reports Running");
  check(!gateway.startScan() && WiFi.scanStartCalls == 2, "active scan gates duplicate start");
  check(WiFi.lastScanAsync && !WiFi.lastScanShowHidden, "scan is asynchronous and excludes hidden networks");
  WiFi.scanCompleteValue = WIFI_SCAN_RUNNING;
  check(!gateway.scanComplete(), "running scan is incomplete");
  WiFi.scanCompleteValue = WIFI_SCAN_FAILED;
  check(!gateway.scanComplete(), "failed scan is not complete");
  check(gateway.scanPhase() == ScanPhase::Failed && gateway.status().apReady,
        "asynchronous scan failure is terminal and keeps AP ready");
  check(gateway.startScan() && WiFi.scanStartCalls == 3, "failed scan releases start gate");
  check(gateway.scanPhase() == ScanPhase::Running,
        "new successful scan resets asynchronous failure");

  WiFi.scanRecords = {{String(""), -10, 1, 1}, {String("A"), -70, 2, 1},
                      {String("B"), -40, 4, 11}, {String("A"), -30, 3, 6},
                      {String("C"), -50, 5, 3}};
  WiFi.scanCompleteValue = 5;
  check(gateway.scanComplete(), "completed scan reports ready");
  check(gateway.scanPhase() == ScanPhase::Complete, "completed scan latches Complete");
  ScanResult output[2];
  const size_t copied = gateway.scanResults(output, 2);
  check(copied == 2, "scan respects output capacity");
  check(output[0].ssid == String("A") && output[0].rssi == -30 && output[0].encryptionType == 3 &&
            output[0].channel == 6,
        "scan deduplicates SSID using strongest record");
  check(output[1].ssid == String("B") && output[1].rssi == -40,
        "scan excludes empty SSID and sorts descending by RSSI");
  check(WiFi.scanDeleteCalls == 1, "scan storage deleted after copying");
  check(gateway.scanPhase() == ScanPhase::Idle, "consumed scan returns to Idle");
  check(gateway.scanResults(output, 2) == 0 && WiFi.scanDeleteCalls == 1,
        "consumed scan cannot be copied or deleted twice");
}

}  // namespace

int main() {
  // Regression: an old association and DHCP address must not accept a new SSID.
  resetFakes();
  {
    CamperNetwork gateway;
    startGateway(gateway);
    gateway.connect(profile("A"), 0);
    associateStation();
    gateway.acceptPendingProfile();
    gateway.connect(profile("B"), 100);
    check(!gateway.pendingProfileConnected(), "stale A address cannot complete B");
  }
  testApValidationOrderAndSingleStartup();
  testStartupFailuresStayHonestAndRetrySafely();
  testInitialAssociationAndSingleDnsWorker();
  testStaleDnsResultCannotValidateNewLifecycle();
  testDnsFailureAndThirtySecondRevalidation();
  testRetryCadenceCapResetAndWraparound();
  testAcceptCancelAndDisconnectKeepAp();
  testEstablishedOutageStaysOfflineThroughEveryRetry(false, 0);
  testEstablishedOutageStaysOfflineThroughEveryRetry(true, 0);
  testEstablishedOutageStaysOfflineThroughEveryRetry(false, UINT32_MAX - 1000);
  testLossBeforeDnsAndStaleWorkerDrain();
  testNewLifecycleDoesNotInheritEstablishment();
  testAsyncScanGatingFailureAndResults();
  return failures == 0 ? 0 : 1;
}
