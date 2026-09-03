#pragma once

#include <Arduino.h>

#include "TouchMapping.h"

struct RawTouchSample {
  bool contact;
  TouchRawPoint point;
  int16_t pressure;
};

#if defined(CYD_SIMULATION) || defined(CYD_HOST_TEST)
RawTouchSample normalizeFt6206Sample(bool contact, int16_t x, int16_t y);
#endif

class RawTouchDevice {
 public:
  virtual ~RawTouchDevice() = default;
  virtual bool begin() = 0;
  virtual RawTouchSample sample() = 0;
};

#ifdef CYD_SIMULATION

#include <Adafruit_FT6206.h>
#include <Wire.h>

class Ft6206RawTouchDevice final : public RawTouchDevice {
 public:
  bool begin() override;
  RawTouchSample sample() override;

 private:
  TwoWire wire_{1};
  Adafruit_FT6206 touch_;
};

using RawTouchDeviceRuntime = Ft6206RawTouchDevice;

#else

#include <SPI.h>
#include <XPT2046_Touchscreen.h>

class Xpt2046RawTouchDevice final : public RawTouchDevice {
 public:
  Xpt2046RawTouchDevice();
  bool begin() override;
  RawTouchSample sample() override;

 private:
  SPIClass spi_;
  XPT2046_Touchscreen touch_;
};

using RawTouchDeviceRuntime = Xpt2046RawTouchDevice;

#endif
