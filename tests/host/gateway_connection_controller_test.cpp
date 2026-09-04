#include <cstdlib>
#include <iostream>
#include <string>
#include "Network.h"
#include "Preferences.h"
#include "WiFi.h"
#include "TFT_eSPI.h"
#include "freertos/task.h"
#include "GatewayConnectionController.h"
#include "CamperNetwork.h"
#include "WifiSetupUi.h"

namespace {
void check(bool ok, const char* message) {
  if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
void addressed(const char* ssid) {
  WiFi.connected = true;
  WiFi.stationSsid = ssid;
  WiFi.stationAddress = IPAddress(10, 0, 0, 4);
}
struct Fixture {
  NetworkProfileStore store;
  CamperNetwork network;
  GatewayConnectionController<CamperNetwork> controller{network, store};
  Fixture(bool active = true) {
    Preferences::reset(); WiFi.reset(); Network.reset(); FakeRtos::reset(); Serial.clear();
    check(store.begin(), "initialize store");
    Preferences::putRawUChar("touchcal", "sentinel", 23);
    size_t index;
    check(store.upsert({"A", "private-a", 3, 10}, index), "seed A");
    check(store.upsert({"B", "private-b", 3, 20}, index), "seed B");
    check(network.begin("Camper", "private-ap-password", 0), "start AP");
    if (active) {
      check(store.activate(0), "activate A");
      NetworkProfile a; check(store.load(0, a), "load A");
      check(network.connect(a, 0), "boot A"); addressed("A"); network.acceptPendingProfile();
    }
  }
  ~Fixture() {
    Preferences calibration; calibration.begin("touchcal", true);
    check(calibration.getUChar("sentinel", 0) == 23, "profile operations preserve calibration namespace");
    check(network.status().apReady && WiFi.AP.createCalls == 1 && WiFi.modeCalls == 1,
          "all attempt paths preserve private AP");
    for (bool off : WiFi.disconnectWifiOff) check(!off, "rollback never disables AP radio");
    check(Serial.output().find("private-") == std::string::npos, "credentials never logged");
  }
  void restored(int active = 0, const char* ssid = "A") {
    check(store.activeIndex() == active && WiFi.beginSsids.back() == ssid,
          "failure preserves selection and reconnects retained profile");
    check(!controller.pendingActive(), "terminal path clears attempt");
  }
};

// Catches immediate acceptance, stale SSID acceptance, and upsert instead of activate.
void testSuccessfulSwitchAndSubsequentRollback() {
  Fixture f;
  check(f.controller.beginSaved(1, 100).outcome == GatewayConnectionOutcome::Started,
        "saved B starts pending");
  check(f.controller.poll(101, 1000).outcome == GatewayConnectionOutcome::None &&
        f.store.activeIndex() == 0, "stale A DHCP cannot activate B");
  WiFi.stationSsid = "B"; WiFi.stationAddress = IPAddress();
  check(f.controller.poll(102, 1000).outcome == GatewayConnectionOutcome::None,
        "B association alone cannot activate");
  addressed("B");
  check(f.controller.poll(103, 1000).outcome == GatewayConnectionOutcome::Connected,
        "B association plus DHCP activates without DNS");
  NetworkProfile b;
  check(f.store.load(1, b) && b.lastSuccessEpoch == 20 && b.passphrase == String("private-b") &&
        b.securityType == 3 && f.store.count() == 2, "activation preserves profile data");
  NetworkProfileStore rebooted;
  check(rebooted.begin() && rebooted.activeIndex() == 1, "B survives store reconstruction");
  TFT_eSPI display; WifiSetupUi ui(display);
  NetworkProfile profiles[2]; f.store.load(0, profiles[0]); f.store.load(1, profiles[1]);
  ui.setSavedProfiles(profiles, 2, f.store.activeIndex()); ui.open(); ui.render(f.network.status());
  check(display.drew("B [ACTIVE]") && !display.drew("A [ACTIVE]"), "refreshed UI marks B active");
  check(!display.drewContaining("private-"), "saved UI does not expose passwords");
  CredentialSubmission submission; check(submission.set("C", "private-c", 3), "set submitted C");
  check(f.controller.beginSubmitted(submission, PendingProfileSource::OnDevice, 200).outcome ==
        GatewayConnectionOutcome::Started && !submission.ready && submission.passphrase[0] == 0,
        "controller consumes and clears submission");
  check(f.controller.poll(60200, 1000).outcome == GatewayConnectionOutcome::TimedOut,
        "later new credentials time out");
  f.restored(1, "B");
  check(f.store.count() == 2, "failed submission not saved");
}

void testProgressAndCancellationThroughUi() {
  Fixture f;
  TFT_eSPI display; WifiSetupUi ui(display);
  NetworkProfile profiles[2]; f.store.load(0, profiles[0]); f.store.load(1, profiles[1]);
  ui.setSavedProfiles(profiles, 2, f.store.activeIndex()); ui.open();
  ui.handleTouch({100, 82}, 1); ui.render(f.network.status());
  coordinateSetupInteraction(
      [&] { return ui.handleTouch({55, 218}, 2); },
      [&] { return ui.takeFullRenderRequest(); },
      [&] { ui.render(f.network.status()); },
      [&](WifiSetupAction action) {
        check(display.drew("Connecting...") && display.drew("B") && WiFi.beginSsids.size() == 1,
              "real saved attempt starts only after its progress frame");
        check(f.controller.beginSaved(action.profileIndex, 2).outcome == GatewayConnectionOutcome::Started,
              "progress action starts controller");
      }, [] {});
  ui.handleRelease(3);
  check(ui.handleTouch({30, 18}, 4).type == WifiSetupActionType::CancelSavedConnection,
        "progress Back requests cancellation");
  f.controller.cancel(4); f.restored();
  ui.setSavedProfiles(profiles, 2, f.store.activeIndex()); ui.cancelSavedConnection();
  check(ui.handleTouchMove({30, 18}, 5).type == WifiSetupActionType::None && ui.isOpen(),
        "cancel contact cannot also exit Saved while still held");
  ui.render(f.network.status());
  check(display.drew("A [ACTIVE]"), "cancelled controller refresh leaves marker on A");
}

void testTimeoutCancellationAndWriteFailure() {
  for (uint32_t start : {100u, UINT32_MAX - 1000u}) {
    Fixture f;
    f.controller.beginSaved(1, start);
    check(f.controller.poll(start + 59999u, 0).outcome == GatewayConnectionOutcome::None,
          "timeout stays pending at 59999 ms including wraparound");
    check(f.controller.poll(start + 60000u, 0).outcome == GatewayConnectionOutcome::TimedOut,
          "timeout fires at 60000 ms including wraparound");
    f.restored();
  }
  {
    Fixture f; f.controller.beginSaved(1, 0);
    check(f.controller.cancel(20).outcome == GatewayConnectionOutcome::Cancelled, "cancel typed outcome");
    f.restored(); addressed("B");
    check(f.controller.poll(21, 10).outcome == GatewayConnectionOutcome::None && f.store.activeIndex() == 0,
          "cancelled late success cannot commit");
  }
  // Fail both before journalling and while writing the canonical snapshot.
  for (size_t mutation : {1u, 16u, 21u}) {
    Fixture f; f.controller.beginSaved(1, 0); addressed("B");
    Preferences::failOnMutation(mutation);
    check(f.controller.poll(1, 900).outcome == GatewayConnectionOutcome::PersistenceFailed,
          "activation write failure reports failure");
    Preferences::clearFaults(); f.restored();
    NetworkProfileStore rebooted; check(rebooted.begin() && rebooted.activeIndex() == 0,
                                       "failed activation retains A on reboot");
  }
}

void testInvalidReadsAndImmediateRejection() {
  Fixture f;
  const size_t starts = WiFi.beginSsids.size(); const int disconnects = WiFi.disconnectCalls;
  for (int index : {-1, 2, 5}) {
    check(f.controller.beginSaved(index, 1).outcome != GatewayConnectionOutcome::Started,
          "invalid index rejected");
  }
  Preferences::failOnReadOpen(2);
  check(f.controller.beginSaved(1, 1).outcome != GatewayConnectionOutcome::Started,
        "candidate read failure rejects before connectivity changes");
  Preferences::clearFaults();
  Preferences::putRawUChar("wanprofiles", "count", 6);
  check(f.controller.beginSaved(1, 2).outcome == GatewayConnectionOutcome::StorageReadFailed,
        "failed previous-profile read rejects safely");
  check(WiFi.beginSsids.size() == starts && WiFi.disconnectCalls == disconnects && WiFi.connected,
        "invalid selections and reads never disrupt current connection");
  Preferences::putRawUChar("wanprofiles", "count", 2);
  // Legacy store accepts oversized SSIDs; real network rejects them immediately.
  size_t index;
  check(f.store.upsert({"12345678901234567890123456789012345", "private-long", 3, 0}, index),
        "seed legacy unconnectable record");
  check(f.controller.beginSaved(static_cast<int>(index), 3).outcome == GatewayConnectionOutcome::Rejected,
        "immediate network rejection rolls back");
  f.restored();
}

void testEmptyPreviousSameProfileAndReplacement() {
  for (bool cancel : {false, true}) {
    Fixture f(false); f.controller.beginSaved(1, 0);
    if (cancel) f.controller.cancel(1); else f.controller.poll(60000, 0);
    check(!WiFi.connected && f.store.activeIndex() == -1 && !f.controller.pendingActive(),
          "no previous profile leaves upstream disconnected");
    check(WiFi.disconnectEraseAp.back(), "no previous profile clears transient station config");
  }
  {
    Fixture f(false); f.controller.beginSaved(1, 0); addressed("B");
    Preferences::failOnMutation(1);
    check(f.controller.poll(1, 10).outcome == GatewayConnectionOutcome::PersistenceFailed &&
          !WiFi.connected && f.store.activeIndex() == -1,
          "activation failure without previous profile disconnects upstream");
  }
  {
    Fixture f; f.controller.beginSaved(1, 100); addressed("B");
    check(f.controller.poll(60100, 10).outcome == GatewayConnectionOutcome::Connected,
          "association plus DHCP at timeout boundary wins as in existing submitted policy");
  }
  {
    Fixture f; f.controller.beginSaved(0, 1); addressed("A");
    check(f.controller.poll(2, 100).outcome == GatewayConnectionOutcome::Connected && f.store.count() == 2,
          "already-active selection creates no duplicate");
  }
  for (auto source : {PendingProfileSource::DirectOpen, PendingProfileSource::Portal,
                      PendingProfileSource::OnDevice}) {
    Fixture f; f.controller.beginSaved(1, 1);
    CredentialSubmission submission; submission.set("C", source == PendingProfileSource::DirectOpen ? "" : "private-c",
                                                   source == PendingProfileSource::DirectOpen ? 0 : 3);
    check(f.controller.beginSubmitted(submission, source, 2).outcome == GatewayConnectionOutcome::Started,
          "submitted attempt replaces saved attempt");
    addressed("B"); check(f.controller.poll(3, 100).outcome == GatewayConnectionOutcome::None,
                          "replaced candidate cannot commit");
    addressed("C"); check(f.controller.poll(4, 100).outcome == GatewayConnectionOutcome::Connected,
                          "all submitted sources persist on success");
    NetworkProfile saved; check(f.store.load(f.store.activeIndex(), saved) && saved.ssid == String("C") &&
                               saved.lastSuccessEpoch == 100, "submitted metadata preserved");
  }
}

void testPortalReadFailureAndLifecycleReplacement() {
  for (bool storeReady : {false, true}) {
    Fixture f;
    f.controller.replace(GatewayLifecycleTarget::PhysicalPortal);
    CredentialSubmission submission; submission.set("C", "private-c", 3);
    if (storeReady) Preferences::putRawUChar("wanprofiles", "count", 6);
    const auto result = f.controller.beginSubmitted(submission, PendingProfileSource::Portal, 1, storeReady);
    check(result.outcome == GatewayConnectionOutcome::StorageReadFailed &&
          !f.controller.physicalPortalActive() && result.cancelPortal,
          "consumed portal submission cannot turn a storage error into portal expiry");
    check(WiFi.connected && WiFi.beginSsids.size() == 1 && WiFi.disconnectCalls == 0,
          "portal storage read failure leaves existing upstream intact");
  }
  for (auto target : {GatewayLifecycleTarget::PhysicalPortal, GatewayLifecycleTarget::ClearAll,
                      GatewayLifecycleTarget::Idle}) {
    Fixture f; f.controller.beginSaved(1, 0);
    f.controller.replace(target, 1);
    check(!f.controller.pendingActive() && WiFi.beginSsids.size() == 2 && !WiFi.connected,
          "non-attempt replacement cancels without briefly reconnecting the old profile");
    check(WiFi.disconnectEraseAp.back(), "non-attempt replacement erases transient station config");
    addressed("B"); check(f.controller.poll(2, 100).outcome == GatewayConnectionOutcome::None &&
                          f.store.activeIndex() == 0, "replaced attempt cannot commit later");
  }
  {
    Fixture f; f.controller.beginSaved(1, 0);
    f.controller.replace(GatewayLifecycleTarget::Exit, 1);
    check(f.controller.pendingActive(), "leaving setup preserves background attempt policy");
  }
}
}
int main() {
  testPortalReadFailureAndLifecycleReplacement();
  testProgressAndCancellationThroughUi();
  testSuccessfulSwitchAndSubsequentRollback();
  testTimeoutCancellationAndWriteFailure();
  testInvalidReadsAndImmediateRejection();
  testEmptyPreviousSameProfileAndReplacement();
  std::cout << "gateway_connection_controller_test: passed\n";
}
