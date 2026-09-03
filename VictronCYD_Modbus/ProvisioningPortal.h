#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include <cstdint>

#include "CredentialSubmission.h"

class ProvisioningPortal {
 public:
  ProvisioningPortal();

  bool begin(const String& selectedSsid, uint8_t securityType, uint32_t nowMs);
  void poll(uint32_t nowMs);
  void cancel();
  bool active() const;
  String pairingCode() const;
  uint32_t expiresAtMs() const;
  bool takeSubmission(CredentialSubmission& out);

 private:
  static constexpr uint32_t kLifetimeMs = 600000;

  void handleGet();
  void handlePost();
  void handleNotFound();
  bool requestAllowed();
  void stopActiveSession();
  void clearActiveMaterial();
  void clearPendingSubmission();

  WebServer server_{80};
  bool handlersRegistered_ = false;
  bool active_ = false;
  bool pendingReady_ = false;
  uint8_t selectedSecurityType_ = 0;
  uint8_t pendingSecurityType_ = 0;
  uint32_t expiresAtMs_ = 0;
  char selectedSsid_[33] = {};
  char pairingCode_[7] = {};
  char pendingSsid_[33] = {};
  char pendingPassphrase_[64] = {};
};
