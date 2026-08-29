#pragma once

#include <Arduino.h>
#include <IPAddress.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "GatewayPolicy.h"
#include "NetworkProfiles.h"

enum class WanPhase : uint8_t { Offline, Connecting, Validating, Online };

struct ScanResult {
  String ssid;
  int32_t rssi;
  uint8_t encryptionType;
  int32_t channel;
};

struct CamperNetworkStatus {
  bool apReady;
  WanPhase wanPhase;
  IPAddress upstreamAddress;
  int32_t upstreamRssi;
  uint8_t apClientCount;
};

class CamperNetwork {
 public:
  bool begin(const char* apSsid, const char* apPassword, uint32_t nowMs);
  void poll(uint32_t nowMs);
  bool connect(const NetworkProfile& profile, uint32_t nowMs);
  bool startScan();
  bool scanComplete() const;
  size_t scanResults(ScanResult* output, size_t capacity);
  CamperNetworkStatus status() const;
  bool pendingProfileConnected() const;
  void acceptPendingProfile();
  void cancelPendingProfile();
  void disconnectUpstream();

 private:
  static void validationWorker(void* context);
  void beginSelectedProfile();
  void startValidation(uint32_t nowMs);
  void clearSelectedProfile();
  bool stationReady() const;

  bool beginAttempted_ = false;
  bool apReady_ = false;
  bool selectedProfile_ = false;
  bool pendingProfile_ = false;
  mutable bool scanActive_ = false;
  bool retryScheduled_ = false;
  bool validationScheduled_ = false;
  bool validationWorkerActive_ = false;
  bool validationResultCurrent_ = false;
  bool stationLifecycleActive_ = false;
  WanPhase wanPhase_ = WanPhase::Offline;
  char selectedSsid_[33] = {};
  char selectedPassphrase_[64] = {};
  RetryBackoff retryBackoff_{5000, 60000};
  uint32_t retryDeadlineMs_ = 0;
  uint32_t validationDeadlineMs_ = 0;
  QueueHandle_t validationQueue_ = nullptr;
};
