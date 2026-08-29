#include <cassert>
#include <cstdint>

#include "../../VictronCYD_Modbus/TouchMapping.h"

int main() {
  TouchCalibration calibration{200, 3800, 250, 3750, false, false, false};
  const TouchPoint topLeft = mapTouchPoint(200, 250, calibration, 320, 240);
  const TouchPoint bottomRight = mapTouchPoint(3800, 3750, calibration, 320, 240);
  assert(topLeft.x == 0 && topLeft.y == 0);
  assert(bottomRight.x == 319 && bottomRight.y == 239);

  const TouchPoint clamped = mapTouchPoint(0, 4095, calibration, 320, 240);
  assert(clamped.x >= 0 && clamped.x < 320);
  assert(clamped.y >= 0 && clamped.y < 240);

  const TouchCalibration swapped{200, 3800, 250, 3750, true, false, false};
  const TouchPoint swappedBottomRight = mapTouchPoint(3800, 3750, swapped, 320, 240);
  assert(swappedBottomRight.x == 319 && swappedBottomRight.y == 239);

  const TouchCalibration inverted{200, 3800, 250, 3750, false, true, true};
  const TouchPoint invertedTopLeft = mapTouchPoint(200, 250, inverted, 320, 240);
  assert(invertedTopLeft.x == 319 && invertedTopLeft.y == 239);

  assert(isValidTouchCalibration(calibration));
  assert(!isValidTouchCalibration(TouchCalibration{200, 200, 250, 3750, false, false, false}));
  assert(!isValidTouchCalibration(TouchCalibration{200, 3800, 250, 250, false, false, false}));
  assert(!isValidTouchCalibration(TouchCalibration{-1, 3800, 250, 3750, false, false, false}));
  assert(!isValidTouchCalibration(TouchCalibration{200, 4096, 250, 3750, false, false, false}));
  assert(!isValidTouchMappingInput(calibration, 0, 240));
  assert(!isValidTouchMappingInput(calibration, 320, 0));

  const TouchRawPoint samples[5]{{100, 200}, {102, 198}, {101, 201}, {99, 199}, {1000, 0}};
  const TouchRawPoint median = medianTouchSamples(samples);
  assert(median.x == 101 && median.y == 199);
  const TouchRawPoint stableSamples[5]{{100, 200}, {102, 198}, {101, 201}, {99, 199}, {100, 200}};
  assert(areTouchSamplesStable(stableSamples, 10));
  assert(!areTouchSamplesStable(samples, 10));
  assert(hasSufficientTouchCalibrationSpan(TouchCalibration{0, 1500, 0, 1500, false, false, false}));
  assert(!hasSufficientTouchCalibrationSpan(TouchCalibration{0, 1499, 0, 1500, false, false, false}));

  TouchGestureState gesture{};
  TouchEvent event = updateTouchGesture(true, TouchPoint{10, 10}, 100, 40, 6, gesture);
  assert(event.type == TouchEventType::Press);
  event = updateTouchGesture(true, TouchPoint{10, 10}, 110, 40, 6, gesture);
  assert(event.type == TouchEventType::None);
  event = updateTouchGesture(true, TouchPoint{17, 10}, 120, 40, 6, gesture);
  assert(event.type == TouchEventType::Scroll && event.deltaX == 7 && event.deltaY == 0);
  event = updateTouchGesture(false, TouchPoint{0, 0}, 130, 40, 6, gesture);
  assert(event.type == TouchEventType::None);
  event = updateTouchGesture(true, TouchPoint{20, 20}, 150, 40, 6, gesture);
  assert(event.type == TouchEventType::None);
  event = updateTouchGesture(true, TouchPoint{20, 20}, 170, 40, 6, gesture);
  assert(event.type == TouchEventType::Press);
  return 0;
}
