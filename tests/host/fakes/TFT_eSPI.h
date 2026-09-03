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
  struct RectCall {
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
  };

  struct CircleCall {
    int32_t x;
    int32_t y;
    int32_t radius;
  };

  struct StringCall {
    std::string value;
    int32_t x;
    int32_t y;
    uint8_t font;
  };

  int16_t width() const { return 320; }
  int16_t height() const { return 240; }

  void fillScreen(uint16_t) {
    ++fillScreenCount;
    drawnStrings.clear();
    stringCalls.clear();
    filledRects.clear();
    drawnCircles.clear();
  }
  void fillRect(int32_t x, int32_t y, int32_t width, int32_t height, uint16_t) {
    filledRects.push_back({x, y, width, height});
  }
  void drawRect(int32_t, int32_t, int32_t, int32_t, uint16_t) {}
  void fillRoundRect(int32_t, int32_t, int32_t, int32_t, int32_t, uint16_t) {}
  void drawRoundRect(int32_t, int32_t, int32_t, int32_t, int32_t, uint16_t) {}
  void drawLine(int32_t, int32_t, int32_t, int32_t, uint16_t) {}
  void fillCircle(int32_t, int32_t, int32_t, uint16_t) {}
  void drawCircle(int32_t x, int32_t y, int32_t radius, uint16_t) {
    drawnCircles.push_back({x, y, radius});
  }
  void setTextColor(uint16_t, uint16_t = TFT_BLACK) {}
  void setTextDatum(uint8_t) {}

  int16_t drawString(const char* value, int32_t x, int32_t y, uint8_t font = 1) {
    const std::string drawn = value == nullptr ? "" : value;
    drawnStrings.push_back(drawn);
    stringCalls.push_back({drawn, x, y, font});
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

  bool drewSince(const std::string& value, size_t checkpoint) const {
    for (size_t index = checkpoint; index < drawnStrings.size(); ++index) {
      if (drawnStrings[index] == value) {
        return true;
      }
    }
    return false;
  }

  bool drewWithFont(const std::string& value, uint8_t font) const {
    for (const StringCall& call : stringCalls) {
      if (call.value == value && call.font == font) {
        return true;
      }
    }
    return false;
  }

  bool filledRectAt(int32_t x, int32_t y, int32_t width, int32_t height) const {
    for (const RectCall& call : filledRects) {
      if (call.x == x && call.y == y && call.width == width && call.height == height) {
        return true;
      }
    }
    return false;
  }

  bool drewCircleAt(int32_t x, int32_t y, int32_t radius) const {
    for (const CircleCall& call : drawnCircles) {
      if (call.x == x && call.y == y && call.radius == radius) {
        return true;
      }
    }
    return false;
  }

  std::vector<std::string> drawnStrings;
  std::vector<StringCall> stringCalls;
  std::vector<RectCall> filledRects;
  std::vector<CircleCall> drawnCircles;
  uint32_t fillScreenCount = 0;
};
