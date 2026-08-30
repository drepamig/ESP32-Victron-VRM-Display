#pragma once

#include <cstdint>

#include "GatewayPolicy.h"

enum class ProvisioningRoute : uint8_t {
  DirectPending,
  PhysicalPortal,
};

enum class ScanUiOutcome : uint8_t {
  None,
  DeliverResults,
  ShowRetryableFailure,
};

enum class DisplaySurface : uint8_t {
  Calibration,
  Setup,
  Dashboard,
};

inline DisplaySurface displaySurfaceFor(bool touchReady, bool calibrated, bool setupOpen) {
  if (touchReady && !calibrated) {
    return DisplaySurface::Calibration;
  }
  return setupOpen ? DisplaySurface::Setup : DisplaySurface::Dashboard;
}

inline bool shouldPaintDashboardWan(bool fullFrameCleared, int countdown, int phase,
                                    int previousCountdown, int previousPhase) {
  return fullFrameCleared || countdown != previousCountdown || phase != previousPhase;
}

inline bool shouldRepaintDashboardHold(int previousCountdown, int countdown) {
  return previousCountdown != countdown;
}

template <typename ClearRegion, typename DrawText>
inline void paintCenteredHeaderTransition(int& previousWidth, int currentWidth, bool holdLabel,
                                          ClearRegion clearRegion, DrawText drawText,
                                          bool fullFrameCleared) {
  if (fullFrameCleared) {
    previousWidth = 0;
  }
  const int width = previousWidth > currentWidth ? previousWidth : currentWidth;
  clearRegion(160 - width / 2 - 2, 0, width + 4, 20);
  drawText(160, holdLabel ? 14 : 11);
  previousWidth = currentWidth;
}

template <typename ClearFrame, typename PaintHeader, typename PaintValues>
inline void redrawDashboardSurface(ClearFrame clearFrame, PaintHeader paintHeader,
                                   PaintValues paintValues) {
  clearFrame();
  paintHeader(true);
  paintValues();
}

template <typename PaintClock, typename PaintWan>
inline void coordinateDashboardWanHold(int countdown, PaintClock paintClock, PaintWan paintWan) {
  const char* label = countdown == 3 ? "HOLD 3" : countdown == 2 ? "HOLD 2"
                                     : countdown == 1 ? "HOLD 1" : nullptr;
  paintClock(label);
  paintWan(label != nullptr);
}

template <typename ApplyInteraction, typename PaintHeader, typename ClearFrame>
inline void coordinateDashboardInteraction(ApplyInteraction applyInteraction,
                                           PaintHeader paintHeader, ClearFrame) {
  applyInteraction();
  paintHeader();
}

inline ScanUiOutcome scanUiOutcome(bool complete, bool failed) {
  if (complete) {
    return ScanUiOutcome::DeliverResults;
  }
  return failed ? ScanUiOutcome::ShowRetryableFailure : ScanUiOutcome::None;
}

inline ProvisioningRoute provisioningRouteForSecurity(uint8_t securityType) {
  return securityType == 0 ? ProvisioningRoute::DirectPending
                           : ProvisioningRoute::PhysicalPortal;
}

enum class PendingProfileOutcome : uint8_t {
  None,
  Commit,
  RestorePrevious,
  FailDisconnected,
};

struct PendingProfileEvaluation {
  PendingProfileOutcome outcome;
  int previousActiveIndex;
};

class PendingProfileLifecycle {
 public:
  static constexpr uint32_t kTimeoutMs = 60000;

  void begin(uint32_t nowMs, int previousActiveIndex) {
    active_ = true;
    previousActiveIndex_ = previousActiveIndex;
    deadlineMs_ = nowMs + kTimeoutMs;
  }

  PendingProfileEvaluation evaluate(bool associatedAndAddressed, uint32_t nowMs) const {
    if (!active_) {
      return {PendingProfileOutcome::None, -1};
    }
    if (associatedAndAddressed) {
      return {PendingProfileOutcome::Commit, previousActiveIndex_};
    }
    if (!isDeadlineReached(nowMs, deadlineMs_)) {
      return {PendingProfileOutcome::None, previousActiveIndex_};
    }
    return failureEvaluation();
  }

  PendingProfileEvaluation immediateFailure() const {
    return active_ ? failureEvaluation()
                   : PendingProfileEvaluation{PendingProfileOutcome::None, -1};
  }

  bool active() const { return active_; }
  int previousActiveIndex() const { return active_ ? previousActiveIndex_ : -1; }

  void finish() {
    active_ = false;
    previousActiveIndex_ = -1;
    deadlineMs_ = 0;
  }

 private:
  PendingProfileEvaluation failureEvaluation() const {
    return {previousActiveIndex_ >= 0 ? PendingProfileOutcome::RestorePrevious
                                     : PendingProfileOutcome::FailDisconnected,
            previousActiveIndex_};
  }

  bool active_ = false;
  int previousActiveIndex_ = -1;
  uint32_t deadlineMs_ = 0;
};

enum class GatewayLifecycleTarget : uint8_t {
  Idle,
  SavedConnection,
  PendingProfile,
  PhysicalPortal,
  ClearAll,
  Exit,
};

struct GatewayLifecycleReplacement {
  bool cancelPendingProfile;
  bool cancelPhysicalPortal;
};

class GatewayLifecyclePolicy {
 public:
  GatewayLifecycleReplacement replaceWith(GatewayLifecycleTarget target,
                                            uint32_t nowMs = 0,
                                            int previousActiveIndex = -1) {
    if (target == GatewayLifecycleTarget::Exit &&
        target_ == GatewayLifecycleTarget::PendingProfile) {
      return {false, false};
    }
    const GatewayLifecycleReplacement replacement{
        target_ == GatewayLifecycleTarget::PendingProfile,
        target_ == GatewayLifecycleTarget::PhysicalPortal,
    };
    target_ = target;
    if (target_ == GatewayLifecycleTarget::PendingProfile) {
      pending_.begin(nowMs, previousActiveIndex);
    } else {
      pending_.finish();
    }
    return replacement;
  }

  PendingProfileEvaluation evaluatePending(bool associatedAndAddressed,
                                            uint32_t nowMs) const {
    return target_ == GatewayLifecycleTarget::PendingProfile
               ? pending_.evaluate(associatedAndAddressed, nowMs)
               : PendingProfileEvaluation{PendingProfileOutcome::None, -1};
  }

  PendingProfileEvaluation pendingImmediateFailure() const {
    return target_ == GatewayLifecycleTarget::PendingProfile
               ? pending_.immediateFailure()
               : PendingProfileEvaluation{PendingProfileOutcome::None, -1};
  }

  void completePending() {
    if (target_ == GatewayLifecycleTarget::PendingProfile) {
      pending_.finish();
      target_ = GatewayLifecycleTarget::Idle;
    }
  }

  GatewayLifecycleTarget target() const { return target_; }
  bool pendingActive() const { return target_ == GatewayLifecycleTarget::PendingProfile; }
  bool physicalPortalActive() const {
    return target_ == GatewayLifecycleTarget::PhysicalPortal;
  }

 private:
  GatewayLifecycleTarget target_ = GatewayLifecycleTarget::Idle;
  PendingProfileLifecycle pending_;
};

inline bool isRecentValidGxSnapshot(bool valid, uint32_t nowMs, uint32_t receivedAtMs,
                                    uint32_t maximumAgeMs) {
  return valid && nowMs - receivedAtMs <= maximumAgeMs;
}
