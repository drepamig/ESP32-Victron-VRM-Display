#include "CamperNetwork.h"

#include <Network.h>
#include <WiFi.h>
#include <algorithm>
#include <cstring>
#include <vector>
#include <freertos/task.h>

namespace {

constexpr uint32_t kValidationIntervalMs = 30000;
constexpr uint32_t kValidationTaskStack = 3072;
constexpr UBaseType_t kValidationTaskPriority = 1;

void secureClear(char* value, size_t length) {
  volatile char* cursor = value;
  while (length-- > 0) {
    *cursor++ = 0;
  }
}

}  // namespace

bool CamperNetwork::begin(const char* apSsid, const char* apPassword, uint32_t) {
  if (beginAttempted_) {
    return apReady_;
  }
  if (apPassword == nullptr) {
    return false;
  }
  const size_t passwordLength = std::strlen(apPassword);
  if (passwordLength < 12 || passwordLength > 63) {
    return false;
  }

  if (validationQueue_ == nullptr) {
    validationQueue_ = xQueueCreate(1, sizeof(bool));
    if (validationQueue_ == nullptr) {
      return false;
    }
  }
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP_STA);
  const bool configReady =
      WiFi.AP.config(IPAddress(192, 168, 50, 1), IPAddress(192, 168, 50, 1),
                     IPAddress(255, 255, 255, 0), IPAddress(192, 168, 50, 100),
                     IPAddress(1, 1, 1, 1));
  if (!configReady) {
    Serial.print("Camper AP failed at ");
    Serial.println(WiFi.AP.localIP());
    return false;
  }
  const bool apReady = WiFi.AP.create(apSsid, apPassword, 6, false, 4);
  apReady_ = apReady && bridge_.begin();
  if (apReady_) {
    beginAttempted_ = true;
    WiFi.setAutoReconnect(false);
  }

  Serial.print("Camper AP ");
  Serial.print(apReady_ ? "ready" : "failed");
  Serial.print(" at ");
  Serial.println(WiFi.AP.localIP());
  return apReady_;
}

void CamperNetwork::poll(uint32_t nowMs) {
  bridge_.poll(stationReady(), nowMs);
  bool validationSucceeded = false;
  if (validationWorkerActive_ &&
      xQueueReceive(validationQueue_, &validationSucceeded, 0) == pdPASS) {
    const bool resultCurrent = validationResultCurrent_;
    validationWorkerActive_ = false;
    validationResultCurrent_ = false;
    if (resultCurrent && stationLifecycleActive_ && wanPhase_ == WanPhase::Validating &&
        stationReady()) {
      wanPhase_ = validationSucceeded ? WanPhase::Online : WanPhase::Offline;
      if (validationSucceeded) {
        retryBackoff_.reset();
      }
      validationScheduled_ = true;
      validationDeadlineMs_ = nowMs + kValidationIntervalMs;
    }
  }

  if (!stationReady()) {
    if (validationWorkerActive_) {
      validationResultCurrent_ = false;
    }
    validationScheduled_ = false;
    if (!selectedProfile_) {
      wanPhase_ = WanPhase::Offline;
      retryScheduled_ = false;
      return;
    }
    wanPhase_ = stationLifecycleEstablished_ ? WanPhase::Offline : WanPhase::Connecting;
    if (!retryScheduled_) {
      retryDeadlineMs_ = nowMs + retryBackoff_.nextDelay();
      retryScheduled_ = true;
    }
    if (isDeadlineReached(nowMs, retryDeadlineMs_)) {
      beginSelectedProfile();
      retryDeadlineMs_ = nowMs + retryBackoff_.nextDelay();
    }
    return;
  }

  retryScheduled_ = false;
  if (!stationLifecycleActive_) {
    wanPhase_ = WanPhase::Offline;
    validationScheduled_ = false;
    return;
  }
  stationLifecycleEstablished_ = true;
  if (validationWorkerActive_) {
    // Restored station readiness still needs fresh DNS after the old worker drains.
    wanPhase_ = WanPhase::Validating;
    return;
  }
  if (!validationScheduled_ || isDeadlineReached(nowMs, validationDeadlineMs_)) {
    startValidation(nowMs);
  }
}

