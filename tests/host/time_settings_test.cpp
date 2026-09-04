#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>

#include "Preferences.h"
#include "../../VictronCYD_Modbus/TimeSettings.h"

namespace { bool failNextSetenv = false; }
extern "C" int __real_setenv(const char*, const char*, int);
extern "C" int __wrap_setenv(const char* name, const char* value, int replace) {
  if (failNextSetenv) {
    failNextSetenv = false;
    return -1;
  }
  return __real_setenv(name, value, replace);
}

namespace {
int failures = 0;

void check(bool condition, const std::string& name) {
  if (!condition) {
    std::cerr << "FAIL: " << name << '\n';
    ++failures;
  }
}

time_t epoch(int year, int month, int day, int hour = 0, int minute = 0, int second = 0) {
  tm value{};
  value.tm_year = year - 1900;
  value.tm_mon = month - 1;
  value.tm_mday = day;
  value.tm_hour = hour;
  value.tm_min = minute;
  value.tm_sec = second;
  return timegm(&value);
}

bool defaults(const TimeSettings& value) {
  return value.zoneId && std::strcmp(value.zoneId, "America/Chicago") == 0 &&
         value.format == ClockFormat::TwelveHour;
}

std::string rawRecord() {
  Preferences preferences;
  if (!preferences.begin("timesettings", true)) return "<read failed>";
  const std::string result = preferences.getString("record", "<missing>").c_str();
  preferences.end();
  return result;
}

void expectClock(time_t utc, ClockFormat format, const char* clock, const char* meridiem) {
  const ClockText text = formatClock(utc, format);
  check(std::strcmp(text.time, clock) == 0 && std::strcmp(text.meridiem, meridiem) == 0,
        std::string("clock expected ") + clock + " " + meridiem + " got " + text.time + " " + text.meridiem);
}

void testMissingDoesNotWrite() {
  Preferences::reset();
  Preferences::failOnMutation(1);
  TimeSettingsStore store;
  TimeSettings loaded{"UTC", ClockFormat::TwentyFourHour};
  check(!store.load(loaded) && defaults(loaded), "missing settings fall back to Chicago/12h");
  check(rawRecord() == "<missing>", "load does not initialize NVS");
  check(store.save(loaded), "saving unchanged effective defaults succeeds");
  check(rawRecord() == "<missing>", "unchanged defaults do not write NVS");
  check(!store.save({"UTC", ClockFormat::TwentyFourHour}), "first actual change encounters first write failure");
}

void testRoundTripAndNoOp() {
  Preferences::reset();
  TimeSettingsStore store;
  check(store.save({"America/St_Johns", ClockFormat::TwentyFourHour}), "save both preferences");
  check(rawRecord() == "1|24|America/St_Johns", "preferences persist in one versioned record");
  TimeSettings loaded;
  TimeSettingsStore reconstructed;
  check(reconstructed.load(loaded), "reconstructed store loads persisted preferences");
  const TimeZone* zone = findTimeZone("America/St_Johns");
  check(zone && loaded.zoneId == zone->id && loaded.format == ClockFormat::TwentyFourHour,
        "load returns static named catalog string and format");
  char equalId[] = "America/St_Johns";
  check(sameTimeSettings(loaded, {equalId, ClockFormat::TwentyFourHour}), "settings compare IDs by value");
  check(!sameTimeSettings(loaded, {equalId, ClockFormat::TwelveHour}), "format change is a real change");
  check(!sameTimeSettings(loaded, {nullptr, ClockFormat::TwentyFourHour}), "comparison handles invalid ID safely");
  Preferences::failOnMutation(1);
  check(reconstructed.save({equalId, ClockFormat::TwentyFourHour}), "no-op save avoids failing write");
  check(!reconstructed.save({"UTC", ClockFormat::TwelveHour}), "following change still encounters first write");
  check(reconstructed.load(loaded) && std::strcmp(loaded.zoneId, "America/St_Johns") == 0 &&
            loaded.format == ClockFormat::TwentyFourHour,
        "failed atomic record write preserves both previous settings");
}

void testCorruptAndReadFailure() {
  const char* invalidRecords[] = {
      "", "1", "1|12", "1|12|", "2|12|UTC", "0|24|UTC", "1|13|UTC", "1|012|UTC",
      "1|12|Europe/London", "1|24|America/Chicago|junk", "1|24|UTC\n", "1|12|../UTC",
      "1|12|america/chicago", "1|12|America/ChicagoXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"};
  for (const char* record : invalidRecords) {
    Preferences::reset();
    Preferences::putRawString("timesettings", "record", record);
    TimeSettings loaded{"UTC", ClockFormat::TwentyFourHour};
    TimeSettingsStore store;
    check(!store.load(loaded) && defaults(loaded), std::string("invalid record falls back: ") + record);
    check(rawRecord() == record, "invalid read does not repair NVS automatically");
  }
  Preferences::reset();
  Preferences::putRawUChar("timesettings", "record", 1);
  TimeSettings loaded;
  TimeSettingsStore store;
  check(!store.load(loaded) && defaults(loaded), "wrong NVS type falls back");
  check(store.save({"UTC", ClockFormat::TwentyFourHour}), "explicit changed save replaces invalid record");
  Preferences::failOnReadOpen(1);
  loaded = {"America/Phoenix", ClockFormat::TwentyFourHour};
  check(!store.load(loaded) && defaults(loaded), "read open failure falls back");
  Preferences::clearFaults();
  check(store.load(loaded) && std::strcmp(loaded.zoneId, "UTC") == 0,
        "read failure does not erase saved preference");
}

void testIsolationAndExplicitApply() {
  Preferences::reset();
  Preferences::putRawString("wanprofiles", "record", "WAN sentinel");
  Preferences::putRawString("campernet", "record", "AP sentinel");
  check(applyTimeSettings({"UTC", ClockFormat::TwentyFourHour}), "apply UTC");
  TimeSettingsStore store;
  check(store.save({"America/Chicago", ClockFormat::TwentyFourHour}), "save draft preferences");
  expectClock(epoch(2027, 1, 1, 3, 5), ClockFormat::TwentyFourHour, "03:05", "");
  TimeSettings loaded;
  check(store.load(loaded), "load saved draft without apply");
  expectClock(epoch(2027, 1, 1, 3, 5), ClockFormat::TwentyFourHour, "03:05", "");
  Preferences::failOnMutation(1);
  check(!store.save({"America/Phoenix", ClockFormat::TwelveHour}), "failed draft save");
  expectClock(epoch(2027, 1, 1, 3, 5), ClockFormat::TwentyFourHour, "03:05", "");
  check(!store.save({"bad", ClockFormat::TwelveHour}) &&
            !store.save({nullptr, ClockFormat::TwelveHour}) &&
            !store.save({"UTC", static_cast<ClockFormat>(99)}),
        "invalid settings rejected before persistence");
  check(!applyTimeSettings({"bad", ClockFormat::TwelveHour}) &&
            !applyTimeSettings({"UTC", static_cast<ClockFormat>(99)}),
        "invalid apply rejected");
  expectClock(epoch(2027, 1, 1, 3, 5), ClockFormat::TwentyFourHour, "03:05", "");
  for (const char* ns : {"wanprofiles", "campernet"}) {
    Preferences preferences;
    preferences.begin(ns, true);
    const char* expected = std::strcmp(ns, "wanprofiles") == 0 ? "WAN sentinel" : "AP sentinel";
    check(preferences.getString("record") == String(expected),
          "other preferences namespace remains intact");
    preferences.end();
  }
}

void testSaveAndApplyIsAtomicToCaller() {
  Preferences::reset();
  TimeSettingsStore store;
  TimeSettings active{"UTC", ClockFormat::TwentyFourHour};
  check(store.save(active) && applyTimeSettings(active), "prepare active UTC settings");
  Preferences::failOnMutation(1);
  check(saveAndApplyTimeSettings(store, active, {"UTC", ClockFormat::TwentyFourHour}),
        "unchanged save/apply avoids writes");
  const TimeSettings changed{"America/Chicago", ClockFormat::TwelveHour};
  check(!saveAndApplyTimeSettings(store, active, changed), "save/apply reports persistence failure");
  check(std::strcmp(active.zoneId, "UTC") == 0 && active.format == ClockFormat::TwentyFourHour,
        "failed save/apply preserves active settings model");
  expectClock(epoch(2027, 1, 1, 12), active.format, "12:00", "");
  check(rawRecord() == "1|24|UTC", "failed save/apply preserves prior record");
  failNextSetenv = true;
  check(!saveAndApplyTimeSettings(store, active, changed), "save/apply reports TZ application failure");
  check(std::strcmp(active.zoneId, "UTC") == 0 && rawRecord() == "1|24|UTC",
        "failed TZ apply leaves runtime model and NVS unchanged");
  expectClock(epoch(2027, 1, 1, 12), active.format, "12:00", "");
  check(saveAndApplyTimeSettings(store, active, changed), "save/apply commits valid changed settings");
  const TimeZone* chicago = findTimeZone("America/Chicago");
  check(chicago && active.zoneId == chicago->id && active.format == ClockFormat::TwelveHour,
        "successful save/apply owns a stable catalog ID");
  expectClock(epoch(2027, 1, 1, 12), active.format, "6:00", "AM");
}

void testFormattingBoundaries() {
  check(applyTimeSettings({"UTC", ClockFormat::TwelveHour}), "apply UTC for formatting");
  expectClock(0, ClockFormat::TwelveHour, "--:--", "");
  expectClock(epoch(2017, 1, 1) - 1, ClockFormat::TwentyFourHour, "--:--", "");
  expectClock(epoch(2017, 1, 1), ClockFormat::TwelveHour, "12:00", "AM");
  expectClock(epoch(2027, 1, 1), ClockFormat::TwentyFourHour, "00:00", "");
  expectClock(epoch(2027, 1, 1, 12), ClockFormat::TwelveHour, "12:00", "PM");
  expectClock(epoch(2027, 1, 1, 13, 7), ClockFormat::TwelveHour, "1:07", "PM");
  expectClock(epoch(2027, 1, 1, 23, 59), ClockFormat::TwentyFourHour, "23:59", "");
  check(applyTimeSettings({"America/Chicago", ClockFormat::TwelveHour}), "apply Chicago");
  expectClock(epoch(2027, 1, 1, 3, 5), ClockFormat::TwelveHour, "9:05", "PM");
  time_t utc = epoch(2027, 1, 1, 3, 5);
  tm local{};
  localtime_r(&utc, &local);
  check(local.tm_year == 126 && local.tm_mon == 11 && local.tm_mday == 31,
        "UTC-to-local conversion rolls calendar into previous year");
  check(applyTimeSettings({"America/St_Johns", ClockFormat::TwentyFourHour}), "apply half-hour Newfoundland");
  expectClock(epoch(2027, 1, 1, 12), ClockFormat::TwentyFourHour, "08:30", "");
  expectClock(epoch(2027, 7, 1, 12), ClockFormat::TwelveHour, "9:30", "AM");
}

void testDstAndRegionalExceptions() {
  check(applyTimeSettings({"America/Chicago", ClockFormat::TwentyFourHour}), "apply DST region");
  expectClock(epoch(2026, 11, 1, 6, 59), ClockFormat::TwentyFourHour, "01:59", "");
  expectClock(epoch(2026, 11, 1, 7), ClockFormat::TwentyFourHour, "01:00", "");
  expectClock(epoch(2027, 3, 14, 7, 59), ClockFormat::TwentyFourHour, "01:59", "");
  expectClock(epoch(2027, 3, 14, 8), ClockFormat::TwentyFourHour, "03:00", "");
  struct FixedZone { const char* id; const char* noonUtc; };
  for (const FixedZone& zone : {FixedZone{"America/Phoenix", "05:00"}, {"Pacific/Honolulu", "02:00"},
                              {"America/Regina", "06:00"}, {"America/Whitehorse", "05:00"},
                              {"America/Mexico_City", "06:00"}, {"America/Cancun", "07:00"},
                              {"America/Edmonton", "06:00"}, {"America/Vancouver", "05:00"}}) {
    check(applyTimeSettings({zone.id, ClockFormat::TwentyFourHour}), std::string("apply exception ") + zone.id);
    expectClock(epoch(2027, 1, 15, 12), ClockFormat::TwentyFourHour, zone.noonUtc, "");
    expectClock(epoch(2027, 7, 15, 12), ClockFormat::TwentyFourHour, zone.noonUtc, "");
  }
  check(applyTimeSettings({"America/Tijuana", ClockFormat::TwentyFourHour}), "apply Mexico border DST");
  expectClock(epoch(2027, 1, 15, 12), ClockFormat::TwentyFourHour, "04:00", "");
  expectClock(epoch(2027, 7, 15, 12), ClockFormat::TwentyFourHour, "05:00", "");
  check(applyTimeSettings({"America/Inuvik", ClockFormat::TwentyFourHour}), "apply NWT distinct from Alberta");
  expectClock(epoch(2027, 1, 15, 12), ClockFormat::TwentyFourHour, "05:00", "");
  expectClock(epoch(2027, 7, 15, 12), ClockFormat::TwentyFourHour, "06:00", "");
}

void testEntireCatalogAgainstPinnedOracle() {
  check(timeZoneAt(timeZoneCount()) == nullptr && findTimeZone(nullptr) == nullptr &&
            findTimeZone("Europe/London") == nullptr,
        "catalog rejects invalid lookup and indices");
  std::ifstream oracle("tests/host/fixtures/timezone_oracle.tsv");
  check(oracle.good(), "pinned IANA timezone oracle is available");
  std::set<std::string> exercised;
  std::string line;
  size_t samples = 0;
  while (std::getline(oracle, line)) {
    if (line.empty() || line.front() == '#') continue;
    std::istringstream row(line);
    std::string id;
    long long timestamp;
    long offset;
    row >> id >> timestamp >> offset;
    check(!row.fail(), "oracle row parses");
    if (row.fail()) continue;
    const TimeZone* zone = findTimeZone(id.c_str());
    check(zone && applyTimeSettings({id.c_str(), ClockFormat::TwentyFourHour}), "apply oracle zone " + id);
    if (!zone) continue;
    const time_t utc = static_cast<time_t>(timestamp);
    const time_t shifted = utc + offset;
    tm expected{}, actual{};
    gmtime_r(&shifted, &expected);
    localtime_r(&utc, &actual);
    check(actual.tm_year == expected.tm_year && actual.tm_mon == expected.tm_mon &&
              actual.tm_mday == expected.tm_mday && actual.tm_hour == expected.tm_hour &&
              actual.tm_min == expected.tm_min && actual.tm_sec == expected.tm_sec,
          "pinned TZif offset/date agrees for " + id + " at " + std::to_string(timestamp));
    exercised.insert(id);
    ++samples;
  }
  check(exercised.size() == timeZoneCount() && samples > timeZoneCount() * 20,
        "oracle covers every selectable zone across seasons and transition boundaries");
  for (size_t index = 0; index < timeZoneCount(); ++index) {
    const TimeZone* zone = timeZoneAt(index);
    check(zone && findTimeZone(zone->id) == zone && exercised.count(zone->id) != 0,
          "indexed catalog entry has unique stable ID and oracle coverage");
  }
}
}  // namespace

int main() {
  testMissingDoesNotWrite();
  testRoundTripAndNoOp();
  testCorruptAndReadFailure();
  testIsolationAndExplicitApply();
  testSaveAndApplyIsAtomicToCaller();
  testFormattingBoundaries();
  testDstAndRegionalExceptions();
  testEntireCatalogAgainstPinnedOracle();
  if (failures) return 1;
  std::cout << "time_settings_test: all passed\n";
  return 0;
}
