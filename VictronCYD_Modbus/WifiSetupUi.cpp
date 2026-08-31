#include "WifiSetupUi.h"

#include <cstdio>

#include <TFT_eSPI.h>

const WifiSetupRect kWifiWanIndicatorBounds{276, 0, 44, 28};

namespace {
constexpr uint16_t kBackground = TFT_BLACK;
constexpr uint16_t kPanel = 0x18E3;
constexpr uint16_t kBorder = 0x4C9F;
constexpr uint16_t kSelected = 0x2D9F;
constexpr uint16_t kMuted = 0x8410;

constexpr WifiSetupRect kBackBounds{4, 4, 56, 28};
constexpr WifiSetupRect kSavedTabBounds{68, 4, 116, 28};
constexpr WifiSetupRect kNearbyTabBounds{192, 4, 124, 28};
constexpr WifiSetupRect kConnectBounds{4, 204, 99, 32};
constexpr WifiSetupRect kDeleteBounds{108, 204, 99, 32};
constexpr WifiSetupRect kClearBounds{212, 204, 104, 32};
constexpr WifiSetupRect kPreviousBounds{4, 204, 99, 32};
constexpr WifiSetupRect kRefreshBounds{108, 204, 99, 32};
constexpr WifiSetupRect kNextBounds{212, 204, 104, 32};
constexpr WifiSetupRect kCancelDeleteBounds{4, 204, 151, 32};
constexpr WifiSetupRect kConfirmDeleteBounds{164, 204, 152, 32};

constexpr int16_t kRowX = 4;
constexpr int16_t kFirstRowY = 42;
constexpr int16_t kRowWidth = 312;
constexpr int16_t kRowHeight = 24;
constexpr int16_t kRowStride = 27;

uint16_t wanPhaseColor(WanPhase phase) {
  switch (phase) {
    case WanPhase::Online:
      return TFT_GREEN;
    case WanPhase::Connecting:
    case WanPhase::Validating:
      return TFT_YELLOW;
    case WanPhase::Offline:
    default:
      return TFT_RED;
  }
}

WifiSetupRect rowBounds(size_t row) {
  return {kRowX, static_cast<int16_t>(kFirstRowY + static_cast<int16_t>(row) * kRowStride),
          kRowWidth, kRowHeight};
}

void drawLockGlyph(TFT_eSPI& display, const WifiSetupRect& bounds) {
  const int16_t bodyX = static_cast<int16_t>(bounds.x + 280);
  const int16_t bodyY = static_cast<int16_t>(bounds.y + 9);
  display.drawCircle(bodyX + 6, bodyY, 5, TFT_WHITE);
  display.fillRect(bodyX, bodyY, 12, 10, TFT_WHITE);
  display.fillCircle(bodyX + 6, bodyY + 5, 1, kPanel);
}

void formatNearbySsid(const String& ssid, char output[25]) {
  if (ssid.length() > 24) {
    std::snprintf(output, 25, "%.21s...", ssid.c_str());
  } else {
    std::snprintf(output, 25, "%s", ssid.c_str());
  }
}
}  // namespace

bool WifiSetupRect::contains(const TouchPoint& point) const {
  return point.x >= x && point.y >= y && point.x < x + width && point.y < y + height;
}

WifiSetupUi::WifiSetupUi(TFT_eSPI& display) : display_(display) {}

void WifiSetupUi::open() {
  clearPortalState();
  view_ = WifiSetupView::Saved;
  awaitEntryRelease_ = false;
  selectedProfileIndex_ = -1;
  scanPage_ = 0;
  lastActivityMs_ = lastNowMs_;
  cancelHolds();
}

void WifiSetupUi::close() {
  clearPortalState();
  view_ = WifiSetupView::Closed;
  awaitEntryRelease_ = false;
  selectedProfileIndex_ = -1;
  cancelHolds();
}

bool WifiSetupUi::isOpen() const {
  return view_ != WifiSetupView::Closed;
}

void WifiSetupUi::requestFullRender() { fullRenderRequested_ = true; }

WifiSetupAction WifiSetupUi::noAction() {
  return {WifiSetupActionType::None, -1, String(), 0};
}

WifiSetupAction WifiSetupUi::simpleAction(WifiSetupActionType type, int profileIndex) {
  return {type, profileIndex, String(), 0};
}

void WifiSetupUi::cancelHolds() {
  clearHoldNeedsRedraw_ = clearHoldNeedsRedraw_ || clearHoldActive_;
  wanHoldActive_ = false;
  clearHoldActive_ = false;
  clearActionEmitted_ = false;
}

