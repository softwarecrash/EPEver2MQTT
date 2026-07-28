#include "DeviceClockService.h"

#include "LegacyRegisters.h"
#include "NcG3Registers.h"

#ifdef EPEVER_SIMULATION
#include "../simulation/SimulationDataSource.h"
#endif

namespace
{
uint8_t daysInMonth(uint16_t year, uint8_t month)
{
  static constexpr uint8_t Days[] = {
      31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month == 2)
  {
    const bool leapYear =
        (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    return leapYear ? 29 : 28;
  }
  return Days[month - 1];
}
}

DeviceClockService::DeviceClockService(ModbusMaster &modbus,
                                       PollingControl &pollingControl,
                                       EpeverController &controller)
    : _modbus(modbus),
      _pollingControl(pollingControl),
      _controller(controller)
{
}

#ifdef EPEVER_SIMULATION
DeviceClockService::DeviceClockService(
    ModbusMaster &modbus, PollingControl &pollingControl,
    EpeverController &controller,
    SimulationDataSource &simulation)
    : _modbus(modbus),
      _pollingControl(pollingControl),
      _controller(controller),
      _simulation(&simulation)
{
}
#endif

bool DeviceClockService::setAll(const char *dateTime, uint8_t deviceQuantity)
{
  uint8_t parts[6];
  if (!parseDateTime(dateTime, parts))
    return false;

#ifdef EPEVER_SIMULATION
  if (_simulation != nullptr)
    return _simulation->setClock(dateTime);
#endif

  ScopedPollingPause pause(_pollingControl);
  bool success = true;

  for (uint8_t device = 1; device <= deviceQuantity; device++)
  {
    _modbus.setSlaveId(device);
    const EpeverProfile profile = _controller.detectProfile(device, false);
    if (profile == EpeverProfile::Unknown)
    {
      success = false;
      continue;
    }

    _modbus.setTransmitBuffer(0, (static_cast<uint16_t>(parts[4]) << 8) |
                                     parts[5]);
    _modbus.setTransmitBuffer(1, (static_cast<uint16_t>(parts[2]) << 8) |
                                     parts[3]);
    _modbus.setTransmitBuffer(2, (static_cast<uint16_t>(parts[0]) << 8) |
                                     parts[1]);
    const uint16_t clockRegister =
        isNcG3Profile(profile) ? NcG3Registers::RtcClock
                               : LegacyRegisters::RtcClock;
    if (_modbus.writeMultipleRegisters(clockRegister, 3) !=
        _modbus.ku8MBSuccess)
      success = false;
    delay(50);
  }

  return success;
}

bool DeviceClockService::parseDateTime(const char *dateTime, uint8_t *parts)
{
  if (dateTime == nullptr || strlen(dateTime) != 12)
    return false;

  for (uint8_t index = 0; index < 12; index++)
    if (!isDigit(dateTime[index]))
      return false;

  for (uint8_t index = 0; index < 6; index++)
    parts[index] = (dateTime[index * 2] - '0') * 10 +
                   (dateTime[index * 2 + 1] - '0');

  if (parts[1] < 1 || parts[1] > 12 || parts[3] > 23 ||
      parts[4] > 59 || parts[5] > 59)
    return false;

  return parts[2] >= 1 &&
         parts[2] <= daysInMonth(2000 + parts[0], parts[1]);
}
