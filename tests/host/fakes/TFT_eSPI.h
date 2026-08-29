#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Arduino.h"

constexpr uint16_t TFT_BLACK = 0x0000;
constexpr uint16_t TFT_WHITE = 0xFFFF;
constexpr uint16_t TFT_RED = 0xF800;
constexpr uint16_t TFT_GREEN = 0x07E0;
constexpr uint16_t TFT_YELLOW = 0xFFE0;
constexpr uint16_t TFT_DARKGREY = 0x7BEF;
constexpr uint16_t TFT_BLUE = 0x001F;
constexpr uint16_t TFT_CYAN = 0x07FF;

constexpr uint8_t TL_DATUM = 0;
constexpr uint8_t TC_DATUM = 1;
constexpr uint8_t TR_DATUM = 2;
constexpr uint8_t ML_DATUM = 3;
constexpr uint8_t MC_DATUM = 4;

class TFT_eSPI {
 public:
  int16_t width() const { return 320; }
  int16_t height() const { return 240; }

  void fillScreen(uint16_t) { drawnStrings.clear(); }
  void fillRect(int32_t, int32_t, int32_t, int32_t, uint16_t) {}
  void drawRect(int32_t, int32_t, int32_t, int32_t, uint16_t) {}
  void fillRoundRect(int32_t, int32_t, int32_t, int32_t, int32_t, uint16_t) {}
  void drawRoundRect(int32_t, int32_t, int32_t, int32_t, int32_t, uint16_t) {}
  void drawLine(int32_t, int32_t, int32_t, int32_t, uint16_t) {}
  void fillCircle(int32_t, int32_t, int32_t, uint16_t) {}
  void drawCircle(int32_t, int32_t, int32_t, uint16_t) {}
  void setTextColor(uint16_t, uint16_t = TFT_BLACK) {}
  void setTextDatum(uint8_t) {}

  int16_t drawString(const char* value, int32_t, int32_t, uint8_t = 1) {
    drawnStrings.emplace_back(value == nullptr ? "" : value);
    return 0;
  }

  int16_t drawString(const String& value, int32_t x, int32_t y, uint8_t font = 1) {
    return drawString(value.c_str(), x, y, font);
  }

  bool drew(const std::string& value) const {
    for (const std::string& drawn : drawnStrings) {
      if (drawn == value) {
        return true;
      }
    }
    return false;
  }

  bool drewContaining(const std::string& value) const {
    for (const std::string& drawn : drawnStrings) {
      if (drawn.find(value) != std::string::npos) {
        return true;
      }
    }
    return false;
  }

  std::vector<std::string> drawnStrings;
};
