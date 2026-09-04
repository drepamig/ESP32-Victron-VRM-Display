#include "Ipv4Bridge.h"
#include "WiFi.h"
#include <map>

namespace {
struct FakeState { BridgeNetworkSnapshot snapshot; uint32_t station = 0; };
std::map<const Ipv4Bridge*, FakeState> states;
}
bool Ipv4Bridge::begin() {
  WiFi.events.push_back("bridge");
  auto& state = states[this]; state = {};
  state.snapshot.ready = true; state.snapshot.generation = 1;
  return true;
}
void Ipv4Bridge::poll(bool stationReady, uint32_t) {
  auto& state = states[this];
  const uint32_t address = stationReady ? static_cast<uint32_t>(WiFi.localIP()) : 0;
  if (state.snapshot.bridged != stationReady || state.station != address) ++state.snapshot.generation;
  state.snapshot.bridged = stationReady; state.station = address;
}
BridgeNetworkSnapshot Ipv4Bridge::snapshot(uint32_t) const { return states[this].snapshot; }
