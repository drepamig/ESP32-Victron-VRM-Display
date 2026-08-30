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

void testReplacingPendingLifecycleInvalidatesOldDeadline() {
  GatewayLifecyclePolicy lifecycle;
  GatewayLifecycleReplacement replacement =
      lifecycle.replaceWith(GatewayLifecycleTarget::PendingProfile, 1000, 1);
  check(!replacement.cancelPendingProfile && !replacement.cancelPhysicalPortal,
        "first pending lifecycle has nothing older to cancel");

  replacement = lifecycle.replaceWith(GatewayLifecycleTarget::SavedConnection, 2000, -1);
  check(replacement.cancelPendingProfile && !replacement.cancelPhysicalPortal &&
            lifecycle.target() == GatewayLifecycleTarget::SavedConnection,
        "pending to saved replacement cancels only the old pending attempt");
  check(lifecycle.evaluatePending(false, 61000).outcome == PendingProfileOutcome::None,
        "saved replacement makes the old pending deadline inert");

  lifecycle.replaceWith(GatewayLifecycleTarget::PendingProfile, 100000, 2);
  replacement = lifecycle.replaceWith(GatewayLifecycleTarget::PhysicalPortal, 101000, -1);
  check(replacement.cancelPendingProfile && !replacement.cancelPhysicalPortal &&
            lifecycle.physicalPortalActive(),
        "pending to portal replacement cancels pending and transfers ownership to portal");
  check(lifecycle.evaluatePending(true, 160000).outcome == PendingProfileOutcome::None,
        "portal replacement prevents old pending success from replacing portal UI");

  lifecycle.replaceWith(GatewayLifecycleTarget::PendingProfile, 200000, 3);
  replacement = lifecycle.replaceWith(GatewayLifecycleTarget::PendingProfile, 230000, 4);
  check(replacement.cancelPendingProfile && !replacement.cancelPhysicalPortal,
        "new open pending flow cancels the older pending attempt");
  check(lifecycle.evaluatePending(false, 260000).outcome == PendingProfileOutcome::None,
        "new pending flow does not inherit the old deadline");
  PendingProfileEvaluation newTimeout = lifecycle.evaluatePending(false, 290000);
  check(newTimeout.outcome == PendingProfileOutcome::RestorePrevious &&
            newTimeout.previousActiveIndex == 4,
        "new pending flow owns its own deadline and previous selection");

  lifecycle.replaceWith(GatewayLifecycleTarget::PendingProfile, 300000, 1);
  replacement = lifecycle.replaceWith(GatewayLifecycleTarget::ClearAll, 301000, -1);
  check(replacement.cancelPendingProfile &&
            lifecycle.evaluatePending(false, 360000).outcome == PendingProfileOutcome::None,
        "clear-all cancels pending without allowing its deadline to restore anything");
}

void testExitPreservesPendingButCancelsPhysicalPortal() {
  GatewayLifecyclePolicy lifecycle;
  lifecycle.replaceWith(GatewayLifecycleTarget::PendingProfile, 400000, 1);
  GatewayLifecycleReplacement replacement =
      lifecycle.replaceWith(GatewayLifecycleTarget::Exit, 401000, -1);
  check(!replacement.cancelPendingProfile && !replacement.cancelPhysicalPortal &&
            lifecycle.pendingActive(),
        "ordinary Exit preserves the pending connection and retained fallback");
  check(lifecycle.evaluatePending(false, 459999).outcome == PendingProfileOutcome::None,
        "pending connection continues through its original deadline after Exit");
  PendingProfileEvaluation timeout = lifecycle.evaluatePending(false, 460000);
  check(timeout.outcome == PendingProfileOutcome::RestorePrevious &&
            timeout.previousActiveIndex == 1,
        "pending timeout after Exit still restores the retained previous profile");

  lifecycle.replaceWith(GatewayLifecycleTarget::PhysicalPortal, 500000, -1);
  replacement = lifecycle.replaceWith(GatewayLifecycleTarget::Exit, 501000, -1);
  check(!replacement.cancelPendingProfile && replacement.cancelPhysicalPortal &&
            lifecycle.target() == GatewayLifecycleTarget::Exit,
        "Exit from Portal still cancels the physical portal lifecycle");
}

void testScanTerminalRouting() {
  check(scanUiOutcome(false, false) == ScanUiOutcome::None,
        "running or idle scan state has no terminal UI action");
  check(scanUiOutcome(true, false) == ScanUiOutcome::DeliverResults,
        "completed scan routes results to Nearby");
  check(scanUiOutcome(false, true) == ScanUiOutcome::ShowRetryableFailure,
        "failed scan routes to a retryable failure result");
}

// Mutation caught: removing calibration priority would let the setup or dashboard
// renderer overwrite the touch calibration targets while calibration is incomplete.
void testCalibrationExclusivelyOwnsDisplayUntilComplete() {
  check(displaySurfaceFor(true, false, false) == DisplaySurface::Calibration,
        "uncalibrated touch owns the display instead of dashboard rendering");
  check(displaySurfaceFor(true, false, true) == DisplaySurface::Calibration,
        "uncalibrated touch owns the display instead of setup rendering");
  check(displaySurfaceFor(true, true, true) == DisplaySurface::Setup,
        "completed calibration lets an open setup UI own the display");
  check(displaySurfaceFor(true, true, false) == DisplaySurface::Dashboard,
        "completed calibration lets the dashboard own a closed setup UI");
  check(displaySurfaceFor(false, false, true) == DisplaySurface::Setup,
        "failed touch initialization preserves setup rendering");
  check(displaySurfaceFor(false, false, false) == DisplaySurface::Dashboard,
        "failed touch initialization preserves dashboard rendering");
}

