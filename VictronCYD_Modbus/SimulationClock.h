#pragma once

#ifdef CYD_SIMULATION

#include <cstdint>

class SimulationClock {
 public:
  const char* text() const;
  uint32_t epoch() const;
  bool setFixture(const char* fixture);
  void resetFixture();

 private:
  enum class Fixture { Fixed, Morning, Evening, Unavailable };
  Fixture fixture_ = Fixture::Fixed;
};

#endif
