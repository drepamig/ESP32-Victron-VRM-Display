#pragma once

#ifdef CYD_SIMULATION

#include <cstdint>

class SimulationClock {
 public:
  uint32_t epoch() const;
  bool setFixture(const char* fixture);
  void resetFixture();

 private:
  enum class Fixture { Fixed, Morning, Evening, Unavailable,
                       SpringBefore, SpringAfter, FallBefore, FallAfter };
  Fixture fixture_ = Fixture::Fixed;
};

#endif
