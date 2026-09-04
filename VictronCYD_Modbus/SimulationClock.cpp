#include "SimulationClock.h"

#ifdef CYD_SIMULATION

#include <cstring>

uint32_t SimulationClock::epoch() const {
  // Fixed UTC instants on 2026-09-03 keep persisted profile metadata repeatable.
  switch (fixture_) {
    case Fixture::Morning: return 1788423300U;
    case Fixture::Evening: return 1788471900U;
    case Fixture::Unavailable: return 0;
    case Fixture::SpringBefore: return 1805011199U;
    case Fixture::SpringAfter: return 1805011200U;
    case Fixture::FallBefore: return 1793516399U;
    case Fixture::FallAfter: return 1793516400U;
    case Fixture::Fixed:
    default: return 1788438840U;
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
  } else if (std::strcmp(fixture, "springbefore") == 0) {
    fixture_ = Fixture::SpringBefore;
  } else if (std::strcmp(fixture, "springafter") == 0) {
    fixture_ = Fixture::SpringAfter;
  } else if (std::strcmp(fixture, "fallbefore") == 0) {
    fixture_ = Fixture::FallBefore;
  } else if (std::strcmp(fixture, "fallafter") == 0) {
    fixture_ = Fixture::FallAfter;
  } else {
    return false;
  }
  return true;
}

void SimulationClock::resetFixture() {
  fixture_ = Fixture::Fixed;
}

#endif
