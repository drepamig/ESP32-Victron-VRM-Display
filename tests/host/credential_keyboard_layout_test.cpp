#include <cstdlib>
#include <iostream>
#include <string>

#include "CredentialEntryController.h"
#include "CredentialKeyboardLayout.h"

namespace {

void check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

TouchPoint keyCenter(int16_t x, int16_t y, size_t column) {
  return {static_cast<int16_t>(x + static_cast<int16_t>(column) * 31 + 14),
          static_cast<int16_t>(y + 12)};
}

void checkRows(CredentialKeyboardPage page, const char* const expected[3],
               const int16_t rowX[3]) {
  for (size_t row = 0; row < 3; ++row) {
    const CredentialKeyboardRow actual = credentialKeyboardRow(page, row);
    check(std::string(actual.characters) == expected[row],
          "each keyboard row must retain its approved literal characters");
    check(actual.x == rowX[row] && actual.y == static_cast<int16_t>(76 + row * 27) &&
              actual.keyWidth == 29 && actual.keyHeight == 24 && actual.gap == 2,
          "each keyboard row must expose the fixed geometry");
  }
}

void checkCharacters(CredentialKeyboardPage page, const char* const expected[3],
                     const int16_t rowX[3], bool uppercase) {
  (void)uppercase;
  for (size_t row = 0; row < 3; ++row) {
    const std::string characters = expected[row];
    for (size_t column = 0; column < characters.size(); ++column) {
      const CredentialKeyHit hit = credentialKeyboardHitTest(
          page, keyCenter(rowX[row], static_cast<int16_t>(76 + row * 27), column));
      check(hit.type == CredentialKeyType::Character && hit.character == characters[column],
            "literal center of every character key must return that key");
    }
  }
}

void testRowsAndCharacterCenters() {
  const char* expectedAlphabet[] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};
  const char* expectedNumbers[] = {"1234567890", "-/:;()$&@\"", ".,?!'#%"};
  const char* expectedSymbols[] = {"*+=<>[]{}\\", "^_`|~", ""};
  const int16_t rowX[] = {6, 21, 52};

  checkRows(CredentialKeyboardPage::Alphabet, expectedAlphabet, rowX);
  checkRows(CredentialKeyboardPage::Numbers, expectedNumbers, rowX);
  checkRows(CredentialKeyboardPage::Symbols, expectedSymbols, rowX);
  checkCharacters(CredentialKeyboardPage::Alphabet, expectedAlphabet, rowX, false);
  checkCharacters(CredentialKeyboardPage::Alphabet, expectedAlphabet, rowX, true);
  checkCharacters(CredentialKeyboardPage::Numbers, expectedNumbers, rowX, false);
  checkCharacters(CredentialKeyboardPage::Symbols, expectedSymbols, rowX, false);
}

void testPrintableAsciiCoverage() {
  const char* expectedAlphabet[] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};
  const char* expectedNumbers[] = {"1234567890", "-/:;()$&@\"", ".,?!'#%"};
  const char* expectedSymbols[] = {"*+=<>[]{}\\", "^_`|~", ""};
  const int16_t rowX[] = {6, 21, 52};
  bool seen[95] = {};
  size_t count = 0;
  for (CredentialKeyboardPage page : {CredentialKeyboardPage::Alphabet,
                                      CredentialKeyboardPage::Numbers,
                                      CredentialKeyboardPage::Symbols}) {
    const char* const* expected = page == CredentialKeyboardPage::Alphabet
                                      ? expectedAlphabet
                                      : page == CredentialKeyboardPage::Numbers ? expectedNumbers
                                                                                 : expectedSymbols;
    for (size_t row = 0; row < 3; ++row) {
      const std::string characters = expected[row];
      for (size_t column = 0; column < characters.size(); ++column) {
        const CredentialKeyHit hit = credentialKeyboardHitTest(
            page, keyCenter(rowX[row], static_cast<int16_t>(76 + row * 27), column));
        check(hit.type == CredentialKeyType::Character && hit.character >= 0x20 &&
                  hit.character <= 0x7e,
              "alternate pages must expose printable ASCII characters");
        check(!seen[static_cast<unsigned char>(hit.character) - 0x20],
              "alternate pages must not duplicate a printable character");
        seen[static_cast<unsigned char>(hit.character) - 0x20] = true;
        ++count;
        if (page == CredentialKeyboardPage::Alphabet) {
          const char uppercase = static_cast<char>(hit.character - 'a' + 'A');
          check(!seen[static_cast<unsigned char>(uppercase) - 0x20],
                "Shift must expose each uppercase printable character");
          seen[static_cast<unsigned char>(uppercase) - 0x20] = true;
          ++count;
        }
      }
    }
  }
  check(count == 94, "keyboard pages must expose every non-space printable ASCII byte");
  for (size_t index = 0; index < 95; ++index) {
    check(seen[index] || index == (' ' - 0x20),
          "alternate pages plus Space must cover printable ASCII exactly");
  }
  const CredentialKeyHit space = credentialKeyboardHitTest(CredentialKeyboardPage::Numbers,
                                                             {186, 172});
  check(space.type == CredentialKeyType::Space && space.character == ' ',
        "Space must complete printable ASCII coverage");
}

