#include "NetworkProfiles.h"

#include <Preferences.h>

namespace {

constexpr char kNamespace[] = "wanprofiles";
constexpr char kCountKey[] = "count";
constexpr char kActiveKey[] = "active";
constexpr char kBackupCountKey[] = "bcount";
constexpr char kBackupActiveKey[] = "bactive";
constexpr char kTransactionKey[] = "txn";

struct StoreSnapshot {
  size_t count = 0;
  int active = -1;
  NetworkProfile profiles[NetworkProfileStore::kMaxProfiles];
};

void profileKey(char* key, const char* prefix, size_t index, bool backup) {
  size_t keyLength = 0;
  if (backup) {
    key[keyLength++] = 'b';
  }
  for (size_t character = 0; prefix[character] != '\0'; ++character) {
    key[keyLength++] = prefix[character];
  }
  key[keyLength++] = static_cast<char>('0' + index);
  key[keyLength] = '\0';
}

void profileKeys(size_t index, bool backup, char* ssidKey, char* passphraseKey, char* securityKey, char* seenKey) {
  profileKey(ssidKey, "ssid", index, backup);
  profileKey(passphraseKey, "pass", index, backup);
  profileKey(securityKey, "sec", index, backup);
  profileKey(seenKey, "seen", index, backup);
}

bool readProfile(Preferences& preferences, size_t index, bool backup, NetworkProfile& out) {
  char ssidKey[7];
  char passphraseKey[7];
  char securityKey[7];
  char seenKey[7];
  profileKeys(index, backup, ssidKey, passphraseKey, securityKey, seenKey);

  if (preferences.getType(ssidKey) != PT_STR || preferences.getType(passphraseKey) != PT_STR ||
      preferences.getType(securityKey) != PT_U8 || preferences.getType(seenKey) != PT_U32) {
    return false;
  }

  NetworkProfile profile;
  profile.ssid = preferences.getString(ssidKey, String());
  profile.passphrase = preferences.getString(passphraseKey, String());
  if (profile.ssid.isEmpty() || profile.passphrase.length() > 63) {
    return false;
  }
  profile.securityType = preferences.getUChar(securityKey, 0);
  profile.lastSuccessEpoch = preferences.getUInt(seenKey, 0);
  out = profile;
  return true;
}

bool writeProfile(Preferences& preferences, size_t index, bool backup, const NetworkProfile& profile) {
  char ssidKey[7];
  char passphraseKey[7];
  char securityKey[7];
  char seenKey[7];
  profileKeys(index, backup, ssidKey, passphraseKey, securityKey, seenKey);

  const bool ssidStored = preferences.putString(ssidKey, profile.ssid) > 0;
  const bool passphraseStored = profile.passphrase.isEmpty()
      ? preferences.putString(passphraseKey, profile.passphrase) == 0 &&
            preferences.getType(passphraseKey) == PT_STR &&
            preferences.getString(passphraseKey, String()) == profile.passphrase
      : preferences.putString(passphraseKey, profile.passphrase) > 0;
  const bool securityStored = preferences.putUChar(securityKey, profile.securityType) == 1;
  const bool seenStored = preferences.putUInt(seenKey, profile.lastSuccessEpoch) == sizeof(uint32_t);
  return ssidStored && passphraseStored && securityStored && seenStored;
}

bool removeProfile(Preferences& preferences, size_t index, bool backup) {
  char ssidKey[7];
  char passphraseKey[7];
  char securityKey[7];
  char seenKey[7];
  profileKeys(index, backup, ssidKey, passphraseKey, securityKey, seenKey);

  const bool ssidRemoved = !preferences.isKey(ssidKey) || preferences.remove(ssidKey);
  const bool passphraseRemoved = !preferences.isKey(passphraseKey) || preferences.remove(passphraseKey);
  const bool securityRemoved = !preferences.isKey(securityKey) || preferences.remove(securityKey);
  const bool seenRemoved = !preferences.isKey(seenKey) || preferences.remove(seenKey);
  return ssidRemoved && passphraseRemoved && securityRemoved && seenRemoved;
}

bool readSnapshot(Preferences& preferences, bool backup, StoreSnapshot& out) {
  const char* countKey = backup ? kBackupCountKey : kCountKey;
  const char* activeKey = backup ? kBackupActiveKey : kActiveKey;
  if (preferences.getType(countKey) != PT_U8 || preferences.getType(activeKey) != PT_I8) {
    return false;
  }

  const size_t count = preferences.getUChar(countKey, 0);
  const int active = preferences.getChar(activeKey, -1);
  if (count > NetworkProfileStore::kMaxProfiles || active < -1 ||
      (active >= 0 && static_cast<size_t>(active) >= count)) {
    return false;
  }

  StoreSnapshot snapshot;
  snapshot.count = count;
  snapshot.active = active;
  for (size_t index = 0; index < count; ++index) {
    if (!readProfile(preferences, index, backup, snapshot.profiles[index])) {
      return false;
    }
  }
  out = snapshot;
  return true;
}

bool snapshotsMatch(const StoreSnapshot& left, const StoreSnapshot& right) {
  if (left.count != right.count || left.active != right.active) {
    return false;
  }
  for (size_t index = 0; index < left.count; ++index) {
    const NetworkProfile& lhs = left.profiles[index];
    const NetworkProfile& rhs = right.profiles[index];
    if (lhs.ssid != rhs.ssid || lhs.passphrase != rhs.passphrase || lhs.securityType != rhs.securityType ||
        lhs.lastSuccessEpoch != rhs.lastSuccessEpoch) {
      return false;
    }
  }
  return true;
}

bool writeBackup(Preferences& preferences, const StoreSnapshot& snapshot) {
  for (size_t index = 0; index < snapshot.count; ++index) {
    if (!writeProfile(preferences, index, true, snapshot.profiles[index])) {
      return false;
    }
  }
  if (preferences.putUChar(kBackupCountKey, static_cast<uint8_t>(snapshot.count)) != 1 ||
      preferences.putChar(kBackupActiveKey, static_cast<int8_t>(snapshot.active)) != 1) {
    return false;
  }

  StoreSnapshot verified;
  return readSnapshot(preferences, true, verified) && snapshotsMatch(snapshot, verified);
}

bool writeCanonical(Preferences& preferences, const StoreSnapshot& snapshot) {
  for (size_t index = 0; index < snapshot.count; ++index) {
    if (!writeProfile(preferences, index, false, snapshot.profiles[index])) {
      return false;
    }
  }
  for (size_t index = snapshot.count; index < NetworkProfileStore::kMaxProfiles; ++index) {
    if (!removeProfile(preferences, index, false)) {
      return false;
    }
  }
  return preferences.putUChar(kCountKey, static_cast<uint8_t>(snapshot.count)) == 1 &&
         preferences.putChar(kActiveKey, static_cast<int8_t>(snapshot.active)) == 1;
}

void clearBackup(Preferences& preferences) {
  for (size_t index = 0; index < NetworkProfileStore::kMaxProfiles; ++index) {
    removeProfile(preferences, index, true);
  }
  if (preferences.isKey(kBackupCountKey)) {
    preferences.remove(kBackupCountKey);
  }
  if (preferences.isKey(kBackupActiveKey)) {
    preferences.remove(kBackupActiveKey);
  }
}

bool transactionPending(Preferences& preferences) {
  return preferences.getType(kTransactionKey) == PT_U8 && preferences.getUChar(kTransactionKey, 0) == 1;
}

bool recoverTransaction(Preferences& preferences) {
  StoreSnapshot backup;
  if (!readSnapshot(preferences, true, backup)) {
    return false;
  }
  if (!writeCanonical(preferences, backup)) {
    return false;
  }
  StoreSnapshot verified;
  if (!readSnapshot(preferences, false, verified) || !snapshotsMatch(backup, verified)) {
    return false;
  }
  if (!preferences.remove(kTransactionKey)) {
    return false;
  }
  clearBackup(preferences);
  return true;
}

bool resetCanonical(Preferences& preferences) {
  const StoreSnapshot empty;
  return writeCanonical(preferences, empty);
}

bool readVisibleSnapshot(Preferences& preferences, StoreSnapshot& out) {
  return transactionPending(preferences) ? readSnapshot(preferences, true, out) : readSnapshot(preferences, false, out);
}

bool prepareStore(Preferences& preferences, StoreSnapshot& out) {
  if (transactionPending(preferences)) {
    if (!recoverTransaction(preferences)) {
      return false;
    }
  }
  if (readSnapshot(preferences, false, out)) {
    return true;
  }
  if (!resetCanonical(preferences)) {
    return false;
  }
  out = StoreSnapshot();
  return true;
}

bool commitSnapshot(Preferences& preferences, const StoreSnapshot& previous, const StoreSnapshot& next) {
  if (!writeBackup(preferences, previous)) {
    return false;
  }
  if (preferences.putUChar(kTransactionKey, 1) != 1) {
    return false;
  }
  if (!writeCanonical(preferences, next)) {
    return false;
  }
  StoreSnapshot verified;
  if (!readSnapshot(preferences, false, verified) || !snapshotsMatch(next, verified)) {
    return false;
  }
  if (!preferences.remove(kTransactionKey)) {
    return false;
  }
  clearBackup(preferences);
  return true;
}

bool makeUpsertSnapshot(const StoreSnapshot& previous, const NetworkProfile& profile,
                        bool activate, StoreSnapshot& next, size_t& target) {
  next = previous;
  target = previous.count;
  for (size_t index = 0; index < previous.count; ++index) {
    if (previous.profiles[index].ssid == profile.ssid) {
      target = index;
      break;
    }
  }

  if (target == previous.count && previous.count == NetworkProfileStore::kMaxProfiles) {
    uint32_t oldestEpoch = 0;
    bool foundReplacement = false;
    for (size_t index = 0; index < previous.count; ++index) {
      if (static_cast<int>(index) == previous.active) {
        continue;
      }
      if (!foundReplacement || previous.profiles[index].lastSuccessEpoch < oldestEpoch) {
        target = index;
        oldestEpoch = previous.profiles[index].lastSuccessEpoch;
        foundReplacement = true;
      }
    }
    if (!foundReplacement) {
      return false;
    }
  } else if (target == previous.count) {
    ++next.count;
  }

  next.profiles[target] = profile;
  if (activate) {
    next.active = static_cast<int>(target);
  }
  return true;
}

}  // namespace

