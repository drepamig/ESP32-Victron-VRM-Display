#include "Ipv4Bridge.h"

#include <algorithm>
#include <atomic>
#include <cstring>

#ifdef CYD_BRIDGE_HOST_TEST
#include "Ipv4BridgeSdkFake.h"
#else
#include <Arduino.h>
#include <esp_netif.h>
#include <esp_netif_net_stack.h>
#include <esp_wifi.h>
#include <lwip/def.h>
#include <lwip/netif.h>
#include <lwip/pbuf.h>
#include <lwip/tcpip.h>
#endif

namespace {
constexpr uint32_t kFallbackAddress = 0xc0a83201;
constexpr uint32_t kFallbackMask = 0xffffff00;
constexpr size_t kRxSlots = 8;
constexpr uint32_t kBridgeRetryMs = 5000;
constexpr uint32_t kFallbackRetryMs = 1000;

struct Domain {
  bool bridged = false;
  uint32_t address = kFallbackAddress, mask = kFallbackMask, gateway = kFallbackAddress;
  bool operator==(const Domain& other) const {
    return bridged == other.bridged && address == other.address &&
           mask == other.mask && gateway == other.gateway;
  }
};
struct Receive {
  std::atomic<bool> used{false};
  pbuf* packet = nullptr;
  netif* interface = nullptr;
  uint32_t generation = 0;
};
struct Runtime {
  Ipv4BridgeCore core;
  esp_netif_t* apHandle = nullptr;
  esp_netif_t* staHandle = nullptr;
  netif* ap = nullptr;
  netif* sta = nullptr;
  netif_input_fn apInput = nullptr, staInput = nullptr;
  netif_output_fn staOutput = nullptr;
  netif_linkoutput_fn apTx = nullptr, staTx = nullptr;
  std::atomic<bool> ready{false};
  std::atomic<bool> forwarding{false};
  std::atomic<uint32_t> generation{0};
  bool installed = false, initialized = false, changing = false;
  bool deauthRequired = false;
  bool bridgeRetryScheduled = false, recoveringFallback = false, fallbackRetryScheduled = false;
  uint32_t bridgeRetryAt = 0, fallbackRetryAt = 0;
  uint8_t stage = 0;
  Domain target;
  uint8_t associated[Ipv4BridgeCore::kMaxClients][6]{};
  size_t associatedCount = 0;
  Receive receive[kRxSlots];
} state;

uint32_t readAddress(const uint8_t* bytes) {
  return (uint32_t(bytes[0]) << 24) | (uint32_t(bytes[1]) << 16) |
         (uint32_t(bytes[2]) << 8) | bytes[3];
}

bool refreshClients(uint32_t nowMs) {
  wifi_sta_list_t list{};
  if (esp_wifi_ap_get_sta_list(&list) != ESP_OK) return false;
  const size_t count = std::min<size_t>(std::max<int>(list.num, 0), Ipv4BridgeCore::kMaxClients);
  for (size_t i = 0; i < state.associatedCount; ++i) {
    bool found = false;
    for (size_t j = 0; j < count; ++j)
      if (std::memcmp(state.associated[i], list.sta[j].mac, 6) == 0) found = true;
    if (!found) state.core.setAssociated(state.associated[i], false);
  }
  for (size_t i = 0; i < count; ++i) {
    state.core.setAssociated(list.sta[i].mac, true);
    std::memcpy(state.associated[i], list.sta[i].mac, 6);
  }
  state.associatedCount = count;
  if (!state.target.bridged && count) {
    esp_netif_pair_mac_ip_t pairs[Ipv4BridgeCore::kMaxClients]{};
    for (size_t i = 0; i < count; ++i) std::memcpy(pairs[i].mac, state.associated[i], 6);
    if (esp_netif_dhcps_get_clients_by_mac(state.apHandle, static_cast<int>(count), pairs) == ESP_OK)
      for (size_t i = 0; i < count; ++i)
        if (pairs[i].ip.addr) state.core.observe(pairs[i].mac, lwip_ntohl(pairs[i].ip.addr), nowMs);
  }
  return true;
}

void localInput(pbuf* packet, netif* interface) {
  const auto input = interface == state.ap ? state.apInput : state.staInput;
  if (input(packet, interface) != ERR_OK) pbuf_free(packet);
}

void processReceive(void* context) {
  auto* receive = static_cast<Receive*>(context);
  pbuf* original = receive->packet;
  netif* interface = receive->interface;
  const uint32_t generation = receive->generation;
  receive->used.store(false, std::memory_order_release);
  if (generation != state.generation.load() || !state.ready.load()) {
    pbuf_free(original);
    return;
  }
  const uint32_t nowMs = millis();
  if (!refreshClients(nowMs)) { pbuf_free(original); return; }
  auto* copy = pbuf_alloc(PBUF_RAW, original->tot_len, PBUF_RAM);
  if (!copy) { pbuf_free(original); return; }
  if (pbuf_copy_partial(original, copy->payload, original->tot_len, 0) != original->tot_len) {
    pbuf_free(copy); pbuf_free(original); return;
  }
  auto* frame = static_cast<uint8_t*>(copy->payload);
  const bool fromAp = interface == state.ap;
  const auto decision = state.core.process(fromAp ? BridgeSide::Ap : BridgeSide::Sta,
                                            frame, copy->tot_len, nowMs);
  if (!state.target.bridged) {
    // Core validates/observes AP frames, but fallback traffic stays in lwIP.
    pbuf_free(copy);
    localInput(original, interface);
    return;
  }
  if (decision.sendAp) state.apTx(state.ap, copy);
  if (decision.sendSta) state.staTx(state.sta, copy);
  if (decision.local && fromAp && copy->tot_len >= 34 && frame[12] == 8 && frame[13] == 0 &&
      readAddress(frame + 30) == state.target.address) {
    // The AP has no IPv4 address in bridge mode. Deliver management traffic
    // using STA identity; do not recurse into the installed STA receive hook.
    std::memcpy(frame, state.sta->hwaddr, 6);
    pbuf_free(original);
    localInput(copy, state.sta);
    return;
  }
  pbuf_free(copy);
  if (decision.local) localInput(original, interface);
  else pbuf_free(original);
}

err_t receiveInput(pbuf* packet, netif* interface) {
  const uint32_t generation = state.generation.load();
  // STA DHCP must continue while no bridge is active or an AP transition fails.
  if (!state.ready.load() || !state.forwarding.load()) {
    if (interface == state.sta) return state.staInput(packet, interface);
    if (!state.ready.load()) return ERR_IF;
  }
  if (!packet || packet->tot_len < 14 || packet->tot_len > Ipv4BridgeCore::kMaxFrame) return ERR_VAL;
  for (auto& receive : state.receive) {
    bool available = false;
    if (!receive.used.compare_exchange_strong(available, true, std::memory_order_acquire)) continue;
    receive.packet = packet;
    receive.interface = interface;
    receive.generation = generation;
    const err_t result = tcpip_try_callback(processReceive, &receive);
    if (result != ERR_OK) receive.used.store(false, std::memory_order_release);
    return result;  // Error: caller still owns packet. Success: callback owns it.
  }
  return ERR_MEM;
}

err_t stationOutput(netif* interface, pbuf* packet, const ip4_addr_t* address) {
  uint8_t mac[6]{};
  if (state.ready.load() && state.target.bridged && address && !refreshClients(millis())) return ERR_IF;
  if (state.ready.load() && state.target.bridged && address &&
      state.core.macForAddress(lwip_ntohl(address->addr), mac, millis())) {
    if (packet->tot_len > Ipv4BridgeCore::kMaxFrame - 14) return ERR_VAL;
    auto* frame = pbuf_alloc(PBUF_RAW, packet->tot_len + 14, PBUF_RAM);
    if (!frame) return ERR_MEM;
    auto* bytes = static_cast<uint8_t*>(frame->payload);
    std::memcpy(bytes, mac, 6); std::memcpy(bytes + 6, state.ap->hwaddr, 6);
    bytes[12] = 8; bytes[13] = 0;
    err_t result = ERR_VAL;
    if (pbuf_copy_partial(packet, bytes + 14, packet->tot_len, 0) == packet->tot_len)
      result = state.apTx(state.ap, frame);
    pbuf_free(frame);
    return result;
  }
  return state.staOutput(interface, packet, address);
}

void startTransition(const Domain& target) {
  const bool wasInitialized = state.initialized;
  state.ready.store(false);
  state.forwarding.store(false);
  state.generation.fetch_add(1);
  state.target = target;
  state.initialized = true;
  state.changing = true;
  state.stage = 0;
  state.deauthRequired = wasInitialized;
  state.core.configure(state.sta->hwaddr, state.ap->hwaddr, target.address, target.mask);
  state.associatedCount = 0;
}

bool advanceTransition() {
  while (state.changing) {
    esp_err_t result = ESP_OK;
    switch (state.stage) {
      case 0:
        result = esp_netif_dhcps_stop(state.apHandle);
        if (result == ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) result = ESP_OK;
        break;
      case 1: {
        esp_netif_ip_info_t info{};
        if (!state.target.bridged) {
          info.ip.addr = lwip_htonl(kFallbackAddress); info.netmask.addr = lwip_htonl(kFallbackMask);
          info.gw.addr = lwip_htonl(kFallbackAddress);
        }
        result = esp_netif_set_ip_info(state.apHandle, &info);
        break;
      }
      case 2:
        if (!state.target.bridged) {
          // ESP-IDF's DHCP server option is in minutes, not wire-format seconds.
          uint32_t leaseMinutes = 1;
          result = esp_netif_dhcps_option(state.apHandle, ESP_NETIF_OP_SET,
                                         ESP_NETIF_IP_ADDRESS_LEASE_TIME, &leaseMinutes, sizeof(leaseMinutes));
        }
        break;
      case 3:
        if (!state.target.bridged) {
          result = esp_netif_dhcps_start(state.apHandle);
          if (result == ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED) result = ESP_OK;
        }
        break;
      case 4:
        if (state.deauthRequired) result = esp_wifi_deauth_sta(0);
        break;
      default:
        state.changing = false;
        state.forwarding.store(state.target.bridged);
        state.ready.store(true);
        return true;
    }
    if (result != ESP_OK) return false;
    ++state.stage;
  }
  return state.initialized;
}

esp_err_t install(void*) {
  if (state.installed) return ESP_OK;
  state.apHandle = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
  state.staHandle = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  if (!state.apHandle || !state.staHandle) return ESP_FAIL;
  state.ap = static_cast<netif*>(esp_netif_get_netif_impl(state.apHandle));
  state.sta = static_cast<netif*>(esp_netif_get_netif_impl(state.staHandle));
  if (!state.ap || !state.sta || !state.ap->input || !state.sta->input ||
      !state.ap->linkoutput || !state.sta->linkoutput || !state.sta->output) return ESP_FAIL;
  state.apInput = state.ap->input; state.staInput = state.sta->input;
  state.apTx = state.ap->linkoutput; state.staTx = state.sta->linkoutput; state.staOutput = state.sta->output;
  state.ap->input = receiveInput; state.sta->input = receiveInput; state.sta->output = stationOutput;
  state.installed = true;
  return ESP_OK;
}

struct Poll { bool stationReady; uint32_t nowMs; };
esp_err_t recoverFallback(const Poll& request) {
  if (state.fallbackRetryScheduled && static_cast<int32_t>(request.nowMs - state.fallbackRetryAt) < 0)
    return ESP_FAIL;
  if (!advanceTransition()) {
    state.fallbackRetryScheduled = true;
    state.fallbackRetryAt = request.nowMs + kFallbackRetryMs;
    return ESP_FAIL;
  }
  state.recoveringFallback = false;
  state.fallbackRetryScheduled = false;
  state.bridgeRetryScheduled = request.stationReady;
  state.bridgeRetryAt = request.nowMs + kBridgeRetryMs;
  refreshClients(request.nowMs);
  return ESP_OK;
}

esp_err_t restoreFallback(const Poll& request) {
  // A failed bridge attempt may have already stopped local DHCP or cleared AP
  // IPv4. Restore that usable domain before considering another bridge attempt.
  startTransition(Domain{});
  state.recoveringFallback = true;
  state.fallbackRetryScheduled = false;
  return recoverFallback(request);
}

esp_err_t pollRuntime(void* context) {
  if (!state.installed) return ESP_FAIL;
  const auto& request = *static_cast<Poll*>(context);
  if (!request.stationReady) state.bridgeRetryScheduled = false;
  if (state.recoveringFallback) return recoverFallback(request);
  if (state.bridgeRetryScheduled && static_cast<int32_t>(request.nowMs - state.bridgeRetryAt) < 0) {
    refreshClients(request.nowMs);
    return ESP_OK;
  }
  state.bridgeRetryScheduled = false;
  Domain desired;
  if (request.stationReady) {
    esp_netif_ip_info_t info{};
    if (esp_netif_get_ip_info(state.staHandle, &info) != ESP_OK) {
      // A read failure does not establish radio loss or a new address domain.
      // Keep the working bridge/fallback; never disable AP RX on this error.
      if (state.ready.load()) { refreshClients(request.nowMs); return ESP_OK; }
      return restoreFallback(request);
    }
    if (info.ip.addr && info.netmask.addr) {
      desired = {true, lwip_ntohl(info.ip.addr), lwip_ntohl(info.netmask.addr), lwip_ntohl(info.gw.addr)};
    }
  }
  if (!state.initialized || !(desired == state.target)) startTransition(desired);
  if (!advanceTransition()) return state.target.bridged ? restoreFallback(request) : ESP_FAIL;
  state.forwarding.store(state.target.bridged);
  state.ready.store(true);
  refreshClients(request.nowMs);
  return ESP_OK;
}

struct SnapshotRequest { uint32_t nowMs; BridgeNetworkSnapshot snapshot; };
esp_err_t copySnapshot(void* context) {
  auto& request = *static_cast<SnapshotRequest*>(context);
  auto& snapshot = request.snapshot;
  snapshot.generation = state.generation.load();
  snapshot.ready = state.ready.load();
  snapshot.bridged = state.target.bridged && snapshot.ready;
  if (snapshot.ready && refreshClients(request.nowMs))
    snapshot.count = state.core.clients(snapshot.clients, Ipv4BridgeCore::kMaxClients, request.nowMs);
  return ESP_OK;
}
}  // namespace

bool Ipv4Bridge::begin() {
  return esp_netif_tcpip_exec([](void*) -> esp_err_t {
    if (install(nullptr) != ESP_OK) return ESP_FAIL;
    if (!state.initialized) startTransition(Domain{});
    return advanceTransition() ? ESP_OK : ESP_FAIL;
  }, nullptr) == ESP_OK;
}
void Ipv4Bridge::poll(bool stationReady, uint32_t nowMs) {
  Poll request{stationReady, nowMs};
  if (esp_netif_tcpip_exec(pollRuntime, &request) != ESP_OK) {
    state.ready.store(false);
    state.forwarding.store(false);
  }
}
BridgeNetworkSnapshot Ipv4Bridge::snapshot(uint32_t nowMs) const {
  SnapshotRequest request{nowMs, {}};
  if (esp_netif_tcpip_exec(copySnapshot, &request) != ESP_OK) return {};
  return request.snapshot;
}
