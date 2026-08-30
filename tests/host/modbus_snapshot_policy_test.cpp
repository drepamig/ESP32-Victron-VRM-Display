#include <cstdio>
#include <cstring>
#include <iostream>

#include "../../VictronCYD_Modbus/ModbusSnapshotPolicy.h"

namespace {

int failures = 0;

void check(bool condition, const char* name) {
  if (!condition) {
    std::cerr << "FAIL: " << name << '\n';
    ++failures;
  }
}

void setText(char* output, size_t capacity, const char* value) {
  std::snprintf(output, capacity, "%s", value);
}

ModbusReadCycle completeCycle() {
  ModbusReadCycle cycle{};
  cycle.requiredValid = true;
  cycle.gridW = 10;
  cycle.acW = 20;
  cycle.battV = 12.5;
  cycle.battA = -3.5;
  cycle.battW = -44;
  cycle.soc = 75;
  setText(cycle.battState, sizeof(cycle.battState), "discharging");
  cycle.pvReady = true;
  cycle.pvW = 100;
  cycle.dcReady = true;
  cycle.dcW = 30;
  cycle.systemStateReady = true;
  setText(cycle.sysState, sizeof(cycle.sysState), "Inverting");
  cycle.batteryTemperatureReady = true;
  cycle.battT = 24.5;
  cycle.receivedAtMs = 500;
  return cycle;
}

void testFirstSnapshotDefaultsMissingOptionalFields() {
  const ModbusSnapshot empty = makeDefaultModbusSnapshot();
  ModbusReadCycle first = completeCycle();
  first.pvReady = false;
  first.dcReady = false;
  first.systemStateReady = false;
  first.batteryTemperatureReady = false;

  const ModbusSnapshot merged = mergeModbusSnapshot(empty, first);
  check(merged.valid && merged.gridW == 10 && merged.acW == 20 &&
            merged.battV == 12.5 && std::strcmp(merged.battState, "discharging") == 0,
        "first valid snapshot publishes required fields");
  check(merged.pvW == 0 && merged.dcW == 0 && merged.battT == 0 &&
            std::strcmp(merged.sysState, "-") == 0,
        "first snapshot uses safe defaults for unavailable optional fields");
}

void testMixedOptionalResultsRetainOnlyFailures() {
  ModbusSnapshot previous = mergeModbusSnapshot(makeDefaultModbusSnapshot(), completeCycle());
  ModbusReadCycle next = completeCycle();
  next.gridW = 11;
  next.acW = 21;
  next.pvW = 150;
  next.dcReady = false;
  next.dcW = 999;
  next.systemStateReady = false;
  setText(next.sysState, sizeof(next.sysState), "must-not-replace");
  next.battT = 26.0;
  next.receivedAtMs = 700;

  const ModbusSnapshot merged = mergeModbusSnapshot(previous, next);
  check(merged.valid && merged.gridW == 11 && merged.acW == 21 && merged.receivedAtMs == 700,
        "new required fields and timestamp replace prior values");
  check(merged.pvW == 150 && merged.battT == 26.0,
        "successful optional reads replace their prior fields");
  check(merged.dcW == 30 && std::strcmp(merged.sysState, "Inverting") == 0,
        "failed optional reads retain their exact prior fields");
}

void testRequiredFailureIsInvalidAndRetainsRequiredFields() {
  ModbusSnapshot previous = mergeModbusSnapshot(makeDefaultModbusSnapshot(), completeCycle());
  ModbusReadCycle failed = completeCycle();
  failed.requiredValid = false;
  failed.gridW = 999;
  failed.acW = 999;
  setText(failed.battState, sizeof(failed.battState), "must-not-replace");
  failed.pvReady = true;
  failed.pvW = 175;
  failed.receivedAtMs = 900;

  const ModbusSnapshot merged = mergeModbusSnapshot(previous, failed);
  check(!merged.valid && merged.receivedAtMs == 900,
        "required read failure publishes an invalid timestamped snapshot");
  check(merged.gridW == 10 && merged.acW == 20 &&
            std::strcmp(merged.battState, "discharging") == 0,
        "required read failure retains all prior required display fields");
  check(merged.pvW == 175,
        "independently successful optional read still advances worker-retained state");
}

}  // namespace

int main() {
  testFirstSnapshotDefaultsMissingOptionalFields();
  testMixedOptionalResultsRetainOnlyFailures();
  testRequiredFailureIsInvalidAndRetainsRequiredFields();
  return failures == 0 ? 0 : 1;
}
