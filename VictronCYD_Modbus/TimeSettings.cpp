#include "TimeSettings.h"

#include <Preferences.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {
constexpr char kNamespace[] = "timesettings";
constexpr char kRecordKey[] = "record";
constexpr size_t kMaxRecordLength = 80;
constexpr time_t kEarliestClockEpoch = 1483228800;  // 2017-01-01 UTC

bool validFormat(ClockFormat format) {
  return format == ClockFormat::TwelveHour || format == ClockFormat::TwentyFourHour;
}

bool validSettings(const TimeSettings& value) {
  return validFormat(value.format) && findTimeZone(value.zoneId) != nullptr;
}

void defaultSettings(TimeSettings& out) {
  out = TimeSettings{};
  out.zoneId = findTimeZone(out.zoneId)->id;
}

bool readRecord(Preferences& preferences, TimeSettings& out) {
  if (preferences.getType(kRecordKey) != PT_STR) return false;
  const String record = preferences.getString(kRecordKey, String());
  if (record.length() <= 5 || record.length() > kMaxRecordLength ||
      strlen(record.c_str()) != record.length()) return false;
  ClockFormat format;
  if (strncmp(record.c_str(), "1|12|", 5) == 0) {
    format = ClockFormat::TwelveHour;
  } else if (strncmp(record.c_str(), "1|24|", 5) == 0) {
    format = ClockFormat::TwentyFourHour;
  } else {
    return false;
  }
  const TimeZone* zone = findTimeZone(record.c_str() + 5);
  if (zone == nullptr) return false;
  out = {zone->id, format};
  return true;
}
}  // namespace

bool sameTimeSettings(const TimeSettings& left, const TimeSettings& right) {
  return left.zoneId && right.zoneId && left.format == right.format &&
         strcmp(left.zoneId, right.zoneId) == 0;
}

bool TimeSettingsStore::load(TimeSettings& out) {
  defaultSettings(out);
  Preferences preferences;
  if (!preferences.begin(kNamespace, true)) return false;
  const bool loaded = readRecord(preferences, out);
  preferences.end();
  return loaded;
}

bool TimeSettingsStore::save(const TimeSettings& value) {
  if (!validSettings(value)) return false;
  Preferences preferences;
  // A write-capable open creates the namespace for the first explicit Save.
  // load() is read-only and never initializes or repairs NVS.
  if (!preferences.begin(kNamespace, false)) return false;
  TimeSettings previous;
  readRecord(preferences, previous);  // Missing/invalid records have effective defaults.
  if (sameTimeSettings(previous, value)) {
    preferences.end();
    return true;
  }
  char record[kMaxRecordLength + 1];
  const int length = snprintf(record, sizeof(record), "1|%s|%s",
                              value.format == ClockFormat::TwelveHour ? "12" : "24", value.zoneId);
  // One NVS string entry keeps zone and format together; no partial two-key save.
  const bool saved = length > 0 && static_cast<size_t>(length) < sizeof(record) &&
                     preferences.putString(kRecordKey, record) == static_cast<size_t>(length);
  preferences.end();
  return saved;
}

bool applyTimeSettings(const TimeSettings& value) {
  if (!validFormat(value.format)) return false;
  const TimeZone* zone = findTimeZone(value.zoneId);
  if (zone == nullptr || setenv("TZ", zone->posix, 1) != 0) return false;
  tzset();
  return true;
}

bool saveAndApplyTimeSettings(TimeSettingsStore& store, TimeSettings& active,
                              const TimeSettings& candidate) {
  if (!validSettings(active) || !validSettings(candidate)) return false;
  if (sameTimeSettings(active, candidate)) return true;
  if (!applyTimeSettings(candidate)) return false;
  if (!store.save(candidate)) {
    applyTimeSettings(active);
    return false;
  }
  active = {findTimeZone(candidate.zoneId)->id, candidate.format};
  return true;
}

ClockText formatClock(time_t utcEpoch, ClockFormat format) {
  ClockText result{"--:--", ""};
  if (utcEpoch < kEarliestClockEpoch || !validFormat(format)) return result;
  tm local{};
  if (localtime_r(&utcEpoch, &local) == nullptr) return result;
  if (format == ClockFormat::TwentyFourHour) {
    // tm fields have defined bounds after a successful localtime_r conversion.
    result.time[0] = static_cast<char>('0' + local.tm_hour / 10);
    result.time[1] = static_cast<char>('0' + local.tm_hour % 10);
    result.time[2] = ':';
    result.time[3] = static_cast<char>('0' + local.tm_min / 10);
    result.time[4] = static_cast<char>('0' + local.tm_min % 10);
    result.time[5] = '\0';
  } else {
    const unsigned hour = local.tm_hour % 12 == 0 ? 12 : local.tm_hour % 12;
    // A 12-hour clock can be four or five characters (h:mm or hh:mm).
    const size_t digits = hour >= 10 ? 2 : 1;
    if (digits == 2) result.time[0] = '1';
    result.time[digits - 1] = static_cast<char>('0' + hour % 10);
    result.time[digits] = ':';
    result.time[digits + 1] = static_cast<char>('0' + local.tm_min / 10);
    result.time[digits + 2] = static_cast<char>('0' + local.tm_min % 10);
    result.time[digits + 3] = '\0';
    memcpy(result.meridiem, local.tm_hour < 12 ? "AM" : "PM", 3);
  }
  return result;
}
