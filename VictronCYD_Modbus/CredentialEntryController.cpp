#include "CredentialEntryController.h"

namespace {

bool copyBounded(char* output, size_t capacity, const char* input, size_t& length) {
  if (output == nullptr || capacity == 0 || input == nullptr) return false;
  for (length = 0; length < capacity; ++length) {
    if (input[length] == '\0') {
      output[length] = '\0';
      return true;
    }
    if (length + 1 == capacity) return false;
    output[length] = input[length];
  }
  return false;
}

bool printableAscii(char value) {
  const uint8_t byte = static_cast<uint8_t>(value);
  return byte >= 0x20 && byte <= 0x7e;
}

}  // namespace

bool credentialPassphraseValid(uint8_t securityType, const char* passphrase, size_t length) {
  if (passphrase == nullptr) return false;
  if (securityType == 0) return length == 0;
  if (length < 8 || length > 63) return false;
  for (size_t index = 0; index < length; ++index) {
    if (!printableAscii(passphrase[index])) return false;
  }
  return true;
}

bool CredentialSubmission::set(const char* selectedSsid, const char* selectedPassphrase,
                               uint8_t selectedSecurityType) {
  clear();
  size_t ssidLength = 0;
  size_t passphraseLength = 0;
  if (!copyBounded(ssid, sizeof(ssid), selectedSsid, ssidLength) || ssidLength == 0 ||
      !copyBounded(passphrase, sizeof(passphrase), selectedPassphrase, passphraseLength) ||
      !credentialPassphraseValid(selectedSecurityType, passphrase, passphraseLength)) {
    clear();
    return false;
  }
  securityType = selectedSecurityType;
  ready = true;
  return true;
}

void CredentialSubmission::clear() {
  secureClearBytes(ssid, sizeof(ssid));
  secureClearBytes(passphrase, sizeof(passphrase));
  securityType = 0;
  ready = false;
}

bool CredentialEntryController::begin(const char* ssid, uint8_t securityType, uint32_t nowMs) {
  cancel();
  size_t ssidLength = 0;
  if (!copyBounded(ssid_, sizeof(ssid_), ssid, ssidLength) || ssidLength == 0) {
    cancel();
    return false;
  }
  securityType_ = securityType;
  active_ = true;
  recordActivity(nowMs);
  return true;
}

bool CredentialEntryController::append(char value, uint32_t nowMs) {
  if (!active_ || connecting_ || length_ >= sizeof(password_) - 1 || !printableAscii(value)) {
    return false;
  }
  password_[length_++] = value;
  password_[length_] = '\0';
  recordActivity(nowMs);
  return true;
}

bool CredentialEntryController::backspace(uint32_t nowMs) {
  if (!active_ || connecting_ || length_ == 0) return false;
  password_[--length_] = '\0';
  recordActivity(nowMs);
  return true;
}

bool CredentialEntryController::toggleShift(uint32_t nowMs) {
  if (!active_ || connecting_) return false;
  uppercase_ = !uppercase_;
  recordActivity(nowMs);
  return true;
}

bool CredentialEntryController::selectPage(CredentialKeyboardPage page, uint32_t nowMs) {
  if (!active_ || connecting_) return false;
  page_ = page;
  recordActivity(nowMs);
  return true;
}

bool CredentialEntryController::toggleVisibility(uint32_t nowMs) {
  if (!active_ || connecting_) return false;
  visible_ = !visible_;
  recordActivity(nowMs);
  return true;
}

bool CredentialEntryController::submit(uint32_t nowMs) {
  if (!canSubmit()) return false;
  connecting_ = true;
  submissionReady_ = true;
  error_ = false;
  recordActivity(nowMs);
  return true;
}

bool CredentialEntryController::takeSubmission(CredentialSubmission& out) {
  if (!submissionReady_) return false;
  if (!out.set(ssid_, password_, securityType_)) return false;
  submissionReady_ = false;
  return true;
}

void CredentialEntryController::connectionFailed(uint32_t nowMs) {
  if (!active_) return;
  connecting_ = false;
  submissionReady_ = false;
  visible_ = false;
  error_ = true;
  recordActivity(nowMs);
}

void CredentialEntryController::succeed() { cancel(); }

void CredentialEntryController::cancel() {
  secureClearBytes(ssid_, sizeof(ssid_));
  secureClearBytes(password_, sizeof(password_));
  securityType_ = 0;
  lastActivityMs_ = 0;
  length_ = 0;
  active_ = false;
  connecting_ = false;
  visible_ = false;
  uppercase_ = false;
  error_ = false;
  submissionReady_ = false;
  page_ = CredentialKeyboardPage::Alphabet;
}

bool CredentialEntryController::pollTimeout(uint32_t nowMs) {
  if (!active_ || connecting_ || static_cast<uint32_t>(nowMs - lastActivityMs_) < kInactivityMs) {
    return false;
  }
  cancel();
  return true;
}

size_t CredentialEntryController::copyDisplayText(char* output, size_t capacity) const {
  if (output != nullptr && capacity > 0) {
    const size_t copied = length_ < capacity - 1 ? length_ : capacity - 1;
    for (size_t index = 0; index < copied; ++index) {
      output[index] = visible_ ? password_[index] : '*';
    }
    output[copied] = '\0';
  }
  return length_;
}

bool CredentialEntryController::active() const { return active_; }
bool CredentialEntryController::connecting() const { return connecting_; }
bool CredentialEntryController::visible() const { return visible_; }
bool CredentialEntryController::uppercase() const { return uppercase_; }
bool CredentialEntryController::hasError() const { return error_; }
bool CredentialEntryController::canSubmit() const {
  return active_ && !connecting_ && credentialPassphraseValid(securityType_, password_, length_);
}
size_t CredentialEntryController::length() const { return length_; }
const char* CredentialEntryController::ssid() const { return ssid_; }
uint8_t CredentialEntryController::securityType() const { return securityType_; }
CredentialKeyboardPage CredentialEntryController::page() const { return page_; }

void CredentialEntryController::recordActivity(uint32_t nowMs) { lastActivityMs_ = nowMs; }
