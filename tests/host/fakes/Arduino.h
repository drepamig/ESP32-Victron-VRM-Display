#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <string>

class String {
 public:
  String() = default;
  String(const char* value) : value_(value == nullptr ? "" : value) {}
  String(const std::string& value) : value_(value) {}

  String& operator=(const char* value) {
    value_ = value == nullptr ? "" : value;
    return *this;
  }

  bool isEmpty() const { return value_.empty(); }
  size_t length() const { return value_.length(); }
  const char* c_str() const { return value_.c_str(); }

  String& operator+=(const char* value) {
    if (value != nullptr) value_ += value;
    return *this;
  }

  String& operator+=(const String& value) {
    value_ += value.value_;
    return *this;
  }

  String& operator+=(char value) {
    value_ += value;
    return *this;
  }

  bool operator==(const String& other) const { return value_ == other.value_; }
  bool operator!=(const String& other) const { return !(*this == other); }

 private:
  std::string value_;
};

class FakeSerial {
 public:
  template <typename T>
  void print(const T&) {}

  template <typename T>
  void println(const T&) {}

  void println(const char* value) {
    output_ += value == nullptr ? "" : value;
    output_ += '\n';
  }

  int available() const { return static_cast<int>(input_.size()); }
  int read() {
    if (input_.empty()) return -1;
    const unsigned char value = static_cast<unsigned char>(input_.front());
    input_.pop_front();
    return value;
  }

  void feed(const std::string& value) {
    for (char character : value) input_.push_back(character);
  }

  void clear() {
    input_.clear();
    output_.clear();
  }

  const std::string& output() const { return output_; }

 private:
  std::deque<char> input_;
  std::string output_;
};

inline FakeSerial Serial;

inline uint32_t fakeMillis = 0;
inline uint32_t millis() { return fakeMillis; }
