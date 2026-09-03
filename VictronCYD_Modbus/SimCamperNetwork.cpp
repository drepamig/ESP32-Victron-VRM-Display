#include "SimCamperNetwork.h"

#ifdef CYD_SIMULATION

#include <cstring>

bool SimCamperNetwork::begin(const char*, const char* apPassword, uint32_t) {
  if (beginAttempted_) {
    return apReady_;
  }
  if (apPassword == nullptr) {
    return false;
  }
  const size_t length = std::strlen(apPassword);
  if (length < 12 || length > 63) {
    return false;
  }
  beginAttempted_ = true;
  apReady_ = true;
  apClients_ = 1;
  return true;
}

void SimCamperNetwork::poll(uint32_t nowMs) {
  if (!connectionPending_ || static_cast<int32_t>(nowMs - connectionReadyAtMs_) < 0) {
    return;
  }
  connectionPending_ = false;
  if (connectFixture_ == ConnectFixture::Success) {
    wanPhase_ = WanPhase::Online;
    pendingConnected_ = true;
    upstreamAddress_ = IPAddress(192, 0, 2, 25);
    upstreamRssi_ = -48;
  } else {
    wanPhase_ = WanPhase::Offline;
    pendingConnected_ = false;
    upstreamAddress_ = IPAddress();
    upstreamRssi_ = 0;
  }
}

bool SimCamperNetwork::connect(const NetworkProfile& profile, uint32_t nowMs) {
  if (!apReady_ || profile.ssid.isEmpty() || profile.ssid.length() > 32 ||
      profile.passphrase.length() > 63) {
    return false;
  }
  wanPhase_ = WanPhase::Connecting;
  connectionPending_ = true;
  connectionReadyAtMs_ = nowMs + 1000;
  pendingConnected_ = false;
  upstreamAddress_ = IPAddress();
  upstreamRssi_ = 0;
  return true;
}

bool SimCamperNetwork::startScan() {
  if (scanPhase_ == ScanPhase::Running) {
    return false;
  }
  scanPhase_ = ScanPhase::Running;
  return true;
}

bool SimCamperNetwork::scanComplete() const {
  if (scanPhase_ == ScanPhase::Complete) {
    return true;
  }
  if (scanPhase_ != ScanPhase::Running) {
    return false;
  }
  scanPhase_ = scanFixture_ == ScanFixture::Failure ? ScanPhase::Failed
                                                    : ScanPhase::Complete;
  return scanPhase_ == ScanPhase::Complete;
}

ScanPhase SimCamperNetwork::scanPhase() const {
  return scanPhase_;
}

void SimCamperNetwork::clearScanFailure() {
  if (scanPhase_ == ScanPhase::Failed) {
    scanPhase_ = ScanPhase::Idle;
  }
}

size_t SimCamperNetwork::scanResults(ScanResult* output, size_t capacity) {
  if (scanPhase_ != ScanPhase::Complete) {
    return 0;
  }
  scanPhase_ = ScanPhase::Idle;
  if (scanFixture_ == ScanFixture::Empty || output == nullptr || capacity == 0) {
    return 0;
  }
  const ScanResult fixture[] = {
      {"Bench-Protected", -42, 3, 6},
      {"Bench-Open", -55, 0, 1},
      {"Bench-Weak", -78, 3, 11},
  };
  const size_t available = sizeof(fixture) / sizeof(fixture[0]);
  const size_t copied = capacity < available ? capacity : available;
  for (size_t index = 0; index < copied; ++index) {
    output[index] = fixture[index];
  }
  return copied;
}

CamperNetworkStatus SimCamperNetwork::status() const {
  return {apReady_, wanPhase_, upstreamAddress_, upstreamRssi_,
          static_cast<uint8_t>(apReady_ ? apClients_ : 0)};
}

bool SimCamperNetwork::pendingProfileConnected() const {
  return pendingConnected_;
}

void SimCamperNetwork::acceptPendingProfile() {
  pendingConnected_ = false;
}

void SimCamperNetwork::cancelPendingProfile(bool) {
  connectionPending_ = false;
  connectionReadyAtMs_ = 0;
  pendingConnected_ = false;
  wanPhase_ = WanPhase::Offline;
  upstreamAddress_ = IPAddress();
  upstreamRssi_ = 0;
}

void SimCamperNetwork::disconnectUpstream() {
  cancelPendingProfile();
}

bool SimCamperNetwork::setScanFixture(const char* fixture) {
  if (fixture == nullptr) {
    return false;
  }
  if (std::strcmp(fixture, "nominal") == 0) {
    scanFixture_ = ScanFixture::Nominal;
  } else if (std::strcmp(fixture, "empty") == 0) {
    scanFixture_ = ScanFixture::Empty;
  } else if (std::strcmp(fixture, "failure") == 0) {
    scanFixture_ = ScanFixture::Failure;
  } else {
    return false;
  }
  scanPhase_ = ScanPhase::Idle;
  return true;
}

bool SimCamperNetwork::setConnectFixture(const char* fixture) {
  if (fixture == nullptr) {
    return false;
  }
  if (std::strcmp(fixture, "success") == 0) {
    connectFixture_ = ConnectFixture::Success;
  } else if (std::strcmp(fixture, "failure") == 0) {
    connectFixture_ = ConnectFixture::Failure;
  } else {
    return false;
  }
  return true;
}

void SimCamperNetwork::setApFixture(bool ready, uint8_t clients) {
  apReady_ = ready;
  apClients_ = ready ? clients : 0;
}

void SimCamperNetwork::resetFixtures() {
  beginAttempted_ = true;
  apReady_ = true;
  apClients_ = 1;
  scanPhase_ = ScanPhase::Idle;
  scanFixture_ = ScanFixture::Nominal;
  connectFixture_ = ConnectFixture::Success;
  wanPhase_ = WanPhase::Offline;
  connectionPending_ = false;
  connectionReadyAtMs_ = 0;
  pendingConnected_ = false;
  upstreamAddress_ = IPAddress();
  upstreamRssi_ = 0;
}

#endif
