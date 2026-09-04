#pragma once

#include <cstddef>
#include <cstdint>

#include "TimeSettings.h"
#include "TouchMapping.h"

class TFT_eSPI;

enum class SettingsView : uint8_t { Closed, Root, Time, Countries, Zones };
enum class SettingsAction : uint8_t { None, OpenWifi, Save, Exit };

struct SettingsUiRect {
  int16_t x;
  int16_t y;
  int16_t width;
  int16_t height;

  bool contains(const TouchPoint& point) const;
};

class SettingsUi {
 public:
  explicit SettingsUi(TFT_eSPI& display);

  void open(uint32_t nowMs, const TimeSettings& active);
  void close();
  bool isOpen() const;
  SettingsView view() const;
  const TimeSettings& draft() const;
  SettingsAction handleTouch(const TouchPoint& point, uint32_t nowMs);
  void handleRelease(uint32_t nowMs);
  SettingsAction poll(uint32_t nowMs);
  bool takeFullRenderRequest();
  void render();
  void saveResult(bool success);

 private:
  static constexpr size_t kRowsPerPage = 5;
  static constexpr uint32_t kInactivityMs = 60000;

  void changeView(SettingsView next);
  void selectCountry(size_t country);
  size_t countryZoneCount() const;
  const TimeZone* countryZoneAt(size_t index) const;
  void drawButton(const SettingsUiRect& bounds, const char* label, bool selected = false,
                  bool enabled = true, uint8_t font = 2);
  void drawHeader(const char* title);
  void renderTime();
  void renderCountries();
  void renderZones();

  TFT_eSPI& display_;
  SettingsView view_ = SettingsView::Closed;
  TimeSettings active_;
  TimeSettings draft_;
  uint32_t lastActivityMs_ = 0;
  size_t countryIndex_ = 0;
  size_t zonePage_ = 0;
  bool awaitRelease_ = false;
  bool fullRenderRequested_ = false;
  bool savePending_ = false;
  bool saveFailed_ = false;
};
