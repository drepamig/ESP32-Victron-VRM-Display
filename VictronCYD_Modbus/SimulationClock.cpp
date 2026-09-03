#include "SimulationClock.h"

#ifdef CYD_SIMULATION

#include <cstring>

const char* SimulationClock::text() const {
  switch (fixture_) {
    case Fixture::Morning:
      return "08:15";
    case Fixture::Evening:
      return "21:45";
    case Fixture::Unavailable:
      return "--:--";
    case Fixture::Fixed:
    default:
      return "12:34";
  }
}

bool SimulationClock::setFixture(const char* fixture) {
  if (fixture == nullptr) return false;
  if (std::strcmp(fixture, "fixed") == 0) {
    fixture_ = Fixture::Fixed;
  } else if (std::strcmp(fixture, "morning") == 0) {
    fixture_ = Fixture::Morning;
  } else if (std::strcmp(fixture, "evening") == 0) {
    fixture_ = Fixture::Evening;
  } else if (std::strcmp(fixture, "unavailable") == 0) {
    fixture_ = Fixture::Unavailable;
  } else {
    return false;
  }
  return true;
}

void SimulationClock::resetFixture() {
  fixture_ = Fixture::Fixed;
}

#endif
