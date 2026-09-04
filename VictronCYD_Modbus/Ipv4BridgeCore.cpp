#include "Ipv4BridgeCore.h"
#include <algorithm>
#include <cstring>

namespace {
uint16_t read16(const uint8_t* p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }
uint32_t read32(const uint8_t* p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; }
void write16(uint8_t* p, uint16_t value) { p[0] = value >> 8; p[1] = value; }
void write32(uint8_t* p, uint32_t value) { p[0] = value >> 24; p[1] = value >> 16; p[2] = value >> 8; p[3] = value; }
bool sameMac(const uint8_t* a, const uint8_t* b) { return std::memcmp(a, b, 6) == 0; }
bool unicastMac(const uint8_t* mac) { const uint8_t zero[6]{}; return !(mac[0] & 1) && !sameMac(mac, zero); }
bool before(uint32_t now, uint32_t deadline) { return static_cast<int32_t>(now - deadline) < 0; }
constexpr uint32_t kObservedLifetimeMs = 600000;
constexpr uint32_t kTransactionLifetimeMs = 30000;

void arpReply(uint8_t* frame, const uint8_t* senderMac, uint32_t senderIp) {
  uint8_t targetMac[6]; std::memcpy(targetMac, frame + 22, 6);
  const uint32_t targetIp = read32(frame + 28);
  std::memcpy(frame, targetMac, 6); std::memcpy(frame + 6, senderMac, 6);
  write16(frame + 20, 2); std::memcpy(frame + 22, senderMac, 6); write32(frame + 28, senderIp);
  std::memcpy(frame + 32, targetMac, 6); write32(frame + 38, targetIp);
}
struct Dhcp {
  bool present = false, valid = false;
  bool reply = false;
  uint8_t* bootp = nullptr;
  uint8_t* udp = nullptr;
  uint8_t type = 0;
  uint32_t leaseSeconds = 600;
};
Dhcp parseDhcp(uint8_t* ip, size_t headerLength, size_t ipLength) {
  Dhcp d;
  if (ip[9] != 17 || (read16(ip + 6) & 0x3fff) || ipLength < headerLength + 8) return d;
  uint8_t* udp = ip + headerLength;
  const auto src = read16(udp), dst = read16(udp + 2);
  if (!((src == 68 && dst == 67) || (src == 67 && dst == 68))) return d;
  d.present = true;
  const size_t udpLength = read16(udp + 4);
  if (udpLength < 248 || udpLength > ipLength - headerLength) return d;
  uint8_t* bootp = udp + 8;
  d.reply = src == 67;
  if (bootp[0] != (d.reply ? 2 : 1) || bootp[1] != 1 || bootp[2] != 6 ||
      read32(bootp + 236) != 0x63825363) return d;
  bool ended = false, typeFound = false;
  for (size_t i = 240; i < udpLength - 8;) {
    const uint8_t option = bootp[i++];
    if (option == 0) continue;
    if (option == 255) { ended = true; break; }
    if (i >= udpLength - 8) return d;
    const size_t count = bootp[i++];
    if (count > udpLength - 8 - i) return d;
    if (option == 53) {
      if (count != 1 || typeFound) return d;
      d.type = bootp[i]; typeFound = true;
    } else if (option == 51) {
      if (count != 4) return d;
      d.leaseSeconds = read32(bootp + i);
    }
    i += count;
  }
  d.valid = ended && typeFound;
  d.bootp = bootp; d.udp = udp;
  return d;
}
}  // namespace

void Ipv4BridgeCore::configure(const uint8_t staMac[6], const uint8_t apMac[6],
                               uint32_t address, uint32_t netmask) {
  std::memcpy(staMac_, staMac, 6); std::memcpy(apMac_, apMac, 6);
  address_ = address; netmask_ = netmask;
  for (auto& client : clients_) client = {};
}

