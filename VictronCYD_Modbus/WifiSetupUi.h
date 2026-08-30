#pragma once

#include <Arduino.h>
#include <vector>

#include "CamperNetwork.h"
#include "NetworkProfiles.h"
#include "TouchMapping.h"

class TFT_eSPI;

struct WifiSetupRect {
  int16_t x;
  int16_t y;
  int16_t width;
  int16_t height;

  bool contains(const TouchPoint& point) const;
};

extern const WifiSetupRect kWifiWanIndicatorBounds;

enum class WifiSetupView : uint8_t {
  Closed,
  Saved,
  Scanning,
  Nearby,
  ConfirmDelete,
  Portal,
  Result,
};

enum class WifiSetupActionType : uint8_t {
  None,
  ConnectSaved,
  ProvisionNew,
  DeleteSaved,
  Refresh,
  ClearAll,
  Exit,
};

struct WifiSetupAction {
  WifiSetupActionType type;
  int profileIndex;
  String ssid;
  uint8_t securityType;
};

class WifiSetupUi {
 public:
  explicit WifiSetupUi(TFT_eSPI& display);

  void open();
  void close();
  bool isOpen() const;
  void render(const CamperNetworkStatus& networkStatus);
  void renderDynamic(const CamperNetworkStatus& networkStatus);
  bool takeFullRenderRequest();
  uint8_t wanHoldCountdown(uint32_t nowMs) const;
  WifiSetupAction handleTouch(const TouchPoint& point, uint32_t nowMs);
  WifiSetupAction handleRelease(uint32_t nowMs);
  WifiSetupAction poll(uint32_t nowMs);
  void setSavedProfiles(const NetworkProfile* profiles, size_t count, int activeIndex);
  void setScanResults(const ScanResult* results, size_t count);
  bool showScanFailure(const String& message);
  void showPortal(const String& ssid, const String& code, uint32_t expiresAtMs);
  void showResult(const String& message, bool success);

 private:
  static constexpr size_t kRowsPerPage = 5;
  static constexpr uint32_t kWanHoldMs = 3000;
  static constexpr uint32_t kClearHoldMs = 10000;
  static constexpr uint32_t kInactivityMs = 60000;

  struct SavedProfileDisplay {
    String ssid;
    uint32_t lastSuccessEpoch;
  };

  static WifiSetupAction noAction();
  static WifiSetupAction simpleAction(WifiSetupActionType type, int profileIndex = -1);
  void drawHeader(WanPhase wanPhase);
  void drawWanStatusLine(WanPhase wanPhase);
  void drawPortalExpiry();
  void drawClearHoldCountdown();
  void drawButton(const WifiSetupRect& bounds, const char* label, bool selected = false);
  void renderSaved();
  void renderScanning();
  void renderNearby();
  void renderConfirmDelete();
  void renderPortal();
  void renderResult();
  void cancelHolds();
  void clearPortalState();
  int savedProfileForSsid(const String& ssid) const;

  TFT_eSPI& display_;
  WifiSetupView view_ = WifiSetupView::Closed;
  SavedProfileDisplay savedProfiles_[NetworkProfileStore::kMaxProfiles]{};
  std::vector<ScanResult> scanResults_;
  size_t savedProfileCount_ = 0;
  size_t scanPage_ = 0;
  int activeProfileIndex_ = -1;
  int selectedProfileIndex_ = -1;
  bool wanHoldActive_ = false;
  bool awaitEntryRelease_ = false;
  bool clearHoldActive_ = false;
  bool clearActionEmitted_ = false;
  bool fullRenderRequested_ = false;
  uint32_t wanHoldStartedMs_ = 0;
  uint32_t clearHoldStartedMs_ = 0;
  uint32_t lastActivityMs_ = 0;
  uint32_t lastNowMs_ = 0;
  int lastDynamicWanPhase_ = -1;
  int lastPortalCountdown_ = -1;
  int lastClearCountdown_ = -1;
  String portalSsid_;
  String portalCode_;
  uint32_t portalExpiresAtMs_ = 0;
  String resultMessage_;
  bool resultSuccess_ = false;
};