void WifiSetupUi::clearPortalState() {
  portalSsid_ = String();
  portalCode_ = String();
  portalExpiresAtMs_ = 0;
}

WifiSetupAction WifiSetupUi::handleRelease(uint32_t nowMs) {
  lastNowMs_ = nowMs;
  awaitEntryRelease_ = false;
  if (isOpen()) {
    lastActivityMs_ = nowMs;
  }
  cancelHolds();
  return noAction();
}

WifiSetupAction WifiSetupUi::poll(uint32_t nowMs) {
  lastNowMs_ = nowMs;
  if (!isOpen()) {
    if (wanHoldActive_ && nowMs - wanHoldStartedMs_ >= kWanHoldMs) {
      open();
      awaitEntryRelease_ = true;
      lastActivityMs_ = nowMs;
      fullRenderRequested_ = true;
    }
    return noAction();
  }

  if (view_ == WifiSetupView::Portal && isDeadlineReached(nowMs, portalExpiresAtMs_)) {
    close();
    return simpleAction(WifiSetupActionType::Exit);
  }

  if (clearHoldActive_ && !clearActionEmitted_ &&
      nowMs - clearHoldStartedMs_ >= kClearHoldMs) {
    clearActionEmitted_ = true;
    clearHoldActive_ = false;
    return simpleAction(WifiSetupActionType::ClearAll);
  }

  if (view_ != WifiSetupView::Portal && nowMs - lastActivityMs_ >= kInactivityMs) {
    close();
    return simpleAction(WifiSetupActionType::Exit);
  }
  return noAction();
}

uint8_t WifiSetupUi::wanHoldCountdown(uint32_t nowMs) const {
  if (!wanHoldActive_) {
    return 0;
  }
  const uint32_t elapsed = nowMs - wanHoldStartedMs_;
  if (elapsed >= kWanHoldMs) {
    return 0;
  }
  return static_cast<uint8_t>((kWanHoldMs - elapsed + 999) / 1000);
}

bool WifiSetupUi::takeFullRenderRequest() {
  const bool requested = fullRenderRequested_;
  fullRenderRequested_ = false;
  return requested;
}