Ipv4BridgeCore::Client* Ipv4BridgeCore::find(const uint8_t mac[6]) {
  for (auto& client : clients_) if (client.associated && sameMac(client.mac, mac)) return &client;
  return nullptr;
}
bool Ipv4BridgeCore::setAssociated(const uint8_t mac[6], bool associated) {
  if (!unicastMac(mac)) return false;
  Client* client = find(mac);
  if (!associated) { if (client) *client = {}; return true; }
  if (client) return true;
  for (auto& entry : clients_) if (!entry.associated) {
    entry = {}; entry.associated = true; std::memcpy(entry.mac, mac, 6); return true;
  }
  return false;
}
bool Ipv4BridgeCore::validAddress(uint32_t address) const {
  return address && address != address_ && (address >> 24) != 127 &&
         (address >> 28) != 0xe && (address >> 28) != 0xf &&
         (address & netmask_) == (address_ & netmask_) &&
         (address & ~netmask_) != 0 && (address & ~netmask_) != ~netmask_;
}
void Ipv4BridgeCore::learn(Client& client, uint32_t address, uint32_t nowMs,
                          uint32_t ttlMs, bool leased) {
  if (!validAddress(address)) return;
  // A confirmed DHCP lease cannot be replaced by traffic claiming a new IP.
  if (!leased && client.leased && before(nowMs, client.expires)) return;
  if (!leased) {
    const auto* owner = findIp(address, nowMs);
    if (owner && owner != &client && owner->leased) return;
  }
  for (auto& entry : clients_) if (&entry != &client && entry.address == address) {
    entry.address = 0; entry.leased = false;
  }
  client.address = address; client.expires = nowMs + ttlMs; client.leased = leased;
}
void Ipv4BridgeCore::observe(const uint8_t mac[6], uint32_t address, uint32_t nowMs) {
  if (auto* client = find(mac)) learn(*client, address, nowMs, kObservedLifetimeMs);
}
const Ipv4BridgeCore::Client* Ipv4BridgeCore::findIp(uint32_t address, uint32_t nowMs) const {
  if (!address) return nullptr;
  for (const auto& entry : clients_) if (entry.associated && entry.address == address && before(nowMs, entry.expires)) return &entry;
  return nullptr;
}
bool Ipv4BridgeCore::macForAddress(uint32_t address, uint8_t mac[6], uint32_t nowMs) const {
  const auto* client = findIp(address, nowMs);
  if (!client) return false;
  std::memcpy(mac, client->mac, 6); return true;
}
size_t Ipv4BridgeCore::clients(BridgeClientAddress* out, size_t capacity, uint32_t nowMs) const {
  if (!out) return 0;
  size_t count = 0;
  for (const auto& client : clients_) if (client.associated && client.address && before(nowMs, client.expires) && count < capacity) {
    std::memcpy(out[count].mac, client.mac, 6); out[count++].address = client.address;
  }
  return count;
}