bool NetworkProfileStore::begin() {
  Preferences preferences;
  if (!preferences.begin(kNamespace, false)) {
    return false;
  }

  bool ready = false;
  if (transactionPending(preferences)) {
    ready = recoverTransaction(preferences);
  } else {
    StoreSnapshot snapshot;
    ready = readSnapshot(preferences, false, snapshot) || resetCanonical(preferences);
    if (ready) {
      clearBackup(preferences);
    }
  }
  preferences.end();
  return ready;
}

size_t NetworkProfileStore::count() const {
  Preferences preferences;
  if (!preferences.begin(kNamespace, true)) {
    return 0;
  }

  StoreSnapshot snapshot;
  const size_t count = readVisibleSnapshot(preferences, snapshot) ? snapshot.count : 0;
  preferences.end();
  return count;
}

int NetworkProfileStore::activeIndex() const {
  Preferences preferences;
  if (!preferences.begin(kNamespace, true)) {
    return -1;
  }

  StoreSnapshot snapshot;
  const int active = readVisibleSnapshot(preferences, snapshot) ? snapshot.active : -1;
  preferences.end();
  return active;
}

bool NetworkProfileStore::load(size_t index, NetworkProfile& out) const {
  Preferences preferences;
  if (!preferences.begin(kNamespace, true)) {
    return false;
  }

  StoreSnapshot snapshot;
  const bool loaded = readVisibleSnapshot(preferences, snapshot) && index < snapshot.count;
  if (loaded) {
    out = snapshot.profiles[index];
  }
  preferences.end();
  return loaded;
}

