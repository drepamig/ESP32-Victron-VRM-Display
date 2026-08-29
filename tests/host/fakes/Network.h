#pragma once

#include <string>

#include "IPAddress.h"

class FakeNetworkManager {
 public:
  void reset() { hostCalls = 0; lastHost.clear(); result = true; }

  int hostByName(const char* host, IPAddress& output) {
    ++hostCalls;
    lastHost = host == nullptr ? "" : host;
    output = result ? IPAddress(1, 1, 1, 1) : IPAddress();
    return result ? 1 : 0;
  }

  int hostCalls = 0;
  std::string lastHost;
  bool result = true;
};

inline FakeNetworkManager Network;
