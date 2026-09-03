#pragma once

#include <cstddef>
#include <cstdint>

#include "CredentialEntryController.h"
#include "TouchMapping.h"

struct CredentialKeyRect {
  int16_t x;
  int16_t y;
  int16_t width;
  int16_t height;

  constexpr bool contains(const TouchPoint& point) const {
    return point.x >= x && point.y >= y && point.x < x + width && point.y < y + height;
  }
};

inline constexpr CredentialKeyRect kCredentialBackBounds{4, 4, 56, 28};
inline constexpr CredentialKeyRect kCredentialShowBounds{252, 4, 64, 28};
inline constexpr CredentialKeyRect kCredentialShiftOrAbcBounds{4, 157, 54, 30};
inline constexpr CredentialKeyRect kCredentialPageBounds{62, 157, 54, 30};
inline constexpr CredentialKeyRect kCredentialSpaceBounds{120, 157, 132, 30};
inline constexpr CredentialKeyRect kCredentialBackspaceBounds{256, 157, 60, 30};
inline constexpr CredentialKeyRect kCredentialUsePhoneBounds{4, 191, 151, 45};
inline constexpr CredentialKeyRect kCredentialConnectBounds{164, 191, 152, 45};

enum class CredentialKeyType : uint8_t {
  None,
  Character,
  Shift,
  ShiftOrAbc = Shift,
  Page,
  Space,
  Backspace,
  Back,
  Show,
  UsePhone,
  Connect,
};

struct CredentialKeyHit {
  CredentialKeyType type = CredentialKeyType::None;
  char character = '\0';
};

struct CredentialKeyboardRow {
  const char* characters;
  int16_t x;
  int16_t y;
  int16_t keyWidth;
  int16_t keyHeight;
  int16_t gap;
};

CredentialKeyboardRow credentialKeyboardRow(CredentialKeyboardPage page, size_t row);
CredentialKeyHit credentialKeyboardHitTest(CredentialKeyboardPage page, const TouchPoint& point);
const char* credentialShiftOrAbcLabel(CredentialKeyboardPage page);
const char* credentialPageLabel(CredentialKeyboardPage page);
