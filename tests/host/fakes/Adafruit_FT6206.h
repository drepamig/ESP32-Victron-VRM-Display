#pragma once

#include <cstdint>

#include "Wire.h"

struct TS_Point {
  int16_t x = 0;
  int16_t y = 0;
  int16_t z = 0;
};

// These are separate I2C observations in the real driver. A release can
// occur after touched() returns 1 but before getPoint() returns (0, 0, 0).
struct FakeFt6206State {
  uint8_t contacts = 0;
  TS_Point point;
};

inline FakeFt6206State FakeFt6206;

class Adafruit_FT6206 {
 public:
  bool begin(uint8_t, TwoWire*) { return true; }
  uint8_t touched() const { return FakeFt6206.contacts; }
  TS_Point getPoint() const { return FakeFt6206.point; }
};
