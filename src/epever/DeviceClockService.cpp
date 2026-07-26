#include "DeviceClockService.h"

#ifdef EPEVER_SIMULATION
#include "../simulation/SimulationDataSource.h"
#endif

namespace
{
constexpr uint16_t LegacyRtcClockRegister = 0x9013;
constexpr uint16_t NcG3RtcClockRegister = 0x9019;
}

DeviceClockService::DeviceClockService(ModbusMaster &modbus,
                                       bool &workerCanRun,
                                       DetectProfileFn detectProfile)
    : _modbus(modbus),
      _workerCanRun(workerCanRun),
      _detectProfile(detectProfile)
{
}

#ifdef EPEVER_SIMULATION
DeviceClockService::DeviceClockService(
    ModbusMaster &modbus, bool &workerCanRun, DetectProfileFn detectProfile,
    SimulationDataSource &simulation)
    : _modbus(modbus),
      _workerCanRun(workerCanRun),
      _detectProfile(detectProfile),
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

  const bool previousWorkerState = _workerCanRun;
  _workerCanRun = false;
  bool success = true;

  for (uint8_t device = 1; device <= deviceQuantity; device++)
  {
    _modbus.setSlaveId(device);
    const EpeverProfile profile = _detectProfile(device, false);
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
        isNcG3Profile(profile) ? NcG3RtcClockRegister
                               : LegacyRtcClockRegister;
    if (_modbus.writeMultipleRegisters(clockRegister, 3) !=
        _modbus.ku8MBSuccess)
      success = false;
    delay(50);
  }

  _workerCanRun = previousWorkerState;
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

  return parts[1] >= 1 && parts[1] <= 12 &&
         parts[2] >= 1 && parts[2] <= 31 &&
         parts[3] <= 23 && parts[4] <= 59 && parts[5] <= 59;
}
