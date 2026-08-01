#pragma once

#include <Arduino.h>

class DeviceAddressService
{
public:
  DeviceAddressService(HardwareSerial &serial, uint8_t transceiverEnablePin);

  bool setAddress(uint8_t address);

private:
  HardwareSerial &_serial;
  uint8_t _transceiverEnablePin;
};
