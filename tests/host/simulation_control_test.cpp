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

  execute(control, "SIM clock=morning", "SIM OK");
  check(std::strcmp(clock.text(), "08:15") == 0, "clock fixture applied");
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
  check(std::strcmp(clock.text(), "12:34") == 0,
        "reset restores fixed default clock");
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
  check(std::strcmp(clock.text(), "21:45") == 0,
        "poll routes clock command");
  check(modbus.fetch(cycle) && !cycle.requiredValid,
        "poll routes Modbus command");

  Serial.clear();
  Serial.feed(std::string(120, 'X') + "\n");
  control.poll();
  check(Serial.output() == std::string("SIM ERROR\n"),
        "overlong line produces one error and resets parser");
  return 0;
}
