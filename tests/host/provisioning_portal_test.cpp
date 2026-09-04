#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "WebServer.h"
#include "esp_random.h"
#include "../../VictronCYD_Modbus/CredentialSubmission.h"
#include "../../VictronCYD_Modbus/ProvisioningPortal.h"
#include "../../VictronCYD_Modbus/GatewayApplicationPolicy.h"

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

void testCodeValidation() {
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
  portal.cancel();
}

void testProtectedPassphraseBoundariesAndPrintableBytes() {
  fakeEspRandomValue = 246810;
  ProvisioningPortal portal;
  const char* const tooShort[] = {"", "x", "xx", "xxx", "xxxx", "xxxxx", "xxxxxx",
                                 "xxxxxxx"};
  std::string genericFailureBody;
  for (size_t index = 0; index < sizeof(tooShort) / sizeof(tooShort[0]); ++index) {
    check(portal.begin("BoundaryNet", 3, static_cast<uint32_t>(index)),
          "protected rejection session starts");
    check(server().simulateRequest(HTTP_POST, "/setup", kAuthorizedClient,
                                   form("246810", tooShort[index])) &&
              server().responseCode() == 400 && portal.active(),
          "protected lengths zero through seven must be rejected");
    if (index == 0) genericFailureBody = server().responseBody();
    check(server().responseBody() == genericFailureBody,
          "protected short lengths must use one generic rejection");
  }

  check(portal.begin("BoundaryNet", 3, 10), "protected oversized session starts");
  const std::string oversizedPassword(64, 'x');
  check(server().simulateRequest(HTTP_POST, "/setup", kAuthorizedClient,
                                 form("246810", oversizedPassword.c_str())) &&
            server().responseCode() == 400 && server().responseBody() == genericFailureBody &&
            server().responseBody().find(oversizedPassword) == std::string::npos && portal.active(),
        "protected length 64 must be rejected generically without echoing input");

  check(portal.begin("BoundaryNet", 3, 11), "protected control-byte session starts");
  const std::string newlinePassword = "A1!A1!A\n";
  check(server().simulateRequest(HTTP_POST, "/setup", kAuthorizedClient,
                                 form("246810", newlinePassword.c_str())) &&
            server().responseCode() == 400 && server().responseBody() == genericFailureBody &&
            portal.active(),
        "protected control bytes must be rejected generically");
  check(portal.begin("BoundaryNet", 3, 12), "protected delete-byte session starts");
  const std::string deletePassword = "A1!A1!A\x7f";
  check(server().simulateRequest(HTTP_POST, "/setup", kAuthorizedClient,
                                 form("246810", deletePassword.c_str())) &&
            server().responseCode() == 400 && server().responseBody() == genericFailureBody &&
            portal.active(),
        "protected delete bytes must be rejected generically");

  check(portal.begin("BoundaryNet", 3, 13), "protected minimum session starts");
  check(server().simulateRequest(HTTP_POST, "/setup", kAuthorizedClient,
                                 form("246810", "A1!A1!A1")) &&
            server().responseCode() == 200 && !portal.active() && !server().running(),
        "protected length eight must be accepted");
  CredentialSubmission acceptedMinimum;
  check(portal.takeSubmission(acceptedMinimum) && acceptedMinimum.ready &&
            std::string(acceptedMinimum.passphrase) == "A1!A1!A1",
        "accepted protected minimum must transfer through the shared structure");
  acceptedMinimum.clear();

  fakeEspRandomValue = 975310;
  check(portal.begin("BoundaryNet", 3, 100), "protected maximum portal starts");
  const std::string maximumPassword(63, 'x');
  check(server().simulateRequest(HTTP_POST, "/setup", kAuthorizedClient,
                                 form("975310", maximumPassword.c_str())) &&
            server().responseCode() == 200 && !portal.active(),
        "protected length 63 must be accepted");
  CredentialSubmission acceptedMaximum;
  check(portal.takeSubmission(acceptedMaximum) && acceptedMaximum.ready &&
            std::string(acceptedMaximum.passphrase) == maximumPassword,
        "accepted protected maximum must transfer without truncation");
  acceptedMaximum.clear();
}

