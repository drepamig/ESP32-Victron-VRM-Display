#pragma once

#include <cstdint>

#include "GatewayPolicy.h"

enum class ProvisioningRoute : uint8_t {
  DirectPending,
  PhysicalPortal,
};

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

inline bool isRecentValidGxSnapshot(bool valid, uint32_t nowMs, uint32_t receivedAtMs,
                                    uint32_t maximumAgeMs) {
  return valid && nowMs - receivedAtMs <= maximumAgeMs;
}
