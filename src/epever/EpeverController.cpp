#include "EpeverController.h"

#include <math.h>
#include "LegacyRegisters.h"
#include "NcG3Registers.h"

#ifdef EPEVER_SIMULATION
#include "../simulation/SimulationDataSource.h"
#endif

EpeverController::EpeverController(
    ModbusMaster &modbus, JsonDocument &liveJson, Settings &settings,
    UnixTime &unixTime, const char *softwareVersion)
    : _modbus(modbus),
      _liveJson(liveJson),
      _settings(settings),
      _unixTime(unixTime),
      _softwareVersion(softwareVersion)
{
}

#ifdef EPEVER_SIMULATION
EpeverController::EpeverController(
    ModbusMaster &modbus, JsonDocument &liveJson, Settings &settings,
    UnixTime &unixTime, const char *softwareVersion,
    SimulationDataSource &simulation)
    : EpeverController(modbus, liveJson, settings, unixTime,
                       softwareVersion)
{
  _simulation = &simulation;
}
#endif

uint32_t EpeverController::wordsToUint32(uint16_t lowWord,
                                         uint16_t highWord)
{
  return static_cast<uint32_t>(lowWord) |
         (static_cast<uint32_t>(highWord) << 16);
}

void EpeverController::synchronizeDeviceClock(uint8_t device)
{
  if (device == 0 || device > MaximumDevices || _unixTime.year < 2000)
    return;

  const uint32_t reportedTime = _unixTime.getUnix();
  // Some controller firmwares return the same RTC value for several reads.
  // Keep the original millis() base until the reported RTC value changes.
  if (_deviceClockBase[device] == 0 ||
      reportedTime != _deviceClockLastReported[device])
  {
    _deviceClockBase[device] = reportedTime;
    _deviceClockLastReported[device] = reportedTime;
    _deviceClockSyncMillis[device] = millis();
  }
}

uint32_t EpeverController::currentDeviceTime(uint8_t device) const
{
  if (device == 0 || device > MaximumDevices ||
      _deviceClockBase[device] == 0)
    return 0;

  return _deviceClockBase[device] +
         (millis() - _deviceClockSyncMillis[device]) / 1000UL;
}

bool EpeverController::readInputBlock(uint16_t address, uint8_t count,
                                      uint16_t *values)
{
#ifdef EPEVER_SIMULATION
  const bool success = _simulation->readInputRegisters(
      _selectedDevice, address, count, values);
  result = success ? _modbus.ku8MBSuccess
                   : _modbus.ku8MBInvalidSlaveID;
  return success;
#else
  _modbus.clearResponseBuffer();
  result = _modbus.readInputRegisters(address, count);
  if (result != _modbus.ku8MBSuccess)
    return false;

  for (uint8_t index = 0; index < count; index++)
    values[index] = _modbus.getResponseBuffer(index);
  return true;
#endif
}

bool EpeverController::readHoldingBlock(uint16_t address, uint8_t count,
                                        uint16_t *values)
{
#ifdef EPEVER_SIMULATION
  const bool success = _simulation->readHoldingRegisters(
      _selectedDevice, address, count, values);
  result = success ? _modbus.ku8MBSuccess
                   : _modbus.ku8MBInvalidSlaveID;
  return success;
#else
  _modbus.clearResponseBuffer();
  result = _modbus.readHoldingRegisters(address, count);
  if (result != _modbus.ku8MBSuccess)
    return false;

  for (uint8_t index = 0; index < count; index++)
    values[index] = _modbus.getResponseBuffer(index);
  return true;
#endif
}

bool EpeverController::readCoil(uint16_t address, bool &value)
{
#ifdef EPEVER_SIMULATION
  uint16_t rawValue = 0;
  const bool success = _simulation->readCoils(
      _selectedDevice, address, 1, &rawValue);
  result = success ? _modbus.ku8MBSuccess
                   : _modbus.ku8MBInvalidSlaveID;
  value = rawValue != 0;
  return success;
#else
  _modbus.clearResponseBuffer();
  result = _modbus.readCoils(address, 1);
  if (result != _modbus.ku8MBSuccess)
    return false;
  value = _modbus.getResponseBuffer(0) != 0;
  return true;
#endif
}

