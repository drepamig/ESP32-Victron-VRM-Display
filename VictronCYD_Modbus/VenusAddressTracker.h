#pragma once
#include "Ipv4BridgeCore.h"
#include "VenusConnectionStatus.h"

// Copied into the worker queue; the worker never reads the mutable tracker.
struct VenusProbe {
  uint32_t address = 0;
  uint32_t token = 0;
  uint8_t mac[6]{};
};

class VenusAddressTracker {
 public:
  void begin();
  void update(const BridgeClientAddress* clients, size_t count, uint32_t generation,
              uint32_t nowMs);
  VenusProbe request() const;
  // Returns true only for a successful response belonging to the current target.
  bool recordResult(const VenusProbe& probe, bool validSystemRead, uint32_t nowMs);
  VenusConnectionStatus status(uint32_t nowMs) const;
  bool persistIdentity(uint32_t nowMs);
 private:
  void select(size_t index);
  bool isBoundMac(const uint8_t mac[6]) const;
  BridgeClientAddress clients_[Ipv4BridgeCore::kMaxClients]{};
  size_t count_ = 0;
  uint32_t generation_ = 0;
  VenusProbe target_{};
  uint8_t boundMac_[6]{};
  bool bound_ = false, identityDirty_ = false;
  bool saveAttempted_ = false, lastReadValid_ = false;
  uint32_t lastSaveAttemptMs_ = 0, lastSuccessMs_ = 0;
  char lastAddress_[16]{};
};
