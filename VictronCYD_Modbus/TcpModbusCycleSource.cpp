#include "TcpModbusCycleSource.h"

#ifndef CYD_SIMULATION

#include <Arduino.h>
#include <cstdio>

namespace {
int signedRegister(uint16_t value) {
  return value > 32767 ? static_cast<int>(value) - 65536 : static_cast<int>(value);
}

const char* batteryStateText(int code) {
  switch (code) {
    case 0:
      return "idle";
    case 1:
      return "charging";
    case 2:
      return "discharging";
    default:
      return "-";
  }
}

void setVebusStateText(int state, char output[20]) {
  const char* text = nullptr;
  switch (state) {
    case 0: text = "Off"; break;
    case 1: text = "Low power"; break;
    case 2: text = "Fault"; break;
    case 3: text = "Bulk"; break;
    case 4: text = "Absorption"; break;
    case 5: text = "Float"; break;
    case 6: text = "Storage"; break;
    case 7: text = "Equalize"; break;
    case 8: text = "Passthru"; break;
    case 9: text = "Inverting"; break;
    case 10: text = "Power assist"; break;
    case 11: text = "Power supply"; break;
    case 244: text = "Sustain"; break;
    case 252: text = "Ext. control"; break;
    default: break;
  }
  if (text == nullptr) {
    std::snprintf(output, 20, "%d", state);
  } else {
    copyModbusText(output, 20, text);
  }
}
}  // namespace

TcpModbusCycleSource::TcpModbusCycleSource(const char* gxAddress)
    : gxAddress_(gxAddress) {}

bool TcpModbusCycleSource::readRegisters(uint8_t unit, uint16_t address,
                                         uint16_t count, uint16_t* output) {
  if (!client_.connected()) {
    client_.stop();
    if (!client_.connect(gxAddress_, 502, 3000)) {
      return false;
    }
  }
  while (client_.available()) {
    client_.read();
  }

  ++transactionId_;
  uint8_t request[12] = {
      static_cast<uint8_t>(transactionId_ >> 8),
      static_cast<uint8_t>(transactionId_), 0, 0, 0, 6, unit, 0x03,
      static_cast<uint8_t>(address >> 8), static_cast<uint8_t>(address),
      static_cast<uint8_t>(count >> 8), static_cast<uint8_t>(count)};
  if (client_.write(request, sizeof(request)) != sizeof(request)) {
    client_.stop();
    return false;
  }

  uint32_t startedAtMs = millis();
  while (client_.available() < 9) {
    if (!client_.connected() || millis() - startedAtMs > 800) {
      client_.stop();
      return false;
    }
    delay(1);
  }
  uint8_t header[9];
  if (client_.readBytes(header, sizeof(header)) != sizeof(header)) {
    client_.stop();
    return false;
  }
  const uint16_t responseTransactionId =
      static_cast<uint16_t>((header[0] << 8) | header[1]);
  if (responseTransactionId != transactionId_ || header[7] != 0x03 ||
      header[8] != count * 2 || header[8] > 64) {
    client_.stop();
    return false;
  }

  const uint8_t byteCount = header[8];
  uint8_t data[64];
  startedAtMs = millis();
  while (client_.available() < byteCount) {
    if (!client_.connected() || millis() - startedAtMs > 800) {
      client_.stop();
      return false;
    }
    delay(1);
  }
  if (client_.readBytes(data, byteCount) != byteCount) {
    client_.stop();
    return false;
  }
  for (uint16_t index = 0; index < count; ++index) {
    output[index] = static_cast<uint16_t>((data[index * 2] << 8) |
                                          data[index * 2 + 1]);
  }
  return true;
}

bool TcpModbusCycleSource::fetch(ModbusReadCycle& cycle) {
  cycle = {};
  copyModbusText(cycle.battState, sizeof(cycle.battState), "-");
  copyModbusText(cycle.sysState, sizeof(cycle.sysState), "-");

  uint16_t registers[8];
  bool acValuesReady = false;
  bool batteryValuesReady = false;
  if (readRegisters(100, 817, 4, registers)) {
    cycle.acW = signedRegister(registers[0]);
    cycle.gridW = signedRegister(registers[3]);
    acValuesReady = true;
  }
  if (readRegisters(100, 840, 5, registers)) {
    cycle.battV = registers[0] / 10.0;
    cycle.battA = signedRegister(registers[1]) / 10.0;
    cycle.battW = signedRegister(registers[2]);
    cycle.soc = registers[3] > 100 ? registers[3] / 10.0 : registers[3];
    copyModbusText(cycle.battState, sizeof(cycle.battState),
                   batteryStateText(registers[4]));
    batteryValuesReady = true;
  }
  cycle.requiredValid = acValuesReady && batteryValuesReady;

  uint16_t value[1];
  if (readRegisters(100, 850, 1, value)) {
    cycle.pvReady = true;
    cycle.pvW = value[0];
  }
  if (readRegisters(100, 860, 1, value)) {
    cycle.dcReady = true;
    cycle.dcW = signedRegister(value[0]);
  }
  if (readRegisters(228, 31, 1, value)) {
    cycle.systemStateReady = true;
    setVebusStateText(value[0], cycle.sysState);
  }
  if (readRegisters(225, 262, 1, value)) {
    cycle.batteryTemperatureReady = true;
    cycle.battT = signedRegister(value[0]) / 10.0;
  }
  cycle.receivedAtMs = millis();
  return cycle.requiredValid;
}

#endif
