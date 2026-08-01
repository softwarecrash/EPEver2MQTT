#include "EpeverController.h"

#include "../app/DiagnosticLog.h"
#include "NcG3Registers.h"

bool EpeverController::readNcG3(uint8_t device, EpeverProfile profile)
{
  ncG3 = {};
  rtc = {};
  _unixTime.setDateTime(0, 0, 0, 0, 0, 0);
  loadState = false;

  uint16_t values[20] = {};
  ncG3.modelId = deviceModelIds[device];

  // Some firmware revisions expose only part of the rated-data range.
  if (readInputBlock(NcG3Registers::PvMaximumVoltage, 1, values))
    ncG3.pvMaxVoltage = values[0];
  if (readInputBlock(NcG3Registers::ChargeRatings, 4, values))
  {
    ncG3.ratedChargePower = wordsToUint32(values[0], values[1]);
    ncG3.ratedBatteryVoltage = values[2];
    ncG3.ratedChargeCurrent = values[3];
    deviceRatedChargeCurrent[device] = values[3];
  }
  if (profile == EpeverProfile::ItNcG3 &&
      readInputBlock(NcG3Registers::RatedLoadCurrent, 1, values))
    ncG3.ratedLoadCurrent = values[0];
  if (readInputBlock(NcG3Registers::DspFirmwareAndPvCount, 2, values))
  {
    ncG3.dspFirmware = values[0];
    ncG3.pvCount = values[1];
  }
  if (readInputBlock(NcG3Registers::ArmFirmware, 1, values))
    ncG3.armFirmware = values[0];
  if (ncG3.pvCount < 1 || ncG3.pvCount > 2)
    ncG3.pvCount = 1;

  if (!readInputBlock(NcG3Registers::Pv1Live, 4, values))
    goto read_failed;
  ncG3.pv1Voltage = values[0];
  ncG3.pv1Current = values[1];
  ncG3.pv1Power = wordsToUint32(values[2], values[3]);

  if (ncG3.pvCount > 1 &&
      readInputBlock(NcG3Registers::Pv2Live, 4, values))
  {
    ncG3.pv2Voltage = values[0];
    ncG3.pv2Current = values[1];
    ncG3.pv2Power = wordsToUint32(values[2], values[3]);
  }

  if (profile == EpeverProfile::ItNcG3)
  {
    if (!readInputBlock(NcG3Registers::LoadLive, 4, values))
      goto read_failed;
    ncG3.loadVoltage = values[0];
    ncG3.loadCurrent = values[1];
    ncG3.loadPower = wordsToUint32(values[2], values[3]);
  }

  if (!readInputBlock(NcG3Registers::BatteryVoltage, 1, values))
    goto read_failed;
  ncG3.batteryVoltage = values[0];
  if (!readInputBlock(NcG3Registers::BatteryLive, 4, values))
    goto read_failed;
  ncG3.batteryCurrent = static_cast<int16_t>(values[0]);
  ncG3.batteryTemperature = static_cast<int16_t>(values[1]);
  ncG3.batterySoc = values[2];
  ncG3.deviceTemperature = static_cast<int16_t>(values[3]);

  if (!readInputBlock(NcG3Registers::PvTotals, 5, values))
    goto read_failed;
  ncG3.systemVoltage = values[0];
  ncG3.highestPvVoltage = values[1];
  ncG3.totalPvCurrent = values[2];
  ncG3.totalPvPower = wordsToUint32(values[3], values[4]);

  if (!readInputBlock(NcG3Registers::Status, 4, values))
    goto read_failed;
  for (uint8_t index = 0; index < 4; index++)
    ncG3.status[index] = values[index];
  if (readInputBlock(NcG3Registers::BmsStatus, 1, values))
    ncG3.status[5] = values[0];

  if (!readInputBlock(NcG3Registers::BatteryStatistics, 2, values))
    goto read_failed;
  ncG3.batteryMaxToday = values[0];
  ncG3.batteryMinToday = values[1];

  if (profile == EpeverProfile::ItNcG3)
  {
    if (!readInputBlock(NcG3Registers::ItEnergyStatistics, 16, values))
      goto read_failed;
    ncG3.consumedDay = wordsToUint32(values[0], values[1]);
    ncG3.consumedMonth = wordsToUint32(values[2], values[3]);
    ncG3.consumedYear = wordsToUint32(values[4], values[5]);
    ncG3.consumedTotal = wordsToUint32(values[6], values[7]);
    ncG3.generatedDay = wordsToUint32(values[8], values[9]);
    ncG3.generatedMonth = wordsToUint32(values[10], values[11]);
    ncG3.generatedYear = wordsToUint32(values[12], values[13]);
    ncG3.generatedTotal = wordsToUint32(values[14], values[15]);
  }
  else
  {
    if (!readInputBlock(NcG3Registers::EtEnergyStatistics, 8, values))
      goto read_failed;
    ncG3.generatedDay = wordsToUint32(values[0], values[1]);
    ncG3.generatedMonth = wordsToUint32(values[2], values[3]);
    ncG3.generatedYear = wordsToUint32(values[4], values[5]);
    ncG3.generatedTotal = wordsToUint32(values[6], values[7]);
  }

  if (!readHoldingBlock(NcG3Registers::BatterySettings, 3, values))
    goto read_failed;
  ncG3.batteryType = values[0];
  ncG3.batteryCapacity = values[1];
  ncG3.temperatureCompensation = values[2];

  if (!readHoldingBlock(NcG3Registers::VoltageSettings, 13, values))
    goto read_failed;
  ncG3.highVoltageDisconnect = values[0];
  ncG3.chargingLimitVoltage = values[1];
  ncG3.overVoltageReconnect = values[2];
  ncG3.equalizationVoltage = values[3];
  ncG3.boostVoltage = values[4];
  ncG3.floatVoltage = values[5];
  ncG3.boostReconnectVoltage = values[6];
  ncG3.lowVoltageReconnect = values[7];
  ncG3.underVoltageRecover = values[8];
  ncG3.underVoltageWarning = values[9];
  ncG3.lowVoltageDisconnect = values[10];
  ncG3.dischargingLimitVoltage = values[11];
  ncG3.chargingCurrentLimit = values[12];

  // These blocks are optional on some NC-G3 firmware versions.
  if (readHoldingBlock(NcG3Registers::OptionalSettingsAndRtc, 9, values))
  {
    ncG3.equalizationTime = values[0];
    ncG3.boostTime = values[1];
    ncG3.lithiumProtection = values[2];
    ncG3.lowTemperatureChargeLimit = static_cast<int16_t>(values[3]);
    ncG3.lowTemperatureDischargeLimit = static_cast<int16_t>(values[4]);
    rtc.words[0] = values[5];
    rtc.words[1] = values[6];
    rtc.words[2] = values[7];
    ncG3.maximumBatteryTemperature = static_cast<int16_t>(values[8]);
    _unixTime.setDateTime(
        2000 + rtc.value.year, rtc.value.month, rtc.value.day,
        rtc.value.hour, rtc.value.minute, rtc.value.second);
  }
  if (readHoldingBlock(NcG3Registers::TemperatureLimits, 3, values))
  {
    ncG3.minimumBatteryTemperature = static_cast<int16_t>(values[0]);
    ncG3.maximumDeviceTemperature = static_cast<int16_t>(values[1]);
    ncG3.deviceTemperatureRecover = static_cast<int16_t>(values[2]);
  }
  if (readHoldingBlock(NcG3Registers::OperatingSettings, 16, values))
  {
    ncG3.chargingMode = values[0];
    ncG3.fullSoc = values[1];
    ncG3.fullSocRecover = values[2];
    ncG3.dischargeRecoverSoc = values[3];
    ncG3.lowPowerRecoverSoc = values[4];
    ncG3.lowPowerAlarmSoc = values[5];
    ncG3.dischargeSoc = values[6];
    ncG3.recordPeriod = values[7];
    ncG3.bmsProtocol = values[8];
    ncG3.bmsEnabled = values[9];
    ncG3.pvInputMode = values[10];
    ncG3.modbusAddress = values[13];
    ncG3.baudRateCode = values[14];
    ncG3.parallelChargeCurrentLimit = values[15];
  }

  if (profile == EpeverProfile::ItNcG3 &&
      readInputBlock(NcG3Registers::BmsTelemetry, 10, values))
  {
    ncG3.bmsCellCount = values[0];
    ncG3.bmsPackVoltage = values[1];
    ncG3.bmsCurrent = static_cast<int16_t>(values[2]);
    ncG3.bmsFullCapacity = values[5];
    ncG3.bmsRemainingCapacity = values[6];
    ncG3.bmsRemainingMinutes = values[7];
    ncG3.bmsMaximumCellTemperature = static_cast<int16_t>(values[8]);
    ncG3.bmsMinimumCellTemperature = static_cast<int16_t>(values[9]);
    ncG3.bmsDataValid = true;
  }

  if (profile == EpeverProfile::ItNcG3)
  {
    if (!readCoil(NcG3Registers::LoadState, loadState))
      loadState = (ncG3.status[1] & 0x0001) != 0;
  }

  _errorCode = 0;
  synchronizeDeviceClock(device);
  DiagnosticLog::printf("[EPEVER:%u] NC-G3 poll successful", device);
  return true;

read_failed:
  _errorCode = result;
  DiagnosticLog::printf(
      "[EPEVER:%u] NC-G3 register read failed (code %u)",
      device, result);
  return false;
}
