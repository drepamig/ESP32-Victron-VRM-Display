#include "NetworkProfiles.h"

#include <Preferences.h>

namespace {

constexpr char kNamespace[] = "wanprofiles";
constexpr char kCountKey[] = "count";
constexpr char kActiveKey[] = "active";
constexpr int8_t kRecoveryActive = -2;

struct StoreSnapshot {
  size_t count = 0;
  int active = -1;
  NetworkProfile profiles[NetworkProfileStore::kMaxProfiles];
};

void profileKey(char* key, const char* prefix, size_t index) {
  const size_t prefixLength = prefix[3] == '\0' ? 3 : 4;
  for (size_t character = 0; character < prefixLength; ++character) {
    key[character] = prefix[character];
  }
  key[prefixLength] = static_cast<char>('0' + index);
  key[prefixLength + 1] = '\0';
}

bool readProfile(Preferences& preferences, size_t index, NetworkProfile& out) {
  char ssidKey[6];
  char passphraseKey[6];
  char securityKey[6];
  char seenKey[6];
  profileKey(ssidKey, "ssid", index);
  profileKey(passphraseKey, "pass", index);
  profileKey(securityKey, "sec", index);
  profileKey(seenKey, "seen", index);

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

bool writeProfile(Preferences& preferences, size_t index, const NetworkProfile& profile) {
  char ssidKey[6];
  char passphraseKey[6];
  char securityKey[6];
  char seenKey[6];
  profileKey(ssidKey, "ssid", index);
  profileKey(passphraseKey, "pass", index);
  profileKey(securityKey, "sec", index);
  profileKey(seenKey, "seen", index);

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

bool readSnapshot(Preferences& preferences, StoreSnapshot& out) {
  if (preferences.getType(kCountKey) != PT_U8 || preferences.getType(kActiveKey) != PT_I8) {
    return false;
  }

  const size_t count = preferences.getUChar(kCountKey, 0);
  const int active = preferences.getChar(kActiveKey, -1);
  if (count > NetworkProfileStore::kMaxProfiles || active < -1 ||
      (active >= 0 && static_cast<size_t>(active) >= count)) {
    return false;
  }

  StoreSnapshot snapshot;
  snapshot.count = count;
  snapshot.active = active;
  for (size_t index = 0; index < count; ++index) {
    if (!readProfile(preferences, index, snapshot.profiles[index])) {
      return false;
    }
  }
  out = snapshot;
  return true;
}

bool writeSnapshot(Preferences& preferences, const StoreSnapshot& snapshot) {
  if (!preferences.clear()) {
    return false;
  }
  for (size_t index = 0; index < snapshot.count; ++index) {
    if (!writeProfile(preferences, index, snapshot.profiles[index])) {
      return false;
    }
  }
  return preferences.putUChar(kCountKey, static_cast<uint8_t>(snapshot.count)) == 1 &&
         preferences.putChar(kActiveKey, static_cast<int8_t>(snapshot.active)) == 1;
}

bool resetStore(Preferences& preferences) {
  const StoreSnapshot empty;
  return writeSnapshot(preferences, empty);
}

bool commitSnapshot(Preferences& preferences, const StoreSnapshot& previous, const StoreSnapshot& next) {
  if (preferences.putChar(kActiveKey, kRecoveryActive) != 1) {
    return false;
  }
  if (writeSnapshot(preferences, next)) {
    return true;
  }
  if (!writeSnapshot(preferences, previous)) {
    return false;
  }
  return false;
}

bool readOrReset(Preferences& preferences, StoreSnapshot& out) {
  if (readSnapshot(preferences, out)) {
    return true;
  }
  if (!resetStore(preferences)) {
    return false;
  }
  out = StoreSnapshot();
  return true;
}

}  // namespace

bool NetworkProfileStore::begin() {
  Preferences preferences;
  if (!preferences.begin(kNamespace, false)) {
    return false;
  }

  StoreSnapshot snapshot;
  const bool ready = readSnapshot(preferences, snapshot) || resetStore(preferences);
  preferences.end();
  return ready;
}

size_t NetworkProfileStore::count() const {
  Preferences preferences;
  if (!preferences.begin(kNamespace, true)) {
    return 0;
  }

  StoreSnapshot snapshot;
  const size_t count = readSnapshot(preferences, snapshot) ? snapshot.count : 0;
  preferences.end();
  return count;
}

int NetworkProfileStore::activeIndex() const {
  Preferences preferences;
  if (!preferences.begin(kNamespace, true)) {
    return -1;
  }

  StoreSnapshot snapshot;
  const int active = readSnapshot(preferences, snapshot) ? snapshot.active : -1;
  preferences.end();
  return active;
}

bool NetworkProfileStore::load(size_t index, NetworkProfile& out) const {
  Preferences preferences;
  if (!preferences.begin(kNamespace, true)) {
    return false;
  }

  StoreSnapshot snapshot;
  const bool loaded = readSnapshot(preferences, snapshot) && index < snapshot.count;
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
  if (!readOrReset(preferences, previous) || index >= previous.count) {
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
  if (!readOrReset(preferences, previous)) {
    preferences.end();
    return false;
  }

  StoreSnapshot next = previous;
  size_t target = previous.count;
  for (size_t index = 0; index < previous.count; ++index) {
    if (previous.profiles[index].ssid == profile.ssid) {
      target = index;
      break;
    }
  }

  if (target == previous.count && previous.count == kMaxProfiles) {
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
      preferences.end();
      return false;
    }
  } else if (target == previous.count) {
    ++next.count;
  }

  next.profiles[target] = profile;
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
  if (!readOrReset(preferences, previous) || index >= previous.count) {
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
