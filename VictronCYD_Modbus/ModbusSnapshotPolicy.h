#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

struct ModbusSnapshot {
  double gridW;
  double acW;
  double dcW;
  double battV;
  double battA;
  double battW;
  double soc;
  double battT;
  double pvW;
  char battState[16];
  char sysState[20];
  uint32_t receivedAtMs;
  bool valid;
};

struct ModbusReadCycle {
  double gridW;
  double acW;
  double dcW;
  double battV;
  double battA;
  double battW;
  double soc;
  double battT;
  double pvW;
  char battState[16];
  char sysState[20];
  uint32_t receivedAtMs;
  bool requiredValid;
  bool dcReady;
  bool pvReady;
  bool batteryTemperatureReady;
  bool systemStateReady;
};

inline void copyModbusText(char* destination, size_t capacity, const char* source) {
  if (capacity == 0) {
    return;
  }
  std::snprintf(destination, capacity, "%s", source == nullptr ? "-" : source);
}

inline ModbusSnapshot makeDefaultModbusSnapshot() {
  ModbusSnapshot snapshot{};
  copyModbusText(snapshot.battState, sizeof(snapshot.battState), "-");
  copyModbusText(snapshot.sysState, sizeof(snapshot.sysState), "-");
  return snapshot;
}

inline ModbusSnapshot mergeModbusSnapshot(const ModbusSnapshot& previous,
                                          const ModbusReadCycle& cycle) {
  ModbusSnapshot merged = previous;
  merged.receivedAtMs = cycle.receivedAtMs;
  merged.valid = cycle.requiredValid;
  if (cycle.requiredValid) {
    merged.gridW = cycle.gridW;
    merged.acW = cycle.acW;
    merged.battV = cycle.battV;
    merged.battA = cycle.battA;
    merged.battW = cycle.battW;
    merged.soc = cycle.soc;
    copyModbusText(merged.battState, sizeof(merged.battState), cycle.battState);
  }
  if (cycle.dcReady) {
    merged.dcW = cycle.dcW;
  }
  if (cycle.pvReady) {
    merged.pvW = cycle.pvW;
  }
  if (cycle.batteryTemperatureReady) {
    merged.battT = cycle.battT;
  }
  if (cycle.systemStateReady) {
    copyModbusText(merged.sysState, sizeof(merged.sysState), cycle.sysState);
  }
  return merged;
}
