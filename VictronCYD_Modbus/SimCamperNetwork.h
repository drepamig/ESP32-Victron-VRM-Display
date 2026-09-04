#pragma once

#ifdef CYD_SIMULATION

#include "CamperNetwork.h"

class SimCamperNetwork {
 public:
  SimCamperNetwork() = default;
  SimCamperNetwork(const SimCamperNetwork&) = delete;
  SimCamperNetwork& operator=(const SimCamperNetwork&) = delete;

  bool begin(const char* apSsid, const char* apPassword, uint32_t nowMs);
  void poll(uint32_t nowMs);
  bool connect(const NetworkProfile& profile, uint32_t nowMs);
  bool startScan();
  bool scanComplete() const;
  ScanPhase scanPhase() const;
  void clearScanFailure();
  size_t scanResults(ScanResult* output, size_t capacity);
  CamperNetworkStatus status() const;
  bool pendingProfileConnected() const;
  void acceptPendingProfile();
  void cancelPendingProfile(bool clearTransientStationConfig = true);
  void disconnectUpstream();

  bool setScanFixture(const char* fixture);
  bool setConnectFixture(const char* fixture);
  bool setWanFixture(const char* fixture);
  void setApFixture(bool ready, uint8_t clients);
  void resetFixtures();

 private:
  enum class ScanFixture : uint8_t { Nominal, Empty, Failure };
  enum class ConnectFixture : uint8_t { Success, Failure };

  bool beginAttempted_ = false;
  bool apReady_ = true;
  uint8_t apClients_ = 1;
  mutable ScanPhase scanPhase_ = ScanPhase::Idle;
  ScanFixture scanFixture_ = ScanFixture::Nominal;
  ConnectFixture connectFixture_ = ConnectFixture::Success;
  WanPhase wanPhase_ = WanPhase::Offline;
  bool connectionPending_ = false;
  bool pendingProfile_ = false;
  bool pendingConnected_ = false;
  bool connectionEstablished_ = false;
  uint32_t connectionReadyAtMs_ = 0;
  IPAddress upstreamAddress_;
  int32_t upstreamRssi_ = 0;
};

#endif
