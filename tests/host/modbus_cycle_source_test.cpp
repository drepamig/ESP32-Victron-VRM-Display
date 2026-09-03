#include <cstdlib>
#include <cstring>
#include <iostream>

#include "Arduino.h"
#include "../../VictronCYD_Modbus/SimModbusCycleSource.h"

namespace {
void check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}
}  // namespace

int main() {
  SimModbusCycleSource source("ignored.example");
  ModbusReadCycle cycle{};

  fakeMillis = 50000;
  check(source.setFixture("nominal") && source.fetch(cycle),
        "nominal fixture is accepted and fetched");
  check(cycle.requiredValid && cycle.gridW == -350 && cycle.acW == 1240 &&
            cycle.battV == 52.4 && cycle.battA == -8.5 && cycle.battW == -445 &&
            cycle.soc == 76 && cycle.dcReady && cycle.dcW == 88 &&
            cycle.pvReady && cycle.pvW == 1625 && cycle.batteryTemperatureReady &&
            cycle.battT == 24.5 && cycle.systemStateReady &&
            std::strcmp(cycle.battState, "discharging") == 0 &&
            std::strcmp(cycle.sysState, "Inverting") == 0 &&
            cycle.receivedAtMs == fakeMillis,
        "nominal fixture values are deterministic");

  check(source.setFixture("stale") && source.fetch(cycle),
        "stale fixture is accepted and fetched");
  check(cycle.requiredValid && cycle.receivedAtMs == fakeMillis - 10001,
        "stale fixture is valid but older than the dashboard limit");

  check(source.setFixture("offline") && source.fetch(cycle),
        "offline fixture is accepted and fetched");
  check(!cycle.requiredValid && !cycle.dcReady && !cycle.pvReady &&
            !cycle.batteryTemperatureReady && !cycle.systemStateReady,
        "offline fixture contains no usable Modbus groups");

  check(source.setFixture("partial") && source.fetch(cycle),
        "partial fixture is accepted and fetched");
  check(cycle.requiredValid && !cycle.dcReady && cycle.pvReady &&
            !cycle.batteryTemperatureReady && cycle.systemStateReady,
        "partial fixture explicitly controls optional groups");

  check(!source.setFixture("mystery"), "unknown Modbus fixture rejected");
  source.resetFixture();
  check(source.fetch(cycle) && cycle.requiredValid && cycle.pvW == 1625,
        "reset restores nominal fixture");
  return 0;
}
