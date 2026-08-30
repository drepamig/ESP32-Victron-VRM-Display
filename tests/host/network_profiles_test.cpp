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

void testRestartRecoversPersistentFailure() {
  Preferences::reset();
  NetworkProfileStore store;
  check(store.begin(), "restart recovery setup");
  check(insert(store, profile("durable", "prior", 1, 100)), "restart recovery add initial");
  check(store.activate(0), "restart recovery activate initial");
  Preferences::failFromMutation(8);
  size_t storedIndex = 0;
  check(!store.upsert(profile("durable", "next", 8, 800), storedIndex),
        "persistent failure returned");
  check(!store.begin(), "persistent failure prevents immediate recovery");

  Preferences::clearFaults();
  NetworkProfileStore restartedStore;
  check(restartedStore.begin(), "restart recovery begin");
  NetworkProfile loaded;
  check(restartedStore.count() == 1 && restartedStore.activeIndex() == 0,
        "restart recovery preserves count and active index");
  check(restartedStore.load(0, loaded) && loaded.securityType == 1 && loaded.lastSuccessEpoch == 100,
        "restart recovery restores prior logical profile");
}

void testAtomicUpsertAndActivateSuccessCases() {
  Preferences::reset();
  NetworkProfileStore store;
  check(store.begin(), "atomic success setup");
  check(insert(store, profile("zero", "a", 1, 10)), "atomic add zero");
  check(insert(store, profile("one", "b", 1, 20)), "atomic add one");
  check(store.activate(1), "atomic activate initial one");

  size_t storedIndex = NetworkProfileStore::kMaxProfiles;
  check(store.upsertAndActivate(profile("new", "c", 3, 30), storedIndex) &&
            storedIndex == 2 && store.count() == 3 && store.activeIndex() == 2,
        "atomic append stores and activates in one operation");

  check(store.upsertAndActivate(profile("new", "updated", 8, 80), storedIndex) &&
            storedIndex == 2 && store.count() == 3 && store.activeIndex() == 2,
        "atomic reprovision of active SSID updates in place and stays active");
  NetworkProfile loaded;
  check(store.load(2, loaded) && loaded.passphrase == String("updated") &&
            loaded.securityType == 8 && loaded.lastSuccessEpoch == 80,
        "atomic active-SSID reprovision persists exact replacement profile");
}

void testAtomicFullCapacityEvictionBecomesActive() {
  Preferences::reset();
  NetworkProfileStore store;
  check(store.begin(), "atomic full setup");
  check(insert(store, profile("zero", "a", 1, 10)), "atomic full add zero");
  check(insert(store, profile("one", "b", 1, 20)), "atomic full add one");
  check(insert(store, profile("two", "c", 1, 30)), "atomic full add two");
  check(insert(store, profile("three", "d", 1, 40)), "atomic full add three");
  check(insert(store, profile("four", "e", 1, 50)), "atomic full add four");
  check(store.activate(2), "atomic full activate protected profile");

  size_t storedIndex = NetworkProfileStore::kMaxProfiles;
  check(store.upsertAndActivate(profile("replacement", "f", 4, 60), storedIndex) &&
            storedIndex == 0 && store.count() == 5 && store.activeIndex() == 0,
        "atomic full insert evicts oldest non-active and activates replacement");
  NetworkProfile loaded;
  check(store.load(0, loaded) && loaded.ssid == String("replacement") &&
            store.load(2, loaded) && loaded.ssid == String("two"),
        "atomic full insert preserves the formerly active protected record");
}

void testAtomicCommitFailurePreservesExactPriorSnapshot() {
  Preferences::reset();
  NetworkProfileStore store;
  check(store.begin(), "atomic rollback setup");
  check(insert(store, profile("zero", "a", 1, 10)), "atomic rollback add zero");
  check(insert(store, profile("one", "b", 2, 20)), "atomic rollback add one");
  check(insert(store, profile("two", "c", 3, 30)), "atomic rollback add two");
  check(store.activate(1), "atomic rollback activate one");

  Preferences::failOnMutation(16);
  size_t storedIndex = 77;
  check(!store.upsertAndActivate(profile("replacement", "new", 9, 90), storedIndex) &&
            storedIndex == 77,
        "atomic injected canonical-write failure returns without publishing an index");
  check(store.count() == 3 && store.activeIndex() == 1,
        "atomic failure preserves exact prior count and active index");
  const NetworkProfile expected[3]{{"zero", "a", 1, 10},
                                   {"one", "b", 2, 20},
                                   {"two", "c", 3, 30}};
  for (size_t index = 0; index < 3; ++index) {
    NetworkProfile loaded;
    check(store.load(index, loaded) && loaded.ssid == expected[index].ssid &&
              loaded.passphrase == expected[index].passphrase &&
              loaded.securityType == expected[index].securityType &&
              loaded.lastSuccessEpoch == expected[index].lastSuccessEpoch,
          "atomic failure preserves every prior profile field");
  }
}

}  // namespace

int main() {
  testAppendAndUpdate();
  testReplacementPreservesActive();
  testCorruptMetadataIsRepaired();
  testOpenPassword();
  testDeletionAndClear();
  testTransientWriteFailureRestoresPriorStore();
  testRestartRecoversPersistentFailure();
  testAtomicUpsertAndActivateSuccessCases();
  testAtomicFullCapacityEvictionBecomesActive();
  testAtomicCommitFailurePreservesExactPriorSnapshot();
  return failures == 0 ? 0 : 1;
}
