#pragma once

#ifdef CYD_SIMULATION

#include <atomic>

#include "ModbusCycleSource.h"

class SimModbusCycleSource final : public ModbusCycleSource {
 public:
  explicit SimModbusCycleSource(const char* ignoredAddress = nullptr);
  void setAddress(uint32_t address) { targetAddress_ = address; }
  bool fetch(ModbusReadCycle& cycle) override;
  bool setFixture(const char* fixture);
  void resetFixture();

 private:
  enum class Fixture : uint8_t { Nominal, Stale, Offline, Partial };
  std::atomic<Fixture> fixture_{Fixture::Nominal};
  uint32_t targetAddress_ = 0;
};

#endif
