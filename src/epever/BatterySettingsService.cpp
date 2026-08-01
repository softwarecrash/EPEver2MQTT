#include "BatterySettingsService.h"

#include "LegacyRegisters.h"
#include "NcG3Registers.h"

#ifdef EPEVER_SIMULATION
#include "../simulation/SimulationDataSource.h"
#endif

namespace
{
constexpr uint16_t VerificationTimeoutMs = 500;
constexpr uint16_t WriteSettleDelayMs = 200;
constexpr uint16_t RetryDelayMs = 250;
}

BatterySettingsService::BatterySettingsService(ModbusMaster &modbus)
    : _modbus(modbus)
{
  static_assert(ValueCount ==
                    NcG3Registers::BatterySettingsCount +
                        NcG3Registers::VoltageSettingsCount,
                "NC G3 logical settings mapping must contain 15 values");
}

#ifdef EPEVER_SIMULATION
BatterySettingsService::BatterySettingsService(
    ModbusMaster &modbus, SimulationDataSource &simulation)
    : _modbus(modbus), _simulation(&simulation)
{
}
#endif

uint8_t BatterySettingsService::readBlock(uint16_t address, uint8_t count,
                                          uint16_t *values)
{
  _modbus.clearResponseBuffer();
  uint8_t status = _modbus.readHoldingRegisters(address, count);
  if (status == _modbus.ku8MBSuccess)
  {
    for (uint8_t index = 0; index < count; index++)
      values[index] = _modbus.getResponseBuffer(index);
  }
  return status;
}

uint8_t BatterySettingsService::writeBlock(uint16_t address, uint8_t count,
                                           const uint16_t *values)
{
  _modbus.clearTransmitBuffer();
  for (uint8_t index = 0; index < count; index++)
    _modbus.setTransmitBuffer(index, values[index]);
  return _modbus.writeMultipleRegisters(address, count);
}

uint8_t BatterySettingsService::read(uint8_t device, EpeverProfile profile,
                                     BatterySettingRegisters &settings)
{
#ifdef EPEVER_SIMULATION
  if (_simulation != nullptr)
    return _simulation->readBatterySettings(device, settings.words)
               ? _modbus.ku8MBSuccess
               : _modbus.ku8MBInvalidSlaveID;
#endif
  _modbus.setSlaveId(device);
  if (profile == EpeverProfile::Unknown)
    return _modbus.ku8MBInvalidSlaveID;

  if (!isNcG3Profile(profile))
    return readBlock(LegacyRegisters::DeviceSettings,
                     LegacyRegisters::DeviceSettingsCount,
                     settings.words);

  uint8_t status =
      readBlock(NcG3Registers::BatterySettings,
                NcG3Registers::BatterySettingsCount,
                settings.words);
  if (status != _modbus.ku8MBSuccess)
    return status;
  return readBlock(
      NcG3Registers::VoltageSettings, NcG3Registers::VoltageSettingsCount,
      settings.words + NcG3Registers::BatterySettingsCount);
}

uint8_t BatterySettingsService::write(uint8_t device, EpeverProfile profile,
                                      const BatterySettingRegisters &settings)
{
#ifdef EPEVER_SIMULATION
  if (_simulation != nullptr)
    return _simulation->writeBatterySettings(device, settings.words)
               ? _modbus.ku8MBSuccess
               : _modbus.ku8MBInvalidSlaveID;
#endif
  _modbus.setSlaveId(device);
  if (profile == EpeverProfile::Unknown)
    return _modbus.ku8MBInvalidSlaveID;

  if (!isNcG3Profile(profile))
    return writeBlock(LegacyRegisters::DeviceSettings,
                      LegacyRegisters::DeviceSettingsCount,
                      settings.words);

  // The voltage block is written first. The gap at 0x9003..0x9006 must never
  // be included in a legacy-style contiguous NC G3 write.
  uint8_t status =
      writeBlock(
          NcG3Registers::VoltageSettings,
          NcG3Registers::VoltageSettingsCount,
          settings.words + NcG3Registers::BatterySettingsCount);
  if (status != _modbus.ku8MBSuccess)
    return status;
  return writeBlock(NcG3Registers::BatterySettings,
                    NcG3Registers::BatterySettingsCount,
                    settings.words);
}

bool BatterySettingsService::valuesMatch(
    const BatterySettingRegisters &expected,
    const BatterySettingRegisters &actual)
{
  for (uint8_t index = 0; index < ValueCount; index++)
  {
    if (expected.words[index] != actual.words[index])
      return false;
  }
  return true;
}

BatterySettingsService::VerifyResult BatterySettingsService::writeAndVerify(
    uint8_t device, EpeverProfile profile,
    const BatterySettingRegisters &settings,
    BatterySettingRegisters &readBack, uint8_t &writeStatus,
    uint8_t &readStatus)
{
  uint16_t previousTimeout = _modbus.getResponseTimeout();
  _modbus.setResponseTimeout(VerificationTimeoutMs);

  writeStatus = write(device, profile, settings);
  delay(WriteSettleDelayMs);
  readStatus = read(device, profile, readBack);

  if (readStatus == _modbus.ku8MBSuccess &&
      valuesMatch(settings, readBack))
  {
    _modbus.setResponseTimeout(previousTimeout);
    return VerifyResult::Verified;
  }

  if (writeStatus == _modbus.ku8MBResponseTimedOut ||
      writeStatus == _modbus.ku8MBInvalidCRC)
  {
    delay(RetryDelayMs);
    writeStatus = write(device, profile, settings);
    delay(WriteSettleDelayMs);
    readStatus = read(device, profile, readBack);
  }

  _modbus.setResponseTimeout(previousTimeout);
  if (readStatus != _modbus.ku8MBSuccess)
    return VerifyResult::ReadBackFailed;
  return valuesMatch(settings, readBack)
             ? VerifyResult::Verified
             : VerifyResult::ValueMismatch;
}

BatterySettingsService::ValidationError BatterySettingsService::validate(
    const BatterySettingRegisters &settings)
{
  const auto &value = settings.value;
  if (!(value.highVoltageDisconnect > value.chargingLimitVoltage &&
        value.chargingLimitVoltage >= value.equalizationVoltage &&
        value.equalizationVoltage >= value.boostVoltage &&
        value.boostVoltage >= value.floatVoltage &&
        value.floatVoltage > value.boostReconnectVoltage))
    return ValidationError::ChargeVoltageOrder;

  if (!(value.underVoltageRecover > value.underVoltageWarning &&
        value.underVoltageWarning > value.lowVoltageDisconnect &&
        value.lowVoltageDisconnect > value.dischargingLimitVoltage))
    return ValidationError::DischargeVoltageOrder;

  if (value.highVoltageDisconnect <= value.overVoltageReconnect ||
      value.lowVoltageReconnect <= value.lowVoltageDisconnect)
    return ValidationError::RecoveryVoltageOrder;

  return ValidationError::None;
}

bool BatterySettingsService::compatible(EpeverProfile source,
                                        EpeverProfile target)
{
  return (source == EpeverProfile::Legacy &&
          target == EpeverProfile::Legacy) ||
         (isNcG3Profile(source) && isNcG3Profile(target));
}
