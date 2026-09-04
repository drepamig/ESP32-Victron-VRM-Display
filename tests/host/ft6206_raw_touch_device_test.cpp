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

void checkReleased(const RawTouchSample& sample, const char* message) {
  check(!sample.contact && sample.pressure == 0 && sample.point.x == 0 &&
            sample.point.y == 0,
        message);
}
}  // namespace

int main() {
  Ft6206RawTouchDevice device;
  check(device.begin(), "FT6206 begins with the hardware boundary available");

  FakeFt6206 = {1, {239, 319, 1}};
  const RawTouchSample pressed = device.sample();
  check(pressed.contact && pressed.pressure == 1000 &&
            pressed.point.x == 3800 && pressed.point.y == 3800,
        "coordinate-frame contact produces a normalized press");

  FakeFt6206 = {1, {0, 0, 0}};
  checkReleased(device.sample(),
                "release between touched and getPoint must not create a press");

  FakeFt6206 = {0, {239, 319, 1}};
  checkReleased(device.sample(),
                "no current contact must ignore stale coordinate data");

  FakeFt6206 = {1, {0, 0, 1}};
  const RawTouchSample origin = device.sample();
  check(origin.contact && origin.pressure == 1000 && origin.point.x == 200 &&
            origin.point.y == 200,
        "a later real press at the origin remains valid");
  return 0;
}
