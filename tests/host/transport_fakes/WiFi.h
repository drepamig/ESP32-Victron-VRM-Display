#pragma once
#include <Arduino.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
inline void delay(uint32_t duration) { fakeMillis += duration; }
class WiFiClient {
 public:
  bool connected() const { return connected_; }
  void stop() { connected_=false; response_.clear(); }
  bool connect(const char* target,uint16_t port,int) {
    destinations.emplace_back(target); ports.push_back(port); connected_=connectSucceeds; return connected_;
  }
  int available() const { return static_cast<int>(response_.size()); }
  int read() { if(response_.empty())return -1; int n=response_[0];response_.erase(response_.begin());return n; }
  size_t readBytes(uint8_t* out,size_t size) {
    if(size>response_.size())return 0;
    std::memcpy(out,response_.data(),size);response_.erase(response_.begin(),response_.begin()+size);return size;
  }
  size_t write(const uint8_t* request,size_t size) {
    if(size!=12||!connected_)return 0;
    const uint8_t count=request[11]*2;
    response_={request[0],request[1],0,0,0,static_cast<uint8_t>(3+count),request[6],3,count};
    response_.resize(9+count,0);return size;
  }
  static inline std::vector<std::string> destinations;
  static inline std::vector<uint16_t> ports;
  static inline bool connectSucceeds=true;
 private:
  bool connected_=false;
  std::vector<uint8_t> response_;
};
