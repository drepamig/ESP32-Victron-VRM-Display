#pragma once

#ifdef CYD_SIMULATION

#include <Arduino.h>

#include "SimCamperNetwork.h"
#include "SimModbusCycleSource.h"
#include "SimulationClock.h"

class SimulationControl {
 public:
  SimulationControl(SimCamperNetwork& network, SimModbusCycleSource& modbus,
                    SimulationClock& clock);

  void poll();
  bool processLine(const char* line, char* response, size_t responseCapacity);

 private:
  static constexpr size_t kMaximumLineLength = 95;

  SimCamperNetwork& network_;
  SimModbusCycleSource& modbus_;
  SimulationClock& clock_;
  char line_[kMaximumLineLength + 1] = {};
  size_t lineLength_ = 0;
  bool lineOverflow_ = false;
};

#endif
