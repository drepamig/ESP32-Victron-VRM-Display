#pragma once

#include <cstdint>

#include "SPI.h"

struct TS_Point {
  TS_Point(int16_t xValue = 0, int16_t yValue = 0, int16_t zValue = 0)
      : x(xValue), y(yValue), z(zValue) {}

  int16_t x;
  int16_t y;
  int16_t z;
};

struct FakeTouchControllerState {
  bool contact = false;
  TS_Point point;
};

inline FakeTouchControllerState FakeTouchController;

class XPT2046_Touchscreen {
 public:
  XPT2046_Touchscreen(uint8_t, uint8_t) {}
  bool begin(SPIClass&) { return true; }
  void setRotation(uint8_t) {}
  bool touched() const { return FakeTouchController.contact; }
  TS_Point getPoint() const { return FakeTouchController.point; }
};
