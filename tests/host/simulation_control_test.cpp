#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#include "Arduino.h"
#include "../../VictronCYD_Modbus/SimulationControl.h"

namespace {
void check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

void execute(SimulationControl& control, const char* command, const char* expected) {
  char response[16] = {};
  control.processLine(command, response, sizeof(response));
  check(std::strcmp(response, expected) == 0, command);
}
}  // namespace

int main() {
  SimCamperNetwork network;
  check(network.begin("Bench", "dummy-pass-123", 0), "network begins");
  SimModbusCycleSource modbus("ignored.example");
  SimulationClock clock;
  SimulationControl control(network, modbus, clock);

  execute(control, "SIM clock=springbefore", "SIM OK");
  check(clock.epoch() == 1805011199U, "spring fixture is the UTC second before the Chicago jump");
  execute(control, "SIM clock=springafter", "SIM OK");
  check(clock.epoch() == 1805011200U, "spring fixture crosses the boundary by one second");
  execute(control, "SIM clock=fallbefore", "SIM OK");
  check(clock.epoch() == 1793516399U, "fall fixture is the UTC second before the Chicago repeat");
  execute(control, "SIM clock=fallafter", "SIM OK");
  check(clock.epoch() == 1793516400U, "fall fixture crosses the boundary by one second");
  execute(control, "SIM reset", "SIM OK");

  execute(control, "SIM wan=online", "SIM ERROR");
  NetworkProfile wanProfile;
  wanProfile.ssid = "Bench-Open";
  network.connect(wanProfile, 0);
  execute(control, "SIM wan=offline", "SIM ERROR");
  network.poll(1000);
  execute(control, "SIM wan=offline", "SIM OK");
  check(network.status().wanPhase == WanPhase::Offline && !network.pendingProfileConnected(),
        "WAN parser applies outage fixture");
  execute(control, "SIM wan=validating", "SIM OK");
  check(network.status().wanPhase == WanPhase::Validating && network.pendingProfileConnected(),
        "WAN parser restores validation readiness");
  for (const char* command : {"SIM wan=", "SIM wan=unknown", "SIM wan=Online",
                             "SIM wan=online extra", "SIM wan=online=offline",
                             "SIM wan =online", "SIM wan=online\t", "SIM wan=online\r"}) {
    execute(control, command, "SIM ERROR");
    check(network.status().wanPhase == WanPhase::Validating && network.pendingProfileConnected(),
          "rejected WAN parser command cannot mutate fixture");
  }
  network.acceptPendingProfile();
  Serial.clear();
  Serial.feed("SIM wan=offline\r\nSIM wan=validating\nSIM wan=online\n");
  control.poll();
  check(Serial.output() == std::string("SIM OK\nSIM OK\nSIM OK\n") &&
            network.status().wanPhase == WanPhase::Online && !network.pendingProfileConnected(),
        "serial WAN fixtures accept CRLF/LF and preserve accepted ownership");
  execute(control, "SIM reset", "SIM OK");
  execute(control, "SIM wan=online", "SIM ERROR");

  execute(control, "SIM clock=morning", "SIM OK");
  check(clock.epoch() == 1788423300U, "morning fixture has a fixed UTC epoch");
  execute(control, "SIM scan=empty", "SIM OK");
  check(network.startScan() && network.scanComplete(), "empty scan completes");
  ScanResult scan[1];
  check(network.scanResults(scan, 1) == 0, "empty scan fixture applied");
  execute(control, "SIM connect=failure", "SIM OK");
  NetworkProfile profile;
  profile.ssid = "Bench-Protected";
  profile.passphrase = "dummy-wifi-pass";
  check(network.connect(profile, 1), "connection attempt begins");
  network.poll(1001);
  check(network.status().wanPhase == WanPhase::Offline,
        "connection fixture applied");
  execute(control, "SIM modbus=partial", "SIM OK");
  ModbusReadCycle cycle{};
  check(modbus.fetch(cycle) && cycle.requiredValid && !cycle.dcReady,
        "Modbus fixture applied");

  execute(control, "SIM reset", "SIM OK");
  check(clock.epoch() == 1788438840U, "reset restores the fixed UTC epoch");
  check(network.setConnectFixture("success"), "reset restores network controls");
  check(modbus.fetch(cycle) && cycle.dcReady && cycle.pvW == 1625,
        "reset restores nominal Modbus data");

  execute(control, "SIM", "SIM ERROR");
  execute(control, "SIM reset=now", "SIM ERROR");
  execute(control, "SIM clock=unknown", "SIM ERROR");
  execute(control, "SIM scan=nominal extra", "SIM ERROR");
  execute(control, " SIM reset", "SIM ERROR");
  execute(control, "NOPE modbus=nominal", "SIM ERROR");

  Serial.clear();
  Serial.feed("SIM clock=evening\r\nSIM modbus=offline\n");
  control.poll();
  check(Serial.output() == std::string("SIM OK\nSIM OK\n"),
        "poll accepts CRLF and LF framed serial commands");
  check(clock.epoch() == 1788471900U, "poll routes clock command as a UTC instant");
  check(modbus.fetch(cycle) && !cycle.requiredValid,
        "poll routes Modbus command");

  Serial.clear();
  Serial.feed(std::string(120, 'X') + "\n");
  control.poll();
  check(Serial.output() == std::string("SIM ERROR\n"),
        "overlong line produces one error and resets parser");
  Serial.clear();
  Serial.feed("SI\rM reset\n");
  Serial.feed(std::string("SIM reset") + '\0' + "extra\n");
  control.poll();
  check(Serial.output() == std::string("SIM ERROR\nSIM ERROR\n"),
        "embedded CR and NUL must not turn malformed commands into valid commands");
  return 0;
}
