#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <string>

#include "TFT_eSPI.h"
#include "../../VictronCYD_Modbus/WifiSetupUi.h"

namespace {

void check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

CamperNetworkStatus offlineStatus() {
  return {true, WanPhase::Offline, IPAddress(), 0, 1};
}

void holdWanToOpen(WifiSetupUi& ui, uint32_t startedAtMs = 1000) {
  check(ui.handleTouch({300, 10}, startedAtMs).type == WifiSetupActionType::None,
        "WAN press must not immediately open setup");
  check(ui.poll(startedAtMs + 2999).type == WifiSetupActionType::None && !ui.isOpen(),
        "WAN hold must remain closed before three seconds");
  check(ui.poll(startedAtMs + 3000).type == WifiSetupActionType::None && ui.isOpen(),
        "WAN hold must open setup at three seconds");
}

void testWanHoldRequiresContinuousContact() {
  TFT_eSPI display;
  WifiSetupUi ui(display);
  check(ui.handleTouch({300, 10}, 100).type == WifiSetupActionType::None,
        "WAN press should arm entry");
  check(ui.poll(3099).type == WifiSetupActionType::None && !ui.isOpen(),
        "entry should remain closed before deadline");
  check(ui.handleRelease(3100).type == WifiSetupActionType::None,
        "release should only cancel entry");
  check(ui.poll(10000).type == WifiSetupActionType::None && !ui.isOpen(),
        "released WAN hold must stay cancelled");
  holdWanToOpen(ui, 11000);
  check(ui.handleTouch({300, 10}, 14001).type == WifiSetupActionType::None,
        "entry contact must not become a Nearby touch before release");
  check(ui.handleRelease(14002).type == WifiSetupActionType::None,
        "entry release should only clear the navigation gate");
  check(ui.handleTouch({300, 10}, 14003).type == WifiSetupActionType::Refresh,
        "setup navigation must work after the entry contact releases");
}

// Mutation caught: visible local setup transitions wait for the old periodic full repaint.
void testLocalSetupTransitionsRequestExactlyOneFullRender() {
  TFT_eSPI display;
  WifiSetupUi ui(display);
  NetworkProfile profiles[1]{{"Known", "hidden", 3, 1}};
  ui.setSavedProfiles(profiles, 1, 0);
  ui.open(); ui.render(offlineStatus());
  check(ui.handleTouch({210, 18}, 10).type == WifiSetupActionType::Refresh &&
            ui.takeFullRenderRequest() && !ui.takeFullRenderRequest(),
        "Nearby transition must request one immediate Scanning render");
  ScanResult results[6]{{"Known", -40, 3, 1}, {"Two", -50, 3, 1}, {"Three", -60, 3, 1},
                        {"Four", -70, 3, 1}, {"Five", -80, 3, 1}, {"Six", -85, 3, 1}};
  ui.setScanResults(results, 6);
  check(ui.handleTouch({278, 218}, 20).type == WifiSetupActionType::None &&
            ui.takeFullRenderRequest(), "Nearby Next must request a page repaint");
  check(ui.handleTouch({55, 218}, 21).type == WifiSetupActionType::None &&
            ui.takeFullRenderRequest(), "Nearby Previous must request a page repaint");
  check(ui.handleTouch({100, 56}, 22).type == WifiSetupActionType::None &&
            ui.takeFullRenderRequest(), "known Nearby routing must request selected Saved repaint");
  check(ui.handleTouch({100, 56}, 23).type == WifiSetupActionType::None &&
            !ui.takeFullRenderRequest(), "unchanged Saved selection must not request repaint");
  check(ui.handleTouch({160, 218}, 24).type == WifiSetupActionType::None &&
            ui.takeFullRenderRequest(), "delete confirmation must request repaint");
  check(ui.handleTouch({30, 18}, 25).type == WifiSetupActionType::None &&
            ui.takeFullRenderRequest() && !ui.takeFullRenderRequest(),
        "confirmation Back must request exactly one immediate Saved repaint");
  ui.handleTouch({160, 218}, 26);
  check(ui.isOpen(), "second delete confirmation must keep setup open while render request remains pending");
  ui.render(offlineStatus());
  check(!ui.takeFullRenderRequest(), "full render must satisfy a pending transition request");
}

// Mutation caught: stale Nearby pagination creates redraws for visually unchanged Saved/Scanning tabs.
void testStaleNearbyPageDoesNotDefeatTransitionCoalescing() {
  TFT_eSPI display; WifiSetupUi ui(display);
  NetworkProfile profiles[1]{{"Six", "hidden", 3, 1}}; ui.setSavedProfiles(profiles, 1, 0); ui.open();
  ScanResult results[6]{{"Known", -40, 3, 1}, {"Two", -50, 3, 1}, {"Three", -60, 3, 1},
                        {"Four", -70, 3, 1}, {"Five", -80, 3, 1}, {"Six", -85, 3, 1}};
  ui.handleTouch({210,18},1); ui.takeFullRenderRequest(); ui.setScanResults(results,6);
  ui.handleTouch({278,218},2); ui.takeFullRenderRequest();
  ui.handleTouch({100,56},3); ui.takeFullRenderRequest();
  check(ui.handleTouch({100,18},4).type == WifiSetupActionType::None && !ui.takeFullRenderRequest(),
        "known route must clear stale page so unchanged Saved contact does not repaint");
  ui.open(); ui.handleTouch({210,18},5); ui.takeFullRenderRequest(); ui.setScanResults(results,6);
  ui.handleTouch({278,218},6); ui.takeFullRenderRequest();
  ui.handleTouch({160,218},7); ui.takeFullRenderRequest();
  check(ui.handleTouch({210,18},8).type == WifiSetupActionType::Refresh && !ui.takeFullRenderRequest(),
        "refresh must clear stale page so unchanged Scanning contact does not repaint");
}

// Mutation caught: unchanged local contacts schedule redundant full setup redraws or omit Saved/Cancel repaint.
void testSetupTransitionRequestsOnlyForActualVisibleChanges() {
  TFT_eSPI display;
  WifiSetupUi ui(display);
  NetworkProfile profiles[1]{{"Saved", "hidden", 3, 1}};
  ui.setSavedProfiles(profiles, 1, 0); ui.open(); ui.render(offlineStatus());
  check(ui.handleTouch({210, 18}, 10).type == WifiSetupActionType::Refresh && ui.takeFullRenderRequest(),
        "Nearby must request its first Scanning repaint");
  check(ui.handleTouch({210, 18}, 11).type == WifiSetupActionType::Refresh && !ui.takeFullRenderRequest(),
        "unchanged Scanning contact must not request another repaint");
  check(ui.handleTouch({100, 18}, 12).type == WifiSetupActionType::None && ui.takeFullRenderRequest(),
        "Scanning to Saved must request an immediate repaint");
  check(ui.handleTouch({100, 56}, 13).type == WifiSetupActionType::None && ui.takeFullRenderRequest(),
        "first Saved-row selection must repaint highlight");
  check(ui.handleTouch({100, 56}, 14).type == WifiSetupActionType::None && !ui.takeFullRenderRequest(),
        "unchanged Saved-row contact must not request another repaint");
  ui.handleTouch({160, 218}, 15); check(ui.takeFullRenderRequest(), "delete confirmation must repaint");
  ui.handleTouch({80, 218}, 16);
  check(ui.takeFullRenderRequest(), "bottom Cancel must request Saved repaint");
}

// Mutation caught: removing remaining-time countdown progression or the immediate
// transition request leaves the dashboard without visible hold feedback or setup redraw.
void testWanHoldCountdownAndImmediateSetupTransition() {
  TFT_eSPI display;
  WifiSetupUi ui(display);
  ui.handleTouch({300, 10}, 1000);
  check(ui.wanHoldCountdown(1000) == 3 && ui.wanHoldCountdown(2000) == 2 &&
            ui.wanHoldCountdown(3000) == 1 && ui.wanHoldCountdown(3999) == 1,
        "WAN hold must show hand-derived 3, 2, 1 countdown values without zero");
  check(ui.poll(4000).type == WifiSetupActionType::None && ui.isOpen() &&
            ui.takeFullRenderRequest(),
        "three-second WAN hold must open and request an immediate full setup render");
  check(!ui.takeFullRenderRequest(), "setup transition render request must be consumed once");
  check(ui.handleTouch({210, 18}, 4001).type == WifiSetupActionType::None,
        "entry contact must remain gated until release");
  ui.handleRelease(4002);
  check(ui.handleTouch({210, 18}, 4003).type == WifiSetupActionType::Refresh,
        "release must unlock setup controls after entry");
}

// Mutation caught: retaining WAN hold state after release or leaving the indicator bounds
// would allow a cancelled contact to open setup later.
void testWanHoldCancellationRestoresIndicatorState() {
  TFT_eSPI display;
  WifiSetupUi ui(display);
  ui.handleTouch({300, 10}, 1000);
  ui.handleRelease(1500);
  check(ui.wanHoldCountdown(1500) == 0 && !ui.isOpen() &&
            ui.poll(5000).type == WifiSetupActionType::None,
        "early release must cancel the hold and prevent delayed setup opening");
  ui.handleTouch({300, 10}, 6000);
  ui.handleTouch({100, 100}, 6500);
  check(ui.wanHoldCountdown(6500) == 0 && !ui.isOpen() &&
            ui.poll(10000).type == WifiSetupActionType::None,
        "moving outside WAN bounds must cancel the hold and restore dashboard state");
}

// Mutation caught: restoring periodic full render increments fillScreen while a stable
// setup surface only needs WAN, portal-expiry, and clear-hold regions refreshed.
void testDynamicSetupRefreshAvoidsFullScreenButUpdatesChangingRegions() {
  TFT_eSPI display;
  WifiSetupUi ui(display);
  ui.open();
  ui.render(offlineStatus());
  const uint32_t savedFullRenders = display.fillScreenCount;
  ui.renderDynamic(offlineStatus());
  ui.renderDynamic(offlineStatus());
  check(display.fillScreenCount == savedFullRenders,
        "stable setup dynamic refresh must not clear the full display");
  CamperNetworkStatus online = offlineStatus();
  online.wanPhase = WanPhase::Online;
  ui.renderDynamic(online);
  check(display.fillScreenCount == savedFullRenders,
        "WAN phase update must use partial drawing without a full clear");

  ui.showPortal("Test", "123456", 10000);
  ui.render(offlineStatus());
  const uint32_t portalFullRenders = display.fillScreenCount;
  ui.poll(1000);
  ui.renderDynamic(offlineStatus());
  check(display.fillScreenCount == portalFullRenders && display.drew("Expires in 9s"),
        "portal expiry countdown must remain refreshable through partial drawing");

  ui.open();
  ui.render(offlineStatus());
  ui.handleTouch({270, 218}, 0);
  ui.poll(1000);
  const uint32_t clearFullRenders = display.fillScreenCount;
  ui.renderDynamic(offlineStatus());
  check(display.fillScreenCount == clearFullRenders && display.drew("Clear in 9s"),
        "clear-saved countdown must remain refreshable without a full clear");
}

// Mutation caught: cancelling Clear Saved without repainting its normal button label.
void testCancelledClearHoldRestoresPartialButton() {
  TFT_eSPI display;
  WifiSetupUi ui(display);
  ui.open(); ui.render(offlineStatus());
  ui.handleTouch({270, 218}, 0); ui.poll(1000); ui.renderDynamic(offlineStatus());
  check(display.drew("Clear in 9s"), "armed clear hold must show its countdown");
  const uint32_t fullRenders = display.fillScreenCount;
  const size_t cancellationCheckpoint = display.drawnStrings.size();
  ui.handleRelease(1100); ui.renderDynamic(offlineStatus());
  check(display.drewSince("Clear Saved", cancellationCheckpoint) &&
            display.fillScreenCount == fullRenders,
        "cancelled clear hold must restore its label without a full clear");
}

void testSavedSelectionConnectAndDeleteConfirmation() {
  TFT_eSPI display;
  WifiSetupUi ui(display);
  NetworkProfile profiles[2]{{"Camp", "do-not-render-one", 3, 1234},
                             {"Backup", "do-not-render-two", 3, 0}};
  ui.setSavedProfiles(profiles, 2, 0);
  ui.open();
  ui.render(offlineStatus());
  check(display.drewContaining("Camp") && display.drewContaining("ACTIVE") &&
            display.drewContaining("1234"),
        "saved view must show SSID, active marker, and last-success metadata");
  check(display.drew("Clear Saved"), "destructive hold control must be clearly named");
  check(!display.drewContaining("do-not-render"), "saved view must not render passphrases");

  check(ui.handleTouch({100, 56}, 10).type == WifiSetupActionType::None,
        "selecting a saved row must not connect or delete");
  WifiSetupAction action = ui.handleTouch({55, 218}, 20);
  check(action.type == WifiSetupActionType::ConnectSaved && action.profileIndex == 0,
        "Connect must explicitly return the selected saved profile");

  check(ui.handleTouch({100, 82}, 30).type == WifiSetupActionType::None,
        "second saved row should select without action");
  check(ui.handleTouch({160, 218}, 40).type == WifiSetupActionType::None,
        "Delete must open confirmation without deleting");
  ui.render(offlineStatus());
  check(display.drewContaining("Delete Backup"), "confirmation must identify selected SSID");
  action = ui.handleTouch({235, 218}, 50);
  check(action.type == WifiSetupActionType::DeleteSaved && action.profileIndex == 1,
        "confirmation must emit DeleteSaved for selected profile");
}

void testNearbyRoutingPaginationAndRefresh() {
  TFT_eSPI display;
  WifiSetupUi ui(display);
  NetworkProfile profiles[1]{{"Known", "hidden", 3, 1}};
  ui.setSavedProfiles(profiles, 1, 0);
  ui.open();

  WifiSetupAction action = ui.handleTouch({210, 18}, 10);
  check(action.type == WifiSetupActionType::Refresh,
        "opening Nearby must request an asynchronous refresh");

  ScanResult results[7]{{"Known", -42, 3, 1}, {"Secure", -55, 3, 1},
                        {"Open", -66, 0, 6}, {"Third", -70, 3, 6},
                        {"ABCDEFGHIJKLMNOPQRSTUVWXYZ123456", -75, 3, 11},
                        {"PageTwo", -80, 3, 11},
                        {"Last", -85, 0, 11}};
  ui.setScanResults(results, 7);
  ui.render(offlineStatus());
  check(display.drewContaining("-55 dBm") && display.drew("OPEN") &&
            !display.drew("LOCK") && display.drewCircleAt(290, 78, 5) &&
            display.filledRectAt(284, 78, 12, 10),
        "nearby rows must show RSSI, OPEN text, and a secured lock glyph");
  check(display.drew("ABCDEFGHIJKLMNOPQRSTU...") &&
            !display.drew("ABCDEFGHIJKLMNOPQRSTUVWXYZ123456"),
        "32-character SSIDs must be ellipsized before the RSSI column");

  check(ui.handleTouch({100, 56}, 20).type == WifiSetupActionType::None,
        "known nearby SSID must route to its saved profile without auto-connect");
  action = ui.handleTouch({55, 218}, 30);
  check(action.type == WifiSetupActionType::ConnectSaved && action.profileIndex == 0,
        "known nearby route must select the matching saved profile");

  check(ui.handleTouch({210, 18}, 40).type == WifiSetupActionType::Refresh,
        "Nearby tab must remain refreshable");
  ui.setScanResults(results, 7);
  action = ui.handleTouch({100, 82}, 50);
  check(action.type == WifiSetupActionType::ProvisionNew && action.ssid == String("Secure") &&
            action.securityType == 3,
        "unknown secured SSID must provision with its security type");

  ui.open();
  check(ui.handleTouch({210, 18}, 60).type == WifiSetupActionType::Refresh,
        "Nearby should request refresh after reopening");
  ui.setScanResults(results, 7);
  action = ui.handleTouch({100, 108}, 70);
  check(action.type == WifiSetupActionType::ProvisionNew && action.ssid == String("Open") &&
            action.securityType == 0,
        "unknown open SSID must use ProvisionNew with open security type");

  ui.open();
  check(ui.handleTouch({210, 18}, 80).type == WifiSetupActionType::Refresh,
        "Nearby should refresh for pagination test");
  ui.setScanResults(results, 7);
  check(ui.handleTouch({278, 218}, 90).type == WifiSetupActionType::None,
        "Next must paginate without selecting");
  action = ui.handleTouch({100, 56}, 100);
  check(action.type == WifiSetupActionType::ProvisionNew && action.ssid == String("PageTwo"),
        "page-two row must route the sixth scan result");

  ui.open();
  check(ui.handleTouch({210, 18}, 110).type == WifiSetupActionType::Refresh,
        "Nearby should enter scanning for explicit refresh test");
  ui.setScanResults(results, 7);
  action = ui.handleTouch({160, 218}, 120);
  check(action.type == WifiSetupActionType::Refresh,
        "Refresh control must emit Refresh explicitly");
}

void testLateScanResultsDoNotOwnNavigation() {
  TFT_eSPI display;
  WifiSetupUi ui(display);
  ScanResult result[1]{{"Late", -50, 3, 1}};

  ui.open();
  check(ui.handleTouch({210, 18}, 10).type == WifiSetupActionType::Refresh,
        "Nearby should enter Scanning before Back test");
  check(ui.handleTouch({30, 18}, 20).type == WifiSetupActionType::Exit && !ui.isOpen(),
        "Back must close while a scan is outstanding");
  ui.setScanResults(result, 1);
  check(!ui.isOpen(), "late scan results must not reopen a Back-closed UI");

  ui.open();
  check(ui.handleTouch({210, 18}, 100).type == WifiSetupActionType::Refresh,
        "Nearby should enter Scanning before inactivity test");
  check(ui.poll(60100).type == WifiSetupActionType::Exit && !ui.isOpen(),
        "Scanning must still honor inactivity exit");
  ui.setScanResults(result, 1);
  check(!ui.isOpen(), "late scan results must not reopen an inactivity-closed UI");

  NetworkProfile saved[1]{{"Saved", "not-rendered", 3, 7}};
  ui.setSavedProfiles(saved, 1, 0);
  ui.open();
  check(ui.handleTouch({210, 18}, 60200).type == WifiSetupActionType::Refresh,
        "Nearby should enter Scanning before navigation-away test");
  check(ui.handleTouch({100, 18}, 60201).type == WifiSetupActionType::None,
        "Saved tab should navigate away from Scanning");
  ui.setScanResults(result, 1);
  check(ui.handleTouch({100, 56}, 60202).type == WifiSetupActionType::None,
        "late results must preserve the open Saved view");
  const WifiSetupAction action = ui.handleTouch({55, 218}, 60203);
  check(action.type == WifiSetupActionType::ConnectSaved && action.profileIndex == 0,
        "Saved controls must remain active after late scan results");
}

void testLateScanFailureDoesNotOwnNavigation() {
  TFT_eSPI display;
  WifiSetupUi ui(display);

  ui.open();
  check(ui.handleTouch({210, 18}, 10).type == WifiSetupActionType::Refresh,
        "Nearby should enter Scanning before late-failure Back test");
  check(ui.handleTouch({30, 18}, 20).type == WifiSetupActionType::Exit && !ui.isOpen(),
        "Back must close while the failing scan is outstanding");
  check(!ui.showScanFailure("Scan failed; retry") && !ui.isOpen(),
        "late scan failure must not reopen a Back-closed UI");

  NetworkProfile saved[1]{{"Saved", "not-rendered", 3, 7}};
  ui.setSavedProfiles(saved, 1, 0);
  ui.open();
  check(ui.handleTouch({210, 18}, 100).type == WifiSetupActionType::Refresh,
        "Nearby should enter Scanning before late-failure navigation test");
  check(ui.handleTouch({100, 18}, 101).type == WifiSetupActionType::None,
        "Saved tab should take ownership from the failing scan");
  check(!ui.showScanFailure("Scan failed; retry"),
        "late scan failure must not replace the newer Saved view");
  check(ui.handleTouch({100, 56}, 102).type == WifiSetupActionType::None &&
            ui.handleTouch({55, 218}, 103).type == WifiSetupActionType::ConnectSaved,
        "Saved controls must remain active after a late scan failure");

  ui.open();
  check(ui.handleTouch({210, 18}, 200).type == WifiSetupActionType::Refresh,
        "Nearby should enter Scanning before owned-failure test");
  check(ui.showScanFailure("Scan failed; retry"),
        "scan failure should be presented while UI still owns Scanning");
  ui.render(offlineStatus());
  check(display.drewContaining("Scan failed; retry") && display.drewContaining("Failed"),
        "owned scan failure should render a retryable result");
}

void testNearbyPaginationRetainsMoreThanTwentyResults() {
  TFT_eSPI display;
  WifiSetupUi ui(display);
  ScanResult results[26];
  for (size_t index = 0; index < 26; ++index) {
    char ssid[8];
    std::snprintf(ssid, sizeof(ssid), "Net%02u", static_cast<unsigned>(index));
    results[index] = {String(ssid), static_cast<int32_t>(-40 - static_cast<int>(index)), 3, 1};
  }

  ui.open();
  check(ui.handleTouch({210, 18}, 10).type == WifiSetupActionType::Refresh,
        "Nearby should enter Scanning before large result set");
  ui.setScanResults(results, 26);
  for (uint32_t page = 0; page < 5; ++page) {
    check(ui.handleTouch({278, 218}, 20 + page).type == WifiSetupActionType::None,
          "Next should advance through every supplied result page");
  }
  const WifiSetupAction action = ui.handleTouch({100, 56}, 30);
  check(action.type == WifiSetupActionType::ProvisionNew && action.ssid == String("Net25"),
        "sixth page must retain and select the twenty-sixth scan result");
}

void testReplacementScanClampsInvalidPage() {
  TFT_eSPI display;
  WifiSetupUi ui(display);
  ScanResult initial[26];
  for (size_t index = 0; index < 26; ++index) {
    char ssid[12];
    std::snprintf(ssid, sizeof(ssid), "Initial%02u", static_cast<unsigned>(index));
    initial[index] = {String(ssid), -50, 3, 1};
  }

  ui.open();
  check(ui.handleTouch({210, 18}, 10).type == WifiSetupActionType::Refresh,
        "Nearby should scan before replacement-page test");
  ui.setScanResults(initial, 26);
  for (uint32_t page = 0; page < 5; ++page) {
    ui.handleTouch({278, 218}, 20 + page);
  }
  check(ui.handleTouch({160, 218}, 30).type == WifiSetupActionType::Refresh,
        "page-six refresh should request a replacement scan");

  ScanResult replacement[7];
  for (size_t index = 0; index < 7; ++index) {
    char ssid[12];
    std::snprintf(ssid, sizeof(ssid), "Smaller%02u", static_cast<unsigned>(index));
    replacement[index] = {String(ssid), -60, 3, 1};
  }
  ui.setScanResults(replacement, 7);
  const WifiSetupAction action = ui.handleTouch({100, 56}, 31);
  check(action.type == WifiSetupActionType::ProvisionNew &&
            action.ssid == String("Smaller05"),
        "replacement scan must clamp an invalid page to the new last page");
}

void testClearAllHoldCountdownAndReleaseCancellation() {
  TFT_eSPI display;
  WifiSetupUi ui(display);
  ui.open();
  check(ui.handleTouch({270, 218}, 1000).type == WifiSetupActionType::None,
        "Clear Saved press must only arm the hold");
  ui.poll(6000);
  ui.render(offlineStatus());
  check(display.drewContaining("Clear in 5s"), "clear hold must render a visible countdown");
  check(ui.poll(10999).type == WifiSetupActionType::None,
        "clear hold must not complete before ten seconds");
  ui.handleRelease(11000);
  check(ui.poll(20000).type == WifiSetupActionType::None,
        "release must cancel a clear-all hold");

  check(ui.handleTouch({270, 218}, 21000).type == WifiSetupActionType::None,
        "second Clear Saved press must re-arm the hold");
  WifiSetupAction action = ui.poll(31000);
  check(action.type == WifiSetupActionType::ClearAll,
        "continuous ten-second hold must emit ClearAll");
  check(ui.poll(31001).type == WifiSetupActionType::None,
        "completed clear hold must emit exactly once");
  check(ui.handleTouch({270, 218}, 31002).type == WifiSetupActionType::None &&
            ui.poll(41002).type == WifiSetupActionType::None,
        "completed clear hold must not re-arm before release");
}

void testBackInactivityPortalAndResultViews() {
  TFT_eSPI display;
  WifiSetupUi ui(display);
  ui.open();
  check(ui.poll(59999).type == WifiSetupActionType::None && ui.isOpen(),
        "setup must remain open before sixty seconds of inactivity");
  WifiSetupAction action = ui.poll(60000);
  check(action.type == WifiSetupActionType::Exit && !ui.isOpen(),
        "setup must exit at sixty seconds of inactivity");

  ui.showPortal("Secure", "123456", 600000);
  ui.render(offlineStatus());
  check(display.drewContaining("Secure") && display.drewContaining("192.168.50.1/setup") &&
            display.drewContaining("123456"),
        "portal view must show SSID, local URL, and pairing code");
  check(ui.poll(300000).type == WifiSetupActionType::None && ui.isOpen(),
        "portal must be exempt from setup inactivity exit");
  action = ui.handleTouch({30, 18}, 300001);
  check(action.type == WifiSetupActionType::Exit && !ui.isOpen(),
        "persistent Back must exit the portal view");

  ui.showResult("Connected", true);
  ui.render(offlineStatus());
  check(display.drewContaining("Connected") && display.drewContaining("Success"),
        "result view must render outcome and message");
  action = ui.handleTouch({30, 18}, 1);
  check(action.type == WifiSetupActionType::Exit && !ui.isOpen(),
        "persistent Back must exit the result view");

  ui.showPortal("Expired SSID", "654321", 100);
  check(ui.poll(99).type == WifiSetupActionType::None && ui.isOpen(),
        "portal UI must remain active before its lifecycle deadline");
  action = ui.poll(100);
  check(action.type == WifiSetupActionType::Exit && !ui.isOpen(),
        "portal UI must close and clear retained portal material at expiry");
  ui.open();
  ui.render(offlineStatus());
  check(!display.drewContaining("Expired SSID") && !display.drewContaining("654321"),
        "stale portal SSID and code must not render after navigation re-entry");
}

}  // namespace

int main() {
  testWanHoldRequiresContinuousContact();
  testLocalSetupTransitionsRequestExactlyOneFullRender();
  testStaleNearbyPageDoesNotDefeatTransitionCoalescing();
  testSetupTransitionRequestsOnlyForActualVisibleChanges();
  testWanHoldCountdownAndImmediateSetupTransition();
  testWanHoldCancellationRestoresIndicatorState();
  testDynamicSetupRefreshAvoidsFullScreenButUpdatesChangingRegions();
  testCancelledClearHoldRestoresPartialButton();
  testSavedSelectionConnectAndDeleteConfirmation();
  testNearbyRoutingPaginationAndRefresh();
  testLateScanResultsDoNotOwnNavigation();
  testLateScanFailureDoesNotOwnNavigation();
  testNearbyPaginationRetainsMoreThanTwentyResults();
  testReplacementScanClampsInvalidPage();
  testClearAllHoldCountdownAndReleaseCancellation();
  testBackInactivityPortalAndResultViews();
  return 0;
}
