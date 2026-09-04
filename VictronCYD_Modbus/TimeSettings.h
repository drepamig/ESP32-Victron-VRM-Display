#pragma once

#include <time.h>
#include "TimeZoneCatalog.h"

enum class ClockFormat { TwelveHour, TwentyFourHour };

struct TimeSettings {
  const char* zoneId = "America/Chicago";
  ClockFormat format = ClockFormat::TwelveHour;
};

bool sameTimeSettings(const TimeSettings& left, const TimeSettings& right);

class TimeSettingsStore {
 public:
  bool load(TimeSettings& out);
  bool save(const TimeSettings& value);
};

bool applyTimeSettings(const TimeSettings& value);
bool saveAndApplyTimeSettings(TimeSettingsStore& store, TimeSettings& active,
                              const TimeSettings& candidate);

struct ClockText {
  char time[6];
  char meridiem[3];
};

ClockText formatClock(time_t utcEpoch, ClockFormat format);