bool NetworkProfileStore::activate(size_t index) {
  Preferences preferences;
  if (!preferences.begin(kNamespace, false)) {
    return false;
  }

  StoreSnapshot previous;
  if (!prepareStore(preferences, previous) || index >= previous.count) {
    preferences.end();
    return false;
  }

  StoreSnapshot next = previous;
  next.active = static_cast<int>(index);
  const bool activated = commitSnapshot(preferences, previous, next);
  preferences.end();
  return activated;
}

bool NetworkProfileStore::upsert(const NetworkProfile& profile, size_t& storedIndex) {
  if (profile.ssid.isEmpty() || profile.passphrase.length() > 63) {
    return false;
  }

  Preferences preferences;
  if (!preferences.begin(kNamespace, false)) {
    return false;
  }

  StoreSnapshot previous;
  if (!prepareStore(preferences, previous)) {
    preferences.end();
    return false;
  }

  StoreSnapshot next;
  size_t target = 0;
  if (!makeUpsertSnapshot(previous, profile, false, next, target)) {
    preferences.end();
    return false;
  }
  const bool stored = commitSnapshot(preferences, previous, next);
  preferences.end();
  if (stored) {
    storedIndex = target;
  }
  return stored;
}

bool NetworkProfileStore::upsertAndActivate(const NetworkProfile& profile,
                                            size_t& storedIndex) {
  if (profile.ssid.isEmpty() || profile.passphrase.length() > 63) {
    return false;
  }

  Preferences preferences;
  if (!preferences.begin(kNamespace, false)) {
    return false;
  }

  StoreSnapshot previous;
  if (!prepareStore(preferences, previous)) {
    preferences.end();
    return false;
  }

  StoreSnapshot next;
  size_t target = 0;
  if (!makeUpsertSnapshot(previous, profile, true, next, target)) {
    preferences.end();
    return false;
  }
  const bool stored = commitSnapshot(preferences, previous, next);
  preferences.end();
  if (stored) {
    storedIndex = target;
  }
  return stored;
}

bool NetworkProfileStore::erase(size_t index) {
  Preferences preferences;
  if (!preferences.begin(kNamespace, false)) {
    return false;
  }

  StoreSnapshot previous;
  if (!prepareStore(preferences, previous) || index >= previous.count) {
    preferences.end();
    return false;
  }

  StoreSnapshot next = previous;
  for (size_t source = index + 1; source < previous.count; ++source) {
    next.profiles[source - 1] = previous.profiles[source];
  }
  --next.count;
  if (previous.active == static_cast<int>(index)) {
    next.active = -1;
  } else if (previous.active > static_cast<int>(index)) {
    next.active = previous.active - 1;
  }

  const bool erased = commitSnapshot(preferences, previous, next);
  preferences.end();
  return erased;
}

bool NetworkProfileStore::clearUpstreamProfiles() {
  Preferences preferences;
  if (!preferences.begin(kNamespace, false)) {
    return false;
  }

  const bool cleared = preferences.clear();
  preferences.end();
  return cleared;
}
