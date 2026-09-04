#pragma once

#include <cstdint>

class TwoWire {
 public:
  explicit TwoWire(uint8_t) {}
  void begin(int, int) {}
};
