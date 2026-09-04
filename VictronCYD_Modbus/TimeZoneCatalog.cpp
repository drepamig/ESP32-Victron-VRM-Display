#include "TimeZoneCatalog.h"

#include <string.h>

namespace {
#include "TimeZoneCatalog.inc"
}  // namespace

size_t timeZoneCount() { return sizeof(kTimeZones) / sizeof(kTimeZones[0]); }

const TimeZone* timeZoneAt(size_t index) {
  return index < timeZoneCount() ? &kTimeZones[index] : nullptr;
}

const TimeZone* findTimeZone(const char* id) {
  if (id == nullptr) return nullptr;
  for (size_t index = 0; index < timeZoneCount(); ++index) {
    if (strcmp(kTimeZones[index].id, id) == 0) return &kTimeZones[index];
  }
  return nullptr;
}
