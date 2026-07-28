#pragma once

#include <Arduino.h>

union RtcRegisters
{
  struct
  {
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint8_t year;
  } value;
  uint16_t words[3];
};

union LegacyLiveRegisters
{
  struct
  {
    int16_t pvVoltage;
    int16_t pvCurrent;
    int32_t pvPower;
    int16_t batteryVoltage;
    int16_t batteryCurrent;
    int32_t batteryPower;
    uint16_t unused[4];
    int16_t loadVoltage;
    int16_t loadCurrent;
    int32_t loadPower;
  } value;
  uint16_t words[16];
};

union LegacyStatisticsRegisters
{
  struct
  {
    uint16_t pvMaximum;
    uint16_t pvMinimum;
    uint16_t batteryMaximum;
    uint16_t batteryMinimum;
    uint32_t consumedDay;
    uint32_t consumedMonth;
    uint32_t consumedYear;
    uint32_t consumedTotal;
    uint32_t generatedDay;
    uint32_t generatedMonth;
    uint32_t generatedYear;
    uint32_t generatedTotal;
  } value;
  uint16_t words[20];
};

union BatterySettingRegisters
{
  struct
  {
    uint16_t batteryType;
    uint16_t batteryCapacity;
    uint16_t temperatureCompensation;
    uint16_t highVoltageDisconnect;
    uint16_t chargingLimitVoltage;
    uint16_t overVoltageReconnect;
    uint16_t equalizationVoltage;
    uint16_t boostVoltage;
    uint16_t floatVoltage;
    uint16_t boostReconnectVoltage;
    uint16_t lowVoltageReconnect;
    uint16_t underVoltageRecover;
    uint16_t underVoltageWarning;
    uint16_t lowVoltageDisconnect;
    uint16_t dischargingLimitVoltage;
  } value;
  uint16_t words[15];
};

static_assert(sizeof(RtcRegisters) == 3 * sizeof(uint16_t),
              "RTC register layout must match Modbus words");
static_assert(sizeof(LegacyLiveRegisters) == 16 * sizeof(uint16_t),
              "Legacy live-data layout must match Modbus words");
static_assert(sizeof(LegacyStatisticsRegisters) == 20 * sizeof(uint16_t),
              "Legacy statistics layout must match Modbus words");
static_assert(sizeof(BatterySettingRegisters) == 15 * sizeof(uint16_t),
              "Battery setting layout must match Modbus words");

struct LegacyBatteryStatus
{
  uint8_t voltage = 0;
  uint8_t temperature = 0;
  bool resistanceAbnormal = false;
  bool ratedVoltageInvalid = false;
};

struct NcG3Data
{
  uint16_t modelId = 0;
  uint16_t pvCount = 0;
  uint16_t pvMaxVoltage = 0;
  uint32_t ratedChargePower = 0;
  uint16_t ratedBatteryVoltage = 0;
  uint16_t ratedChargeCurrent = 0;
  uint16_t ratedLoadCurrent = 0;
  uint16_t dspFirmware = 0;
  uint16_t armFirmware = 0;

  uint16_t pv1Voltage = 0;
  uint16_t pv1Current = 0;
  uint32_t pv1Power = 0;
  uint16_t pv2Voltage = 0;
  uint16_t pv2Current = 0;
  uint32_t pv2Power = 0;
  uint16_t loadVoltage = 0;
  uint16_t loadCurrent = 0;
  uint32_t loadPower = 0;
  uint16_t batteryVoltage = 0;
  int16_t batteryCurrent = 0;
  int16_t batteryTemperature = 0;
  uint16_t batterySoc = 0;
  int16_t deviceTemperature = 0;
  uint16_t systemVoltage = 0;
  uint16_t highestPvVoltage = 0;
  uint16_t totalPvCurrent = 0;
  uint32_t totalPvPower = 0;

  uint16_t status[6] = {};
  uint16_t batteryMaxToday = 0;
  uint16_t batteryMinToday = 0;
  uint32_t consumedDay = 0;
  uint32_t consumedMonth = 0;
  uint32_t consumedYear = 0;
  uint32_t consumedTotal = 0;
  uint32_t generatedDay = 0;
  uint32_t generatedMonth = 0;
  uint32_t generatedYear = 0;
  uint32_t generatedTotal = 0;

  uint16_t batteryType = 0;
  uint16_t batteryCapacity = 0;
  uint16_t temperatureCompensation = 0;
  uint16_t highVoltageDisconnect = 0;
  uint16_t chargingLimitVoltage = 0;
  uint16_t overVoltageReconnect = 0;
  uint16_t equalizationVoltage = 0;
  uint16_t boostVoltage = 0;
  uint16_t floatVoltage = 0;
  uint16_t boostReconnectVoltage = 0;
  uint16_t lowVoltageReconnect = 0;
  uint16_t underVoltageRecover = 0;
  uint16_t underVoltageWarning = 0;
  uint16_t lowVoltageDisconnect = 0;
  uint16_t dischargingLimitVoltage = 0;
  uint16_t chargingCurrentLimit = 0;
  uint16_t equalizationTime = 0;
  uint16_t boostTime = 0;
  uint16_t lithiumProtection = 0;
  int16_t lowTemperatureChargeLimit = 0;
  int16_t lowTemperatureDischargeLimit = 0;
  int16_t maximumBatteryTemperature = 0;
  int16_t minimumBatteryTemperature = 0;
  int16_t maximumDeviceTemperature = 0;
  int16_t deviceTemperatureRecover = 0;
  uint16_t chargingMode = 0;
  uint16_t fullSoc = 0;
  uint16_t fullSocRecover = 0;
  uint16_t dischargeRecoverSoc = 0;
  uint16_t lowPowerRecoverSoc = 0;
  uint16_t lowPowerAlarmSoc = 0;
  uint16_t dischargeSoc = 0;
  uint16_t recordPeriod = 0;
  uint16_t bmsProtocol = 0;
  uint16_t bmsEnabled = 0;
  uint16_t pvInputMode = 0;
  uint16_t modbusAddress = 0;
  uint16_t baudRateCode = 0;
  uint16_t parallelChargeCurrentLimit = 0;

  uint16_t bmsCellCount = 0;
  uint16_t bmsPackVoltage = 0;
  int16_t bmsCurrent = 0;
  uint16_t bmsFullCapacity = 0;
  uint16_t bmsRemainingCapacity = 0;
  uint16_t bmsRemainingMinutes = 0;
  int16_t bmsMaximumCellTemperature = 0;
  int16_t bmsMinimumCellTemperature = 0;
  bool bmsDataValid = false;
};
