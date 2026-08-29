#pragma once

#include <cstdint>

struct TouchCalibration {
  int32_t minX;
  int32_t maxX;
  int32_t minY;
  int32_t maxY;
  bool swapAxes;
  bool invertX;
  bool invertY;
};

struct TouchPoint {
  int16_t x;
  int16_t y;
};

struct TouchRawPoint {
  int32_t x;
  int32_t y;
};

enum class TouchEventType : uint8_t {
  None,
  Press,
  Scroll,
  CalibrationComplete,
};

struct TouchEvent {
  TouchEventType type;
  TouchPoint point;
  int16_t deltaX;
  int16_t deltaY;
};

struct TouchGestureState {
  bool pressed;
  uint32_t lastTransitionMs;
  TouchPoint lastPoint;
};

constexpr int32_t kMinimumTouchCalibrationSpan = 1500;

inline bool isValidTouchCalibration(const TouchCalibration& calibration) {
  return calibration.minX >= 0 && calibration.maxX <= 4095 && calibration.minY >= 0 &&
         calibration.maxY <= 4095 && calibration.maxX > calibration.minX &&
         calibration.maxY > calibration.minY;
}

inline bool isValidTouchMappingInput(const TouchCalibration& calibration, int16_t width,
                                     int16_t height) {
  return isValidTouchCalibration(calibration) && width > 0 && height > 0;
}

inline bool hasSufficientTouchCalibrationSpan(const TouchCalibration& calibration) {
  return isValidTouchCalibration(calibration) &&
         calibration.maxX - calibration.minX >= kMinimumTouchCalibrationSpan &&
         calibration.maxY - calibration.minY >= kMinimumTouchCalibrationSpan;
}

inline int32_t medianOfFiveTouchSamples(const int32_t samples[5]) {
  int32_t sorted[5];
  for (uint8_t index = 0; index < 5; ++index) {
    sorted[index] = samples[index];
  }
  for (uint8_t current = 1; current < 5; ++current) {
    const int32_t value = sorted[current];
    uint8_t previous = current;
    while (previous > 0 && sorted[previous - 1] > value) {
      sorted[previous] = sorted[previous - 1];
      --previous;
    }
    sorted[previous] = value;
  }
  return sorted[2];
}

inline TouchRawPoint medianTouchSamples(const TouchRawPoint samples[5]) {
  int32_t x[5];
  int32_t y[5];
  for (uint8_t index = 0; index < 5; ++index) {
    x[index] = samples[index].x;
    y[index] = samples[index].y;
  }
  return {medianOfFiveTouchSamples(x), medianOfFiveTouchSamples(y)};
}

inline bool areTouchSamplesStable(const TouchRawPoint samples[5], int32_t maximumSpread) {
  if (maximumSpread < 0) {
    return false;
  }
  int32_t minimumX = samples[0].x;
  int32_t maximumX = samples[0].x;
  int32_t minimumY = samples[0].y;
  int32_t maximumY = samples[0].y;
  for (uint8_t index = 1; index < 5; ++index) {
    minimumX = samples[index].x < minimumX ? samples[index].x : minimumX;
    maximumX = samples[index].x > maximumX ? samples[index].x : maximumX;
    minimumY = samples[index].y < minimumY ? samples[index].y : minimumY;
    maximumY = samples[index].y > maximumY ? samples[index].y : maximumY;
  }
  return maximumX - minimumX <= maximumSpread && maximumY - minimumY <= maximumSpread;
}

inline int32_t absoluteTouchValue(int32_t value) {
  return value < 0 ? -value : value;
}

inline TouchEvent updateTouchGesture(bool touching, TouchPoint point, uint32_t now,
                                     uint32_t debounceMs, int16_t minimumScrollDistance,
                                     TouchGestureState& state) {
  if (!touching) {
    if (state.pressed) {
      state.pressed = false;
      state.lastTransitionMs = now;
    }
    return {TouchEventType::None, point, 0, 0};
  }

  if (!state.pressed) {
    if (now - state.lastTransitionMs < debounceMs) {
      return {TouchEventType::None, point, 0, 0};
    }
    state.pressed = true;
    state.lastPoint = point;
    return {TouchEventType::Press, point, 0, 0};
  }

  const int32_t deltaX = static_cast<int32_t>(point.x) - state.lastPoint.x;
  const int32_t deltaY = static_cast<int32_t>(point.y) - state.lastPoint.y;
  if (absoluteTouchValue(deltaX) >= minimumScrollDistance ||
      absoluteTouchValue(deltaY) >= minimumScrollDistance) {
    state.lastPoint = point;
    return {TouchEventType::Scroll, point, static_cast<int16_t>(deltaX),
            static_cast<int16_t>(deltaY)};
  }
  return {TouchEventType::None, point, 0, 0};
}

inline int32_t clampTouchValue(int32_t value, int32_t minimum, int32_t maximum) {
  return value < minimum ? minimum : (value > maximum ? maximum : value);
}

inline TouchPoint mapTouchPoint(int32_t rawX, int32_t rawY,
                                const TouchCalibration& calibration, int16_t width,
                                int16_t height) {
  if (!isValidTouchMappingInput(calibration, width, height)) {
    return {-1, -1};
  }

  const int32_t sourceX = calibration.swapAxes ? rawY : rawX;
  const int32_t sourceY = calibration.swapAxes ? rawX : rawY;
  const int32_t minX = calibration.swapAxes ? calibration.minY : calibration.minX;
  const int32_t maxX = calibration.swapAxes ? calibration.maxY : calibration.maxX;
  const int32_t minY = calibration.swapAxes ? calibration.minX : calibration.minY;
  const int32_t maxY = calibration.swapAxes ? calibration.maxX : calibration.maxY;
  const int32_t clampedX = clampTouchValue(sourceX, minX, maxX);
  const int32_t clampedY = clampTouchValue(sourceY, minY, maxY);
  const int32_t x = static_cast<int32_t>(
      (static_cast<int64_t>(clampedX - minX) * (width - 1)) / (maxX - minX));
  const int32_t y = static_cast<int32_t>(
      (static_cast<int64_t>(clampedY - minY) * (height - 1)) / (maxY - minY));
  const int32_t mappedX = calibration.invertX ? width - 1 - x : x;
  const int32_t mappedY = calibration.invertY ? height - 1 - y : y;
  return {static_cast<int16_t>(clampTouchValue(mappedX, 0, width - 1)),
          static_cast<int16_t>(clampTouchValue(mappedY, 0, height - 1))};
}
