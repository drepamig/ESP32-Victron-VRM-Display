#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Arduino.h"
#include "IPAddress.h"

constexpr int WIFI_AP_STA = 3;
constexpr int16_t WIFI_SCAN_RUNNING = -1;
constexpr int16_t WIFI_SCAN_FAILED = -2;

struct FakeAccessPoint {
  struct ConfigCall {
    IPAddress local;
    IPAddress gateway;
    IPAddress subnet;
    IPAddress leaseStart;
    IPAddress dns;
  };

  std::vector<std::string>* events = nullptr;
  bool configResult = true;
  bool createResult = true;
  bool naptResult = true;
  uint8_t clients = 0;
  IPAddress address;
  std::vector<ConfigCall> configCalls;
  int createCalls = 0;
  int createChannel = 0;
  bool createHidden = true;
  int createMaxConnections = 0;
  int naptCalls = 0;

  bool config(IPAddress local, IPAddress gateway, IPAddress subnet, IPAddress leaseStart, IPAddress dns) {
    events->push_back("config");
    configCalls.push_back({local, gateway, subnet, leaseStart, dns});
    address = local;
    return configResult;
  }

  bool create(const char*, const char*, int channel, bool hidden, int maxConnections) {
    events->push_back("create");
    ++createCalls;
    createChannel = channel;
    createHidden = hidden;
    createMaxConnections = maxConnections;
    return createResult;
  }

  bool enableNAPT(bool) {
    events->push_back("napt");
    ++naptCalls;
    return naptResult;
  }

  IPAddress localIP() const { return address; }
  uint8_t stationCount() const { return clients; }
};

struct FakeScanRecord {
  String ssid;
  int32_t rssi = 0;
  uint8_t encryption = 0;
  int32_t channel = 0;
};

class FakeWiFiClass {
 public:
  FakeWiFiClass() { AP.events = &events; }

  void reset() {
    events.clear();
    persistentCalls = 0;
    persistentValue = true;
    modeCalls = 0;
    modeValue = 0;
    autoReconnectCalls = 0;
    autoReconnect = true;
    beginSsids.clear();
    beginPassphraseLengths.clear();
    disconnectCalls = 0;
    disconnectWifiOff.clear();
    disconnectEraseAp.clear();
    connected = false;
    stationSsid = String();
    stationAddress = IPAddress();
    stationRssi = 0;
    scanStartCalls = 0;
    lastScanAsync = false;
    lastScanShowHidden = true;
    scanStartResult = WIFI_SCAN_RUNNING;
    scanCompleteValue = WIFI_SCAN_FAILED;
    scanDeleteCalls = 0;
    scanRecords.clear();
    AP = FakeAccessPoint();
    AP.events = &events;
  }

  void persistent(bool enabled) {
    events.push_back("persistent");
    ++persistentCalls;
    persistentValue = enabled;
  }

  bool mode(int value) {
    events.push_back("mode");
    ++modeCalls;
    modeValue = value;
    return true;
  }

  bool setAutoReconnect(bool enabled) {
    events.push_back("auto-reconnect");
    ++autoReconnectCalls;
    autoReconnect = enabled;
    return true;
  }

  int begin(const char* ssid, const char* passphrase) {
    events.push_back("begin:" + std::string(ssid == nullptr ? "" : ssid));
    beginSsids.emplace_back(ssid == nullptr ? "" : ssid);
    beginPassphraseLengths.push_back(passphrase == nullptr ? 0 : std::string(passphrase).length());
    return 0;
  }

  bool disconnect(bool wifiOff = false, bool eraseAp = false, unsigned long = 100) {
    events.push_back("disconnect");
    ++disconnectCalls;
    disconnectWifiOff.push_back(wifiOff);
    disconnectEraseAp.push_back(eraseAp);
    connected = false;
    stationAddress = IPAddress();
    return true;
  }

  bool isConnected() const { return connected; }
  String SSID() const { return stationSsid; }
  IPAddress localIP() const { return stationAddress; }
  int8_t RSSI() const { return stationRssi; }

  int16_t scanNetworks(bool async, bool showHidden) {
    events.push_back("scan");
    ++scanStartCalls;
    lastScanAsync = async;
    lastScanShowHidden = showHidden;
    return scanStartResult;
  }

  int16_t scanComplete() const { return scanCompleteValue; }
  String SSID(uint8_t index) const { return scanRecords.at(index).ssid; }
  int32_t RSSI(uint8_t index) const { return scanRecords.at(index).rssi; }
  uint8_t encryptionType(uint8_t index) const { return scanRecords.at(index).encryption; }
  int32_t channel(uint8_t index) const { return scanRecords.at(index).channel; }

  void scanDelete() {
    events.push_back("scan-delete");
    ++scanDeleteCalls;
  }

  std::vector<std::string> events;
  int persistentCalls = 0;
  bool persistentValue = true;
  int modeCalls = 0;
  int modeValue = 0;
  int autoReconnectCalls = 0;
  bool autoReconnect = true;
  std::vector<std::string> beginSsids;
  std::vector<size_t> beginPassphraseLengths;
  int disconnectCalls = 0;
  std::vector<bool> disconnectWifiOff;
  std::vector<bool> disconnectEraseAp;
  bool connected = false;
  String stationSsid;
  IPAddress stationAddress;
  int8_t stationRssi = 0;
  int scanStartCalls = 0;
  bool lastScanAsync = false;
  bool lastScanShowHidden = true;
  int16_t scanStartResult = WIFI_SCAN_RUNNING;
  int16_t scanCompleteValue = WIFI_SCAN_FAILED;
  int scanDeleteCalls = 0;
  std::vector<FakeScanRecord> scanRecords;
  FakeAccessPoint AP;
};

inline FakeWiFiClass WiFi;
