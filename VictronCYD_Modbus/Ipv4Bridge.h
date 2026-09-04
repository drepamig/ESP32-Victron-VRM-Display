#pragma once

#include "Ipv4BridgeCore.h"

struct BridgeNetworkSnapshot {
  BridgeClientAddress clients[Ipv4BridgeCore::kMaxClients]{};
  size_t count = 0;
  uint32_t generation = 0;
  bool bridged = false;
  bool ready = false;
};

// One runtime for the sketch's AP/STA pair. Call from the application task;
// packet callbacks and snapshots are serialized through the TCP/IP context.
class Ipv4Bridge {
 public:
  bool begin();
  void poll(bool stationReady, uint32_t nowMs);
  BridgeNetworkSnapshot snapshot(uint32_t nowMs) const;
};
