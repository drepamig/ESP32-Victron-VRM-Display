#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <type_traits>

#include "CredentialEntryController.h"
#include "CredentialSubmission.h"

namespace {

void check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

void testPassphraseValidationBoundariesAndAscii() {
  const std::string seven(7, 'A');
  const std::string eight(8, 'A');
  const std::string sixtyThree(63, 'A');
  const std::string sixtyFour(64, 'A');
  const std::string empty;
  const std::string newline = "ABCD\nEFG";
  const std::string deleteByte = std::string("ABCDEFG") + static_cast<char>(0x7f);
  struct ValidationCase {
    uint8_t securityType;
    const std::string* value;
    bool expected;
  };
  const ValidationCase cases[] = {
      {3, &seven, false}, {3, &eight, true}, {3, &sixtyThree, true}, {3, &sixtyFour, false},
      {0, &empty, true},
  };
  for (const ValidationCase& test : cases) {
    check(credentialPassphraseValid(test.securityType, test.value->c_str(), test.value->size()) ==
              test.expected,
          "passphrase boundary validation must match the fixed credential contract");
  }
  check(credentialPassphraseValid(0, empty.c_str(), empty.size()),
        "open networks must accept an empty passphrase");
  check(!credentialPassphraseValid(0, "A", 1),
        "open networks must reject non-empty passphrases");
  check(!credentialPassphraseValid(3, newline.c_str(), newline.size()),
        "protected passphrases must reject control bytes");
  check(!credentialPassphraseValid(3, deleteByte.c_str(), deleteByte.size()),
        "protected passphrases must reject bytes outside printable ASCII");
}

void testSubmissionIsBoundedNoncopyableAndClearable() {
  static_assert(!std::is_copy_constructible<CredentialSubmission>::value,
                "credential submissions must not be copied");
  static_assert(!std::is_copy_assignable<CredentialSubmission>::value,
                "credential submissions must not be copy-assigned");

  CredentialSubmission submission;
  check(submission.set("BoundedNet", "A1!A1!A1!", 3),
        "valid selected values must populate fixed submission storage");
  check(submission.ready && std::string(submission.ssid) == "BoundedNet" &&
            std::string(submission.passphrase) == "A1!A1!A1!" && submission.securityType == 3,
        "submission must retain exact selected values in bounded storage");
  submission.clear();
  check(!submission.ready && submission.ssid[0] == '\0' && submission.passphrase[0] == '\0' &&
            submission.securityType == 0,
        "clear must remove ready credential data");
  submission.clear();
  check(!submission.ready && submission.ssid[0] == '\0' && submission.passphrase[0] == '\0',
        "clear must be idempotent");
}

void testEntryDefaultsAndEditingContract() {
  CredentialEntryController entry;
  check(entry.begin("SyntheticNet", 3, 100), "protected selection starts entry");
  check(entry.active() && !entry.connecting() && !entry.visible() && !entry.uppercase() &&
            !entry.hasError() && entry.length() == 0 && entry.page() == CredentialKeyboardPage::Alphabet,
        "new protected entry starts lowercase, masked, empty, and on alphabet page");
  check(std::string(entry.ssid()) == "SyntheticNet" && entry.securityType() == 3,
        "active entry retains its selected network metadata");
  check(entry.append('A', 101) && entry.append(' ', 102) && entry.append('1', 103),
        "printable characters including space append in order");
  char display[8] = {};
  check(entry.copyDisplayText(display, sizeof(display)) == 3 && std::string(display) == "***",
        "masked display must expose only one marker per entered byte");
  check(entry.toggleVisibility(104) && entry.visible() &&
            entry.copyDisplayText(display, sizeof(display)) == 3 && std::string(display) == "A 1",
        "visibility toggle must expose the exact editable text");
  check(entry.backspace(105) && entry.length() == 2 && entry.backspace(106) && entry.backspace(107) &&
            !entry.backspace(108),
        "backspace must remove exactly one byte and reject an empty buffer");
  check(!entry.append('\n', 109) && entry.length() == 0,
        "append must reject non-printable bytes without changing the entry");
}

void testEntryCapacityAndKeyboardState() {
  CredentialEntryController entry;
  check(entry.begin("CapacityNet", 3, 1), "capacity test starts an entry");
  for (size_t index = 0; index < 63; ++index) {
    check(entry.append('A', static_cast<uint32_t>(2 + index)),
          "each byte through the 63-byte cap must append");
  }
  check(entry.length() == 63 && !entry.append('B', 100),
        "the controller must reject a 64th password byte");
  check(entry.toggleShift(101) && entry.uppercase(), "shift must toggle uppercase state");
  check(entry.selectPage(CredentialKeyboardPage::Numbers, 102) &&
            entry.page() == CredentialKeyboardPage::Numbers,
        "page selection must retain the selected numeric page");
  check(entry.selectPage(CredentialKeyboardPage::Symbols, 103) &&
            entry.page() == CredentialKeyboardPage::Symbols,
        "page selection must retain the selected symbols page");
}

void testSubmissionIsOneShotAndConnectionFailureRetainsMaskedEntry() {
  CredentialEntryController entry;
  check(entry.begin("SyntheticNet", 3, 100), "protected selection starts entry");
  for (char value : std::string("A1!A1!A1!")) {
    check(entry.append(value, 101), "printable synthetic byte appends");
  }
  check(entry.toggleVisibility(199) && entry.visible(),
        "test setup exposes the entry before the connection attempt");
  CredentialSubmission submission;
  check(entry.submit(200) && !entry.visible() && entry.takeSubmission(submission),
        "submission immediately masks the retained entry before Connecting renders");
  check(submission.ready && std::string(submission.ssid) == "SyntheticNet" &&
            std::string(submission.passphrase) == "A1!A1!A1!" && submission.securityType == 3,
        "submission preserves exact selected values");
  check(!entry.submit(201) && !entry.takeSubmission(submission),
        "connecting entry cannot submit twice or re-emit a submission");
  submission.clear();
  entry.connectionFailed(1000);
  check(entry.active() && !entry.connecting() && !entry.visible() && entry.length() == 9 && entry.hasError(),
        "connection failure retains only masked editable state");
  check(entry.copyDisplayText(nullptr, 0) == 9,
        "display length remains available without exposing retained credentials");
}

void testOpenNetworkRequiresEmptyPassword() {
  CredentialEntryController entry;
  CredentialSubmission submission;
  check(entry.begin("OpenNet", 0, 10) && entry.canSubmit(),
        "open network starts with an immediately valid empty passphrase");
  check(entry.submit(11) && entry.takeSubmission(submission) && submission.ready &&
            submission.passphrase[0] == '\0' && submission.securityType == 0,
        "open network submission must contain an empty passphrase");

  check(entry.begin("OpenNet", 0, 20) && entry.append('A', 21) && !entry.canSubmit() &&
            !entry.submit(22),
        "open network entry must reject any non-empty passphrase");
}

void testTimeoutClearsAtDeadlineAndAcrossWraparound() {
  CredentialEntryController ordinary;
  check(ordinary.begin("TimeoutNet", 3, 100) && ordinary.append('A', 100),
        "ordinary timeout test starts a non-empty entry");
  check(!ordinary.pollTimeout(300099) && ordinary.active(),
        "timeout does not fire before the 300000 ms deadline");
  check(ordinary.pollTimeout(300100) && !ordinary.active() && ordinary.length() == 0,
        "timeout clears an entry exactly at its deadline");

  CredentialEntryController timeout;
  check(timeout.begin("TimeoutNet", 3, std::numeric_limits<uint32_t>::max() - 100),
        "wrapped entry starts");
  check(!timeout.pollTimeout(299898) && timeout.active(), "timeout does not fire early");
  check(timeout.pollTimeout(299899) && !timeout.active(), "timeout fires at 300000 ms");
}

void testConnectingDoesNotTimeoutAndTerminalPathsClear() {
  CredentialEntryController entry;
  CredentialSubmission submission;
  check(entry.begin("ClearNet", 3, 1), "clear test starts entry");
  for (char value : std::string("A1!A1!A1!")) entry.append(value, 2);
  check(entry.submit(3) && entry.takeSubmission(submission) && !entry.pollTimeout(400000) &&
            entry.active() && entry.connecting(),
        "a connecting entry must not time out while its request is in flight");
  entry.succeed();
  check(!entry.active() && entry.length() == 0 && entry.ssid()[0] == '\0',
        "success must clear retained entry data");

  check(entry.begin("CancelNet", 3, 4) && entry.append('A', 5), "cancel test starts entry");
  entry.cancel();
  check(!entry.active() && entry.length() == 0 && entry.ssid()[0] == '\0',
        "cancel must clear retained entry data");

  check(entry.begin("FallbackNet", 3, 6) && entry.append('A', 7), "fallback test starts entry");
  check(!entry.begin(nullptr, 3, 8) && !entry.active() && entry.length() == 0 && entry.ssid()[0] == '\0',
        "an invalid selection fallback must clear retained entry data");
}

}  // namespace

int main() {
  testPassphraseValidationBoundariesAndAscii();
  testSubmissionIsBoundedNoncopyableAndClearable();
  testEntryDefaultsAndEditingContract();
  testEntryCapacityAndKeyboardState();
  testSubmissionIsOneShotAndConnectionFailureRetainsMaskedEntry();
  testOpenNetworkRequiresEmptyPassword();
  testTimeoutClearsAtDeadlineAndAcrossWraparound();
  testConnectingDoesNotTimeoutAndTerminalPathsClear();
  return 0;
}
