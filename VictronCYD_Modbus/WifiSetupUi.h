#pragma once

#include <Arduino.h>
#include <vector>

#include "CamperNetwork.h"
#include "CredentialEntryController.h"
#include "CredentialKeyboardLayout.h"
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
  SavedConnecting,
  Scanning,
  Nearby,
  ConfirmDelete,
  Password,
  Connecting,
  Portal,
  Result,
};

enum class WifiSetupActionType : uint8_t {
  None,
  ConnectSaved,
  ProvisionNew,
  SubmitCredentials,
  UsePhone,
  CancelCredentialAttempt,
  CancelSavedConnection,
  DeleteSaved,
  Refresh,
  ClearAll,
  Exit,
};

enum class WifiSetupExitReason : uint8_t { Back, Inactivity, PortalExpired };

struct WifiSetupAction {
  WifiSetupActionType type;
  int profileIndex;
  String ssid;
  uint8_t securityType;
  WifiSetupExitReason exitReason = WifiSetupExitReason::Back;
};

class WifiSetupUi {
 public:
  explicit WifiSetupUi(TFT_eSPI& display);

  void open();
  void openFromSettings(uint32_t nowMs);
  void close();
  bool isOpen() const;
  void render(const CamperNetworkStatus& networkStatus);
  void renderDynamic(const CamperNetworkStatus& networkStatus);
  bool takeFullRenderRequest();
  uint8_t wanHoldCountdown(uint32_t nowMs) const;
  WifiSetupAction handleTouch(const TouchPoint& point, uint32_t nowMs);
  WifiSetupAction handleTouchMove(const TouchPoint& point, uint32_t nowMs);
  WifiSetupAction handleRelease(uint32_t nowMs);
  WifiSetupAction poll(uint32_t nowMs, bool allowDashboardEntry = true);
  void setSavedProfiles(const NetworkProfile* profiles, size_t count, int activeIndex);
  void setScanResults(const ScanResult* results, size_t count);
  bool showScanFailure(const String& message);
  void showCredentialEntry(const String& ssid, uint8_t securityType, uint32_t nowMs);
  bool takeCredentialSubmission(CredentialSubmission& out);
  bool showCredentialFailure(const String& message, uint32_t nowMs);
  void cancelCredentialAttempt();
  void cancelSavedConnection();
  void showPortal(const String& ssid, const String& code, uint32_t expiresAtMs);
  void setPortalAddress(const IPAddress& address);
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
  void drawButton(const WifiSetupRect& bounds, const char* label, bool selected = false,
                  bool enabled = true, uint8_t font = 2);
  void renderSaved();
  void renderSavedConnecting();
  void renderScanning();
  void renderNearby();
  void renderConfirmDelete();
  void renderCredential();
  void renderPortal();
  void renderResult();
  void drawCredentialHeader();
  void drawCredentialField();
  void drawCredentialStatus();
  void drawCredentialKeyboard();
  void drawCredentialControls();
  void drawCredentialShowControl();
  void drawCredentialShiftControl();
  void drawCredentialConnectControl();
  void clearCredentialState();
  void returnToNearby();
  void cancelHolds();
  void clearPortalState();
  void requestFullRender();
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
  bool credentialContactActive_ = false;
  bool clearHoldActive_ = false;
  bool clearActionEmitted_ = false;
  bool clearHoldNeedsRedraw_ = false;
  bool fullRenderRequested_ = false;
  uint32_t wanHoldStartedMs_ = 0;
  uint32_t clearHoldStartedMs_ = 0;
  uint32_t lastActivityMs_ = 0;
  uint32_t lastNowMs_ = 0;
  int lastDynamicWanPhase_ = -1;
  int lastPortalCountdown_ = -1;
  int lastClearCountdown_ = -1;
  bool credentialFieldNeedsRedraw_ = false;
  bool credentialKeyboardNeedsRedraw_ = false;
  bool credentialShowNeedsRedraw_ = false;
  bool credentialShiftNeedsRedraw_ = false;
  bool credentialConnectNeedsRedraw_ = false;
  CredentialEntryController credentialEntry_;
  String credentialError_;
  String nearbyNotice_;
  String savedConnectingSsid_;
  String portalSsid_;
  String portalCode_;
  IPAddress portalAddress_{192, 168, 50, 1};
  uint32_t portalExpiresAtMs_ = 0;
  String resultMessage_;
  bool resultSuccess_ = false;
};
