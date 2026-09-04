#pragma once

#ifndef CYD_SIMULATION

#include <WiFi.h>

#include "ModbusCycleSource.h"

class TcpModbusCycleSource final : public ModbusCycleSource {
 public:
  explicit TcpModbusCycleSource(const char* gxAddress = nullptr);
  void setAddress(uint32_t address);
  bool fetch(ModbusReadCycle& cycle) override;

 private:
  bool readRegisters(uint8_t unit, uint16_t address, uint16_t count,
                     uint16_t* output);

  char gxAddress_[16]{};
  WiFiClient client_;
  uint16_t transactionId_ = 0;
};

#endif