BridgeDecision Ipv4BridgeCore::process(BridgeSide side, uint8_t* frame, size_t length, uint32_t nowMs) {
  if (!frame || length < 14 || length > kMaxFrame) return {};
  if (side == BridgeSide::Ap && !find(frame + 6)) return {};
  const auto type = read16(frame + 12);
  if (type == 0x0806) return processArp(side, frame, length, nowMs);
  if (type == 0x0800) return processIp(side, frame, length, nowMs);
  // Non-IPv4 traffic is outside the repeater scope, but the ESP stack may use it.
  return {true, false, false};
}
BridgeDecision Ipv4BridgeCore::processArp(BridgeSide side, uint8_t* frame, size_t length, uint32_t nowMs) {
  if (length < 42 || read16(frame + 14) != 1 || read16(frame + 16) != 0x0800 ||
      frame[18] != 6 || frame[19] != 4) return {};
  const auto op = read16(frame + 20);
  if (op != 1 && op != 2) return {};
  const uint32_t target = read32(frame + 38);
  if (side == BridgeSide::Ap) {
    if (!sameMac(frame + 6, frame + 22)) return {};
    observe(frame + 6, read32(frame + 28), nowMs);
    const auto* destination = findIp(target, nowMs);
    // DHCP clients probe and announce their own offered IP. Answering those
    // requests would report our translated MAC as a duplicate address.
    if (op == 1 && (target == address_ ||
                   (destination && !sameMac(destination->mac, frame + 6)))) {
      arpReply(frame, apMac_, target); return {false, true, false};
    }
    std::memcpy(frame + 6, staMac_, 6); std::memcpy(frame + 22, staMac_, 6);
    return {false, false, true};
  }
  if (op == 1 && findIp(target, nowMs)) {
    arpReply(frame, staMac_, target); return {false, false, true};
  }
  if (target == address_) return {true, false, false};
  // A reply to a DAD probe targets 0.0.0.0. Its sender IP identifies the
  // offered lease being checked; preserve the real sender MAC as the conflict.
  const auto* client = findIp(op == 2 && target == 0 ? read32(frame + 28) : target, nowMs);
  if (client) {
    std::memcpy(frame, client->mac, 6); std::memcpy(frame + 32, client->mac, 6);
  } else if (!(frame[0] & 1)) return {};
  // Preserve the upstream ARP sender: clients must address subsequent packets
  // to the real upstream gateway/peer MAC, not to the ESP's AP MAC.
  std::memcpy(frame + 6, apMac_, 6);
  return {true, true, false};
}
BridgeDecision Ipv4BridgeCore::processIp(BridgeSide side, uint8_t* frame, size_t length, uint32_t nowMs) {
  if (length < 34) return {};
  uint8_t* ip = frame + 14;
  const size_t header = (ip[0] & 15) * 4, total = read16(ip + 2);
  if ((ip[0] >> 4) != 4 || header < 20 || header > total || total > length - 14) return {};
  const uint32_t source = read32(ip + 12), target = read32(ip + 16);
  Dhcp dhcp = parseDhcp(ip, header, total);
  if (dhcp.present && !dhcp.valid) return {};
  if (side == BridgeSide::Ap) {
    auto* client = find(frame + 6);
    if (dhcp.present) {
      if (dhcp.reply || !sameMac(dhcp.bootp + 28, client->mac)) return {};
      client->pendingDhcp = true; client->xid = read32(dhcp.bootp + 4);
      client->xidExpires = nowMs + kTransactionLifetimeMs;
      if (dhcp.type == 7 || dhcp.type == 4) { client->address = 0; client->leased = false; }
      // A normal AP cannot transmit to an unassociated client's MAC. Broadcast
      // replies solve that constraint while preserving chaddr and option 61.
      dhcp.bootp[10] |= 0x80;
      write16(dhcp.udp + 6, 0);  // Legal IPv4 UDP "no checksum", after BOOTP edit.
    } else {
      learn(*client, source, nowMs, kObservedLifetimeMs);
      if (target == address_) return {true, false, false};
      if (const auto* dest = findIp(target, nowMs)) {
        std::memcpy(frame, dest->mac, 6); std::memcpy(frame + 6, apMac_, 6);
        return {false, true, false};
      }
    }
    std::memcpy(frame + 6, staMac_, 6); return {false, false, true};
  }
  if (dhcp.present && dhcp.reply) {
    if (auto* client = find(dhcp.bootp + 28)) {
      const bool expected = client->pendingDhcp && client->xid == read32(dhcp.bootp + 4) && before(nowMs, client->xidExpires);
      if (!expected) return {};
      if (dhcp.type == 5) {
        const uint32_t ttl = static_cast<uint32_t>(std::min<uint64_t>(uint64_t(dhcp.leaseSeconds) * 1000, 0x7fffffff));
        learn(*client, read32(dhcp.bootp + 16), nowMs, ttl, true);
        client->pendingDhcp = false;
      } else if (dhcp.type == 6) {
        client->address = 0; client->leased = false; client->pendingDhcp = false;
      }
      std::memcpy(frame, client->mac, 6); std::memcpy(frame + 6, apMac_, 6);
      return {false, true, false};
    }
    if (sameMac(dhcp.bootp + 28, staMac_)) return {true, false, false};
  }
  if (target == address_) return {true, false, false};
  if (const auto* client = findIp(target, nowMs)) {
    std::memcpy(frame, client->mac, 6); std::memcpy(frame + 6, apMac_, 6);
    return {false, true, false};
  }
  if (target == 0xffffffff || target == (address_ | ~netmask_)) {
    std::memset(frame, 255, 6); std::memcpy(frame + 6, apMac_, 6); return {true, true, false};
  }
  // Multicast remains local only; direct IPv4 unicast is the supported contract.
  return {(target >> 28) == 0xe, false, false};
}