EpeverProfile EpeverController::detectProfile(uint8_t device, bool force)
{
  if (device == 0 || device > MaximumDevices)
    return EpeverProfile::Unknown;
  if (!force && deviceProfiles[device] != EpeverProfile::Unknown)
    return deviceProfiles[device];

  _modbus.setSlaveId(device);
#ifdef EPEVER_SIMULATION
  _selectedDevice = device;
  if (!_simulation->prepareDevice(device))
    return EpeverProfile::Unknown;
#endif
  uint16_t model = 0;
  if (!readInputBlock(NcG3Registers::Model, 1, &model))
  {
    // Legacy controllers do not implement the NC-G3 model register. Probe a
    // known Legacy register so a missing device is not mistaken for Legacy.
    uint16_t legacyClock[LegacyRegisters::RtcClockCount] = {};
    if (!readHoldingBlock(LegacyRegisters::RtcClock,
                          LegacyRegisters::RtcClockCount, legacyClock))
      return EpeverProfile::Unknown;
    deviceProfiles[device] = EpeverProfile::Legacy;
    return deviceProfiles[device];
  }

  if (model <= 11)
    deviceProfiles[device] = EpeverProfile::ItNcG3;
  else if (model <= 23)
    deviceProfiles[device] = EpeverProfile::EtNcG3;
  else
    deviceProfiles[device] = EpeverProfile::Legacy;

  if (model <= 23)
    deviceModelIds[device] = model;
  return deviceProfiles[device];
}

bool EpeverController::read(uint8_t device)
{
#ifdef EPEVER_SIMULATION
  _selectedDevice = device;
  if (!_simulation->prepareDevice(device))
    return false;
#endif

  _errorCode = 0;
  _modbus.setSlaveId(device);
  const EpeverProfile profile = detectProfile(device);
  if (isNcG3Profile(profile))
    return readNcG3(device, profile);
  return readLegacy(device);
}

bool EpeverController::writeLoadState(uint8_t device, bool state)
{
#ifdef EPEVER_SIMULATION
  return _simulation->setLoadState(device, state);
#endif

  const EpeverProfile profile = detectProfile(device);
  if (profile == EpeverProfile::Unknown ||
      profile == EpeverProfile::EtNcG3)
    return false;

  _modbus.setSlaveId(device);
  const uint16_t coil = profile == EpeverProfile::ItNcG3
                            ? NcG3Registers::LoadState
                            : LegacyRegisters::LoadState;
  const uint8_t writeStatus =
      _modbus.writeSingleCoil(coil, state ? 1 : 0);
  delay(50);
  _modbus.clearResponseBuffer();
  const uint8_t readStatus = _modbus.readCoils(coil, 1);
  return writeStatus == _modbus.ku8MBSuccess &&
         readStatus == _modbus.ku8MBSuccess &&
         (_modbus.getResponseBuffer(0) != 0) == state;
}

bool EpeverController::writeChargeCurrentLimit(uint8_t device, float amps)
{
#ifdef EPEVER_SIMULATION
  return _simulation->setChargeCurrentLimit(device, amps);
#endif

  if (!isfinite(amps) || amps <= 0)
    return false;

  const EpeverProfile profile = detectProfile(device);
  if (!isNcG3Profile(profile))
    return false;

  _modbus.setSlaveId(device);
  uint16_t ratedRaw = deviceRatedChargeCurrent[device];
  if (ratedRaw == 0 &&
      !readInputBlock(NcG3Registers::RatedChargeCurrent, 1, &ratedRaw))
    return false;
  deviceRatedChargeCurrent[device] = ratedRaw;

  const float scaled = amps * 100.0f;
  const uint32_t requestedRaw = static_cast<uint32_t>(roundf(scaled));
  if (requestedRaw == 0 || requestedRaw > ratedRaw ||
      requestedRaw > UINT16_MAX ||
      fabsf(scaled - requestedRaw) > 0.01f)
    return false;

  const uint8_t writeStatus = _modbus.writeSingleRegister(
      NcG3Registers::ChargeCurrentLimit,
      static_cast<uint16_t>(requestedRaw));
  delay(100);
  uint16_t readBack = 0;
  const bool verified =
      readHoldingBlock(NcG3Registers::ChargeCurrentLimit, 1, &readBack) &&
      readBack == requestedRaw;
  if (verified)
    ncG3.chargingCurrentLimit = readBack;
  return writeStatus == _modbus.ku8MBSuccess && verified;
}

uint16_t EpeverController::ratedChargeCurrent(uint8_t device) const
{
  return device <= MaximumDevices
             ? deviceRatedChargeCurrent[device]
             : 0;
}

int EpeverController::errorCode() const
{
  return _errorCode;
}
