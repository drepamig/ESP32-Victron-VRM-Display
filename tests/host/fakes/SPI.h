#pragma once

#include <cstdint>

constexpr uint8_t HSPI = 1;

class SPIClass {
 public:
  explicit SPIClass(uint8_t) {}
  void begin(int8_t, int8_t, int8_t, int8_t) {}
};
