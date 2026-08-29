#pragma once

#include <Preferences.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>

#include "TouchMapping.h"

class TFT_eSPI;

class TouchInput {
 public:
  explicit TouchInput(TFT_eSPI& display);
  TouchInput(const TouchInput&) = delete;
  TouchInput& operator=(const TouchInput&) = delete;

  bool begin();
  bool poll(TouchEvent& event);
  bool calibrated() const;
  void startCalibration();
  bool applyCalibration(const TouchCalibration& calibration);

 private:
  static constexpr uint8_t kCalibrationSampleCount = 5;
  static constexpr uint8_t kCalibrationTargetCount = 4;
  static constexpr int16_t kMinimumPressure = 400;
  static constexpr uint32_t kDebounceMs = 40;
  static constexpr int16_t kMinimumScrollDistance = 6;
  static constexpr int32_t kMaximumCalibrationSampleSpread = 80;

  bool loadCalibration();
  bool saveCalibration(const TouchCalibration& calibration);
  bool pollCalibration(const TouchRawPoint& rawPoint, TouchEvent& event);
  TouchCalibration makeCalibration() const;
  void drawCalibrationScreen(bool invalidCalibration);
  void drawCalibrationTarget();

  TFT_eSPI& display_;
  SPIClass touchSpi_;
  XPT2046_Touchscreen touch_;
  TouchCalibration calibration_{};
  TouchGestureState gesture_{};
  TouchRawPoint calibrationSamples_[kCalibrationSampleCount]{};
  TouchRawPoint calibrationCorners_[kCalibrationTargetCount]{};
  TouchPoint calibrationTargets_[kCalibrationTargetCount]{};
  uint8_t calibrationSampleCount_ = 0;
  uint8_t calibrationTargetIndex_ = 0;
  bool calibrationActive_ = false;
  bool calibrationAwaitingRelease_ = false;
};