WifiSetupAction WifiSetupUi::handleTouch(const TouchPoint& point, uint32_t nowMs) {
  lastNowMs_ = nowMs;
  if (!isOpen()) {
    if (!kWifiWanIndicatorBounds.contains(point)) {
      wanHoldActive_ = false;
      return noAction();
    }
    if (!wanHoldActive_) {
      wanHoldActive_ = true;
      wanHoldStartedMs_ = nowMs;
    }
    return noAction();
  }

  lastActivityMs_ = nowMs;
  if (awaitEntryRelease_) {
    return noAction();
  }
  wanHoldActive_ = false;
  if (clearHoldActive_ && !kClearBounds.contains(point)) {
    clearHoldActive_ = false;
  }

  if (kBackBounds.contains(point)) {
    if (view_ == WifiSetupView::ConfirmDelete) {
      view_ = WifiSetupView::Saved;
      requestFullRender();
      return noAction();
    }
    close();
    return simpleAction(WifiSetupActionType::Exit);
  }

  if (view_ == WifiSetupView::Portal || view_ == WifiSetupView::Result) {
    return noAction();
  }

  if (kSavedTabBounds.contains(point)) {
    view_ = WifiSetupView::Saved;
    scanPage_ = 0;
    return noAction();
  }
  if (kNearbyTabBounds.contains(point)) {
    view_ = WifiSetupView::Scanning;
    scanPage_ = 0;
    requestFullRender();
    return simpleAction(WifiSetupActionType::Refresh);
  }

  if (view_ == WifiSetupView::ConfirmDelete) {
    if (kCancelDeleteBounds.contains(point)) {
      view_ = WifiSetupView::Saved;
      requestFullRender();
      return noAction();
    }
    if (kConfirmDeleteBounds.contains(point) && selectedProfileIndex_ >= 0) {
      const int profileIndex = selectedProfileIndex_;
      view_ = WifiSetupView::Saved;
      selectedProfileIndex_ = -1;
      return simpleAction(WifiSetupActionType::DeleteSaved, profileIndex);
    }
    return noAction();
  }

  if (view_ == WifiSetupView::Saved) {
    for (size_t row = 0; row < savedProfileCount_; ++row) {
      if (rowBounds(row).contains(point)) {
        selectedProfileIndex_ = static_cast<int>(row);
        return noAction();
      }
    }
    if (kConnectBounds.contains(point) && selectedProfileIndex_ >= 0) {
      return simpleAction(WifiSetupActionType::ConnectSaved, selectedProfileIndex_);
    }
    if (kDeleteBounds.contains(point) && selectedProfileIndex_ >= 0) {
      view_ = WifiSetupView::ConfirmDelete;
      requestFullRender();
      return noAction();
    }
    if (kClearBounds.contains(point)) {
      if (clearActionEmitted_) {
        return noAction();
      }
      if (!clearHoldActive_) {
        clearHoldActive_ = true;
        clearHoldStartedMs_ = nowMs;
      }
      return noAction();
    }
    return noAction();
  }

  if (view_ == WifiSetupView::Scanning) {
    if (kRefreshBounds.contains(point)) {
      return simpleAction(WifiSetupActionType::Refresh);
    }
    return noAction();
  }

  if (view_ == WifiSetupView::Nearby) {
    const size_t pageStart = scanPage_ * kRowsPerPage;
    const size_t remaining = scanResults_.size() > pageStart ? scanResults_.size() - pageStart : 0;
    const size_t rowCount = remaining < kRowsPerPage ? remaining : kRowsPerPage;
    for (size_t row = 0; row < rowCount; ++row) {
      if (!rowBounds(row).contains(point)) {
        continue;
      }
      const ScanResult& selected = scanResults_[pageStart + row];
      const int savedProfileIndex = savedProfileForSsid(selected.ssid);
      if (savedProfileIndex >= 0) {
        selectedProfileIndex_ = savedProfileIndex;
        view_ = WifiSetupView::Saved;
        return noAction();
      }
      return {WifiSetupActionType::ProvisionNew, -1, selected.ssid, selected.encryptionType};
    }
    if (kPreviousBounds.contains(point)) {
      if (scanPage_ > 0) {
        --scanPage_;
        requestFullRender();
      }
      return noAction();
    }
    if (kRefreshBounds.contains(point)) {
      view_ = WifiSetupView::Scanning;
      requestFullRender();
      return simpleAction(WifiSetupActionType::Refresh);
    }
    if (kNextBounds.contains(point)) {
      if ((scanPage_ + 1) * kRowsPerPage < scanResults_.size()) {
        ++scanPage_;
        requestFullRender();
      }
      return noAction();
    }
  }
  return noAction();
}

void WifiSetupUi::setSavedProfiles(const NetworkProfile* profiles, size_t count,
                                   int activeIndex) {
  savedProfileCount_ = count < NetworkProfileStore::kMaxProfiles
                           ? count
                           : NetworkProfileStore::kMaxProfiles;
  if (profiles == nullptr) {
    savedProfileCount_ = 0;
  }
  for (size_t index = 0; index < savedProfileCount_; ++index) {
    savedProfiles_[index].ssid = profiles[index].ssid;
    savedProfiles_[index].lastSuccessEpoch = profiles[index].lastSuccessEpoch;
  }
  activeProfileIndex_ = activeIndex >= 0 && static_cast<size_t>(activeIndex) < savedProfileCount_
                            ? activeIndex
                            : -1;
  if (selectedProfileIndex_ < 0 ||
      static_cast<size_t>(selectedProfileIndex_) >= savedProfileCount_) {
    selectedProfileIndex_ = -1;
  }
}

void WifiSetupUi::setScanResults(const ScanResult* results, size_t count) {
  const bool showCompletedScan = isOpen() && view_ == WifiSetupView::Scanning;
  if (results == nullptr || count == 0) {
    scanResults_.clear();
  } else {
    scanResults_.assign(results, results + count);
  }
  const size_t lastPage = scanResults_.empty() ? 0 : (scanResults_.size() - 1) / kRowsPerPage;
  if (scanPage_ > lastPage) {
    scanPage_ = lastPage;
  }
  if (showCompletedScan) {
    view_ = WifiSetupView::Nearby;
  }
}

bool WifiSetupUi::showScanFailure(const String& message) {
  if (!isOpen() || view_ != WifiSetupView::Scanning) {
    return false;
  }
  showResult(message, false);
  return true;
}

void WifiSetupUi::showPortal(const String& ssid, const String& code,
                             uint32_t expiresAtMs) {
  clearPortalState();
  portalSsid_ = ssid;
  portalCode_ = code;
  portalExpiresAtMs_ = expiresAtMs;
  view_ = WifiSetupView::Portal;
  lastActivityMs_ = lastNowMs_;
  cancelHolds();
}

