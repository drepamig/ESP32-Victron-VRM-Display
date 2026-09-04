#pragma once
#include "CredentialSubmission.h"
#include "GatewayApplicationPolicy.h"
#include "NetworkProfiles.h"

enum class GatewayConnectionOutcome : uint8_t {
  None, Started, Connected, InvalidSelection, StorageReadFailed, Rejected,
  TimedOut, Cancelled, PersistenceFailed,
};
struct GatewayConnectionResult {
  GatewayConnectionOutcome outcome = GatewayConnectionOutcome::None;
  PendingProfileSource source = PendingProfileSource::None;
  bool cancelPortal = false;
};
template <class Network, class Store = NetworkProfileStore>
class GatewayConnectionController {
 public:
  GatewayConnectionController(Network& network, Store& store) : network_(network), store_(store) {}
  GatewayConnectionController(const GatewayConnectionController&) = delete;
  GatewayConnectionController& operator=(const GatewayConnectionController&) = delete;

  GatewayConnectionResult beginSaved(int index, uint32_t nowMs, bool storeReady = true) {
    const auto source = PendingProfileSource::Saved;
    if (!storeReady) return {GatewayConnectionOutcome::StorageReadFailed, source};
    if (index < 0 || static_cast<size_t>(index) >= Store::kMaxProfiles) {
      return {GatewayConnectionOutcome::InvalidSelection, source};
    }
    PrivateProfile candidate, previous;
    int previousIndex = -1;
    if (!store_.loadActive(previous.value, previousIndex)) {
      return {GatewayConnectionOutcome::StorageReadFailed, source};
    }
    if (!store_.load(static_cast<size_t>(index), candidate.value)) {
      return {GatewayConnectionOutcome::InvalidSelection, source};
    }
    return start(candidate.value, previous.value, previousIndex, index, source, nowMs);
  }

  GatewayConnectionResult beginSubmitted(CredentialSubmission& submission,
                                         PendingProfileSource source, uint32_t nowMs,
                                         bool storeReady = true) {
    PrivateProfile candidate, previous;
    if (!submission.ready || (source != PendingProfileSource::DirectOpen &&
        source != PendingProfileSource::Portal && source != PendingProfileSource::OnDevice)) {
      submission.clear();
      return rejectSubmission(GatewayConnectionOutcome::Rejected, source);
    }
    candidate.value = {submission.ssid, submission.passphrase, submission.securityType, 0};
    submission.clear();
    int previousIndex = -1;
    if (!storeReady || !store_.loadActive(previous.value, previousIndex)) {
      return rejectSubmission(GatewayConnectionOutcome::StorageReadFailed, source);
    }
    return start(candidate.value, previous.value, previousIndex, -1, source, nowMs);
  }

  GatewayConnectionResult poll(uint32_t nowMs, uint32_t epoch) {
    const auto evaluation = lifecycle_.evaluatePending(network_.pendingProfileConnected(), nowMs);
    if (evaluation.outcome == PendingProfileOutcome::None) return {};
    if (evaluation.outcome != PendingProfileOutcome::Commit) {
      return fail(GatewayConnectionOutcome::TimedOut, nowMs);
    }
    const auto source = pendingSource();
    bool persisted;
    if (source == PendingProfileSource::Saved) {
      persisted = store_.activate(static_cast<size_t>(savedIndex_));
    } else {
      candidate_.value.lastSuccessEpoch = epoch;
      size_t storedIndex = 0;
      persisted = store_.upsertAndActivate(candidate_.value, storedIndex);
    }
    if (!persisted) return fail(GatewayConnectionOutcome::PersistenceFailed, nowMs);
    network_.acceptPendingProfile();
    finish();
    return {GatewayConnectionOutcome::Connected, source};
  }

  GatewayConnectionResult cancel(uint32_t nowMs) {
    return pendingActive() ? fail(GatewayConnectionOutcome::Cancelled, nowMs)
                           : GatewayConnectionResult{};
  }

  // The sketch owns the portal and acts on this cancellation request.
  bool replace(GatewayLifecycleTarget target, uint32_t nowMs = 0) {
    const auto replacement = lifecycle_.replaceWith(target, nowMs);
    if (replacement.cancelPendingProfile) network_.cancelPendingProfile();
    if (target != GatewayLifecycleTarget::Exit || !pendingActive()) clearBuffers();
    return replacement.cancelPhysicalPortal;
  }
  bool pendingActive() const { return lifecycle_.pendingActive(); }
  PendingProfileSource pendingSource() const { return lifecycle_.pendingSource(); }
  bool physicalPortalActive() const { return lifecycle_.physicalPortalActive(); }

 private:
  GatewayConnectionResult rejectSubmission(GatewayConnectionOutcome outcome,
                                           PendingProfileSource source) {
    // A consumed phone POST has already stopped the portal, even when storage fails.
    const bool cancelPortal = source == PendingProfileSource::Portal && physicalPortalActive();
    if (cancelPortal) lifecycle_.replaceWith(GatewayLifecycleTarget::Idle);
    return {outcome, source, cancelPortal};
  }

  struct PrivateProfile {
    NetworkProfile value;
    ~PrivateProfile() { clear(); }
    void clear() {
      if (value.passphrase.length()) {
        secureClearBytes(const_cast<char*>(value.passphrase.c_str()), value.passphrase.length());
      }
      value = NetworkProfile{};
    }
  };

  GatewayConnectionResult start(const NetworkProfile& candidate, const NetworkProfile& previous,
                                int previousIndex, int savedIndex, PendingProfileSource source,
                                uint32_t nowMs) {
    // Both profiles have been read before cancelling or replacing connectivity.
    const auto replacement = lifecycle_.replaceWith(GatewayLifecycleTarget::PendingProfile,
                                                     nowMs, previousIndex, source);
    if (replacement.cancelPendingProfile) network_.cancelPendingProfile(false);
    candidate_.clear(); previous_.clear();
    candidate_.value = candidate;
    previous_.value = previous;
    previousAvailable_ = previousIndex >= 0;
    savedIndex_ = savedIndex;
    auto result = GatewayConnectionResult{GatewayConnectionOutcome::Started, source};
    if (!network_.connect(candidate_.value, nowMs)) {
      result = fail(GatewayConnectionOutcome::Rejected, nowMs);
    }
    result.cancelPortal = replacement.cancelPhysicalPortal;
    return result;
  }

  GatewayConnectionResult fail(GatewayConnectionOutcome outcome, uint32_t nowMs) {
    const auto source = pendingSource();
    network_.cancelPendingProfile(!previousAvailable_);
    if (previousAvailable_ && network_.connect(previous_.value, nowMs)) {
      // Rollback resumes the existing automatic reconnection policy.
      network_.acceptPendingProfile();
    } else {
      network_.disconnectUpstream();
    }
    finish();
    return {outcome, source};
  }

  void finish() {
    lifecycle_.completePending();
    clearBuffers();
  }
  void clearBuffers() {
    candidate_.clear(); previous_.clear();
    previousAvailable_ = false;
    savedIndex_ = -1;
  }
  Network& network_;
  Store& store_;
  GatewayLifecyclePolicy lifecycle_;
  PrivateProfile candidate_, previous_;
  bool previousAvailable_ = false;
  int savedIndex_ = -1;
};
