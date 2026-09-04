#pragma once
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <vector>
#include "Arduino.h"
using err_t = int;
using esp_err_t = int;
constexpr int ERR_OK = 0, ERR_MEM = -1, ERR_IF = -2, ERR_VAL = -3;
constexpr int ESP_OK = 0, ESP_FAIL = -1;
constexpr int ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED = 10;
constexpr int ESP_ERR_ESP_NETIF_DHCP_ALREADY_STARTED = 11;
constexpr int ESP_NETIF_OP_SET = 1, ESP_NETIF_IP_ADDRESS_LEASE_TIME = 51;
constexpr int PBUF_RAW = 0, PBUF_RAM = 0;
struct ip4_addr_t { uint32_t addr = 0; };
using esp_ip4_addr_t = ip4_addr_t;
struct pbuf { pbuf* next = nullptr; void* payload = nullptr; uint16_t len = 0, tot_len = 0; };
struct netif;
using netif_input_fn = err_t (*)(pbuf*, netif*);
using netif_output_fn = err_t (*)(netif*, pbuf*, const ip4_addr_t*);
using netif_linkoutput_fn = err_t (*)(netif*, pbuf*);
struct netif {
  netif_input_fn input = nullptr;
  netif_output_fn output = nullptr;
  netif_linkoutput_fn linkoutput = nullptr;
  uint8_t hwaddr[6]{};
};
struct esp_netif_t { netif* impl; };
struct esp_netif_ip_info_t { esp_ip4_addr_t ip, netmask, gw; };
struct esp_netif_pair_mac_ip_t { uint8_t mac[6]{}; esp_ip4_addr_t ip; };
struct wifi_sta_info_t { uint8_t mac[6]{}; };
struct wifi_sta_list_t { wifi_sta_info_t sta[10]{}; int num = 0; };
using esp_netif_callback_fn = esp_err_t (*)(void*);
using tcpip_callback_fn = void (*)(void*);
inline uint32_t lwip_ntohl(uint32_t value) { return __builtin_bswap32(value); }
inline uint32_t lwip_htonl(uint32_t value) { return __builtin_bswap32(value); }
namespace FakeBridgeSdk {
inline std::vector<std::string> events;
inline std::string failOperation;
inline std::string failOnceOperation;
inline bool failBridgeIp = false, dhcpRunning = false;
inline bool queueFails = false, allocationFails = false, inputFails = false, stationListFails = false;
inline bool ipInfoFails = false;
inline int coreDepth = 0, livePbufs = 0, localAp = 0, localSta = 0, originalOutputs = 0;
inline std::vector<std::vector<uint8_t>> apFrames, staFrames;
inline std::deque<std::pair<tcpip_callback_fn, void*>> callbacks;
inline wifi_sta_list_t associated;
inline esp_netif_ip_info_t staInfo, apInfo;
inline std::vector<esp_netif_pair_mac_ip_t> leases;
inline uint32_t leaseMinutes = 0;
inline netif ap, sta;
inline esp_netif_t apHandle{&ap}, staHandle{&sta};
inline int operation(const char* name) {
  assert(coreDepth > 0); events.emplace_back(name);
  if (failOnceOperation == name) { failOnceOperation.clear(); return ESP_FAIL; }
  return failOperation == name ? ESP_FAIL : ESP_OK;
}
inline void drain() {
  while (!callbacks.empty()) {
    auto job = callbacks.front(); callbacks.pop_front();
    ++coreDepth; job.first(job.second); --coreDepth;
  }
}
}
inline pbuf* pbuf_alloc(int, uint16_t length, int) {
  if (FakeBridgeSdk::allocationFails) return nullptr;
  auto* p = new pbuf;
  p->payload = std::malloc(length); p->len = p->tot_len = length;
  ++FakeBridgeSdk::livePbufs; return p;
}
inline uint8_t pbuf_free(pbuf* p) {
  uint8_t count = 0;
  while (p) { auto* next = p->next; std::free(p->payload); delete p; --FakeBridgeSdk::livePbufs; ++count; p = next; }
  return count;
}
inline uint16_t pbuf_copy_partial(const pbuf* p, void* output, uint16_t length, uint16_t offset) {
  uint16_t copied = 0;
  auto* bytes = static_cast<uint8_t*>(output);
  while (p && copied < length) {
    if (offset >= p->len) { offset -= p->len; p = p->next; continue; }
    const auto take = std::min<uint16_t>(p->len - offset, length - copied);
    std::memcpy(bytes + copied, static_cast<const uint8_t*>(p->payload) + offset, take);
    copied += take; offset = 0; p = p->next;
  }
  return copied;
}
inline err_t pbuf_take(pbuf* p, const void* data, uint16_t length) {
  if (p->len < length) return ERR_VAL;
  std::memcpy(p->payload, data, length); return ERR_OK;
}
inline err_t tcpip_try_callback(tcpip_callback_fn fn, void* ctx) {
  if (FakeBridgeSdk::queueFails) return ERR_MEM;
  FakeBridgeSdk::callbacks.emplace_back(fn, ctx); return ERR_OK;
}
inline esp_err_t esp_netif_tcpip_exec(esp_netif_callback_fn fn, void* ctx) {
  ++FakeBridgeSdk::coreDepth; const auto result = fn(ctx); --FakeBridgeSdk::coreDepth; return result;
}
inline esp_netif_t* esp_netif_get_handle_from_ifkey(const char* key) {
  return std::strcmp(key, "WIFI_AP_DEF") == 0 ? &FakeBridgeSdk::apHandle : &FakeBridgeSdk::staHandle;
}
inline void* esp_netif_get_netif_impl(esp_netif_t* handle) { return handle->impl; }
inline esp_err_t esp_netif_get_ip_info(esp_netif_t* handle, esp_netif_ip_info_t* info) {
  if (FakeBridgeSdk::ipInfoFails) return ESP_FAIL;
  *info = handle == &FakeBridgeSdk::apHandle ? FakeBridgeSdk::apInfo : FakeBridgeSdk::staInfo; return ESP_OK;
}
inline esp_err_t esp_netif_dhcps_stop(esp_netif_t*) {
  const auto result = FakeBridgeSdk::operation("stop");
  if (!result) FakeBridgeSdk::dhcpRunning = false;
  return result;
}
inline esp_err_t esp_netif_dhcps_start(esp_netif_t*) {
  const auto result = FakeBridgeSdk::operation("start");
  if (!result) FakeBridgeSdk::dhcpRunning = true;
  return result;
}
inline esp_err_t esp_netif_set_ip_info(esp_netif_t*, const esp_netif_ip_info_t* info) {
  const auto result = FakeBridgeSdk::operation("ip");
  if (FakeBridgeSdk::failBridgeIp && !info->ip.addr) return ESP_FAIL;
  if (!result) FakeBridgeSdk::apInfo = *info;
  return result;
}
inline esp_err_t esp_netif_dhcps_option(esp_netif_t*, int, int, void* value, uint32_t) {
  const auto result = FakeBridgeSdk::operation("lease");
  if (!result) FakeBridgeSdk::leaseMinutes = *static_cast<uint32_t*>(value);
  return result;
}
inline esp_err_t esp_wifi_deauth_sta(uint16_t) { return FakeBridgeSdk::operation("deauth"); }
inline esp_err_t esp_wifi_ap_get_sta_list(wifi_sta_list_t* list) {
  assert(FakeBridgeSdk::coreDepth > 0);
  if (FakeBridgeSdk::stationListFails) return ESP_FAIL;
  *list = FakeBridgeSdk::associated; return ESP_OK;
}
inline esp_err_t esp_netif_dhcps_get_clients_by_mac(esp_netif_t*, int count, esp_netif_pair_mac_ip_t* pairs) {
  for (int i = 0; i < count; ++i) for (const auto& lease : FakeBridgeSdk::leases)
    if (std::memcmp(pairs[i].mac, lease.mac, 6) == 0) pairs[i].ip = lease.ip;
  return ESP_OK;
}
