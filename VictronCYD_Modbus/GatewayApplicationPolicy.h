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
