#include "BatterySettingsService.h"

#ifdef EPEVER_SIMULATION
#include "../simulation/SimulationDataSource.h"
#endif

namespace
{
constexpr uint16_t LegacySettingsAddress = 0x9000;
constexpr uint8_t LegacySettingsCount = BatterySettingsService::ValueCount;

constexpr uint16_t NcG3BatterySettingsAddress = 0x9000;
constexpr uint8_t NcG3BatterySettingsCount = 3;
constexpr uint16_t NcG3VoltageSettingsAddress = 0x9007;
constexpr uint8_t NcG3VoltageSettingsCount = 12;

constexpr uint16_t VerificationTimeoutMs = 500;
constexpr uint16_t WriteSettleDelayMs = 200;
constexpr uint16_t RetryDelayMs = 250;
}

BatterySettingsService::BatterySettingsService(ModbusMaster &modbus)
    : _modbus(modbus)
{
  static_assert(ValueCount == NcG3BatterySettingsCount + NcG3VoltageSettingsCount,
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
                                     uint16_t *values)
{
#ifdef EPEVER_SIMULATION
  if (_simulation != nullptr)
    return _simulation->readBatterySettings(device, values)
               ? _modbus.ku8MBSuccess
               : _modbus.ku8MBInvalidSlaveID;
#endif
  _modbus.setSlaveId(device);
  if (profile == EpeverProfile::Unknown)
    return _modbus.ku8MBInvalidSlaveID;

  if (!isNcG3Profile(profile))
    return readBlock(LegacySettingsAddress, LegacySettingsCount, values);

  uint8_t status =
      readBlock(NcG3BatterySettingsAddress, NcG3BatterySettingsCount, values);
  if (status != _modbus.ku8MBSuccess)
    return status;
  return readBlock(NcG3VoltageSettingsAddress, NcG3VoltageSettingsCount,
                   values + NcG3BatterySettingsCount);
}

uint8_t BatterySettingsService::write(uint8_t device, EpeverProfile profile,
                                      const uint16_t *values)
{
#ifdef EPEVER_SIMULATION
  if (_simulation != nullptr)
    return _simulation->writeBatterySettings(device, values)
               ? _modbus.ku8MBSuccess
               : _modbus.ku8MBInvalidSlaveID;
#endif
  _modbus.setSlaveId(device);
  if (profile == EpeverProfile::Unknown)
    return _modbus.ku8MBInvalidSlaveID;

  if (!isNcG3Profile(profile))
    return writeBlock(LegacySettingsAddress, LegacySettingsCount, values);

  // The voltage block is written first. The gap at 0x9003..0x9006 must never
  // be included in a legacy-style contiguous NC G3 write.
  uint8_t status =
      writeBlock(NcG3VoltageSettingsAddress, NcG3VoltageSettingsCount,
                 values + NcG3BatterySettingsCount);
  if (status != _modbus.ku8MBSuccess)
    return status;
  return writeBlock(NcG3BatterySettingsAddress, NcG3BatterySettingsCount,
                    values);
}

bool BatterySettingsService::valuesMatch(const uint16_t *expected,
                                         const uint16_t *actual)
{
  for (uint8_t index = 0; index < ValueCount; index++)
  {
    if (expected[index] != actual[index])
      return false;
  }
  return true;
}

BatterySettingsService::VerifyResult BatterySettingsService::writeAndVerify(
    uint8_t device, EpeverProfile profile, const uint16_t *values,
    uint16_t *readBack, uint8_t &writeStatus, uint8_t &readStatus)
{
  uint16_t previousTimeout = _modbus.getResponseTimeout();
  _modbus.setResponseTimeout(VerificationTimeoutMs);

  writeStatus = write(device, profile, values);
  delay(WriteSettleDelayMs);
  readStatus = read(device, profile, readBack);

  if (readStatus == _modbus.ku8MBSuccess &&
      valuesMatch(values, readBack))
  {
    _modbus.setResponseTimeout(previousTimeout);
    return VerifyResult::Verified;
  }

  if (writeStatus == _modbus.ku8MBResponseTimedOut ||
      writeStatus == _modbus.ku8MBInvalidCRC)
  {
    delay(RetryDelayMs);
    writeStatus = write(device, profile, values);
    delay(WriteSettleDelayMs);
    readStatus = read(device, profile, readBack);
  }

  _modbus.setResponseTimeout(previousTimeout);
  if (readStatus != _modbus.ku8MBSuccess)
    return VerifyResult::ReadBackFailed;
  return valuesMatch(values, readBack)
             ? VerifyResult::Verified
             : VerifyResult::ValueMismatch;
}

BatterySettingsService::ValidationError BatterySettingsService::validate(
    const uint16_t *values)
{
  if (!(values[3] > values[4] && values[4] >= values[6] &&
        values[6] >= values[7] && values[7] >= values[8] &&
        values[8] > values[9]))
    return ValidationError::ChargeVoltageOrder;

  if (!(values[11] > values[12] && values[12] > values[13] &&
        values[13] > values[14]))
    return ValidationError::DischargeVoltageOrder;

  if (values[3] <= values[5] || values[10] <= values[13])
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
