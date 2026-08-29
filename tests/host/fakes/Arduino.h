#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

class String {
 public:
  String() = default;
  String(const char* value) : value_(value == nullptr ? "" : value) {}
  String(const std::string& value) : value_(value) {}

  bool isEmpty() const { return value_.empty(); }
  size_t length() const { return value_.length(); }
  const char* c_str() const { return value_.c_str(); }

  bool operator==(const String& other) const { return value_ == other.value_; }
  bool operator!=(const String& other) const { return !(*this == other); }

 private:
  std::string value_;
};
