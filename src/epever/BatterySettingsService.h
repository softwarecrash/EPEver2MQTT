#pragma once

#include <Arduino.h>
#include <ModbusMaster.h>

#include "ControllerTypes.h"
#include "EpeverData.h"

#ifdef EPEVER_SIMULATION
class SimulationDataSource;
#endif

class BatterySettingsService
{
public:
  static constexpr uint8_t ValueCount = 15;

  enum class VerifyResult : uint8_t
  {
    Verified,
    ReadBackFailed,
    ValueMismatch
  };

  enum class ValidationError : uint8_t
  {
    None,
    ChargeVoltageOrder,
    DischargeVoltageOrder,
    RecoveryVoltageOrder
  };

  explicit BatterySettingsService(ModbusMaster &modbus);
#ifdef EPEVER_SIMULATION
  BatterySettingsService(ModbusMaster &modbus, SimulationDataSource &simulation);
#endif

  uint8_t read(uint8_t device, EpeverProfile profile,
               BatterySettingRegisters &settings);

  VerifyResult writeAndVerify(uint8_t device, EpeverProfile profile,
                              const BatterySettingRegisters &settings,
                              BatterySettingRegisters &readBack,
                              uint8_t &writeStatus, uint8_t &readStatus);

  static ValidationError validate(
      const BatterySettingRegisters &settings);
  static bool compatible(EpeverProfile source, EpeverProfile target);

private:
  uint8_t readBlock(uint16_t address, uint8_t count, uint16_t *values);
  uint8_t writeBlock(uint16_t address, uint8_t count, const uint16_t *values);
  uint8_t write(uint8_t device, EpeverProfile profile,
                const BatterySettingRegisters &settings);
  static bool valuesMatch(const BatterySettingRegisters &expected,
                          const BatterySettingRegisters &actual);

  ModbusMaster &_modbus;
#ifdef EPEVER_SIMULATION
  SimulationDataSource *_simulation = nullptr;
#endif
};