bool CamperNetwork::connect(const NetworkProfile& profile, uint32_t nowMs) {
  const size_t ssidLength = profile.ssid.length();
  const size_t passphraseLength = profile.passphrase.length();
  if (!apReady_ || ssidLength == 0 || ssidLength > 32 || passphraseLength > 63) {
    return false;
  }

  bridge_.poll(false, nowMs);
  clearSelectedProfile();
  std::memcpy(selectedSsid_, profile.ssid.c_str(), ssidLength + 1);
  std::memcpy(selectedPassphrase_, profile.passphrase.c_str(), passphraseLength + 1);
  selectedProfile_ = true;
  pendingProfile_ = true;
  stationLifecycleActive_ = true;
  stationLifecycleEstablished_ = false;
  validationResultCurrent_ = false;
  retryBackoff_.reset();
  retryDeadlineMs_ = nowMs + retryBackoff_.nextDelay();
  retryScheduled_ = true;
  validationScheduled_ = false;
  wanPhase_ = WanPhase::Connecting;
  beginSelectedProfile();
  return true;
}

bool CamperNetwork::startScan() {
  if (scanPhase_ == ScanPhase::Running) {
    return false;
  }
  scanPhase_ = WiFi.scanNetworks(true, false) == WIFI_SCAN_RUNNING
                   ? ScanPhase::Running
                   : ScanPhase::Failed;
  return scanPhase_ == ScanPhase::Running;
}

bool CamperNetwork::scanComplete() const {
  if (scanPhase_ == ScanPhase::Complete) {
    return true;
  }
  if (scanPhase_ != ScanPhase::Running) {
    return false;
  }
  const int16_t result = WiFi.scanComplete();
  if (result == WIFI_SCAN_RUNNING) {
    return false;
  }
  if (result == WIFI_SCAN_FAILED) {
    scanPhase_ = ScanPhase::Failed;
    return false;
  }
  scanPhase_ = ScanPhase::Complete;
  return true;
}

ScanPhase CamperNetwork::scanPhase() const {
  return scanPhase_;
}

void CamperNetwork::clearScanFailure() {
  if (scanPhase_ == ScanPhase::Failed) {
    scanPhase_ = ScanPhase::Idle;
  }
}

size_t CamperNetwork::scanResults(ScanResult* output, size_t capacity) {
  if (scanPhase_ != ScanPhase::Complete) {
    return 0;
  }
  const int16_t scanCount = WiFi.scanComplete();
  if (scanCount == WIFI_SCAN_RUNNING) {
    return 0;
  }
  if (scanCount == WIFI_SCAN_FAILED) {
    scanPhase_ = ScanPhase::Failed;
    WiFi.scanDelete();
    return 0;
  }

  std::vector<ScanResult> unique;
  const int16_t boundedCount = std::min<int16_t>(scanCount, 255);
  unique.reserve(static_cast<size_t>(boundedCount));
  for (int16_t index = 0; index < boundedCount; ++index) {
    const String ssid = WiFi.SSID(static_cast<uint8_t>(index));
    if (ssid.isEmpty()) {
      continue;
    }
    const int32_t rssi = WiFi.RSSI(static_cast<uint8_t>(index));
    auto existing = std::find_if(unique.begin(), unique.end(), [&ssid](const ScanResult& result) {
      return result.ssid == ssid;
    });
    if (existing == unique.end()) {
      unique.push_back({ssid, rssi,
                        static_cast<uint8_t>(WiFi.encryptionType(static_cast<uint8_t>(index))),
                        WiFi.channel(static_cast<uint8_t>(index))});
    } else if (rssi > existing->rssi) {
      *existing = {ssid, rssi,
                   static_cast<uint8_t>(WiFi.encryptionType(static_cast<uint8_t>(index))),
                   WiFi.channel(static_cast<uint8_t>(index))};
    }
  }
  std::sort(unique.begin(), unique.end(), [](const ScanResult& left, const ScanResult& right) {
    return left.rssi > right.rssi;
  });

  const size_t copied = output == nullptr ? 0 : std::min(capacity, unique.size());
  for (size_t index = 0; index < copied; ++index) {
    output[index] = unique[index];
  }
  WiFi.scanDelete();
  scanPhase_ = ScanPhase::Idle;
  return copied;
}

