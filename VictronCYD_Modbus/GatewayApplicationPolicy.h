#pragma once

#include <cstdint>

#include "GatewayPolicy.h"

enum class ProvisioningRoute : uint8_t {
  DirectPending,
  OnDevicePassword,
};

enum class PendingProfileSource : uint8_t {
  None,
  DirectOpen,
  Portal,
  OnDevice,
  Saved,
};

enum class ScanUiOutcome : uint8_t {
  None,
  DeliverResults,
  ShowRetryableFailure,
};

enum class DisplaySurface : uint8_t {
  Calibration,
  Setup,
  Settings,
  Dashboard,
};

inline DisplaySurface displaySurfaceFor(bool touchReady, bool calibrated, bool setupOpen,
                                       bool settingsOpen = false) {
  if (touchReady && !calibrated) {
    return DisplaySurface::Calibration;
  }
  if (setupOpen) return DisplaySurface::Setup;
  return settingsOpen ? DisplaySurface::Settings : DisplaySurface::Dashboard;
}

enum class WifiSetupOrigin : uint8_t { Dashboard, Settings };

inline bool canOpenSettings(DisplaySurface surface, bool pendingAttempt, bool portalActive) {
  return surface == DisplaySurface::Dashboard && !pendingAttempt && !portalActive;
}

inline bool returnToSettings(WifiSetupOrigin origin, bool explicitBack) {
  return origin == WifiSetupOrigin::Settings && explicitBack;
}

class TouchSurfaceGate {
 public:
  void press(DisplaySurface surface, bool permitted) { surface_ = surface; active_ = permitted; }
  bool allowMove(DisplaySurface surface) const { return active_ && surface == surface_; }
  void release() { active_ = false; }
 private:
  DisplaySurface surface_ = DisplaySurface::Dashboard;
  bool active_ = false;
};

// A successful POST stops the portal too. Consume it before treating an inactive
// portal as expiry, after the main loop has polled its deadline/server.
template <typename Portal, typename Submission, typename Submit, typename Expire>
inline void coordinatePortalLifecycle(bool savedAttempt, bool physicalPortalActive,
                                      Portal& portal, Submission& submission,
                                      Submit submit, Expire expire) {
  if (savedAttempt) return;
  if (portal.takeSubmission(submission)) { submit(submission); return; }
  if (physicalPortalActive && !portal.active()) expire();
}

inline bool shouldPaintDashboardWan(bool fullFrameCleared, int countdown, int phase,
                                    int previousCountdown, int previousPhase) {
  return fullFrameCleared || countdown != previousCountdown || phase != previousPhase;
}

inline bool shouldRepaintDashboardHold(int previousCountdown, int countdown) {
  return previousCountdown != countdown;
}

template <typename CaptureTouch, typename HasPendingRender, typename Paint, typename ApplyAction,
          typename PaintDashboard>
inline void coordinateSetupInteraction(CaptureTouch captureTouch, HasPendingRender hasPendingRender,
                                       Paint paint, ApplyAction applyAction,
                                       PaintDashboard paintDashboard) {
  const auto action = captureTouch();
  if (hasPendingRender()) { paint(); } else { paintDashboard(); }
  applyAction(action);
}

template <typename Display>
inline void paintCenterHeaderText(Display& display, int& previousWidth, const char* text,
                                  bool holdLabel, bool fullFrameCleared, int background = 0,
                                  const char* meridiem = "") {
  if (fullFrameCleared) {
    previousWidth = 0;
  }
  const int font = holdLabel ? 2 : 4;
  const int numberWidth = display.textWidth(text, font);
  const int suffixWidth = !holdLabel && meridiem[0] ? display.textWidth(meridiem, 2) + 3 : 0;
  const int currentWidth = numberWidth + suffixWidth;
  const int width = previousWidth > currentWidth ? previousWidth : currentWidth;
  display.fillRect(160 - width / 2 - 2, 0, width + 4, 20, background);
  display.drawString(text, 160 - suffixWidth / 2, holdLabel ? 12 : 11, font);
  if (suffixWidth) display.drawString(meridiem, 160 + (numberWidth + 3) / 2, 11, 2);
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
                           : ProvisioningRoute::OnDevicePassword;
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

inline bool retainPendingStationConfigForImmediateReplacement(
    const GatewayLifecycleReplacement& replacement, GatewayLifecycleTarget target) {
  return replacement.cancelPendingProfile &&
         (target == GatewayLifecycleTarget::SavedConnection ||
          target == GatewayLifecycleTarget::PendingProfile);
}

template <typename EraseStationConfig>
inline void finishStationConfigReplacement(bool retainedForReplacement,
                                           bool replacementStarted,
                                           EraseStationConfig eraseStationConfig) {
  if (retainedForReplacement && !replacementStarted) {
    eraseStationConfig();
  }
}

class GatewayLifecyclePolicy {
 public:
  GatewayLifecycleReplacement replaceWith(GatewayLifecycleTarget target,
                                            uint32_t nowMs = 0,
                                            int previousActiveIndex = -1,
                                            PendingProfileSource source =
                                                PendingProfileSource::None) {
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
      pendingSource_ = source;
    } else {
      pending_.finish();
      pendingSource_ = PendingProfileSource::None;
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
    pendingSource_ = PendingProfileSource::None;
  }

  GatewayLifecycleTarget target() const { return target_; }
  bool pendingActive() const { return target_ == GatewayLifecycleTarget::PendingProfile; }
  PendingProfileSource pendingSource() const { return pendingSource_; }
  bool physicalPortalActive() const {
    return target_ == GatewayLifecycleTarget::PhysicalPortal;
  }

 private:
  GatewayLifecycleTarget target_ = GatewayLifecycleTarget::Idle;
  PendingProfileLifecycle pending_;
  PendingProfileSource pendingSource_ = PendingProfileSource::None;
};

inline bool isRecentValidGxSnapshot(bool valid, uint32_t nowMs, uint32_t receivedAtMs,
                                    uint32_t maximumAgeMs) {
  return valid && nowMs - receivedAtMs <= maximumAgeMs;
}
