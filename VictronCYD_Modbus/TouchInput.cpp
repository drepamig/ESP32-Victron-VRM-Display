#include "TouchInput.h"

#ifndef TOUCH_CS
#define TOUCH_CS 33
#endif
#include <TFT_eSPI.h>

namespace {
constexpr char kTouchCalibrationNamespace[] = "touchcal";
constexpr char kMinXKey[] = "minx";
constexpr char kMaxXKey[] = "maxx";
constexpr char kMinYKey[] = "miny";
constexpr char kMaxYKey[] = "maxy";
constexpr char kSwapAxesKey[] = "swap";
constexpr char kInvertXKey[] = "invx";
constexpr char kInvertYKey[] = "invy";

int32_t minimumOfFour(int32_t first, int32_t second, int32_t third, int32_t fourth) {
  int32_t result = first < second ? first : second;
  result = result < third ? result : third;
  return result < fourth ? result : fourth;
}

int32_t maximumOfFour(int32_t first, int32_t second, int32_t third, int32_t fourth) {
  int32_t result = first > second ? first : second;
  result = result > third ? result : third;
  return result > fourth ? result : fourth;
}

int32_t averageOfTwo(int32_t first, int32_t second) {
  return first + (second - first) / 2;
}
}  // namespace

TouchInput::TouchInput(TFT_eSPI& display)
    : display_(display), touchSpi_(HSPI), touch_(33, 36) {}

bool TouchInput::begin() {
  touchSpi_.begin(25, 39, 32, 33);
  if (!touch_.begin(touchSpi_)) {
    return false;
  }
  touch_.setRotation(1);
  if (!loadCalibration()) {
    startCalibration();
  }
  return true;
}

bool TouchInput::calibrated() const {
  return !calibrationActive_ && hasSufficientTouchCalibrationSpan(calibration_);
}

bool TouchInput::poll(TouchEvent& event) {
  event = {TouchEventType::None, {0, 0}, 0, 0};
  const uint32_t now = millis();
  const bool touched = touch_.touched();
  TouchRawPoint rawPoint{};
  bool contactPresent = false;
  if (touched) {
    const TS_Point rawTouch = touch_.getPoint();
    if (rawTouch.z >= kMinimumPressure) {
      rawPoint = {rawTouch.x, rawTouch.y};
      contactPresent = true;
    }
  }
  if (calibrationActive_) {
    return pollCalibration(contactPresent, rawPoint, now, event);
  }
  if (!contactPresent || !calibrated()) {
    event = updateTouchGesture(false, {0, 0}, now, kDebounceMs, kMinimumScrollDistance, gesture_);
    return event.type != TouchEventType::None;
  }

  const TouchPoint point = mapTouchPoint(rawPoint.x, rawPoint.y, calibration_,
                                         static_cast<int16_t>(display_.width()),
                                         static_cast<int16_t>(display_.height()));
  event = updateTouchGesture(true, point, now, kDebounceMs, kMinimumScrollDistance, gesture_);
  return event.type != TouchEventType::None;
}

void TouchInput::startCalibration() {
  calibrationActive_ = true;
  calibrationContact_ = {};
  calibrationTargetIndex_ = 0;
  drawCalibrationScreen(false);
}

bool TouchInput::applyCalibration(const TouchCalibration& calibration) {
  if (!hasSufficientTouchCalibrationSpan(calibration) || !saveCalibration(calibration)) {
    return false;
  }
  calibration_ = calibration;
  calibrationActive_ = false;
  calibrationContact_ = {};
  return true;
}

bool TouchInput::loadCalibration() {
  Preferences preferences;
  if (!preferences.begin(kTouchCalibrationNamespace, true)) {
    return false;
  }
  const bool validTypes = preferences.getType(kMinXKey) == PT_I32 &&
                          preferences.getType(kMaxXKey) == PT_I32 &&
                          preferences.getType(kMinYKey) == PT_I32 &&
                          preferences.getType(kMaxYKey) == PT_I32 &&
                          preferences.getType(kSwapAxesKey) == PT_U8 &&
                          preferences.getType(kInvertXKey) == PT_U8 &&
                          preferences.getType(kInvertYKey) == PT_U8;
  TouchCalibration loaded{};
  if (validTypes) {
    loaded = {preferences.getInt(kMinXKey), preferences.getInt(kMaxXKey),
              preferences.getInt(kMinYKey), preferences.getInt(kMaxYKey),
              preferences.getBool(kSwapAxesKey), preferences.getBool(kInvertXKey),
              preferences.getBool(kInvertYKey)};
  }
  preferences.end();
  if (!validTypes || !hasSufficientTouchCalibrationSpan(loaded)) {
    return false;
  }
  calibration_ = loaded;
  return true;
}

bool TouchInput::saveCalibration(const TouchCalibration& calibration) {
  Preferences preferences;
  if (!preferences.begin(kTouchCalibrationNamespace, false)) {
    return false;
  }
  const bool saved = preferences.putInt(kMinXKey, calibration.minX) == sizeof(int32_t) &&
                     preferences.putInt(kMaxXKey, calibration.maxX) == sizeof(int32_t) &&
                     preferences.putInt(kMinYKey, calibration.minY) == sizeof(int32_t) &&
                     preferences.putInt(kMaxYKey, calibration.maxY) == sizeof(int32_t) &&
                     preferences.putBool(kSwapAxesKey, calibration.swapAxes) == sizeof(uint8_t) &&
                     preferences.putBool(kInvertXKey, calibration.invertX) == sizeof(uint8_t) &&
                     preferences.putBool(kInvertYKey, calibration.invertY) == sizeof(uint8_t);
  preferences.end();
  return saved;
}

