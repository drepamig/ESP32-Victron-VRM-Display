#include <cstdlib>
#include <iostream>

#include "../../VictronCYD_Modbus/RawTouchDevice.h"

namespace {
void check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}
}  // namespace

int main() {
  const RawTouchSample released = normalizeFt6206Sample(false, 120, 160);
  check(!released.contact && released.pressure == 0,
        "released FT6206 sample stays released");

  const RawTouchSample origin = normalizeFt6206Sample(true, 0, 0);
  check(origin.contact && origin.pressure == 1000 && origin.point.x == 200 &&
            origin.point.y == 200,
        "FT6206 origin maps to low XPT-style raw values");

  const RawTouchSample farCorner = normalizeFt6206Sample(true, 239, 319);
  check(farCorner.point.x == 3800 && farCorner.point.y == 3800,
        "FT6206 far corner maps to high XPT-style raw values");

  const RawTouchSample clampedLow = normalizeFt6206Sample(true, -10, -20);
  check(clampedLow.point.x == 200 && clampedLow.point.y == 200,
        "negative FT6206 coordinates clamp before scaling");

  const RawTouchSample clampedHigh = normalizeFt6206Sample(true, 999, 999);
  check(clampedHigh.point.x == 3800 && clampedHigh.point.y == 3800,
        "oversized FT6206 coordinates clamp before scaling");
  return 0;
}
