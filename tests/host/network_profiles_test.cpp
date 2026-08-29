#include <cstdint>
#include <iostream>

#include "Preferences.h"
#include "../../VictronCYD_Modbus/NetworkProfiles.h"

namespace {

int failures = 0;

void check(bool condition, const char* name) {
  if (!condition) {
    std::cerr << "FAIL: " << name << '\n';
    ++failures;
  }
}

NetworkProfile profile(const char* ssid, const char* passphrase, uint8_t security, uint32_t seen) {
  NetworkProfile value;
  value.ssid = String(ssid);
  value.passphrase = String(passphrase);
  value.securityType = security;
  value.lastSuccessEpoch = seen;
  return value;
}

bool insert(NetworkProfileStore& store, const NetworkProfile& value) {
  size_t index = NetworkProfileStore::kMaxProfiles;
  return store.upsert(value, index) && index < NetworkProfileStore::kMaxProfiles;
}

void testAppendAndUpdate() {
  Preferences::reset();
  NetworkProfileStore store;
  check(store.begin(), "append setup");
  check(insert(store, profile("one", "first", 1, 10)), "append profile");
  check(insert(store, profile("one", "second", 2, 20)), "update matching ssid");
  NetworkProfile loaded;
  check(store.count() == 1, "update keeps count");
  check(store.load(0, loaded) && loaded.securityType == 2 && loaded.lastSuccessEpoch == 20,
        "update persists metadata");
}

void testReplacementPreservesActive() {
  Preferences::reset();
  NetworkProfileStore store;
  check(store.begin(), "replacement setup");
  check(insert(store, profile("zero", "a", 1, 10)), "replacement add zero");
  check(insert(store, profile("one", "b", 1, 20)), "replacement add one");
  check(insert(store, profile("two", "c", 1, 30)), "replacement add two");
  check(insert(store, profile("three", "d", 1, 40)), "replacement add three");
  check(insert(store, profile("four", "e", 1, 50)), "replacement add four");
  check(store.activate(2), "replacement activate");
  check(insert(store, profile("new", "f", 1, 60)), "replacement insert full");
  NetworkProfile loaded;
  check(store.count() == 5, "replacement keeps bounded count");
  check(store.activeIndex() == 2, "replacement protects active index");
  check(store.load(0, loaded) && loaded.ssid == String("new"), "replacement chooses oldest non-active");
}

void testCorruptMetadataIsRepaired() {
  Preferences::reset();
  Preferences::putRawUChar("wanprofiles", "count", 6);
  Preferences::putRawChar("wanprofiles", "active", 0);
  NetworkProfileStore store;
  check(store.begin(), "corrupt metadata setup repairs");
  check(store.count() == 0, "corrupt count does not create phantom profiles");
  check(store.activeIndex() == -1, "corrupt active index resets");
  check(!store.activate(0), "phantom profile activation rejected");

  Preferences::putRawString("wanprofiles", "count", "invalid-type");
  Preferences::putRawChar("wanprofiles", "active", -1);
  check(store.begin() && store.count() == 0, "corrupt metadata type resets");
}

void testOpenPassword() {
  Preferences::reset();
  NetworkProfileStore store;
  check(store.begin(), "open password setup");
  check(insert(store, profile("open", "", 0, 1)), "open password persists");
  NetworkProfile loaded;
  check(store.load(0, loaded) && loaded.passphrase.isEmpty(), "open password loads empty");
}

void testDeletionAndClear() {
  Preferences::reset();
  NetworkProfileStore store;
  check(store.begin(), "delete setup");
  check(insert(store, profile("first", "a", 1, 1)), "delete add first");
  check(insert(store, profile("middle", "b", 1, 2)), "delete add middle");
  check(insert(store, profile("last", "c", 1, 3)), "delete add last");
  check(store.activate(2), "delete activate last");
  check(store.erase(1), "delete middle");
  NetworkProfile loaded;
  check(store.count() == 2 && store.activeIndex() == 1, "delete compacts active index");
  check(store.load(1, loaded) && loaded.ssid == String("last"), "delete compacts profile data");
  check(store.erase(1) && store.activeIndex() == -1, "delete active clears active index");
  check(store.clearUpstreamProfiles() && store.count() == 0 && store.activeIndex() == -1,
        "clear removes upstream profiles");
}

void testTransientWriteFailureRestoresPriorStore() {
  Preferences::reset();
  NetworkProfileStore store;
  check(store.begin(), "rollback setup");
  check(insert(store, profile("stable", "prior", 1, 100)), "rollback add initial");
  Preferences::failOnMutation(4);
  size_t storedIndex = 0;
  check(!store.upsert(profile("stable", "next", 8, 800), storedIndex), "injected write failure returned");
  NetworkProfile loaded;
  check(store.load(0, loaded) && loaded.securityType == 1 && loaded.lastSuccessEpoch == 100,
        "injected failure preserves prior logical profile");
}

}  // namespace

int main() {
  testAppendAndUpdate();
  testReplacementPreservesActive();
  testCorruptMetadataIsRepaired();
  testOpenPassword();
  testDeletionAndClear();
  testTransientWriteFailureRestoresPriorStore();
  return failures == 0 ? 0 : 1;
}
