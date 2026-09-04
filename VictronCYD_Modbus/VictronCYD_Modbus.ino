/*
 * ESP32-Victron-VRM-Display — local Modbus TCP dashboard and camper gateway.
 *
 * The ESP32 keeps its AP/IPv4 repeater service alive while independently managing
 * an upstream profile, the touch WiFi selector, and GX Modbus polling.
 */
#include <WiFi.h>
#include "ProvisioningPortal.h"
#include <TFT_eSPI.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <cstdio>
#include <cstring>
#include <time.h>

#include "CamperNetworkRuntime.h"
#include "GatewayApplicationPolicy.h"
#include "GatewayConnectionController.h"
#include "ModbusCycleSourceRuntime.h"
#include "ModbusSnapshotPolicy.h"
#include "NetworkProfiles.h"
#include "TouchInput.h"
#include "WifiSetupUi.h"
#include "SettingsUi.h"
#include "TimeSettings.h"
#include "VenusAddressTracker.h"
#include "esp_task_wdt.h"
#include "RuntimeConfig.h"
#ifdef CYD_SIMULATION
#include <Network.h>
#include "SimulationClock.h"
#include "SimulationControl.h"
#endif

#define WDT_TIMEOUT_S 30

namespace {

constexpr uint32_t kModbusPollMs = 2000;
constexpr uint32_t kGxSnapshotMaximumAgeMs = 10000;
constexpr uint32_t kModbusTaskStack = 4096;
constexpr UBaseType_t kModbusTaskPriority = 1;
constexpr size_t kMaximumScanResults = 64;

constexpr int kScreenWidth = 320;
constexpr int kScreenHeight = 240;
constexpr uint16_t kBackground = 0x0000;
constexpr uint16_t kBox = 0x10A2;
constexpr uint16_t kBorder = 0x3D7F;
constexpr uint16_t kTitle = 0xACD3;
constexpr uint16_t kValue = 0xFFFF;
constexpr uint16_t kUnit = 0x8C71;
constexpr uint16_t kBlue = 0x2D9F;
constexpr uint16_t kLine = 0x4C9F;
constexpr uint16_t kGreen = 0x2FE6;
constexpr uint16_t kAmber = 0xFD20;
constexpr uint16_t kRed = 0xF800;

struct Box {
  int x;
  int y;
  int width;
  int height;
};

TFT_eSPI tft;
TouchInput touchInput(tft);
WifiSetupUi wifiSetupUi(tft);
SettingsUi settingsUi(tft);
TimeSettingsStore timeSettingsStore;
TimeSettings activeTimeSettings;
WifiSetupOrigin wifiSetupOrigin = WifiSetupOrigin::Dashboard;
TouchSurfaceGate touchSurfaceGate;
CamperNetworkRuntime camperNetwork;
NetworkProfileStore profileStore;
bool isAssociatedPortalClient(const IPAddress& address, void*) {
  const auto snapshot = camperNetwork.bridgeSnapshot(millis());
  const uint32_t ip = (uint32_t(address[0]) << 24) | (uint32_t(address[1]) << 16) |
                      (uint32_t(address[2]) << 8) | address[3];
  for (size_t i = 0; i < snapshot.count; ++i) {
    if (snapshot.ready && ip && snapshot.clients[i].address == ip) return true;
  }
  return false;
}
ProvisioningPortal portal(isAssociatedPortalClient);
VenusAddressTracker venusTracker;

ModbusCycleSourceRuntime modbusCycleSource;
struct ModbusWorkResult {
  VenusProbe probe;
  ModbusSnapshot snapshot;
};
#ifdef CYD_SIMULATION
SimulationClock simulationClock;
SimulationControl simulationControl(camperNetwork, modbusCycleSource, simulationClock);
#endif
QueueHandle_t modbusRequestQueue = nullptr;
QueueHandle_t modbusResultQueue = nullptr;
bool modbusWorkerReady = false;
bool modbusRequestInFlight = false;
bool touchInputReady = false;

ModbusSnapshot dashboardSnapshot = makeDefaultModbusSnapshot();
bool hasValidGxSnapshot = false;
bool gxOnline = false;
bool profileStoreReady = false;
bool blink = false;
uint32_t lastModbusRequestMs = 0;
uint32_t lastModbusTargetToken = 0;
uint32_t lastClockMs = 0;
uint32_t lastVenusRefreshMs = 0;

GatewayConnectionController<CamperNetworkRuntime> connectionController(camperNetwork, profileStore);

const Box bGrid{4, 24, 100, 66};
const Box bInv{110, 24, 100, 66};
const Box bAC{216, 24, 100, 66};
const Box bBatt{4, 96, 206, 140};
const Box bDC{216, 96, 100, 66};
const Box bPV{216, 170, 100, 66};

void secureClearString(String& value) {
  if (!value.isEmpty()) {
    volatile char* cursor = const_cast<char*>(value.c_str());
    for (size_t remaining = value.length(); remaining > 0; --remaining) {
      *cursor++ = 0;
    }
  }
  value = String();
}

void clearProfile(NetworkProfile& profile) {
  secureClearString(profile.passphrase);
  profile.ssid = String();
  profile.securityType = 0;
  profile.lastSuccessEpoch = 0;
}

ClockText clockText() {
#ifdef CYD_SIMULATION
  return formatClock(simulationClock.epoch(), activeTimeSettings.format);
#else
  return formatClock(time(nullptr), activeTimeSettings.format);
#endif
}

void modbusWorker(void*) {
  VenusProbe request{};
  uint32_t activeToken = 0;
  ModbusSnapshot retainedSnapshot = makeDefaultModbusSnapshot();
  for (;;) {
    if (xQueueReceive(modbusRequestQueue, &request, portMAX_DELAY) != pdPASS) {
      continue;
    }
    if (request.token != activeToken) {
      modbusCycleSource.setAddress(0);
      retainedSnapshot = makeDefaultModbusSnapshot();
      activeToken = request.token;
    }
    modbusCycleSource.setAddress(request.address);
    ModbusReadCycle cycle{};
    if (request.address) modbusCycleSource.fetch(cycle);
    retainedSnapshot = mergeModbusSnapshot(retainedSnapshot, cycle);
    Serial.printf("[MB] result=%s\n", retainedSnapshot.valid ? "valid" : "invalid");
    const ModbusWorkResult result{request, retainedSnapshot};
    xQueueOverwrite(modbusResultQueue, &result);
  }
}

bool startModbusWorker() {
  modbusRequestQueue = xQueueCreate(1, sizeof(VenusProbe));
  modbusResultQueue = xQueueCreate(1, sizeof(ModbusWorkResult));
  if (modbusRequestQueue == nullptr || modbusResultQueue == nullptr) {
    if (modbusRequestQueue != nullptr) {
      vQueueDelete(modbusRequestQueue);
      modbusRequestQueue = nullptr;
    }
    if (modbusResultQueue != nullptr) {
      vQueueDelete(modbusResultQueue);
      modbusResultQueue = nullptr;
    }
    return false;
  }
  if (xTaskCreatePinnedToCore(modbusWorker, "gx-modbus", kModbusTaskStack, nullptr,
                              kModbusTaskPriority, nullptr, 0) != pdPASS) {
    vQueueDelete(modbusRequestQueue);
    vQueueDelete(modbusResultQueue);
    modbusRequestQueue = nullptr;
    modbusResultQueue = nullptr;
    return false;
  }
  return true;
}

void drawBoxFrame(const Box& box, const char* title) {
  tft.fillRoundRect(box.x, box.y, box.width, box.height, 5, kBox);
  tft.drawRoundRect(box.x, box.y, box.width, box.height, 5, kBorder);
  tft.setTextColor(kTitle, kBox);
  tft.setTextDatum(TL_DATUM);
  tft.drawString(title, box.x + 6, box.y + 5, 2);
}

void drawConnector(int x1, int y1, int x2, int y2) {
  tft.drawLine(x1, y1, x2, y2, kLine);
  tft.fillCircle((x1 + x2) / 2, (y1 + y2) / 2, 2, kLine);
}

void drawDashboardFrame() {
  tft.fillScreen(kBackground);
  tft.setTextColor(kTitle, kBackground);
  tft.setTextDatum(ML_DATUM);
  String siteName(SECRET_SITE_NAME);
  if (tft.textWidth(siteName, 2) > 76) {
    while (siteName.length() && tft.textWidth(siteName + "...", 2) > 76) {
      siteName.remove(siteName.length() - 1);
    }
    siteName += "...";
  }
  tft.drawString(siteName, 28, 11, 2);
  drawBoxFrame(bGrid, "Grid");
  drawBoxFrame(bInv, "Inverter");
  drawBoxFrame(bAC, "AC Loads");
  drawBoxFrame(bBatt, "Battery");
  drawBoxFrame(bDC, "DC Loads");
  drawBoxFrame(bPV, "Solar");
  drawConnector(bGrid.x + bGrid.width, bGrid.y + 33, bInv.x, bGrid.y + 33);
  drawConnector(bInv.x + bInv.width, bInv.y + 33, bAC.x, bInv.y + 33);
  drawConnector(bInv.x + bInv.width / 2, bInv.y + bInv.height,
                bInv.x + bInv.width / 2, bBatt.y);
  drawConnector(bBatt.x + bBatt.width, bDC.y + 33, bDC.x, bDC.y + 33);
  drawConnector(bBatt.x + bBatt.width, bPV.y + 33, bPV.x, bPV.y + 33);
}

void drawWatt(const Box& box, int centerY, double watts) {
  tft.fillRect(box.x + 3, centerY - 16, box.width - 6, 32, kBox);
  const String number(static_cast<long>(round(watts)));
  tft.setTextColor(kValue, kBox);
  tft.setTextDatum(ML_DATUM);
  tft.drawString(number, box.x + 8, centerY, 4);
  const int numberWidth = tft.textWidth(number, 4);
  tft.setTextColor(kUnit, kBox);
  tft.drawString("W", box.x + 8 + numberWidth + 4, centerY, 2);
}

void drawBattery() {
  const Box box = bBatt;
  const int right = box.x + box.width - 8;
  tft.fillRect(box.x + box.width - 60, box.y + 5, 56, 16, kBox);
  tft.setTextColor(kTitle, kBox);
  tft.setTextDatum(TR_DATUM);
  tft.drawString("C", right, box.y + 6, 2);
  const int unitWidth = tft.textWidth("C", 2);
  tft.drawCircle(right - unitWidth - 3, box.y + 9, 2, kTitle);
  tft.drawString(String(static_cast<int>(round(dashboardSnapshot.battT))),
                 right - unitWidth - 7, box.y + 6, 2);
  tft.fillRect(box.x + 3, box.y + 22, box.width - 6, 52, kBox);
  const String stateOfCharge(static_cast<int>(round(dashboardSnapshot.soc)));
  tft.setTextColor(kValue, kBox);
  tft.setTextDatum(TL_DATUM);
  tft.drawString(stateOfCharge, box.x + 12, box.y + 22, 7);
  const int stateOfChargeWidth = tft.textWidth(stateOfCharge, 7);
  tft.setTextColor(kUnit, kBox);
  tft.drawString("%", box.x + 18 + stateOfChargeWidth, box.y + 44, 4);
  tft.fillRect(box.x + 3, box.y + 78, box.width - 6, 16, kBox);
  tft.setTextColor(0x6E5F, kBox);
  tft.setTextDatum(TL_DATUM);
  tft.drawString(dashboardSnapshot.battState, box.x + 12, box.y + 78, 2);
  const int batteryX = box.x + 12;
  const int batteryY = box.y + 98;
  const int batteryWidth = box.width - 24;
  const int batteryHeight = 18;
  tft.drawRect(batteryX, batteryY, batteryWidth, batteryHeight, kBorder);
  tft.fillRect(batteryX + 1, batteryY + 1, batteryWidth - 2, batteryHeight - 2, kBox);
  const int fillWidth = static_cast<int>((batteryWidth - 2) *
                                         constrain(dashboardSnapshot.soc, 0, 100) / 100.0);
  tft.fillRect(batteryX + 1, batteryY + 1, fillWidth, batteryHeight - 2, kBlue);
  tft.fillRect(box.x + 3, box.y + 120, box.width - 6, 18, kBox);
  tft.setTextColor(kTitle, kBox);
  tft.setTextDatum(TL_DATUM);
  char line[48];
  std::snprintf(line, sizeof(line), "%.2fV    %.1fA    %dW", dashboardSnapshot.battV,
                dashboardSnapshot.battA, static_cast<int>(round(dashboardSnapshot.battW)));
  tft.drawString(line, box.x + 12, box.y + 121, 2);
}

void drawInverterState() {
  const Box box = bInv;
  tft.fillRect(box.x + 3, box.y + 24, box.width - 6, 38, kBox);
  tft.setTextColor(kValue, kBox);
  tft.setTextDatum(TL_DATUM);
  const String state(dashboardSnapshot.sysState);
  if (tft.textWidth(state, 4) <= box.width - 12) {
    tft.drawString(state, box.x + 8, box.y + 30, 4);
  } else {
    tft.drawString(state, box.x + 8, box.y + 34, 2);
  }
}

void drawDashboardValues() {
  drawWatt(bGrid, bGrid.y + 44, dashboardSnapshot.gridW);
  drawWatt(bAC, bAC.y + 44, dashboardSnapshot.acW);
  drawWatt(bDC, bDC.y + 44, dashboardSnapshot.dcW);
  drawWatt(bPV, bPV.y + 44, dashboardSnapshot.pvW);
  drawInverterState();
  drawBattery();
}

uint16_t wanColor(WanPhase phase) {
  switch (phase) {
    case WanPhase::Online:
      return kGreen;
    case WanPhase::Connecting:
    case WanPhase::Validating:
      return kAmber;
    case WanPhase::Offline:
    default:
      return kRed;
  }
}

int lastDashboardWanHoldCountdown = -1;
int lastDashboardWanPhase = -1;
int lastDashboardHeaderHoldCountdown = -1;
int lastDashboardCenterTextWidth = 0;

void drawDashboardWanIndicator(uint32_t nowMs, bool fullFrameCleared = false,
                               bool highlighted = false) {
  const CamperNetworkStatus networkStatus = camperNetwork.status();
  const int countdown = wifiSetupUi.wanHoldCountdown(nowMs);
  const int phase = static_cast<int>(networkStatus.wanPhase);
  if (!shouldPaintDashboardWan(fullFrameCleared, countdown, phase, lastDashboardWanHoldCountdown,
                               lastDashboardWanPhase)) {
    return;
  }
  const uint16_t statusColor = highlighted ? kAmber : wanColor(networkStatus.wanPhase);
  tft.fillRoundRect(kWifiWanIndicatorBounds.x + 1, 2,
                    kWifiWanIndicatorBounds.width - 3, 20, 4, statusColor);
  tft.setTextColor(kBackground, statusColor);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("WAN", kWifiWanIndicatorBounds.x + kWifiWanIndicatorBounds.width / 2, 12, 2);
  lastDashboardWanHoldCountdown = countdown;
  lastDashboardWanPhase = phase;
}

void drawDashboardHeader(uint32_t nowMs, bool fullFrameCleared = false) {
  gxOnline = isRecentValidGxSnapshot(hasValidGxSnapshot, nowMs,
                                     dashboardSnapshot.receivedAtMs,
                                     kGxSnapshotMaximumAgeMs);
  const int holdCountdown = wifiSetupUi.wanHoldCountdown(nowMs);
  if ((holdCountdown > 0 || lastDashboardHeaderHoldCountdown > 0) &&
      !shouldRepaintDashboardHold(lastDashboardHeaderHoldCountdown, holdCountdown)) {
    return;
  }
  lastDashboardHeaderHoldCountdown = holdCountdown;
  const bool settingsAvailable = canOpenSettings(DisplaySurface::Dashboard,
      connectionController.pendingActive(), connectionController.physicalPortalActive());
  const uint16_t gearColor = settingsAvailable ? kTitle : kLine;
  tft.fillRect(0, 0, 24, 22, kBackground);
  tft.drawCircle(12, 11, 6, gearColor);
  tft.drawCircle(12, 11, 2, gearColor);
  for (int offset : {-1, 1}) {
    tft.drawLine(12 + offset * 6, 11, 12 + offset * 9, 11, gearColor);
    tft.drawLine(12, 11 + offset * 6, 12, 11 + offset * 9, gearColor);
    tft.drawLine(12 + offset * 4, 7, 12 + offset * 6, 5, gearColor);
    tft.drawLine(12 + offset * 4, 15, 12 + offset * 6, 17, gearColor);
  }
  coordinateDashboardWanHold(
      holdCountdown,
      [&](const char* holdLabel) {
        const ClockText normalClock = clockText();
        const char* text = holdLabel == nullptr ? normalClock.time : holdLabel;
        tft.setTextColor(gxOnline ? kValue : kUnit, kBackground);
        tft.setTextDatum(MC_DATUM);
        paintCenterHeaderText(tft, lastDashboardCenterTextWidth, text, holdLabel != nullptr,
                              fullFrameCleared, kBackground, normalClock.meridiem);
      },
      [&](bool highlighted) { drawDashboardWanIndicator(nowMs, fullFrameCleared, highlighted); });

  tft.fillRect(224, 0, 50, 22, kBackground);
  tft.setTextColor(kTitle, kBackground);
  tft.setTextDatum(ML_DATUM);
  tft.drawString("GX", 228, 11, 2);
  tft.fillCircle(268, 10, 3, gxOnline ? kGreen : kRed);

  tft.fillCircle(216, 10, 2, blink ? kLine : kBackground);
}

DisplaySurface currentDisplaySurface() {
  return displaySurfaceFor(touchInputReady, touchInputReady && touchInput.calibrated(),
                           wifiSetupUi.isOpen(), settingsUi.isOpen());
}

void redrawCurrentView(uint32_t nowMs) {
  switch (currentDisplaySurface()) {
    case DisplaySurface::Calibration:
      return;
    case DisplaySurface::Setup:
      wifiSetupUi.render(camperNetwork.status());
      return;
    case DisplaySurface::Settings:
      settingsUi.render();
      return;
    case DisplaySurface::Dashboard:
      lastDashboardWanHoldCountdown = -1;
      lastDashboardWanPhase = -1;
      lastDashboardHeaderHoldCountdown = -1;
      lastDashboardCenterTextWidth = 0;
      redrawDashboardSurface(
          [] { drawDashboardFrame(); },
          [nowMs](bool fullFrameCleared) { drawDashboardHeader(nowMs, fullFrameCleared); },
          [] { drawDashboardValues(); });
      return;
  }
}

void refreshSavedProfiles() {
  NetworkProfile profiles[NetworkProfileStore::kMaxProfiles];
  size_t loadedCount = 0;
  size_t profilesContainingMaterial = 0;
  int activeIndex = -1;
  if (profileStoreReady) {
    const size_t profileCount = profileStore.count();
    bool complete = profileCount <= NetworkProfileStore::kMaxProfiles;
    for (size_t index = 0; complete && index < profileCount; ++index) {
      complete = profileStore.load(index, profiles[index]);
      if (complete) {
        profilesContainingMaterial = index + 1;
      }
    }
    if (complete) {
      loadedCount = profileCount;
      activeIndex = profileStore.activeIndex();
    }
  }
  wifiSetupUi.setSavedProfiles(profiles, loadedCount, activeIndex);
  for (size_t index = 0; index < profilesContainingMaterial; ++index) {
    clearProfile(profiles[index]);
  }
}

void replaceGatewayLifecycle(GatewayLifecycleTarget target, uint32_t nowMs = 0) {
  if (connectionController.replace(target, nowMs)) portal.cancel();
}

void handleConnectionResult(const GatewayConnectionResult& result, uint32_t nowMs) {
  if (result.cancelPortal) portal.cancel();
  if (result.outcome == GatewayConnectionOutcome::None) return;
  if (result.outcome == GatewayConnectionOutcome::Started) {
    if (result.source == PendingProfileSource::DirectOpen ||
        result.source == PendingProfileSource::Portal) {
      wifiSetupUi.close();
      redrawCurrentView(nowMs);
    }
    return;
  }
  refreshSavedProfiles();
  if (result.outcome == GatewayConnectionOutcome::Cancelled) {
    if (result.source == PendingProfileSource::Saved) wifiSetupUi.cancelSavedConnection();
    else wifiSetupUi.cancelCredentialAttempt();
  } else if (result.outcome == GatewayConnectionOutcome::Connected) {
    wifiSetupUi.showResult("Upstream connected", true);
  } else {
    const bool connectionFailure = result.outcome == GatewayConnectionOutcome::Rejected ||
                                   result.outcome == GatewayConnectionOutcome::TimedOut;
    const char* message = "Saved connection failed";
    switch (result.outcome) {
      case GatewayConnectionOutcome::StorageReadFailed: message = "Profile storage unavailable"; break;
      case GatewayConnectionOutcome::PersistenceFailed: message = "Credential persistence failed"; break;
      case GatewayConnectionOutcome::Rejected: message = "Connection rejected"; break;
      case GatewayConnectionOutcome::TimedOut: message = "Connection timed out"; break;
      default: break;
    }
    if (!connectionFailure || result.source != PendingProfileSource::OnDevice ||
        !wifiSetupUi.showCredentialFailure("Connection failed", nowMs)) {
      wifiSetupUi.showResult(message, false);
    }
  }
  wifiSetupUi.render(camperNetwork.status());
}

void beginPendingProfile(CredentialSubmission& submission,
                         PendingProfileSource source, uint32_t nowMs) {
  handleConnectionResult(connectionController.beginSubmitted(submission, source, nowMs,
                                                              profileStoreReady), nowMs);
}

void startPhysicalPortal(const String& ssid, uint8_t securityType, uint32_t nowMs) {
  replaceGatewayLifecycle(GatewayLifecycleTarget::PhysicalPortal, nowMs);
  if (portal.begin(ssid, securityType, nowMs)) {
    wifiSetupUi.showPortal(ssid, portal.pairingCode(), portal.expiresAtMs());
  } else {
    replaceGatewayLifecycle(GatewayLifecycleTarget::Idle, nowMs);
    wifiSetupUi.showResult("Setup portal failed", false);
  }
  wifiSetupUi.render(camperNetwork.status());
}

void handlePendingProfileCommit(uint32_t nowMs) {
  if (!connectionController.pendingActive()) return;
#ifdef CYD_SIMULATION
  const time_t currentEpoch = simulationClock.epoch();
#else
  const time_t currentEpoch = time(nullptr);
#endif
  handleConnectionResult(connectionController.poll(
      nowMs, currentEpoch > 0 ? static_cast<uint32_t>(currentEpoch) : 0), nowMs);
}

void exitWifiSetup(WifiSetupExitReason reason, uint32_t nowMs) {
  replaceGatewayLifecycle(GatewayLifecycleTarget::Exit, nowMs);
  wifiSetupUi.close();
  if (returnToSettings(wifiSetupOrigin, reason == WifiSetupExitReason::Back) &&
      !connectionController.pendingActive() && !connectionController.physicalPortalActive()) {
    settingsUi.open(nowMs, activeTimeSettings);
  }
  wifiSetupOrigin = WifiSetupOrigin::Dashboard;
  redrawCurrentView(nowMs);
}

void handlePortalLifecycle(uint32_t nowMs) {
  CredentialSubmission submission;
  coordinatePortalLifecycle(connectionController.pendingSource() == PendingProfileSource::Saved,
      connectionController.physicalPortalActive(), portal, submission,
      [&](CredentialSubmission& accepted) { beginPendingProfile(accepted, PendingProfileSource::Portal, nowMs); },
      [&] { exitWifiSetup(WifiSetupExitReason::PortalExpired, nowMs); });
}

void handleUiAction(const WifiSetupAction& action, uint32_t nowMs) {
  if (connectionController.pendingSource() == PendingProfileSource::Saved &&
      action.type != WifiSetupActionType::CancelSavedConnection) return;
  switch (action.type) {
    case WifiSetupActionType::ConnectSaved:
      handleConnectionResult(connectionController.beginSaved(action.profileIndex, nowMs,
                                                              profileStoreReady), nowMs);
      break;
    case WifiSetupActionType::ProvisionNew:
      if (provisioningRouteForSecurity(action.securityType) ==
          ProvisioningRoute::DirectPending) {
        CredentialSubmission submission;
        if (submission.set(action.ssid.c_str(), "", action.securityType)) {
          beginPendingProfile(submission, PendingProfileSource::DirectOpen, nowMs);
        }
      } else {
        wifiSetupUi.showCredentialEntry(action.ssid, action.securityType, nowMs);
        wifiSetupUi.render(camperNetwork.status());
      }
      break;
    case WifiSetupActionType::SubmitCredentials: {
      CredentialSubmission submission;
      if (wifiSetupUi.takeCredentialSubmission(submission)) {
        beginPendingProfile(submission, PendingProfileSource::OnDevice, nowMs);
      }
      break;
    }
    case WifiSetupActionType::UsePhone:
      startPhysicalPortal(action.ssid, action.securityType, nowMs);
      break;
    case WifiSetupActionType::CancelCredentialAttempt:
    case WifiSetupActionType::CancelSavedConnection:
      handleConnectionResult(connectionController.cancel(nowMs), nowMs);
      break;
    case WifiSetupActionType::DeleteSaved: {
      const bool erased = profileStoreReady && action.profileIndex >= 0 &&
                          profileStore.erase(static_cast<size_t>(action.profileIndex));
      refreshSavedProfiles();
      if (!erased) {
        wifiSetupUi.showResult("Delete failed", false);
      }
      wifiSetupUi.render(camperNetwork.status());
      break;
    }
    case WifiSetupActionType::Refresh:
      camperNetwork.startScan();
      break;
    case WifiSetupActionType::ClearAll: {
      replaceGatewayLifecycle(GatewayLifecycleTarget::ClearAll, nowMs);
      camperNetwork.disconnectUpstream();
      const bool cleared = profileStoreReady && profileStore.clearUpstreamProfiles();
      refreshSavedProfiles();
      wifiSetupUi.showResult(cleared ? "Saved networks cleared" : "Clear failed", cleared);
      wifiSetupUi.render(camperNetwork.status());
      break;
    }
    case WifiSetupActionType::Exit:
      exitWifiSetup(action.exitReason, nowMs);
      break;
    case WifiSetupActionType::None:
    default:
      break;
  }
}

void handleSettingsAction(SettingsAction action, uint32_t nowMs) {
  switch (action) {
    case SettingsAction::OpenWifi:
      wifiSetupOrigin = WifiSetupOrigin::Settings;
      refreshSavedProfiles();
      wifiSetupUi.openFromSettings(nowMs);
      redrawCurrentView(nowMs);
      break;
    case SettingsAction::Save:
      settingsUi.saveResult(saveAndApplyTimeSettings(timeSettingsStore, activeTimeSettings,
                                                     settingsUi.draft()));
      break;
    case SettingsAction::Exit:
      redrawCurrentView(nowMs);
      break;
    case SettingsAction::None:
      break;
  }
}

void handleTouchAndUiActions(uint32_t nowMs) {
  TouchEvent event;
  if (touchInputReady && touchInput.poll(event)) {
    switch (event.type) {
      case TouchEventType::Press:
      case TouchEventType::Scroll:
        if (event.type == TouchEventType::Press) {
          const bool permitted = currentDisplaySurface() != DisplaySurface::Dashboard ||
              canOpenSettings(currentDisplaySurface(), connectionController.pendingActive(),
                              connectionController.physicalPortalActive());
          touchSurfaceGate.press(currentDisplaySurface(), permitted);
        }
        if (!touchSurfaceGate.allowMove(currentDisplaySurface())) break;
        if (currentDisplaySurface() == DisplaySurface::Settings) {
          if (event.type == TouchEventType::Press) {
            handleSettingsAction(settingsUi.handleTouch(event.point, nowMs), nowMs);
          }
          break;
        }
        if (currentDisplaySurface() == DisplaySurface::Dashboard &&
            event.type == TouchEventType::Press) {
          if (event.point.x >= 0 && event.point.x < 24 && event.point.y >= 0 && event.point.y < 22 &&
              canOpenSettings(currentDisplaySurface(), connectionController.pendingActive(),
                              connectionController.physicalPortalActive())) {
            // Cancel an interrupted WAN hold before the new surface owns contact.
            wifiSetupUi.handleRelease(nowMs);
            settingsUi.open(nowMs, activeTimeSettings);
            break;
          }
          if (kWifiWanIndicatorBounds.contains(event.point)) {
            wifiSetupOrigin = WifiSetupOrigin::Dashboard;
          }
        }
        coordinateSetupInteraction(
            [&] {
              return event.type == TouchEventType::Press
                         ? wifiSetupUi.handleTouch(event.point, nowMs)
                         : wifiSetupUi.handleTouchMove(event.point, nowMs);
            },
            [&] { return wifiSetupUi.takeFullRenderRequest(); },
            [&] { redrawCurrentView(nowMs); },
            [&](const WifiSetupAction& action) { handleUiAction(action, nowMs); },
            [&] { if (currentDisplaySurface() == DisplaySurface::Dashboard) drawDashboardHeader(nowMs); });
        break;
      case TouchEventType::Release:
        touchSurfaceGate.release();
        if (currentDisplaySurface() == DisplaySurface::Settings) {
          settingsUi.handleRelease(nowMs);
          break;
        }
        coordinateDashboardInteraction(
            [&] { handleUiAction(wifiSetupUi.handleRelease(nowMs), nowMs); },
            [&] { if (currentDisplaySurface() == DisplaySurface::Dashboard) drawDashboardHeader(nowMs); },
            [] {});
        break;
      case TouchEventType::CalibrationComplete:
        refreshSavedProfiles();
        redrawCurrentView(nowMs);
        break;
      case TouchEventType::None:
      default:
        break;
    }
  }
  if (currentDisplaySurface() == DisplaySurface::Settings) {
    handleSettingsAction(settingsUi.poll(nowMs), nowMs);
  } else {
    handleUiAction(wifiSetupUi.poll(nowMs, !connectionController.pendingActive() &&
                                         !connectionController.physicalPortalActive()), nowMs);
  }
  if (settingsUi.takeFullRenderRequest()) redrawCurrentView(nowMs);
  if (wifiSetupUi.takeFullRenderRequest()) {
    redrawCurrentView(nowMs);
  }
  switch (currentDisplaySurface()) {
    case DisplaySurface::Calibration:
      return;
    case DisplaySurface::Settings:
      return;
    case DisplaySurface::Setup:
      wifiSetupUi.renderDynamic(camperNetwork.status());
      return;
    case DisplaySurface::Dashboard:
      if (wifiSetupUi.wanHoldCountdown(nowMs) > 0) {
        drawDashboardHeader(nowMs);
      }
      return;
  }
}

void collectScanTerminal() {
  camperNetwork.scanComplete();
  const ScanPhase phase = camperNetwork.scanPhase();
  switch (scanUiOutcome(phase == ScanPhase::Complete, phase == ScanPhase::Failed)) {
    case ScanUiOutcome::DeliverResults: {
      ScanResult results[kMaximumScanResults];
      const size_t count = camperNetwork.scanResults(results, kMaximumScanResults);
      wifiSetupUi.setScanResults(results, count);
      if (wifiSetupUi.isOpen()) {
        wifiSetupUi.render(camperNetwork.status());
      }
      break;
    }
    case ScanUiOutcome::ShowRetryableFailure:
      camperNetwork.clearScanFailure();
      if (wifiSetupUi.showScanFailure("Scan failed; retry")) {
        wifiSetupUi.render(camperNetwork.status());
      }
      break;
    case ScanUiOutcome::None:
    default:
      break;
  }
}

void pollModbusWhenDue(uint32_t nowMs) {
  if (!modbusWorkerReady) {
    gxOnline = false;
    return;
  }

  ModbusWorkResult received;
  if (xQueueReceive(modbusResultQueue, &received, 0) == pdPASS) {
    modbusRequestInFlight = false;
    const bool fresh = isRecentValidGxSnapshot(received.snapshot.valid, millis(),
                                              received.snapshot.receivedAtMs,
                                              kGxSnapshotMaximumAgeMs);
    const bool accepted = venusTracker.recordResult(received.probe, fresh,
                                                     received.snapshot.receivedAtMs);
    if (accepted) {
      dashboardSnapshot = received.snapshot;
      hasValidGxSnapshot = true;
      if (currentDisplaySurface() == DisplaySurface::Dashboard) {
        drawDashboardValues();
      }
    }
  }

  const VenusProbe request = venusTracker.request();
  const bool targetChanged = request.token != lastModbusTargetToken;
  if (!modbusRequestInFlight &&
      (targetChanged || (request.address && nowMs - lastModbusRequestMs >= kModbusPollMs))) {
    // A zero-address change is a worker command too: close the old socket even
    // when the device remains absent and there is no next endpoint to poll.
    if (xQueueSend(modbusRequestQueue, &request, 0) == pdPASS) {
      modbusRequestInFlight = true;
      lastModbusRequestMs = nowMs;
      lastModbusTargetToken = request.token;
    }
  }
}

void refreshVenusAddress(uint32_t nowMs) {
  if (nowMs - lastVenusRefreshMs >= 250) {
    lastVenusRefreshMs = nowMs;
    const auto network = camperNetwork.bridgeSnapshot(nowMs);
    wifiSetupUi.setPortalAddress(network.bridged && network.ready
                                    ? camperNetwork.status().upstreamAddress
                                    : IPAddress(192, 168, 50, 1));
    const auto previous = venusTracker.request();
    venusTracker.update(network.clients, network.ready ? network.count : 0,
                        network.generation, nowMs);
    if (previous.token != venusTracker.request().token) {
      hasValidGxSnapshot = false;
      gxOnline = false;
    }
  }
  venusTracker.persistIdentity(nowMs);
  settingsUi.setVenusStatus(venusTracker.status(nowMs));
}

void updateClockAndStatusWhenDue(uint32_t nowMs) {
  if (nowMs - lastClockMs < 1000) {
    return;
  }
  lastClockMs = nowMs;
  blink = !blink;
  switch (currentDisplaySurface()) {
    case DisplaySurface::Calibration:
      return;
    case DisplaySurface::Settings:
      return;
    case DisplaySurface::Setup:
      wifiSetupUi.renderDynamic(camperNetwork.status());
      return;
    case DisplaySurface::Dashboard:
      drawDashboardHeader(nowMs);
      return;
  }
}

void drawPrivateApFailureAndRestart() {
  tft.fillScreen(kBackground);
  tft.setTextColor(kValue, kBackground);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("Private AP failed", kScreenWidth / 2, kScreenHeight / 2, 4);
  delay(5000);
  ESP.restart();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  tft.init();
  tft.setRotation(1);
  tft.invertDisplay(true);
  tft.fillScreen(kBackground);

  const uint32_t startedAtMs = millis();
  if (!camperNetwork.begin(SECRET_CAMPER_AP_SSID, SECRET_CAMPER_AP_PASS, startedAtMs)) {
    drawPrivateApFailureAndRestart();
    return;
  }

  profileStoreReady = profileStore.begin();
  if (profileStoreReady) {
    const int activeIndex = profileStore.activeIndex();
    NetworkProfile activeProfile;
    if (activeIndex >= 0 && profileStore.load(static_cast<size_t>(activeIndex), activeProfile) &&
        camperNetwork.connect(activeProfile, millis())) {
      camperNetwork.acceptPendingProfile();
    }
    clearProfile(activeProfile);
  }

  touchInputReady = touchInput.begin();
  timeSettingsStore.load(activeTimeSettings);
  applyTimeSettings(activeTimeSettings);
  venusTracker.begin();
#ifdef CYD_SIMULATION
  // Initialize local sockets for the real portal without Wi-Fi or NTP traffic.
  if (!Network.begin()) {
    Serial.println("SIM ERROR network initialization");
    return;
  }
#else
  configTzTime(findTimeZone(activeTimeSettings.zoneId)->posix, "pool.ntp.org", "time.google.com");
#endif
  modbusWorkerReady = startModbusWorker();
  if (!modbusWorkerReady) {
    Serial.println("[MB] worker unavailable");
  }
  refreshSavedProfiles();
  copyModbusText(dashboardSnapshot.battState, sizeof(dashboardSnapshot.battState), "-");
  copyModbusText(dashboardSnapshot.sysState, sizeof(dashboardSnapshot.sysState), "-");
  if (currentDisplaySurface() != DisplaySurface::Calibration) {
    redrawCurrentView(millis());
  }

  esp_task_wdt_config_t watchdogConfig = {
      .timeout_ms = static_cast<uint32_t>(WDT_TIMEOUT_S) * 1000,
      .idle_core_mask = 0,
      .trigger_panic = true,
  };
  if (esp_task_wdt_init(&watchdogConfig) == ESP_ERR_INVALID_STATE) {
    esp_task_wdt_reconfigure(&watchdogConfig);
  }
  esp_task_wdt_add(nullptr);
  lastModbusRequestMs = millis() - kModbusPollMs;
  lastClockMs = millis();
#ifdef CYD_SIMULATION
  Serial.println("SIM READY");
#endif
}

void loop() {
  esp_task_wdt_reset();
#ifdef CYD_SIMULATION
  simulationControl.poll();
#endif
  const uint32_t nowMs = millis();
  camperNetwork.poll(nowMs);
  refreshVenusAddress(nowMs);
  portal.poll(nowMs);
  handlePortalLifecycle(nowMs);
  handleTouchAndUiActions(nowMs);
  collectScanTerminal();
  handlePendingProfileCommit(nowMs);
  pollModbusWhenDue(nowMs);
  updateClockAndStatusWhenDue(nowMs);
  delay(5);
}
