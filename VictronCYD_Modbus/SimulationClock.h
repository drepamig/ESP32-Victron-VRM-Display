#pragma once

#ifdef CYD_SIMULATION

class SimulationClock {
 public:
  const char* text() const;
  bool setFixture(const char* fixture);
  void resetFixture();

 private:
  enum class Fixture { Fixed, Morning, Evening, Unavailable };
  Fixture fixture_ = Fixture::Fixed;
};

#endif
