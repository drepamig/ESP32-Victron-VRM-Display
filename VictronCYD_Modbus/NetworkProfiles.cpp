#include "NetworkProfiles.h"

#include <Preferences.h>

namespace {

constexpr char kNamespace[] = "wanprofiles";
constexpr char kCountKey[] = "count";
constexpr char kActiveKey[] = "active";

void profileKey(char* key, const char* prefix, size_t index) {
  snprintf(key, 6, "%s%u", prefix, static_cast<unsigned int>(index));
}

size_t storedCount(Preferences& preferences) {
  const uint8_t count = preferences.getUChar(kCountKey, 0);
  return count > NetworkProfileStore::kMaxProfiles ? NetworkProfileStore::kMaxProfiles : count;
}

int storedActiveIndex(Preferences& preferences, size_t count) {
  const int active = preferences.getChar(kActiveKey, -1);
  return active >= 0 && static_cast<size_t>(active) < count ? active : -1;
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
            preferences.isKey(passphraseKey) &&
            preferences.getString(passphraseKey, String()) == profile.passphrase
      : preferences.putString(passphraseKey, profile.passphrase) > 0;
  const bool securityStored = preferences.putUChar(securityKey, profile.securityType) == 1;
  const bool seenStored = preferences.putUInt(seenKey, profile.lastSuccessEpoch) == sizeof(uint32_t);
  return ssidStored && passphraseStored && securityStored && seenStored;
}

bool removeProfile(Preferences& preferences, size_t index) {
  char ssidKey[6];
  char passphraseKey[6];
  char securityKey[6];
  char seenKey[6];
  profileKey(ssidKey, "ssid", index);
  profileKey(passphraseKey, "pass", index);
  profileKey(securityKey, "sec", index);
  profileKey(seenKey, "seen", index);

  const bool ssidRemoved = !preferences.isKey(ssidKey) || preferences.remove(ssidKey);
  const bool passphraseRemoved = !preferences.isKey(passphraseKey) || preferences.remove(passphraseKey);
  const bool securityRemoved = !preferences.isKey(securityKey) || preferences.remove(securityKey);
  const bool seenRemoved = !preferences.isKey(seenKey) || preferences.remove(seenKey);
  return ssidRemoved && passphraseRemoved && securityRemoved && seenRemoved;
}

}  // namespace

bool NetworkProfileStore::begin() {
  Preferences preferences;
  if (!preferences.begin(kNamespace, false)) {
    return false;
  }

  const bool countReady = preferences.isKey(kCountKey) || preferences.putUChar(kCountKey, 0) == 1;
  const bool activeReady = preferences.isKey(kActiveKey) || preferences.putChar(kActiveKey, -1) == 1;
  preferences.end();
  return countReady && activeReady;
}

size_t NetworkProfileStore::count() const {
  Preferences preferences;
  if (!preferences.begin(kNamespace, true)) {
    return 0;
  }

  const size_t count = storedCount(preferences);
  preferences.end();
  return count;
}

int NetworkProfileStore::activeIndex() const {
  Preferences preferences;
  if (!preferences.begin(kNamespace, true)) {
    return -1;
  }

  const size_t count = storedCount(preferences);
  const int active = storedActiveIndex(preferences, count);
  preferences.end();
  return active;
}

bool NetworkProfileStore::load(size_t index, NetworkProfile& out) const {
  Preferences preferences;
  if (!preferences.begin(kNamespace, true)) {
    return false;
  }

  const size_t count = storedCount(preferences);
  if (index >= count) {
    preferences.end();
    return false;
  }

  const bool loaded = readProfile(preferences, index, out);
  preferences.end();
  return loaded;
}

bool NetworkProfileStore::activate(size_t index) {
  Preferences preferences;
  if (!preferences.begin(kNamespace, false)) {
    return false;
  }

  const size_t count = storedCount(preferences);
  if (index >= count) {
    preferences.end();
    return false;
  }

  const bool activated = preferences.putChar(kActiveKey, static_cast<int8_t>(index)) == 1;
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

  const size_t count = storedCount(preferences);
  for (size_t index = 0; index < count; ++index) {
    char ssidKey[6];
    profileKey(ssidKey, "ssid", index);
    if (preferences.getString(ssidKey, String()) == profile.ssid) {
      const bool updated = writeProfile(preferences, index, profile);
      preferences.end();
      if (updated) {
        storedIndex = index;
      }
      return updated;
    }
  }

  if (count < kMaxProfiles) {
    const bool stored = writeProfile(preferences, count, profile) &&
                        preferences.putUChar(kCountKey, static_cast<uint8_t>(count + 1)) == 1;
    preferences.end();
    if (stored) {
      storedIndex = count;
    }
    return stored;
  }

  const int active = storedActiveIndex(preferences, count);
  size_t oldestIndex = 0;
  uint32_t oldestEpoch = 0;
  bool foundReplacement = false;
  for (size_t index = 0; index < count; ++index) {
    if (static_cast<int>(index) == active) {
      continue;
    }

    char seenKey[6];
    profileKey(seenKey, "seen", index);
    const uint32_t seen = preferences.getUInt(seenKey, 0);
    if (!foundReplacement || seen < oldestEpoch) {
      oldestIndex = index;
      oldestEpoch = seen;
      foundReplacement = true;
    }
  }

  const bool stored = foundReplacement && writeProfile(preferences, oldestIndex, profile);
  preferences.end();
  if (stored) {
    storedIndex = oldestIndex;
  }
  return stored;
}

bool NetworkProfileStore::erase(size_t index) {
  Preferences preferences;
  if (!preferences.begin(kNamespace, false)) {
    return false;
  }

  const size_t count = storedCount(preferences);
  if (index >= count) {
    preferences.end();
    return false;
  }

  for (size_t source = index + 1; source < count; ++source) {
    NetworkProfile profile;
    if (!readProfile(preferences, source, profile) || !writeProfile(preferences, source - 1, profile)) {
      preferences.end();
      return false;
    }
  }

  if (!removeProfile(preferences, count - 1) ||
      preferences.putUChar(kCountKey, static_cast<uint8_t>(count - 1)) != 1) {
    preferences.end();
    return false;
  }

  const int active = storedActiveIndex(preferences, count);
  const int adjustedActive = active == static_cast<int>(index)
      ? -1
      : active > static_cast<int>(index) ? active - 1 : active;
  const bool activeStored = preferences.putChar(kActiveKey, static_cast<int8_t>(adjustedActive)) == 1;
  preferences.end();
  return activeStored;
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