void WifiSetupUi::showResult(const String& message, bool success) {
  clearPortalState();
  resultMessage_ = message;
  resultSuccess_ = success;
  view_ = WifiSetupView::Result;
  lastActivityMs_ = lastNowMs_;
  cancelHolds();
}

int WifiSetupUi::savedProfileForSsid(const String& ssid) const {
  for (size_t index = 0; index < savedProfileCount_; ++index) {
    if (savedProfiles_[index].ssid == ssid) {
      return static_cast<int>(index);
    }
  }
  return -1;
}

void WifiSetupUi::drawButton(const WifiSetupRect& bounds, const char* label, bool selected) {
  display_.fillRoundRect(bounds.x, bounds.y, bounds.width, bounds.height, 4,
                         selected ? kSelected : kPanel);
  display_.drawRoundRect(bounds.x, bounds.y, bounds.width, bounds.height, 4, kBorder);
  display_.setTextColor(TFT_WHITE, selected ? kSelected : kPanel);
  display_.setTextDatum(MC_DATUM);
  display_.drawString(label, bounds.x + bounds.width / 2, bounds.y + bounds.height / 2, 2);
}

void WifiSetupUi::drawHeader(WanPhase wanPhase) {
  drawButton(kBackBounds, "Back");
  if (view_ != WifiSetupView::Portal && view_ != WifiSetupView::Result &&
      view_ != WifiSetupView::ConfirmDelete) {
    drawButton(kSavedTabBounds, "Saved", view_ == WifiSetupView::Saved);
    drawButton(kNearbyTabBounds, "Nearby",
               view_ == WifiSetupView::Nearby || view_ == WifiSetupView::Scanning);
  }
  drawWanStatusLine(wanPhase);
}

void WifiSetupUi::drawWanStatusLine(WanPhase wanPhase) {
  display_.drawLine(0, 35, display_.width() - 1, 35, wanPhaseColor(wanPhase));
}

void WifiSetupUi::drawPortalExpiry() {
  display_.fillRect(70, 215, 180, 20, kBackground);
  if (static_cast<int32_t>(portalExpiresAtMs_ - lastNowMs_) <= 0) {
    return;
  }
  char expiry[32];
  const unsigned long seconds =
      static_cast<unsigned long>((portalExpiresAtMs_ - lastNowMs_ + 999) / 1000);
  std::snprintf(expiry, sizeof(expiry), "Expires in %lus", seconds);
  display_.setTextColor(TFT_WHITE, kBackground);
  display_.setTextDatum(MC_DATUM);
  display_.drawString(expiry, display_.width() / 2, 224, 1);
}

void WifiSetupUi::drawClearHoldCountdown() {
  if (!clearHoldActive_) {
    drawButton(kClearBounds, "Clear Saved");
    return;
  }
  const uint32_t elapsed = lastNowMs_ - clearHoldStartedMs_;
  const uint32_t remaining = elapsed >= kClearHoldMs ? 0 : kClearHoldMs - elapsed;
  const unsigned long seconds = static_cast<unsigned long>((remaining + 999) / 1000);
  char clearLabel[32];
  std::snprintf(clearLabel, sizeof(clearLabel), "Clear in %lus", seconds);
  drawButton(kClearBounds, clearLabel, true);
}

void WifiSetupUi::render(const CamperNetworkStatus& networkStatus) {
  if (!isOpen()) {
    return;
  }
  display_.fillScreen(kBackground);
  fullRenderRequested_ = false;
  drawHeader(networkStatus.wanPhase);
  switch (view_) {
    case WifiSetupView::Saved:
      renderSaved();
      break;
    case WifiSetupView::Scanning:
      renderScanning();
      break;
    case WifiSetupView::Nearby:
      renderNearby();
      break;
    case WifiSetupView::ConfirmDelete:
      renderConfirmDelete();
      break;
    case WifiSetupView::Portal:
      renderPortal();
      break;
    case WifiSetupView::Result:
      renderResult();
      break;
    case WifiSetupView::Closed:
    default:
      break;
  }
  lastDynamicWanPhase_ = static_cast<int>(networkStatus.wanPhase);
  const int32_t portalRemainingMs = static_cast<int32_t>(portalExpiresAtMs_ - lastNowMs_);
  lastPortalCountdown_ = view_ == WifiSetupView::Portal && portalRemainingMs > 0
                              ? static_cast<int>((portalRemainingMs + 999) / 1000)
                              : -1;
  lastClearCountdown_ = clearHoldActive_ ? static_cast<int>((kClearHoldMs -
      (lastNowMs_ - clearHoldStartedMs_) + 999) / 1000) : -1;
}