bool TouchInput::pollCalibration(bool contactPresent, const TouchRawPoint& rawPoint, uint32_t now,
                                 TouchEvent& event) {
  const CalibrationContactTransition transition =
      updateCalibrationContact(contactPresent, now, kDebounceMs, calibrationContact_);
  if (transition == CalibrationContactTransition::TargetCanAdvance) {
    ++calibrationTargetIndex_;
    if (calibrationTargetIndex_ < kCalibrationTargetCount) {
      drawCalibrationTarget();
      return false;
    }

    const TouchCalibration calibration = makeCalibration();
    if (!applyCalibration(calibration)) {
      calibrationTargetIndex_ = 0;
      calibrationContact_ = {};
      drawCalibrationScreen(true);
      return false;
    }
    display_.fillScreen(TFT_BLACK);
    event = {TouchEventType::CalibrationComplete, {0, 0}, 0, 0};
    return true;
  }
  if (!contactPresent || !calibrationContact_.contactActive ||
      calibrationContact_.awaitingRelease) {
    return false;
  }
  calibrationSamples_[calibrationContact_.sampleCount++] = rawPoint;
  if (calibrationContact_.sampleCount < kCalibrationSampleCount) {
    return false;
  }
  calibrationContact_.sampleCount = 0;
  if (!areTouchSamplesStable(calibrationSamples_, kMaximumCalibrationSampleSpread)) {
    return false;
  }

  calibrationCorners_[calibrationTargetIndex_] = medianTouchSamples(calibrationSamples_);
  calibrationContact_.awaitingRelease = true;
  calibrationContact_.targetReady = true;
  return false;
}

TouchCalibration TouchInput::makeCalibration() const {
  const TouchRawPoint& topLeft = calibrationCorners_[0];
  const TouchRawPoint& topRight = calibrationCorners_[1];
  const TouchRawPoint& bottomRight = calibrationCorners_[2];
  const TouchRawPoint& bottomLeft = calibrationCorners_[3];

  const int32_t horizontalRawX = absoluteTouchValue(
      averageOfTwo(topRight.x, bottomRight.x) - averageOfTwo(topLeft.x, bottomLeft.x));
  const int32_t horizontalRawY = absoluteTouchValue(
      averageOfTwo(topRight.y, bottomRight.y) - averageOfTwo(topLeft.y, bottomLeft.y));
  const bool swapAxes = horizontalRawY > horizontalRawX;
  const int32_t leftX = averageOfTwo(topLeft.x, bottomLeft.x);
  const int32_t rightX = averageOfTwo(topRight.x, bottomRight.x);
  const int32_t topX = averageOfTwo(topLeft.x, topRight.x);
  const int32_t bottomX = averageOfTwo(bottomLeft.x, bottomRight.x);
  const int32_t leftY = averageOfTwo(topLeft.y, bottomLeft.y);
  const int32_t rightY = averageOfTwo(topRight.y, bottomRight.y);
  const int32_t topY = averageOfTwo(topLeft.y, topRight.y);
  const int32_t bottomY = averageOfTwo(bottomLeft.y, bottomRight.y);

  TouchCalibration calibration{
      minimumOfFour(topLeft.x, topRight.x, bottomRight.x, bottomLeft.x),
      maximumOfFour(topLeft.x, topRight.x, bottomRight.x, bottomLeft.x),
      minimumOfFour(topLeft.y, topRight.y, bottomRight.y, bottomLeft.y),
      maximumOfFour(topLeft.y, topRight.y, bottomRight.y, bottomLeft.y),
      swapAxes,
      swapAxes ? leftY > rightY : leftX > rightX,
      swapAxes ? topX > bottomX : topY > bottomY,
  };
  return calibration;
}

void TouchInput::drawCalibrationScreen(bool invalidCalibration) {
  const int16_t width = static_cast<int16_t>(display_.width());
  const int16_t height = static_cast<int16_t>(display_.height());
  calibrationTargets_[0] = {20, 20};
  calibrationTargets_[1] = {static_cast<int16_t>(width - 21), 20};
  calibrationTargets_[2] = {static_cast<int16_t>(width - 21), static_cast<int16_t>(height - 21)};
  calibrationTargets_[3] = {20, static_cast<int16_t>(height - 21)};
  display_.fillScreen(TFT_BLACK);
  display_.setTextColor(invalidCalibration ? TFT_RED : TFT_WHITE, TFT_BLACK);
  display_.setTextDatum(MC_DATUM);
  display_.drawString(invalidCalibration ? "Calibration invalid" : "Touch calibration", width / 2,
                      height / 2 - 18, 2);
  display_.setTextColor(TFT_WHITE, TFT_BLACK);
  display_.drawString("Hold each target", width / 2, height / 2 + 4, 2);
  drawCalibrationTarget();
}

void TouchInput::drawCalibrationTarget() {
  const TouchPoint target = calibrationTargets_[calibrationTargetIndex_];
  display_.drawCircle(target.x, target.y, 12, TFT_WHITE);
  display_.fillCircle(target.x, target.y, 4, TFT_RED);
}
