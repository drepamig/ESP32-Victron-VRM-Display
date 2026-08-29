#pragma once

#include <cstdint>

class IPAddress {
 public:
  IPAddress() = default;
  IPAddress(uint32_t address) : address_(address) {}
  IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
      : address_(static_cast<uint32_t>(a) | (static_cast<uint32_t>(b) << 8) |
                 (static_cast<uint32_t>(c) << 16) | (static_cast<uint32_t>(d) << 24)) {}

  operator uint32_t() const { return address_; }
  uint8_t operator[](int index) const {
    return static_cast<uint8_t>((address_ >> (static_cast<uint32_t>(index) * 8U)) & 0xffU);
  }
  bool operator==(const IPAddress& other) const { return address_ == other.address_; }
  bool operator!=(const IPAddress& other) const { return !(*this == other); }

 private:
  uint32_t address_ = 0;
};
