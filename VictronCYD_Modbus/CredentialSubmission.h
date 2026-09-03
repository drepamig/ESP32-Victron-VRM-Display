#pragma once

#include <cstddef>
#include <cstdint>

inline void secureClearBytes(void* value, size_t length) {
  volatile uint8_t* cursor = static_cast<volatile uint8_t*>(value);
  while (length-- > 0) *cursor++ = 0;
}

struct CredentialSubmission {
  char ssid[33] = {};
  char passphrase[64] = {};
  uint8_t securityType = 0;
  bool ready = false;

  CredentialSubmission() = default;
  CredentialSubmission(const CredentialSubmission&) = delete;
  CredentialSubmission& operator=(const CredentialSubmission&) = delete;
  ~CredentialSubmission() { clear(); }

  bool set(const char* selectedSsid, const char* selectedPassphrase, uint8_t selectedSecurityType);
  void clear();
};

bool credentialPassphraseValid(uint8_t securityType, const char* passphrase, size_t length);
