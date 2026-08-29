#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "WebServer.h"
#include "esp_random.h"
#include "../../VictronCYD_Modbus/ProvisioningPortal.h"

namespace {

const IPAddress kAuthorizedClient(192, 168, 50, 42);
const IPAddress kUnauthorizedClient(192, 168, 51, 42);

int failures = 0;

void check(bool condition, const char* name) {
  if (!condition) {
    std::cerr << "FAIL: " << name << '\n';
    ++failures;
  }
}

WebServer& server() { return *WebServer::lastInstance(); }

std::vector<std::pair<std::string, std::string>> form(const char* code,
                                                       const char* password) {
  return {{"code", code == nullptr ? "" : code},
          {"password", password == nullptr ? "" : password}};
}

void testCodeLifetimeAndEscapedForm() {
  fakeEspRandomValue = 7;
  ProvisioningPortal portal;
  check(!server().simulateRequest(HTTP_GET, "/setup", kAuthorizedClient),
        "inactive portal must not serve requests");
  check(portal.begin("A<&\"'>", 3, 100), "valid selected network starts portal");
  check(portal.active() && portal.pairingCode() == String("000007"),
        "pairing code must be exactly six zero-padded digits");
  check(portal.expiresAtMs() == 600100, "portal lifetime must be exactly 600000 milliseconds");
  check(server().port() == 80 && server().running(), "portal must serve on the local HTTP server");

  check(server().simulateRequest(HTTP_GET, "/setup", kAuthorizedClient),
        "authorized client can load setup form");
  const std::string html = server().responseBody();
  check(server().responseCode() == 200 && server().responseContentType() == "text/html",
        "setup form must be an HTML success response");
  check(html.find("A&lt;&amp;&quot;&#39;&gt;") != std::string::npos &&
            html.find("A<&\"'>") == std::string::npos,
        "selected SSID must be HTML-escaped as untrusted text");
  check(html.find("name=\"code\"") != std::string::npos &&
            html.find("type=\"password\"") != std::string::npos &&
            html.find("000007") == std::string::npos,
        "form must request code and password without echoing the active code");
  check(html.find("<script") == std::string::npos && html.find("stylesheet") == std::string::npos &&
            html.find("http://") == std::string::npos && html.find("https://") == std::string::npos,
        "setup HTML must contain no external resources or network calls");

  portal.poll(600099);
  check(portal.active() && server().handleClientCalls() == 1,
        "portal must remain active through the millisecond before expiry");
  portal.poll(600100);
  check(!portal.active() && portal.pairingCode().isEmpty() && !server().running(),
        "portal must stop and clear its code exactly at expiry");
}

void testWraparoundSafeExpiry() {
  fakeEspRandomValue = 42;
  ProvisioningPortal portal;
  const uint32_t startedAt = UINT32_MAX - 100;
  check(portal.begin("Wrapped", 3, startedAt), "wraparound portal starts");
  check(portal.expiresAtMs() == 599899, "expiry deadline must wrap modulo uint32_t");
  portal.poll(599898);
  check(portal.active(), "wrapped portal must not expire one millisecond early");
  portal.poll(599899);
  check(!portal.active() && !server().running(), "wrapped portal must expire on its deadline");
}

void testRouteAndSubnetBoundaries() {
  fakeEspRandomValue = 123456;
  ProvisioningPortal portal;
  check(portal.begin("Boundary", 3, 0), "boundary portal starts");

  check(server().simulateRequest(HTTP_GET, "/setup", kUnauthorizedClient) &&
            server().responseCode() == 403,
        "GET outside 192.168.50.0/24 must be rejected");
  check(server().simulateRequest(HTTP_POST, "/setup", IPAddress(10, 0, 0, 2),
                                 form("123456", "secret-pass")) &&
            server().responseCode() == 403 && portal.active(),
        "POST outside 192.168.50.0/24 must not accept credentials");
  check(server().simulateRequest(HTTP_GET, "/other", kUnauthorizedClient) &&
            server().responseCode() == 403,
        "subnet boundary must be enforced before rejecting an unserved route");
  check(server().simulateRequest(HTTP_GET, "/", kAuthorizedClient) &&
            server().responseCode() == 404,
        "GET routes other than /setup must not be served");
  check(server().simulateRequest(HTTP_POST, "/other", kAuthorizedClient,
                                 form("123456", "secret-pass")) &&
            server().responseCode() == 404,
        "POST routes other than /setup must not be served");
  check(server().simulateRequest(HTTP_PUT, "/setup", kAuthorizedClient) &&
            server().responseCode() == 404,
        "methods other than GET and POST must not be served");
  portal.cancel();
}

void testCodeAndPassphraseValidation() {
  fakeEspRandomValue = 246810;
  ProvisioningPortal portal;
  check(portal.begin("Secured", 3, 0), "secured validation portal starts");

  check(server().simulateRequest(HTTP_POST, "/setup", kAuthorizedClient,
                                 {{"password", "secret-pass"}}),
        "missing-code request reaches validation");
  const std::string missingCodeBody = server().responseBody();
  check(server().responseCode() == 400 && portal.active(),
        "missing pairing code must be rejected without consuming the session");
  check(server().simulateRequest(HTTP_POST, "/setup", kAuthorizedClient,
                                 form("000000", "secret-pass")),
        "wrong-code request reaches validation");
  check(server().responseCode() == 400 && server().responseBody() == missingCodeBody &&
            server().responseBody().find("000000") == std::string::npos && portal.active(),
        "wrong code must use the same generic non-secret failure as missing code");

  check(server().simulateRequest(HTTP_POST, "/setup", kAuthorizedClient,
                                 form("246810", "")) &&
            server().responseCode() == 400 && portal.active(),
        "secured network must reject an empty passphrase");
  const std::string emptyPasswordBody = server().responseBody();
  const std::string oversizedPassword(64, 'x');
  check(server().simulateRequest(HTTP_POST, "/setup", kAuthorizedClient,
                                 form("246810", oversizedPassword.c_str())) &&
            server().responseCode() == 400 && server().responseBody() == emptyPasswordBody &&
            server().responseBody().find(oversizedPassword) == std::string::npos && portal.active(),
        "passphrases over 63 bytes must get a generic non-echoing rejection");

  check(server().simulateRequest(HTTP_POST, "/setup", kAuthorizedClient,
                                 form("246810", "valid-passphrase")) &&
            server().responseCode() == 200 && !portal.active() && !server().running(),
        "valid secured credentials must close the session and server");
  check(server().responseBody().find("valid-passphrase") == std::string::npos &&
            server().responseBody().find("246810") == std::string::npos,
        "success response must not echo the password or active code");
  ProvisioningSubmission secured;
  check(portal.takeSubmission(secured) && secured.ready && secured.ssid == String("Secured") &&
            secured.passphrase == String("valid-passphrase") && secured.securityType == 3,
        "accepted secured credentials and encryption type must transfer exactly");
}

void testOpenSubmissionAndOneShotTransfer() {
  fakeEspRandomValue = 13579;
  ProvisioningPortal portal;
  check(portal.begin("Cafe Open", 0, 500), "open-network portal starts");
  check(server().simulateRequest(HTTP_POST, "/setup", kAuthorizedClient,
                                 form("013579", "")) &&
            server().responseCode() == 200,
        "security type zero must allow an empty passphrase");
  check(!portal.active() && portal.pairingCode().isEmpty() && !server().running(),
        "accepted submission must invalidate its code and stop serving");

  ProvisioningSubmission accepted;
  check(portal.takeSubmission(accepted) && accepted.ready &&
            accepted.ssid == String("Cafe Open") && accepted.passphrase.isEmpty() &&
            accepted.securityType == 0,
        "accepted open-network data must transfer exactly to the consumer");
  ProvisioningSubmission untouched;
  untouched.ssid = "sentinel";
  check(!portal.takeSubmission(untouched) && untouched.ssid == String("sentinel"),
        "submission must be consumable exactly once");
}

void testCancelTimeoutAndRepeatedBeginClearOldState() {
  fakeEspRandomValue = 111111;
  ProvisioningPortal portal;
  check(portal.begin("Cancelled", 3, 0), "cancel test portal starts");
  portal.cancel();
  ProvisioningSubmission output;
  check(!portal.active() && portal.pairingCode().isEmpty() && !server().running() &&
            !portal.takeSubmission(output),
        "cancel must close the server and clear session material");

  fakeEspRandomValue = 222222;
  check(portal.begin("Old Active", 3, 10), "first replacement session starts");
  fakeEspRandomValue = 333333;
  check(portal.begin("New Active", 0, 20), "second begin replaces active session");
  check(portal.pairingCode() == String("333333") && portal.expiresAtMs() == 600020,
        "repeated begin must replace code and lifetime");
  check(server().simulateRequest(HTTP_POST, "/setup", kAuthorizedClient,
                                 form("222222", "old-secret")) &&
            server().responseCode() == 400 && portal.active(),
        "replaced pairing code must no longer authorize a submission");
  check(server().simulateRequest(HTTP_POST, "/setup", kAuthorizedClient,
                                 form("333333", "")) &&
            server().responseCode() == 200,
        "replacement session must use its new SSID and open security type");

  fakeEspRandomValue = 444444;
  check(portal.begin("After Accepted", 3, 30),
        "begin after acceptance starts a fresh session and discards pending submission");
  check(!portal.takeSubmission(output), "repeated begin must clear an older pending submission");
  portal.poll(600030);
  check(!portal.active() && portal.pairingCode().isEmpty() && !server().running() &&
            !portal.takeSubmission(output),
        "timeout must close and clear without producing a submission");
}

}  // namespace

int main() {
  testCodeLifetimeAndEscapedForm();
  testWraparoundSafeExpiry();
  testRouteAndSubnetBoundaries();
  testCodeAndPassphraseValidation();
  testOpenSubmissionAndOneShotTransfer();
  testCancelTimeoutAndRepeatedBeginClearOldState();
  return failures == 0 ? 0 : 1;
}
