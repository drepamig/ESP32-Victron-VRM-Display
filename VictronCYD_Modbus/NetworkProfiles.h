#pragma once

#include <Arduino.h>

struct NetworkProfile {
  String ssid;
  String passphrase;
  uint8_t securityType = 0;
  uint32_t lastSuccessEpoch = 0;
};

class NetworkProfileStore {
 public:
  static constexpr size_t kMaxProfiles = 5;

  bool begin();
  size_t count() const;
  int activeIndex() const;
  bool load(size_t index, NetworkProfile& out) const;
  bool activate(size_t index);
  bool upsert(const NetworkProfile& profile, size_t& storedIndex);
  bool erase(size_t index);
  bool clearUpstreamProfiles();
};
