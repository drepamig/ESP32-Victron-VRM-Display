#pragma once

#ifndef CYD_SIMULATION

#include <WiFi.h>

#include "ModbusCycleSource.h"

class TcpModbusCycleSource final : public ModbusCycleSource {
 public:
  explicit TcpModbusCycleSource(const char* gxAddress);
  bool fetch(ModbusReadCycle& cycle) override;

 private:
  bool readRegisters(uint8_t unit, uint16_t address, uint16_t count,
                     uint16_t* output);

  const char* gxAddress_;
  WiFiClient client_;
  uint16_t transactionId_ = 0;
};

#endif
