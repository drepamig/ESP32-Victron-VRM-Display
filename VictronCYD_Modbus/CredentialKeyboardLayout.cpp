#include "CredentialKeyboardLayout.h"

namespace {

constexpr int16_t kRowY = 76;
constexpr int16_t kRowStride = 27;
constexpr int16_t kKeyWidth = 29;
constexpr int16_t kKeyHeight = 24;
constexpr int16_t kKeyGap = 2;

struct PageRows {
  const char* rows[3];
  const int16_t x[3];
};

constexpr PageRows kAlphabetRows{{"qwertyuiop", "asdfghjkl", "zxcvbnm"}, {6, 21, 52}};
constexpr PageRows kNumberRows{{"1234567890", "-/:;()$&@\"", ".,?!'#%"}, {6, 21, 52}};
constexpr PageRows kSymbolRows{{"*+=<>[]{}\\", "^_`|~", ""}, {6, 21, 52}};

const PageRows* pageRows(CredentialKeyboardPage page) {
  switch (page) {
    case CredentialKeyboardPage::Alphabet:
      return &kAlphabetRows;
    case CredentialKeyboardPage::Numbers:
      return &kNumberRows;
    case CredentialKeyboardPage::Symbols:
      return &kSymbolRows;
    default:
      return nullptr;
  }
}

CredentialKeyHit controlHit(const TouchPoint& point) {
  if (kCredentialBackBounds.contains(point)) {
    return {CredentialKeyType::Back, '\0'};
  }
  if (kCredentialShowBounds.contains(point)) {
    return {CredentialKeyType::Show, '\0'};
  }
  if (kCredentialShiftOrAbcBounds.contains(point)) {
    return {CredentialKeyType::Shift, '\0'};
  }
  if (kCredentialPageBounds.contains(point)) {
    return {CredentialKeyType::Page, '\0'};
  }
  if (kCredentialSpaceBounds.contains(point)) {
    return {CredentialKeyType::Space, ' '};
  }
  if (kCredentialBackspaceBounds.contains(point)) {
    return {CredentialKeyType::Backspace, '\0'};
  }
  if (kCredentialUsePhoneBounds.contains(point)) {
    return {CredentialKeyType::UsePhone, '\0'};
  }
  if (kCredentialConnectBounds.contains(point)) {
    return {CredentialKeyType::Connect, '\0'};
  }
  return {CredentialKeyType::None, '\0'};
}

}  // namespace

CredentialKeyboardRow credentialKeyboardRow(CredentialKeyboardPage page, size_t row) {
  const PageRows* rows = pageRows(page);
  if (rows == nullptr || row >= 3) {
    return {"", 0, 0, kKeyWidth, kKeyHeight, kKeyGap};
  }
  return {rows->rows[row], rows->x[row], static_cast<int16_t>(kRowY + row * kRowStride),
          kKeyWidth, kKeyHeight, kKeyGap};
}

CredentialKeyHit credentialKeyboardHitTest(CredentialKeyboardPage page, const TouchPoint& point) {
  if (point.x < 0 || point.x >= 320 || point.y < 0 || point.y >= 240) {
    return {CredentialKeyType::None, '\0'};
  }

  const CredentialKeyHit utilityHit = controlHit(point);
  if (utilityHit.type != CredentialKeyType::None) {
    return utilityHit;
  }

  const PageRows* rows = pageRows(page);
  if (rows == nullptr) {
    return {CredentialKeyType::None, '\0'};
  }
  for (size_t row = 0; row < 3; ++row) {
    const int16_t rowY = static_cast<int16_t>(kRowY + row * kRowStride);
    if (point.y < rowY || point.y >= rowY + kKeyHeight) {
      continue;
    }
    for (size_t column = 0; rows->rows[row][column] != '\0'; ++column) {
      const int16_t keyX = static_cast<int16_t>(rows->x[row] + column * (kKeyWidth + kKeyGap));
      if (point.x >= keyX && point.x < keyX + kKeyWidth) {
        return {CredentialKeyType::Character, rows->rows[row][column]};
      }
    }
  }
  return {CredentialKeyType::None, '\0'};
}

const char* credentialShiftOrAbcLabel(CredentialKeyboardPage page) {
  return page == CredentialKeyboardPage::Alphabet ? "Shift" : "ABC";
}

const char* credentialPageLabel(CredentialKeyboardPage page) {
  return page == CredentialKeyboardPage::Numbers ? "#+=" :
         page == CredentialKeyboardPage::Symbols ? "123" : "123";
}
