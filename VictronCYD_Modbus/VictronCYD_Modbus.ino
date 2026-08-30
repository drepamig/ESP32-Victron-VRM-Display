/*
 * ESP32-Victron-VRM-Display — local Modbus TCP dashboard and camper gateway.
 *
 * The ESP32 keeps its private AP/NAPT service alive while independently managing
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

#include "CamperNetwork.h"
#include "GatewayApplicationPolicy.h"
#include "ModbusSnapshotPolicy.h"
#include "NetworkProfiles.h"
#include "TouchInput.h"
#include "WifiSetupUi.h"
#include "esp_task_wdt.h"
#include "secrets.h"

#define WDT_TIMEOUT_S 30

namespace {

constexpr long kTimezoneOffsetSeconds = 7200;
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
CamperNetwork camperNetwork;
NetworkProfileStore profileStore;
ProvisioningPortal portal;

WiFiClient modbusClient;
uint16_t modbusTransactionId = 0;
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
uint32_t lastClockMs = 0;

GatewayLifecyclePolicy gatewayLifecycle;
NetworkProfile pendingProfile;
NetworkProfile previousProfile;
bool previousProfileAvailable = false;

const Box bGrid{4, 24, 100, 66};
const Box bInv{110, 24, 100, 66};
const Box bAC{216, 24, 100, 66};
const Box bBatt{4, 96, 206, 140};
const Box bDC{216, 96, 100, 66};
const Box bPV{216, 170, 100, 66};

int signedRegister(uint16_t value) {
  return value > 32767 ? static_cast<int>(value) - 65536 : static_cast<int>(value);
}

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

void copyText(char* destination, size_t capacity, const char* source) {
  if (capacity == 0) {
    return;
  }
  std::snprintf(destination, capacity, "%s", source == nullptr ? "-" : source);
}

const char* batteryStateText(int code) {
  switch (code) {
    case 0:
      return "idle";
    case 1:
      return "charging";
    case 2:
      return "discharging";
    default:
      return "-";
  }
}

void setVebusStateText(int state, char output[20]) {
  const char* text = nullptr;
  switch (state) {
    case 0: text = "Off"; break;
    case 1: text = "Low power"; break;
    case 2: text = "Fault"; break;
    case 3: text = "Bulk"; break;
    case 4: text = "Absorption"; break;
    case 5: text = "Float"; break;
    case 6: text = "Storage"; break;
    case 7: text = "Equalize"; break;
    case 8: text = "Passthru"; break;
    case 9: text = "Inverting"; break;
    case 10: text = "Power assist"; break;
    case 11: text = "Power supply"; break;
    case 244: text = "Sustain"; break;
    case 252: text = "Ext. control"; break;
    default: break;
  }
  if (text == nullptr) {
    std::snprintf(output, 20, "%d", state);
  } else {
    copyText(output, 20, text);
  }
}

String clockText() {
  struct tm currentTime;
  if (!getLocalTime(&currentTime, 0)) {
    return "--:--";
  }
  char output[6];
  std::snprintf(output, sizeof(output), "%02d:%02d", currentTime.tm_hour,
                currentTime.tm_min);
  return String(output);
}

bool readModbusRegisters(uint8_t unit, uint16_t address, uint16_t count,
                         uint16_t* output) {
  if (!modbusClient.connected()) {
    modbusClient.stop();
    if (!modbusClient.connect(SECRET_GX_IP, 502, 3000)) {
      return false;
    }
  }
  while (modbusClient.available()) {
    modbusClient.read();
  }

  ++modbusTransactionId;
  uint8_t request[12] = {
      static_cast<uint8_t>(modbusTransactionId >> 8),
      static_cast<uint8_t>(modbusTransactionId), 0, 0, 0, 6, unit, 0x03,
      static_cast<uint8_t>(address >> 8), static_cast<uint8_t>(address),
      static_cast<uint8_t>(count >> 8), static_cast<uint8_t>(count)};
  if (modbusClient.write(request, sizeof(request)) != sizeof(request)) {
    modbusClient.stop();
    return false;
  }

  uint32_t startedAtMs = millis();
  while (modbusClient.available() < 9) {
    if (!modbusClient.connected() || millis() - startedAtMs > 800) {
      modbusClient.stop();
      return false;
    }
    delay(1);
  }
  uint8_t header[9];
  if (modbusClient.readBytes(header, sizeof(header)) != sizeof(header)) {
    modbusClient.stop();
    return false;
  }
  const uint16_t responseTransactionId =
      static_cast<uint16_t>((header[0] << 8) | header[1]);
  if (responseTransactionId != modbusTransactionId || header[7] != 0x03 ||
      header[8] != count * 2 || header[8] > 64) {
    modbusClient.stop();
    return false;
  }

  const uint8_t byteCount = header[8];
  uint8_t data[64];
  startedAtMs = millis();
  while (modbusClient.available() < byteCount) {
    if (!modbusClient.connected() || millis() - startedAtMs > 800) {
      modbusClient.stop();
      return false;
    }
    delay(1);
  }
  if (modbusClient.readBytes(data, byteCount) != byteCount) {
    modbusClient.stop();
    return false;
  }
  for (uint16_t index = 0; index < count; ++index) {
    output[index] = static_cast<uint16_t>((data[index * 2] << 8) | data[index * 2 + 1]);
  }
  return true;
}

void fetchModbusReadCycle(ModbusReadCycle& cycle) {
  cycle = {};
  copyText(cycle.battState, sizeof(cycle.battState), "-");
  copyText(cycle.sysState, sizeof(cycle.sysState), "-");

  uint16_t registers[8];
  bool acValuesReady = false;
  bool batteryValuesReady = false;
  if (readModbusRegisters(100, 817, 4, registers)) {
    cycle.acW = signedRegister(registers[0]);
    cycle.gridW = signedRegister(registers[3]);
    acValuesReady = true;
  }
  if (readModbusRegisters(100, 840, 5, registers)) {
    cycle.battV = registers[0] / 10.0;
    cycle.battA = signedRegister(registers[1]) / 10.0;
    cycle.battW = signedRegister(registers[2]);
    cycle.soc = registers[3] > 100 ? registers[3] / 10.0 : registers[3];
    copyText(cycle.battState, sizeof(cycle.battState),
             batteryStateText(registers[4]));
    batteryValuesReady = true;
  }
  cycle.requiredValid = acValuesReady && batteryValuesReady;

  uint16_t value[1];
  if (readModbusRegisters(100, 850, 1, value)) {
    cycle.pvReady = true;
    cycle.pvW = value[0];
  }
  if (readModbusRegisters(100, 860, 1, value)) {
    cycle.dcReady = true;
    cycle.dcW = signedRegister(value[0]);
  }
  if (readModbusRegisters(228, 31, 1, value)) {
    cycle.systemStateReady = true;
    setVebusStateText(value[0], cycle.sysState);
  }
  if (readModbusRegisters(225, 262, 1, value)) {
    cycle.batteryTemperatureReady = true;
    cycle.battT = signedRegister(value[0]) / 10.0;
  }

  cycle.receivedAtMs = millis();
}

void modbusWorker(void*) {
  uint8_t request = 0;
  ModbusSnapshot retainedSnapshot = makeDefaultModbusSnapshot();
  for (;;) {
    if (xQueueReceive(modbusRequestQueue, &request, portMAX_DELAY) != pdPASS) {
      continue;
    }
    ModbusReadCycle cycle;
    fetchModbusReadCycle(cycle);
    retainedSnapshot = mergeModbusSnapshot(retainedSnapshot, cycle);
    Serial.printf("[MB] result=%s\n", retainedSnapshot.valid ? "valid" : "invalid");
    xQueueOverwrite(modbusResultQueue, &retainedSnapshot);
  }
}

bool startModbusWorker() {
  modbusRequestQueue = xQueueCreate(1, sizeof(uint8_t));
  modbusResultQueue = xQueueCreate(1, sizeof(ModbusSnapshot));
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
  tft.drawString(SECRET_SITE_NAME, 4, 11, 2);
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
  coordinateDashboardWanHold(
      holdCountdown,
      [&](const char* holdLabel) {
        tft.fillRect(kScreenWidth / 2 - 30, 0, 60, 20, kBackground);
        tft.setTextColor(gxOnline ? kValue : kUnit, kBackground);
        tft.setTextDatum(MC_DATUM);
        tft.drawString(holdLabel == nullptr ? clockText() : holdLabel, kScreenWidth / 2, 11, 4);
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
                           wifiSetupUi.isOpen());
}

void redrawCurrentView(uint32_t nowMs) {
  switch (currentDisplaySurface()) {
    case DisplaySurface::Calibration:
      return;
    case DisplaySurface::Setup:
      wifiSetupUi.render(camperNetwork.status());
      return;
    case DisplaySurface::Dashboard:
      lastDashboardWanHoldCountdown = -1;
      lastDashboardWanPhase = -1;
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

void clearPendingApplicationBuffers() {
  clearProfile(pendingProfile);
  clearProfile(previousProfile);
  previousProfileAvailable = false;
}

void replaceGatewayLifecycle(GatewayLifecycleTarget target, uint32_t nowMs = 0,
                             int previousActiveIndex = -1) {
  const GatewayLifecycleReplacement replacement =
      gatewayLifecycle.replaceWith(target, nowMs, previousActiveIndex);
  if (replacement.cancelPendingProfile) {
    camperNetwork.cancelPendingProfile();
  }
  if (replacement.cancelPhysicalPortal) {
    portal.cancel();
  }
  if (target != GatewayLifecycleTarget::Exit || !gatewayLifecycle.pendingActive()) {
    clearPendingApplicationBuffers();
  }
}

bool reconnectPreviousProfile(uint32_t nowMs, int previousIndex) {
  if (!previousProfileAvailable || previousIndex < 0) {
    return false;
  }
  const bool connected = camperNetwork.connect(previousProfile, nowMs);
  if (connected) {
    camperNetwork.acceptPendingProfile();
  }
  return connected;
}

void showPendingFailure(const char* message, const PendingProfileEvaluation& evaluation,
                        uint32_t nowMs) {
  camperNetwork.cancelPendingProfile();
  if (evaluation.outcome == PendingProfileOutcome::RestorePrevious) {
    reconnectPreviousProfile(nowMs, evaluation.previousActiveIndex);
  }
  gatewayLifecycle.completePending();
  clearPendingApplicationBuffers();
  refreshSavedProfiles();
  wifiSetupUi.showResult(message, false);
  wifiSetupUi.render(camperNetwork.status());
}

void beginPendingProfile(const String& ssid, String& passphrase, uint8_t securityType,
                         uint32_t nowMs) {
  NetworkProfile retainedPreviousProfile;
  bool retainedPreviousAvailable = false;
  int previousActiveIndex = -1;
  if (profileStoreReady) {
    previousActiveIndex = profileStore.activeIndex();
    retainedPreviousAvailable = previousActiveIndex >= 0 &&
                                profileStore.load(static_cast<size_t>(previousActiveIndex),
                                                  retainedPreviousProfile);
    if (!retainedPreviousAvailable) {
      previousActiveIndex = -1;
    }
  }

  replaceGatewayLifecycle(GatewayLifecycleTarget::PendingProfile, nowMs,
                          previousActiveIndex);
  previousProfileAvailable = retainedPreviousAvailable;
  if (retainedPreviousAvailable) {
    previousProfile = retainedPreviousProfile;
  }
  clearProfile(retainedPreviousProfile);

  pendingProfile.ssid = ssid;
  pendingProfile.passphrase = passphrase;
  pendingProfile.securityType = securityType;
  pendingProfile.lastSuccessEpoch = 0;
  secureClearString(passphrase);
  wifiSetupUi.close();
  redrawCurrentView(nowMs);
  if (!camperNetwork.connect(pendingProfile, nowMs)) {
    showPendingFailure("Connection rejected", gatewayLifecycle.pendingImmediateFailure(), nowMs);
  }
}

void handlePendingProfileCommit(uint32_t nowMs) {
  if (!gatewayLifecycle.pendingActive()) {
    return;
  }
  const PendingProfileEvaluation evaluation =
      gatewayLifecycle.evaluatePending(camperNetwork.pendingProfileConnected(), nowMs);
  if (evaluation.outcome == PendingProfileOutcome::None) {
    return;
  }
  if (evaluation.outcome != PendingProfileOutcome::Commit) {
    showPendingFailure("Connection timed out", evaluation, nowMs);
    return;
  }

  const time_t currentEpoch = time(nullptr);
  pendingProfile.lastSuccessEpoch = currentEpoch > 0 ? static_cast<uint32_t>(currentEpoch) : 0;
  size_t storedIndex = 0;
  if (!profileStoreReady || !profileStore.upsertAndActivate(pendingProfile, storedIndex)) {
    showPendingFailure("Credential persistence failed",
                       gatewayLifecycle.pendingImmediateFailure(), nowMs);
    return;
  }

  camperNetwork.acceptPendingProfile();
  gatewayLifecycle.completePending();
  clearPendingApplicationBuffers();
  refreshSavedProfiles();
  wifiSetupUi.showResult("Upstream connected", true);
  wifiSetupUi.render(camperNetwork.status());
}

void handlePortalLifecycle(uint32_t nowMs) {
  ProvisioningSubmission submission;
  if (portal.takeSubmission(submission)) {
    beginPendingProfile(submission.ssid, submission.passphrase,
                        submission.securityType, nowMs);
    secureClearString(submission.passphrase);
    submission.ssid = String();
    return;
  }
  if (gatewayLifecycle.physicalPortalActive() && !portal.active()) {
    replaceGatewayLifecycle(GatewayLifecycleTarget::Idle);
    wifiSetupUi.showResult("Setup portal expired", false);
    wifiSetupUi.render(camperNetwork.status());
  }
}

void handleUiAction(const WifiSetupAction& action, uint32_t nowMs) {
  switch (action.type) {
    case WifiSetupActionType::ConnectSaved: {
      replaceGatewayLifecycle(GatewayLifecycleTarget::SavedConnection);
      NetworkProfile profile;
      const bool loaded = profileStoreReady && action.profileIndex >= 0 &&
                          profileStore.load(static_cast<size_t>(action.profileIndex), profile);
      const bool connecting = loaded && camperNetwork.connect(profile, nowMs);
      if (connecting) {
        camperNetwork.acceptPendingProfile();
      } else {
        wifiSetupUi.showResult("Saved connection failed", false);
      }
      clearProfile(profile);
      refreshSavedProfiles();
      wifiSetupUi.render(camperNetwork.status());
      break;
    }
    case WifiSetupActionType::ProvisionNew:
      if (provisioningRouteForSecurity(action.securityType) ==
          ProvisioningRoute::DirectPending) {
        String emptyPassphrase;
        beginPendingProfile(action.ssid, emptyPassphrase, action.securityType, nowMs);
      } else {
        replaceGatewayLifecycle(GatewayLifecycleTarget::PhysicalPortal);
        if (portal.begin(action.ssid, action.securityType, nowMs)) {
          wifiSetupUi.showPortal(action.ssid, portal.pairingCode(), portal.expiresAtMs());
        } else {
          replaceGatewayLifecycle(GatewayLifecycleTarget::Idle);
          wifiSetupUi.showResult("Setup portal failed", false);
        }
        wifiSetupUi.render(camperNetwork.status());
      }
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
      replaceGatewayLifecycle(GatewayLifecycleTarget::ClearAll);
      camperNetwork.disconnectUpstream();
      const bool cleared = profileStoreReady && profileStore.clearUpstreamProfiles();
      refreshSavedProfiles();
      wifiSetupUi.showResult(cleared ? "Saved networks cleared" : "Clear failed", cleared);
      wifiSetupUi.render(camperNetwork.status());
      break;
    }
    case WifiSetupActionType::Exit:
      replaceGatewayLifecycle(GatewayLifecycleTarget::Exit);
      wifiSetupUi.close();
      redrawCurrentView(nowMs);
      break;
    case WifiSetupActionType::None:
    default:
      break;
  }
}

void handleTouchAndUiActions(uint32_t nowMs) {
  bool dashboardInteraction = false;
  TouchEvent event;
  if (touchInputReady && touchInput.poll(event)) {
    switch (event.type) {
      case TouchEventType::Press:
      case TouchEventType::Scroll:
        handleUiAction(wifiSetupUi.handleTouch(event.point, nowMs), nowMs);
        dashboardInteraction = true;
        break;
      case TouchEventType::Release:
        handleUiAction(wifiSetupUi.handleRelease(nowMs), nowMs);
        dashboardInteraction = true;
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
  handleUiAction(wifiSetupUi.poll(nowMs), nowMs);
  if (wifiSetupUi.takeFullRenderRequest()) {
    redrawCurrentView(nowMs);
  }
  switch (currentDisplaySurface()) {
    case DisplaySurface::Calibration:
      return;
    case DisplaySurface::Setup:
      wifiSetupUi.renderDynamic(camperNetwork.status());
      return;
    case DisplaySurface::Dashboard:
      if (dashboardInteraction || wifiSetupUi.wanHoldCountdown(nowMs) > 0) {
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

  ModbusSnapshot received;
  if (xQueueReceive(modbusResultQueue, &received, 0) == pdPASS) {
    modbusRequestInFlight = false;
    if (received.valid) {
      dashboardSnapshot = received;
      hasValidGxSnapshot = true;
      if (currentDisplaySurface() == DisplaySurface::Dashboard) {
        drawDashboardValues();
      }
    }
  }

  if (!modbusRequestInFlight && nowMs - lastModbusRequestMs >= kModbusPollMs) {
    const uint8_t request = 1;
    if (xQueueSend(modbusRequestQueue, &request, 0) == pdPASS) {
      modbusRequestInFlight = true;
      lastModbusRequestMs = nowMs;
    }
  }
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
  configTime(kTimezoneOffsetSeconds, 0, "pool.ntp.org", "time.google.com");
  modbusWorkerReady = startModbusWorker();
  if (!modbusWorkerReady) {
    Serial.println("[MB] worker unavailable");
  }
  refreshSavedProfiles();
  copyText(dashboardSnapshot.battState, sizeof(dashboardSnapshot.battState), "-");
  copyText(dashboardSnapshot.sysState, sizeof(dashboardSnapshot.sysState), "-");
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
}

void loop() {
  esp_task_wdt_reset();
  const uint32_t nowMs = millis();
  camperNetwork.poll(nowMs);
  portal.poll(nowMs);
  handlePortalLifecycle(nowMs);
  handleTouchAndUiActions(nowMs);
  collectScanTerminal();
  handlePendingProfileCommit(nowMs);
  pollModbusWhenDue(nowMs);
  updateClockAndStatusWhenDue(nowMs);
  delay(5);
}
