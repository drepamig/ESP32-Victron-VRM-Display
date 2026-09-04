#pragma once

#include <map>
#include <string>
#include <variant>

#include "Arduino.h"

typedef enum {
  PT_I8,
  PT_U8,
  PT_I16,
  PT_U16,
  PT_I32,
  PT_U32,
  PT_I64,
  PT_U64,
  PT_STR,
  PT_BLOB,
  PT_INVALID
} PreferenceType;

class Preferences {
 public:
  bool begin(const char* name, bool readOnly = false, const char* = nullptr) {
    if (readOnly && ++readOpenCount_ == failOnReadOpen_) return false;
    namespace_ = name == nullptr ? "" : name;
    readOnly_ = readOnly;
    started_ = true;
    return true;
  }

  void end() { started_ = false; }

  bool clear() {
    if (!canWrite()) {
      return false;
    }
    storage_.erase(namespace_);
    return true;
  }

  bool remove(const char* key) {
    if (!canWrite()) {
      return false;
    }
    return storage_[namespace_].erase(key) != 0;
  }

  size_t putChar(const char* key, int8_t value) { return put(key, value, 1); }
  size_t putUChar(const char* key, uint8_t value) { return put(key, value, 1); }
  size_t putInt(const char* key, int32_t value) { return put(key, value, sizeof(value)); }
  size_t putUInt(const char* key, uint32_t value) { return put(key, value, sizeof(value)); }
  size_t putBool(const char* key, bool value) {
    return put(key, static_cast<uint8_t>(value ? 1 : 0), sizeof(uint8_t));
  }

  size_t putString(const char* key, const String& value) {
    if (!canWrite()) {
      return 0;
    }
    storage_[namespace_][key] = value;
    return value.length();
  }

  size_t putString(const char* key, const char* value) { return putString(key, String(value)); }

  bool isKey(const char* key) { return entry(key) != nullptr; }

  PreferenceType getType(const char* key) {
    const Entry* value = entry(key);
    if (value == nullptr) {
      return PT_INVALID;
    }
    if (std::holds_alternative<int8_t>(*value)) {
      return PT_I8;
    }
    if (std::holds_alternative<uint8_t>(*value)) {
      return PT_U8;
    }
    if (std::holds_alternative<int32_t>(*value)) {
      return PT_I32;
    }
    if (std::holds_alternative<uint32_t>(*value)) {
      return PT_U32;
    }
    return PT_STR;
  }

  int8_t getChar(const char* key, int8_t defaultValue = 0) {
    const Entry* value = entry(key);
    return value != nullptr && std::holds_alternative<int8_t>(*value) ? std::get<int8_t>(*value) : defaultValue;
  }

  uint8_t getUChar(const char* key, uint8_t defaultValue = 0) {
    const Entry* value = entry(key);
    return value != nullptr && std::holds_alternative<uint8_t>(*value) ? std::get<uint8_t>(*value) : defaultValue;
  }

  uint32_t getUInt(const char* key, uint32_t defaultValue = 0) {
    const Entry* value = entry(key);
    return value != nullptr && std::holds_alternative<uint32_t>(*value) ? std::get<uint32_t>(*value) : defaultValue;
  }

  int32_t getInt(const char* key, int32_t defaultValue = 0) {
    const Entry* value = entry(key);
    return value != nullptr && std::holds_alternative<int32_t>(*value)
               ? std::get<int32_t>(*value)
               : defaultValue;
  }

  bool getBool(const char* key, bool defaultValue = false) {
    const Entry* value = entry(key);
    return value != nullptr && std::holds_alternative<uint8_t>(*value)
               ? std::get<uint8_t>(*value) != 0
               : defaultValue;
  }

  String getString(const char* key, const String& defaultValue = String()) {
    const Entry* value = entry(key);
    return value != nullptr && std::holds_alternative<String>(*value) ? std::get<String>(*value) : defaultValue;
  }

  static void reset() {
    storage_.clear();
    clearFaults();
    mutationCount_ = 0;
  }

  static void failOnMutation(size_t mutation) {
    failOnMutation_ = mutation;
    failFromMutation_ = 0;
    mutationCount_ = 0;
  }

  static void failFromMutation(size_t mutation) {
    failOnMutation_ = 0;
    failFromMutation_ = mutation;
    mutationCount_ = 0;
  }

  static void clearFaults() {
    failOnMutation_ = 0;
    failFromMutation_ = 0;
    failOnReadOpen_ = 0;
    readOpenCount_ = 0;
  }

  static void failOnReadOpen(size_t open) { failOnReadOpen_ = open; readOpenCount_ = 0; }

  static void putRawUChar(const char* name, const char* key, uint8_t value) { storage_[name][key] = value; }
  static void putRawChar(const char* name, const char* key, int8_t value) { storage_[name][key] = value; }
  static void putRawString(const char* name, const char* key, const char* value) {
    storage_[name][key] = String(value);
  }

 private:
  using Entry = std::variant<int8_t, uint8_t, int32_t, uint32_t, String>;
  using Namespace = std::map<std::string, Entry>;

  bool canWrite() {
    if (!started_ || readOnly_) {
      return false;
    }
    ++mutationCount_;
    if (failFromMutation_ != 0 && mutationCount_ >= failFromMutation_) {
      return false;
    }
    if (failOnMutation_ != 0 && mutationCount_ == failOnMutation_) {
      failOnMutation_ = 0;
      return false;
    }
    return true;
  }

  template <typename T>
  size_t put(const char* key, T value, size_t bytes) {
    if (!canWrite()) {
      return 0;
    }
    storage_[namespace_][key] = value;
    return bytes;
  }

  const Entry* entry(const char* key) const {
    const auto namespaceIt = storage_.find(namespace_);
    if (namespaceIt == storage_.end()) {
      return nullptr;
    }
    const auto entryIt = namespaceIt->second.find(key);
    return entryIt == namespaceIt->second.end() ? nullptr : &entryIt->second;
  }

  inline static std::map<std::string, Namespace> storage_;
  inline static size_t failOnMutation_ = 0;
  inline static size_t failFromMutation_ = 0;
  inline static size_t mutationCount_ = 0;
  inline static size_t failOnReadOpen_ = 0;
  inline static size_t readOpenCount_ = 0;
  std::string namespace_;
  bool readOnly_ = false;
  bool started_ = false;
};