void WifiSetupUi::renderDynamic(const CamperNetworkStatus& networkStatus) {
  if (!isOpen()) {
    return;
  }
  const int wanPhase = static_cast<int>(networkStatus.wanPhase);
  if (wanPhase != lastDynamicWanPhase_) {
    drawWanStatusLine(networkStatus.wanPhase);
    lastDynamicWanPhase_ = wanPhase;
  }
  if (view_ == WifiSetupView::Portal) {
    const int32_t remainingMs = static_cast<int32_t>(portalExpiresAtMs_ - lastNowMs_);
    const int countdown = remainingMs > 0 ? static_cast<int>((remainingMs + 999) / 1000) : 0;
    if (countdown != lastPortalCountdown_) {
      drawPortalExpiry();
      lastPortalCountdown_ = countdown;
    }
  }
  if (view_ == WifiSetupView::Saved && (clearHoldActive_ || clearHoldNeedsRedraw_)) {
    if (clearHoldNeedsRedraw_) {
      drawClearHoldCountdown();
      lastClearCountdown_ = -1;
      clearHoldNeedsRedraw_ = false;
      return;
    }
    const int countdown = static_cast<int>((kClearHoldMs - (lastNowMs_ - clearHoldStartedMs_) +
                                            999) / 1000);
    if (countdown != lastClearCountdown_) {
      drawClearHoldCountdown();
      lastClearCountdown_ = countdown;
    }
  }
}

void WifiSetupUi::renderSaved() {
  display_.setTextDatum(TL_DATUM);
  for (size_t row = 0; row < savedProfileCount_; ++row) {
    const WifiSetupRect bounds = rowBounds(row);
    const bool selected = selectedProfileIndex_ == static_cast<int>(row);
    display_.fillRoundRect(bounds.x, bounds.y, bounds.width, bounds.height, 3,
                           selected ? kSelected : kPanel);
    char ssidLine[64];
    std::snprintf(ssidLine, sizeof(ssidLine), "%s%s", savedProfiles_[row].ssid.c_str(),
                  activeProfileIndex_ == static_cast<int>(row) ? " [ACTIVE]" : "");
    display_.setTextColor(TFT_WHITE, selected ? kSelected : kPanel);
    display_.drawString(ssidLine, bounds.x + 5, bounds.y + 2, 1);
    char successLine[48];
    if (savedProfiles_[row].lastSuccessEpoch == 0) {
      std::snprintf(successLine, sizeof(successLine), "Never connected");
    } else {
      std::snprintf(successLine, sizeof(successLine), "Last success: %lu",
                    static_cast<unsigned long>(savedProfiles_[row].lastSuccessEpoch));
    }
    display_.setTextColor(kMuted, selected ? kSelected : kPanel);
    display_.drawString(successLine, bounds.x + 5, bounds.y + 13, 1);
  }
  if (savedProfileCount_ == 0) {
    display_.setTextColor(kMuted, kBackground);
    display_.setTextDatum(MC_DATUM);
    display_.drawString("No saved networks", display_.width() / 2, 105, 2);
  }
  drawButton(kConnectBounds, "Connect", selectedProfileIndex_ >= 0);
  drawButton(kDeleteBounds, "Delete", selectedProfileIndex_ >= 0);
  char clearLabel[32];
  if (clearHoldActive_) {
    const uint32_t elapsed = lastNowMs_ - clearHoldStartedMs_;
    const uint32_t remaining = elapsed >= kClearHoldMs ? 0 : kClearHoldMs - elapsed;
    const unsigned long seconds = static_cast<unsigned long>((remaining + 999) / 1000);
    std::snprintf(clearLabel, sizeof(clearLabel), "Clear in %lus", seconds);
  } else {
    std::snprintf(clearLabel, sizeof(clearLabel), "Clear Saved");
  }
  drawButton(kClearBounds, clearLabel, clearHoldActive_);
}

void WifiSetupUi::renderScanning() {
  display_.setTextColor(TFT_WHITE, kBackground);
  display_.setTextDatum(MC_DATUM);
  display_.drawString("Scanning nearby networks...", display_.width() / 2, 110, 2);
  drawButton(kRefreshBounds, "Refresh");
}

