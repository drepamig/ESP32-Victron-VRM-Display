#pragma once

#include <cstdint>

class RetryBackoff {
 public:
  RetryBackoff(uint32_t minimumMs, uint32_t maximumMs)
      : minimumMs_(minimumMs), maximumMs_(maximumMs), nextMs_(minimumMs) {}

  uint32_t nextDelay() {
    const uint32_t current = nextMs_;
    nextMs_ = nextMs_ >= maximumMs_ / 2 ? maximumMs_ : nextMs_ * 2;
    return current;
  }

  void reset() { nextMs_ = minimumMs_; }

 private:
  uint32_t minimumMs_;
  uint32_t maximumMs_;
  uint32_t nextMs_;
};

inline bool shouldCommitPendingProfile(bool associated, bool hasAddress) {
  return associated && hasAddress;
}

inline bool isDeadlineReached(uint32_t nowMs, uint32_t deadlineMs) {
  return static_cast<int32_t>(nowMs - deadlineMs) >= 0;
}
