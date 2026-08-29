#include <cstdlib>
#include <iostream>

#include "Arduino.h"
#include "TFT_eSPI.h"
#include "XPT2046_Touchscreen.h"
#include "../../VictronCYD_Modbus/TouchInput.h"

namespace {
void check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}
}  // namespace

int main() {
  TFT_eSPI display;
  TouchInput input(display);
  check(input.applyCalibration({200, 3800, 250, 3750, false, false, false}),
        "valid calibration should be accepted");

  TouchEvent event{};
  FakeTouchController.contact = true;
  FakeTouchController.point = TS_Point(1000, 1000, 500);
  fakeMillis = 100;
  check(!input.poll(event) && event.type == TouchEventType::None,
        "contact must debounce before Press");
  fakeMillis = 140;
  check(input.poll(event) && event.type == TouchEventType::Press,
        "stable contact must emit Press");

  FakeTouchController.contact = false;
  fakeMillis = 150;
  check(!input.poll(event) && event.type == TouchEventType::None,
        "release candidate must debounce");
  fakeMillis = 190;
  check(input.poll(event) && event.type == TouchEventType::Release,
        "stable release must be forwarded by TouchInput::poll");
  fakeMillis = 191;
  check(!input.poll(event) && event.type == TouchEventType::None,
        "TouchInput::poll must emit Release exactly once");
  return 0;
}