CamperNetworkStatus CamperNetwork::status() const {
  const bool ready = stationReady();
  return {apReady_, wanPhase_, ready ? WiFi.localIP() : IPAddress(),
          ready ? static_cast<int32_t>(WiFi.RSSI()) : 0,
          static_cast<uint8_t>(apReady_ ? WiFi.AP.stationCount() : 0)};
}

bool CamperNetwork::pendingProfileConnected() const {
  return pendingProfile_ && stationReady() && WiFi.SSID() == String(selectedSsid_);
}

void CamperNetwork::acceptPendingProfile() {
  if (!pendingProfile_) {
    return;
  }
  pendingProfile_ = false;
}

void CamperNetwork::cancelPendingProfile(bool clearTransientStationConfig) {
  if (!pendingProfile_) {
    return;
  }
  WiFi.disconnect(false, clearTransientStationConfig);
  bridge_.poll(false, millis());
  pendingProfile_ = false;
  selectedProfile_ = false;
  stationLifecycleActive_ = false;
  stationLifecycleEstablished_ = false;
  validationResultCurrent_ = false;
  retryScheduled_ = false;
  validationScheduled_ = false;
  wanPhase_ = WanPhase::Offline;
  clearSelectedProfile();
}

void CamperNetwork::disconnectUpstream() {
  WiFi.disconnect(false, true);
  bridge_.poll(false, millis());
  pendingProfile_ = false;
  selectedProfile_ = false;
  stationLifecycleActive_ = false;
  stationLifecycleEstablished_ = false;
  validationResultCurrent_ = false;
  retryScheduled_ = false;
  validationScheduled_ = false;
  wanPhase_ = WanPhase::Offline;
  clearSelectedProfile();
}

void CamperNetwork::validationWorker(void* context) {
  QueueHandle_t validationQueue = static_cast<QueueHandle_t>(context);
  IPAddress result;
  const bool succeeded = Network.hostByName("vrm.victronenergy.com", result) != 0;
  xQueueSend(validationQueue, &succeeded, portMAX_DELAY);
  vTaskDelete(nullptr);
}

void CamperNetwork::beginSelectedProfile() {
  WiFi.begin(selectedSsid_, selectedPassphrase_);
}

void CamperNetwork::startValidation(uint32_t nowMs) {
  if (validationWorkerActive_ || validationQueue_ == nullptr) {
    return;
  }
  validationWorkerActive_ = true;
  validationResultCurrent_ = true;
  validationScheduled_ = false;
  wanPhase_ = WanPhase::Validating;
  if (xTaskCreatePinnedToCore(validationWorker, "wan-validation", kValidationTaskStack,
                              validationQueue_, kValidationTaskPriority, nullptr, 0) != pdPASS) {
    validationWorkerActive_ = false;
    validationResultCurrent_ = false;
    wanPhase_ = WanPhase::Offline;
    validationScheduled_ = true;
    validationDeadlineMs_ = nowMs + kValidationIntervalMs;
  }
}

void CamperNetwork::clearSelectedProfile() {
  secureClear(selectedSsid_, sizeof(selectedSsid_));
  secureClear(selectedPassphrase_, sizeof(selectedPassphrase_));
}

bool CamperNetwork::stationReady() const {
  return stationLifecycleActive_ && WiFi.isConnected() &&
         static_cast<uint32_t>(WiFi.localIP()) != 0 && WiFi.SSID() == String(selectedSsid_);
}

BridgeNetworkSnapshot CamperNetwork::bridgeSnapshot(uint32_t nowMs) const {
  return bridge_.snapshot(nowMs);
}