void testSharedSubmissionContractAndOneShotTransfer() {
  fakeEspRandomValue = 13579;
  ProvisioningPortal portal;
  check(portal.begin("SyntheticNet", 3, 500), "shared-submission portal starts");
  check(server().simulateRequest(HTTP_POST, "/setup", kAuthorizedClient,
                                 form("013579", "A1!A1!A1!")) &&
            server().responseCode() == 200,
        "valid protected submission must be accepted");
  check(!portal.active() && portal.pairingCode().isEmpty() && !server().running(),
        "accepted submission must invalidate its code and stop serving");
  check(server().responseBody().find("A1!A1!A1!") == std::string::npos &&
            server().responseBody().find("013579") == std::string::npos,
        "success response must not echo submitted material");

  CredentialSubmission accepted;
  check(portal.takeSubmission(accepted) && accepted.ready &&
            std::string(accepted.ssid) == "SyntheticNet" &&
            std::string(accepted.passphrase) == "A1!A1!A1!" && accepted.securityType == 3,
        "portal transfers one bounded shared submission");
  accepted.clear();
  CredentialSubmission untouched;
  check(untouched.set("SentinelNet", "A1!A1!A1!", 3), "one-shot sentinel initializes");
  check(!portal.takeSubmission(untouched) && std::string(untouched.ssid) == "SentinelNet",
        "submission must be consumable exactly once");
  untouched.clear();
}

void testOpenNetworksAcceptOnlyEmptyPasswords() {
  fakeEspRandomValue = 112233;
  ProvisioningPortal portal;
  check(portal.begin("Cafe Open", 0, 500), "open-network portal starts");
  check(server().simulateRequest(HTTP_POST, "/setup", kAuthorizedClient,
                                 form("112233", "x")) &&
            server().responseCode() == 400 && portal.active(),
        "open networks must reject a non-empty password");
  const std::string genericFailureBody = server().responseBody();
  check(genericFailureBody.find("x") == std::string::npos,
        "open-network rejection must not echo submitted material");
  check(server().simulateRequest(HTTP_POST, "/setup", kAuthorizedClient,
                                 form("112233", "")) &&
            server().responseCode() == 200 && !portal.active(),
        "open networks must accept an empty password");
  CredentialSubmission accepted;
  check(portal.takeSubmission(accepted) && accepted.ready &&
            std::string(accepted.ssid) == "Cafe Open" && accepted.passphrase[0] == '\0' &&
            accepted.securityType == 0,
        "accepted open-network data must transfer through the shared structure");
  accepted.clear();
}

void testCancelTimeoutAndRepeatedBeginClearOldState() {
  fakeEspRandomValue = 111111;
  ProvisioningPortal portal;
  check(portal.begin("Cancelled", 3, 0), "cancel test portal starts");
  portal.cancel();
  CredentialSubmission output;
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

  fakeEspRandomValue = 555555;
  check(portal.begin("PendingCancel", 3, 40), "pending-cancel portal starts");
  check(server().simulateRequest(HTTP_POST, "/setup", kAuthorizedClient,
                                 form("555555", "A1!A1!A1!")) &&
            server().responseCode() == 200,
        "pending-cancel fixture is accepted");
  portal.cancel();
  check(!portal.takeSubmission(output), "cancel must clear an older pending submission");
  output.clear();
}

}  // namespace

int main() {
  {
    ProvisioningPortal portal;
    CredentialSubmission submission;
    bool physicalPortalActive = true;
    int submitted = 0, expired = 0;
    auto dispatch = [&] {
      coordinatePortalLifecycle(false, physicalPortalActive, portal, submission,
          [&](CredentialSubmission&) { ++submitted; physicalPortalActive = false; },
          [&] { ++expired; physicalPortalActive = false; });
    };
    check(portal.begin("Expiry fixture", 3, 100), "navigation expiry portal starts");
    portal.poll(600100);  // Production loop order: poll before lifecycle/UI handling.
    dispatch();
    dispatch();
    check(expired == 1 && submitted == 0 && !physicalPortalActive,
          "real portal expiry routes once to automatic exit before UI polling");
    physicalPortalActive = true;
    check(portal.begin("Submitted fixture", 3, 700000), "submission navigation portal starts");
    check(server().simulateRequest(HTTP_POST, "/setup", kAuthorizedClient,
                                   form(portal.pairingCode().c_str(), "dummy-password")),
          "valid phone POST is accepted");
    portal.poll(700001);
    dispatch();
    check(submitted == 1 && expired == 1 && submission.ready,
          "accepted POST stops the portal but must be consumed before expiry routing");
    submission.clear();
  }
  testCodeLifetimeAndEscapedForm();
  testWraparoundSafeExpiry();
  testRouteAndSubnetBoundaries();
  testCodeValidation();
  testProtectedPassphraseBoundariesAndPrintableBytes();
  testSharedSubmissionContractAndOneShotTransfer();
  testOpenNetworksAcceptOnlyEmptyPasswords();
  testCancelTimeoutAndRepeatedBeginClearOldState();
  return failures == 0 ? 0 : 1;
}