void testUtilityLabelsAndControlCenters() {
  const CredentialKeyRect expectedBack{4, 4, 56, 28};
  const CredentialKeyRect expectedShow{252, 4, 64, 28};
  const CredentialKeyRect expectedShift{4, 157, 54, 30};
  const CredentialKeyRect expectedPage{62, 157, 54, 30};
  const CredentialKeyRect expectedSpace{120, 157, 132, 30};
  const CredentialKeyRect expectedBackspace{256, 157, 60, 30};
  const CredentialKeyRect expectedUsePhone{4, 191, 151, 45};
  const CredentialKeyRect expectedConnect{164, 191, 152, 45};
  const auto sameRect = [](const CredentialKeyRect& actual, const CredentialKeyRect& expected) {
    return actual.x == expected.x && actual.y == expected.y && actual.width == expected.width &&
           actual.height == expected.height;
  };
  check(sameRect(kCredentialBackBounds, expectedBack) &&
            sameRect(kCredentialShowBounds, expectedShow) &&
            sameRect(kCredentialShiftOrAbcBounds, expectedShift) &&
            sameRect(kCredentialPageBounds, expectedPage) &&
            sameRect(kCredentialSpaceBounds, expectedSpace) &&
            sameRect(kCredentialBackspaceBounds, expectedBackspace) &&
            sameRect(kCredentialUsePhoneBounds, expectedUsePhone) &&
            sameRect(kCredentialConnectBounds, expectedConnect),
        "all utility bounds must retain the approved literal geometry");
  const CredentialKeyboardPage pages[] = {CredentialKeyboardPage::Alphabet,
                                           CredentialKeyboardPage::Numbers,
                                           CredentialKeyboardPage::Symbols};
  for (CredentialKeyboardPage page : pages) {
    check(credentialKeyboardHitTest(page, {32, 18}).type == CredentialKeyType::Back,
          "Back center must be available on every page");
    check(credentialKeyboardHitTest(page, {284, 18}).type == CredentialKeyType::Show,
          "Show center must be available on every page");
    check(credentialKeyboardHitTest(page, {31, 172}).type == CredentialKeyType::ShiftOrAbc,
          "left mode center must be available on every page");
    check(credentialKeyboardHitTest(page, {89, 172}).type == CredentialKeyType::Page,
          "page mode center must be available on every page");
    check(credentialKeyboardHitTest(page, {186, 172}).type == CredentialKeyType::Space,
          "Space center must be available on every page");
    check(credentialKeyboardHitTest(page, {286, 172}).type == CredentialKeyType::Backspace,
          "Backspace center must be available on every page");
    check(credentialKeyboardHitTest(page, {79, 213}).type == CredentialKeyType::UsePhone,
          "Use phone center must be available on every page");
    check(credentialKeyboardHitTest(page, {240, 213}).type == CredentialKeyType::Connect,
          "Connect center must be available on every page");
  }
  check(std::string(credentialShiftOrAbcLabel(CredentialKeyboardPage::Alphabet)) == "Shift" &&
            std::string(credentialPageLabel(CredentialKeyboardPage::Alphabet)) == "123" &&
            std::string(credentialShiftOrAbcLabel(CredentialKeyboardPage::Numbers)) == "ABC" &&
            std::string(credentialPageLabel(CredentialKeyboardPage::Numbers)) == "#+=" &&
            std::string(credentialShiftOrAbcLabel(CredentialKeyboardPage::Symbols)) == "ABC" &&
            std::string(credentialPageLabel(CredentialKeyboardPage::Symbols)) == "123",
        "mode labels must describe the fixed page transitions");
}

void testBoundariesAndNonOverlap() {
  const CredentialKeyboardPage pages[] = {CredentialKeyboardPage::Alphabet,
                                           CredentialKeyboardPage::Numbers,
                                           CredentialKeyboardPage::Symbols};
  for (CredentialKeyboardPage page : pages) {
    for (size_t row = 0; row < 3; ++row) {
      const CredentialKeyboardRow geometry = credentialKeyboardRow(page, row);
      for (size_t column = 0; geometry.characters[column] != '\0'; ++column) {
        const int16_t left = static_cast<int16_t>(geometry.x + column * 31);
        const int16_t right = static_cast<int16_t>(left + geometry.keyWidth);
        check(credentialKeyboardHitTest(page, {left, geometry.y}).type ==
                  CredentialKeyType::Character,
              "a key top-left boundary must belong to that key");
        check(credentialKeyboardHitTest(page, {right, geometry.y}).type !=
                  CredentialKeyType::Character || column + 1 >= std::string(geometry.characters).size(),
              "adjacent key rectangles must not overlap");
      }
    }
    check(credentialKeyboardHitTest(page, {-1, 120}).type == CredentialKeyType::None,
          "point immediately outside left screen edge must be None");
    check(credentialKeyboardHitTest(page, {320, 120}).type == CredentialKeyType::None,
          "point immediately outside right screen edge must be None");
    check(credentialKeyboardHitTest(page, {160, -1}).type == CredentialKeyType::None,
          "point immediately outside top screen edge must be None");
    check(credentialKeyboardHitTest(page, {160, 240}).type == CredentialKeyType::None,
          "point immediately outside bottom screen edge must be None");
  }
}

}  // namespace

int main() {
  testRowsAndCharacterCenters();
  testPrintableAsciiCoverage();
  testUtilityLabelsAndControlCenters();
  testBoundariesAndNonOverlap();
  return 0;
}
