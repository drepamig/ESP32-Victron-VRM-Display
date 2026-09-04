#include "ProvisioningPortal.h"

#include <cstdio>
#include <cstring>
#ifndef CYD_SIMULATION
#include <esp_random.h>
#endif

namespace {

void secureClear(char* value, size_t length) {
  volatile char* cursor = value;
  while (length-- > 0) {
    *cursor++ = 0;
  }
}

void secureClearString(String& value) {
  if (!value.isEmpty()) {
    secureClear(const_cast<char*>(value.c_str()), value.length());
  }
  value = String();
}

bool deadlineReached(uint32_t nowMs, uint32_t deadlineMs) {
  return static_cast<int32_t>(nowMs - deadlineMs) >= 0;
}

String escapeHtml(const char* value) {
  String escaped;
  if (value == nullptr) return escaped;
  while (*value != '\0') {
    switch (*value) {
      case '&':
        escaped += "&amp;";
        break;
      case '<':
        escaped += "&lt;";
        break;
      case '>':
        escaped += "&gt;";
        break;
      case '"':
        escaped += "&quot;";
        break;
      case '\'':
        escaped += "&#39;";
        break;
      default:
        escaped += *value;
        break;
    }
    ++value;
  }
  return escaped;
}

}  // namespace

ProvisioningPortal::ProvisioningPortal(ClientCheck clientCheck, void* context)
    : clientCheck_(clientCheck), clientContext_(context) {}

bool ProvisioningPortal::begin(const String& selectedSsid, uint8_t securityType, uint32_t nowMs) {
  const size_t ssidLength = selectedSsid.length();
  if (ssidLength == 0 || ssidLength > 32) return false;

  stopActiveSession();
  clearPendingSubmission();
  if (!handlersRegistered_) {
    server_.on("/setup", HTTP_GET, [this]() { handleGet(); });
    server_.on("/setup", HTTP_POST, [this]() { handlePost(); });
    server_.onNotFound([this]() { handleNotFound(); });
    handlersRegistered_ = true;
  }

  std::memcpy(selectedSsid_, selectedSsid.c_str(), ssidLength + 1);
  selectedSecurityType_ = securityType;
#ifdef CYD_SIMULATION
  const unsigned long code = 424242UL;
#else
  const unsigned long code = static_cast<unsigned long>(esp_random() % 1000000U);
#endif
  std::snprintf(pairingCode_, sizeof(pairingCode_), "%06lu", code);
  expiresAtMs_ = nowMs + kLifetimeMs;
  active_ = true;
  server_.begin();
  return true;
}

void ProvisioningPortal::poll(uint32_t nowMs) {
  if (!active_) return;
  if (deadlineReached(nowMs, expiresAtMs_)) {
    cancel();
    return;
  }
  server_.handleClient();
}

void ProvisioningPortal::cancel() {
  stopActiveSession();
  clearPendingSubmission();
}

bool ProvisioningPortal::active() const { return active_; }

String ProvisioningPortal::pairingCode() const {
  return active_ ? String(pairingCode_) : String();
}

uint32_t ProvisioningPortal::expiresAtMs() const { return expiresAtMs_; }

bool ProvisioningPortal::takeSubmission(CredentialSubmission& out) {
  if (!pendingReady_) return false;
  const bool transferred = out.set(pendingSsid_, pendingPassphrase_, pendingSecurityType_);
  clearPendingSubmission();
  return transferred;
}

void ProvisioningPortal::handleGet() {
  if (!requestAllowed()) {
    server_.send(403, "text/plain", "Request rejected.");
    return;
  }

  String html;
  html += "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">";
  html += "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">";
  html += "<title>WiFi setup</title></head><body><main><h1>WiFi setup</h1><p>Network: ";
  html += escapeHtml(selectedSsid_);
  html += "</p><form method=\"post\" action=\"/setup\">";
  html += "<label>Pairing code <input name=\"code\" inputmode=\"numeric\" maxlength=\"6\" required></label>";
  html += "<label>Password <input name=\"password\" type=\"password\" maxlength=\"63\"></label>";
  html += "<button type=\"submit\">Connect</button></form></main></body></html>";
  server_.send(200, "text/html", html);
}

void ProvisioningPortal::handlePost() {
  if (!requestAllowed()) {
    server_.send(403, "text/plain", "Request rejected.");
    return;
  }

  const bool hasCode = server_.hasArg("code");
  const String submittedCode = hasCode ? server_.arg("code") : String();
  if (!hasCode || submittedCode != String(pairingCode_)) {
    server_.send(400, "text/plain", "Request rejected.");
    return;
  }

  String submittedPassphrase =
      server_.hasArg("password") ? server_.arg("password") : String();
  const size_t passphraseLength = submittedPassphrase.length();
  const bool valid = credentialPassphraseValid(selectedSecurityType_,
                                               submittedPassphrase.c_str(), passphraseLength);
  if (!valid) {
    secureClearString(submittedPassphrase);
    server_.send(400, "text/plain", "Request rejected.");
    return;
  }

  const size_t ssidLength = std::strlen(selectedSsid_);
  std::memcpy(pendingSsid_, selectedSsid_, ssidLength + 1);
  std::memcpy(pendingPassphrase_, submittedPassphrase.c_str(), passphraseLength + 1);
  pendingSecurityType_ = selectedSecurityType_;
  pendingReady_ = true;
  secureClearString(submittedPassphrase);
  server_.send(200, "text/plain", "Setup accepted.");
  stopActiveSession();
}

void ProvisioningPortal::handleNotFound() {
  server_.send(requestAllowed() ? 404 : 403, "text/plain", "Request rejected.");
}

bool ProvisioningPortal::requestAllowed() {
  if (!active_ || clientCheck_ == nullptr) return false;
  const IPAddress remote = server_.client().remoteIP();
  return clientCheck_(remote, clientContext_);
}

void ProvisioningPortal::stopActiveSession() {
  if (active_) server_.stop();
  active_ = false;
  expiresAtMs_ = 0;
  clearActiveMaterial();
}

void ProvisioningPortal::clearActiveMaterial() {
  secureClear(selectedSsid_, sizeof(selectedSsid_));
  secureClear(pairingCode_, sizeof(pairingCode_));
  selectedSecurityType_ = 0;
}

void ProvisioningPortal::clearPendingSubmission() {
  secureClear(pendingSsid_, sizeof(pendingSsid_));
  secureClear(pendingPassphrase_, sizeof(pendingPassphrase_));
  pendingSecurityType_ = 0;
  pendingReady_ = false;
}
