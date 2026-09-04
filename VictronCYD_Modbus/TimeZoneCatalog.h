#pragma once

#include <stddef.h>

struct TimeZone {
  const char* id;
  const char* country;
  const char* label;
  const char* posix;
};

size_t timeZoneCount();
const TimeZone* timeZoneAt(size_t index);
const TimeZone* findTimeZone(const char* id);