void WifiSetupUi::renderNearby() {
  const size_t pageStart = scanPage_ * kRowsPerPage;
  const size_t remaining = scanResults_.size() > pageStart ? scanResults_.size() - pageStart : 0;
  const size_t rowCount = remaining < kRowsPerPage ? remaining : kRowsPerPage;
  for (size_t row = 0; row < rowCount; ++row) {
    const ScanResult& result = scanResults_[pageStart + row];
    const WifiSetupRect bounds = rowBounds(row);
    display_.fillRoundRect(bounds.x, bounds.y, bounds.width, bounds.height, 3, kPanel);
    display_.setTextDatum(TL_DATUM);
    display_.setTextColor(TFT_WHITE, kPanel);
    char ssid[25];
    formatNearbySsid(result.ssid, ssid);
    display_.drawString(ssid, bounds.x + 5, bounds.y + 5, 1);
    char rssi[24];
    std::snprintf(rssi, sizeof(rssi), "%ld dBm", static_cast<long>(result.rssi));
    display_.drawString(rssi, bounds.x + 170, bounds.y + 5, 1);
    if (result.encryptionType == 0) {
      display_.drawString("OPEN", bounds.x + 272, bounds.y + 5, 1);
    } else {
      drawLockGlyph(display_, bounds);
    }
    int bars = 1;
    if (result.rssi >= -60) {
      bars = 4;
    } else if (result.rssi >= -70) {
      bars = 3;
    } else if (result.rssi >= -80) {
      bars = 2;
    }
    for (int bar = 0; bar < 4; ++bar) {
      const int16_t barHeight = static_cast<int16_t>(3 + bar * 3);
      display_.fillRect(bounds.x + 235 + bar * 5, bounds.y + 18 - barHeight, 3, barHeight,
                        bar < bars ? TFT_GREEN : kMuted);
    }
  }
  if (scanResults_.empty()) {
    display_.setTextColor(kMuted, kBackground);
    display_.setTextDatum(MC_DATUM);
    display_.drawString("No nearby networks", display_.width() / 2, 105, 2);
  }
  drawButton(kPreviousBounds, "Previous", scanPage_ > 0);
  drawButton(kRefreshBounds, "Refresh");
  drawButton(kNextBounds, "Next", (scanPage_ + 1) * kRowsPerPage < scanResults_.size());
}

void WifiSetupUi::renderConfirmDelete() {
  display_.setTextColor(TFT_WHITE, kBackground);
  display_.setTextDatum(MC_DATUM);
  char prompt[80];
  const char* ssid = selectedProfileIndex_ >= 0 &&
                             static_cast<size_t>(selectedProfileIndex_) < savedProfileCount_
                         ? savedProfiles_[selectedProfileIndex_].ssid.c_str()
                         : "network";
  std::snprintf(prompt, sizeof(prompt), "Delete %s?", ssid);
  display_.drawString(prompt, display_.width() / 2, 105, 2);
  display_.setTextColor(TFT_YELLOW, kBackground);
  display_.drawString("This cannot be undone", display_.width() / 2, 135, 2);
  drawButton(kCancelDeleteBounds, "Cancel");
  drawButton(kConfirmDeleteBounds, "Confirm Delete");
}

void WifiSetupUi::renderPortal() {
  display_.setTextDatum(MC_DATUM);
  display_.setTextColor(TFT_WHITE, kBackground);
  display_.drawString("WiFi setup portal", display_.width() / 2, 55, 2);
  display_.drawString(portalSsid_, display_.width() / 2, 82, 2);
  display_.drawString("Join Camper-Victron, then open", display_.width() / 2, 112, 2);
  display_.setTextColor(TFT_CYAN, kBackground);
  display_.drawString("http://192.168.50.1/setup", display_.width() / 2, 137, 2);
  display_.setTextColor(TFT_WHITE, kBackground);
  display_.drawString("Pairing code", display_.width() / 2, 168, 2);
  display_.drawString(portalCode_, display_.width() / 2, 191, 4);
  drawPortalExpiry();
}

void WifiSetupUi::renderResult() {
  display_.setTextDatum(MC_DATUM);
  display_.setTextColor(resultSuccess_ ? TFT_GREEN : TFT_RED, kBackground);
  display_.drawString(resultSuccess_ ? "Success" : "Failed", display_.width() / 2, 90, 4);
  display_.setTextColor(TFT_WHITE, kBackground);
  display_.drawString(resultMessage_, display_.width() / 2, 135, 2);
  display_.drawString("Tap Back to return", display_.width() / 2, 175, 2);
}
