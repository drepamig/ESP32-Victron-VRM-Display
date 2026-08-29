#include <cstdint>
#include <iostream>

#include "../../VictronCYD_Modbus/GatewayApplicationPolicy.h"

namespace {

int failures = 0;

void check(bool condition, const char* name) {
  if (!condition) {
    std::cerr << "FAIL: " << name << '\n';
    ++failures;
  }
}

void testUnknownNetworkRouting() {
  check(provisioningRouteForSecurity(0) == ProvisioningRoute::DirectPending,
        "unknown open network routes directly to RAM-only pending flow");
  check(provisioningRouteForSecurity(1) == ProvisioningRoute::PhysicalPortal &&
            provisioningRouteForSecurity(3) == ProvisioningRoute::PhysicalPortal,
        "unknown secured network routes to the physical password portal");
}

void testPendingCommitAndTimeoutRetainPriorSelection() {
  PendingProfileLifecycle pending;
  pending.begin(100000, 2);
  check(pending.active() && pending.previousActiveIndex() == 2,
        "pending flow retains the prior active profile index");
  check(pending.evaluate(false, 159999).outcome == PendingProfileOutcome::None,
        "pending flow remains active before sixty seconds");

  PendingProfileEvaluation connected = pending.evaluate(true, 160000);
  check(connected.outcome == PendingProfileOutcome::Commit &&
            connected.previousActiveIndex == 2,
        "association plus DHCP commits even at the timeout boundary");
  check(pending.active() && pending.previousActiveIndex() == 2,
        "decision evaluation preserves prior selection until application cleanup");
  pending.finish();
  check(!pending.active() && pending.previousActiveIndex() == -1,
        "finished pending flow clears retained selection metadata");

  pending.begin(200000, 1);
  PendingProfileEvaluation timeout = pending.evaluate(false, 260000);
  check(timeout.outcome == PendingProfileOutcome::RestorePrevious &&
            timeout.previousActiveIndex == 1,
        "timed-out pending flow restores the retained previous profile");

  pending.begin(300000, -1);
  check(pending.evaluate(false, 360000).outcome == PendingProfileOutcome::FailDisconnected,
        "timed-out pending flow without a prior profile remains disconnected");
  check(pending.immediateFailure().outcome == PendingProfileOutcome::FailDisconnected,
        "immediate connection rejection without a prior profile fails disconnected");

  pending.begin(400000, 4);
  PendingProfileEvaluation rejected = pending.immediateFailure();
  check(rejected.outcome == PendingProfileOutcome::RestorePrevious &&
            rejected.previousActiveIndex == 4,
        "immediate connection rejection restores the retained previous profile");
}

void testPendingDeadlineAndGxFreshnessWraparound() {
  PendingProfileLifecycle pending;
  pending.begin(UINT32_MAX - 100, 3);
  check(pending.evaluate(false, 59898).outcome == PendingProfileOutcome::None,
        "wrapped pending deadline does not fire early");
  PendingProfileEvaluation expired = pending.evaluate(false, 59899);
  check(expired.outcome == PendingProfileOutcome::RestorePrevious &&
            expired.previousActiveIndex == 3,
        "wrapped pending deadline restores the prior profile exactly on time");

  check(!isRecentValidGxSnapshot(false, 5000, 4000, 5000),
        "invalid Modbus snapshot can never make GX online");
  check(isRecentValidGxSnapshot(true, 9000, 4000, 5000),
        "valid Modbus snapshot remains recent through maximum age");
  check(!isRecentValidGxSnapshot(true, 9001, 4000, 5000),
        "valid Modbus snapshot becomes stale after maximum age");
  check(isRecentValidGxSnapshot(true, 50, UINT32_MAX - 49, 100),
        "GX snapshot freshness is wraparound-safe");
}

}  // namespace

int main() {
  testUnknownNetworkRouting();
  testPendingCommitAndTimeoutRetainPriorSelection();
  testPendingDeadlineAndGxFreshnessWraparound();
  return failures == 0 ? 0 : 1;
}