// Mutation caught: removing the full-frame force from the sketch redraw sequence leaves WAN
// blank after setup returns even though cached state is unchanged.
void testDashboardRedrawClearsThenForcesHeaderBeforeValues() {
  int sequence = 0;
  bool forcedHeader = false;
  redrawDashboardSurface(
      [&] { check(++sequence == 1, "dashboard redraw must clear frame first"); },
      [&](bool forceWan) { forcedHeader = forceWan; check(++sequence == 2,
          "dashboard redraw must paint header after frame clear"); },
      [&] { check(++sequence == 3, "dashboard redraw must paint values after header"); });
  check(forcedHeader, "full dashboard redraw must force WAN/header painting despite cache");
}

// Mutation caught: moving the hold label back into WAN or letting normal clock painting
// overwrite it during a valid hold.
void testDashboardHeaderCoordinatesHoldLabelsAndCancellation() {
  const char* label = nullptr;
  bool highlighted = false;
  coordinateDashboardWanHold(3, [&](const char* value) { label = value; },
                            [&](bool value) { highlighted = value; });
  check(std::string(label) == "HOLD 3" && highlighted,
        "valid hold must place HOLD 3 in the clock coordination region and highlight WAN");
  coordinateDashboardWanHold(2, [&](const char* value) { label = value; },
                            [&](bool value) { highlighted = value; });
  check(std::string(label) == "HOLD 2" && highlighted, "hold countdown must advance to HOLD 2");
  coordinateDashboardWanHold(1, [&](const char* value) { label = value; },
                            [&](bool value) { highlighted = value; });
  check(std::string(label) == "HOLD 1" && highlighted, "hold countdown must advance to HOLD 1");
  coordinateDashboardWanHold(0, [&](const char* value) { label = value; },
                            [&](bool value) { highlighted = value; });
  check(label == nullptr && !highlighted, "cancelled hold must restore normal clock coordination");
}

// Mutation caught: omitting the immediate interaction repaint leaves HOLD/normal clock stale
// until the periodic tick and may replace highlighted WAN with stale state.
void testDashboardInteractionImmediatelyRepaintsHeaderWithoutFrameClear() {
  bool actionRan = false;
  int headerPaints = 0;
  int frameClears = 0;
  coordinateDashboardInteraction([&] { actionRan = true; },
                                 [&] { ++headerPaints; },
                                 [&] { ++frameClears; });
  check(actionRan && headerPaints == 1 && frameClears == 0,
        "dashboard press or cancel must immediately repaint header without a full clear");
}

// Mutation caught: repainting unchanged HOLD 3 on every five-millisecond loop iteration.
void testDashboardHoldRepaintCoalescesVisibleStates() {
  check(shouldRepaintDashboardHold(-1, 3), "press must repaint HOLD 3 once");
  check(!shouldRepaintDashboardHold(3, 3), "unchanged HOLD 3 must not repaint");
  check(shouldRepaintDashboardHold(3, 2) && shouldRepaintDashboardHold(2, 1),
        "visible HOLD transitions must repaint");
  check(shouldRepaintDashboardHold(1, 0), "release or slide cancellation must repaint normal state");
}

// Mutation caught: clearing only the new clock width leaves HOLD glyphs after cancellation.
void testCenterHeaderTransitionClearsUnionAndAlignsHold() {
  int priorWidth = 87;
  int clearX = 0, clearWidth = 0, drawY = 0;
  paintCenteredHeaderTransition(priorWidth, 39, false,
      [&](int x, int, int width, int) { clearX = x; clearWidth = width; },
      [&](int, int y) { drawY = y; }, false);
  check(clearX == 115 && clearWidth == 91 && drawY == 11 && priorWidth == 39,
        "HOLD to clock must clear padded 115..205 span and use clock y 11");
  paintCenteredHeaderTransition(priorWidth, 87, true,
      [&](int x, int, int width, int) { clearX = x; clearWidth = width; },
      [&](int, int y) { drawY = y; }, false);
  check(clearX == 115 && clearWidth == 91 && drawY == 14 && priorWidth == 87,
        "clock to HOLD must clear safe union and use hold y 14");
  paintCenteredHeaderTransition(priorWidth, 39, false,
      [&](int, int, int, int) {}, [&](int, int) {}, true);
  check(priorWidth == 39, "full frame transition must reset retained width before paint");
}

}  // namespace

int main() {
  testUnknownNetworkRouting();
  testPendingCommitAndTimeoutRetainPriorSelection();
  testPendingDeadlineAndGxFreshnessWraparound();
  testReplacingPendingLifecycleInvalidatesOldDeadline();
  testExitPreservesPendingButCancelsPhysicalPortal();
  testScanTerminalRouting();
  testCalibrationExclusivelyOwnsDisplayUntilComplete();
  testDashboardRedrawClearsThenForcesHeaderBeforeValues();
  testDashboardHeaderCoordinatesHoldLabelsAndCancellation();
  testDashboardInteractionImmediatelyRepaintsHeaderWithoutFrameClear();
  testDashboardHoldRepaintCoalescesVisibleStates();
  testCenterHeaderTransitionClearsUnionAndAlignsHold();
  return failures == 0 ? 0 : 1;
}
