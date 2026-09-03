#include "SimModbusCycleSource.h"

#ifdef CYD_SIMULATION

#include <Arduino.h>
#include <cstring>

namespace {
void populateNominal(ModbusReadCycle& cycle) {
  cycle = {};
  cycle.gridW = -350;
  cycle.acW = 1240;
  cycle.dcW = 88;
  cycle.battV = 52.4;
  cycle.battA = -8.5;
  cycle.battW = -445;
  cycle.soc = 76;
  cycle.battT = 24.5;
  cycle.pvW = 1625;
  copyModbusText(cycle.battState, sizeof(cycle.battState), "discharging");
  copyModbusText(cycle.sysState, sizeof(cycle.sysState), "Inverting");
  cycle.receivedAtMs = millis();
  cycle.requiredValid = true;
  cycle.dcReady = true;
  cycle.pvReady = true;
  cycle.batteryTemperatureReady = true;
  cycle.systemStateReady = true;
}
}  // namespace

SimModbusCycleSource::SimModbusCycleSource(const char*) {}

bool SimModbusCycleSource::fetch(ModbusReadCycle& cycle) {
  populateNominal(cycle);
  switch (fixture_.load(std::memory_order_relaxed)) {
    case Fixture::Nominal:
      break;
    case Fixture::Stale:
      cycle.receivedAtMs = millis() - 10001;
      break;
    case Fixture::Offline:
      cycle = {};
      copyModbusText(cycle.battState, sizeof(cycle.battState), "-");
      copyModbusText(cycle.sysState, sizeof(cycle.sysState), "-");
      cycle.receivedAtMs = millis();
      break;
    case Fixture::Partial:
      cycle.dcReady = false;
      cycle.pvReady = true;
      cycle.batteryTemperatureReady = false;
      cycle.systemStateReady = true;
      break;
  }
  return true;
}

bool SimModbusCycleSource::setFixture(const char* fixture) {
  if (fixture == nullptr) {
    return false;
  }
  if (std::strcmp(fixture, "nominal") == 0) {
    fixture_.store(Fixture::Nominal, std::memory_order_relaxed);
  } else if (std::strcmp(fixture, "stale") == 0) {
    fixture_.store(Fixture::Stale, std::memory_order_relaxed);
  } else if (std::strcmp(fixture, "offline") == 0) {
    fixture_.store(Fixture::Offline, std::memory_order_relaxed);
  } else if (std::strcmp(fixture, "partial") == 0) {
    fixture_.store(Fixture::Partial, std::memory_order_relaxed);
  } else {
    return false;
  }
  return true;
}

void SimModbusCycleSource::resetFixture() {
  fixture_.store(Fixture::Nominal, std::memory_order_relaxed);
}

#endif
