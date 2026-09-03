#pragma once

#include "ModbusSnapshotPolicy.h"

class ModbusCycleSource {
 public:
  virtual ~ModbusCycleSource() = default;
  virtual bool fetch(ModbusReadCycle& cycle) = 0;
};
