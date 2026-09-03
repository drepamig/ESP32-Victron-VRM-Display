#include "RawTouchDevice.h"

namespace {
#if defined(CYD_SIMULATION) || defined(CYD_HOST_TEST)
constexpr int16_t kRawMinimum = 200;
constexpr int16_t kRawMaximum = 3800;
constexpr int16_t kFtMaximumX = 239;
constexpr int16_t kFtMaximumY = 319;

int16_t clampCoordinate(int16_t value, int16_t maximum) {
  if (value < 0) {
    return 0;
  }
  return value > maximum ? maximum : value;
}

int16_t scaleCoordinate(int16_t value, int16_t maximum) {
  const int32_t span = kRawMaximum - kRawMinimum;
  return static_cast<int16_t>(kRawMinimum +
                              (static_cast<int32_t>(clampCoordinate(value, maximum)) * span) /
                                  maximum);
}
#endif
}  // namespace

#if defined(CYD_SIMULATION) || defined(CYD_HOST_TEST)
RawTouchSample normalizeFt6206Sample(bool contact, int16_t x, int16_t y) {
  if (!contact) {
    return {false, {0, 0}, 0};
  }
  return {true,
          {scaleCoordinate(x, kFtMaximumX), scaleCoordinate(y, kFtMaximumY)},
          1000};
}
#endif

#ifdef CYD_SIMULATION

bool Ft6206RawTouchDevice::begin() {
  wire_.begin(32, 25);
  return touch_.begin(40, &wire_);
}

RawTouchSample Ft6206RawTouchDevice::sample() {
  if (!touch_.touched()) {
    return {false, {0, 0}, 0};
  }
  const TS_Point point = touch_.getPoint();
  return normalizeFt6206Sample(true, point.x, point.y);
}

#else

Xpt2046RawTouchDevice::Xpt2046RawTouchDevice() : spi_(HSPI), touch_(33, 36) {}

bool Xpt2046RawTouchDevice::begin() {
  spi_.begin(25, 39, 32, 33);
  if (!touch_.begin(spi_)) {
    return false;
  }
  touch_.setRotation(1);
  return true;
}

RawTouchSample Xpt2046RawTouchDevice::sample() {
  if (!touch_.touched()) {
    return {false, {0, 0}, 0};
  }
  const TS_Point point = touch_.getPoint();
  return {true, {point.x, point.y}, point.z};
}

#endif
