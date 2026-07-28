#include "DeviceAddressService.h"

#include <ModbusMaster.h>

DeviceAddressService::DeviceAddressService(
    HardwareSerial &serial, uint8_t transceiverEnablePin)
    : _serial(serial),
      _transceiverEnablePin(transceiverEnablePin)
{
}

bool DeviceAddressService::setAddress(uint8_t address)
{
  if (address == 0 || address > 247)
    return false;

#ifdef EPEVER_SIMULATION
  return true;
#else
  // Address changes use EPEver's vendor-specific broadcast command rather
  // than a normal Modbus register write.
  uint8_t frame[8] = {0xF8, 0x45, 0x00, 0x01, 0x01, address, 0, 0};
  uint16_t crc = 0xFFFF;
  for (uint8_t index = 0; index < 6; index++)
    crc = crc16_update(crc, frame[index]);
  frame[6] = lowByte(crc);
  frame[7] = highByte(crc);

  digitalWrite(_transceiverEnablePin, HIGH);
  delay(50);
  _serial.write(frame, sizeof(frame));
  _serial.flush();
  digitalWrite(_transceiverEnablePin, LOW);

  uint8_t response[4] = {};
  const size_t received = _serial.readBytes(response, sizeof(response));
  return received == sizeof(response) && response[2] == address;
#endif
}
