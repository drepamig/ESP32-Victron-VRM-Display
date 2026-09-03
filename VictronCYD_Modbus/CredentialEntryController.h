#pragma once

#include <cstddef>
#include <cstdint>

#include "CredentialSubmission.h"

enum class CredentialKeyboardPage : uint8_t { Alphabet, Numbers, Symbols };

class CredentialEntryController {
 public:
  static constexpr uint32_t kInactivityMs = 300000;
  ~CredentialEntryController() { cancel(); }
  bool begin(const char* ssid, uint8_t securityType, uint32_t nowMs);
  bool append(char value, uint32_t nowMs);
  bool backspace(uint32_t nowMs);
  bool toggleShift(uint32_t nowMs);
  bool selectPage(CredentialKeyboardPage page, uint32_t nowMs);
  bool toggleVisibility(uint32_t nowMs);
  bool submit(uint32_t nowMs);
  bool takeSubmission(CredentialSubmission& out);
  void connectionFailed(uint32_t nowMs);
  void succeed();
  void cancel();
  bool pollTimeout(uint32_t nowMs);
  size_t copyDisplayText(char* output, size_t capacity) const;
  bool active() const;
  bool connecting() const;
  bool visible() const;
  bool uppercase() const;
  bool hasError() const;
  bool canSubmit() const;
  size_t length() const;
  const char* ssid() const;
  uint8_t securityType() const;
  CredentialKeyboardPage page() const;

 private:
  void recordActivity(uint32_t nowMs);

  char ssid_[33] = {};
  char password_[64] = {};
  uint8_t securityType_ = 0;
  uint32_t lastActivityMs_ = 0;
  size_t length_ = 0;
  bool active_ = false;
  bool connecting_ = false;
  bool visible_ = false;
  bool uppercase_ = false;
  bool error_ = false;
  bool submissionReady_ = false;
  CredentialKeyboardPage page_ = CredentialKeyboardPage::Alphabet;
};
