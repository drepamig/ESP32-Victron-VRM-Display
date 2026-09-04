#pragma once

#include <cstddef>
#include <cstdint>

// IPv4 values use canonical network significance (192.168.1.2 = 0xc0a80102),
// independently of the CPU's native byte order. Access only on the TCP/IP thread.
enum class BridgeSide : uint8_t { Ap, Sta };
struct BridgeDecision { bool local = false; bool sendAp = false; bool sendSta = false; };
struct BridgeClientAddress {
  uint8_t mac[6]{};
  uint32_t address = 0;
};

class Ipv4BridgeCore {
 public:
  static constexpr size_t kMaxClients = 4;
  static constexpr size_t kMaxFrame = 1518;
  void configure(const uint8_t staMac[6], const uint8_t apMac[6], uint32_t address,
                 uint32_t netmask);
  bool setAssociated(const uint8_t mac[6], bool associated);
  // Takes a private mutable copy of a received Ethernet frame. The caller keeps
  // the original frame for local delivery when both local and forwarding apply.
  BridgeDecision process(BridgeSide side, uint8_t* frame, size_t length, uint32_t nowMs);
  bool macForAddress(uint32_t address, uint8_t mac[6], uint32_t nowMs) const;
  size_t clients(BridgeClientAddress* out, size_t capacity, uint32_t nowMs) const;
  // Local DHCP/ARP observations while fallback is active use the same registry.
  void observe(const uint8_t mac[6], uint32_t address, uint32_t nowMs);

 private:
  struct Client {
    uint8_t mac[6]{};
    bool associated = false;
    uint32_t address = 0;
    uint32_t expires = 0;
    bool leased = false;
    bool pendingDhcp = false;
    uint32_t xid = 0;
    uint32_t xidExpires = 0;
  };
  Client* find(const uint8_t mac[6]);
  const Client* findIp(uint32_t address, uint32_t nowMs) const;
  bool validAddress(uint32_t address) const;
  void learn(Client& client, uint32_t address, uint32_t nowMs, uint32_t ttlMs,
             bool leased = false);
  BridgeDecision processArp(BridgeSide side, uint8_t* frame, size_t length, uint32_t nowMs);
  BridgeDecision processIp(BridgeSide side, uint8_t* frame, size_t length, uint32_t nowMs);
  Client clients_[kMaxClients]{};
  uint8_t staMac_[6]{}, apMac_[6]{};
  uint32_t address_ = 0, netmask_ = 0;
};
