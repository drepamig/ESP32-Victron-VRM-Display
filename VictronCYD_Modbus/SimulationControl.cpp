#include "SimulationControl.h"

#ifdef CYD_SIMULATION

#include <cstdio>
#include <cstring>

namespace {
void writeResponse(char* response, size_t capacity, bool accepted) {
  if (capacity == 0) return;
  std::snprintf(response, capacity, "%s", accepted ? "SIM OK" : "SIM ERROR");
}
}  // namespace

SimulationControl::SimulationControl(SimCamperNetwork& network,
                                     SimModbusCycleSource& modbus,
                                     SimulationClock& clock)
    : network_(network), modbus_(modbus), clock_(clock) {}

bool SimulationControl::processLine(const char* line, char* response,
                                    size_t responseCapacity) {
  bool accepted = false;
  if (line != nullptr && std::strcmp(line, "SIM reset") == 0) {
    network_.resetFixtures();
    modbus_.resetFixture();
    clock_.resetFixture();
    accepted = true;
  } else if (line != nullptr && std::strncmp(line, "SIM ", 4) == 0) {
    const char* assignment = line + 4;
    const char* equals = std::strchr(assignment, '=');
    if (equals != nullptr && equals != assignment && equals[1] != '\0' &&
        std::strchr(equals + 1, '=') == nullptr &&
        std::strchr(equals + 1, ' ') == nullptr) {
      const size_t keyLength = static_cast<size_t>(equals - assignment);
      const char* fixture = equals + 1;
      if (keyLength == 5 && std::strncmp(assignment, "clock", 5) == 0) {
        accepted = clock_.setFixture(fixture);
      } else if (keyLength == 4 && std::strncmp(assignment, "scan", 4) == 0) {
        accepted = network_.setScanFixture(fixture);
      } else if (keyLength == 7 && std::strncmp(assignment, "connect", 7) == 0) {
        accepted = network_.setConnectFixture(fixture);
      } else if (keyLength == 6 && std::strncmp(assignment, "modbus", 6) == 0) {
        accepted = modbus_.setFixture(fixture);
      }
    }
  }
  writeResponse(response, responseCapacity, accepted);
  return accepted;
}

void SimulationControl::poll() {
  while (Serial.available() > 0) {
    const int next = Serial.read();
    if (next < 0) return;
    const char character = static_cast<char>(next);
    if (character != '\n') {
      if (character == '\0') lineOverflow_ = true;
      if (lineLength_ < kMaximumLineLength) {
        line_[lineLength_++] = character;
      } else {
        lineOverflow_ = true;
      }
      continue;
    }

    if (lineLength_ > 0 && line_[lineLength_ - 1] == '\r') --lineLength_;
    line_[lineLength_] = '\0';
    char response[16];
    if (lineOverflow_) {
      writeResponse(response, sizeof(response), false);
    } else {
      processLine(line_, response, sizeof(response));
    }
    Serial.println(static_cast<const char*>(response));
    lineLength_ = 0;
    lineOverflow_ = false;
  }
}

#endif
